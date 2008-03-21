/*
 *  Copyright (C) 2007 Jonathan Matthew
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

#ifndef __RB_ASYNC_QUEUE_WATCH_H
#define __RB_ASYNC_QUEUE_WATCH_H

#include <glib.h>

typedef void (*RBAsyncQueueWatchFunc) (gpointer item, gpointer data);

guint rb_async_queue_watch_new (GAsyncQueue *queue,
				gint priority,
				RBAsyncQueueWatchFunc callback,
				gpointer user_data,
				GDestroyNotify notify,
				GMainContext *context);

#endif /* __RB_ASYNC_QUEUE_WATCH_H */

