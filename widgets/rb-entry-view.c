/*
 *  arch-tag: Implementation of widget to display RhythmDB entries
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

#include "monkey-media-audio-quality.h"

#include "rb-tree-dnd.h"
#include "rb-tree-view-column.h"
#include "rb-entry-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-cell-renderer-rating.h"
#include "rb-string-helpers.h"
#include "rb-stock-icons.h"
#include "rb-preferences.h"
#include "rb-tree-view.h"
#include "eel-gconf-extensions.h"

static const GtkTargetEntry rb_entry_view_drag_types[] = {{  "text/uri-list", 0, 0 }};

struct RBEntryViewColumnSortData
{
	GCompareDataFunc func;
	gpointer data;
};

static void rb_entry_view_class_init (RBEntryViewClass *klass);
static void rb_entry_view_init (RBEntryView *view);
static GObject *rb_entry_view_constructor (GType type, guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
static void rb_entry_view_finalize (GObject *object);
static void rb_entry_view_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_entry_view_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_entry_view_selection_changed_cb (GtkTreeSelection *selection,
				               RBEntryView *view);
static void rb_entry_view_row_activated_cb (GtkTreeView *treeview,
			                   GtkTreePath *path,
			                   GtkTreeViewColumn *column,
			                   RBEntryView *view);
static void rb_entry_view_row_inserted_cb (GtkTreeModel *model,
					   GtkTreePath *path,
					   GtkTreeIter *iter,
					   RBEntryView *view);
static void rb_entry_view_row_deleted_cb (GtkTreeModel *model,
					  GtkTreePath *path,
					  RBEntryView *view);
static void rb_entry_view_row_changed_cb (GtkTreeModel *model,
					  GtkTreePath *path,
					  GtkTreeIter *iter,
					  RBEntryView *view);
static gboolean emit_entry_changed (RBEntryView *view);
static void queue_changed_sig (RBEntryView *view);
static void rb_entry_view_sync_columns_visible (RBEntryView *view);
static void rb_entry_view_columns_config_changed_cb (GConfClient* client,
						    guint cnxn_id,
						    GConfEntry *entry,
						    gpointer user_data);
static void rb_entry_view_sort_key_changed_cb (GConfClient* client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       gpointer user_data);
/* static gboolean rb_entry_view_dummy_drag_drop_cb (GtkWidget *widget, */
/* 						  GdkDragContext *drag_context, */
/* 						  int x, int y, guint time, */
/* 						  gpointer user_data); */
static void rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
				   const char *path,
				   int rating,
				   RBEntryView *view);
static gboolean rb_entry_view_button_press_cb (GtkTreeView *treeview,
					      GdkEventButton *event,
					      RBEntryView *view);
static gboolean rb_entry_view_entry_is_visible (RBEntryView *view, RhythmDBEntry *entry,
						GtkTreeIter *iter);
static void rb_entry_view_scroll_to_iter (RBEntryView *view,
					  GtkTreeIter *iter);
static gboolean idle_poll_model (RBEntryView *view);

struct RBEntryViewReverseSortingData
{
	GCompareDataFunc func;
	gpointer data;
};

static gint reverse_sorting_func (gpointer a, gpointer b, struct RBEntryViewReverseSortingData *data);

struct RBEntryViewPrivate
{
	RhythmDB *db;
	
	RhythmDBQueryModel *model;

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	gboolean playing;
	RhythmDBQueryModel *playing_model;
	RhythmDBEntry *playing_entry;
	gboolean playing_entry_in_view;
	GtkTreeIter playing_entry_iter;

	gboolean is_drag_source;
	gboolean is_drag_dest;

	GdkPixbuf *playing_pixbuf;
	GdkPixbuf *paused_pixbuf;
	
	char *sorting_key;
	guint sorting_gconf_notification_id;
	GList *clickable_columns;
	GtkTreeViewColumn *sorting_column;
	gint sorting_order;
	struct RBEntryViewReverseSortingData *reverse_sorting_data;

	gboolean have_selection;

	gboolean keep_selection;

	RhythmDBEntry *selected_entry;

	gboolean change_sig_queued;
	guint change_sig_id;

	gboolean selection_lock;

	GHashTable *column_key_map;

	guint gconf_notification_id;
	GHashTable *propid_column_map;
	GHashTable *column_sort_data_map;

	gboolean idle;

	guint model_poll_id;

#ifdef USE_GTK_TREE_VIEW_WORKAROUND	
	guint freeze_count;
#endif
};

enum
{
	ENTRY_ADDED,
	ENTRY_DELETED,
	ENTRY_SELECTED,
	ENTRY_ACTIVATED,
	CHANGED,
	SHOW_POPUP,
	PLAYING_ENTRY_DELETED,
	HAVE_SEL_CHANGED,
	SORT_ORDER_CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_MODEL,
	PROP_PLAYING_ENTRY,
	PROP_SORTING_KEY,
	PROP_IS_DRAG_SOURCE,
	PROP_IS_DRAG_DEST,
};

static GObjectClass *parent_class = NULL;

static guint rb_entry_view_signals[LAST_SIGNAL] = { 0 };

GType
rb_entry_view_get_type (void)
{
	static GType rb_entry_view_type = 0;

	if (rb_entry_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBEntryViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_entry_view_class_init,
			NULL,
			NULL,
			sizeof (RBEntryView),
			0,
			(GInstanceInitFunc) rb_entry_view_init
		};

		rb_entry_view_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW,
							    "RBEntryView",
							    &our_info, 0);
	}

	return rb_entry_view_type;
}

