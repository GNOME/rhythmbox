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

#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-tree-model-node.h"
#include "rb-stock-icons.h"
#include "rb-library.h" /* FIXME, rb_node_get_artist, etc */

static void rb_tree_model_node_class_init (RBTreeModelNodeClass *klass);
static void rb_tree_model_node_init (RBTreeModelNode *model);
static void rb_tree_model_node_finalize (GObject *object);
static void rb_tree_model_node_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_tree_model_node_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);
static guint rb_tree_model_node_get_flags (GtkTreeModel *tree_model);
static int rb_tree_model_node_get_n_columns (GtkTreeModel *tree_model);
static GType rb_tree_model_node_get_column_type	(GtkTreeModel *tree_model,
						 int index);
static gboolean rb_tree_model_node_get_iter (GtkTreeModel *tree_model,
					     GtkTreeIter *iter,
					     GtkTreePath *path);
static GtkTreePath *rb_tree_model_node_get_path	(GtkTreeModel *tree_model,
						 GtkTreeIter *iter);
static void rb_tree_model_node_get_value (GtkTreeModel *tree_model,
					  GtkTreeIter *iter,
					  int column,
					  GValue *value);
static gboolean	rb_tree_model_node_iter_next (GtkTreeModel *tree_model,
					      GtkTreeIter *iter);
static gboolean	rb_tree_model_node_iter_children (GtkTreeModel *tree_model,
						  GtkTreeIter *iter,
						  GtkTreeIter *parent);
static gboolean	rb_tree_model_node_iter_has_child (GtkTreeModel *tree_model,
						   GtkTreeIter *iter);
static int rb_tree_model_node_iter_n_children (GtkTreeModel *tree_model,
					       GtkTreeIter *iter);
static gboolean	rb_tree_model_node_iter_nth_child (GtkTreeModel *tree_model,
						   GtkTreeIter *iter,
						   GtkTreeIter *parent,
					           int n);
static gboolean	rb_tree_model_node_iter_parent (GtkTreeModel *tree_model,
					        GtkTreeIter *iter,
					        GtkTreeIter *child);
static void rb_tree_model_node_tree_model_init (GtkTreeModelIface *iface);
static void root_child_removed_cb (RBNode *node,
				   RBNode *child,
				   RBTreeModelNode *model);
static void root_child_added_cb (RBNode *node,
				 RBNode *child,
				 RBTreeModelNode *model);
static void root_child_changed_cb (RBNode *node,
				   RBNode *child,
		                   RBTreeModelNode *model);
static void rb_tree_model_node_update_node (RBTreeModelNode *model,
				            RBNode *node);
static void root_destroyed_cb (RBNode *node,
		               RBTreeModelNode *model);
static void filter_root_child_added_cb (RBNode *node,
					RBNode *child,
					RBTreeModelNode *model);
static void filter_root_child_removed_cb (RBNode *node,
					  RBNode *child,
					  RBTreeModelNode *model);
static void filter_root_destroyed_cb (RBNode *node,
				      RBTreeModelNode *model);

struct RBTreeModelNodePrivate
{
	RBNode *root;

	RBNode *filter_root;
	RBNode *playing_node;

	GdkPixbuf *playing_pixbuf;
};

enum
{
	PROP_0,
	PROP_ROOT,
	PROP_FILTER_ROOT,
	PROP_PLAYING_NODE
};

static GObjectClass *parent_class = NULL;

GType
rb_tree_model_node_get_type (void)
{
	static GType rb_tree_model_node_type = 0;

	if (rb_tree_model_node_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTreeModelNodeClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_tree_model_node_class_init,
			NULL,
			NULL,
			sizeof (RBTreeModelNode),
			0,
			(GInstanceInitFunc) rb_tree_model_node_init
		};

		static const GInterfaceInfo tree_model_info =
		{
			(GInterfaceInitFunc) rb_tree_model_node_tree_model_init,
			NULL,
			NULL
		};

		rb_tree_model_node_type = g_type_register_static (G_TYPE_OBJECT,
								  "RBTreeModelNode",
								  &our_info, 0);

		g_type_add_interface_static (rb_tree_model_node_type,
					     GTK_TYPE_TREE_MODEL,
					     &tree_model_info);
	}

	return rb_tree_model_node_type;
}

