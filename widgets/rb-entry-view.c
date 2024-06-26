/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

/**
 * SECTION:rbentryview
 * @short_description: a #GtkTreeView for displaying track listings
 *
 * This class provides a predefined set of columns for displaying the
 * common set of #RhythmDBEntry properties, but also allows custom columns
 * to be appended.  The 'playing' column is always created as the first
 * column in the tree view, displaying a playing or paused image next to
 * the currently playing entry, and also an error image next to entries for
 * which a playback error message has been set.  Clicking on the error
 * image opens a dialog displaying the full message.
 *
 * All columns added to entry view columns should be expandable, or have a fixed
 * minimum width set.  Otherwise, the tree view must measure the contents of each
 * row to assign sizes, which is very slow for large track lists.  All the predefined
 * column types handle this correctly.
 */

/**
 * RBEntryViewColumn:
 * @RB_ENTRY_VIEW_COL_TRACK_NUMBER: the track number column
 * @RB_ENTRY_VIEW_COL_TITLE: the title column
 * @RB_ENTRY_VIEW_COL_ARTIST: the artist column
 * @RB_ENTRY_VIEW_COL_COMPOSER: the composer column
 * @RB_ENTRY_VIEW_COL_ALBUM: the album column
 * @RB_ENTRY_VIEW_COL_GENRE: the genre column
 * @RB_ENTRY_VIEW_COL_DURATION: the duration column
 * @RB_ENTRY_VIEW_COL_QUALITY: the quality (bitrate) column
 * @RB_ENTRY_VIEW_COL_RATING: the rating column
 * @RB_ENTRY_VIEW_COL_PLAY_COUNT: the play count column
 * @RB_ENTRY_VIEW_COL_YEAR: the year (release date) column
 * @RB_ENTRY_VIEW_COL_LAST_PLAYED: the last played time column
 * @RB_ENTRY_VIEW_COL_FIRST_SEEN: the first seen (imported) column
 * @RB_ENTRY_VIEW_COL_LAST_SEEN: the last seen column
 * @RB_ENTRY_VIEW_COL_LOCATION: the location column
 * @RB_ENTRY_VIEW_COL_BPM: the BPM column
 * @RB_ENTRY_VIEW_COL_COMMENT: the comment column
 * @RB_ENTRY_VIEW_COL_ERROR: the error column
 *
 * Predefined column types to use in #RBEntryView<!-- -->s.  Use
 * #rb_entry_view_append_column to add these to an entry view.
 * The predefined column names map directly to the #RhythmDBEntry properties
 * the columns display.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>

#include "rb-tree-dnd.h"
#include "rb-entry-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-text-helpers.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-cell-renderer-rating.h"
#include "rb-shell-player.h"
#include "rb-cut-and-paste-code.h"
#include "nautilus-floating-bar.h"

static const GtkTargetEntry rb_entry_view_drag_types[] = {
	{ "application/x-rhythmbox-entry", 0, 0 },
	{ "text/uri-list", 0, 1 }
};

struct RBEntryViewColumnSortData
{
	GCompareDataFunc func;
	gpointer data;
	GDestroyNotify data_destroy;
};

/* GObject data item used to associate cell renderers with property IDs */
#define CELL_PROPID_ITEM "rb-cell-propid"

static void rb_entry_view_class_init (RBEntryViewClass *klass);
static void rb_entry_view_init (RBEntryView *view);
static void rb_entry_view_constructed (GObject *object);
static void rb_entry_view_dispose (GObject *object);
static void rb_entry_view_finalize (GObject *object);
static void rb_entry_view_sort_data_finalize (gpointer column,
					      gpointer sort_data,
					      gpointer user_data);
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
static void rb_entry_view_grab_focus (GtkWidget *widget);
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
static void rb_entry_view_rows_reordered_cb (GtkTreeModel *model,
					     GtkTreePath *path,
					     GtkTreeIter *iter,
					     gint *order,
					     RBEntryView *view);
static void rb_entry_view_sync_columns_visible (RBEntryView *view);
static void rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
				   const char *path,
				   double rating,
				   RBEntryView *view);
static void rb_entry_view_pixbuf_clicked_cb (RBEntryView *view,
					     const char *path,
					     RBCellRendererPixbuf *cellpixbuf);
static void rb_entry_view_playing_column_clicked_cb (GtkTreeViewColumn *column,
						     RBEntryView *view);
static gboolean rb_entry_view_button_press_cb (GtkTreeView *treeview,
					      GdkEventButton *event,
					      RBEntryView *view);
static gboolean rb_entry_view_popup_menu_cb (GtkTreeView *treeview,
					     RBEntryView *view);
static void rb_entry_view_entry_is_visible (RBEntryView *view, RhythmDBEntry *entry,
					    gboolean *realized, gboolean *visible,
					    GtkTreeIter *iter);
static void rb_entry_view_scroll_to_iter (RBEntryView *view,
					  GtkTreeIter *iter);
static gboolean rb_entry_view_emit_row_changed (RBEntryView *view,
						RhythmDBEntry *entry);
static void rb_entry_view_playing_song_changed (RBShellPlayer *player,
						RhythmDBEntry *entry,
						RBEntryView *view);

struct RBEntryViewPrivate
{
	RhythmDB *db;
	RBShellPlayer *shell_player;

	RhythmDBQueryModel *model;

	GtkWidget *overlay;
	GtkWidget *scrolled_window;
	GtkWidget *status;
	GtkWidget *treeview;
	GtkTreeSelection *selection;

	RBEntryViewState playing_state;
	RhythmDBQueryModel *playing_model;
	RhythmDBEntry *playing_entry;
	gboolean playing_entry_in_view;
	guint selection_changed_id;

	gboolean is_drag_source;
	gboolean is_drag_dest;

	char *sorting_key;
	GtkTreeViewColumn *sorting_column;
	gint sorting_order;
	char *sorting_column_name;
	RhythmDBPropType type_ahead_propid;
	char **visible_columns;

	gboolean have_selection, have_complete_selection;

	GHashTable *column_key_map;

	GHashTable *propid_column_map;
	GHashTable *column_sort_data_map;
};


enum
{
	ENTRY_ADDED,
	ENTRY_DELETED,
	ENTRIES_REPLACED,
	SELECTION_CHANGED,
	ENTRY_ACTIVATED,
	SHOW_POPUP,
	HAVE_SEL_CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_SHELL_PLAYER,
	PROP_MODEL,
	PROP_SORT_ORDER,
	PROP_IS_DRAG_SOURCE,
	PROP_IS_DRAG_DEST,
	PROP_PLAYING_STATE,
	PROP_VISIBLE_COLUMNS
};

G_DEFINE_TYPE (RBEntryView, rb_entry_view, GTK_TYPE_BOX)

static guint rb_entry_view_signals[LAST_SIGNAL] = { 0 };

static GQuark rb_entry_view_column_always_visible;

static gboolean
type_ahead_search_func (GtkTreeModel *model,
			gint column,
			const gchar *key,
			GtkTreeIter *iter,
			gpointer search_data)
{
	RBEntryView *view = RB_ENTRY_VIEW (search_data);
	RhythmDBEntry *entry;
	gchar *folded;
	const gchar *entry_folded;
	gboolean res;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	folded = rb_search_fold (key);
	entry_folded = rb_refstring_get_folded (rhythmdb_entry_get_refstring (entry, view->priv->type_ahead_propid));
	rhythmdb_entry_unref (entry);

	if (entry_folded == NULL || folded == NULL)
		return 1;

	res = (strstr (entry_folded, folded) == NULL);
	g_free (folded);

	return res;
}