static void
rb_entry_view_class_init (RBEntryViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_entry_view_finalize;
	object_class->constructor = rb_entry_view_constructor;

	object_class->set_property = rb_entry_view_set_property;
	object_class->get_property = rb_entry_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_PLAYING_ENTRY,
					 g_param_spec_pointer ("playing-entry",
							       "Playing entry",
							       "Playing entry",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORTING_KEY,
					 g_param_spec_string ("sort-key",
							      "sorting key",
							      "sorting key",
							      "",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_SOURCE,
					 g_param_spec_boolean ("is-drag-source",
							       "is drag source",
							       "whether or not this is a drag source",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_DEST,
					 g_param_spec_boolean ("is-drag-dest",
							       "is drag dest",
							       "whether or not this is a drag dest",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	rb_entry_view_signals[ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[ENTRY_DELETED] =
		g_signal_new ("entry-deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[ENTRY_ACTIVATED] =
		g_signal_new ("entry-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[ENTRY_SELECTED] =
		g_signal_new ("entry-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_entry_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, show_popup),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_entry_view_signals[PLAYING_ENTRY_DELETED] =
		g_signal_new ("playing_entry_deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, playing_entry_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
	rb_entry_view_signals[HAVE_SEL_CHANGED] =
		g_signal_new ("have_selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, have_selection_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
	rb_entry_view_signals[SORT_ORDER_CHANGED] =
		g_signal_new ("sort-order-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, sort_order_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_entry_view_init (RBEntryView *view)
{
	GtkWidget *dummy;
	
	view->priv = g_new0 (RBEntryViewPrivate, 1);

	dummy = gtk_tree_view_new ();
	view->priv->playing_pixbuf = gtk_widget_render_icon (dummy,
							     RB_STOCK_PLAYING,
							     GTK_ICON_SIZE_MENU,
							     NULL);

	view->priv->paused_pixbuf = gtk_widget_render_icon (dummy,
							    RB_STOCK_PAUSED,
							    GTK_ICON_SIZE_MENU,
							    NULL);
	gtk_widget_destroy (dummy);

	view->priv->propid_column_map = g_hash_table_new (NULL, NULL);
	view->priv->column_sort_data_map = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	view->priv->column_key_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
rb_entry_view_finalize (GObject *object)
{
	RBEntryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_ENTRY_VIEW (object));

	view = RB_ENTRY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	if (view->priv->change_sig_queued)
		g_source_remove (view->priv->change_sig_id);

	g_source_remove (view->priv->model_poll_id);

	if (view->priv->gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->gconf_notification_id);
	if (view->priv->sorting_gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->sorting_gconf_notification_id);

	g_list_free (view->priv->clickable_columns);

	g_hash_table_destroy (view->priv->propid_column_map);
	g_hash_table_destroy (view->priv->column_sort_data_map);
	g_hash_table_destroy (view->priv->column_key_map);

	g_object_unref (G_OBJECT (view->priv->playing_pixbuf));
	g_object_unref (G_OBJECT (view->priv->paused_pixbuf));

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* FIXME!
 * This is a gross hack to work around a GTK+ bug.  See
 * http://bugzilla.gnome.org/show_bug.cgi?id=119797 for more
 * information.
 */
void
rb_entry_view_thaw (RBEntryView *view)
{
/* RHYTHMDB FIXME */
#if 0
#ifdef USE_GTK_TREE_VIEW_WORKAROUND
	GList *columns;
	int i;

	g_return_if_fail (view->priv->freeze_count > 0);

	view->priv->freeze_count--;
	if (view->priv->freeze_count > 0)
		return;

	rb_debug ("thawing entry view");

	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));

	for (i = 0; i < RB_TREE_MODEL_NODE_NUM_COLUMNS && columns != NULL; i++, columns = g_list_next (columns))
		switch (i)
		{
		case RB_TREE_MODEL_NODE_COL_PLAYING:
			break;
		case RB_TREE_MODEL_NODE_COL_RATING:
			break;
		default:
			gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (columns->data),
							 GTK_TREE_VIEW_COLUMN_AUTOSIZE);

			break;
		}
	g_list_free (columns);
#endif
#endif
}

void
rb_entry_view_freeze (RBEntryView *view)
{
/* RHYTHMDB FIXME */
#if 0
#ifdef USE_GTK_TREE_VIEW_WORKAROUND
	GList *columns;
	int i;

	view->priv->freeze_count++;

	rb_debug ("freezing entry view");
	
	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
	
	for (i = 0; i < RB_TREE_MODEL_NODE_NUM_COLUMNS && columns != NULL; i++, columns = g_list_next (columns))
		switch (i)
		{
		case RB_TREE_MODEL_NODE_COL_PLAYING:
			break;
		case RB_TREE_MODEL_NODE_COL_RATING:
			break;
		default:
			gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (columns->data),
							 GTK_TREE_VIEW_COLUMN_FIXED);

			break;
		}

	g_list_free (columns);
#endif
#endif
}

static void
rb_entry_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBEntryView *view = RB_ENTRY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		view->priv->db = g_value_get_object (value);
		break;
	case PROP_SORTING_KEY:
		view->priv->sorting_key = g_value_dup_string (value);
		break;
	case PROP_MODEL:
	{
		RhythmDBQueryModel *new_model;
		struct RBEntryViewColumnSortData *sort_data;
		
		if (view->priv->model) {
			rhythmdb_query_model_cancel (view->priv->model);
			rhythmdb_query_model_set_connected (RHYTHMDB_QUERY_MODEL (view->priv->model), FALSE);
		}
		new_model = g_value_get_object (value);

		rhythmdb_query_model_set_connected (RHYTHMDB_QUERY_MODEL (new_model),
						    TRUE);

		g_signal_connect_object (G_OBJECT (new_model),
					 "row_inserted",
					 G_CALLBACK (rb_entry_view_row_inserted_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (new_model),
					 "row_deleted",
					 G_CALLBACK (rb_entry_view_row_deleted_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (new_model),
					 "row_changed",
					 G_CALLBACK (rb_entry_view_row_changed_cb),
					 view,
					 0);

		if (view->priv->sorting_column) {
			sort_data = g_hash_table_lookup (view->priv->column_sort_data_map,
							 view->priv->sorting_column);
			g_assert (sort_data);

			if (view->priv->sorting_order != GTK_SORT_DESCENDING) {
				g_object_set (G_OBJECT (new_model), "sort-func",
					      sort_data->func, "sort-data", sort_data->data, NULL);
			} else {
				g_free (view->priv->reverse_sorting_data);
				view->priv->reverse_sorting_data
					= g_new (struct RBEntryViewReverseSortingData, 1);
				view->priv->reverse_sorting_data->func = sort_data->func;
				view->priv->reverse_sorting_data->data = sort_data->data;
				
				g_object_set (G_OBJECT (new_model), "sort-func",
					      reverse_sorting_func, "sort-data",
					      view->priv->reverse_sorting_data, NULL);
			}
		}

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (new_model));
		view->priv->model = new_model;
		view->priv->have_selection = FALSE;
		queue_changed_sig (view);

		break;
	}
	case PROP_PLAYING_ENTRY:
	{
		GtkTreeIter iter;
		GtkTreePath *path;
		RhythmDBEntry *entry;
		rb_entry_view_freeze (view);

		entry = g_value_get_pointer (value);
		
		if (view->priv->playing_entry != NULL
		    && view->priv->playing_entry_in_view) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->playing_model),
							&view->priv->playing_entry_iter);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (view->priv->playing_model),
						    path, &view->priv->playing_entry_iter);
			gtk_tree_path_free (path);
			g_object_unref (G_OBJECT (view->priv->playing_model));
		}
		
		view->priv->playing_entry = entry;
		g_object_ref (G_OBJECT (view->priv->model));
		view->priv->playing_model = view->priv->model;

		if (view->priv->playing_entry != NULL) {
			view->priv->playing_entry_in_view = 
				rhythmdb_query_model_entry_to_iter (view->priv->model,
								    view->priv->playing_entry,
								    &view->priv->playing_entry_iter);
			if (view->priv->playing_entry_in_view) {
				path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model),
								&view->priv->playing_entry_iter);
				gtk_tree_model_row_changed (GTK_TREE_MODEL (view->priv->model),
							    path, &view->priv->playing_entry_iter);
				gtk_tree_path_free (path);
			}
		}

		rb_entry_view_thaw (view);

		if (view->priv->playing_entry
		    && view->priv->playing_entry_in_view
		    && !rb_entry_view_entry_is_visible (view, view->priv->playing_entry, &iter))
			rb_entry_view_scroll_to_iter (view, &iter);
	}
	break;
	case PROP_IS_DRAG_SOURCE:
		view->priv->is_drag_source = g_value_get_boolean (value);
		break;
	case PROP_IS_DRAG_DEST:
		view->priv->is_drag_dest = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_entry_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBEntryView *view = RB_ENTRY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, view->priv->db);
		break;
	case PROP_SORTING_KEY:
		g_value_set_string (value, view->priv->sorting_key);
		break;
	case PROP_PLAYING_ENTRY:
		g_value_set_pointer (value, view->priv->playing_entry);
		break;
	case PROP_IS_DRAG_SOURCE:
		g_value_set_boolean (value, view->priv->is_drag_source);
		break;
	case PROP_IS_DRAG_DEST:
		g_value_set_boolean (value, view->priv->is_drag_dest);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBEntryView *
rb_entry_view_new (RhythmDB *db, const char *sort_key,
		   gboolean is_drag_source, gboolean is_drag_dest) 
{
	RBEntryView *view;

	view = RB_ENTRY_VIEW (g_object_new (RB_TYPE_ENTRY_VIEW,
					   "hadjustment", NULL,
					   "vadjustment", NULL,
					   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					   "vscrollbar_policy", GTK_POLICY_ALWAYS,
					   "shadow_type", GTK_SHADOW_IN,
					    "db", db,
					    "sort-key", sort_key,
					    "is-drag-source", is_drag_source,
					    "is-drag-dest", is_drag_dest,
					   NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

void
rb_entry_view_set_model (RBEntryView *view, RhythmDBQueryModel *model)
{
	g_object_set (G_OBJECT (view), "model", model, NULL);
}

gboolean
rb_entry_view_busy (RBEntryView *view)
{
	return view->priv->model &&
		rhythmdb_query_model_has_pending_changes (RHYTHMDB_QUERY_MODEL (view->priv->model));
}

glong
rb_entry_view_get_duration (RBEntryView *view)
{
	return rhythmdb_query_model_get_duration (RHYTHMDB_QUERY_MODEL (view->priv->model));
}

GnomeVFSFileSize
rb_entry_view_get_total_size (RBEntryView *view)
{
	return rhythmdb_query_model_get_size (RHYTHMDB_QUERY_MODEL (view->priv->model));
}

static RhythmDBEntry *
entry_from_tree_path (RBEntryView *view, GtkTreePath *path)
{
	GtkTreeIter entry_iter;
	RhythmDBEntry *entry;

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->model), &entry_iter, path);
	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->model), &entry_iter, 0,
			    &entry, -1);
	return entry;
}

static inline RhythmDBEntry *
entry_from_tree_iter (RBEntryView *view, GtkTreeIter *iter)
{
	RhythmDBEntry *entry;
	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->model), iter, 0,
			    &entry, -1);
	return entry;
}

