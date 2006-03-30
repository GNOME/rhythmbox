/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-property-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"

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
static gboolean rb_property_view_popup_menu_cb (GtkTreeView *treeview,
						RBPropertyView *view);
static gboolean rb_property_view_button_press_cb (GtkTreeView *tree,
						  GdkEventButton *event,
						  RBPropertyView *view);

struct RBPropertyViewPrivate
{
	RhythmDB *db;

	RhythmDBPropType propid;

	RhythmDBPropertyModel *prop_model;

	char *title;

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	gboolean draggable;
	gboolean handling_row_deletion;
};

#define RB_PROPERTY_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyViewPrivate))

enum
{
	PROPERTY_SELECTED,
	PROPERTIES_SELECTED,
	PROPERTY_ACTIVATED,
	SELECTION_RESET,
	SHOW_POPUP,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_PROP,
	PROP_TITLE,
	PROP_MODEL,
	PROP_DRAGGABLE,
};

static guint rb_property_view_signals[LAST_SIGNAL] = { 0 };

enum {
	TARGET_ALBUMS,
	TARGET_GENRE,
	TARGET_ARTISTS,
	TARGET_URIS,
};

static const GtkTargetEntry targets_album  [] = {
	{ "text/x-rhythmbox-album",  0, TARGET_ALBUMS },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_genre  [] = {
	{ "text/x-rhythmbox-genre",  0, TARGET_GENRE },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_artist [] = {
	{ "text/x-rhythmbox-artist", 0, TARGET_ARTISTS },
	{ "text/uri-list", 0, TARGET_URIS },
};

G_DEFINE_TYPE (RBPropertyView, rb_property_view, GTK_TYPE_SCROLLED_WINDOW)


static void
rb_property_view_class_init (RBPropertyViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

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
							    RHYTHMDB_TYPE_PROP_TYPE,
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
	g_object_class_install_property (object_class,
					 PROP_DRAGGABLE,
					 g_param_spec_boolean ("draggable",
						 	       "draggable",
							       "is a drag source",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
	rb_property_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, show_popup),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);


	g_type_class_add_private (klass, sizeof (RBPropertyViewPrivate));
}

static void
rb_property_view_init (RBPropertyView *view)
{
	view->priv = RB_PROPERTY_VIEW_GET_PRIVATE (view);
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

	G_OBJECT_CLASS (rb_property_view_parent_class)->finalize (object);
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
			g_object_unref (G_OBJECT (view->priv->prop_model));
		}

		view->priv->prop_model = g_value_get_object (value);

		if (!view->priv->prop_model)
			break;

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (view->priv->prop_model));

		g_object_ref (G_OBJECT (view->priv->prop_model));
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
		
		gtk_tree_selection_unselect_all (view->priv->selection);
		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model), &iter))
			gtk_tree_selection_select_iter (view->priv->selection, &iter);
		g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
						   G_CALLBACK (rb_property_view_selection_changed_cb),
						   view);

	}
		break;
	case PROP_DRAGGABLE:
		view->priv->draggable = g_value_get_boolean (value);
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
	case PROP_DRAGGABLE:
		g_value_set_boolean (value, view->priv->draggable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPropertyView *
rb_property_view_new (RhythmDB *db,
		      guint propid,
		      const char *title)
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
					       "draggable", TRUE,
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
		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model), &first_iter)) {
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
}

guint
rb_property_view_get_num_properties (RBPropertyView *view)
{
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->prop_model),
					       NULL)-1;
}

static void
rb_property_view_cell_data_func (GtkTreeViewColumn *column,
				 GtkCellRenderer *renderer,
				 GtkTreeModel *tree_model,
				 GtkTreeIter *iter,
				 RBPropertyView *view)
{
	char *title;
	char *str;
	guint number;
	gboolean is_all;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &title,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &number, -1);

	if (is_all) {
		int nodes;
		const char *fmt;

		nodes = gtk_tree_model_iter_n_children  (GTK_TREE_MODEL (tree_model), NULL);
		/* Subtract one for the All node */
		nodes--;

		switch (view->priv->propid) {
		case RHYTHMDB_PROP_ARTIST:
			fmt = ngettext ("All %d artist (%d)", "All %d artists (%d)", nodes);
			break;
		case RHYTHMDB_PROP_ALBUM:
			fmt = ngettext ("All %d album (%d)", "All %d albums (%d)", nodes);
			break;
		case RHYTHMDB_PROP_GENRE:
			fmt = ngettext ("All %d genre (%d)", "All %d genres (%d)", nodes);
			break;
		default:
			fmt = _("All %d (%d)");
			break;
		}

		str = g_strdup_printf (fmt, nodes, number);
	} else {
		str = g_strdup_printf (_("%s (%d)"), title, number);
	}

	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", G_UNLIKELY (is_all) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);
	g_free (str);
	g_free (title);
}

