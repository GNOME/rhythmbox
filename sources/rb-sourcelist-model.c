/*
 *  arch-tag: Implementation of GtkTreeModel iface containing RBSource objects
 *
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <libgnome/gnome-i18n.h>

#include "rb-sourcelist-model.h"
#include "rb-tree-dnd.h"
#include "rb-debug.h"
#include "rb-marshal.h"

struct RBSourceListModelPriv
{
};

enum
{
	DROP_RECEIVED,
	LAST_SIGNAL
};

static void rb_sourcelist_model_class_init (RBSourceListModelClass *klass);
static void rb_sourcelist_model_init (RBSourceListModel *model);
static void rb_sourcelist_model_drag_dest_init (RbTreeDragDestIface *iface);
static void rb_sourcelist_model_drag_source_init (RbTreeDragSourceIface *iface);
static void rb_sourcelist_model_finalize (GObject *object);
static gboolean rb_sourcelist_model_drag_data_received (RbTreeDragDest *drag_dest,
							GtkTreePath *dest,
							GtkTreeViewDropPosition pos,
							GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_drop_possible (RbTreeDragDest *drag_dest,
						       GtkTreePath *dest,
						       GtkTreeViewDropPosition pos,
						       GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_drop_position (RbTreeDragDest   *drag_dest,
						       GtkTreePath       *dest_path,
						       GList *targets,
						       GtkTreeViewDropPosition *pos);
static gboolean rb_sourcelist_model_drag_data_delete (RbTreeDragSource *drag_source,
						      GList *path_list);
static gboolean rb_sourcelist_model_drag_data_get (RbTreeDragSource *drag_source,
						   GList *path_list,
						   GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_draggable (RbTreeDragSource *drag_source,
						   GList *path_list);


static GtkListStoreClass *parent_class = NULL;

static guint rb_sourcelist_model_signals[LAST_SIGNAL] = { 0 };

GType
rb_sourcelist_model_get_type (void)
{
	static GType rb_sourcelist_model_type = 0;

	if (!rb_sourcelist_model_type)
	{
		static const GTypeInfo rb_sourcelist_model_info = {
			sizeof (RBSourceListModelClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) rb_sourcelist_model_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (RBSourceListModel),
			0,              /* n_preallocs */
			(GInstanceInitFunc) rb_sourcelist_model_init
		};

		static const GInterfaceInfo drag_dest_info = {
			(GInterfaceInitFunc) rb_sourcelist_model_drag_dest_init,
			NULL,
			NULL
		};


		static const GInterfaceInfo drag_source_info = {
			(GInterfaceInitFunc) rb_sourcelist_model_drag_source_init,
			NULL,
			NULL
		};

		rb_sourcelist_model_type = g_type_register_static (GTK_TYPE_LIST_STORE, "RBSourceListModel",
								   &rb_sourcelist_model_info, 0);
		g_type_add_interface_static (rb_sourcelist_model_type,
					     RB_TYPE_TREE_DRAG_SOURCE,
					     &drag_source_info);
		g_type_add_interface_static (rb_sourcelist_model_type,
					     RB_TYPE_TREE_DRAG_DEST,
					     &drag_dest_info);
	}
	
	return rb_sourcelist_model_type;
}

static void
rb_sourcelist_model_class_init (RBSourceListModelClass *class)
{
	GObjectClass   *o_class;
	GtkObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);

	o_class = (GObjectClass *) class;
	object_class = (GtkObjectClass *) class;

	o_class->finalize = rb_sourcelist_model_finalize;

	rb_sourcelist_model_signals[DROP_RECEIVED] =
		g_signal_new ("drop_received",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListModelClass, drop_received),
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_INT_POINTER,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_POINTER, G_TYPE_INT, G_TYPE_POINTER);
}

static void
rb_sourcelist_model_drag_dest_init (RbTreeDragDestIface *iface)
{
  iface->drag_data_received = rb_sourcelist_model_drag_data_received;
  iface->row_drop_possible = rb_sourcelist_model_row_drop_possible;
  iface->row_drop_position = rb_sourcelist_model_row_drop_position;

}

