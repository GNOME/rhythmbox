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

#include <config.h>
#include <gtk/gtktreeview.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libgnome/gnome-i18n.h>
#include <time.h>
#include <string.h>

#include "rb-tree-model-node.h"
#include "rb-stock-icons.h"
#include "rb-node-song.h"
#include "rb-string-helpers.h"
#include "rb-node.h"
#include "rb-node-song.h"
#include "rb-node-station.h"
#include "rb-debug.h"

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
static inline void rb_tree_model_node_update_node (RBTreeModelNode *model,
				                   RBNode *node,
					           int idx);
static void root_destroyed_cb (RBNode *node,
		               RBTreeModelNode *model);
static inline GtkTreePath *get_path_real (RBTreeModelNode *model,
	                                  RBNode *node);

struct RBTreeModelNodePrivate
{
	RBNode *root;

	RBNode *playing_node;

	GdkPixbuf *playing_pixbuf;
	GdkPixbuf *paused_pixbuf;

	gboolean playing;

	RBNodeFilter *filter;
};

enum
{
	PROP_0,
	PROP_ROOT,
	PROP_PLAYING_NODE,
	PROP_FILTER
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
					 PROP_PLAYING_NODE,
					 g_param_spec_object ("playing-node",
							      "Playing node",
							      "Playing node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_FILTER,
					 g_param_spec_object ("filter",
							      "Filter object",
							      "Filter object",
							      RB_TYPE_NODE_FILTER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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
	model->priv->paused_pixbuf = gtk_widget_render_icon (dummy,
							     RB_STOCK_PAUSED,
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
	g_object_unref (G_OBJECT (model->priv->paused_pixbuf));

	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
filter_changed_cb (RBNodeFilter *filter,
		   RBTreeModelNode *model)
{
	GPtrArray *kids;
	int i;

	kids = rb_node_get_children (model->priv->root);
	
	for (i = 0; i < kids->len; i++)
	{
		rb_tree_model_node_update_node (model,
						g_ptr_array_index (kids, i),
						i);
	}

	rb_node_thaw (model->priv->root);
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
	case PROP_PLAYING_NODE:
		{
			RBNode *old = model->priv->playing_node;
	
			model->priv->playing_node = g_value_get_object (value);
	
			if (old != NULL)
				rb_tree_model_node_update_node (model, old, -1);
			if (model->priv->playing_node != NULL)
				rb_tree_model_node_update_node (model, model->priv->playing_node, -1);
		}
		break;
	case PROP_FILTER:
		model->priv->filter = g_value_get_object (value);

		if (model->priv->filter != NULL)
		{
			g_signal_connect_object (G_OBJECT (model->priv->filter),
					         "changed",
					         G_CALLBACK (filter_changed_cb),
					         G_OBJECT (model),
						 0);
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
	case PROP_PLAYING_NODE:
		g_value_set_object (value, model->priv->playing_node);
		break;
	case PROP_FILTER:
		g_value_set_object (value, model->priv->filter);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBTreeModelNode *
rb_tree_model_node_new (RBNode *root,
			RBNodeFilter *filter)
{
	RBTreeModelNode *model;

	model = RB_TREE_MODEL_NODE (g_object_new (RB_TYPE_TREE_MODEL_NODE,
						  "filter", filter,
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
	case RB_TREE_MODEL_NODE_COL_TITLE:
	case RB_TREE_MODEL_NODE_COL_TITLE_KEY:
	case RB_TREE_MODEL_NODE_COL_ARTIST:
	case RB_TREE_MODEL_NODE_COL_ARTIST_KEY:
	case RB_TREE_MODEL_NODE_COL_ALBUM:
	case RB_TREE_MODEL_NODE_COL_ALBUM_KEY:
	case RB_TREE_MODEL_NODE_COL_GENRE:
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER:
	case RB_TREE_MODEL_NODE_COL_DURATION:
	case RB_TREE_MODEL_NODE_COL_LAST_PLAYED:
	case RB_TREE_MODEL_NODE_COL_PLAY_COUNT:
	case RB_TREE_MODEL_NODE_COL_QUALITY:
		return G_TYPE_STRING;
	case RB_TREE_MODEL_NODE_COL_DUMMY:
	case RB_TREE_MODEL_NODE_COL_PRIORITY:
	case RB_TREE_MODEL_NODE_COL_VISIBLE:
		return G_TYPE_BOOLEAN;
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER_INT:
	case RB_TREE_MODEL_NODE_COL_RATING:
		return G_TYPE_INT;
	default:
		g_assert_not_reached ();
		return G_TYPE_INVALID;
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

	iter->stamp = model->stamp;
	iter->user_data = rb_node_get_nth_child (model->priv->root, i);

	if (iter->user_data == NULL)
	{
		iter->stamp = 0;
		return FALSE;
	}
	
	return TRUE;
}

static inline GtkTreePath *
get_path_real (RBTreeModelNode *model,
	       RBNode *node)
{
	GtkTreePath *retval;

	retval = gtk_tree_path_new ();
	gtk_tree_path_append_index (retval, rb_node_get_child_index (model->priv->root, node));

	return retval;
}

static GtkTreePath *
rb_tree_model_node_get_path (GtkTreeModel *tree_model,
			     GtkTreeIter *iter)
{
	RBTreeModelNode *model = RB_TREE_MODEL_NODE (tree_model);
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
	
	return get_path_real (model, node);
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
	g_return_if_fail (iter->stamp == model->stamp);
	g_return_if_fail (RB_IS_NODE (iter->user_data));
	g_return_if_fail (column < RB_TREE_MODEL_NODE_NUM_COLUMNS);

	if (model->priv->root == NULL)
		return;

	node = RB_NODE (iter->user_data);

	switch (column)
	{
	case RB_TREE_MODEL_NODE_COL_PLAYING:
		g_value_init (value, GDK_TYPE_PIXBUF);

		if (node == model->priv->playing_node && model->priv->playing)
			g_value_set_object (value, model->priv->playing_pixbuf);
		else if (node == model->priv->playing_node)
			g_value_set_object (value, model->priv->paused_pixbuf);
		else
			g_value_set_object (value, NULL);
		break;
	/* Generic node stuff */
	case RB_TREE_MODEL_NODE_COL_TITLE:
		rb_node_get_property (node,
				      RB_NODE_PROP_NAME,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_TITLE_KEY:
		rb_node_get_property (node,
				      RB_NODE_PROP_NAME_SORT_KEY,
				      value);
		break;

	case RB_TREE_MODEL_NODE_COL_VISIBLE:
		g_value_init (value, G_TYPE_BOOLEAN);

		if (model->priv->filter != NULL)
		{
			g_value_set_boolean (value,
					     rb_node_filter_evaluate (model->priv->filter, node));
		}
		else
		{
			g_value_set_boolean (value, TRUE);
		}
		break;
	case RB_TREE_MODEL_NODE_COL_PRIORITY:
		{
			g_value_init (value, G_TYPE_BOOLEAN);

			/* ! because of sorting; 1 goes after 0 .. */
			g_value_set_boolean (value,
					     !rb_node_get_property_boolean (node,
						                            RB_ALL_NODE_PROP_PRIORITY));
		}
		break;
	case RB_TREE_MODEL_NODE_COL_DUMMY:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;
	case RB_TREE_MODEL_NODE_COL_QUALITY:
		if (rb_node_get_property (node,
					  RB_NODE_PROP_QUALITY,
					  value) == FALSE)
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, _("(Unknown)"));
		}
		break;
	case RB_TREE_MODEL_NODE_COL_RATING:
		if (rb_node_get_property (node,
				          RB_NODE_PROP_RATING,
				          value) == FALSE)
		{
			g_value_init (value, G_TYPE_INT);
			g_value_set_int (value, 0);
		}
		break;
	case RB_TREE_MODEL_NODE_COL_ARTIST:
		rb_node_get_property (node,
				      RB_NODE_PROP_ARTIST,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_ARTIST_KEY:
		rb_node_get_property (node,
				      RB_NODE_PROP_ARTIST_SORT_KEY,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_ALBUM:
		rb_node_get_property (node,
				      RB_NODE_PROP_ALBUM,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_ALBUM_KEY:
		rb_node_get_property (node,
				      RB_NODE_PROP_ALBUM_SORT_KEY,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_GENRE:
		rb_node_get_property (node,
				      RB_NODE_PROP_GENRE,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER:
		rb_node_get_property (node,
				      RB_NODE_PROP_TRACK_NUMBER,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_TRACK_NUMBER_INT:
		rb_node_get_property (node,
				      RB_NODE_PROP_REAL_TRACK_NUMBER,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_DURATION:
		rb_node_get_property (node,
				      RB_NODE_PROP_DURATION,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_LAST_PLAYED:
		rb_node_get_property (node,
				      RB_NODE_PROP_LAST_PLAYED_SIMPLE,
				      value);
		break;
	case RB_TREE_MODEL_NODE_COL_PLAY_COUNT:
		rb_node_get_property (node,
				      RB_NODE_PROP_NUM_PLAYS,
				      value);
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
	
	iter->user_data = rb_node_get_next_child (model->priv->root, node);

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
		return rb_node_get_n_children (model->priv->root);

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

	if (node == model->priv->playing_node)
		model->priv->playing_node = NULL;

	path = get_path_real (model, child);
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

	path = get_path_real (model, child);
	gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);
}

static inline void
rb_tree_model_node_update_node (RBTreeModelNode *model,
				RBNode *node,
				int idx)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	rb_tree_model_node_iter_from_node (model, node, &iter);

	if (idx >= 0)
	{
		path = gtk_tree_path_new ();
		gtk_tree_path_append_index (path, idx);
	}
	else
	{
		path = get_path_real (model, node);
	}

	gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);
}

static void
root_child_changed_cb (RBNode *node,
		       RBNode *child,
		       RBTreeModelNode *model)
{
	rb_tree_model_node_update_node (model, child, -1);
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
rb_tree_model_node_set_playing_node (RBTreeModelNode *model,
			             RBNode *node)
{
	g_return_if_fail (RB_IS_TREE_MODEL_NODE (model));

	g_object_set (G_OBJECT (model),
		      "playing-node", node,
		      NULL);
}

RBNode *
rb_tree_model_node_get_playing_node (RBTreeModelNode *model)
{
	g_return_val_if_fail (RB_IS_TREE_MODEL_NODE (model), NULL);

	return model->priv->playing_node;
}

GType
rb_tree_model_node_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ RB_TREE_MODEL_NODE_COL_PLAYING,          "RB_TREE_MODEL_NODE_COL_PLAYING",          "playing" },
			{ RB_TREE_MODEL_NODE_COL_TRACK_NUMBER,     "RB_TREE_MODEL_NODE_COL_TRACK_NUMBER",     "track number" },
			{ RB_TREE_MODEL_NODE_COL_TRACK_NUMBER_INT, "RB_TREE_MODEL_NODE_COL_TRACK_NUMBER_INT", "track number (int format)" },
			{ RB_TREE_MODEL_NODE_COL_TITLE,            "RB_TREE_MODEL_NODE_COL_TITLE",            "title" },
			{ RB_TREE_MODEL_NODE_COL_TITLE_KEY,        "RB_TREE_MODEL_NODE_COL_TITLE_KEY",        "title (g_utf8_collate_key)" },
			{ RB_TREE_MODEL_NODE_COL_ARTIST,           "RB_TREE_MODEL_NODE_COL_ARTIST",           "artist" },
			{ RB_TREE_MODEL_NODE_COL_ARTIST_KEY,       "RB_TREE_MODEL_NODE_COL_ARTIST_KEY",       "artist (g_utf8_collate_key)" },
			{ RB_TREE_MODEL_NODE_COL_ALBUM,            "RB_TREE_MODEL_NODE_COL_ALBUM",            "album" },
			{ RB_TREE_MODEL_NODE_COL_ALBUM_KEY,        "RB_TREE_MODEL_NODE_COL_ALBUM_KEY",        "album (g_utf_collate_key)" },
			{ RB_TREE_MODEL_NODE_COL_GENRE,            "RB_TREE_MODEL_NODE_COL_GENRE",            "genre" },
			{ RB_TREE_MODEL_NODE_COL_DURATION,         "RB_TREE_MODEL_NODE_COL_DURATION",         "duration" },
			{ RB_TREE_MODEL_NODE_COL_RATING,           "RB_TREE_MODEL_NODE_COL_RATING",           "rating" },
			{ RB_TREE_MODEL_NODE_COL_PRIORITY,         "RB_TREE_MODEL_NODE_COL_PRIORITY",         "priority" },
			{ RB_TREE_MODEL_NODE_COL_VISIBLE,          "RB_TREE_MODEL_NODE_COL_VISIBLE",          "visible" },
			{ RB_TREE_MODEL_NODE_COL_PLAY_COUNT,       "RB_TREE_MODEL_NODE_COL_PLAY_COUNT",       "play count" },
			{ RB_TREE_MODEL_NODE_COL_LAST_PLAYED,      "RB_TREE_MODEL_NODE_COL_LAST_PLAYED",      "last played" },
			{ RB_TREE_MODEL_NODE_COL_QUALITY,	   "RB_TREE_MODEL_NODE_COL_QUALITY",	      "quality" },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBTreeModelNodeColumn", values);
	}

	return etype;
}

void
rb_tree_model_node_set_playing (RBTreeModelNode *model,
				gboolean playing)
{
	g_return_if_fail (RB_IS_TREE_MODEL_NODE (model));

	if (model->priv->playing == playing)
		return;

	model->priv->playing = playing;

	if (model->priv->playing_node != NULL)
		rb_tree_model_node_update_node (model,
						model->priv->playing_node,
						-1);
}
