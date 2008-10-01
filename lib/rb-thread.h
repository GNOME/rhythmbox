/*
 *  Copyright (C) 2005 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#ifndef __RB_THREAD_H
#define __RB_THREAD_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_THREAD         (rb_thread_get_type ())
#define RB_THREAD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_THREAD, RBThread))
#define RB_THREAD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_THREAD, RBThreadClass))
#define RB_IS_THREAD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_THREAD))
#define RB_IS_THREAD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_THREAD))
#define RB_THREAD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_THREAD, RBThreadClass))

typedef struct RBThreadPrivate RBThreadPrivate;

typedef struct
{
	GObject parent;

	RBThreadPrivate *priv;
} RBThread;

typedef struct
{
	GObjectClass parent;
} RBThreadClass;

typedef gpointer (*RBThreadActionFunc)        (gpointer data, gpointer user_data, gint *exit_flag);
typedef void (*RBThreadResultFunc)        (gpointer data, gpointer user_data);
typedef void (*RBThreadActionDestroyFunc) (gpointer data, gpointer user_data);
typedef void (*RBThreadResultDestroyFunc) (gpointer result, gpointer user_data);

GType		rb_thread_get_type		(void);

RBThread *      rb_thread_new			(GMainContext *context,
						 RBThreadActionFunc action_cb,
						 RBThreadResultFunc result_cb,
						 RBThreadActionDestroyFunc action_destroy_func,
						 RBThreadResultDestroyFunc result_destroy_func,
						 gpointer user_data);

void            rb_thread_push_action           (RBThread *thread,
						 gpointer  action);

void		rb_thread_terminate             (RBThread *thread);

G_END_DECLS

#endif /* __RB_THREAD_H */
