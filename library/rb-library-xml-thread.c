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

#include "rb-library-xml-thread.h"

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
	
		thread->priv->thread = g_thread_create ((GThreadFunc) thread_main,
							thread->priv, TRUE, NULL);
		break;
	case PROP_FILE_NAME:
		if (thread->priv->filename != NULL)
			g_free (thread->priv->filename);
		thread->priv->filename = g_strdup (g_value_get_string (value));
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
	while (TRUE)
	{
		g_mutex_lock (priv->lock);
		
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			g_free (priv);
			priv->object->priv = NULL;
			g_thread_exit (NULL);
		}

		g_mutex_unlock (priv->lock);

		done_loading (priv);

		g_usleep (10);
	}

	return NULL;
}

static void
done_loading (RBLibraryXMLThreadPrivate *priv)
{
	g_timeout_add (0, (GSourceFunc) done_loading_timeout_cb, priv->object);

	priv->dead = TRUE;
}

static gboolean
done_loading_timeout_cb (RBLibraryXMLThread *thread)
{
	g_signal_emit (G_OBJECT (thread), rb_library_xml_thread_signals[DONE_LOADING], 0);

	return FALSE;
}
