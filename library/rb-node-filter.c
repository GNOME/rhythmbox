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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "rb-node-filter.h"

static void rb_node_filter_class_init (RBNodeFilterClass *klass);
static void rb_node_filter_init (RBNodeFilter *node);
static void rb_node_filter_finalize (GObject *object);
static gboolean rb_node_filter_expression_evaluate (RBNodeFilterExpression *expression,
						      RBNode *node);

enum
{
	CHANGED,
	LAST_SIGNAL
};

struct RBNodeFilterPrivate
{
	GPtrArray *levels;
};

struct RBNodeFilterExpression
{
	RBNodeFilterExpressionType type;

	union
	{
		struct
		{
			RBNode *a;
			RBNode *b;
		} node_args;

		struct
		{
			int prop_id;

			union
			{
				RBNode *node;
				char *string;
				int number;
			} second_arg;
		} prop_args;
	} args;
};

static GObjectClass *parent_class = NULL;

static guint rb_node_filter_signals[LAST_SIGNAL] = { 0 };

GType
rb_node_filter_get_type (void)
{
	static GType rb_node_filter_type = 0;

	if (rb_node_filter_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNodeFilterClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_filter_class_init,
			NULL,
			NULL,
			sizeof (RBNodeFilter),
			0,
			(GInstanceInitFunc) rb_node_filter_init
		};

		rb_node_filter_type = g_type_register_static (G_TYPE_OBJECT,
							      "RBNodeFilter",
							      &our_info, 0);
	}

	return rb_node_filter_type;
}

