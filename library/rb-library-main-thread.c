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

#include "rb-library-main-thread.h"
#include "rb-node-song.h"
#include "rb-file-helpers.h"
#include "rb-file-monitor.h"
#include "rb-debug.h"

static void rb_library_main_thread_class_init (RBLibraryMainThreadClass *klass);
static void rb_library_main_thread_init (RBLibraryMainThread *thread);
static void rb_library_main_thread_finalize (GObject *object);
static void rb_library_main_thread_set_property (GObject *object,
                                                 guint prop_id,
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void rb_library_main_thread_get_property (GObject *object,
                                                 guint prop_id,
                                                 GValue *value,
                                                 GParamSpec *pspec);
static gpointer thread_main (RBLibraryMainThreadPrivate *priv);

struct RBLibraryMainThreadPrivate
{
	RBLibrary *library;

	GThread *thread;
	GMutex *lock;
	gboolean dead;
};

enum
{
	PROP_0,
	PROP_LIBRARY
};

static GObjectClass *parent_class = NULL;

GType
rb_library_main_thread_get_type (void)
{
	static GType rb_library_main_thread_type = 0;

	if (rb_library_main_thread_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryMainThreadClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_main_thread_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryMainThread),
			0,
			(GInstanceInitFunc) rb_library_main_thread_init
		};

		rb_library_main_thread_type = g_type_register_static (G_TYPE_OBJECT,
						                      "RBLibraryMainThread",
						                      &our_info, 0);
	}

	return rb_library_main_thread_type;
}

static void
rb_library_main_thread_class_init (RBLibraryMainThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_main_thread_finalize;

	object_class->set_property = rb_library_main_thread_set_property;
        object_class->get_property = rb_library_main_thread_get_property;
		                        
        g_object_class_install_property (object_class,
                                         PROP_LIBRARY,
                                         g_param_spec_object ("library",
                                                              "Library object",
                                                              "Library object",
                                                              RB_TYPE_LIBRARY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
file_changed_cb (RBFileMonitor *monitor,
		 const char *uri,
		 RBLibraryMainThread *thread)
{
	rb_library_action_queue_add (rb_library_get_main_queue (thread->priv->library),
				     TRUE,
				     RB_LIBRARY_ACTION_UPDATE_FILE,
				     uri);
}

static void
file_removed_cb (RBFileMonitor *monitor,
		 const char *uri,
		 RBLibraryMainThread *thread)
{
	rb_library_action_queue_add (rb_library_get_main_queue (thread->priv->library),
				     TRUE,
				     RB_LIBRARY_ACTION_REMOVE_FILE,
				     uri);
}

static void
rb_library_main_thread_init (RBLibraryMainThread *thread)
{
	thread->priv = g_new0 (RBLibraryMainThreadPrivate, 1);

	thread->priv->lock = g_mutex_new ();

	g_signal_connect (G_OBJECT (rb_file_monitor_get ()),
			  "file_changed",
			  G_CALLBACK (file_changed_cb),
			  thread);
	g_signal_connect (G_OBJECT (rb_file_monitor_get ()),
			  "file_removed",
			  G_CALLBACK (file_removed_cb),
			  thread);
}

static void
rb_library_main_thread_finalize (GObject *object)
{
	RBLibraryMainThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_MAIN_THREAD (object));

	thread = RB_LIBRARY_MAIN_THREAD (object);

	g_return_if_fail (thread->priv != NULL);

	g_mutex_lock (thread->priv->lock);
	thread->priv->dead = TRUE;
	g_mutex_unlock (thread->priv->lock);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryMainThread *
rb_library_main_thread_new (RBLibrary *library)
{
	RBLibraryMainThread *library_main_thread;

	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	library_main_thread = RB_LIBRARY_MAIN_THREAD (g_object_new (RB_TYPE_LIBRARY_MAIN_THREAD,
								    "library", library,
								    NULL));

	g_return_val_if_fail (library_main_thread->priv != NULL, NULL);

	return library_main_thread;
}

static void
rb_library_main_thread_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
	RBLibraryMainThread *thread = RB_LIBRARY_MAIN_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		thread->priv->library = g_value_get_object (value);                    
	
		thread->priv->thread = g_thread_create ((GThreadFunc) thread_main,
							thread->priv, TRUE, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_main_thread_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
	RBLibraryMainThread *thread = RB_LIBRARY_MAIN_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, thread->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gpointer
thread_main (RBLibraryMainThreadPrivate *priv)
{
	while (TRUE)
	{
		RBLibraryActionQueue *queue;
		int i = 0;
		gboolean empty;

		g_mutex_lock (priv->lock);
		
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			g_free (priv);
			g_thread_exit (NULL);
		}

		queue = rb_library_get_main_queue (priv->library);
		while (rb_library_action_queue_is_empty (queue) == FALSE && i <= 10)
		{
			RBLibraryActionType type;
			char *uri, *realuri;

			rb_library_action_queue_peek_head (queue,
							   &type,
							   &uri);

			realuri = rb_uri_resolve_symlink (uri);

			switch (type)
			{
			case RB_LIBRARY_ACTION_ADD_FILE:
				if (rb_library_get_song_by_location (priv->library, realuri) == NULL)
				{
					rb_node_song_new (realuri,
							  priv->library);
				}

				rb_file_monitor_add (rb_file_monitor_get (), realuri);
				break;
			case RB_LIBRARY_ACTION_UPDATE_FILE:
				{
					RBNode *song;

					song = rb_library_get_song_by_location (priv->library, realuri);
					if (song == NULL)
						break;

					if (rb_uri_exists (realuri) == FALSE)
					{
						rb_node_unref (song);
						break;
					}

					rb_node_song_update_if_changed (RB_NODE_SONG (song), priv->library);
				}

				/* just to be sure */
				rb_file_monitor_add (rb_file_monitor_get (), realuri);
				break;
			case RB_LIBRARY_ACTION_REMOVE_FILE:
				{
					RBNode *song;

					song = rb_library_get_song_by_location (priv->library, realuri);
					if (song == NULL)
						break;

					rb_node_unref (song);
				}

				rb_file_monitor_remove (rb_file_monitor_get (), realuri);
				break;
			default:
				break;
			}

			g_free (realuri);

			rb_library_action_queue_pop_head (queue);

			i++;
		}

		empty = rb_library_action_queue_is_empty (queue);

		g_mutex_unlock (priv->lock);

		if (empty == TRUE)
			g_usleep (10);
	}

	return NULL;
}