static void
rb_tree_model_node_class_init (RBTreeModelNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_tree_model_node_finalize;

	object_class->set_property = rb_tree_model_node_set_property;
	object_class->get_property = rb_tree_model_node_get_property;

	g_object_class_install_property (object_class,
					 PROP_ROOT,
					 g_param_spec_object ("root",
							      "Root node",
							      "Root node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_FILTER_ROOT,
					 g_param_spec_object ("filter-root",
							      "Filter root node",
							      "Filter root node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYING_NODE,
					 g_param_spec_object ("playing-node",
							      "Playing node",
							      "Playing node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
}

static void
rb_tree_model_node_init (RBTreeModelNode *model)
{
	GtkWidget *dummy;
	
	do
	{
		model->stamp = g_random_int ();
	}
	while (model->stamp == 0);

	model->priv = g_new0 (RBTreeModelNodePrivate, 1);

	dummy = gtk_tree_view_new ();
	model->priv->playing_pixbuf = gtk_widget_render_icon (dummy,
							      RB_STOCK_PLAYING,
							      GTK_ICON_SIZE_MENU,
							      NULL);
	gtk_widget_destroy (dummy);
}

static void
rb_tree_model_node_finalize (GObject *object)
{
	RBTreeModelNode *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TREE_MODEL_NODE (object));

	model = RB_TREE_MODEL_NODE (object);

	g_return_if_fail (model->priv != NULL);

	g_object_unref (G_OBJECT (model->priv->playing_pixbuf));
	
	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_tree_model_node_set_property (GObject *object,
			         guint prop_id,
			         const GValue *value,
			         GParamSpec *pspec)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (object);

	switch (prop_id)
	{
	case PROP_ROOT:
		model->priv->root = g_value_get_object (value);
		
		g_signal_connect_object (G_OBJECT (model->priv->root),
				         "child_added",
				         G_CALLBACK (root_child_added_cb),
				         G_OBJECT (model),
					 0);
		g_signal_connect_object (G_OBJECT (model->priv->root),
				         "child_removed",
				         G_CALLBACK (root_child_removed_cb),
				         G_OBJECT (model),
					 0);
		g_signal_connect_object (G_OBJECT (model->priv->root),
				         "child_changed",
				         G_CALLBACK (root_child_changed_cb),
				         G_OBJECT (model),
					 0);
		g_signal_connect_object (G_OBJECT (model->priv->root),
				         "destroyed",
				         G_CALLBACK (root_destroyed_cb),
				         G_OBJECT (model),
					 0);
		break;
	case PROP_FILTER_ROOT:
		{
			RBNode *old = model->priv->filter_root;

			model->priv->filter_root = g_value_get_object (value);

			if (old != NULL)
			{
				GList *kids = rb_node_get_children (old);
				GList *l;

				for (l = kids; l != NULL; l = g_list_next (l))
				{
					rb_tree_model_node_update_node (model, RB_NODE (l->data));
				}

				g_signal_handlers_disconnect_by_func (G_OBJECT (old),
						                      G_CALLBACK (filter_root_child_added_cb),
				                		      model);
				g_signal_handlers_disconnect_by_func (G_OBJECT (old),
						                      G_CALLBACK (filter_root_child_removed_cb),
						                      model);
				g_signal_handlers_disconnect_by_func (G_OBJECT (old),
						                      G_CALLBACK (filter_root_destroyed_cb),
						                      model);
			}

			if (model->priv->filter_root != NULL)
			{
				GList *kids = rb_node_get_children (model->priv->filter_root);
				GList *l;

				for (l = kids; l != NULL; l = g_list_next (l))
				{
					rb_tree_model_node_update_node (model, RB_NODE (l->data));
				}

				g_signal_connect_object (G_OBJECT (model->priv->filter_root),
						         "child_added",
						         G_CALLBACK (filter_root_child_added_cb),
						         G_OBJECT (model),
							 0);
				g_signal_connect_object (G_OBJECT (model->priv->filter_root),
						         "child_removed",
						         G_CALLBACK (filter_root_child_removed_cb),
						         G_OBJECT (model),
							 0);
				g_signal_connect_object (G_OBJECT (model->priv->filter_root),
						         "destroyed",
						         G_CALLBACK (filter_root_destroyed_cb),
						         G_OBJECT (model),
							 0);
			}
		}
		break;
	case PROP_PLAYING_NODE:
		{
			RBNode *old = model->priv->playing_node;
	
			model->priv->playing_node = g_value_get_object (value);
	
			if (old != NULL)
				rb_tree_model_node_update_node (model, old);
			if (model->priv->playing_node != NULL)
				rb_tree_model_node_update_node (model, model->priv->playing_node);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_tree_model_node_get_property (GObject *object,
			         guint prop_id,
				 GValue *value,
			         GParamSpec *pspec)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (object);

	switch (prop_id)
	{
	case PROP_ROOT:
		g_value_set_object (value, model->priv->root);
		break;
	case PROP_FILTER_ROOT:
		g_value_set_object (value, model->priv->filter_root);
		break;
	case PROP_PLAYING_NODE:
		g_value_set_object (value, model->priv->playing_node);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBTreeModelNode *
rb_tree_model_node_new (RBNode *root)
{
	RBTreeModelNode *model;

	model = RB_TREE_MODEL_NODE (g_object_new (RB_TYPE_TREE_MODEL_NODE,
						  "root", root,
						  NULL));

	g_return_val_if_fail (model->priv != NULL, NULL);

	return model;
}

static void
rb_tree_model_node_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags       = rb_tree_model_node_get_flags;
	iface->get_n_columns   = rb_tree_model_node_get_n_columns;
	iface->get_column_type = rb_tree_model_node_get_column_type;
	iface->get_iter        = rb_tree_model_node_get_iter;
	iface->get_path        = rb_tree_model_node_get_path;
	iface->get_value       = rb_tree_model_node_get_value;
	iface->iter_next       = rb_tree_model_node_iter_next;
	iface->iter_children   = rb_tree_model_node_iter_children;
	iface->iter_has_child  = rb_tree_model_node_iter_has_child;
	iface->iter_n_children = rb_tree_model_node_iter_n_children;
	iface->iter_nth_child  = rb_tree_model_node_iter_nth_child;
	iface->iter_parent     = rb_tree_model_node_iter_parent;
}

static guint
rb_tree_model_node_get_flags (GtkTreeModel *tree_model)
{
	return 0;
}

static int
rb_tree_model_node_get_n_columns (GtkTreeModel *tree_model)
{
	return RB_TREE_MODEL_NODE_NUM_COLUMNS;
}

static GType
rb_tree_model_node_get_column_type (GtkTreeModel *tree_model,
			            int index)
{
	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (tree_model), G_TYPE_INVALID);
	g_return_val_if_fail ((index < RB_TREE_MODEL_NODE_NUM_COLUMNS) && (index >= 0), G_TYPE_INVALID);
	
	switch (index)
	{
	case RB_TREE_MODEL_NODE_COL_PLAYING:
		return GDK_TYPE_PIXBUF;
		break;
	case RB_TREE_MODEL_NODE_COL_TITLE:
	case RB_TREE_MODEL_NODE_COL_ARTIST:
	case RB_TREE_MODEL_NODE_COL_ALBUM:
	case RB_TREE_MODEL_NODE_COL_GENRE:
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER:
	case RB_TREE_MODEL_NODE_COL_DURATION:
		return G_TYPE_STRING;
		break;
	case RB_TREE_MODEL_NODE_COL_PRIORITY:
	case RB_TREE_MODEL_NODE_COL_VISIBLE:
		return G_TYPE_BOOLEAN;
		break;
	default:
		g_assert_not_reached ();
		return G_TYPE_INVALID;
		break;
	}
}

static gboolean
rb_tree_model_node_get_iter (GtkTreeModel *tree_model,
			     GtkTreeIter *iter,
			     GtkTreePath *path)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	int i;
	
	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (model), FALSE);
	g_return_val_if_fail (gtk_tree_path_get_depth (path) > 0, FALSE);

	if (model->priv->root == NULL)
		return FALSE;

	i = gtk_tree_path_get_indices (path)[0];

	if (i >= rb_node_n_children (model->priv->root))
		return FALSE;

	iter->stamp = model->stamp;
	iter->user_data = rb_node_get_nth_child (model->priv->root, i);
	
	return TRUE;
}

static GtkTreePath *
rb_tree_model_node_get_path (GtkTreeModel *tree_model,
			     GtkTreeIter *iter)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	GtkTreePath *retval;
	RBNode *node;

	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (tree_model), NULL);
	g_return_val_if_fail (iter != NULL, NULL);
	g_return_val_if_fail (iter->user_data != NULL, NULL);
	g_return_val_if_fail (iter->stamp == model->stamp, NULL);

	if (model->priv->root == NULL)
		return NULL;

	node = RB_NODE (iter->user_data);

	if (node == model->priv->root)
		return gtk_tree_path_new ();
	
	retval = gtk_tree_path_new ();
	gtk_tree_path_append_index (retval, rb_node_get_child_index (model->priv->root, node));

	return retval;
}

static void
rb_tree_model_node_get_value (GtkTreeModel *tree_model,
			      GtkTreeIter *iter,
			      int column,
			      GValue *value)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	RBNode *node;

	g_return_if_fail (RB_IS_TREE_MODEL_NODE (tree_model));
	g_return_if_fail (iter != NULL);
	g_return_if_fail (iter->stamp == RB_TREE_MODEL_NODE (tree_model)->stamp);
	g_return_if_fail (RB_IS_NODE (iter->user_data));
	g_return_if_fail (column < RB_TREE_MODEL_NODE_NUM_COLUMNS);

	if (model->priv->root == NULL)
		return;
	
	node = RB_NODE (iter->user_data);

	g_value_init (value, rb_tree_model_node_get_column_type (tree_model, column));
	
	switch (column) {
	case RB_TREE_MODEL_NODE_COL_PLAYING:
		if (node == model->priv->playing_node)
			g_value_set_object (value, model->priv->playing_pixbuf);
		else
			g_value_set_object (value, NULL);
		break;
	case RB_TREE_MODEL_NODE_COL_TITLE:
		g_value_set_string (value,
				    rb_node_get_string_property (node, NODE_PROPERTY_NAME));
		break;
	case RB_TREE_MODEL_NODE_COL_ARTIST:
		{
			RBNode *artist = rb_node_get_grandparent (node);

			if (artist == NULL)
				break;

			g_value_set_string (value,
					    rb_node_get_string_property (artist, NODE_PROPERTY_NAME));
		}
		break;
	case RB_TREE_MODEL_NODE_COL_ALBUM:
		{
			RBNode *album = rb_node_get_parent (node);

			if (album == NULL)
				break;
			
			g_value_set_string (value,
					    rb_node_get_string_property (album, NODE_PROPERTY_NAME));
		}
		break;
	case RB_TREE_MODEL_NODE_COL_GENRE:
		g_value_set_string (value,
				    rb_node_get_string_property (node, SONG_PROPERTY_GENRE));
		break;
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER:
		g_value_set_string (value,
				    rb_node_get_string_property (node, SONG_PROPERTY_TRACK_NUMBER));
		break;
	case RB_TREE_MODEL_NODE_COL_DURATION:
		{
			char *duration;
			int seconds = rb_node_get_int_property (node, SONG_PROPERTY_DURATION);
			int minutes = 0;

			if (seconds > 0)
			{
				minutes = seconds / 60;
				seconds = seconds % 60;
			}

			duration = g_strdup_printf ("%d:%02d", minutes, seconds);
			g_value_set_string (value, duration);
			g_free (duration);
		}
		break;
	case RB_TREE_MODEL_NODE_COL_VISIBLE:
		if (model->priv->filter_root != NULL)
		{
			g_value_set_boolean (value,
					     rb_node_has_child (model->priv->filter_root, node));
		}
		else
		{
			g_value_set_boolean (value, TRUE);
		}
		break;
	case RB_TREE_MODEL_NODE_COL_PRIORITY:
		g_value_set_boolean (value,
		     		     rb_node_get_int_property (node, "priority"));
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static gboolean
rb_tree_model_node_iter_next (GtkTreeModel *tree_model,
			      GtkTreeIter *iter)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	RBNode *node;

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (iter->user_data != NULL, FALSE);
	g_return_val_if_fail (iter->stamp == RB_TREE_MODEL_NODE (tree_model)->stamp, FALSE);

	if (model->priv->root == NULL)
		return FALSE;
	
	node = RB_NODE (iter->user_data);

	if (node == model->priv->root)
		return FALSE;
	
	iter->user_data = rb_node_next (node);

	return (iter->user_data != NULL);
}

static gboolean
rb_tree_model_node_iter_children (GtkTreeModel *tree_model,
			          GtkTreeIter *iter,
			          GtkTreeIter *parent)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);

	if (model->priv->root == NULL)
		return FALSE;
	
	if (parent != NULL)
		return FALSE;

	iter->stamp = model->stamp;
	iter->user_data = model->priv->root;

	return TRUE;
}

