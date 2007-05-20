/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  Jonathan Matthew
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <rb-async-queue-watch.h>

typedef struct {
	GSource source;
	GAsyncQueue *queue;
} RBAsyncQueueWatch;

static gboolean
rb_async_queue_watch_prepare (GSource *source, gint *timeout)
{
	RBAsyncQueueWatch *watch = (RBAsyncQueueWatch *)source;
	*timeout = -1;
	return (g_async_queue_length (watch->queue) > 0);
}

static gboolean
rb_async_queue_watch_check (GSource *source)
{
	RBAsyncQueueWatch *watch = (RBAsyncQueueWatch *)source;
	return (g_async_queue_length (watch->queue) > 0);
}

static gboolean
rb_async_queue_watch_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
	RBAsyncQueueWatch *watch = (RBAsyncQueueWatch *)source;
	RBAsyncQueueWatchFunc cb = (RBAsyncQueueWatchFunc)callback;
	gpointer event;

	event = g_async_queue_try_pop (watch->queue);
	if (event == NULL) {
		return TRUE;
	}

	if (cb == NULL) {
		return FALSE;
	}

	cb (event, user_data);
	return TRUE;
}

static void
rb_async_queue_watch_finalize (GSource *source)
{
	RBAsyncQueueWatch *watch = (RBAsyncQueueWatch *)source;

	if (watch->queue != NULL) {
		g_async_queue_unref (watch->queue);
		watch->queue = NULL;
	}
}

static GSourceFuncs rb_async_queue_watch_funcs = {
	rb_async_queue_watch_prepare,
	rb_async_queue_watch_check,
	rb_async_queue_watch_dispatch,
	rb_async_queue_watch_finalize
};

guint rb_async_queue_watch_new (GAsyncQueue *queue,
				gint priority,
				RBAsyncQueueWatchFunc callback,
				gpointer user_data,
				GDestroyNotify notify,
				GMainContext *context)
{
	GSource *source;
	RBAsyncQueueWatch *watch;
	guint id;

	source = (GSource *) g_source_new (&rb_async_queue_watch_funcs,
					   sizeof (RBAsyncQueueWatch));

	watch = (RBAsyncQueueWatch *)source;
	watch->queue = g_async_queue_ref (queue);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);

	g_source_set_callback (source, (GSourceFunc) callback, user_data, notify);

	id = g_source_attach (source, context);
	g_source_unref (source);
	return id;
}