RhythmDBQueryModel *
rb_entry_view_get_model (RBEntryView *view)
{
	g_return_val_if_fail (RB_IS_ENTRY_VIEW (view), NULL);
	return view->priv->model;
}

static gint
reverse_sorting_func (gpointer a, gpointer b, struct RBEntryViewReverseSortingData *data)
{
	return - data->func (a, b, data->data);
}

/* Sweet name, eh? */
struct RBEntryViewCellDataFuncData {
	RBEntryView *view;
	RhythmDBPropType propid;
};

static gint
rb_entry_view_artist_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				RBEntryView *view)
{
	gint a_int, b_int;
	const char *a_str = NULL;
	const char *b_str = NULL;
	gint ret;

	a_str = rhythmdb_entry_get_string (view->priv->db, a, RHYTHMDB_PROP_ARTIST_SORT_KEY);
	b_str = rhythmdb_entry_get_string (view->priv->db, b, RHYTHMDB_PROP_ARTIST_SORT_KEY);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		goto out;

	a_str = rhythmdb_entry_get_string (view->priv->db, a, RHYTHMDB_PROP_ALBUM_SORT_KEY);
	b_str = rhythmdb_entry_get_string (view->priv->db, b, RHYTHMDB_PROP_ALBUM_SORT_KEY);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		goto out;

	a_int = rhythmdb_entry_get_int (view->priv->db, a, RHYTHMDB_PROP_TRACK_NUMBER);
	b_int = rhythmdb_entry_get_int (view->priv->db, b, RHYTHMDB_PROP_TRACK_NUMBER);

	if (a_int != b_int) {
		ret = (a_int < b_int ? -1 : 1);
		goto out;
	}

	a_str = rhythmdb_entry_get_string (view->priv->db, a, RHYTHMDB_PROP_TITLE_SORT_KEY);
	b_str = rhythmdb_entry_get_string (view->priv->db, b, RHYTHMDB_PROP_TITLE_SORT_KEY);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		goto out;

	ret = 0;

out:
	return ret;
}

static gint
rb_entry_view_album_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
			       RBEntryView *view)
{
	gint a_int, b_int;
	const char *a_str = NULL;
	const char *b_str = NULL;
	gint ret;

	a_str = rhythmdb_entry_get_string (view->priv->db, a, RHYTHMDB_PROP_ALBUM_SORT_KEY);
	b_str = rhythmdb_entry_get_string (view->priv->db, b, RHYTHMDB_PROP_ALBUM_SORT_KEY);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		goto out;

	a_int = rhythmdb_entry_get_int (view->priv->db, a, RHYTHMDB_PROP_TRACK_NUMBER);
	b_int = rhythmdb_entry_get_int (view->priv->db, b, RHYTHMDB_PROP_TRACK_NUMBER);

	if (a_int != b_int) {
		ret = (a_int < b_int ? -1 : 1);
		goto out;
	}

out:
	return ret;
}

static gint
rb_entry_view_track_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
			       RBEntryView *view)
{
	gint a_int, b_int;
	const char *a_str = NULL;
	const char *b_str = NULL;	
	gint ret;

	a_str = rhythmdb_entry_get_string (view->priv->db, a, RHYTHMDB_PROP_ALBUM_SORT_KEY);
	b_str = rhythmdb_entry_get_string (view->priv->db, b, RHYTHMDB_PROP_ALBUM_SORT_KEY);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		goto out;

	a_int = rhythmdb_entry_get_int (view->priv->db, a, RHYTHMDB_PROP_TRACK_NUMBER);
	b_int = rhythmdb_entry_get_int (view->priv->db, b, RHYTHMDB_PROP_TRACK_NUMBER);

	if (a_int != b_int) {
		ret = (a_int < b_int ? -1 : 1);
		goto out;
	}

out:
	return ret;
}