static void
rb_node_filter_class_init (RBNodeFilterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_node_filter_finalize;

	rb_node_filter_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeFilterClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_node_filter_init (RBNodeFilter *filter)
{
	filter->priv = g_new0 (RBNodeFilterPrivate, 1);

	filter->priv->levels = g_ptr_array_new ();
}

static void
rb_node_filter_finalize (GObject *object)
{
	RBNodeFilter *filter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_FILTER (object));

	filter = RB_NODE_FILTER (object);

	g_return_if_fail (filter->priv != NULL);

	rb_node_filter_empty (filter);

	g_ptr_array_free (filter->priv->levels, FALSE);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBNodeFilter *
rb_node_filter_new (void)
{
	RBNodeFilter *filter;

	filter = RB_NODE_FILTER (g_object_new (RB_TYPE_NODE_FILTER,
					       NULL));

	g_return_val_if_fail (filter->priv != NULL, NULL);

	return filter;
}

void
rb_node_filter_add_expression (RBNodeFilter *filter,
			         RBNodeFilterExpression *exp,
			         int level)
{
	while (level >= filter->priv->levels->len)
		g_ptr_array_add (filter->priv->levels, NULL);

	g_ptr_array_index (filter->priv->levels, level) =
		g_list_append (g_ptr_array_index (filter->priv->levels, level), exp);
}

void
rb_node_filter_empty (RBNodeFilter *filter)
{
	int i;
	
	for (i = filter->priv->levels->len - 1; i >= 0; i--)
	{
		GList *list, *l;

		list = g_ptr_array_index (filter->priv->levels, i);

		for (l = list; l != NULL; l = g_list_next (l))
		{
			RBNodeFilterExpression *exp;

			exp = (RBNodeFilterExpression *) l->data;

			rb_node_filter_expression_free (exp);
		}

		g_list_free (list);

		g_ptr_array_remove_index (filter->priv->levels, i);
	}
}

void
rb_node_filter_done_changing (RBNodeFilter *filter)
{
	g_signal_emit (G_OBJECT (filter), rb_node_filter_signals[CHANGED], 0);
}

/*
 * We go through each level evaluating the filter expressions. 
 * Every time we get a match we immediately do a break and jump
 * to the next level. We'll return FALSE if we arrive to a level 
 * without matches, TRUE otherwise.
 */
gboolean
rb_node_filter_evaluate (RBNodeFilter *filter,
			   RBNode *node)
{
	int i;

	for (i = 0; i < filter->priv->levels->len; i++) {
		GList *l, *list;
		gboolean handled;

		handled = FALSE;

		list = g_ptr_array_index (filter->priv->levels, i);

		for (l = list; l != NULL; l = g_list_next (l)) {
			if (rb_node_filter_expression_evaluate (l->data, node) == TRUE) {
				handled = TRUE;
				break;
			}
		}

		if (handled == FALSE)
			return FALSE;
	}
	
	return TRUE;
}

RBNodeFilterExpression *
rb_node_filter_expression_new (RBNodeFilterExpressionType type,
			         ...)
{
	RBNodeFilterExpression *exp;
	va_list valist;

	va_start (valist, type);

	exp = g_new0 (RBNodeFilterExpression, 1);

	exp->type = type;

	switch (type)
	{
	case RB_NODE_FILTER_EXPRESSION_NODE_EQUALS:
		exp->args.node_args.a = va_arg (valist, RBNode *);
		exp->args.node_args.b = va_arg (valist, RBNode *);
		break;
	case RB_NODE_FILTER_EXPRESSION_EQUALS:
	case RB_NODE_FILTER_EXPRESSION_HAS_PARENT:
	case RB_NODE_FILTER_EXPRESSION_HAS_CHILD:
		exp->args.node_args.a = va_arg (valist, RBNode *);
		break;
	case RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS:
	case RB_NODE_FILTER_EXPRESSION_CHILD_PROP_EQUALS:
		exp->args.prop_args.prop_id = va_arg (valist, int);
		exp->args.prop_args.second_arg.node = va_arg (valist, RBNode *);
		break;
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS:
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_EQUALS:
		exp->args.prop_args.prop_id = va_arg (valist, int);
		exp->args.prop_args.second_arg.string = g_utf8_casefold (va_arg (valist, char *), -1);
		break;
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_CONTAINS:
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_EQUALS:
	{
		char *folded;

		exp->args.prop_args.prop_id = va_arg (valist, int);

		folded = g_utf8_casefold (va_arg (valist, char *), -1);
		exp->args.prop_args.second_arg.string = g_utf8_collate_key (folded, -1);
		g_free (folded);
		break;
	}
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_EQUALS:
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_BIGGER_THAN:
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_LESS_THAN:
		exp->args.prop_args.prop_id = va_arg (valist, int);
		exp->args.prop_args.second_arg.number = va_arg (valist, int);
		break;
	default:
		break;
	}

	va_end (valist);

	return exp;
}

void
rb_node_filter_expression_free (RBNodeFilterExpression *exp)
{
	switch (exp->type)
	{
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS:
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_EQUALS:
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_CONTAINS:
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_EQUALS:
		g_free (exp->args.prop_args.second_arg.string);
		break;
	default:
		break;
	}
	
	g_free (exp);
}

static gboolean
rb_node_filter_expression_evaluate (RBNodeFilterExpression *exp,
				      RBNode *node)
{
	switch (exp->type)
	{
	case RB_NODE_FILTER_EXPRESSION_ALWAYS_TRUE:
		return TRUE;
	case RB_NODE_FILTER_EXPRESSION_NODE_EQUALS:
		return (exp->args.node_args.a == exp->args.node_args.b);
	case RB_NODE_FILTER_EXPRESSION_EQUALS:
		return (exp->args.node_args.a == node);	
	case RB_NODE_FILTER_EXPRESSION_HAS_PARENT:
		return rb_node_has_child (exp->args.node_args.a, node);
	case RB_NODE_FILTER_EXPRESSION_HAS_CHILD:
		return rb_node_has_child (node, exp->args.node_args.a);
	case RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS:
	{
		RBNode *prop;

		prop = rb_node_get_property_pointer (node,
						     exp->args.prop_args.prop_id);
		
		return (prop == exp->args.prop_args.second_arg.node);
	}
	case RB_NODE_FILTER_EXPRESSION_CHILD_PROP_EQUALS:
	{
		RBNode *prop;
		GPtrArray *children;
		int i;
		
		children = rb_node_get_children (node);
		for (i = 0; i < children->len; i++)
		{
			RBNode *child;
			
			child = g_ptr_array_index (children, i);
			prop = rb_node_get_property_pointer 
				(child, exp->args.prop_args.prop_id);
		
			if (prop == exp->args.prop_args.second_arg.node)
			{
				rb_node_thaw (node);
				return TRUE;
			}
		}
		
		rb_node_thaw (node);
		return FALSE;
	}
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS:
	{
		const char *prop;
		char *folded_case;
		gboolean ret;

		prop = rb_node_get_property_string (node,
						    exp->args.prop_args.prop_id);
		if (prop == NULL)
			return FALSE;

		folded_case = g_utf8_casefold (prop, -1);
		ret = (strstr (folded_case, exp->args.prop_args.second_arg.string) != NULL);
		g_free (folded_case);

		return ret;
	}
	case RB_NODE_FILTER_EXPRESSION_STRING_PROP_EQUALS:
	{
		const char *prop;
		char *folded_case;
		gboolean ret;

		prop = rb_node_get_property_string (node,
						    exp->args.prop_args.prop_id);

		if (prop == NULL)
			return FALSE;

		folded_case = g_utf8_casefold (prop, -1);
		ret = (strcmp (folded_case, exp->args.prop_args.second_arg.string) == 0);
		g_free (folded_case);

		return ret;
	}
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_CONTAINS:
	{
		const char *prop;

		prop = rb_node_get_property_string (node,
						    exp->args.prop_args.prop_id);

		if (prop == NULL)
			return FALSE;

		return (strstr (prop, exp->args.prop_args.second_arg.string) != NULL);
	}
	case RB_NODE_FILTER_EXPRESSION_KEY_PROP_EQUALS:
	{
		const char *prop;

		prop = rb_node_get_property_string (node,
						    exp->args.prop_args.prop_id);

		if (prop == NULL)
			return FALSE;

		return (strcmp (prop, exp->args.prop_args.second_arg.string) == 0);
	}
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_EQUALS:
	{
		int prop;

		prop = rb_node_get_property_int (node,
						 exp->args.prop_args.prop_id);

		return (prop == exp->args.prop_args.second_arg.number);
	}
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_BIGGER_THAN:
	{
		int prop;

		prop = rb_node_get_property_int (node,
						 exp->args.prop_args.prop_id);

		return (prop > exp->args.prop_args.second_arg.number);
	}
	case RB_NODE_FILTER_EXPRESSION_INT_PROP_LESS_THAN:
	{
		int prop;

		prop = rb_node_get_property_int (node,
						 exp->args.prop_args.prop_id);
		g_print ("%d %d\n", prop, exp->args.prop_args.second_arg.number);
		return (prop < exp->args.prop_args.second_arg.number);
	}
	default:
		break;
	}

	return FALSE;
}