static void
rb_entry_view_class_init (RBEntryViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = rb_entry_view_dispose;
	object_class->finalize = rb_entry_view_finalize;
	object_class->constructed = rb_entry_view_constructed;

	object_class->set_property = rb_entry_view_set_property;
	object_class->get_property = rb_entry_view_get_property;

	widget_class->grab_focus = rb_entry_view_grab_focus;

	/**
	 * RBEntryView:db:
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
	 * RBEntryView:shell-player:
	 *
	 * #RBShellPlayer instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBEntryView:model:
	 *
	 * The #RhythmDBQueryModel backing the view
	 */
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));
	/**
	 * RBEntryView:sort-order:
	 *
	 * The sort order for the track listing.
	 */
	g_object_class_install_property (object_class,
					 PROP_SORT_ORDER,
					 g_param_spec_string ("sort-order",
							      "sorting order",
							      "sorting order",
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * RBEntryView:is-drag-source:
	 *
	 * If TRUE, the view acts as a data source for drag and drop operations.
	 */
	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_SOURCE,
					 g_param_spec_boolean ("is-drag-source",
							       "is drag source",
							       "whether or not this is a drag source",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBEntryView:is-drag-dest:
	 *
	 * If TRUE, the view acts as a destination for drag and drop operations.
	 */
	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_DEST,
					 g_param_spec_boolean ("is-drag-dest",
							       "is drag dest",
							       "whether or not this is a drag dest",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBEntryView:playing-state:
	 *
	 * Determines the icon to show in the 'playing' column next to the current
	 * playing entry.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYING_STATE,
					 g_param_spec_int ("playing-state",
						 	   "playing state",
							   "playback state for this entry view",
							   RB_ENTRY_VIEW_NOT_PLAYING,
							   RB_ENTRY_VIEW_PAUSED,
							   RB_ENTRY_VIEW_NOT_PLAYING,
							   G_PARAM_READWRITE));
	/**
	 * RBEntryView:visible-columns:
	 *
	 * An array containing the names of the visible columns.
	 */
	g_object_class_install_property (object_class,
					 PROP_VISIBLE_COLUMNS,
					 g_param_spec_boxed ("visible-columns",
							     "visible columns",
							     "visible columns",
							     G_TYPE_STRV,
							     G_PARAM_READWRITE));
	/**
	 * RBEntryView::entry-added:
	 * @view: the #RBEntryView
	 * @entry: the #RhythmDBEntry that was added
	 *
	 * Emitted when an entry is added to the view
	 */
	rb_entry_view_signals[ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	/**
	 * RBEntryView::entry-deleted:
	 * @view: the #RBEntryView
	 * @entry: the #RhythmDBEntry that was removed
	 *
	 * Emitted when an entry has been removed from the view
	 */
	rb_entry_view_signals[ENTRY_DELETED] =
		g_signal_new ("entry-deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_deleted),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	/**
	 * RBEntryView::entries-replaced:
	 * @view: the #RBEntryView
	 *
	 * Emitted when the model backing the entry view is replaced.
	 */
	rb_entry_view_signals[ENTRIES_REPLACED] =
		g_signal_new ("entries-replaced",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entries_replaced),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBEntryView::entry-activated:
	 * @view: the #RBEntryView
	 * @entry: the #RhythmDBEntry that was activated
	 *
	 * Emitted when an entry in the view is activated (by double clicking
	 * or by various key presses)
	 */
	rb_entry_view_signals[ENTRY_ACTIVATED] =
		g_signal_new ("entry-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_activated),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	/**
	 * RBEntryView::selection-changed:
	 * @view: the #RBEntryView
	 *
	 * Emitted when the set of selected entries changes
	 */
	rb_entry_view_signals[SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, selection_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBEntryView::show-popup:
	 * @view: the #RBEntryView
	 * @over_entry: if TRUE, the popup request was made while pointing
	 * at an entry in the view
	 *
	 * Emitted when the user performs an action that should result in a
	 * popup menu appearing.  If the action was a mouse button click, 
	 * over_entry is FALSE if the mouse pointer was in the blank space after
	 * the last row in the view.  If the action was a key press, over_entry
	 * is FALSE if no rows in the view are selected.
	 */
	rb_entry_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, show_popup),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
	/**
	 * RBEntryView::have-selection-changed:
	 * @view: the #RBEntryView
	 * @have_selection: TRUE if one or more rows are selected
	 *
	 * Emitted when the user first selects a row, or when no rows are selected
	 * any more.
	 */
	rb_entry_view_signals[HAVE_SEL_CHANGED] =
		g_signal_new ("have_selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, have_selection_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (RBEntryViewPrivate));

	rb_entry_view_column_always_visible = g_quark_from_static_string ("rb_entry_view_column_always_visible");
}

static void
rb_entry_view_init (RBEntryView *view)
{
	view->priv = G_TYPE_INSTANCE_GET_PRIVATE (view, RB_TYPE_ENTRY_VIEW, RBEntryViewPrivate);

	view->priv->propid_column_map = g_hash_table_new (NULL, NULL);
	view->priv->column_sort_data_map = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	view->priv->column_key_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	view->priv->type_ahead_propid = RHYTHMDB_PROP_TITLE;
}

static void
rb_entry_view_dispose (GObject *object)
{
	RBEntryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_ENTRY_VIEW (object));

	view = RB_ENTRY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	if (view->priv->selection_changed_id > 0) {
		g_source_remove (view->priv->selection_changed_id);
		view->priv->selection_changed_id = 0;
	}

	if (view->priv->selection) {
		g_signal_handlers_disconnect_by_func (view->priv->selection,
						      G_CALLBACK (rb_entry_view_selection_changed_cb),
						      view);
		g_clear_object (&view->priv->selection);
	}

	if (view->priv->playing_model != NULL) {
		g_object_unref (view->priv->playing_model);
		view->priv->playing_model = NULL;
	}

	if (view->priv->model != NULL) {
		/* remove the model from the treeview so
		 * atk-bridge doesn't have to emit deletion events
		 * for each cell in the view.
		 */
		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview), NULL);

		g_object_unref (view->priv->model);
		view->priv->model = NULL;
	}

	G_OBJECT_CLASS (rb_entry_view_parent_class)->dispose (object);
}

static void
rb_entry_view_finalize (GObject *object)
{
	RBEntryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_ENTRY_VIEW (object));

	view = RB_ENTRY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_hash_table_destroy (view->priv->propid_column_map);
	g_hash_table_foreach (view->priv->column_sort_data_map,
			      rb_entry_view_sort_data_finalize, NULL);
	g_hash_table_destroy (view->priv->column_sort_data_map);
	g_hash_table_destroy (view->priv->column_key_map);

	g_free (view->priv->sorting_column_name);
	g_strfreev (view->priv->visible_columns);

	G_OBJECT_CLASS (rb_entry_view_parent_class)->finalize (object);
}

static void
rb_entry_view_sort_data_finalize (gpointer column,
				  gpointer gsort_data,
				  gpointer user_data)
{
	struct RBEntryViewColumnSortData *sort_data = gsort_data;

	if (sort_data->data_destroy) {
		sort_data->data_destroy (sort_data->data);

		sort_data->data_destroy = NULL;
		sort_data->data = NULL;
		sort_data->func = NULL;
	}
}

static void
rb_entry_view_set_shell_player_internal (RBEntryView   *view,
					 RBShellPlayer *player)
{
	if (view->priv->shell_player != NULL) {
		g_signal_handlers_disconnect_by_func (view->priv->shell_player,
						      G_CALLBACK (rb_entry_view_playing_song_changed),
						      view);
	}

	view->priv->shell_player = player;

	g_signal_connect_object (view->priv->shell_player,
				 "playing-song-changed",
				 G_CALLBACK (rb_entry_view_playing_song_changed),
				 view, 0);
}

static void
rb_entry_view_set_model_internal (RBEntryView        *view,
				  RhythmDBQueryModel *model)
{
	if (view->priv->model != NULL) {
		g_signal_handlers_disconnect_by_func (view->priv->model,
						      G_CALLBACK (rb_entry_view_row_inserted_cb),
						      view);
		g_signal_handlers_disconnect_by_func (view->priv->model,
						      G_CALLBACK (rb_entry_view_row_deleted_cb),
						      view);
		g_signal_handlers_disconnect_by_func (view->priv->model,
						      G_CALLBACK (rb_entry_view_rows_reordered_cb),
						      view);
		g_object_unref (view->priv->model);
	}

	gtk_tree_selection_unselect_all (view->priv->selection);

	view->priv->model = model;
	if (view->priv->model != NULL) {
		g_object_ref (view->priv->model);
		g_signal_connect_object (view->priv->model,
					 "row_inserted",
					 G_CALLBACK (rb_entry_view_row_inserted_cb),
					 view,
					 0);
		g_signal_connect_object (view->priv->model,
					 "row_deleted",
					 G_CALLBACK (rb_entry_view_row_deleted_cb),
					 view,
					 0);
		g_signal_connect_object (view->priv->model,
					 "rows_reordered",
					 G_CALLBACK (rb_entry_view_rows_reordered_cb),
					 view,
					 0);

		if (view->priv->sorting_column != NULL) {
			rb_entry_view_resort_model (view);
		}

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (view->priv->model));
	}

	view->priv->have_selection = FALSE;
	view->priv->have_complete_selection = FALSE;

	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRIES_REPLACED], 0);
}