static gint
rb_entry_view_int_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
			     struct RBEntryViewCellDataFuncData *data)
{
	gint a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_int (data->view->priv->db, a, data->propid);
	b_val = rhythmdb_entry_get_int (data->view->priv->db, b, data->propid);

	ret = (a_val == b_val ? 0 : (a_val > b_val ? 1 : -1));

	return ret;
}

static gint
rb_entry_view_long_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
			      struct RBEntryViewCellDataFuncData *data)
{
	glong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_long (data->view->priv->db, a, data->propid);
	b_val = rhythmdb_entry_get_long (data->view->priv->db, b, data->propid);

	ret = (a_val == b_val ? 0 : (a_val > b_val ? 1 : -1));

	return ret;
}

static gint
rb_entry_view_string_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				struct RBEntryViewCellDataFuncData *data)
{
	const char *a_val;
	const char *b_val;	
	gint ret;

	a_val = rhythmdb_entry_get_string (data->view->priv->db, a, data->propid);
	b_val = rhythmdb_entry_get_string (data->view->priv->db, b, data->propid);

	ret = strcmp (a_val, b_val);

	return ret;
}

static void
rb_entry_view_playing_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				      GtkTreeModel *tree_model, GtkTreeIter *iter,
				      RBEntryView *view)
{
	RhythmDBEntry *entry;
	GdkPixbuf *pixbuf;

	entry = entry_from_tree_iter (view, iter);

	if (entry == view->priv->playing_entry && view->priv->playing)
		pixbuf = view->priv->playing_pixbuf;
	else if (entry == view->priv->playing_entry)
		pixbuf = view->priv->paused_pixbuf;
	else
		pixbuf = NULL;

	g_object_set (G_OBJECT (renderer), "pixbuf", pixbuf, NULL);
}

static void
rb_entry_view_rating_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model, GtkTreeIter *iter,
				     RBEntryView *view)
{
	RhythmDBEntry *entry;
	guint rating;

	entry = entry_from_tree_iter (view, iter);

	rhythmdb_read_lock (view->priv->db);

	rating = rhythmdb_entry_get_int (view->priv->db, entry, RHYTHMDB_PROP_RATING);

	rhythmdb_read_unlock (view->priv->db);

	g_object_set (G_OBJECT (renderer), "rating", rating, NULL);
}

static void
rb_entry_view_intstr_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				  GtkTreeModel *tree_model, GtkTreeIter *iter,
				  struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	int val;

	entry = entry_from_tree_iter (data->view, iter);

	rhythmdb_read_lock (data->view->priv->db);

	val = rhythmdb_entry_get_int (data->view->priv->db, entry, data->propid);
	rhythmdb_read_unlock (data->view->priv->db);

	if (val >= 0)
		str = g_strdup_printf ("%d", val);
	else
		str = g_strdup ("");

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);
}

static void
rb_entry_view_play_count_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model, GtkTreeIter *iter,
					 struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	int i;
	char *str;

	entry = entry_from_tree_iter (data->view, iter);

	rhythmdb_read_lock (data->view->priv->db);

	i = rhythmdb_entry_get_int (data->view->priv->db, entry, data->propid);
	if (i == 0)
		str = _("Never");
	else
		str = g_strdup_printf ("%d", i);

	rhythmdb_read_unlock (data->view->priv->db);

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	if (i != 0)
		g_free (str);
}

static void
rb_entry_view_duration_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				       GtkTreeModel *tree_model, GtkTreeIter *iter,
				       struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	long duration;
	int minutes, seconds;

	entry = entry_from_tree_iter (data->view, iter);

	rhythmdb_read_lock (data->view->priv->db);

	duration  = rhythmdb_entry_get_long (data->view->priv->db, entry,
					     data->propid);

	rhythmdb_read_unlock (data->view->priv->db);

	minutes = duration / 60;
	seconds = duration % 60;

	str = g_strdup_printf (_("%d:%02d"), minutes, seconds);

	g_object_set (G_OBJECT (renderer), "text", str, NULL);

	g_free (str);
}

static void
rb_entry_view_quality_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				      GtkTreeModel *tree_model, GtkTreeIter *iter,
				      struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	MonkeyMediaAudioQuality quality;

	entry = entry_from_tree_iter (data->view, iter);

	rhythmdb_read_lock (data->view->priv->db);

	quality = rhythmdb_entry_get_int (data->view->priv->db, entry,
					  data->propid);

	rhythmdb_read_unlock (data->view->priv->db);

	if (quality > 0) {
		str = monkey_media_audio_quality_to_string (quality);
		g_object_set (G_OBJECT (renderer), "text", str, NULL);
		g_free (str);
	} else {
		g_object_set (G_OBJECT (renderer), "text", _("Unknown"), NULL);
	}
}


static void
rb_entry_view_string_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model, GtkTreeIter *iter,
				     struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	const char *str;

	entry = entry_from_tree_iter (data->view, iter);

	rhythmdb_read_lock (data->view->priv->db);

	str = rhythmdb_entry_get_string (data->view->priv->db, entry, data->propid);

	g_object_set (G_OBJECT (renderer), "text", str, NULL);

	rhythmdb_read_unlock (data->view->priv->db);
}

static void
rb_entry_view_sync_sorting (RBEntryView *view)
{
	GList *columns, *tem;
	GtkTreeViewColumn *column;
	char **strs;
	gint direction;
	char *sorttype;

	if (!view->priv->sorting_key)
		return;

	sorttype = eel_gconf_get_string (view->priv->sorting_key);

	if (!sorttype || !*sorttype) {
		rb_debug ("no sorting data available in gconf!");
		return;
	}

	if (!strchr (sorttype, ',')) {
		g_warning ("malformed sort data");
		return;
	}
	
	strs = g_strsplit (sorttype, ",", 0);

	column = g_hash_table_lookup (view->priv->column_key_map, strs[0]);
	if (!column)
		goto free_out;

	if (!strcmp ("ascending", strs[1]))
		direction = GTK_SORT_ASCENDING;
	else if (!strcmp ("descending", strs[1]))
		direction = GTK_SORT_DESCENDING;		
	else if (!strcmp ("none", strs[1]))
		direction = -1;
	else
		goto free_out;

  	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
	for (tem = columns; tem; tem = tem->next)
		gtk_tree_view_column_set_sort_indicator (tem->data, FALSE); 
	g_list_free (columns);
	
	view->priv->sorting_column = column;
	view->priv->sorting_order = direction;

	if (view->priv->sorting_order != -1) {
		gtk_tree_view_column_set_sort_indicator (column, TRUE);
		gtk_tree_view_column_set_sort_order (column, view->priv->sorting_order);

		rb_debug ("emitting sort order changed");
		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SORT_ORDER_CHANGED], 0); 
	}