static GObject *
rb_property_view_constructor (GType type,
			      guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	RBPropertyView *view;
	RBPropertyViewClass *klass;

	klass = RB_PROPERTY_VIEW_CLASS (g_type_class_peek (RB_TYPE_PROPERTY_VIEW));

	view = RB_PROPERTY_VIEW (G_OBJECT_CLASS (rb_property_view_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	view->priv->prop_model = rhythmdb_property_model_new (view->priv->db, view->priv->propid);
	view->priv->treeview = GTK_WIDGET (gtk_tree_view_new_with_model (GTK_TREE_MODEL (view->priv->prop_model)));

	if (view->priv->draggable)
		rhythmdb_property_model_enable_drag (view->priv->prop_model,
						     GTK_TREE_VIEW (view->priv->treeview));

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
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
				 "popup_menu",
				 G_CALLBACK (rb_property_view_popup_menu_cb),
				 view,
				 0);
	
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "button_press_event",
			         G_CALLBACK (rb_property_view_button_press_cb),
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
	g_return_if_fail (gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->prop_model),
			  &iter, path));

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->prop_model), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &val,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all, -1);

	rb_debug ("emitting property activated");
	g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTY_ACTIVATED], 0,
		       is_all ? NULL : val);
}

void
rb_property_view_set_selection (RBPropertyView *view, const GList *vals)
{
	view->priv->handling_row_deletion = TRUE;
	
	gtk_tree_selection_unselect_all (view->priv->selection);
	
	for (; vals ; vals = vals->next) {
		GtkTreeIter iter;
		
		if (rhythmdb_property_model_iter_from_string (view->priv->prop_model, vals->data, &iter)) {
			GtkTreePath *path;

			gtk_tree_selection_select_iter (view->priv->selection, &iter);
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->prop_model), &iter);
			if (path != NULL) {
				gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview),
							      path, NULL, TRUE,
							      0.5, 0.0);
				gtk_tree_path_free (path);
			}

		}
	}
		
	view->priv->handling_row_deletion = FALSE;
	rb_property_view_selection_changed_cb (view->priv->selection, view);
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
			gtk_tree_model_get (model, &iter,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &selected_prop,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all, -1);
			if (is_all) {
				g_list_free (selected_properties);
				selected_properties = NULL;
				break;
			}
			selected_properties = g_list_prepend (selected_properties,
							     g_strdup (selected_prop));
		}

		g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (selected_rows);

		if (is_all) {
			g_signal_handlers_block_by_func (G_OBJECT (view->priv->selection),
							 G_CALLBACK (rb_property_view_selection_changed_cb),
							 view);
			gtk_tree_selection_unselect_all (selection);
			if (gtk_tree_model_get_iter_first (model, &iter))
				gtk_tree_selection_select_iter (selection, &iter);
			g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
							   G_CALLBACK (rb_property_view_selection_changed_cb),
							   view);
		}
		g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTIES_SELECTED], 0,
			       selected_properties);
		rb_list_deep_free (selected_properties);
	} else {
		if (gtk_tree_selection_get_selected (view->priv->selection, &model, &iter)) {
			gtk_tree_model_get (model, &iter,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &selected_prop,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all, -1);
			g_signal_emit (G_OBJECT (view), rb_property_view_signals[PROPERTY_SELECTED], 0,
				       is_all ? NULL : selected_prop);
		}
	}
}

static gboolean 
rb_property_view_popup_menu_cb (GtkTreeView *treeview,
				RBPropertyView *view)
{
	g_signal_emit (G_OBJECT (view), rb_property_view_signals[SHOW_POPUP], 0);
	return TRUE;
}

void		
rb_property_view_append_column_custom (RBPropertyView *view,
				       GtkTreeViewColumn *column)
{
	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);
}

static gboolean
rb_property_view_button_press_cb (GtkTreeView *tree,
				  GdkEventButton *event,
				  RBPropertyView *view)
{

	if (event->button == 3) {
		GtkTreeSelection *selection;
		GtkTreePath *path;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));

		gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (view->priv->treeview), event->x, event->y, &path, NULL, NULL, NULL);
		if (path == NULL) {
			gtk_tree_selection_unselect_all (selection);
		} else {
			GtkTreeModel *model;
			GtkTreeIter iter;
			const char *val;
			GList *lst = NULL;
			
			model = gtk_tree_view_get_model (GTK_TREE_VIEW (view->priv->treeview));
			if (gtk_tree_model_get_iter (model, &iter, path)) {
				gtk_tree_model_get (model, &iter, 0, &val, -1);
				lst = g_list_prepend (lst, (gpointer) val);
				rb_property_view_set_selection (view, lst);
			}
		}
		g_signal_emit (G_OBJECT (view), rb_property_view_signals[SHOW_POPUP], 0);
		return TRUE;
	}

	return FALSE;
}

void
rb_property_view_set_search_func (RBPropertyView *view,
				  GtkTreeViewSearchEqualFunc func,
				  gpointer func_data,
				  GtkDestroyNotify notify)
{
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view->priv->treeview),
					     func, func_data,
					     notify);
}


