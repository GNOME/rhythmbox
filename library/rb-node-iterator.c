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

#include "rb-node-iterator.h"

static void rb_node_iterator_class_init (RBNodeIteratorClass *klass);
static void rb_node_iterator_init (RBNodeIterator *node_iterator);
static void rb_node_iterator_finalize (GObject *object);

struct RBNodeIteratorPrivate
{
	RBNode *position;
	RBNode *parent;
};

static GObjectClass *parent_class = NULL;

GType
rb_node_iterator_get_type (void)
{
	static GType rb_node_iterator_type = 0;

	if (rb_node_iterator_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNodeIteratorClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_iterator_class_init,
			NULL,
			NULL,
			sizeof (RBNodeIterator),
			0,
			(GInstanceInitFunc) rb_node_iterator_init
		};

		rb_node_iterator_type = g_type_register_static (G_TYPE_OBJECT,
						                "RBNodeIterator",
						                &our_info, 0);
	}

	return rb_node_iterator_type;
}

static void
rb_node_iterator_class_init (RBNodeIteratorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_node_iterator_finalize;
}

static void
rb_node_iterator_init (RBNodeIterator *node_iterator)
{
	node_iterator->priv = g_new0 (RBNodeIteratorPrivate, 1);
}

static void
rb_node_iterator_finalize (GObject *object)
{
	RBNodeIterator *node_iterator;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_ITERATOR (object));

	node_iterator = RB_NODE_ITERATOR (object);

	g_return_if_fail (node_iterator->priv != NULL);

	g_free (node_iterator->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBNodeIterator *
rb_node_iterator_new (void)
{
	RBNodeIterator *node_iterator;

	node_iterator = RB_NODE_ITERATOR (g_object_new (RB_TYPE_NODE_ITERATOR, NULL));

	g_return_val_if_fail (node_iterator->priv != NULL, NULL);

	return node_iterator;
}

void
rb_node_iterator_set_parent (RBNodeIterator *iterator,
			     RBNode *parent)
{
	iterator->priv->parent = parent;
}

void
rb_node_iterator_set_position (RBNodeIterator *iterator,
			       RBNode *position)
{
	iterator->priv->position = position;
}

RBNode *
rb_node_iterator_next (RBNodeIterator *iterator)
{
	GList *kids, *pos;

	kids = rb_node_get_children (iterator->priv->parent);
	pos = g_list_find (kids, iterator->priv->position);
	if (pos == NULL)
	{
		g_list_free (kids);
		return NULL;
	}
	for (pos = g_list_next (pos); pos != NULL; pos = g_list_next (pos))
	{
		if (rb_node_is_handled (RB_NODE (pos->data)) == TRUE)
			break;
	}

	if (pos != NULL)
	{
		iterator->priv->position = RB_NODE (pos->data);
		g_list_free (kids);
		return iterator->priv->position;
	}
	else
	{
		g_list_free (kids);
		return NULL;
	}
}