free_out:
	g_strfreev (strs);
}

const char *
rb_entry_view_get_sorting_type (RBEntryView *view)
{
	return eel_gconf_get_string (view->priv->sorting_key);
}

static void
rb_entry_view_column_clicked_cb (GtkTreeViewColumn *column, RBEntryView *view)
{
	GString *key = g_string_new ("");
	gboolean is_sorted;
	gint sorting_order;

	rb_debug ("sorting on column %p", column);
	g_string_append (key, (char*) g_object_get_data (G_OBJECT (column), "rb-entry-view-key"));
	g_string_append_c (key, ',');

	is_sorted = gtk_tree_view_column_get_sort_indicator (column);

	if (is_sorted) {
		sorting_order = gtk_tree_view_column_get_sort_order (column);
		if (sorting_order == GTK_SORT_ASCENDING)
			sorting_order = GTK_SORT_DESCENDING;
		else
			sorting_order = -1;
	} else
		sorting_order = GTK_SORT_ASCENDING;

	switch (sorting_order)
	{
	case -1:
		g_string_append (key, "none");
		break;
	case GTK_SORT_ASCENDING:
		g_string_append (key, "ascending");
		break;
	case GTK_SORT_DESCENDING:
		g_string_append (key, "descending");
		break;
	default:
		g_assert_not_reached ();
	}
	eel_gconf_set_string (view->priv->sorting_key, key->str);
	g_string_free (key, TRUE);
}

void
rb_entry_view_append_column (RBEntryView *view, RBEntryViewColumn coltype)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	struct RBEntryViewCellDataFuncData *cell_data;
	struct RBEntryViewCellDataFuncData *sort_data;
	const char *title = NULL;
	const char *key = NULL;
	GtkTreeCellDataFunc cell_data_func = NULL;
	GCompareDataFunc sort_func = NULL;
	gpointer real_sort_data = NULL;
	RhythmDBPropType propid;

	column = GTK_TREE_VIEW_COLUMN (rb_tree_view_column_new ());

	if (coltype == RB_ENTRY_VIEW_COL_RATING) {
		guint width;

		propid = RHYTHMDB_PROP_RATING;

		sort_data = g_new0 (struct RBEntryViewCellDataFuncData, 1);
		sort_data->view = view;
		sort_data->propid = propid;
		sort_func = (GCompareDataFunc) rb_entry_view_int_sort_func;

		renderer = rb_cell_renderer_rating_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_entry_view_rating_cell_data_func,
							 view,
							 NULL);
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_clickable (column, TRUE);
		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
		gtk_tree_view_column_set_fixed_width (column, width * 5 + 5);
		g_signal_connect_object (renderer,
					 "rated",
					 G_CALLBACK (rb_entry_view_rated_cb),
					 G_OBJECT (view),
					 0);
		real_sort_data = sort_data;
		title = _("_Rating");
		key = "Rating";
		goto append;
	}

	cell_data = g_new0 (struct RBEntryViewCellDataFuncData, 1);
	cell_data->view = view;
	sort_data = g_new0 (struct RBEntryViewCellDataFuncData, 1);
	sort_data->view = view;

	switch (coltype)
	{
	case RB_ENTRY_VIEW_COL_TRACK_NUMBER:
		propid = RHYTHMDB_PROP_TRACK_NUMBER;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_intstr_cell_data_func;
		sort_func = (GCompareDataFunc) rb_entry_view_track_sort_func;
		real_sort_data = view;
		title = _("Tra_ck");
		key = "Track";
		break;
	case RB_ENTRY_VIEW_COL_TITLE:
		propid = RHYTHMDB_PROP_TITLE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				
		sort_data->propid = RHYTHMDB_PROP_TITLE_SORT_KEY;
		sort_func = (GCompareDataFunc) rb_entry_view_string_sort_func;
		title = _("_Title");
		key = "Title";
		rb_tree_view_column_set_expand (RB_TREE_VIEW_COLUMN (column), TRUE);
		break;
	case RB_ENTRY_VIEW_COL_ARTIST:
		propid = RHYTHMDB_PROP_ARTIST;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				
		sort_data->propid = RHYTHMDB_PROP_ARTIST_SORT_KEY;
		sort_func = (GCompareDataFunc) rb_entry_view_artist_sort_func;
		real_sort_data = view;
		title = _("Art_ist");
		key = "Artist";
		rb_tree_view_column_set_expand (RB_TREE_VIEW_COLUMN (column), TRUE);
		break;
	case RB_ENTRY_VIEW_COL_ALBUM:
		propid = RHYTHMDB_PROP_ALBUM;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				
		sort_data->propid = RHYTHMDB_PROP_ALBUM_SORT_KEY;
		sort_func = (GCompareDataFunc) rb_entry_view_album_sort_func;
		real_sort_data = view;
		title = _("A_lbum");
		key = "Album";
		rb_tree_view_column_set_expand (RB_TREE_VIEW_COLUMN (column), TRUE);
		break;
	case RB_ENTRY_VIEW_COL_GENRE:
		propid = RHYTHMDB_PROP_GENRE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				
		sort_data->propid = RHYTHMDB_PROP_GENRE_SORT_KEY;
		sort_func = (GCompareDataFunc) rb_entry_view_string_sort_func;
		title = _("Ge_nre");
		key = "Genre";
		rb_tree_view_column_set_expand (RB_TREE_VIEW_COLUMN (column), TRUE);
		break;
	case RB_ENTRY_VIEW_COL_DURATION:
		propid = RHYTHMDB_PROP_DURATION;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_duration_cell_data_func;
		sort_data->propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rb_entry_view_long_sort_func;
		title = _("Ti_me");
		key = "Time";
		break;
	case RB_ENTRY_VIEW_COL_QUALITY:
		propid = RHYTHMDB_PROP_QUALITY;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_quality_cell_data_func;
		sort_data->propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rb_entry_view_int_sort_func;
		title = _("_Quality");
		key = "Quality";
		break;
	/* RB_ENTRY_VIEW_COL_RATING at bottom */
	case RB_ENTRY_VIEW_COL_PLAY_COUNT:
		propid = RHYTHMDB_PROP_PLAY_COUNT;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_play_count_cell_data_func;
		sort_data->propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rb_entry_view_int_sort_func;
		title = _("_Play Count");
		key = "PlayCount";
		break;
	case RB_ENTRY_VIEW_COL_LAST_PLAYED:
		propid = RHYTHMDB_PROP_LAST_PLAYED;
		cell_data->propid = RHYTHMDB_PROP_LAST_PLAYED_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				
		sort_data->propid = RHYTHMDB_PROP_LAST_PLAYED;
		sort_func = (GCompareDataFunc) rb_entry_view_long_sort_func;
		title = _("L_ast Played");
		key = "LastPlayed";
		break;
	case RB_ENTRY_VIEW_COL_RATING:
	default:
		g_assert_not_reached ();
		propid = -1;
		break;
	}

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 cell_data_func, cell_data, g_free);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);

