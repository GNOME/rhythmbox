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

#include "rb-library-watcher-thread.h"

static void rb_library_watcher_thread_class_init (RBLibraryWatcherThreadClass *klass);
static void rb_library_watcher_thread_init (RBLibraryWatcherThread *thread);
static void rb_library_watcher_thread_finalize (GObject *object);
static void rb_library_watcher_thread_set_property (GObject *object,
                                                    guint prop_id,
                                                    const GValue *value,
                                                    GParamSpec *pspec);
static void rb_library_watcher_thread_get_property (GObject *object,
                                                    guint prop_id,
                                                    GValue *value,
                                                    GParamSpec *pspec);
static gpointer thread_main (RBLibraryWatcherThreadPrivate *priv);

struct RBLibraryWatcherThreadPrivate
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
rb_library_watcher_thread_get_type (void)
{
	static GType rb_library_watcher_thread_type = 0;

	if (rb_library_watcher_thread_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryWatcherThreadClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_watcher_thread_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryWatcherThread),
			0,
			(GInstanceInitFunc) rb_library_watcher_thread_init
		};

		rb_library_watcher_thread_type = g_type_register_static (G_TYPE_OBJECT,
						                         "RBLibraryWatcherThread",
						                         &our_info, 0);
	}

	return rb_library_watcher_thread_type;
}

static void
rb_library_watcher_thread_class_init (RBLibraryWatcherThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_watcher_thread_finalize;

	object_class->set_property = rb_library_watcher_thread_set_property;
        object_class->get_property = rb_library_watcher_thread_get_property;
		                        
        g_object_class_install_property (object_class,
                                         PROP_LIBRARY,
                                         g_param_spec_object ("library",
                                                              "Library object",
                                                              "Library object",
                                                              RB_TYPE_LIBRARY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_library_watcher_thread_init (RBLibraryWatcherThread *thread)
{
	thread->priv = g_new0 (RBLibraryWatcherThreadPrivate, 1);

	thread->priv->lock = g_mutex_new ();
}

static void
rb_library_watcher_thread_finalize (GObject *object)
{
	RBLibraryWatcherThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_WATCHER_THREAD (object));

	thread = RB_LIBRARY_WATCHER_THREAD (object);

	g_return_if_fail (thread->priv != NULL);

	g_mutex_lock (thread->priv->lock);
	thread->priv->dead = TRUE;
	g_mutex_unlock (thread->priv->lock);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryWatcherThread *
rb_library_watcher_thread_new (RBLibrary *library)
{
	RBLibraryWatcherThread *library_watcher_thread;

	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	library_watcher_thread = RB_LIBRARY_WATCHER_THREAD (g_object_new (RB_TYPE_LIBRARY_WATCHER_THREAD,
								    "library", library,
								    NULL));

	g_return_val_if_fail (library_watcher_thread->priv != NULL, NULL);

	return library_watcher_thread;
}

static void
rb_library_watcher_thread_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
	RBLibraryWatcherThread *thread = RB_LIBRARY_WATCHER_THREAD (object);

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
rb_library_watcher_thread_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
	RBLibraryWatcherThread *thread = RB_LIBRARY_WATCHER_THREAD (object);

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
thread_main (RBLibraryWatcherThreadPrivate *priv)
{
	while (TRUE)
	{
		g_mutex_lock (priv->lock);
		
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			g_free (priv);
			g_thread_exit (NULL);
		}

		g_mutex_unlock (priv->lock);

		g_usleep (10);
	}

	return NULL;
}
