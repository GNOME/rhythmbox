/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gdk/gdk.h>

#include "rb-library-main-thread.h"
#include "rb-file-helpers.h"
#include "rb-library-action.h"
#include "rb-file-monitor.h"
#include "rb-debug.h"
#include "rb-marshal.h"

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
static gpointer main_thread_main (RBLibraryMainThread *thread);
static gpointer add_thread_main (RBLibraryMainThread *thread);

struct RBLibraryLoadErrorData {
	RBLibraryMainThread *thread;
	char *uri;
	char *msg;
};

struct RBLibraryMainThreadPrivate
{
	RBLibrary *library;

	GThread *main_thread;
	GThread *add_thread;

	GMutex *lock;
	gboolean dead;

	GList *failed_loads;

	GAsyncQueue *main_queue;
	GAsyncQueue *add_queue;
};

enum
{
	PROP_0,
	PROP_LIBRARY
};

enum
{
	ERROR,
	LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;

static guint rb_library_main_thread_signals[LAST_SIGNAL] = { 0 };

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

	rb_library_main_thread_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryMainThreadClass, error),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
}

static void
file_changed_cb (RBFileMonitor *monitor,
		 const char *uri,
		 RBLibraryMainThread *thread)
{
	RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_UPDATE_FILE, uri);
	g_async_queue_push (rb_library_get_add_queue (thread->priv->library), action);
}

static void
file_removed_cb (RBFileMonitor *monitor,
		 const char *uri,
		 RBLibraryMainThread *thread)
{
	RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_REMOVE_FILE, uri);
	g_async_queue_push (rb_library_get_add_queue (thread->priv->library), action);
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

	g_thread_join (thread->priv->main_thread);
	g_thread_join (thread->priv->add_thread);

	g_mutex_free (thread->priv->lock);

	g_free (thread->priv);

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
		
		thread->priv->main_queue = rb_library_get_main_queue (thread->priv->library);
		g_async_queue_ref (thread->priv->main_queue);
		thread->priv->add_queue = rb_library_get_add_queue (thread->priv->library);
		g_async_queue_ref (thread->priv->add_queue);
	
		thread->priv->main_thread = g_thread_create ((GThreadFunc) main_thread_main,
								thread, TRUE, NULL);
		thread->priv->add_thread = g_thread_create ((GThreadFunc) add_thread_main,
							    thread, TRUE, NULL);
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

static gboolean
signal_err_idle (struct RBLibraryLoadErrorData *data)
{
	g_signal_emit (G_OBJECT (data->thread), rb_library_main_thread_signals[ERROR], 0,
		       data->uri, data->msg);
	g_free (data->uri);
	g_free (data->msg);
	g_free (data);

	return FALSE;
}

static void
push_err (RBLibraryMainThread *thread, const char *uri, GError *error)
{
	struct RBLibraryLoadErrorData *loaderr = g_new0 (struct RBLibraryLoadErrorData, 1);

	loaderr->thread = thread;
	loaderr->uri = g_strdup (uri);
	loaderr->msg = g_strdup (error->message);

	g_error_free (error);

	rb_debug ("queueing error for \"%s\": %s", loaderr->uri, loaderr->msg);
	g_idle_add ((GSourceFunc) signal_err_idle, loaderr);
}

static gboolean
am_dead (RBLibraryMainThread *thread)
{
	gboolean ret;
	g_mutex_lock (thread->priv->lock);
	ret = thread->priv->dead;
	g_mutex_unlock (thread->priv->lock);
	return ret;
}

static RBLibraryAction *
read_action (RBLibraryMainThread *thread, GAsyncQueue *queue)
{
	GTimeVal timeout;
	RBLibraryAction *action;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, G_USEC_PER_SEC);
	
	while ((action = g_async_queue_timed_pop (queue, &timeout)) == NULL) {
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC);

		if (am_dead (thread))
			return NULL;
	}

	return action;
}

static gpointer
main_thread_main (RBLibraryMainThread *thread)
{
	while (TRUE)
	{
		RBLibraryAction *action;
		RBLibraryActionType type;
		char *uri, *realuri;
		GError *error = NULL;

		action = read_action (thread, thread->priv->main_queue);

		if (action == NULL)
			break;

		rb_library_action_get (action, &type, &uri);

		realuri = rb_uri_resolve_symlink (uri);

		rb_debug ("popped action from main queue, type: %d uri: %s", type, uri);
		switch (type)
		{
		case RB_LIBRARY_ACTION_UPDATE_FILE:
			rb_library_update_uri (thread->priv->library, realuri, &error);
			break;
		case RB_LIBRARY_ACTION_REMOVE_FILE:
			rb_library_remove_uri (thread->priv->library, realuri);
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		if (error != NULL) {
			push_err (thread, realuri, error);
		}

		g_free (realuri);
		g_object_unref (G_OBJECT (action));
			
		g_usleep (10);
	}

	rb_debug ("exiting");
	g_async_queue_unref (thread->priv->main_queue);
	g_thread_exit (NULL);
	return NULL;
}


static gpointer
add_thread_main (RBLibraryMainThread *thread)
{
	while (TRUE)
	{
		RBLibraryAction *action;
		RBLibraryActionType type;
		char *uri, *realuri;
		GError *error = NULL;

		action = read_action (thread, thread->priv->add_queue);

		if (action == NULL)
			break;

		rb_library_action_get (action, &type, &uri);

		realuri = rb_uri_resolve_symlink (uri);

		rb_debug ("popped action from add queue, type: %d uri: %s", type, uri);
		switch (type)
		{
		case RB_LIBRARY_ACTION_ADD_FILE:
			rb_library_add_uri_sync (thread->priv->library, realuri, &error);
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		if (error != NULL) {
			push_err (thread, realuri, error);
		}

		g_free (realuri);
		g_object_unref (G_OBJECT (action));
			
		g_usleep (10);
	}

	rb_debug ("exiting");
	g_async_queue_unref (thread->priv->add_queue);
	g_thread_exit (NULL);
	return NULL;
}