static gboolean
rb_tree_model_node_iter_has_child (GtkTreeModel *tree_model,
			           GtkTreeIter *iter)
{
	return FALSE;
}

static int
rb_tree_model_node_iter_n_children (GtkTreeModel *tree_model,
			            GtkTreeIter *iter)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	
	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (tree_model), -1);

	if (model->priv->root == NULL)
		return 0;

	if (iter == NULL)
		return rb_node_n_children (model->priv->root);

	g_return_val_if_fail (model->stamp == iter->stamp, -1);

	return 0;
}

static gboolean
rb_tree_model_node_iter_nth_child (GtkTreeModel *tree_model,
			           GtkTreeIter *iter,
			           GtkTreeIter *parent,
			           int n)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
	RBNode *node;

	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (tree_model), FALSE);

	if (model->priv->root == NULL)
		return FALSE;

	if (parent != NULL)
		return FALSE;

	node = rb_node_get_nth_child (model->priv->root, n);

	if (node != NULL)
	{
		iter->stamp = model->stamp;
		iter->user_data = node;
		return TRUE;
	}
	else
		return FALSE;
}

static gboolean
rb_tree_model_node_iter_parent (GtkTreeModel *tree_model,
			        GtkTreeIter *iter,
			        GtkTreeIter *child)
{
	return FALSE;
}

