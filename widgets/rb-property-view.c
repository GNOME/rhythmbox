/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "rb-property-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-util.h"

static void rb_property_view_class_init (RBPropertyViewClass *klass);
static void rb_property_view_init (RBPropertyView *view);
static void rb_property_view_dispose (GObject *object);
static void rb_property_view_finalize (GObject *object);
static void rb_property_view_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec);
static void rb_property_view_get_property (GObject *object,
					   guint prop_id,
					   GValue *value,
					   GParamSpec *pspec);
static void rb_property_view_constructed (GObject *object);
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
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	gboolean draggable;
	gboolean handling_row_deletion;
	guint update_selection_id;
};

#define RB_PROPERTY_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyViewPrivate))

/**
 * SECTION:rbpropertyview
 * @short_description: a #GtkTreeView backed by a #RhythmDBPropertyModel
 *
 * A simple #GtkTreeView that displays the contents of a #RhythmDBPropertyModel.
 * The first row in the tree view displays the total number of properties and entries,
 * in the form "All 473 artists (6241)".  Each subsequent row in the tree view 
 * displays a property value and the number of entries from the #RhythmDBQueryModel
 * with that value.
 *
 * The property view itself creates a single column, but additional columns can be
 * added.
 */

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

G_DEFINE_TYPE (RBPropertyView, rb_property_view, GTK_TYPE_SCROLLED_WINDOW)

