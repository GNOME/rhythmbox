/*
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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

#ifndef __RB_GLIST_WRAPPER_H
#define __RB_GLIST_WRAPPER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_GLIST_WRAPPER         (rb_glist_wrapper_get_type ())
#define RB_GLIST_WRAPPER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GLIST_WRAPPER, RBGListWrapper))
#define RB_GLIST_WRAPPER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GLIST_WRAPPER, RBGListWrapperClass))
#define RB_IS_GLIST_WRAPPER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GLIST_WRAPPER))
#define RB_IS_GLIST_WRAPPER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GLIST_WRAPPER))
#define RB_GLIST_WRAPPER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GLIST_WRAPPER, RBGListWrapperClass))

typedef struct _RBGListWrapperPrivate RBGListWrapperPrivate;

typedef struct
{
	GObject parent;

	RBGListWrapperPrivate *priv;
} RBGListWrapper;

typedef struct
{
	GObjectClass parent;
} RBGListWrapperClass;

GType rb_glist_wrapper_get_type (void);

GList *rb_glist_wrapper_get_list (RBGListWrapper *listwrapper);

void rb_glist_wrapper_set_list (RBGListWrapper *listwrapper, GList *val);

G_END_DECLS

#endif /* __RB_GLIST_WRAPPER_H */
