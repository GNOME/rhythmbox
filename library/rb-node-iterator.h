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

#ifndef __RB_NODE_ITERATOR_H
#define __RB_NODE_ITERATOR_H

#include "rb-node.h"

G_BEGIN_DECLS

#define RB_TYPE_NODE_ITERATOR         (rb_node_iterator_get_type ())
#define RB_NODE_ITERATOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_ITERATOR, RBNodeIterator))
#define RB_NODE_ITERATOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_ITERATOR, RBNodeIteratorClass))
#define RB_IS_NODE_ITERATOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_ITERATOR))
#define RB_IS_NODE_ITERATOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_ITERATOR))
#define RB_NODE_ITERATOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_ITERATOR, RBNodeIteratorClass))

typedef struct RBNodeIteratorPrivate RBNodeIteratorPrivate;

typedef struct
{
	GObject parent;

	RBNodeIteratorPrivate *priv;
} RBNodeIterator;

typedef struct
{
	GObjectClass parent;
} RBNodeIteratorClass;

GType           rb_node_iterator_get_type     (void);

RBNodeIterator *rb_node_iterator_new          (void);

void            rb_node_iterator_set_parent   (RBNodeIterator *iterator,
					       RBNode *parent);

void            rb_node_iterator_set_position (RBNodeIterator *iterator,
					       RBNode *position);

RBNode         *rb_node_iterator_next         (RBNodeIterator *iterator);

G_END_DECLS

#endif /* __RB_NODE_ITERATOR_H */