static void
rb_entry_view_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBEntryView *view = RB_ENTRY_VIEW (object);

	switch (prop_id) {
	case PROP_DB:
		view->priv->db = g_value_get_object (value);
		break;
	case PROP_SHELL_PLAYER:
		rb_entry_view_set_shell_player_internal (view, g_value_get_object (value));
		break;
	case PROP_SORT_ORDER:
		rb_entry_view_set_sorting_type (view, g_value_get_string (value));
		break;
	case PROP_MODEL:
		rb_entry_view_set_model_internal (view, g_value_get_object (value));
		break;
	case PROP_IS_DRAG_SOURCE:
		view->priv->is_drag_source = g_value_get_boolean (value);
		break;
	case PROP_IS_DRAG_DEST:
		view->priv->is_drag_dest = g_value_get_boolean (value);
		break;
	case PROP_PLAYING_STATE:
		view->priv->playing_state = g_value_get_int (value);

		/* redraw the playing entry, as the icon will have changed */
		if (view->priv->playing_entry != NULL) {
			rb_entry_view_emit_row_changed (view, view->priv->playing_entry);
		}
		break;
	case PROP_VISIBLE_COLUMNS:
		g_strfreev (view->priv->visible_columns);
		view->priv->visible_columns = g_value_dup_boxed (value);
		rb_entry_view_sync_columns_visible (view);
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

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, view->priv->db);
		break;
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, view->priv->shell_player);
		break;
	case PROP_SORT_ORDER:
		g_value_take_string (value, rb_entry_view_get_sorting_type (view));
		break;
	case PROP_MODEL:
		g_value_set_object (value, view->priv->model);
		break;
	case PROP_IS_DRAG_SOURCE:
		g_value_set_boolean (value, view->priv->is_drag_source);
		break;
	case PROP_IS_DRAG_DEST:
		g_value_set_boolean (value, view->priv->is_drag_dest);
		break;
	case PROP_PLAYING_STATE:
		g_value_set_int (value, view->priv->playing_state);
		break;
	case PROP_VISIBLE_COLUMNS:
		g_value_set_boxed (value, view->priv->visible_columns);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_entry_view_new:
 * @db: the #RhythmDB instance
 * @shell_player: the #RBShellPlayer instance
 * @is_drag_source: if TRUE, the view should act as a drag and drop data source
 * @is_drag_dest: if TRUE, the view should act as a drag and drop destination
 *
 * Creates a new entry view.  If it makes sense to allow the user to drag entries
 * from this entry view to other sources, @is_drag_source should be TRUE.  If it
 * makes sense to allow the user to drag entries from other sources to this view,
 * @is_drag_dest should be TRUE.  Drag and drop in this sense is used for two purposes:
 * to transfer tracks between the filesystem and removable devices, and to add tracks
 * to playlists.
 *
 * Return value: the new entry view
 */
