/*
 *  arch-tag: Implementation of widget to display RhythmDB properties
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtktreeview.h>

#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtktooltips.h>
#include <gdk/gdkkeysyms.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <string.h>
#include <stdlib.h>

#include "rb-property-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-string-helpers.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

static void rb_property_view_class_init (RBPropertyViewClass *klass);
static void rb_property_view_init (RBPropertyView *view);
static void rb_property_view_finalize (GObject *object);
static void rb_property_view_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_property_view_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static GObject * rb_property_view_constructor (GType type, guint n_construct_properties,
					       GObjectConstructParam *construct_properties);
static void rb_property_view_row_activated_cb (GtkTreeView *treeview,
			                   GtkTreePath *path,
			                   GtkTreeViewColumn *column,
			                   RBPropertyView *view);
static void rb_property_view_selection_changed_cb (GtkTreeSelection *selection,
						   RBPropertyView *view);
static void rb_property_view_pre_row_deleted_cb (RhythmDBPropertyModel *model,
						 RBPropertyView *view);
static void rb_property_view_post_row_deleted_cb (GtkTreeModel *model,
						  GtkTreePath *path,
						  RBPropertyView *view);

struct RBPropertyViewPrivate
{
	RhythmDB *db;

	RhythmDBPropType propid;

	GPtrArray *query;
	
	RhythmDBPropertyModel *prop_model;

	char *title;

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	gboolean handling_row_deletion;

	guint refresh_idle_id;
};

enum
{
	PROPERTY_SELECTED,
	PROPERTIES_SELECTED,
	PROPERTY_ACTIVATED,
	SELECTION_RESET,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_PROP,
	PROP_TITLE,
	PROP_MODEL,
};

static GObjectClass *parent_class = NULL;

static guint rb_property_view_signals[LAST_SIGNAL] = { 0 };

GType
rb_property_view_get_type (void)
{
	static GType rb_property_view_type = 0;

	if (rb_property_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPropertyViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_property_view_class_init,
			NULL,
			NULL,
			sizeof (RBPropertyView),
			0,
			(GInstanceInitFunc) rb_property_view_init
		};
		
		rb_property_view_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW,
								"RBPropertyView",
								&our_info, 0);
	}

	return rb_property_view_type;
}

static void
rb_property_view_class_init (RBPropertyViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_property_view_finalize;
	object_class->constructor = rb_property_view_constructor;

	object_class->set_property = rb_property_view_set_property;
	object_class->get_property = rb_property_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_PROP,
					 g_param_spec_enum ("prop",
							    "PropertyId",
							    "RhythmDBPropType",
							    RHYTHMDB_TYPE_PROP,
							    RHYTHMDB_PROP_TYPE,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "title",
							      "title",
							      "",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("property-model",
							      "property model",
							      "RhythmDBPropertyModel",
							      RHYTHMDB_TYPE_PROPERTY_MODEL,
							      G_PARAM_READWRITE));

	rb_property_view_signals[PROPERTY_ACTIVATED] =
		g_signal_new ("property-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, property_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	rb_property_view_signals[PROPERTY_SELECTED] =
		g_signal_new ("property-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, property_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	rb_property_view_signals[PROPERTIES_SELECTED] =
		g_signal_new ("properties-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, properties_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);

	rb_property_view_signals[SELECTION_RESET] =
		g_signal_new ("property-selection-reset",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, selection_reset),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0); 
}

static void
rb_property_view_init (RBPropertyView *view)
{
	view->priv = g_new0 (RBPropertyViewPrivate, 1);
}

static void
rb_property_view_finalize (GObject *object)
{
	RBPropertyView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PROPERTY_VIEW (object));

	view = RB_PROPERTY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv->title);
	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
rb_property_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBPropertyView *view = RB_PROPERTY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		view->priv->db = g_value_get_object (value);
		break;
	case PROP_PROP:
		view->priv->propid = g_value_get_enum (value);
		break;
	case PROP_TITLE:
		view->priv->title = g_value_dup_string (value);
		break;
	case PROP_MODEL:
	{
		GtkTreeIter iter;

		if (view->priv->prop_model) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (view->priv->prop_model),
							      G_CALLBACK (rb_property_view_pre_row_deleted_cb),
							      view);
			g_signal_handlers_disconnect_by_func (G_OBJECT (view->priv->prop_model),
							      G_CALLBACK (rb_property_view_post_row_deleted_cb),
							      view);
		}

		view->priv->prop_model = g_value_get_object (value);

		if (!view->priv->prop_model)
			break;

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (view->priv->prop_model));

		g_signal_connect_object (G_OBJECT (view->priv->prop_model),
					 "pre-row-deletion",
					 G_CALLBACK (rb_property_view_pre_row_deleted_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (view->priv->prop_model),
					 "row_deleted",
					 G_CALLBACK (rb_property_view_post_row_deleted_cb),
					 view,
					 G_CONNECT_AFTER);

		g_signal_handlers_block_by_func (G_OBJECT (view->priv->selection),
						 G_CALLBACK (rb_property_view_selection_changed_cb),
						 view);
		
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model), &iter);
		gtk_tree_selection_unselect_all (view->priv->selection);
		gtk_tree_selection_select_iter (view->priv->selection, &iter);
		g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
						   G_CALLBACK (rb_property_view_selection_changed_cb),
						   view);

	}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_property_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBPropertyView *view = RB_PROPERTY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, view->priv->db);
		break;
	case PROP_PROP:
		g_value_set_enum (value, view->priv->propid);
		break;
	case PROP_TITLE:
		g_value_set_string (value, view->priv->title);
		break;
	case PROP_MODEL:
		g_value_set_object (value, view->priv->prop_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPropertyView *
rb_property_view_new (RhythmDB *db, guint propid, const char *title)
{
	RBPropertyView *view;

	view = RB_PROPERTY_VIEW (g_object_new (RB_TYPE_PROPERTY_VIEW,
					       "hadjustment", NULL,
					       "vadjustment", NULL,
					       "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					       "vscrollbar_policy", GTK_POLICY_ALWAYS,
					       "shadow_type", GTK_SHADOW_IN,
					       "db", db,
					       "prop", propid,
					       "title", title,
					       NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

void
rb_property_view_set_selection_mode (RBPropertyView *view,
				     GtkSelectionMode mode)
{
	g_return_if_fail (mode == GTK_SELECTION_SINGLE || mode == GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview)),
				     mode);
}

void
rb_property_view_reset (RBPropertyView *view)
{
	RhythmDBPropertyModel *model;
	
	model = rhythmdb_property_model_new (view->priv->db, view->priv->propid);

	g_object_set (G_OBJECT (view), "property-model", model, NULL);
	g_object_unref (G_OBJECT (model));
}

RhythmDBPropertyModel *
rb_property_view_get_model (RBPropertyView *view)
{
	RhythmDBPropertyModel *model;
	
	g_object_get (G_OBJECT (view), "property-model", &model, NULL);
	return model;
}

void
rb_property_view_set_model (RBPropertyView *view, RhythmDBPropertyModel *model)
{
	g_object_set (G_OBJECT (view), "property-model", model, NULL);
}

static void
rb_property_view_pre_row_deleted_cb (RhythmDBPropertyModel *model,
				     RBPropertyView *view)
{
	view->priv->handling_row_deletion = TRUE;
	rb_debug ("pre row deleted");
}

static void
rb_property_view_post_row_deleted_cb (GtkTreeModel *model,
				      GtkTreePath *path,
				      RBPropertyView *view)
{
	view->priv->handling_row_deletion = FALSE;
	rb_debug ("post row deleted");
	if (gtk_tree_selection_count_selected_rows (view->priv->selection) == 0) {
		GtkTreeIter first_iter;
		rb_debug ("no rows selected, signalling reset");
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model),
					       &first_iter);
		g_signal_handlers_block_by_func (G_OBJECT (view->priv->selection),
						 G_CALLBACK (rb_property_view_selection_changed_cb),
						 view);
		gtk_tree_selection_select_iter (view->priv->selection, &first_iter);
		g_signal_emit (G_OBJECT (view), rb_property_view_signals[SELECTION_RESET], 0);
		g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
						   G_CALLBACK (rb_property_view_selection_changed_cb),
						   view);
	}
}

guint
rb_property_view_get_num_properties (RBPropertyView *view)
{
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->prop_model),
					       NULL)-1;
}

static void
rb_property_view_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				 GtkTreeModel *tree_model, GtkTreeIter *iter,
				 RBPropertyView *view)
{
	char *str;
	gboolean bold;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &str,
			    1, &bold, -1);
	
	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", G_UNLIKELY (bold) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);
	g_free (str);
}


static GObject *
rb_property_view_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	RBPropertyView *view;
	RBPropertyViewClass *klass;
	GObjectClass *parent_class;  

	klass = RB_PROPERTY_VIEW_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	view = RB_PROPERTY_VIEW (parent_class->constructor (type, n_construct_properties,
							    construct_properties));

	view->priv->prop_model = rhythmdb_property_model_new (view->priv->db, view->priv->propid);
	view->priv->treeview = GTK_WIDGET (gtk_tree_view_new_with_model (GTK_TREE_MODEL (view->priv->prop_model)));
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view->priv->treeview), FALSE);
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "row_activated",
			         G_CALLBACK (rb_property_view_row_activated_cb),
			         view,
				 0);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect_object (G_OBJECT (view->priv->selection),
			         "changed",
			         G_CALLBACK (rb_property_view_selection_changed_cb),
			         view,
				 0);

	gtk_container_add (GTK_CONTAINER (view), view->priv->treeview);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_selection_set_mode (view->priv->selection, GTK_SELECTION_SINGLE);
	
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_property_view_cell_data_func,
						 view, NULL);
	gtk_tree_view_column_set_title (column, view->priv->title);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview),
				     column);

	return G_OBJECT (view);
}

static void
rb_property_view_row_activated_cb (GtkTreeView *treeview,
				   GtkTreePath *path,
				   GtkTreeViewColumn *column,
				   RBPropertyView *view)
{
	GtkTreeIter iter;
	const char *val;
	gboolean is_all;

	rb_debug ("row activated");
	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->prop_model), &iter, path);

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->prop_model), &iter, 0,
			    &val, 1, &is_all, -1);

	rb_debug ("emitting property activated");
	g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTY_ACTIVATED], 0,
		       is_all ? NULL : val);
}

static void
rb_property_view_selection_changed_cb (GtkTreeSelection *selection,
				       RBPropertyView *view)
{
	const char *selected_prop;
	gboolean is_all;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (view->priv->handling_row_deletion)
		return;

	rb_debug ("selection changed");
	if (gtk_tree_selection_get_mode (selection) == GTK_SELECTION_MULTIPLE) {
		GList *selected_rows, *tem;
		GList *selected_properties = NULL;
		
		selected_rows = gtk_tree_selection_get_selected_rows (view->priv->selection, &model);
		for (tem = selected_rows; tem; tem = tem->next) {
			g_assert (gtk_tree_model_get_iter (model, &iter, tem->data));
			gtk_tree_model_get (model, &iter, 0, &selected_prop, 1, &is_all, -1);
			if (is_all) {
				g_list_free (selected_properties);
				selected_properties = NULL;
				break;
			}
			selected_properties = g_list_append (selected_properties,
							     g_strdup (selected_prop));
		}

		g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (selected_rows);

		if (is_all) {
			g_signal_handlers_block_by_func (G_OBJECT (view->priv->selection),
							 G_CALLBACK (rb_property_view_selection_changed_cb),
							 view);
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_model_get_iter_first (model, &iter);
			gtk_tree_selection_select_iter (selection, &iter);
			g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
							   G_CALLBACK (rb_property_view_selection_changed_cb),
							   view);
		}
		g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTIES_SELECTED], 0,
			       selected_properties);
	} else {
		if (gtk_tree_selection_get_selected (view->priv->selection, &model, &iter)) {
			gtk_tree_model_get (model, &iter, 0, &selected_prop, 1, &is_all, -1);
			g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTY_SELECTED], 0,
				       is_all ? NULL : selected_prop);
		}
	}
}
