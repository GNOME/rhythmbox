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

#include "rb-library-dnd-types.h"
#include "rb-sourcelist-model.h"
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
static void rb_sourcelist_model_drag_dest_init (GtkTreeDragDestIface *iface);
static void rb_sourcelist_model_finalize (GObject *object);
static gboolean rb_sourcelist_model_drag_data_received (GtkTreeDragDest *drag_dest,
							GtkTreePath *dest,
							GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_drop_possible (GtkTreeDragDest *drag_dest,
						       GtkTreePath *dest,
						       GtkSelectionData *selection_data);

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

		rb_sourcelist_model_type = g_type_register_static (GTK_TYPE_LIST_STORE, "RBSourceListModel",
								   &rb_sourcelist_model_info, 0);
		g_type_add_interface_static (rb_sourcelist_model_type,
					     GTK_TYPE_TREE_DRAG_DEST,
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
			      gtk_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);
}

static void
rb_sourcelist_model_drag_dest_init (GtkTreeDragDestIface *iface)
{
  iface->drag_data_received = rb_sourcelist_model_drag_data_received;
  iface->row_drop_possible = rb_sourcelist_model_row_drop_possible;
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
rb_sourcelist_model_drag_data_received (GtkTreeDragDest *drag_dest,
					GtkTreePath *dest,
					GtkSelectionData *selection_data)
{
	RBSourceListModel *model;

	rb_debug ("drag data received");
	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);
	
	if (selection_data->type == gdk_atom_intern (RB_LIBRARY_DND_URI_LIST_TYPE, TRUE)
	    || selection_data->type == gdk_atom_intern (RB_LIBRARY_DND_NODE_ID_TYPE, TRUE)) {
		GtkTreeIter iter;
		RBSource *target;
		rb_debug ("matched dnd type");
		
		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest))
			target = NULL;
		else
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		
		g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
			       0, target, selection_data);
		return TRUE;
	}

	return FALSE;
}

static gboolean
rb_sourcelist_model_row_drop_possible (GtkTreeDragDest *drag_dest,
				       GtkTreePath *dest,
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