RBEntryView *
rb_entry_view_new (RhythmDB *db,
		   GObject *shell_player,
		   gboolean is_drag_source,
		   gboolean is_drag_dest)
{
	RBEntryView *view;

	view = RB_ENTRY_VIEW (g_object_new (RB_TYPE_ENTRY_VIEW,
					   "orientation", GTK_ORIENTATION_VERTICAL,
					   "hexpand", TRUE,
					   "vexpand", TRUE,
					   "db", db,
					   "shell-player", RB_SHELL_PLAYER (shell_player),
					   "is-drag-source", is_drag_source,
					   "is-drag-dest", is_drag_dest,
					   NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

/**
 * rb_entry_view_set_model:
 * @view: the #RBEntryView
 * @model: the new #RhythmDBQueryModel to use for the view
 *
 * Replaces the model backing the entry view.
 */
void
rb_entry_view_set_model (RBEntryView *view,
			 RhythmDBQueryModel *model)
{
	g_object_set (view, "model", model, NULL);
}

/* Sweet name, eh? */
struct RBEntryViewCellDataFuncData {
	RBEntryView *view;
	RhythmDBPropType propid;
};

static void
rb_entry_view_playing_cell_data_func (GtkTreeViewColumn *column,
				      GtkCellRenderer *renderer,
				      GtkTreeModel *tree_model,
				      GtkTreeIter *iter,
				      RBEntryView *view)
{
	RhythmDBEntry *entry;
	const char *name = NULL;

	entry = rhythmdb_query_model_iter_to_entry (view->priv->model, iter);

	if (entry == NULL) {
		return;
	}

	if (entry == view->priv->playing_entry) {
		switch (view->priv->playing_state) {
		case RB_ENTRY_VIEW_PLAYING:
			name = "media-playback-start-symbolic";
			break;
		case RB_ENTRY_VIEW_PAUSED:
			name = "media-playback-pause-symbolic";
			break;
		default:
			name = NULL;
			break;
		}
	}

	if (name == NULL && rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR)) {
		name = "dialog-error-symbolic";
	}

	g_object_set (renderer, "icon-name", name, NULL);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_rating_cell_data_func (GtkTreeViewColumn *column,
				     GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model,
				     GtkTreeIter *iter,
				     RBEntryView *view)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (view->priv->model, iter);

	g_object_set (renderer,
		      "rating", rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING),
		      NULL);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_bpm_cell_data_func (GtkTreeViewColumn *column,
				   GtkCellRenderer *renderer,
				   GtkTreeModel *tree_model,
				   GtkTreeIter *iter,
				   struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	gdouble val;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	val = rhythmdb_entry_get_double (entry, data->propid);

	if (val > 0.001)
		str = g_strdup_printf ("%.2f", val);
	else
		str = g_strdup ("");

	g_object_set (renderer, "text", str, NULL);
	g_free (str);
	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_long_cell_data_func (GtkTreeViewColumn *column,
				   GtkCellRenderer *renderer,
				   GtkTreeModel *tree_model,
				   GtkTreeIter *iter,
				   struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	gulong val;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	val = rhythmdb_entry_get_ulong (entry, data->propid);

	if (val > 0)
		str = g_strdup_printf ("%lu", val);
	else
		str = g_strdup ("");

	g_object_set (renderer, "text", str, NULL);
	g_free (str);
	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_play_count_cell_data_func (GtkTreeViewColumn *column,
					 GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model,
					 GtkTreeIter * iter,
					 struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	gulong i;
	char *str;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	i = rhythmdb_entry_get_ulong (entry, data->propid);
	if (i == 0)
		str = _("Never");
	else
		str = g_strdup_printf ("%ld", i);

	g_object_set (renderer, "text", str, NULL);
	if (i != 0)
		g_free (str);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_duration_cell_data_func (GtkTreeViewColumn *column,
				       GtkCellRenderer *renderer,
				       GtkTreeModel *tree_model,
				       GtkTreeIter *iter,
				       struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	gulong duration;
	char *str;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);
	duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

	str = rb_make_duration_string (duration);
	g_object_set (renderer, "text", str, NULL);
	g_free (str);
	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_year_cell_data_func (GtkTreeViewColumn *column,
				   GtkCellRenderer *renderer,
				   GtkTreeModel *tree_model,
				   GtkTreeIter *iter,
				   struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char str[255];
	int julian;
	GDate *date;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);
	julian = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);

	if (julian > 0) {
		date = g_date_new_julian (julian);
		g_date_strftime (str, sizeof (str), "%Y", date);
		g_object_set (renderer, "text", str, NULL);
		g_date_free (date);
	} else {
		g_object_set (renderer, "text", _("Unknown"), NULL);
	}

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_quality_cell_data_func (GtkTreeViewColumn *column,
				      GtkCellRenderer *renderer,
				      GtkTreeModel *tree_model,
				      GtkTreeIter *iter,
				      struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	gulong bitrate;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);
	bitrate = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE);

	if (rhythmdb_entry_is_lossless (entry)) {
		g_object_set (renderer, "text", _("Lossless"), NULL);
	} else if (bitrate == 0) {
		g_object_set (renderer, "text", _("Unknown"), NULL);
	} else {
		char *s;
	       
		s = g_strdup_printf (_("%lu kbps"), bitrate);
		g_object_set (renderer, "text", s, NULL);
		g_free (s);
	}

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_location_cell_data_func (GtkTreeViewColumn *column,
				       GtkCellRenderer *renderer,
				       GtkTreeModel *tree_model,
				       GtkTreeIter *iter,
				       struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	const char *location;
	char *str;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	location = rhythmdb_entry_get_string (entry, data->propid);
	str = g_uri_unescape_string (location, NULL);

	g_object_set (renderer, "text", str, NULL);
	g_free (str);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_string_cell_data_func (GtkTreeViewColumn *column,
				     GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model,
				     GtkTreeIter *iter,
				     struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	const char *str;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	str = rhythmdb_entry_get_string (entry, data->propid);
	if (str != NULL) {
		g_object_set (renderer, "text", str, NULL);
	}

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_sync_sorting (RBEntryView *view)
{
	GtkTreeViewColumn *column;
	gint direction;
	char *column_name;
	RhythmDBPropType type_ahead_propid;
	GList *renderers;

	direction = GTK_SORT_ASCENDING;
	column_name = NULL;
	rb_entry_view_get_sorting_order (view, &column_name, &direction);

	if (column_name == NULL) {
		return;
	}

	column = g_hash_table_lookup (view->priv->column_key_map, column_name);
	if (column == NULL) {
		rb_debug ("couldn't find column %s", column_name);
		g_free (column_name);
		return;
	}

	rb_debug ("Updating EntryView sort order to %s:%d", column_name, direction);

	/* remove the old sorting indicator */
	if (view->priv->sorting_column)
		gtk_tree_view_column_set_sort_indicator (view->priv->sorting_column, FALSE);

	/* set the sorting order and indicator of the new sorting column */
	view->priv->sorting_column = column;
	gtk_tree_view_column_set_sort_indicator (column, TRUE);
	gtk_tree_view_column_set_sort_order (column, direction);

	/* set the property id to use for the typeahead search */
	renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	type_ahead_propid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (renderers->data), CELL_PROPID_ITEM));
	g_list_free (renderers);
	if (type_ahead_propid != 0 && rhythmdb_get_property_type (view->priv->db, type_ahead_propid) == G_TYPE_STRING)
		view->priv->type_ahead_propid = type_ahead_propid;
	else
		view->priv->type_ahead_propid = RHYTHMDB_PROP_TITLE;

	g_free (column_name);
}

/**
 * rb_entry_view_get_sorting_type:
 * @view: an #RBEntryView
 *
 * Constructs a string that describes the sort settings for the entry view.
 * This consists of a column name and an order ('ascending' or 'descending')
 * separated by a comma.
 *
 * Return value: (transfer full): sort order description
 */
char *
rb_entry_view_get_sorting_type (RBEntryView *view)
{
	char *sorttype;
	GString *key = g_string_new (view->priv->sorting_column_name);

	g_string_append_c (key, ',');

	switch (view->priv->sorting_order)
	{
	case GTK_SORT_ASCENDING:
		g_string_append (key, "ascending");
		break;
	case GTK_SORT_DESCENDING:
		g_string_append (key, "descending");
		break;
	default:
		g_assert_not_reached ();
	}

	sorttype = key->str;
	g_string_free (key, FALSE);

	return sorttype;
}

/**
 * rb_entry_view_set_sorting_type:
 * @view: a #RBEntryView
 * @sorttype: sort order description
 *
 * Changes the sort order for the entry view.  The sort order
 * description must be a column name, followed by a comma, followed
 * by an order description ('ascending' or 'descending').
 */
void
rb_entry_view_set_sorting_type (RBEntryView *view,
				const char *sorttype)
{
	char **strs;

	if (!sorttype || !strchr (sorttype, ',')) {
		rb_debug ("malformed sort data: %s", (sorttype) ? sorttype : "(null)");
		return;
	}

	strs = g_strsplit (sorttype, ",", 0);

	g_free (view->priv->sorting_column_name);
	view->priv->sorting_column_name = g_strdup(strs[0]);

	if (!strcmp ("ascending", strs[1]))
		view->priv->sorting_order = GTK_SORT_ASCENDING;
	else if (!strcmp ("descending", strs[1]))
		view->priv->sorting_order = GTK_SORT_DESCENDING;
	else {
		g_warning ("atttempting to sort in unknown direction");
		view->priv->sorting_order = GTK_SORT_ASCENDING;
	}

	g_strfreev (strs);

	rb_entry_view_sync_sorting (view);
	g_object_notify (G_OBJECT (view), "sort-order");
}

/**
 * rb_entry_view_get_sorting_order:
 * @view: a #RBEntryView
 * @column_name: (out callee-allocates) (allow-none) (transfer full): returns the sort column name
 * @sort_order: (out) (allow-none): returns the sort ordering as a #GtkSortType value
 *
 * Retrieves the sort settings for the view.
 */
void
rb_entry_view_get_sorting_order (RBEntryView *view,
				 char **column_name,
				 gint *sort_order)
{
 	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	if (column_name != NULL) {
		*column_name = g_strdup (view->priv->sorting_column_name);
	}

	if (sort_order != NULL) {
		*sort_order = view->priv->sorting_order;
	}
}

/**
 * rb_entry_view_set_sorting_order:
 * @view: a #RBEntryView
 * @column_name: name of the column to sort on
 * @sort_order: order to sort in, as a #GtkSortType
 *
 * Sets the sort order for the entry view.
 */
void
rb_entry_view_set_sorting_order (RBEntryView *view,
				 const char *column_name,
				 gint sort_order)
{
	if (column_name == NULL)
		return;

	g_free (view->priv->sorting_column_name);
	view->priv->sorting_column_name = g_strdup (column_name);
	view->priv->sorting_order = sort_order;

	rb_entry_view_sync_sorting (view);
	g_object_notify (G_OBJECT (view), "sort-order");
}

static void
rb_entry_view_column_clicked_cb (GtkTreeViewColumn *column, RBEntryView *view)
{
	gint sort_order;
	char *clicked_column;

	rb_debug ("sorting on column %p", column);

	/* identify the clicked column, and then update the sorting order */
	clicked_column = (char*) g_object_get_data (G_OBJECT (column), "rb-entry-view-key");
	sort_order = view->priv->sorting_order;

	if (view->priv->sorting_column_name
	    && !strcmp(clicked_column, view->priv->sorting_column_name)
	    && (sort_order == GTK_SORT_ASCENDING))
		sort_order = GTK_SORT_DESCENDING;
	else
		sort_order = GTK_SORT_ASCENDING;

	rb_entry_view_set_sorting_order (view, clicked_column, sort_order);
}

/**
 * rb_entry_view_get_column:
 * @view: a #RBEntryView
 * @coltype: type of column to retrieve
 *
 * Retrieves a predefined column from the entry view.  This can be used
 * to insert additional cell renderers into the column.
 *
 * Return value: (transfer none): a #GtkTreeViewColumn instance, or NULL
 */
GtkTreeViewColumn *
rb_entry_view_get_column (RBEntryView *view, RBEntryViewColumn coltype)
{
	RhythmDBPropType propid;

	/* convert column type to property ID */
	switch (coltype) {
	case RB_ENTRY_VIEW_COL_TRACK_NUMBER:
		propid = RHYTHMDB_PROP_TRACK_NUMBER;
		break;
	case RB_ENTRY_VIEW_COL_TITLE:
		propid = RHYTHMDB_PROP_TITLE;
		break;
	case RB_ENTRY_VIEW_COL_ARTIST:
		propid = RHYTHMDB_PROP_ARTIST;
		break;
	case RB_ENTRY_VIEW_COL_ALBUM:
		propid = RHYTHMDB_PROP_ALBUM;
		break;
	case RB_ENTRY_VIEW_COL_GENRE:
		propid = RHYTHMDB_PROP_GENRE;
		break;
	case RB_ENTRY_VIEW_COL_COMMENT:
		propid = RHYTHMDB_PROP_COMMENT;
		break;
	case RB_ENTRY_VIEW_COL_DURATION:
		propid = RHYTHMDB_PROP_DURATION;
		break;
	case RB_ENTRY_VIEW_COL_YEAR:
		propid = RHYTHMDB_PROP_DATE;
		break;
	case RB_ENTRY_VIEW_COL_QUALITY:
		propid = RHYTHMDB_PROP_BITRATE;
		break;
	case RB_ENTRY_VIEW_COL_RATING:
		propid = RHYTHMDB_PROP_RATING;
		break;
	case RB_ENTRY_VIEW_COL_PLAY_COUNT:
		propid = RHYTHMDB_PROP_PLAY_COUNT;
		break;
	case RB_ENTRY_VIEW_COL_LAST_PLAYED:
		propid = RHYTHMDB_PROP_LAST_PLAYED;
		break;
	case RB_ENTRY_VIEW_COL_FIRST_SEEN:
		propid = RHYTHMDB_PROP_FIRST_SEEN;
		break;
	case RB_ENTRY_VIEW_COL_LAST_SEEN:
		propid = RHYTHMDB_PROP_LAST_SEEN;
		break;
	case RB_ENTRY_VIEW_COL_LOCATION:
		propid = RHYTHMDB_PROP_LOCATION;
		break;
	case RB_ENTRY_VIEW_COL_BPM:
		propid = RHYTHMDB_PROP_BPM;
		break;
	case RB_ENTRY_VIEW_COL_ERROR:
		propid = RHYTHMDB_PROP_PLAYBACK_ERROR;
		break;
	case RB_ENTRY_VIEW_COL_COMPOSER:
		propid = RHYTHMDB_PROP_COMPOSER;
		break;
	default:
		g_assert_not_reached ();
		propid = -1;
		break;
	}

	/* find the column */
	return (GtkTreeViewColumn *)g_hash_table_lookup (view->priv->propid_column_map, GINT_TO_POINTER (propid));
}

static void
rb_entry_view_cell_edited_cb (GtkCellRendererText *renderer,
			      char *path_str,
			      char *new_text,
			      RBEntryView *view)
{
	RhythmDBPropType propid;
	RhythmDBEntry *entry;
	GValue value = {0,};
	GtkTreePath *path;

	/* get the property corresponding to the cell, filter out properties we can't edit */
	propid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (renderer), CELL_PROPID_ITEM));
	switch (propid) {
	case RHYTHMDB_PROP_TITLE:
	case RHYTHMDB_PROP_GENRE:
	case RHYTHMDB_PROP_ARTIST:
	case RHYTHMDB_PROP_ALBUM:
	case RHYTHMDB_PROP_COMMENT:
	case RHYTHMDB_PROP_ARTIST_SORTNAME:
	case RHYTHMDB_PROP_ALBUM_SORTNAME:
		break;

	default:
		rb_debug ("can't edit property %s", rhythmdb_nice_elt_name_from_propid (view->priv->db, propid));
		return;
	}

	/* find entry */
	path = gtk_tree_path_new_from_string (path_str);
	entry = rhythmdb_query_model_tree_path_to_entry (view->priv->model, path);
	gtk_tree_path_free (path);

	if (entry != NULL) {
		/* update it */
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, new_text);
		rhythmdb_entry_set (view->priv->db, entry, propid, &value);
		g_value_unset (&value);

		rhythmdb_commit (view->priv->db);
		rhythmdb_entry_unref (entry);
	}
}


/**
 * rb_entry_view_append_column:
 * @view: a #RBEntryView
 * @coltype: type of column to append
 * @always_visible: if TRUE, ignore the user's column visibility settings
 *
 * Appends a predefined column type to the set of columns already present
 * in the entry view.  If @always_visible is TRUE, the column will ignore
 * the user's coulmn visibility settings and will always be visible.
 * This should only be used when it is vital for the purpose of the 
 * source that the column be visible.
 */
