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

#include <string.h>
#include "rb-node.h"
#include "rb-node-song.h"
#include "rb-tree-model-sort.h"
#include "eggtreemultidnd.h"
#include "eggtreemodelfilter.h"
#include "rb-tree-model-node.h"

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
/* dnd */
static const GtkTargetEntry target_table[] = { { "text/uri-list", 0, 0 }, };

static GObjectClass  *parent_class = NULL;
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
}

static void
rb_tree_model_sort_init (RBTreeModelSort *ma)
{
}

static void
rb_tree_model_sort_finalize (GObject *object)
{
	RBTreeModelSort *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TREE_MODEL_SORT (object));

	model = RB_TREE_MODEL_SORT (object);

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
	iface->row_draggable = rb_tree_model_sort_multi_row_draggable;
	iface->drag_data_get = rb_tree_model_sort_multi_drag_data_get;
	iface->drag_data_delete = rb_tree_model_sort_multi_drag_data_delete;
}


static gboolean
rb_tree_model_sort_multi_row_draggable (EggTreeMultiDragSource *drag_source, GList *path_list)
{
	return TRUE;
}


static gboolean
rb_tree_model_sort_multi_drag_data_get (EggTreeMultiDragSource *drag_source,
					GList *path_list,
					GtkSelectionData *selection_data)
{
	guint target_info;
	GList *i;
	GtkTreeModelSort *sort_model = NULL;
	GtkTreeModel *filter_model = NULL;
	GtkTreeModel *node_model = NULL;
	char *uri = "";


	if (!drag_target_list) 
	{
		drag_target_list = gtk_target_list_new (target_table, 1);
	}

	if (!gtk_target_list_find (drag_target_list,
				   selection_data->target,
				   &target_info)) 
	{
		return FALSE;
	}
		

	/* ok, let's start retrieving the uris of the selection */
	sort_model = GTK_TREE_MODEL_SORT (drag_source);
	filter_model = gtk_tree_model_sort_get_model (sort_model);
	node_model = egg_tree_model_filter_get_model (EGG_TREE_MODEL_FILTER (filter_model));

	g_return_val_if_fail ((filter_model != NULL) && (node_model != NULL), FALSE);

	for (i = path_list; i != NULL; i = i->next)
	{
		RBNode *node = NULL;
		GtkTreeIter filter_iter, node_iter, sorted_iter;
		GtkTreePath *path = gtk_tree_row_reference_get_path (i->data);
		
		/* Real stuff comes here: we have a path that we must transform into
		 * a GtkTreeIter. When it's done, we can get through the models to
		 * finally get the node and the uri */
		gtk_tree_model_get_iter (GTK_TREE_MODEL (sort_model), &sorted_iter, path);
		gtk_tree_model_sort_convert_iter_to_child_iter (sort_model,
								&filter_iter, &sorted_iter);
		egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (filter_model),
								  &node_iter, &filter_iter);
		node = rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (node_model), &node_iter);

		if ((node != NULL) && (RB_IS_NODE (node)))
		{
			char *tmp =  rb_node_song_get_location (node);
			uri = g_strdup_printf ("%s\r\n%s", uri, tmp);
			g_free (tmp);
		}
	}

	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, uri, strlen (uri));
	
	return TRUE;

}

static gboolean
rb_tree_model_sort_multi_drag_data_delete (EggTreeMultiDragSource *drag_source, 
					   GList *path_list)
{
	return TRUE;
}