static void
rb_sourcelist_model_drag_source_init (RbTreeDragSourceIface *iface)
{
  iface->row_draggable = rb_sourcelist_model_row_draggable;
  iface->drag_data_get = rb_sourcelist_model_drag_data_get;
  iface->drag_data_delete = rb_sourcelist_model_drag_data_delete;

}

static void
rb_sourcelist_model_init (RBSourceListModel *model)
{
	model->priv = g_new0 (RBSourceListModelPriv, 1);
}

static void
rb_sourcelist_model_finalize (GObject *object)
{
	RBSourceListModel *model = RB_SOURCELIST_MODEL (object);

	g_free (model->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

GtkTreeModel *
rb_sourcelist_model_new (void)
{
	RBSourceListModel *model;
	GType *column_types = g_new (GType, 4);
	column_types[0] = GDK_TYPE_PIXBUF;
	column_types[1] = G_TYPE_STRING;
	column_types[2] = G_TYPE_POINTER;
	column_types[3] = PANGO_TYPE_ATTR_LIST;

	model = RB_SOURCELIST_MODEL (g_object_new (RB_TYPE_SOURCELIST_MODEL, NULL));
	gtk_list_store_set_column_types (GTK_LIST_STORE (model), 4, column_types);
	g_free (column_types);
	return GTK_TREE_MODEL (model);
}

static gboolean
rb_sourcelist_model_drag_data_received (RbTreeDragDest *drag_dest,
					GtkTreePath *dest,
					GtkTreeViewDropPosition pos,
					GtkSelectionData *selection_data)
{
	RBSourceListModel *model;

	rb_debug ("drag data received");
	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);
	
	if (selection_data->type == gdk_atom_intern ("text/uri-list", TRUE)) {
		GtkTreeIter iter;
		RBSource *target;
		rb_debug ("matched dnd type");
		
		if (dest == NULL || !gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest))
			target = NULL;
		else
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		
		g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
			       0, target, pos, selection_data);
		return TRUE;
	}

	return FALSE;
}

static gboolean
rb_sourcelist_model_row_drop_possible (RbTreeDragDest *drag_dest,
				       GtkTreePath *dest,
				       GtkTreeViewDropPosition pos,
				       GtkSelectionData *selection_data)
{
	RBSourceListModel *model;

	rb_debug ("row drop possible");
	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);

	/* Call the superclass method */
	return gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (GTK_LIST_STORE (model)),
						     dest, selection_data);
}


static gboolean
rb_sourcelist_model_row_drop_position (RbTreeDragDest   *drag_dest,
				       GtkTreePath       *dest_path,
				       GList *targets,
				       GtkTreeViewDropPosition *pos)
{
	GtkTreeIter iter;
	GtkTreeModel *model = GTK_TREE_MODEL (drag_dest);
	
	if (gtk_tree_model_get_iter (model, &iter, dest_path)) {
		RBSource *source;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

		if (!rb_source_can_rename (source)) {

			if (*pos == GTK_TREE_VIEW_DROP_BEFORE)
				*pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
			else if (*pos == GTK_TREE_VIEW_DROP_AFTER)
				*pos = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;

			return TRUE;

		} else {

			if (g_list_find (targets, gdk_atom_intern ("text/uri-list", TRUE)))
				return TRUE;
		}
	}
		
	return FALSE;
}



static gboolean
rb_sourcelist_model_row_draggable (RbTreeDragSource *drag_source,
				   GList *path_list)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model = GTK_TREE_MODEL (drag_source);
	
	/* we don't support multi selection */
	g_return_val_if_fail (g_list_length (path_list) == 1, FALSE);

	path = g_list_first (path_list)->data;

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		RBSource *source;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

		return rb_source_can_rename (source);
	}
		
	return FALSE;
}

static gboolean
rb_sourcelist_model_drag_data_get (RbTreeDragSource *drag_source,
				   GList *path_list,
				   GtkSelectionData *selection_data)
{
	return FALSE;
}

static gboolean
rb_sourcelist_model_drag_data_delete (RbTreeDragSource *drag_source,
				      GList *path_list)
{
	return FALSE;
}
