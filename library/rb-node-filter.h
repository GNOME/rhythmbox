/*
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
 *            (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include "rb-node.h"

G_BEGIN_DECLS

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

	void (*changed) (RBNodeFilter *filter);
} RBNodeFilterClass;

typedef enum
{
	RB_NODE_FILTER_EXPRESSION_ALWAYS_TRUE,           /* args: none */
	RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,           /* args: RBNode *a, RBNode *b */
	RB_NODE_FILTER_EXPRESSION_EQUALS,                /* args: RBNode *node */
	RB_NODE_FILTER_EXPRESSION_HAS_PARENT,            /* args: RBNode *parent */
	RB_NODE_FILTER_EXPRESSION_HAS_CHILD,             /* args: RBNode *child */     
	RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,      /* args: int prop_id, RBNode *node */
	RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,  /* args: int prop_id, const char *string */
	RB_NODE_FILTER_EXPRESSION_STRING_PROP_EQUALS,    /* args: int prop_id, const char *string */
	RB_NODE_FILTER_EXPRESSION_KEY_PROP_CONTAINS,     /* args: int prop_id, const char *string */
	RB_NODE_FILTER_EXPRESSION_KEY_PROP_EQUALS,       /* args: int prop_id, const char *string */
	RB_NODE_FILTER_EXPRESSION_INT_PROP_EQUALS,       /* args: int prop_id, int int */
	RB_NODE_FILTER_EXPRESSION_INT_PROP_BIGGER_THAN,  /* args: int prop_id, int int */
	RB_NODE_FILTER_EXPRESSION_INT_PROP_LESS_THAN     /* args: int prop_id, int int */
} RBNodeFilterExpressionType;

typedef struct RBNodeFilterExpression RBNodeFilterExpression;

/* The filter starts iterating over all expressions at level 0,
 * if one of them is TRUE it continues to level 1, etc.
 * If it still has TRUE when there are no more expressions at the
 * next level, the result is TRUE. Otherwise, it's FALSE.
 */

GType         rb_node_filter_get_type       (void);

RBNodeFilter *rb_node_filter_new            (void);

void          rb_node_filter_add_expression (RBNodeFilter *filter,
					     RBNodeFilterExpression *expression,
					     int level);

void          rb_node_filter_empty          (RBNodeFilter *filter);

void          rb_node_filter_done_changing  (RBNodeFilter *filter);

gboolean      rb_node_filter_evaluate       (RBNodeFilter *filter,
					     RBNode *node);

RBNodeFilterExpression *rb_node_filter_expression_new  (RBNodeFilterExpressionType,
						        ...);
/* no need to free unless you didn't add the expression to a filter */
void                    rb_node_filter_expression_free (RBNodeFilterExpression *expression);

G_END_DECLS

#endif /* __RB_NODE_FILTER_H */
