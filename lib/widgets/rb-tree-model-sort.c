/*  Rhythmbox.
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

#include <gtk/gtkmarshal.h>
#include <string.h>

#include "rb-node.h"
#include "rb-node-song.h"
#include "rb-tree-model-sort.h"
#include "eggtreemultidnd.h"
#include "rb-library-dnd-types.h"

static void rb_tree_model_sort_class_init (RBTreeModelSortClass *klass);
static void rb_tree_model_sort_init (RBTreeModelSort *ma);
static void rb_tree_model_sort_finalize (GObject *object);
static void rb_tree_model_sort_multi_drag_source_init (EggTreeMultiDragSourceIface *iface);
static gboolean rb_tree_model_sort_multi_row_draggable (EggTreeMultiDragSource *drag_source, 
							GList *path_list);
static gboolean rb_tree_model_sort_multi_drag_data_get (EggTreeMultiDragSource *drag_source,
							GList *path_list,
							GtkSelectionData *selection_data);
static gboolean rb_tree_model_sort_multi_drag_data_delete (EggTreeMultiDragSource *drag_source, 
					   		   GList *path_list);

struct RBTreeModelSortPrivate
{
	char *str_list;
};

enum
{
	NODE_FROM_ITER,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_tree_model_sort_signals[LAST_SIGNAL] = { 0 };

/* dnd */
static const GtkTargetEntry target_table [] = 
		{
			{ RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST } ,
			{ RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID }
		};

static GtkTargetList *drag_target_list = NULL;

GType
rb_tree_model_sort_get_type (void)
{
	static GType rb_tree_model_sort_type = 0;

	if (rb_tree_model_sort_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTreeModelSortClass),
			NULL, /* base init */
			NULL, /* base finalize */
			(GClassInitFunc) rb_tree_model_sort_class_init,
			NULL, /* class finalize */
			NULL, /* class data */
			sizeof (RBTreeModelSort),
			0, /* n_preallocs */
			(GInstanceInitFunc) rb_tree_model_sort_init
		};
		static const GInterfaceInfo multi_drag_source_info =
		{
			(GInterfaceInitFunc) rb_tree_model_sort_multi_drag_source_init,
			NULL,
			NULL
		};

		rb_tree_model_sort_type = g_type_register_static (GTK_TYPE_TREE_MODEL_SORT,
								  "RBTreeModelSort",
								  &our_info, 0);

		g_type_add_interface_static (rb_tree_model_sort_type,
					     EGG_TYPE_TREE_MULTI_DRAG_SOURCE,
					     &multi_drag_source_info);
	}

	return rb_tree_model_sort_type;
}

static void
rb_tree_model_sort_class_init (RBTreeModelSortClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_tree_model_sort_finalize;

	rb_tree_model_sort_signals[NODE_FROM_ITER] =
		g_signal_new ("node_from_iter",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTreeModelSortClass, node_from_iter),
			      NULL, NULL,
			      gtk_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER,
			      G_TYPE_POINTER);
}

static void
rb_tree_model_sort_init (RBTreeModelSort *ma)
{
	ma->priv = g_new0 (RBTreeModelSortPrivate, 1);
}

static void
rb_tree_model_sort_finalize (GObject *object)
{
	RBTreeModelSort *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TREE_MODEL_SORT (object));

	model = RB_TREE_MODEL_SORT (object);

	g_free (model->priv->str_list);
	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkTreeModel*
rb_tree_model_sort_new (GtkTreeModel *child_model)
{
	GtkTreeModel *model;

	g_return_val_if_fail (child_model != NULL, NULL);

	model = GTK_TREE_MODEL (g_object_new (RB_TYPE_TREE_MODEL_SORT,
					      "model", child_model,
					      NULL));

	return model;
}

static void
rb_tree_model_sort_multi_drag_source_init (EggTreeMultiDragSourceIface *iface)
{
	iface->row_draggable    = rb_tree_model_sort_multi_row_draggable;
	iface->drag_data_get    = rb_tree_model_sort_multi_drag_data_get;
	iface->drag_data_delete = rb_tree_model_sort_multi_drag_data_delete;
}

static gboolean
rb_tree_model_sort_multi_row_draggable (EggTreeMultiDragSource *drag_source, GList *path_list)
{
	return TRUE;
}

static gboolean
rb_tree_model_sort_multi_drag_data_delete (EggTreeMultiDragSource *drag_source, 
					   GList *path_list)
{
	return TRUE;
}

static gboolean
rb_tree_model_sort_multi_drag_data_get (EggTreeMultiDragSource *drag_source,
					GList *path_list,
					GtkSelectionData *selection_data)
{
	guint target_info;
	char *drag_data = NULL;
	RBTreeModelSort *model = RB_TREE_MODEL_SORT (drag_source);
	
	/* Check that the items list is not empty and that
	 * the receiver can handle our data.  */
	if (drag_target_list == NULL)
	{
		drag_target_list = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	}

	if (gtk_target_list_find (drag_target_list,
				  selection_data->target,
				  &target_info) == FALSE)
	{
		return FALSE;
	}

	/* Set the appropriate data */
	switch (target_info)
	{
		case RB_LIBRARY_DND_NODE_ID:
		{
			GtkTreeIter iter;
			GtkTreePath *path = gtk_tree_row_reference_get_path (path_list->data);
			RBNode *node = NULL;

			gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
			g_signal_emit (G_OBJECT (model), 
				       rb_tree_model_sort_signals[NODE_FROM_ITER], 
				       0, &iter, &node);

			if (node == NULL)
				return FALSE;

				drag_data = g_strdup_printf ("%ld", rb_node_get_id (node));
		}
		break;

		case RB_LIBRARY_DND_URI_LIST:
		{
			GList *i = NULL;
			for (i = path_list; i != NULL; i = i->next)
			{
				GtkTreeIter iter;
				GtkTreePath *path = gtk_tree_row_reference_get_path (i->data);
				RBNode *node = NULL;
				char *tmp, *tmp2;

				gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
				g_signal_emit (G_OBJECT (model), 
					       rb_tree_model_sort_signals[NODE_FROM_ITER], 
					       0, &iter, &node);

				if (node == NULL)
					return FALSE;


				tmp = rb_node_song_get_location (node);	
				if (drag_data != NULL)
				{
					tmp2 = g_strdup (drag_data);
					g_free (drag_data);
					drag_data = g_strdup_printf ("%s\r\n%s", tmp2, tmp);
					g_free (tmp2);
					g_free (tmp);
				}
				else
					drag_data = tmp;
			}
		}
		break;
	}

	g_free (model->priv->str_list);
	model->priv->str_list = drag_data;
	
	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, drag_data, strlen (drag_data));

	return TRUE;
}
