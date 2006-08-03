/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-tree-dnd.h"
#include "rb-entry-view.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-cell-renderer-rating.h"
#include "rb-stock-icons.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rb-shell-player.h"
#include "rb-cut-and-paste-code.h"

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
static void rb_entry_view_columns_config_changed_cb (GConfClient* client,
						    guint cnxn_id,
						    GConfEntry *entry,
						    gpointer user_data);
static void rb_entry_view_sort_key_changed_cb (GConfClient* client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       gpointer user_data);
static void rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
				   const char *path,
				   double rating,
				   RBEntryView *view);
static void rb_entry_view_pixbuf_clicked_cb (RBEntryView *view,
					     const char *path,
					     RBCellRendererPixbuf *cellpixbuf);
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

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	RBEntryViewState playing_state;
	RhythmDBQueryModel *playing_model;
	RhythmDBEntry *playing_entry;
	gboolean playing_entry_in_view;
	guint selection_changed_id;

	gboolean is_drag_source;
	gboolean is_drag_dest;

	GdkPixbuf *playing_pixbuf;
	GdkPixbuf *paused_pixbuf;
	GdkPixbuf *error_pixbuf;

	char *sorting_key;
	guint sorting_gconf_notification_id;
	GtkTreeViewColumn *sorting_column;
	gint sorting_order;
	char *sorting_column_name;

	gboolean have_selection, have_complete_selection;

	GHashTable *column_key_map;

	guint gconf_notification_id;
	GHashTable *propid_column_map;
	GHashTable *column_sort_data_map;
};

#define RB_ENTRY_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_ENTRY_VIEW, RBEntryViewPrivate))

enum
{
	ENTRY_ADDED,
	ENTRY_DELETED,
	ENTRIES_REPLACED,
	SELECTION_CHANGED,
	ENTRY_ACTIVATED,
	SHOW_POPUP,
	HAVE_SEL_CHANGED,
	SORT_ORDER_CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_SHELL_PLAYER,
	PROP_MODEL,
	PROP_SORTING_KEY,
	PROP_IS_DRAG_SOURCE,
	PROP_IS_DRAG_DEST,
	PROP_PLAYING_STATE
};

G_DEFINE_TYPE (RBEntryView, rb_entry_view, GTK_TYPE_SCROLLED_WINDOW)

static guint rb_entry_view_signals[LAST_SIGNAL] = { 0 };

static GQuark rb_entry_view_column_always_visible;