RBNode *
rb_tree_model_node_node_from_iter (RBTreeModelNode *model,
				   GtkTreeIter *iter)
{
	return RB_NODE (iter->user_data);
}

void
rb_tree_model_node_iter_from_node (RBTreeModelNode *model,
				   RBNode *node,
				   GtkTreeIter *iter)
{
	iter->stamp = model->stamp;
	iter->user_data = node;
}

static void
root_child_removed_cb (RBNode *node,
		       RBNode *child,
		       RBTreeModelNode *model)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	if (node == model->priv->playing_node)
		model->priv->playing_node = NULL;

	rb_tree_model_node_iter_from_node (model, child, &iter);

	path = rb_tree_model_node_get_path (GTK_TREE_MODEL (model), &iter);
	gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
	gtk_tree_path_free (path);
}

static void
root_child_added_cb (RBNode *node,
		     RBNode *child,
		     RBTreeModelNode *model)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	rb_tree_model_node_iter_from_node (model, child, &iter);

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, rb_node_get_child_index (node, child));
	gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);
}

static void
rb_tree_model_node_update_node (RBTreeModelNode *model,
				RBNode *node)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	rb_tree_model_node_iter_from_node (model, node, &iter);

	path = rb_tree_model_node_get_path (GTK_TREE_MODEL (model), &iter);
	gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);
}