static void
rb_property_view_class_init (RBPropertyViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_property_view_dispose;
	object_class->finalize = rb_property_view_finalize;
	object_class->constructed = rb_property_view_constructed;

	object_class->set_property = rb_property_view_set_property;
	object_class->get_property = rb_property_view_get_property;

	/**
	 * RBPropertyView:db:
	 *
	 * #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBPropertyView:prop:
	 *
	 * The property that is displayed in this view
	 */
	g_object_class_install_property (object_class,
					 PROP_PROP,
					 g_param_spec_enum ("prop",
							    "PropertyId",
							    "RhythmDBPropType",
							    RHYTHMDB_TYPE_PROP_TYPE,
							    RHYTHMDB_PROP_TYPE,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBPropertyView:title:
	 * 
	 * The title displayed in the header of the property view
	 */
	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "title",
							      "title",
							      "",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBPropertyView:property-model:
	 *
	 * The #RhythmDBPropertyModel backing the view.
	 */
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("property-model",
							      "property model",
							      "RhythmDBPropertyModel",
							      RHYTHMDB_TYPE_PROPERTY_MODEL,
							      G_PARAM_READWRITE));
	/**
	 * RBPropertyView:draggable:
	 *
	 * Whether the property view acts as a data source for drag and drop operations.
	 */
	g_object_class_install_property (object_class,
					 PROP_DRAGGABLE,
					 g_param_spec_boolean ("draggable",
						 	       "draggable",
							       "is a drag source",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBPropertyView::property-activated:
	 * @view: the #RBPropertyView
	 * @name: the property value that was activated
	 *
	 * Emitted when a row in a property view is activated by double clicking.
	 */
	rb_property_view_signals[PROPERTY_ACTIVATED] =
		g_signal_new ("property-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, property_activated),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBPropertyView::property-selected:
	 * @view: the #RBPropertyView
	 * @name: the property value that has been selected
	 *
	 * Emitted when an individual property value becomes selected.  This is only
	 * emitted for single-selection property views.  For multiple-selection views,
	 * use the properties-selected signal.
	 */
	rb_property_view_signals[PROPERTY_SELECTED] =
		g_signal_new ("property-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, property_selected),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBPropertyView::properties-selected:
	 * @view: the #RBPropertyView
	 * @properties: (element-type utf8): a list containing the selected property values
	 *
	 * Emitted when the set of selected property values changes.  This is only
	 * emitted for multiple selection property views.  For single-selection views,
	 * use the property-selected signal.
	 */
	rb_property_view_signals[PROPERTIES_SELECTED] =
		g_signal_new ("properties-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, properties_selected),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);

	/**
	 * RBPropertyView::property-selection-reset:
	 * @view: the #RBPropertyView
	 *
	 * Emitted when the selection is reset.  At this point, no property values
	 * are selected.
	 */
	rb_property_view_signals[SELECTION_RESET] =
		g_signal_new ("property-selection-reset",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, selection_reset),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBPropertyView::show-popup:
	 * @view: the #RBPropertyView
	 *
	 * Emitted when a popup menu should be displayed for the property view.
	 * The source containing the property view should connect a handler to
	 * this signal that * displays an appropriate popup.
	 */
	rb_property_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPropertyViewClass, show_popup),
			      NULL, NULL,
			      NULL,
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
rb_property_view_set_model_internal (RBPropertyView *view,
				     RhythmDBPropertyModel *model)
{
	if (view->priv->prop_model != NULL) {
		g_signal_handlers_disconnect_by_func (view->priv->prop_model,
						      G_CALLBACK (rb_property_view_pre_row_deleted_cb),
						      view);
		g_signal_handlers_disconnect_by_func (view->priv->prop_model,
						      G_CALLBACK (rb_property_view_post_row_deleted_cb),
						      view);
		g_object_unref (view->priv->prop_model);
	}

	view->priv->prop_model = model;

	if (view->priv->prop_model != NULL) {
		GtkTreeIter iter;

		g_object_ref (view->priv->prop_model);

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (view->priv->prop_model));

		g_signal_connect_object (view->priv->prop_model,
					 "pre-row-deletion",
					 G_CALLBACK (rb_property_view_pre_row_deleted_cb),
					 view,
					 0);
		g_signal_connect_object (view->priv->prop_model,
					 "row_deleted",
					 G_CALLBACK (rb_property_view_post_row_deleted_cb),
					 view,
					 G_CONNECT_AFTER);

		g_signal_handlers_block_by_func (view->priv->selection,
						 G_CALLBACK (rb_property_view_selection_changed_cb),
						 view);

		gtk_tree_selection_unselect_all (view->priv->selection);

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model), &iter))
			gtk_tree_selection_select_iter (view->priv->selection, &iter);

		g_signal_handlers_unblock_by_func (view->priv->selection,
						   G_CALLBACK (rb_property_view_selection_changed_cb),
						   view);
	}
}

static void
rb_property_view_dispose (GObject *object)
{
	RBPropertyView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PROPERTY_VIEW (object));

	view = RB_PROPERTY_VIEW (object);

	if (view->priv->update_selection_id != 0) {
		g_source_remove (view->priv->update_selection_id);
		view->priv->update_selection_id = 0;
	}

	rb_property_view_set_model_internal (view, NULL);

	G_OBJECT_CLASS (rb_property_view_parent_class)->dispose (object);
}

static void
rb_property_view_finalize (GObject *object)
{
	RBPropertyView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PROPERTY_VIEW (object));

	view = RB_PROPERTY_VIEW (object);

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

	switch (prop_id) {
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
		rb_property_view_set_model_internal (view, g_value_get_object (value));
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

	switch (prop_id) {
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

/**
 * rb_property_view_new:
 * @db: #RhythmDB instance
 * @propid: property ID to be displayed in the property view
 * @title: title of the property view
 *
 * Creates a new #RBPropertyView displaying the specified property.
 *
 * Return value: new property view instance
 */
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
					       "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
					       "hexpand", TRUE,
					       "vexpand", TRUE,
					       "shadow_type", GTK_SHADOW_NONE,
					       "db", db,
					       "prop", propid,
					       "title", title,
					       "draggable", TRUE,
					       NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

/**
 * rb_property_view_set_selection_mode:
 * @view: a #RBPropertyView
 * @mode: the new #GtkSelectionMode for the property view
 *
 * Sets the selection mode (single or multiple) for the property view>
 * The default selection mode is single.
 */
void
rb_property_view_set_selection_mode (RBPropertyView *view,
				     GtkSelectionMode mode)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));
	g_return_if_fail (mode == GTK_SELECTION_SINGLE || mode == GTK_SELECTION_MULTIPLE);

	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview)),
				     mode);
}

