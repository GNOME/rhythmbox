/*
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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

#ifndef __RB_NODE_FILTER_H
#define __RB_NODE_FILTER_H

#include <glib-object.h>

#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_NODE_FILTER_TYPE (rb_node_filter_type_get_type ())

GType rb_node_filter_type_get_type (void);

#define RB_TYPE_NODE_FILTER         (rb_node_filter_get_type ())
#define RB_NODE_FILTER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_FILTER, RBNodeFilter))
#define RB_NODE_FILTER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_FILTER, RBNodeFilterClass))
#define RB_IS_NODE_FILTER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_FILTER))
#define RB_IS_NODE_FILTER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_FILTER))
#define RB_NODE_FILTER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_FILTER, RBNodeFilterClass))

typedef struct RBNodeFilterPrivate RBNodeFilterPrivate;

typedef struct
{
	GObject parent;

	RBNodeFilterPrivate *priv;
} RBNodeFilter;

typedef struct
{
	GObjectClass parent;

} RBNodeFilterClass;

GType         rb_node_filter_get_type       (void);

RBNodeFilter *rb_node_filter_new            (RBLibrary *library);

RBNode       *rb_node_filter_get_root       (RBNodeFilter *filter);

void	      rb_node_filter_set_expression (RBNodeFilter *filter,
					     const char *expression);

void	      rb_node_filter_abort_search   (RBNodeFilter *filter);

G_END_DECLS

#endif /* __RB_NODE_FILTER_H */