append:
	g_hash_table_insert (view->priv->propid_column_map, GINT_TO_POINTER (propid), column);
	rb_entry_view_append_column_custom (view, column, title, key, sort_func,
					    real_sort_data ? real_sort_data : sort_data);
}

void
rb_entry_view_append_column_custom (RBEntryView *view,
				    GtkTreeViewColumn *column,
				    const char *title,
				    const char *key,
				    GCompareDataFunc sort_func, gpointer user_data)
{
	struct RBEntryViewColumnSortData *sortdata;

	gtk_tree_view_column_set_title (column, title);
	gtk_tree_view_column_set_reorderable (column, FALSE);

	if (gtk_tree_view_column_get_clickable (column))
		view->priv->clickable_columns = g_list_append (view->priv->clickable_columns, column);

	g_signal_connect_object (G_OBJECT (column), "clicked",
				 G_CALLBACK (rb_entry_view_column_clicked_cb),
				 view, 0);

	g_object_set_data_full (G_OBJECT (column), "rb-entry-view-key",
				g_strdup (key), g_free);

	rb_debug ("appending column: %p (%s)", column, title);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);

	sortdata = g_new (struct RBEntryViewColumnSortData, 1);
	sortdata->func = (GCompareDataFunc) sort_func;
	sortdata->data = user_data;
	g_hash_table_insert (view->priv->column_sort_data_map, column, sortdata);
	g_hash_table_insert (view->priv->column_key_map, g_strdup (key), column);

	rb_entry_view_sync_columns_visible (view);
	rb_entry_view_sync_sorting (view);
}

void
rb_entry_view_set_columns_clickable (RBEntryView *view, gboolean clickable)
{
	GList *columns, *tem;

  	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
	for (tem = columns; tem; tem = tem->next)
		gtk_tree_view_column_set_clickable (tem->data, clickable); 
	g_list_free (columns);
}

static GObject *
rb_entry_view_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBEntryView *view;
	RBEntryViewClass *klass;
	GObjectClass *parent_class;
	klass = RB_ENTRY_VIEW_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	view = RB_ENTRY_VIEW (parent_class->constructor (type, n_construct_properties,
							 construct_properties));

	view->priv->treeview = GTK_WIDGET (rb_tree_view_new ());

	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "button_press_event",
			         G_CALLBACK (rb_entry_view_button_press_cb),
			         view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "row_activated",
			         G_CALLBACK (rb_entry_view_row_activated_cb),
			         view,
				 0);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect_object (G_OBJECT (view->priv->selection),
			         "changed",
			         G_CALLBACK (rb_entry_view_selection_changed_cb),
			         view,
				 0);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_selection_set_mode (view->priv->selection, GTK_SELECTION_MULTIPLE);
	
	if (view->priv->is_drag_source)
		rb_tree_dnd_add_drag_source_support (GTK_TREE_VIEW (view->priv->treeview),
						     GDK_BUTTON1_MASK,
						     rb_entry_view_drag_types,
						     G_N_ELEMENTS (rb_entry_view_drag_types),
						     GDK_ACTION_COPY | GDK_ACTION_MOVE);
	if (view->priv->is_drag_dest)
		rb_tree_dnd_add_drag_dest_support (GTK_TREE_VIEW (view->priv->treeview),
						   RB_TREE_DEST_CAN_DROP_BETWEEN | RB_TREE_DEST_EMPTY_VIEW_DROP,
						   rb_entry_view_drag_types,
						   G_N_ELEMENTS (rb_entry_view_drag_types),
						   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	gtk_container_add (GTK_CONTAINER (view), view->priv->treeview);

	{
		GtkTreeViewColumn *column;
		GtkTooltips *tooltip;
		GtkCellRenderer *renderer;
		guint width;

		tooltip = gtk_tooltips_new ();
		
		/* Playing icon column */
		column = GTK_TREE_VIEW_COLUMN (rb_tree_view_column_new ());
		renderer = rb_cell_renderer_pixbuf_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_entry_view_playing_cell_data_func,
							 view,
							 NULL);
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
		gtk_tree_view_column_set_fixed_width (column, width + 5);
		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);

		gtk_tooltips_set_tip (GTK_TOOLTIPS (tooltip), GTK_WIDGET (column->button),
				       _("Now Playing"), NULL);
	}

	view->priv->gconf_notification_id = 
		eel_gconf_notification_add (CONF_UI_COLUMNS_SETUP,
					    rb_entry_view_columns_config_changed_cb,
					    view);
	if (view->priv->sorting_key)
		view->priv->sorting_gconf_notification_id = 
			eel_gconf_notification_add (view->priv->sorting_key,
						    rb_entry_view_sort_key_changed_cb,
						    view);
	
	{
		RhythmDBQueryModel *query_model;
		query_model = rhythmdb_query_model_new_empty (view->priv->db);
		rb_entry_view_set_model (view, RHYTHMDB_QUERY_MODEL (query_model));
		g_object_unref (G_OBJECT (query_model));
	}
		
	view->priv->model_poll_id = g_idle_add ((GSourceFunc) idle_poll_model, view);

	return G_OBJECT (view);
}

/* static gboolean */
/* rb_entry_view_dummy_drag_drop_cb (GtkWidget *widget, */
/* 				  GdkDragContext *drag_context, */
/* 				  int x, int y, guint time, */
/* 				  gpointer user_data) */
/* { */
/* 	g_signal_stop_emission_by_name (widget, "drag_drop"); */

/* 	return TRUE; */
/* } */



static void
rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
		       const char *path_string,
		       int rating,
		       RBEntryView *view)
{
	GtkTreePath *path;
	RhythmDBEntry *entry;
	GValue value = { 0, };

	g_return_if_fail (rating >= 1 && rating <= 5 );
	g_return_if_fail (path_string != NULL);

	path = gtk_tree_path_new_from_string (path_string);
	entry = entry_from_tree_path (view, path);
	gtk_tree_path_free (path);

	rb_entry_view_freeze (view);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, rating);
	rhythmdb_entry_queue_set (view->priv->db, entry, RHYTHMDB_PROP_RATING,
				  &value);
	g_value_unset (&value);

	rb_entry_view_thaw (view);
}

void
rb_entry_view_set_playing_entry (RBEntryView *view,
				 RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	g_object_set (G_OBJECT (view), "playing-entry", entry, NULL);
}

RhythmDBEntry *
rb_entry_view_get_playing_entry (RBEntryView *view)
{
	g_return_val_if_fail (RB_IS_ENTRY_VIEW (view), NULL);

	return view->priv->playing_entry;
}