static gboolean
type_ahead_search_func (GtkTreeModel *model,
			gint column,
			const gchar *key,
			GtkTreeIter *iter,
			gpointer search_data)
{
	RhythmDBEntry *entry;
	gchar *folded;
	const gchar *entry_folded;
	gboolean res;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	folded = rb_search_fold (key);
	entry_folded = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
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

	object_class->finalize = rb_entry_view_finalize;
	object_class->constructor = rb_entry_view_constructor;

	object_class->set_property = rb_entry_view_set_property;
	object_class->get_property = rb_entry_view_get_property;

	widget_class->grab_focus = rb_entry_view_grab_focus;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel",
							      RHYTHMDB_TYPE_QUERY_MODEL,
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
	g_object_class_install_property (object_class,
					 PROP_PLAYING_STATE,
					 g_param_spec_int ("playing-state",
						 	   "playing state",
							   "playback state for this entry view",
							   RB_ENTRY_VIEW_NOT_PLAYING,
							   RB_ENTRY_VIEW_PAUSED,
							   RB_ENTRY_VIEW_NOT_PLAYING,
							   G_PARAM_READWRITE));
	rb_entry_view_signals[ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	rb_entry_view_signals[ENTRY_DELETED] =
		g_signal_new ("entry-deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	rb_entry_view_signals[ENTRIES_REPLACED] =
		g_signal_new ("entries-replaced",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entries_replaced),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_entry_view_signals[ENTRY_ACTIVATED] =
		g_signal_new ("entry-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);
	rb_entry_view_signals[SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, selection_changed),
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
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
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

	g_type_class_add_private (klass, sizeof (RBEntryViewPrivate));

	rb_entry_view_column_always_visible = g_quark_from_static_string ("rb_entry_view_column_always_visible");
}

static void
rb_entry_view_init (RBEntryView *view)
{
	GtkIconTheme *icon_theme;

	view->priv = RB_ENTRY_VIEW_GET_PRIVATE (view);

	icon_theme = gtk_icon_theme_get_default ();

	view->priv->playing_pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                   			       "stock_media-play",
                                   			       16,
                                   			       0,
                                   			       NULL);
	view->priv->paused_pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                   			      "stock_media-pause",
                                   			      16,
                                   			      0,
                                   			      NULL);
	view->priv->error_pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                   			     "stock_dialog-error",
                                   			     16,
                                   			     0,
                                   			     NULL);

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

	if (view->priv->gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->gconf_notification_id);
	if (view->priv->sorting_gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->sorting_gconf_notification_id);

	if (view->priv->selection_changed_id > 0)
		g_source_remove (view->priv->selection_changed_id);

	g_hash_table_destroy (view->priv->propid_column_map);
	g_hash_table_destroy (view->priv->column_sort_data_map);
	g_hash_table_destroy (view->priv->column_key_map);

	if (view->priv->playing_pixbuf != NULL)
		g_object_unref (view->priv->playing_pixbuf);
	if (view->priv->paused_pixbuf != NULL)
		g_object_unref (view->priv->paused_pixbuf);
	if (view->priv->error_pixbuf != NULL)
		g_object_unref (view->priv->error_pixbuf);

	if (view->priv->playing_model != NULL) {
		g_object_unref (view->priv->playing_model);
	}
	if (view->priv->model != NULL) {
		g_object_unref (view->priv->model);
	}

	g_free (view->priv->sorting_key);
	g_free (view->priv->sorting_column_name);

	G_OBJECT_CLASS (rb_entry_view_parent_class)->finalize (object);
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
	}

	if (view->priv->sorting_column != NULL) {
		rb_entry_view_resort_model (view);
	}

	if (view->priv->model != NULL) {
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
	case PROP_SORTING_KEY:
		g_free (view->priv->sorting_key);
		view->priv->sorting_key = g_value_dup_string (value);
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
	case PROP_SORTING_KEY:
		g_value_set_string (value, view->priv->sorting_key);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBEntryView *
rb_entry_view_new (RhythmDB *db,
		   GObject *shell_player,
		   const char *sort_key,
		   gboolean is_drag_source,
		   gboolean is_drag_dest)
{
	RBEntryView *view;

	view = RB_ENTRY_VIEW (g_object_new (RB_TYPE_ENTRY_VIEW,
					   "hadjustment", NULL,
					   "vadjustment", NULL,
					   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					   "vscrollbar_policy", GTK_POLICY_ALWAYS,
					   "shadow_type", GTK_SHADOW_IN,
					   "db", db,
					   "shell-player", RB_SHELL_PLAYER (shell_player),
					   "sort-key", sort_key,
					   "is-drag-source", is_drag_source,
					   "is-drag-dest", is_drag_dest,
					   NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

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
	GdkPixbuf *pixbuf = NULL;

	entry = rhythmdb_query_model_iter_to_entry (view->priv->model, iter);

	if (entry == NULL) {
		return;
	}

	if (entry == view->priv->playing_entry) {
		switch (view->priv->playing_state) {
		case RB_ENTRY_VIEW_PLAYING:
			pixbuf = view->priv->playing_pixbuf;
			break;
		case RB_ENTRY_VIEW_PAUSED:
			pixbuf = view->priv->paused_pixbuf;
			break;
		default:
			pixbuf = NULL;
			break;
		}
	}

	if (pixbuf == NULL && rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR)) {
		pixbuf = view->priv->error_pixbuf;
	}

	g_object_set (renderer, "pixbuf", pixbuf, NULL);

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
	guint bitrate;

	entry = rhythmdb_query_model_iter_to_entry (data->view->priv->model, iter);

	bitrate = rhythmdb_entry_get_ulong (entry, data->propid);

	if (bitrate > 0) {
		char *s = g_strdup_printf (_("%u kbps"), (guint)bitrate);
		g_object_set (renderer, "text", s, NULL);
		g_free (s);
	} else {
		g_object_set (renderer, "text", _("Unknown"), NULL);
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
	str = gnome_vfs_unescape_string_for_display (location);

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

	column_name = NULL;
	rb_entry_view_get_sorting_order (view, &column_name, &direction);

	if (column_name == NULL) {
		return;
	}

	column = g_hash_table_lookup (view->priv->column_key_map, column_name);
	if (column == NULL) {
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

	rb_debug ("emitting sort order changed");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SORT_ORDER_CHANGED], 0);

	g_free (column_name);
}

const char *
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

	sorttype = g_strdup(key->str);
	g_string_free (key, TRUE);

	return sorttype;
}

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
}

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

	/* update the sort order in GConf */
	if (view->priv->sorting_key)
		eel_gconf_set_string (view->priv->sorting_key, rb_entry_view_get_sorting_type(view));
}

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
	const char *strings[4] = {0};
	GtkTreeCellDataFunc cell_data_func = NULL;
	GCompareDataFunc sort_func = NULL;
	RhythmDBPropType propid;
	RhythmDBPropType sort_propid = RHYTHMDB_NUM_PROPERTIES;
	gboolean ellipsize = FALSE;
	gboolean resizable = TRUE;
	gint column_width = -1;

	column = gtk_tree_view_column_new ();

	cell_data = g_new0 (struct RBEntryViewCellDataFuncData, 1);
	cell_data->view = view;

	switch (coltype) {
	case RB_ENTRY_VIEW_COL_TRACK_NUMBER:
		propid = RHYTHMDB_PROP_TRACK_NUMBER;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_long_cell_data_func;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_track_sort_func;
		title = _("Trac_k");
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
		title = _("_Title");
		key = "Title";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_ARTIST:
		propid = RHYTHMDB_PROP_ARTIST;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_ARTIST_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_artist_sort_func;
		title = _("Art_ist");
		key = "Artist";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_ALBUM:
		propid = RHYTHMDB_PROP_ALBUM;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_ALBUM_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_album_sort_func;
		title = _("_Album");
		key = "Album";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_GENRE:
		propid = RHYTHMDB_PROP_GENRE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_GENRE_SORT_KEY;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_genre_sort_func;
		title = _("_Genre");
		key = "Genre";
		ellipsize = TRUE;
		break;
	case RB_ENTRY_VIEW_COL_DURATION:
		propid = RHYTHMDB_PROP_DURATION;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_duration_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Tim_e");
		key = "Time";
		strings[0] = title;
		strings[1] = "000:00";
		strings[2] = _("Unknown");
		break;
	case RB_ENTRY_VIEW_COL_YEAR:
		propid = RHYTHMDB_PROP_DATE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_year_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_date_sort_func;
		title = _("_Year");
		key = "Year";
		strings[0] = title;
		strings[1] = "0000";
		strings[2] = _("Unknown");
		break;
	case RB_ENTRY_VIEW_COL_QUALITY:
		propid = RHYTHMDB_PROP_BITRATE;
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_quality_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("_Quality");
		key = "Quality";
		strings[0] = title;
		strings[1] = _("000 kbps");
		strings[2] = _("Unknown");
		break;
	case RB_ENTRY_VIEW_COL_RATING:
		propid = RHYTHMDB_PROP_RATING;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_double_ceiling_sort_func;

		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &column_width, NULL);
		column_width = column_width * 5 + 5;
		resizable = FALSE;
		title = _("_Rating");
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
		cell_data->propid = propid;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_play_count_cell_data_func;
		sort_propid = cell_data->propid;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("_Play Count");
		key = "PlayCount";
		strings[0] = title;
		strings[1] = _("Never");
		strings[2] = "9999";
		break;
	case RB_ENTRY_VIEW_COL_LAST_PLAYED:
		propid = RHYTHMDB_PROP_LAST_PLAYED;
		cell_data->propid = RHYTHMDB_PROP_LAST_PLAYED_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_LAST_PLAYED;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("_Last Played");
		key = "LastPlayed";
		strings[0] = title;
		strings[1] = rb_entry_view_get_time_date_column_sample ();
		strings[2] = _("Never");
		break;
	case RB_ENTRY_VIEW_COL_FIRST_SEEN:
		propid = RHYTHMDB_PROP_FIRST_SEEN;
		cell_data->propid = RHYTHMDB_PROP_FIRST_SEEN_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_FIRST_SEEN;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("_Date Added");
		key = "FirstSeen";
		strings[0] = title;
		strings[1] = rb_entry_view_get_time_date_column_sample ();
		break;
	case RB_ENTRY_VIEW_COL_LAST_SEEN:
		propid = RHYTHMDB_PROP_LAST_SEEN;
		cell_data->propid = RHYTHMDB_PROP_LAST_SEEN_STR;
		cell_data_func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;
		sort_propid = RHYTHMDB_PROP_LAST_SEEN;
		sort_func = (GCompareDataFunc) rhythmdb_query_model_ulong_sort_func;
		title = _("Last _Seen");
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
		title = _("L_ocation");
		key = "Location";
		ellipsize = TRUE;
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
	} else {
		g_free (cell_data);
	}

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

	if (resizable)
		gtk_tree_view_column_set_resizable (column, TRUE);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_clickable (column, TRUE);

	if (always_visible)
		g_object_set_qdata (G_OBJECT (column),
				    rb_entry_view_column_always_visible,
				    GINT_TO_POINTER (1));

	g_hash_table_insert (view->priv->propid_column_map, GINT_TO_POINTER (propid), column);

	rb_entry_view_append_column_custom (view, column, title, key, sort_func, GINT_TO_POINTER (sort_propid));
}