void
rb_entry_view_append_column (RBEntryView *view,
			     RBEntryViewColumn coltype,
			     gboolean always_visible)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer = NULL;
	struct RBEntryViewCellDataFuncData *cell_data;
	const char *title = NULL;
	const char *key = NULL;
	const char *strings[5] = {0};
	GtkTreeCellDataFunc cell_data_func = NULL;
	GCompareDataFunc sort_func = NULL;
	RhythmDBPropType propid;
	RhythmDBPropType sort_propid = RHYTHMDB_NUM_PROPERTIES;
	gboolean ellipsize = FALSE;
	gboolean resizable = TRUE;
	gint column_width = -1;
	gboolean has_numeric_data = FALSE;

	column = gtk_tree_view_column_new ();

	cell_data = g_new0 (struct RBEntryViewCellDataFuncData, 1);
	cell_data->view = view;

	switch (coltype) {
	case RB_ENTRY_VIEW_COL_TRACK_NUMBER:
		propid = RHYTHMDB_PROP_TRACK_NUMBER;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_long_cell_data_func;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_track_sort_func;
		title = _("Track");
		key = "Track";
		strings[0] = title;
		strings[1] = "9999";
		break;
	case RB_ENTRY_VIEW_COL_TITLE:
		propid = RHYTHMDB_PROP_TITLE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_TITLE_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_string_sort_func;
		title = _("Title");
		key = "Title";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_ARTIST:
		propid = RHYTHMDB_PROP_ARTIST;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_ARTIST_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_artist_sort_func;
		title = _("Artist");
		key = "Artist";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_COMPOSER:
		propid = RHYTHMDB_PROP_COMPOSER;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_COMPOSER_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_composer_sort_func;
		title = _("Composer");
		key = "Composer";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_ALBUM:
		propid = RHYTHMDB_PROP_ALBUM;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_ALBUM_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_album_sort_func;
		title = _("Album");
		key = "Album";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_GENRE:
		propid = RHYTHMDB_PROP_GENRE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_GENRE_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_genre_sort_func;
		title = _("Genre");
		key = "Genre";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_COMMENT:
		propid = RHYTHMDB_PROP_COMMENT;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_string_sort_func;
		title = _("Comment");
		key = "Comment";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_DURATION:
		propid = RHYTHMDB_PROP_DURATION;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_duration_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Time");
		key = "Time";
		strings[0] = title;
		strings[1] = "000:00";
		strings[2] = _("Unknown");
		break;
	case RB_ENTRY_VIEW_COL_YEAR:
		propid = RHYTHMDB_PROP_DATE;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_year_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_date_sort_func;
		title = _("Year");
		key = "Year";
		strings[0] = title;
		strings[1] = "0000";
		strings[2] = _("Unknown");
		break;
	case RB_ENTRY_VIEW_COL_QUALITY:
		propid = RHYTHMDB_PROP_BITRATE;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_quality_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_bitrate_sort_func;
		title = _("Quality");
		key = "Quality";
		strings[0] = title;
		strings[1] = _("000 kbps");
		strings[2] = _("Unknown");
		strings[3] = _("Lossless");
		break;
	case RB_ENTRY_VIEW_COL_RATING:
		propid = RHYTHMDB_PROP_RATING;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_double_ceiling_sort_func;

		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &column_width, NULL);
		column_width = column_width * 5 + 5;
		resizable = FALSE;
		title = _("Rating");
		key = "Rating";

		renderer = rb_cell_renderer_rating_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_entry_view_rating_cell_data_func,
							 view,
							 NULL);
		g_signal_connect_object (renderer,
					 "rated",
					 G_CALLBACK (rb_entry_view_rated_cb),
					 view,
					 0);
		break;
	case RB_ENTRY_VIEW_COL_PLAY_COUNT:
		propid = RHYTHMDB_PROP_PLAY_COUNT;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_play_count_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Play Count");
		key = "PlayCount";
		strings[0] = title;
		strings[1] = _("Never");
		strings[2] = "9999";
		break;
	case RB_ENTRY_VIEW_COL_LAST_PLAYED:
		propid = RHYTHMDB_PROP_LAST_PLAYED;
		has_numeric_data = TRUE;
		cell_data->propid = RHYTHMDB_PROP_LAST_PLAYED_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_LAST_PLAYED;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Last Played");
		key = "LastPlayed";
		strings[0] = title;
		strings[1] = rb_entry_view_get_time_date_column_sample ();
		strings[2] = _("Never");
		break;
	case RB_ENTRY_VIEW_COL_FIRST_SEEN:
		propid = RHYTHMDB_PROP_FIRST_SEEN;
		has_numeric_data = TRUE;
		cell_data->propid = RHYTHMDB_PROP_FIRST_SEEN_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_FIRST_SEEN;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Date Added");
		key = "FirstSeen";
		strings[0] = title;
		strings[1] = rb_entry_view_get_time_date_column_sample ();
		break;
	case RB_ENTRY_VIEW_COL_LAST_SEEN:
		propid = RHYTHMDB_PROP_LAST_SEEN;
		has_numeric_data = TRUE;
		cell_data->propid = RHYTHMDB_PROP_LAST_SEEN_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_LAST_SEEN;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Last Seen");
		key = "LastSeen";
		strings[0] = title;
		strings[1] = rb_entry_view_get_time_date_column_sample ();
		break;
	case RB_ENTRY_VIEW_COL_LOCATION:
		propid = RHYTHMDB_PROP_LOCATION;
		cell_data->propid = RHYTHMDB_PROP_LOCATION;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_location_cell_data_func;
		sort_propid = RHYTHMDB_PROP_LOCATION;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_location_sort_func;
		title = _("Location");
		key = "Location";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_BPM:
		propid = RHYTHMDB_PROP_BPM;
		has_numeric_data = TRUE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_bpm_cell_data_func;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_double_ceiling_sort_func;
		title = _("BPM");
		key = "BPM";
		strings[0] = title;
		strings[1] = "999.99";
		break;
	case RB_ENTRY_VIEW_COL_ERROR:
		propid = RHYTHMDB_PROP_PLAYBACK_ERROR;
		cell_data->propid = RHYTHMDB_PROP_PLAYBACK_ERROR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		title = _("Error");
		key = "Error";
		ellipsize = TRUE;
		break;
	default:
		g_assert_not_reached ();
		propid = -1;
		break;
	}

	if (sort_propid == RHYTHMDB_NUM_PROPERTIES)
		sort_propid = propid;

	if (renderer == NULL) {
		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 cell_data_func, cell_data, g_free);

		/* use tabular figures and right align columns
		 * with numeric data.
		 */
		if (has_numeric_data) {
			g_object_set (renderer,
				      "attributes", rb_text_numeric_get_pango_attr_list (),
				      "xalign", 1.0,
				      NULL);
			gtk_tree_view_column_set_alignment (column, 1.0);
		}

		g_object_set_data (G_OBJECT (renderer), CELL_PROPID_ITEM, GINT_TO_POINTER (propid));
		g_signal_connect_object (renderer, "edited",
					 G_CALLBACK (rb_entry_view_cell_edited_cb),
					 view, 0);
		g_object_set (renderer, "single-paragraph-mode", TRUE, NULL);
	} else {
		g_free (cell_data);
	}

	if (resizable)
		gtk_tree_view_column_set_resizable (column, TRUE);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_clickable (column, TRUE);

	if (always_visible)
		g_object_set_qdata (G_OBJECT (column),
				    rb_entry_view_column_always_visible,
				    GINT_TO_POINTER (1));

	g_hash_table_insert (view->priv->propid_column_map, GINT_TO_POINTER (propid), column);

	rb_entry_view_append_column_custom (view, column, title, key, sort_func, GINT_TO_POINTER (sort_propid), NULL);

	/*
	 * Columns must either be expanding (ellipsized) or have a
	 * fixed minimum width specified.  Otherwise, gtk+ gives them a
	 * width of 0.
	 */
	if (ellipsize) {
		g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	} else if (column_width != -1) {
		gtk_tree_view_column_set_fixed_width (column, column_width);
	} else {
		rb_entry_view_set_fixed_column_width (view, column, renderer, strings);
	}
}

/**
 * rb_entry_view_append_column_custom:
 * @view: a #RBEntryView
 * @column: (transfer full): a #GtkTreeViewColumn to append
 * @title: title for the column (translated)
 * @key: sort key for the column (not translated)
 * @sort_func: comparison function to use for sorting on the column
 * @data: (closure) (scope notified): data to pass to the sort function
 * @data_destroy: function to use to destroy the sort data
 *
 * Appends a custom column to the entry view.  
 */
void
rb_entry_view_append_column_custom (RBEntryView *view,
				    GtkTreeViewColumn *column,
				    const char *title,
				    const char *key,
				    GCompareDataFunc sort_func,
				    gpointer data,
				    GDestroyNotify data_destroy)
{
	rb_entry_view_insert_column_custom (view, column, title, key, sort_func, data, data_destroy, -1);
}

