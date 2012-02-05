/*
 *  Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

#ifndef __RB_ASYNC_COPY_H
#define __RB_ASYNC_COPY_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define RB_TYPE_ASYNC_COPY           (rb_async_copy_get_type ())
#define RB_ASYNC_COPY(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_ASYNC_COPY, RBAsyncCopy))
#define RB_ASYNC_COPY_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_ASYNC_COPY, RBAsyncCopyClass))
#define RB_IS_ASYNC_COPY(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_ASYNC_COPY))
#define RB_IS_ASYNC_COPY_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_ASYNC_COPY))
#define RB_ASYNC_COPY_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_ASYNC_COPY, RBAsyncCopyClass))

typedef struct _RBAsyncCopy RBAsyncCopy;
typedef struct _RBAsyncCopyClass RBAsyncCopyClass;
typedef struct _RBAsyncCopyPrivate RBAsyncCopyPrivate;

typedef void (*RBAsyncCopyProgressCallback) (RBAsyncCopy *copy, goffset position, goffset total, gpointer data);
typedef void (*RBAsyncCopyCallback) (RBAsyncCopy *copy, gboolean success, gpointer data);

struct _RBAsyncCopy
{
	GObject parent;
	RBAsyncCopyPrivate *priv;
};

struct _RBAsyncCopyClass
{
	GObjectClass parent_class;
};

GType			rb_async_copy_get_type		(void);

RBAsyncCopy *		rb_async_copy_new		(void);

void			rb_async_copy_set_progress 	(RBAsyncCopy *copy,
							 RBAsyncCopyProgressCallback callback,
							 gpointer user_data,
							 GDestroyNotify destroy_data);

GError *		rb_async_copy_get_error		(RBAsyncCopy *copy);

void			rb_async_copy_start		(RBAsyncCopy *copy,
							 const char *src,
							 const char *dest,
							 RBAsyncCopyCallback callback,
							 gpointer user_data,
							 GDestroyNotify destroy_data);

void			rb_async_copy_cancel		(RBAsyncCopy *copy);

G_END_DECLS

#endif /* __RB_ASYNC_COPY_H */