void
rb_entry_view_append_column_custom (RBEntryView *view,
				    GtkTreeViewColumn *column,
				    const char *title,
				    const char *key,
				    GCompareDataFunc sort_func,
				    gpointer data)
{
	rb_entry_view_insert_column_custom (view, column, title, key, sort_func, data, -1);
}

void
rb_entry_view_insert_column_custom (RBEntryView *view,
				    GtkTreeViewColumn *column,
				    const char *title,
				    const char *key,
				    GCompareDataFunc sort_func,
				    gpointer data,
				    gint position)
{
	struct RBEntryViewColumnSortData *sortdata;

	gtk_tree_view_column_set_title (column, title);
	gtk_tree_view_column_set_reorderable (column, FALSE);

	g_signal_connect_object (column, "clicked",
				 G_CALLBACK (rb_entry_view_column_clicked_cb),
				 view, 0);

	g_object_set_data_full (G_OBJECT (column), "rb-entry-view-key",
				g_strdup (key), g_free);

	rb_debug ("appending column: %p (%s)", column, title);

	gtk_tree_view_insert_column (GTK_TREE_VIEW (view->priv->treeview), column, position);

	if (sort_func != NULL) {
		sortdata = g_new (struct RBEntryViewColumnSortData, 1);
		sortdata->func = (GCompareDataFunc) sort_func;
		sortdata->data = data;
		g_hash_table_insert (view->priv->column_sort_data_map, column, sortdata);
	}
	g_hash_table_insert (view->priv->column_key_map, g_strdup (key), column);

	rb_entry_view_sync_columns_visible (view);
	rb_entry_view_sync_sorting (view);
}

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