static void
root_child_changed_cb (RBNode *node,
		       RBNode *child,
		       RBTreeModelNode *model)
{
	rb_tree_model_node_update_node (model, child);
}

static void
root_destroyed_cb (RBNode *node,
		   RBTreeModelNode *model)
{
	model->priv->root = NULL;

	/* no need to do other stuff since we should have had a bunch of child_removed
	 * signals already */
}

void
rb_tree_model_node_set_filter_root (RBTreeModelNode *model,
				    RBNode *root)
{
	g_return_if_fail (RB_IS_TREE_MODEL_NODE (model));

	g_object_set (G_OBJECT (model),
		      "filter-root", root,
		      NULL);
}

void
rb_tree_model_node_set_playing_node (RBTreeModelNode *model,
			             RBNode *node)
{
	g_return_if_fail (RB_IS_TREE_MODEL_NODE (model));

	g_object_set (G_OBJECT (model),
		      "playing-node", node,
		      NULL);
}

static void
filter_root_child_added_cb (RBNode *node,
			    RBNode *child,
			    RBTreeModelNode *model)
{
	if (model->priv->root == NULL)
		return;

	if (rb_node_has_child (model->priv->root, child) == FALSE)
		return;

	rb_tree_model_node_update_node (model, child);
}

static void
filter_root_child_removed_cb (RBNode *node,
			      RBNode *child,
			      RBTreeModelNode *model)
{
	if (model->priv->root == NULL)
		return;

	if (rb_node_has_child (model->priv->root, child) == FALSE)
		return;

	rb_tree_model_node_update_node (model, child);
}