/**
 * rb_entry_view_insert_column_custom:
 * @view: a #RBEntryView
 * @column: (transfer full): a #GtkTreeViewColumn to append
 * @title: title for the column (translated)
 * @key: sort key for the column (not translated)
 * @sort_func: comparison function to use for sorting on the column
 * @data: (closure) (scope notified): data to pass to the sort function
 * @data_destroy: function to use to destroy the sort data
 * @position: position at which to insert the column (-1 to insert at the end)
 *
 * Inserts a custom column at the specified position.
 */
void
rb_entry_view_insert_column_custom (RBEntryView *view,
				    GtkTreeViewColumn *column,
				    const char *title,
				    const char *key,
				    GCompareDataFunc sort_func,
				    gpointer data,
				    GDestroyNotify data_destroy,
				    gint position)
{
	struct RBEntryViewColumnSortData *sortdata;

	gtk_tree_view_column_set_title (column, title);
	gtk_tree_view_column_set_reorderable (column, FALSE);


	g_object_set_data_full (G_OBJECT (column), "rb-entry-view-key",
				g_strdup (key), g_free);

	rb_debug ("appending column: %p (title: %s, key: %s)", column, title, key);

	gtk_tree_view_insert_column (GTK_TREE_VIEW (view->priv->treeview), column, position);

	if (sort_func != NULL) {
		sortdata = g_new (struct RBEntryViewColumnSortData, 1);
		sortdata->func = (GCompareDataFunc) sort_func;
		sortdata->data = data;
		sortdata->data_destroy = data_destroy;
		g_hash_table_insert (view->priv->column_sort_data_map, column, sortdata);

		g_signal_connect_object (column, "clicked",
					 G_CALLBACK (rb_entry_view_column_clicked_cb),
					 view, 0);
	}
	g_hash_table_insert (view->priv->column_key_map, g_strdup (key), column);

	rb_entry_view_sync_columns_visible (view);
	rb_entry_view_sync_sorting (view);
}

/**
 * rb_entry_view_set_columns_clickable:
 * @view: a #RBEntryView
 * @clickable: if TRUE, sortable columns will be made clickable
 *
 * Makes the headers for sortable columns (those for which a sort function was
 * provided) clickable, so the user can set the sort order.
 */
void
rb_entry_view_set_columns_clickable (RBEntryView *view,
				     gboolean clickable)
{
	GList *columns, *tem;

  	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
	for (tem = columns; tem; tem = tem->next) {
		/* only columns we can sort on should be clickable */
		GtkTreeViewColumn *column = (GtkTreeViewColumn *) tem->data;
		if (g_hash_table_lookup (view->priv->column_sort_data_map, column) != NULL)
			gtk_tree_view_column_set_clickable (tem->data, clickable);
	}
	g_list_free (columns);
}

static void
rb_entry_view_constructed (GObject *object)
{
	RBEntryView *view;
	RhythmDBQueryModel *query_model;

	RB_CHAIN_GOBJECT_METHOD (rb_entry_view_parent_class, constructed, object);

	view = RB_ENTRY_VIEW (object);

	view->priv->overlay = gtk_overlay_new ();
	gtk_widget_set_vexpand (view->priv->overlay, TRUE);
	gtk_widget_set_hexpand (view->priv->overlay, TRUE);
	gtk_container_add (GTK_CONTAINER (view), view->priv->overlay);
	gtk_widget_show (view->priv->overlay);

	/* NautilusFloatingBar needs enter and leavy notify events */
	gtk_widget_add_events (view->priv->overlay, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

	view->priv->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (view->priv->scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (view->priv->scrolled_window), GTK_SHADOW_NONE);
	gtk_widget_show (view->priv->scrolled_window);
	gtk_container_add (GTK_CONTAINER (view->priv->overlay), view->priv->scrolled_window);

	view->priv->treeview = gtk_tree_view_new ();
	gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view->priv->treeview), TRUE);

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view->priv->treeview),
					     type_ahead_search_func,
					     view, NULL);

	g_signal_connect_object (view->priv->treeview,
			         "button_press_event",
			         G_CALLBACK (rb_entry_view_button_press_cb),
			         view,
				 0);
	g_signal_connect_object (view->priv->treeview,
			         "row_activated",
			         G_CALLBACK (rb_entry_view_row_activated_cb),
			         view,
				 0);
	g_signal_connect_object (view->priv->treeview,
				 "popup_menu",
				 G_CALLBACK (rb_entry_view_popup_menu_cb),
				 view,
				 0);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect_object (view->priv->selection,
			         "changed",
			         G_CALLBACK (rb_entry_view_selection_changed_cb),
			         view,
				 0);
	g_object_ref (view->priv->selection);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_selection_set_mode (view->priv->selection, GTK_SELECTION_MULTIPLE);

	if (view->priv->is_drag_source) {
		rb_tree_dnd_add_drag_source_support (GTK_TREE_VIEW (view->priv->treeview),
						     GDK_BUTTON1_MASK,
						     rb_entry_view_drag_types,
						     G_N_ELEMENTS (rb_entry_view_drag_types),
						     GDK_ACTION_COPY);
	}

	if (view->priv->is_drag_dest) {
		rb_tree_dnd_add_drag_dest_support (GTK_TREE_VIEW (view->priv->treeview),
						   RB_TREE_DEST_CAN_DROP_BETWEEN | RB_TREE_DEST_EMPTY_VIEW_DROP,
						   rb_entry_view_drag_types,
						   G_N_ELEMENTS (rb_entry_view_drag_types),
						   GDK_ACTION_COPY | GDK_ACTION_MOVE);
	}

	gtk_container_add (GTK_CONTAINER (view->priv->scrolled_window), view->priv->treeview);

	{
		GtkTreeViewColumn *column;
		GtkCellRenderer *renderer;
		GtkWidget *image_widget;

		/* Playing icon column */
		column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
		renderer = rb_cell_renderer_pixbuf_new ();
		g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);
		if (gtk_check_version (3, 16, 0) != NULL) {
			g_object_set (renderer, "follow-state", TRUE, NULL);
		}

		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_entry_view_playing_cell_data_func,
							 view,
							 NULL);

		image_widget = gtk_image_new_from_icon_name ("audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);
		gtk_tree_view_column_set_widget (column, image_widget);
		gtk_widget_show_all (image_widget);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);
		g_signal_connect_swapped (renderer,
					  "pixbuf-clicked",
					  G_CALLBACK (rb_entry_view_pixbuf_clicked_cb),
					  view);

		gtk_widget_set_tooltip_text (gtk_tree_view_column_get_widget (column),
					     _("Now Playing"));

		g_signal_connect (column,
				  "clicked",
				  G_CALLBACK (rb_entry_view_playing_column_clicked_cb),
				  view);
		gtk_tree_view_column_set_clickable (column, TRUE);
	}

	query_model = rhythmdb_query_model_new_empty (view->priv->db);
	rb_entry_view_set_model (view, RHYTHMDB_QUERY_MODEL (query_model));
	g_object_unref (query_model);

	view->priv->status = nautilus_floating_bar_new (NULL, NULL, FALSE);
	gtk_widget_set_no_show_all (view->priv->status, TRUE);
	gtk_widget_set_halign (view->priv->status, GTK_ALIGN_END);
	gtk_widget_set_valign (view->priv->status, GTK_ALIGN_END);
	gtk_overlay_add_overlay (GTK_OVERLAY (view->priv->overlay), view->priv->status);
}

static void
rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
			const char *path_string,
			double rating,
			RBEntryView *view)
{
	GtkTreePath *path;
	RhythmDBEntry *entry;
	GValue value = { 0, };

	g_return_if_fail (rating >= 0 && rating <= 5 );
	g_return_if_fail (path_string != NULL);

	path = gtk_tree_path_new_from_string (path_string);
	entry = rhythmdb_query_model_tree_path_to_entry (view->priv->model, path);
	gtk_tree_path_free (path);

	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, rating);
	rhythmdb_entry_set (view->priv->db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);

	rhythmdb_commit (view->priv->db);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_pixbuf_clicked_cb (RBEntryView          *view,
				 const char           *path_string,
				 RBCellRendererPixbuf *cellpixbuf)
{
	GtkTreePath *path;
	RhythmDBEntry *entry;
	const gchar *error;

	g_return_if_fail (path_string != NULL);

	path = gtk_tree_path_new_from_string (path_string);
	entry = rhythmdb_query_model_tree_path_to_entry (view->priv->model, path);

	gtk_tree_path_free (path);

	error = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR);
	if (error) {
		rb_error_dialog (NULL, _("Playback Error"), "%s", error);
	}

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_playing_column_clicked_cb (GtkTreeViewColumn *column,
					 RBEntryView *view)
{
	if (view->priv->playing_entry) {
		rb_entry_view_scroll_to_entry (view, view->priv->playing_entry);
	}
}