static GObject *
rb_entry_view_constructor (GType type,
			   guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBEntryView *view;
	RBEntryViewClass *klass;
	klass = RB_ENTRY_VIEW_CLASS (g_type_class_peek (RB_TYPE_ENTRY_VIEW));

	view = RB_ENTRY_VIEW (G_OBJECT_CLASS (rb_entry_view_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	view->priv->treeview = GTK_WIDGET (gtk_tree_view_new ());
	gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view->priv->treeview), TRUE);

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view->priv->treeview),
					     type_ahead_search_func,
					     NULL, NULL);

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

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), TRUE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view->priv->treeview), TRUE);
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

	gtk_container_add (GTK_CONTAINER (view), view->priv->treeview);

	{
		GtkTreeViewColumn *column;
		GtkTooltips *tooltip;
		GtkCellRenderer *renderer;
		GtkWidget *image_widget;
		gint width;

		tooltip = gtk_tooltips_new ();

		/* Playing icon column */
		column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
		renderer = rb_cell_renderer_pixbuf_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_entry_view_playing_cell_data_func,
							 view,
							 NULL);

		image_widget = gtk_image_new_from_icon_name ("stock_volume-max", GTK_ICON_SIZE_MENU);
		gtk_tree_view_column_set_widget (column, image_widget);
		gtk_widget_show (image_widget);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_clickable (column, FALSE);
		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
		gtk_tree_view_column_set_fixed_width (column, width + 5);
		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), column);
		g_signal_connect_swapped (renderer,
					  "pixbuf-clicked",
					  G_CALLBACK (rb_entry_view_pixbuf_clicked_cb),
					  view);

		gtk_tooltips_set_tip (GTK_TOOLTIPS (tooltip), GTK_WIDGET (column->button),
				       _("Now Playing"), NULL);
	}

	view->priv->gconf_notification_id =
		eel_gconf_notification_add (CONF_UI_COLUMNS_SETUP,
					    rb_entry_view_columns_config_changed_cb,
					    view);
	if (view->priv->sorting_key) {
		view->priv->sorting_gconf_notification_id =
			eel_gconf_notification_add (view->priv->sorting_key,
						    rb_entry_view_sort_key_changed_cb,
						    view);
	}

	if (view->priv->sorting_key) {
		char *s = eel_gconf_get_string (view->priv->sorting_key);
		rb_entry_view_set_sorting_type (view, s);
		g_free (s);
	}

	{
		RhythmDBQueryModel *query_model;
		query_model = rhythmdb_query_model_new_empty (view->priv->db);
		rb_entry_view_set_model (view, RHYTHMDB_QUERY_MODEL (query_model));
		g_object_unref (query_model);
	}

	return G_OBJECT (view);
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