/**
 * rb_property_view_reset:
 * @view: a #RBPropertyView
 *
 * Clears the selection in the property view.
 */
void
rb_property_view_reset (RBPropertyView *view)
{
	RhythmDBPropertyModel *model;

	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));

	model = rhythmdb_property_model_new (view->priv->db, view->priv->propid);

	rb_property_view_set_model_internal (view, model);
	g_object_unref (model);
}

/**
 * rb_property_view_get_model:
 * @view: a #RBPropertyView
 * 
 * Returns the #RhythmDBPropertyModel backing the view; no reference is taken
 *
 * Return value: (transfer none): property model
 */
RhythmDBPropertyModel *
rb_property_view_get_model (RBPropertyView *view)
{
	RhythmDBPropertyModel *model;

	g_return_val_if_fail (RB_IS_PROPERTY_VIEW (view), NULL);

	model = view->priv->prop_model;

	return model;
}

/**
 * rb_property_view_set_model:
 * @view: a #RBPropertyView
 * @model: the new #RhythmDBPropertyModel for the property view
 *
 * Replaces the model backing the property view.
 */
void
rb_property_view_set_model (RBPropertyView *view,
			    RhythmDBPropertyModel *model)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));

	rb_property_view_set_model_internal (view, model);
}

static void
rb_property_view_pre_row_deleted_cb (RhythmDBPropertyModel *model,
				     RBPropertyView *view)
{
	view->priv->handling_row_deletion = TRUE;
	rb_debug ("pre row deleted");
}

static gboolean
update_selection_cb (RBPropertyView *view)
{
	if (gtk_tree_selection_count_selected_rows (view->priv->selection) == 0) {
		GtkTreeIter first_iter;

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->prop_model), &first_iter)) {
			rb_debug ("no rows selected, signalling reset");
			g_signal_handlers_block_by_func (G_OBJECT (view->priv->selection),
							 G_CALLBACK (rb_property_view_selection_changed_cb),
							 view);
			gtk_tree_selection_select_iter (view->priv->selection, &first_iter);
			g_signal_emit (G_OBJECT (view), rb_property_view_signals[SELECTION_RESET], 0);
			g_signal_handlers_unblock_by_func (G_OBJECT (view->priv->selection),
							   G_CALLBACK (rb_property_view_selection_changed_cb),
							   view);
		}
	} else {
		rb_property_view_selection_changed_cb (view->priv->selection, view);
	}

	view->priv->update_selection_id = 0;
	return FALSE;
}

static void
rb_property_view_post_row_deleted_cb (GtkTreeModel *model,
				      GtkTreePath *path,
				      RBPropertyView *view)
{
	view->priv->handling_row_deletion = FALSE;
	rb_debug ("post row deleted");
	if (view->priv->update_selection_id == 0) {
		/* we could be in the middle of renaming a property.
		 * we've seen it being removed, but we haven't seen the
		 * new name being added yet, which means the property model
		 * is out of sync with the query model, so we can't
		 * reset the selection here.  wait until we've finished
		 * processing the change.
		 */
		view->priv->update_selection_id = g_idle_add ((GSourceFunc) update_selection_cb, view);
	}
}

/**
 * rb_property_view_get_num_properties:
 * @view: a #RBPropertyView
 *
 * Returns the number of property values present in the view.
 *
 * Return value: number of properties
 */
guint
rb_property_view_get_num_properties (RBPropertyView *view)
{
	g_return_val_if_fail (RB_IS_PROPERTY_VIEW (view), 0);

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
			fmt = ngettext ("%d artist (%d)", "All %d artists (%d)", nodes);
			break;
		case RHYTHMDB_PROP_ALBUM:
			fmt = ngettext ("%d album (%d)", "All %d albums (%d)", nodes);
			break;
		case RHYTHMDB_PROP_GENRE:
			fmt = ngettext ("%d genre (%d)", "All %d genres (%d)", nodes);
			break;
		default:
			fmt = ngettext ("%d (%d)", "All %d (%d)", nodes);
			break;
		}

		str = g_strdup_printf (fmt, nodes, number);
	} else {
		str = g_strdup_printf (_("%s (%d)"), title, number);
	}

	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", G_UNLIKELY (is_all) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
		      NULL);
	g_free (str);
	g_free (title);
}