static void
rb_entry_view_playing_song_changed (RBShellPlayer *player,
				    RhythmDBEntry *entry,
				    RBEntryView *view)
{
 	gboolean realized, visible;
  	GtkTreeIter iter;

 	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	if (view->priv->playing_entry != NULL) {
		if (view->priv->playing_state != RB_ENTRY_VIEW_NOT_PLAYING)
			rb_entry_view_emit_row_changed (view, view->priv->playing_entry);
		g_object_unref (view->priv->playing_model);
	}

 	view->priv->playing_entry = entry;
 	view->priv->playing_model = view->priv->model;
 	g_object_ref (view->priv->playing_model);

 	if (view->priv->playing_state != RB_ENTRY_VIEW_NOT_PLAYING) {
		if (view->priv->playing_entry != NULL) {
			view->priv->playing_entry_in_view =
				rb_entry_view_emit_row_changed (view, view->priv->playing_entry);
		}

		if (view->priv->playing_entry
		    && view->priv->playing_entry_in_view) {
		    rb_entry_view_entry_is_visible (view, view->priv->playing_entry,
						    &realized, &visible, &iter);
		    if (realized && !visible)
			    rb_entry_view_scroll_to_iter (view, &iter);
		}
	}
}

static gboolean
harvest_entries (GtkTreeModel *model,
		 GtkTreePath *path,
		 GtkTreeIter *iter,
		 GList      **list)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	*list = g_list_prepend (*list, entry);

	return FALSE;
}

/**
 * rb_entry_view_get_selected_entries:
 * @view: a #RBEntryView
 *
 * Gathers the selected entries from the view.
 *
 * Return value: (element-type RhythmDBEntry) (transfer full): a #GList of
 * selected entries in the view.
 */
GList *
rb_entry_view_get_selected_entries (RBEntryView *view)
{
	GList *list = NULL;

	gtk_tree_selection_selected_foreach (view->priv->selection,
					     (GtkTreeSelectionForeachFunc) harvest_entries,
					     (gpointer) &list);

	list = g_list_reverse (list);
	return list;
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
			GList *selected;
			entry = rhythmdb_query_model_tree_path_to_entry (view->priv->model, path);

			selected = rb_entry_view_get_selected_entries (view);

			if (!g_list_find (selected, entry))
				rb_entry_view_select_entry (view, entry);

			g_list_free (selected);

			rhythmdb_entry_unref (entry);
		}
		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SHOW_POPUP], 0, (path != NULL));
		return TRUE;
	}

	return FALSE;
}

static gboolean
rb_entry_view_popup_menu_cb (GtkTreeView *treeview,
			     RBEntryView *view)
{
	if (gtk_tree_selection_count_selected_rows (gtk_tree_view_get_selection (treeview)) == 0)
		return FALSE;

	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SHOW_POPUP], 0);
	return TRUE;
}

static gboolean
rb_entry_view_emit_selection_changed (RBEntryView *view)
{
	gboolean available;
	gint sel_count;

	sel_count = gtk_tree_selection_count_selected_rows (view->priv->selection);
	available = (sel_count > 0);

	if (available != view->priv->have_selection) {
		gint entry_count;

		entry_count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->model), NULL);
		view->priv->have_complete_selection = (sel_count == entry_count);

		view->priv->have_selection = available;

		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[HAVE_SEL_CHANGED], 0, available);
	}

	view->priv->selection_changed_id = 0;
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SELECTION_CHANGED], 0);
	return FALSE;
}

static void
rb_entry_view_selection_changed_cb (GtkTreeSelection *selection,
				    RBEntryView *view)
{
	if (view->priv->selection_changed_id == 0)
		view->priv->selection_changed_id = g_idle_add ((GSourceFunc)rb_entry_view_emit_selection_changed, view);
}

/**
 * rb_entry_view_have_selection:
 * @view: a #RBEntryView
 *
 * Determines whether there is an active selection in the view.
 *
 * Return value: TRUE if one or more rows are selected
 */
gboolean
rb_entry_view_have_selection (RBEntryView *view)
{
	return view->priv->have_selection;
}

/**
 * rb_entry_view_have_complete_selection:
 * @view: a #RBEntryView
 *
 * Determines whether all entries in the view are selected.
 *
 * Return value: TRUE if all rows in the view are selected
 */
gboolean
rb_entry_view_have_complete_selection (RBEntryView *view)
{
	return view->priv->have_complete_selection;
}

static void
rb_entry_view_row_activated_cb (GtkTreeView *treeview,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       RBEntryView *view)
{
	RhythmDBEntry *entry;

	rb_debug ("row activated");

	entry = rhythmdb_query_model_tree_path_to_entry (view->priv->model, path);

	rb_debug ("emitting entry activated");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_ACTIVATED], 0, entry);

	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_row_inserted_cb (GtkTreeModel *model,
			       GtkTreePath *path,
			       GtkTreeIter *iter,
			       RBEntryView *view)
{
	RhythmDBEntry *entry = rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (model), path);

	rb_debug ("row added");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_ADDED], 0, entry);
	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_row_deleted_cb (GtkTreeModel *model,
			      GtkTreePath *path,
			      RBEntryView *view)
{
	RhythmDBEntry *entry = rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (model), path);

	rb_debug ("row deleted");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_DELETED], 0, entry);
	rhythmdb_entry_unref (entry);
}

static void
rb_entry_view_rows_reordered_cb (GtkTreeModel *model,
				 GtkTreePath *path,
				 GtkTreeIter *iter,
				 gint *order,
				 RBEntryView *view)
{
	GList *selected_rows;
	GList *i;
	gint model_size;
	gboolean scrolled = FALSE;

	rb_debug ("rows reordered");

	model_size = gtk_tree_model_iter_n_children (model, NULL);

	/* check if a selected row was moved; if so, we'll
	 * need to move the selection too.
	 */
	selected_rows = gtk_tree_selection_get_selected_rows (view->priv->selection,
							      NULL);
	for (i = selected_rows; i != NULL; i = i->next) {
		GtkTreePath *path = (GtkTreePath *)i->data;
		gint index = gtk_tree_path_get_indices (path)[0];
		gint newindex;
		if (order[index] != index) {
			GtkTreePath *newpath;
			gtk_tree_selection_unselect_path (view->priv->selection, path);

			for (newindex = 0; newindex < model_size; newindex++) {
				if (order[newindex] == index) {
					newpath = gtk_tree_path_new_from_indices (newindex, -1);
					gtk_tree_selection_select_path (view->priv->selection, newpath);
					if (!scrolled) {
						GtkTreeViewColumn *col;
						GtkTreeView *treeview = GTK_TREE_VIEW (view->priv->treeview);

						col = gtk_tree_view_get_column (treeview, 0);
						gtk_tree_view_scroll_to_cell (treeview, newpath, col, TRUE, 0.5, 0.0);
						scrolled = TRUE;
					}
					gtk_tree_path_free (newpath);
					break;
				}
			}

		}
	}

	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	gtk_widget_queue_draw (GTK_WIDGET (view));
}

/**
 * rb_entry_view_select_all:
 * @view: a #RBEntryView
 *
 * Selects all rows in the view
 */
void
rb_entry_view_select_all (RBEntryView *view)
{
	gtk_tree_selection_select_all (view->priv->selection);
}

/**
 * rb_entry_view_select_none:
 * @view: a #RBEntryView
 *
 * Deselects all rows in the view.
 */
void
rb_entry_view_select_none (RBEntryView *view)
{
	gtk_tree_selection_unselect_all (view->priv->selection);
}

/**
 * rb_entry_view_select_entry:
 * @view: a #RBEntryView
 * @entry: a #RhythmDBEntry to select
 *
 * If the specified entry is present in the view, it is added
 * to the selection.
 */
void
rb_entry_view_select_entry (RBEntryView *view,
			    RhythmDBEntry *entry)
{
	GtkTreeIter iter;

	if (entry == NULL)
		return;

	rb_entry_view_select_none (view);

	if (rhythmdb_query_model_entry_to_iter (view->priv->model,
						entry, &iter)) {
		gtk_tree_selection_select_iter (view->priv->selection, &iter);
	}
}

/**
 * rb_entry_view_scroll_to_entry:
 * @view: a #RBEntryView
 * @entry: a #RhythmDBEntry to scroll to
 *
 * If the specified entry is present in the view, the view will be
 * scrolled so that the entry is visible.
 */
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

	/* It's possible to we can be asked to scroll the play queue's entry
	 * view to the playing entry before the view has ever been displayed.
	 * This will result in gtk+ warnings, so we avoid it in this case.
	 */
	if (!gtk_widget_get_realized (GTK_WIDGET (view)))
		return;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model), iter);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview), path,
				      gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				      TRUE, 0.5, 0.0);

	if ((gtk_tree_selection_count_selected_rows (view->priv->selection) != 1) ||
	    (gtk_tree_selection_path_is_selected (view->priv->selection, path) == FALSE)) {
		gtk_tree_selection_unselect_all (view->priv->selection);
		gtk_tree_selection_select_iter (view->priv->selection, iter);
	}

	gtk_tree_path_free (path);
}

/**
 * rb_entry_view_get_entry_visible:
 * @view: a #RBEntryView
 * @entry: a #RhythmDBEntry to check
 *
 * Determines whether a specified entry is present in the view
 * and is currently visible.
 *
 * Return value: TRUE if the entry is visible
 */
gboolean
rb_entry_view_get_entry_visible (RBEntryView *view,
				 RhythmDBEntry *entry)
{
	GtkTreeIter unused;
	gboolean realized, visible;

	if (view->priv->playing_model != view->priv->model)
		return FALSE;

	rb_entry_view_entry_is_visible (view, entry, &realized, &visible,
					&unused);
	return realized && visible;
}