RhythmDBEntry *
rb_entry_view_get_first_entry (RBEntryView *view)
{
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->model), &iter))
		return entry_from_tree_iter (view, &iter);

	return NULL;
}

RhythmDBEntry *
rb_entry_view_get_next_from_entry (RBEntryView *view, RhythmDBEntry *entry)
{
	GtkTreeIter iter;

	g_return_val_if_fail (entry != NULL, NULL);

	if (!rhythmdb_query_model_entry_to_iter (view->priv->model,
						 entry, &iter)) {
		/* If the entry isn't in the entryview, the "next" entry is the first. */
		return rb_entry_view_get_first_entry (view);
	}
	
	if (gtk_tree_model_iter_next (GTK_TREE_MODEL (view->priv->model),
				      &iter))
		return entry_from_tree_iter (view, &iter);

	return NULL;
}

RhythmDBEntry *
rb_entry_view_get_previous_from_entry (RBEntryView *view, RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	g_return_val_if_fail (entry != NULL, NULL);

	if (!rhythmdb_query_model_entry_to_iter (view->priv->model,
						 entry, &iter))
		return NULL;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model), &iter);
	g_assert (path);
	if (!gtk_tree_path_prev (path)) {
		gtk_tree_path_free (path);
		return NULL;
	}

	g_assert (gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->model), &iter, path));
	return entry_from_tree_iter (view, &iter);
}


RhythmDBEntry *
rb_entry_view_get_next_entry (RBEntryView *view)
{
	if (view->priv->playing_entry == NULL)
		return NULL;

	return rb_entry_view_get_next_from_entry (view,
						  view->priv->playing_entry);
}

RhythmDBEntry *
rb_entry_view_get_previous_entry (RBEntryView *view)
{
	if (view->priv->playing_entry == NULL)
		return NULL;
	
	return rb_entry_view_get_previous_from_entry (view,
						      view->priv->playing_entry);
}

static gboolean
harvest_entries (GtkTreeModel *model,
		 GtkTreePath *path,
		 GtkTreeIter *iter,
		 void **data)
{
	GList **list = (GList **) data;
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	*list = g_list_append (*list, entry);

	return FALSE;
}

GList *
rb_entry_view_get_selected_entries (RBEntryView *view)
{
	GList *list = NULL;

	gtk_tree_selection_selected_foreach (view->priv->selection,
					     (GtkTreeSelectionForeachFunc) harvest_entries,
					     (gpointer) &list);

	return list;
}

RhythmDBEntry *
rb_entry_view_get_random_entry (RBEntryView *view)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	char *path_str;
	int index, n_rows;

	n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->model), NULL);
	if (n_rows == 0)
		return NULL;
	else if ((n_rows - 1) > 0)
		index = g_random_int_range (0, n_rows);
	else
		index = 0;

	path_str = g_strdup_printf ("%d", index);
	path = gtk_tree_path_new_from_string (path_str);
	g_free (path_str);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->model),
				 &iter, path);

	gtk_tree_path_free (path);

	return entry_from_tree_iter (view, &iter);
}

static gboolean
rb_entry_view_button_press_cb (GtkTreeView *treeview,
			      GdkEventButton *event,
			      RBEntryView *view)
{
	if (event->button == 3) {
		GtkTreePath *path;
		RhythmDBEntry *entry;

		gtk_tree_view_get_path_at_pos (treeview, event->x, event->y, &path, NULL, NULL, NULL);
		if (path != NULL) {
			entry = entry_from_tree_path (view, path);
			
			/* FIXME - don't require a popup to select an entry */
			rb_entry_view_select_entry (view, entry);
		
			g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SHOW_POPUP], 0);
		}
		return view->priv->have_selection;
	}

	return FALSE;
}

static void
queue_changed_sig (RBEntryView *view)
{
	if (!view->priv->change_sig_queued) {
		rb_debug ("queueing changed signal");
		view->priv->change_sig_id = g_timeout_add (250, (GSourceFunc) emit_entry_changed, view);
	}
	view->priv->change_sig_queued = TRUE;
}

static void
rb_entry_view_selection_changed_cb (GtkTreeSelection *selection,
				   RBEntryView *view)
{
	gboolean available;
	RhythmDBEntry *selected_entry = NULL;
	GList *sel;

	if (view->priv->selection_lock == TRUE)
		return;

	sel = rb_entry_view_get_selected_entries (view);
	available = (sel != NULL);
	if (sel != NULL)
		selected_entry = (g_list_first (sel))->data;

	if (available != view->priv->have_selection) {
		queue_changed_sig (view);
		view->priv->have_selection = available;

		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[HAVE_SEL_CHANGED], 0, available);
	}

	if (selected_entry != NULL && selected_entry != view->priv->selected_entry)
		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_SELECTED], 0, selected_entry);

	view->priv->selected_entry = selected_entry;

	g_list_free (sel);
}

gboolean
rb_entry_view_have_selection (RBEntryView *view)
{
	return view->priv->have_selection;
}

static void
rb_entry_view_row_activated_cb (GtkTreeView *treeview,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       RBEntryView *view)
{
	RhythmDBEntry *entry;

	rb_debug ("row activated");

	entry = entry_from_tree_path (view, path);

	rb_debug ("emitting entry activated");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_ACTIVATED], 0, entry);
}

static void
rb_entry_view_row_inserted_cb (GtkTreeModel *model,
			       GtkTreePath *path,
			       GtkTreeIter *iter,
			       RBEntryView *view)
{
	queue_changed_sig (view);
}

static void
rb_entry_view_row_deleted_cb (GtkTreeModel *model,
			      GtkTreePath *path,
			      RBEntryView *view)
{
	RhythmDBEntry *entry = entry_from_tree_path (view, path);

	if (entry == view->priv->playing_entry) {
		view->priv->playing_entry = NULL;
		
		rb_debug ("emitting playing entry destroyed");
		
		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[PLAYING_ENTRY_DELETED],
			       0, view->priv->playing_entry);
	}
	
	rb_debug ("row deleted");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_DELETED], 0, entry);
	queue_changed_sig (view);
}

static void
rb_entry_view_row_changed_cb (GtkTreeModel *model,
			      GtkTreePath *path,
			      GtkTreeIter *iter,
			      RBEntryView *view)
{
	rb_debug ("row changed");
	queue_changed_sig (view);
}

guint
rb_entry_view_get_num_entries (RBEntryView *view)
{
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->model),
					       NULL);
}

void
rb_entry_view_select_all (RBEntryView *view)
{
	gtk_tree_selection_select_all (view->priv->selection);
}

void
rb_entry_view_select_none (RBEntryView *view)
{
	view->priv->selection_lock = TRUE;

	gtk_tree_selection_unselect_all (view->priv->selection);

	view->priv->selected_entry = NULL;

	view->priv->selection_lock = FALSE;
}