gboolean
rb_entry_view_have_selection (RBEntryView *view)
{
	return view->priv->have_selection;
}

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

void
rb_entry_view_select_all (RBEntryView *view)
{
	gtk_tree_selection_select_all (view->priv->selection);
}

void
rb_entry_view_select_none (RBEntryView *view)
{
	gtk_tree_selection_unselect_all (view->priv->selection);
}

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
	if (!GTK_WIDGET_REALIZED (view))
		return;

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
	gboolean realized, visible;

	if (view->priv->playing_model != view->priv->model)
		return FALSE;

	rb_entry_view_entry_is_visible (view, entry, &realized, &visible,
					&unused);
	return realized && visible;
}

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

	if (!GTK_WIDGET_REALIZED (view))
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

	rb_entry_view_set_sorting_type (view, eel_gconf_get_string (view->priv->sorting_key));
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
	GEnumClass *prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP_TYPE);
	GEnumValue *ev;
	int ret;

	ev = g_enum_get_value_by_name (prop_class, name);
	if (ev)
		ret = ev->value;
	else
		ret = -1;
	return ret;
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
	char **items;
	GList *visible_properties = NULL;
	char *config = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);

	g_return_if_fail (view != NULL);
	g_return_if_fail (config != NULL);

	items = g_strsplit (config, ",", 0);
	if (items != NULL) {
		int i;
		for (i = 0; items[i] != NULL && *(items[i]); i++) {
			int value = propid_from_name (items[i]);

			if ((value >= 0) && (value < RHYTHMDB_NUM_PROPERTIES))
				visible_properties = g_list_prepend (visible_properties, GINT_TO_POINTER (value));
		}
		g_strfreev (items);
	}

	g_hash_table_foreach (view->priv->propid_column_map, (GHFunc) set_column_visibility, visible_properties);

	g_list_free (visible_properties);
	g_free (config);
}

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

