/*
 *  arch-tag: Implementation of small wrapper around GtkTreeViewColumn
 * 
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
 */

#include <stdlib.h>

#include "rb-tree-view-column.h"

static void rb_tree_view_column_class_init (RBTreeViewColumnClass *klass);
static void rb_tree_view_column_init (RBTreeViewColumn *tree_view_column);
static void rb_tree_view_column_finalize (GObject *object);
static void rb_tree_view_column_set_property (GObject *object,
		                              guint prop_id,
		                              const GValue *value,
		                              GParamSpec *pspec);
static void rb_tree_view_column_get_property (GObject *object,
		                              guint prop_id,
		                              GValue *value,
		                              GParamSpec *pspec);

struct RBTreeViewColumnPrivate
{
	gboolean expand;
	
	GList *sort_order;
};

enum
{
	PROP_0,
	PROP_EXPAND,
	PROP_SORT_ORDER
};

static GObjectClass *parent_class = NULL;

GType
rb_tree_view_column_get_type (void)
{
	static GType rb_tree_view_column_type = 0;

	if (rb_tree_view_column_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTreeViewColumnClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_tree_view_column_class_init,
			NULL,
			NULL,
			sizeof (RBTreeViewColumn),
			0,
			(GInstanceInitFunc) rb_tree_view_column_init
		};

		rb_tree_view_column_type = g_type_register_static (GTK_TYPE_TREE_VIEW_COLUMN,
						                   "RBTreeViewColumn",
						                   &our_info, 0);
	}

	return rb_tree_view_column_type;
}

static void
rb_tree_view_column_class_init (RBTreeViewColumnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_tree_view_column_finalize;

	object_class->set_property = rb_tree_view_column_set_property;
	object_class->get_property = rb_tree_view_column_get_property;

	g_object_class_install_property (object_class,
					 PROP_EXPAND,
					 g_param_spec_boolean ("expand",
							       "Expanded",
							       "Column is expanded",
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORT_ORDER,
					 g_param_spec_pointer ("sort-order",
							       "Sort order",
							       "Sort order",
							       G_PARAM_READWRITE));
}

static void
rb_tree_view_column_init (RBTreeViewColumn *tree_view_column)
{
	tree_view_column->priv = g_new0 (RBTreeViewColumnPrivate, 1);
}

static void
rb_tree_view_column_finalize (GObject *object)
{
	RBTreeViewColumn *tree_view_column;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TREE_VIEW_COLUMN (object));

	tree_view_column = RB_TREE_VIEW_COLUMN (object);

	g_return_if_fail (tree_view_column->priv != NULL);
	
	g_list_free (tree_view_column->priv->sort_order);

	g_free (tree_view_column->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_tree_view_column_set_property (GObject *object,
		                  guint prop_id,
		                  const GValue *value,
		                  GParamSpec *pspec)
{
	RBTreeViewColumn *tree_view_column = RB_TREE_VIEW_COLUMN (object);

	switch (prop_id)
	{
	case PROP_EXPAND:
		tree_view_column->priv->expand = g_value_get_boolean (value);
		break;
	case PROP_SORT_ORDER:
		g_list_free (tree_view_column->priv->sort_order);
		tree_view_column->priv->sort_order = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_tree_view_column_get_property (GObject *object,
		                  guint prop_id,
		                  GValue *value,
		                  GParamSpec *pspec)
{
	RBTreeViewColumn *tree_view_column = RB_TREE_VIEW_COLUMN (object);

	switch (prop_id)
	{
	case PROP_EXPAND:
		g_value_set_boolean (value, tree_view_column->priv->expand);
		break;
	case PROP_SORT_ORDER:
		g_value_set_pointer (value, tree_view_column->priv->sort_order);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBTreeViewColumn *
rb_tree_view_column_new (void)
{
	RBTreeViewColumn *tree_view_column;

	tree_view_column = RB_TREE_VIEW_COLUMN (g_object_new (RB_TYPE_TREE_VIEW_COLUMN, NULL));

	g_return_val_if_fail (tree_view_column->priv != NULL, NULL);

	return tree_view_column;
}

gboolean
rb_tree_view_column_get_expand (RBTreeViewColumn *column)
{
	g_return_val_if_fail (RB_IS_TREE_VIEW_COLUMN (column), FALSE);

	return column->priv->expand;
}

void
rb_tree_view_column_set_expand (RBTreeViewColumn *column, gboolean expand)
{
	g_return_if_fail (RB_IS_TREE_VIEW_COLUMN (column));

	g_object_set (G_OBJECT (column),
		      "expand", expand,
		      NULL);
}

GList *
rb_tree_view_column_get_sort_order (RBTreeViewColumn *column)
{
	g_return_val_if_fail (RB_IS_TREE_VIEW_COLUMN (column), NULL);
	
	return column->priv->sort_order;
}

void
rb_tree_view_column_set_sort_order (RBTreeViewColumn *column, GList *sort_order)
{
	g_return_if_fail (RB_IS_TREE_VIEW_COLUMN (column));

	g_object_set (G_OBJECT (column),
		      "sort-order", sort_order,
		      NULL);
}