void
rb_entry_view_select_entry (RBEntryView *view,
			    RhythmDBEntry *entry)
{
	GtkTreeIter iter;

	if (entry == NULL)
		return;

	view->priv->selection_lock = TRUE;

	rb_entry_view_select_none (view);

	if (rhythmdb_query_model_entry_to_iter (view->priv->model,
						entry, &iter)) {
		gtk_tree_selection_select_iter (view->priv->selection, &iter);
	}

	view->priv->selection_lock = FALSE;
}

void
rb_entry_view_scroll_to_entry (RBEntryView *view,
			       RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	
	if (rhythmdb_query_model_entry_to_iter (view->priv->model,
						entry, &iter)) {
		rb_entry_view_scroll_to_iter (view, &iter);
	}
}

static void
rb_entry_view_scroll_to_iter (RBEntryView *view,
			      GtkTreeIter *iter)
{
	GtkTreePath *path;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model), iter);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview), path,
				      gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				      TRUE, 0.5, 0.0);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (view->priv->treeview), path,
				  gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0), FALSE);

	gtk_tree_path_free (path);
}

gboolean
rb_entry_view_get_entry_visible (RBEntryView *view,
				 RhythmDBEntry *entry)
{
	GtkTreeIter unused;

	if (view->priv->playing_model != view->priv->model)
		return FALSE;

	return rb_entry_view_entry_is_visible (view, entry, &unused);
}

gboolean
rb_entry_view_get_entry_contained (RBEntryView *view,
				   RhythmDBEntry *entry)
{
	GtkTreeIter unused;

	return rhythmdb_query_model_entry_to_iter (view->priv->model,
						   entry, &unused);
}

static gboolean
rb_entry_view_entry_is_visible (RBEntryView *view,
				RhythmDBEntry *entry,
				GtkTreeIter *iter)
{
	GtkTreePath *path;
	GdkRectangle rect;

	g_return_val_if_fail (entry != NULL, FALSE);

	if (!GTK_WIDGET_REALIZED (view))
		return FALSE;

	if (!rhythmdb_query_model_entry_to_iter (view->priv->model,
						 entry, iter))
		return FALSE;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model), iter);
	gtk_tree_view_get_cell_area (GTK_TREE_VIEW (view->priv->treeview),
				     path,
				     gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				     &rect);

	gtk_tree_path_free (path);

	return rect.y != 0 && rect.height != 0;
}

static gboolean
emit_entry_changed (RBEntryView *view)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[CHANGED], 0);

	view->priv->change_sig_queued = FALSE;

	GDK_THREADS_LEAVE ();

	return FALSE;
}

/* static void */
/* playing_entry_deleted_cb (RhythmDB *db, RBEntryView *view) */
/* { */
/* 	rb_debug ("emitting playing entry destroyed"); */
/* 	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[PLAYING_ENTRY_DELETED], */
/* 		       0, view->priv->playing_entry); */
/* } */

void
rb_entry_view_enable_drag_source (RBEntryView *view,
				 const GtkTargetEntry *targets,
				 int n_targets)
{
	g_return_if_fail (view != NULL);

	rb_tree_dnd_add_drag_source_support (GTK_TREE_VIEW (view->priv->treeview),
					 GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
					 targets, n_targets, GDK_ACTION_COPY);
}

static void
rb_entry_view_sort_key_changed_cb (GConfClient* client,
				   guint cnxn_id,
				   GConfEntry *entry,
				   gpointer user_data)
{
	RBEntryView *view = user_data;

	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	rb_entry_view_sync_sorting (view);
}

static void
rb_entry_view_columns_config_changed_cb (GConfClient* client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer user_data)
{
	RBEntryView *view = user_data;

	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	rb_entry_view_sync_columns_visible (view);
}

static gint
propid_from_name (const char *name)
{
	GEnumClass *prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	GEnumClass *unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);
	GEnumValue *ev;
	int ret;

	ev = g_enum_get_value_by_name (prop_class, name);
	if (!ev)
		ev = g_enum_get_value_by_name (unsaved_prop_class, name);
	if (ev)
		ret = ev->value;
	else
		ret = -1;
	return ret;
}

static void
set_column_not_visible (guint propid, GtkTreeViewColumn *column, gpointer unused)
{
	/* title is always visible */
	if (propid == RHYTHMDB_PROP_TITLE)
		return;
	
	gtk_tree_view_column_set_visible (column, FALSE);
}

static void
rb_entry_view_sync_columns_visible (RBEntryView *view)
{
	char **items;
	GList *visible_properties = NULL, *tem;
	char *config = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);

	g_return_if_fail (view != NULL);
	g_return_if_fail (config != NULL);

	g_hash_table_foreach (view->priv->propid_column_map, (GHFunc) set_column_not_visible, NULL);

	items = g_strsplit (config, ",", 0);
	if (items != NULL) {
		int i;
		for (i = 0; items[i] != NULL && *(items[i]); i++) {
			int value = propid_from_name (items[i]);

			g_assert ((value >= 0) && (value < RHYTHMDB_NUM_PROPERTIES));
			visible_properties = g_list_append (visible_properties, GINT_TO_POINTER (value));
		}
		g_strfreev (items);
	}

	for (tem = visible_properties; tem; tem = tem->next) {
		GtkTreeViewColumn *column
			= g_hash_table_lookup (view->priv->propid_column_map, tem->data);
		if (column)
			gtk_tree_view_column_set_visible (column, TRUE);
	}

	g_list_free (visible_properties);
	g_free (config);
}

void
rb_entry_view_set_playing (RBEntryView *view,
			   gboolean playing)
{
	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	rb_entry_view_freeze (view);

	view->priv->playing = TRUE;

	rb_entry_view_thaw (view);
}

gboolean
rb_entry_view_poll_model (RBEntryView *view)
{
	GTimeVal timeout;
	gboolean did_sync;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, G_USEC_PER_SEC*0.75);

	did_sync = rhythmdb_query_model_poll (view->priv->model, &timeout);
	if (did_sync) {
		g_source_remove (view->priv->model_poll_id);
		view->priv->model_poll_id =
			g_idle_add ((GSourceFunc) idle_poll_model, view);
	}
	return did_sync;
}

static gboolean
idle_poll_model (RBEntryView *view)
{
	gboolean did_sync;
	GTimeVal timeout;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, G_USEC_PER_SEC*0.75);

	GDK_THREADS_ENTER ();

	did_sync = rhythmdb_query_model_poll (view->priv->model, &timeout);

	if (did_sync)
		view->priv->model_poll_id =
			g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc) idle_poll_model, view, NULL);
	else
		view->priv->model_poll_id =
			g_timeout_add (300, (GSourceFunc) idle_poll_model, view);

	GDK_THREADS_LEAVE ();

	return FALSE;
}