static void
rb_property_view_constructed (GObject *object)
{
	GtkCellRenderer *renderer;
	RBPropertyView *view;

	RB_CHAIN_GOBJECT_METHOD (rb_property_view_parent_class, constructed, object);

	view = RB_PROPERTY_VIEW (object);

	view->priv->treeview = GTK_WIDGET (gtk_tree_view_new_with_model (GTK_TREE_MODEL (view->priv->prop_model)));


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

	rb_property_view_set_model_internal (view, rhythmdb_property_model_new (view->priv->db, view->priv->propid));
	if (view->priv->draggable)
		rhythmdb_property_model_enable_drag (view->priv->prop_model,
						     GTK_TREE_VIEW (view->priv->treeview));

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_selection_set_mode (view->priv->selection, GTK_SELECTION_SINGLE);

	view->priv->column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (view->priv->column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (view->priv->column, renderer,
						 (GtkTreeCellDataFunc) rb_property_view_cell_data_func,
						 view, NULL);
	gtk_tree_view_column_set_title (view->priv->column, view->priv->title);
	gtk_tree_view_column_set_sizing (view->priv->column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview),
				     view->priv->column);
	g_object_set (renderer, "single-paragraph-mode", TRUE, NULL);
}

static void
rb_property_view_row_activated_cb (GtkTreeView *treeview,
				   GtkTreePath *path,
				   GtkTreeViewColumn *column,
				   RBPropertyView *view)
{
	GtkTreeIter iter;
	char *val;
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

	g_free (val);
}

/**
 * rb_property_view_set_selection:
 * @view: a #RBPropertyView
 * @vals: (element-type utf8): the values to be selected
 *
 * Replaces the selection in the property view.  All values in the list
 * that are present in the view will be selected, and the view will be
 * scrolled to show the last value selected.
 */
void
rb_property_view_set_selection (RBPropertyView *view,
				const GList *vals)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));

	view->priv->handling_row_deletion = TRUE;

	gtk_tree_selection_unselect_all (view->priv->selection);

	for (; vals ; vals = vals->next) {
		GtkTreeIter iter;

		if (rhythmdb_property_model_iter_from_string (view->priv->prop_model, vals->data, &iter)) {
			GtkTreePath *path;

			gtk_tree_selection_select_iter (view->priv->selection, &iter);
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->prop_model), &iter);
			if (path != NULL) {
				GtkTreePath *start_path, *end_path;

				if (gtk_tree_view_get_visible_range (GTK_TREE_VIEW (view->priv->treeview), &start_path, &end_path)) {
					if (gtk_tree_path_compare (path, start_path) < 0 ||
					    gtk_tree_path_compare (path, end_path) > 0) {
						gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview),
									      path, NULL, TRUE,
									      0.5, 0.0);
					}
					gtk_tree_path_free (start_path);
					gtk_tree_path_free (end_path);
				}
				gtk_tree_path_free (path);
			}
		}
	}

	view->priv->handling_row_deletion = FALSE;
	rb_property_view_selection_changed_cb (view->priv->selection, view);
}

/**
 * rb_property_view_get_selection:
 * @view: a #RBPropertyView
 *
 * Returns a #GList containing the selected property values.  The list must
 * be freed by the caller.
 *
 * Return value: (element-type utf8) (transfer full): list of selected values
 */
GList *
rb_property_view_get_selection (RBPropertyView *view)
{
	gboolean is_all = TRUE;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GList *selected_rows, *tem;
	GList *selected_properties = NULL;

	selected_rows = gtk_tree_selection_get_selected_rows (view->priv->selection, &model);
	for (tem = selected_rows; tem; tem = tem->next) {
		char *selected_prop = NULL;

		g_assert (gtk_tree_model_get_iter (model, &iter, tem->data));
		gtk_tree_model_get (model, &iter,
				    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &selected_prop,
				    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all, -1);
		if (is_all) {
			rb_list_deep_free (selected_properties);
			selected_properties = NULL;
			break;
		}
		selected_properties = g_list_prepend (selected_properties,
						      selected_prop);
	}

	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);
	
	return selected_properties;
}

