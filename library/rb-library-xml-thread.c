/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include <string.h>
#include <unistd.h>

#include "rb-library-xml-thread.h"
#include "rb-node-song.h"
#include "rb-debug.h"

static void rb_library_xml_thread_class_init (RBLibraryXMLThreadClass *klass);
static void rb_library_xml_thread_init (RBLibraryXMLThread *thread);
static void rb_library_xml_thread_finalize (GObject *object);
static void rb_library_xml_thread_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);
static void rb_library_xml_thread_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static gpointer thread_main (RBLibraryXMLThreadPrivate *priv);
static void done_loading (RBLibraryXMLThreadPrivate *priv);
static gboolean done_loading_timeout_cb (RBLibraryXMLThread *thread);

struct RBLibraryXMLThreadPrivate
{
	RBLibrary *library;
	char *filename;

	GThread *thread;
	GMutex *lock;
	gboolean dead;

	RBLibraryXMLThread *object;

	gboolean initialized_file;
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr child;

	gboolean finished_preloading;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
	PROP_FILE_NAME
};

enum
{
	DONE_LOADING,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_library_xml_thread_signals[LAST_SIGNAL] = { 0 };

GType
rb_library_xml_thread_get_type (void)
{
	static GType rb_library_xml_thread_type = 0;

	if (rb_library_xml_thread_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryXMLThreadClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_xml_thread_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryXMLThread),
			0,
			(GInstanceInitFunc) rb_library_xml_thread_init
		};

		rb_library_xml_thread_type = g_type_register_static (G_TYPE_OBJECT,
						                     "RBLibraryXMLThread",
						                     &our_info, 0);
	}

	return rb_library_xml_thread_type;
}

static void
rb_library_xml_thread_class_init (RBLibraryXMLThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_xml_thread_finalize;

	object_class->set_property = rb_library_xml_thread_set_property;
        object_class->get_property = rb_library_xml_thread_get_property;
		                        
        g_object_class_install_property (object_class,
                                         PROP_LIBRARY,
                                         g_param_spec_object ("library",
                                                              "Library object",
                                                              "Library object",
                                                              RB_TYPE_LIBRARY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_FILE_NAME,
                                         g_param_spec_string ("filename",
                                                              "XML filename",
                                                              "XML filename",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_library_xml_thread_signals[DONE_LOADING] =
		g_signal_new ("done_loading",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryXMLThreadClass, done_loading),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_library_xml_thread_init (RBLibraryXMLThread *thread)
{
	thread->priv = g_new0 (RBLibraryXMLThreadPrivate, 1);

	thread->priv->lock = g_mutex_new ();

	thread->priv->object = thread;
}

static void
rb_library_xml_thread_finalize (GObject *object)
{
	RBLibraryXMLThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_XML_THREAD (object));

	thread = RB_LIBRARY_XML_THREAD (object);

	if (thread->priv != NULL)
	{
		g_mutex_lock (thread->priv->lock);
		thread->priv->dead = TRUE;
		g_mutex_unlock (thread->priv->lock);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryXMLThread *
rb_library_xml_thread_new (RBLibrary *library,
			   const char *filename)
{
	RBLibraryXMLThread *library_xml_thread;

	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	library_xml_thread = RB_LIBRARY_XML_THREAD (g_object_new (RB_TYPE_LIBRARY_XML_THREAD,
								  "library", library,
								  "filename", filename,
								  NULL));

	g_return_val_if_fail (library_xml_thread->priv != NULL, NULL);

	return library_xml_thread;
}

static void
rb_library_xml_thread_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	RBLibraryXMLThread *thread = RB_LIBRARY_XML_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		thread->priv->library = g_value_get_object (value);                    
		break;
	case PROP_FILE_NAME:
		if (thread->priv->filename != NULL)
			g_free (thread->priv->filename);
		thread->priv->filename = g_strdup (g_value_get_string (value));
	
		if (g_file_test (thread->priv->filename, G_FILE_TEST_EXISTS) == TRUE)
		{
			thread->priv->thread = g_thread_create ((GThreadFunc) thread_main,
								thread->priv, TRUE, NULL);
		}
		else
		{
			done_loading (thread->priv);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_xml_thread_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
	RBLibraryXMLThread *thread = RB_LIBRARY_XML_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, thread->priv->library);
		break;
	case PROP_FILE_NAME:
		g_value_set_string (value, thread->priv->filename);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gpointer
thread_main (RBLibraryXMLThreadPrivate *priv)
{
	int media_files = 0;

	static RBProfiler *p = NULL;

	priv->finished_preloading = FALSE;

	while (TRUE)
	{
		int i = 0;

		g_mutex_lock (priv->lock);
		
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			if (priv->doc != NULL)
				xmlFreeDoc (priv->doc);
			g_free (priv);
			priv->object->priv = NULL;
			rb_profiler_dump (p);
			rb_profiler_free (p);
			g_thread_exit (NULL);
		}

		if (priv->initialized_file == FALSE)
		{
			char *tmp;
			
			priv->doc = xmlParseFile (priv->filename);

			if (priv->doc == NULL)
			{
				done_loading (priv);
			}

			priv->root = xmlDocGetRootElement (priv->doc);

			tmp = xmlGetProp (priv->root, "version");
			if (tmp == NULL || strcmp (tmp, RB_LIBRARY_XML_VERSION) != 0)
			{
				unlink (priv->filename);
				done_loading (priv);
			}
			g_free (tmp);

			priv->initialized_file = TRUE;

			p = rb_profiler_new ("XML loader thread");
		}

		if (priv->child == NULL && priv->dead == FALSE)
			priv->child = priv->root->children;

		for (; priv->child != NULL && i <= 10; priv->child = priv->child->next, i++)
		{
			RBNode *node;

			node = rb_node_new_from_xml (priv->child);
			if (node == NULL)
				continue;

			if (rb_node_has_child (rb_library_get_all_songs (priv->library), node)) {
				const char *location;

				location = rb_node_get_property_string (node,
							                RB_NODE_SONG_PROP_LOCATION);
					
				rb_library_action_queue_add (rb_library_get_main_queue (priv->library),
							     FALSE,
							     RB_LIBRARY_ACTION_UPDATE_FILE,
							     location);

				media_files++;
			}
			else if (rb_node_has_child (rb_library_get_all_artists (priv->library), node))
				rb_node_add_child (node, rb_library_get_all_songs (priv->library));
			else if (rb_node_has_child (rb_library_get_all_genres (priv->library), node))
				rb_node_add_child (node, rb_library_get_all_albums (priv->library));
		}

		/* this value is tunable */
		if (media_files >= 150 && !priv->finished_preloading)
		{
			rb_library_finished_preloading (priv->library);
			priv->finished_preloading = TRUE;
		}

		if (priv->child == NULL && priv->dead == FALSE)
		{
			xmlFreeDoc (priv->doc);
			priv->doc = NULL;

			done_loading (priv);
		}

		g_mutex_unlock (priv->lock);

		g_usleep (10);
	}

	return NULL;
}

static void
done_loading (RBLibraryXMLThreadPrivate *priv)
{
	if (!priv->finished_preloading)
		rb_library_finished_preloading (priv->library);

	g_timeout_add (0, (GSourceFunc) done_loading_timeout_cb, priv->object);

	priv->dead = TRUE;
}

static gboolean
done_loading_timeout_cb (RBLibraryXMLThread *thread)
{
	g_signal_emit (G_OBJECT (thread), rb_library_xml_thread_signals[DONE_LOADING], 0);

	return FALSE;
}