static void
filter_root_destroyed_cb (RBNode *node,
			  RBTreeModelNode *model)
{
	model->priv->filter_root = NULL;
}

GType
rb_tree_model_node_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ RB_TREE_MODEL_NODE_COL_PLAYING,      "RB_TREE_MODEL_NODE_COL_PLAYING",      "playing" },
			{ RB_TREE_MODEL_NODE_COL_TRACK_NUMBER, "RB_TREE_MODEL_NODE_COL_TRACK_NUMBER", "track number" },
			{ RB_TREE_MODEL_NODE_COL_TITLE,        "RB_TREE_MODEL_NODE_COL_TITLE",        "title" },
			{ RB_TREE_MODEL_NODE_COL_ARTIST,       "RB_TREE_MODEL_NODE_COL_ARTIST",       "artist" },
			{ RB_TREE_MODEL_NODE_COL_ALBUM,        "RB_TREE_MODEL_NODE_COL_ALBUM",        "album" },
			{ RB_TREE_MODEL_NODE_COL_GENRE,        "RB_TREE_MODEL_NODE_COL_GENRE",        "genre" },
			{ RB_TREE_MODEL_NODE_COL_DURATION,     "RB_TREE_MODEL_NODE_COL_DURATION",     "duration" },
			{ RB_TREE_MODEL_NODE_COL_PRIORITY,     "RB_TREE_MODEL_NODE_COL_PRIORITY",     "priority" },
			{ RB_TREE_MODEL_NODE_COL_VISIBLE,      "RB_TREE_MODEL_NODE_COL_VISIBLE",      "visible" },
		};

		etype = g_enum_register_static ("RBTreeModelNodeColumn", values);
	}

	return etype;
}