static void
select_all (RBPropertyView *view, GtkTreeSelection *selection, GtkTreeModel *model)
{
	GtkTreeIter iter;

	g_signal_handlers_block_by_func (selection,
					 G_CALLBACK (rb_property_view_selection_changed_cb),
					 view);
	gtk_tree_selection_unselect_all (selection);
	if (gtk_tree_model_get_iter_first (model, &iter))
		gtk_tree_selection_select_iter (selection, &iter);
	g_signal_handlers_unblock_by_func (selection,
					   G_CALLBACK (rb_property_view_selection_changed_cb),
					   view);
}

static void
rb_property_view_selection_changed_cb (GtkTreeSelection *selection,
				       RBPropertyView *view)
{
	char *selected_prop = NULL;
	gboolean is_all = TRUE;
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

		if (is_all)
			select_all (view, selection, model);

		g_signal_emit (view, rb_property_view_signals[PROPERTIES_SELECTED], 0,
			       selected_properties);
		rb_list_deep_free (selected_properties);
	} else {
		if (gtk_tree_selection_get_selected (view->priv->selection, &model, &iter)) {
			gtk_tree_model_get (model, &iter,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &selected_prop,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all, -1);
			g_signal_emit (view, rb_property_view_signals[PROPERTY_SELECTED], 0,
				       is_all ? NULL : selected_prop);
		} else {
			select_all (view, selection, model);
			g_signal_emit (view, rb_property_view_signals[PROPERTY_SELECTED], 0, NULL);
		}
	}

	g_free (selected_prop);
}

static gboolean
rb_property_view_popup_menu_cb (GtkTreeView *treeview,
				RBPropertyView *view)
{
	g_signal_emit (G_OBJECT (view), rb_property_view_signals[SHOW_POPUP], 0);
	return TRUE;
}

/**
 * rb_property_view_append_column_custom:
 * @view: a #RBPropertyView
 * @column: a #GtkTreeViewColumn to append to the view
 *
 * Appends a custom created column to the view.
 */
void
rb_property_view_append_column_custom (RBPropertyView *view,
				       GtkTreeViewColumn *column)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));

	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);
}

/**
 * rb_property_view_set_column_visible:
 * @view: a #RBPropertyView
 * @visible: whether the property column should be visible
 *
 * Sets the visibility of the property column.
 */
void
rb_property_view_set_column_visible (RBPropertyView *view, gboolean visible)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));
	gtk_tree_view_column_set_visible (view->priv->column, visible);
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
			char *val;
			GList *lst = NULL;

			model = gtk_tree_view_get_model (GTK_TREE_VIEW (view->priv->treeview));
			if (gtk_tree_model_get_iter (model, &iter, path)) {
				gtk_tree_model_get (model, &iter,
						    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &val, -1);
				lst = g_list_prepend (lst, (gpointer) val);
				rb_property_view_set_selection (view, lst);
				g_free (val);
			}
		}
		g_signal_emit (G_OBJECT (view), rb_property_view_signals[SHOW_POPUP], 0);
		return TRUE;
	}

	return FALSE;
}

/**
 * rb_property_view_set_search_func:
 * @view: a #RBPropertyView
 * @func: tree view search function to use for this view
 * @func_data: data to pass to the search function
 * @notify: function to call to dispose of the data
 *
 * Sets the compare function for the interactive search capabilities.
 * The function must return FALSE when the search key string matches
 * the row it is passed.
 */
void
rb_property_view_set_search_func (RBPropertyView *view,
				  GtkTreeViewSearchEqualFunc func,
				  gpointer func_data,
				  GDestroyNotify notify)
{
	g_return_if_fail (RB_IS_PROPERTY_VIEW (view));

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view->priv->treeview),
					     func, func_data,
					     notify);
}

