/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-library-walker-thread.h"
#include "rb-library-action.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"

static void rb_library_walker_thread_class_init (RBLibraryWalkerThreadClass *klass);
static void rb_library_walker_thread_init (RBLibraryWalkerThread *thread);
static void rb_library_walker_thread_finalize (GObject *object);
static void rb_library_walker_thread_set_property (GObject *object,
                                                   guint prop_id,
                                                   const GValue *value,
                                                   GParamSpec *pspec);
static void rb_library_walker_thread_get_property (GObject *object,
                                                   guint prop_id,
                                                   GValue *value,
                                                   GParamSpec *pspec);
static gpointer thread_main (RBLibraryWalkerThread *thread);

struct RBLibraryWalkerThreadPrivate
{
	RBLibrary *library;
	char *uri;

	RBLibraryAction *action;

	GThread *thread;
	GMutex *lock;
	gboolean dead;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
	PROP_URI
};

enum
{
	DONE,
	LAST_SIGNAL,
};

static guint rb_library_walker_thread_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
rb_library_walker_thread_get_type (void)
{
	static GType rb_library_walker_thread_type = 0;

	if (rb_library_walker_thread_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryWalkerThreadClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_walker_thread_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryWalkerThread),
			0,
			(GInstanceInitFunc) rb_library_walker_thread_init
		};

		rb_library_walker_thread_type = g_type_register_static (G_TYPE_OBJECT,
						                        "RBLibraryWalkerThread",
						                        &our_info, 0);
	}

	return rb_library_walker_thread_type;
}

static void
rb_library_walker_thread_class_init (RBLibraryWalkerThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_walker_thread_finalize;

	object_class->set_property = rb_library_walker_thread_set_property;
        object_class->get_property = rb_library_walker_thread_get_property;
		                        
        g_object_class_install_property (object_class,
                                         PROP_LIBRARY,
                                         g_param_spec_object ("library",
                                                              "Library object",
                                                              "Library object",
                                                              RB_TYPE_LIBRARY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_URI,
                                         g_param_spec_string ("uri",
                                                              "uri",
							      "uri",
							      "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_library_walker_thread_signals[DONE] =
		g_signal_new ("done",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryWalkerThreadClass, done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_library_walker_thread_init (RBLibraryWalkerThread *thread)
{
	thread->priv = g_new0 (RBLibraryWalkerThreadPrivate, 1);

	thread->priv->lock = g_mutex_new ();
}

static void
rb_library_walker_thread_finalize (GObject *object)
{
	RBLibraryWalkerThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_WALKER_THREAD (object));

	thread = RB_LIBRARY_WALKER_THREAD (object);

	g_return_if_fail (thread->priv != NULL);

	g_mutex_free (thread->priv->lock);
	g_free (thread->priv->uri);

	g_free (thread->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryWalkerThread *
rb_library_walker_thread_new (RBLibrary *library, const char *uri)
{
	RBLibraryWalkerThread *library_walker_thread;

	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	library_walker_thread = RB_LIBRARY_WALKER_THREAD (g_object_new (RB_TYPE_LIBRARY_WALKER_THREAD,
								        "library", library,
									"uri", uri,
								        NULL));

	g_return_val_if_fail (library_walker_thread->priv != NULL, NULL);

	return library_walker_thread;
}

static void
rb_library_walker_thread_set_property (GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
	RBLibraryWalkerThread *thread = RB_LIBRARY_WALKER_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		thread->priv->library = g_value_get_object (value);                    
	
		break;
	case PROP_URI:
		thread->priv->uri = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_walker_thread_get_property (GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
	RBLibraryWalkerThread *thread = RB_LIBRARY_WALKER_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, thread->priv->library);
		break;
	case PROP_URI:
		g_value_set_string (value, thread->priv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_library_walker_thread_start (RBLibraryWalkerThread *thread)
{
	thread->priv->thread = g_thread_create ((GThreadFunc) thread_main,
						thread, TRUE, NULL);
}

void
rb_library_walker_thread_kill (RBLibraryWalkerThread *thread)
{
	g_mutex_lock (thread->priv->lock);
	thread->priv->dead = TRUE;
	g_mutex_unlock (thread->priv->lock);
}

static void
add_file (const char *filename,
	  RBLibraryWalkerThread *thread)
{
	RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_ADD_FILE, filename);
	
	g_async_queue_push (rb_library_get_main_queue (thread->priv->library), action);
}

static gpointer
thread_main (RBLibraryWalkerThread *thread)
{
	g_async_queue_ref (rb_library_get_main_queue (thread->priv->library));

	rb_uri_handle_recursively (thread->priv->uri, (GFunc) add_file, thread->priv->lock, &thread->priv->dead, thread);
	if (!thread->priv->dead)
		g_signal_emit (G_OBJECT (thread), rb_library_walker_thread_signals[DONE], 0);

	g_async_queue_unref (rb_library_get_main_queue (thread->priv->library));

	g_object_unref (G_OBJECT (thread));
	g_thread_exit (NULL);

	return NULL;
}