void
rb_entry_view_set_fixed_column_width (RBEntryView *view,
				      GtkTreeViewColumn *column,
				      GtkCellRenderer *renderer,
				      const gchar **strings)
{
	gint max_width = 0;
	int i = 0;

	while (strings[i] != NULL) {
		gint width;
		g_object_set (renderer, "text", strings[i], NULL);
		gtk_cell_renderer_get_size (renderer,
					    view->priv->treeview,
					    NULL,
					    NULL, NULL,
					    &width, NULL);

		if (width > max_width)
			max_width = width;

		i++;
	}

	/* include some arbitrary amount of padding, just to be safeish */
	gtk_tree_view_column_set_fixed_width (column, max_width + 5);
}

const char *
rb_entry_view_get_time_date_column_sample ()
{
	static const char *sample = NULL;
	/*
	 * Currently, Japanese is the only translation that uses
	 * anything other than %Y, %m ,%d, %H, and %M to format dates.
	 * It uses %B (month name) and %e (days), and the values for
	 * the month name appear to all consist of the month number
	 * followed by a single character, so they're of consistent
	 * width.  So, this approach should work for every locale.
	 *
	 * Midnight on September 30th, 2000 is the widest date/time I
	 * can think of.  2000-09-30 00:00.
	 */

	if (sample == NULL) {
				/*    s  m  h   d  M    Y dw   dY  x */
		struct tm someday = { 0, 0, 0, 30, 9, 100, 6, 274, 0};

		/* Translators:  Please keep the translated date format
		 * compact, and avoid variable-width items such as month and
		 * day names wherever possible.  This allows us to disable
		 * column autosizing, which makes the Rhythmbox UI much faster.
		 */
		sample = eel_strdup_strftime (_("%Y-%m-%d %H:%M"), &someday);
	}
	return sample;
}

void
rb_entry_view_resort_model (RBEntryView *view)
{
	struct RBEntryViewColumnSortData *sort_data;

	g_assert (view->priv->sorting_column);
	sort_data = g_hash_table_lookup (view->priv->column_sort_data_map,
					 view->priv->sorting_column);
	g_assert (sort_data);

	rhythmdb_query_model_set_sort_order (view->priv->model,
					     sort_data->func,
					     sort_data->data,
					     NULL,
					     (view->priv->sorting_order == GTK_SORT_DESCENDING));
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_entry_view_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_TRACK_NUMBER, "Track Number"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_TITLE, "Title"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ARTIST, "Artist"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ALBUM, "Album"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_GENRE, "Genre"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_DURATION, "Duration"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_QUALITY, "Quality"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_RATING, "Rating"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_PLAY_COUNT, "Play Count"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_YEAR, "Year"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LAST_PLAYED, "Last Played"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_FIRST_SEEN, "First Seen"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LAST_SEEN, "Last Seen"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_LOCATION, "Location"),
			ENUM_ENTRY (RB_ENTRY_VIEW_COL_ERROR, "Error"),
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
			ENUM_ENTRY (RB_ENTRY_VIEW_NOT_PLAYING, "Not Playing"),
			ENUM_ENTRY (RB_ENTRY_VIEW_PLAYING, "Playing"),
			ENUM_ENTRY (RB_ENTRY_VIEW_PAUSED, "Paused"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBEntryViewState", values);
	}

	return etype;
}