/**
 * rb_entry_view_get_entry_contained:
 * @view: a #RBEntryView
 * @entry: a #RhythmDBEntry to check
 *
 * Determines whether a specified entry is present in the view.
 *
 * Return value: TRUE if the entry is present in the view
 */
gboolean
rb_entry_view_get_entry_contained (RBEntryView *view,
				   RhythmDBEntry *entry)
{
	GtkTreeIter unused;

	return rhythmdb_query_model_entry_to_iter (view->priv->model,
						   entry, &unused);
}

static void
rb_entry_view_entry_is_visible (RBEntryView *view,
				RhythmDBEntry *entry,
				gboolean *realized,
				gboolean *visible,
				GtkTreeIter *iter)
{
	GtkTreePath *path;
	GdkRectangle rect;

	*realized = FALSE;
	*visible = FALSE;

	g_return_if_fail (entry != NULL);

	if (!gtk_widget_get_realized (GTK_WIDGET (view)))
		return;

	*realized = TRUE;

	if (!rhythmdb_query_model_entry_to_iter (view->priv->model,
						 entry, iter))
		return;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model), iter);
	gtk_tree_view_get_cell_area (GTK_TREE_VIEW (view->priv->treeview),
				     path,
				     gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				     &rect);

	gtk_tree_path_free (path);

	*visible = (rect.y != 0 && rect.height != 0);
}

/**
 * rb_entry_view_enable_drag_source:
 * @view: a #RBEntryView
 * @targets: an array of #GtkTargetEntry structures defining the drag data targets
 * @n_targets: the number of entries in the target array
 *
 * Enables the entry view to act as a data source for drag an drop operations,
 * using a specified set of data targets.
 */
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
set_column_visibility (guint propid,
		       GtkTreeViewColumn *column,
		       GList *visible_props)
{
	gboolean visible;

	if (g_object_get_qdata (G_OBJECT (column),
				rb_entry_view_column_always_visible) == GINT_TO_POINTER (1))
		visible = TRUE;
	else
		visible = (g_list_find (visible_props, GINT_TO_POINTER (propid)) != NULL);

	gtk_tree_view_column_set_visible (column, visible);
}

static void
rb_entry_view_sync_columns_visible (RBEntryView *view)
{
	GList *visible_properties = NULL;

	g_return_if_fail (view != NULL);

	if (view->priv->visible_columns != NULL) {
		int i;
		for (i = 0; view->priv->visible_columns[i] != NULL && *(view->priv->visible_columns[i]); i++) {
			int value = rhythmdb_propid_from_nice_elt_name (view->priv->db, (const xmlChar *)view->priv->visible_columns[i]);
			rb_debug ("visible columns: %s => %d", view->priv->visible_columns[i], value);

			if ((value >= 0) && (value < RHYTHMDB_NUM_PROPERTIES))
				visible_properties = g_list_prepend (visible_properties, GINT_TO_POINTER (value));
		}
	}

	g_hash_table_foreach (view->priv->propid_column_map, (GHFunc) set_column_visibility, visible_properties);
	g_list_free (visible_properties);
}

/**
 * rb_entry_view_set_state:
 * @view: a #RBEntryView
 * @state: the new playing entry state
 *
 * Sets the icon to be drawn in the 'playing' column next to the
 * current playing entry.  RB_ENTRY_VIEW_PLAYING and RB_ENTRY_VIEW_PAUSED
 * should be used when the source containing the entry view is playing,
 * and RB_ENTRY_VIEW_NOT_PLAYING otherwise.
 */
void
rb_entry_view_set_state (RBEntryView *view,
			 RBEntryViewState state)
{
	g_return_if_fail (RB_IS_ENTRY_VIEW (view));
	g_object_set (view, "playing-state", state, NULL);
}

static void
rb_entry_view_grab_focus (GtkWidget *widget)
{
	RBEntryView *view = RB_ENTRY_VIEW (widget);

	gtk_widget_grab_focus (GTK_WIDGET (view->priv->treeview));
}

static gboolean
rb_entry_view_emit_row_changed (RBEntryView *view,
				RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (!rhythmdb_query_model_entry_to_iter (view->priv->model, entry, &iter))
		return FALSE;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->model),
					&iter);
	gtk_tree_model_row_changed (GTK_TREE_MODEL (view->priv->model),
				    path, &iter);
	gtk_tree_path_free (path);
	return TRUE;
}

/**
 * rb_entry_view_set_fixed_column_width:
 * @view: a #RBEntryView
 * @column: the column to set the width for
 * @renderer: a temporary cell renderer to use
 * @strings: (array zero-terminated=1): a NULL-terminated array of strings that will be displayed in the column
 *
 * Helper function for calling @rb_set_tree_view_column_fixed_width on
 * a column.  This is important for performance reasons, as having the
 * tree view measure the strings in each of 20000 rows is very slow.
 */
void
rb_entry_view_set_fixed_column_width (RBEntryView *view,
				      GtkTreeViewColumn *column,
				      GtkCellRenderer *renderer,
				      const gchar **strings)
{
	rb_set_tree_view_column_fixed_width (view->priv->treeview,
					     column,
					     renderer,
					     strings,
					     5);
}

/**
 * rb_entry_view_get_time_date_column_sample:
 *
 * Returns a sample string for use in columns displaying times
 * and dates in 'friendly' form (see @rb_utf_friendly_time).
 * For use with @rb_entry_view_set_fixed_column_width.
 *
 * Return value: sample date string
 */
const char *
rb_entry_view_get_time_date_column_sample (void)
{
	static const char *sample = NULL;
	if (sample == NULL) {
 		time_t then;

 		/* A reasonable estimate of the widest friendly date
 		   is "Yesterday NN:NN PM" */
 		then = time (NULL) - 86400;
 		sample = rb_utf_friendly_time (then);
  	}

	return sample;
}

/**
 * rb_entry_view_resort_model:
 * @view: a #RBEntryView to resort
 *
 * Resorts the entries in the entry view.  Mostly to be used
 * when a new model is associated with the view.
 */
void
rb_entry_view_resort_model (RBEntryView *view)
{
	struct RBEntryViewColumnSortData *sort_data;

	if (view->priv->sorting_column == NULL) {
		rb_debug ("can't sort yet, the sorting column isn't here");
		return;
	}

	sort_data = g_hash_table_lookup (view->priv->column_sort_data_map,
					 view->priv->sorting_column);
	g_assert (sort_data);

	rhythmdb_query_model_set_sort_order (view->priv->model,
					     sort_data->func,
					     sort_data->data,
					     NULL,
					     (view->priv->sorting_order == GTK_SORT_DESCENDING));
}

/**
 * rb_entry_view_set_column_editable:
 * @view: a #RBEntryView
 * @column: a #RBEntryViewColumn to update
 * @editable: %TRUE to make the column editable, %FALSE otherwise
 *
 * Enables in-place editing of the values in a column.
 * The underlying %RhythmDBEntry is updated when editing is complete.
 */
void
rb_entry_view_set_column_editable (RBEntryView *view,
				   RBEntryViewColumn column_type,
				   gboolean editable)
{
	GtkTreeViewColumn *column;
	GList *renderers;

	column = rb_entry_view_get_column (view, column_type);
	if (column == NULL)
		return;

	renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	g_object_set (renderers->data, "editable", editable, NULL);
	g_list_free (renderers);
}

/**
 * rb_entry_view_set_status:
 * @view: a #RBEntryView
 * @status: status text to display, or NULL
 * @busy: whether the source is busy
 *
 * Sets the status text to be displayed inside the entry view, and
 * shows the spinner if busy.
 */
void
rb_entry_view_set_status (RBEntryView *view, const char *status, gboolean busy)
{
	if (status == NULL) {
		gtk_widget_hide (view->priv->status);
	} else {
		nautilus_floating_bar_set_primary_label (NAUTILUS_FLOATING_BAR (view->priv->status), status);
		nautilus_floating_bar_set_show_spinner (NAUTILUS_FLOATING_BAR (view->priv->status), busy);
		gtk_widget_show (view->priv->status);
	}
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_entry_view_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_TRACK_NUMBER, "track-number"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_TITLE, "title"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ARTIST, "artist"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ALBUM, "album"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_GENRE, "genre"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_COMMENT, "comment"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_DURATION, "duration"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_QUALITY, "quality"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_RATING, "rating"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_PLAY_COUNT, "play-count"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_YEAR, "year"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LAST_PLAYED, "last-played"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_FIRST_SEEN, "first-seen"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LAST_SEEN, "last-seen"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LOCATION, "location"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_BPM, "bpm"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ERROR, "error"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_COMPOSER, "composer"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBEntryViewColumn", values);
	}

	return etype;
}

GType
rb_entry_view_state_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_ENTRY_VIEW_NOT_PLAYING, "not-playing"),
			ENUM_ENTRY (RB_ENTRY_VIEW_PLAYING, "playing"),
			ENUM_ENTRY (RB_ENTRY_VIEW_PAUSED, "paused"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBEntryViewState", values);
	}

	return etype;
}
