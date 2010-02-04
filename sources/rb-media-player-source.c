/*
 *  arch-tag: Implementation of the Media Player Source object
 *
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rb-media-player-sync-settings.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-playlist-manager.h"
#include "rb-podcast-manager.h"
#include "rb-util.h"

typedef struct {
	RBMediaPlayerSyncSettings *sync_settings;

	GtkActionGroup *action_group;
	GtkAction *sync_action;

	/* properties dialog bits */
	GtkDialog *properties_dialog;
	GtkTreeStore *sync_tree_store;
	GtkWidget *preview_bar;

	/* sync state */
	guint64 sync_space_needed;
	GList *sync_to_add;
	GList *sync_to_remove;

} RBMediaPlayerSourcePrivate;

G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))

static void rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass);
static void rb_media_player_source_init (RBMediaPlayerSource *source);
static void rb_media_player_source_dispose (GObject *object);

static void rb_media_player_source_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void rb_media_player_source_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);
static void rb_media_player_source_constructed (GObject *object);

static gboolean rb_media_player_source_track_added (RBRemovableMediaSource *source,
						    RhythmDBEntry *entry,
						    const char *uri,
						    guint64 dest_size,
						    const char *mimetype);
static gboolean rb_media_player_source_track_add_error (RBRemovableMediaSource *source,
							RhythmDBEntry *entry,
							const char *uri,
							GError *error);

static void track_add_done (RBMediaPlayerSource *source, RhythmDBEntry *entry);
static void update_sync (RBMediaPlayerSource *source);
static void sync_cmd (GtkAction *action, RBSource *source);
static char *make_track_uuid  (RhythmDBEntry *entry);

static GtkActionEntry rb_media_player_source_actions[] = {
	{ "MediaPlayerSourceSync", GTK_STOCK_REFRESH, N_("Sync"), NULL,
	  N_("Synchronize media player with the library"),
	  G_CALLBACK (sync_cmd) },
};

enum
{
	PROP_0,
	PROP_DEVICE_SERIAL
};

static GtkActionGroup *action_group = NULL;

void
rb_media_player_source_init_actions (RBShell *shell)
{
	GtkUIManager *uimanager;

	if (action_group != NULL) {
		return;
	}

	action_group = gtk_action_group_new ("MediaPlayerActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);

	g_object_get (shell, "ui-manager", &uimanager, NULL);
	gtk_ui_manager_insert_action_group (uimanager, action_group, 0);
	g_object_unref (uimanager);

	_rb_action_group_add_source_actions (action_group,
					     G_OBJECT (shell),
					     rb_media_player_source_actions,
					     G_N_ELEMENTS (rb_media_player_source_actions));
}

static void
rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);

	object_class->dispose = rb_media_player_source_dispose;

	object_class->set_property = rb_media_player_source_set_property;
	object_class->get_property = rb_media_player_source_get_property;
	object_class->constructed = rb_media_player_source_constructed;

	rms_class->impl_track_added = rb_media_player_source_track_added;
	rms_class->impl_track_add_error = rb_media_player_source_track_add_error;

	klass->impl_get_entries = NULL;
	klass->impl_get_capacity = NULL;
	klass->impl_get_free_space = NULL;
	klass->impl_add_playlist = NULL;
	klass->impl_remove_playlists = NULL;
	klass->impl_show_properties = NULL;

	g_object_class_install_property (object_class,
					 PROP_DEVICE_SERIAL,
					 g_param_spec_string ("serial",
							      "serial",
							      "device serial number",
							      NULL,
							      G_PARAM_READABLE));

	g_type_class_add_private (klass, sizeof (RBMediaPlayerSourcePrivate));
}

static void
rb_media_player_source_dispose (GObject *object)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);

	if (priv->sync_settings) {
		g_object_unref (priv->sync_settings);
		priv->sync_settings = NULL;
	}

	G_OBJECT_CLASS (rb_media_player_source_parent_class)->dispose (object);
}

static void
rb_media_player_source_init (RBMediaPlayerSource *source)
{
}

static void
rb_media_player_source_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	/*RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);*/
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_media_player_source_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	/*RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);*/
	switch (prop_id) {
	case PROP_DEVICE_SERIAL:
		/* not actually supported in the base class */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_media_player_source_constructed (GObject *object)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	RBShell *shell;

	RB_CHAIN_GOBJECT_METHOD (rb_media_player_source_parent_class, constructed, object);

	g_object_get (object, "shell", &shell, NULL);
	rb_media_player_source_init_actions (shell);
	g_object_unref (shell);

	priv->sync_action = gtk_action_group_get_action (action_group, "MediaPlayerSourceSync");
}

static gboolean
rb_media_player_source_track_added (RBRemovableMediaSource *source,
				    RhythmDBEntry *entry,
				    const char *uri,
				    guint64 dest_size,
				    const char *mimetype)
{
	track_add_done (RB_MEDIA_PLAYER_SOURCE (source), entry);
	return TRUE;
}

static gboolean
rb_media_player_source_track_add_error (RBRemovableMediaSource *source,
					RhythmDBEntry *entry,
					const char *uri,
					GError *error)
{
	track_add_done (RB_MEDIA_PLAYER_SOURCE (source), entry);
	return TRUE;
}

/* must be called once device information is available via source properties */
void
rb_media_player_source_load		(RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	char *device_id;
	char *sync_filename;
	char *sync_path;
	char *sync_dir;

	/* make sure the sync settings dir exists */
	sync_dir = g_build_filename (rb_user_data_dir (), "sync", NULL);
	g_mkdir (sync_dir, 0700);

	/* construct path to sync settings file */
	g_object_get (source, "serial", &device_id, NULL);
	if (device_id == NULL) {
		g_object_get (source, "name", &device_id, NULL);
	}
	sync_filename = g_strdup_printf ("device-%s.conf", device_id);
	sync_path = g_build_filename (sync_dir, sync_filename, NULL);
	g_free (sync_filename);
	g_free (device_id);
	g_free (sync_dir);

	priv->sync_settings = rb_media_player_sync_settings_new (sync_path);
	g_free (sync_path);
}

static guint64
get_capacity (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_capacity (source);
}

static guint64
get_free_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_free_space (source);
}

void
rb_media_player_source_delete_entries	(RBMediaPlayerSource *source,
					 GList *entries,
					 RBMediaPlayerSourceDeleteCallback callback,
					 gpointer callback_data,
					 GDestroyNotify destroy_data)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_delete_entries (source, entries, callback, callback_data, destroy_data);
}

static void
properties_dialog_response_cb (GtkDialog *dialog,
			       int response_id,
			       RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("media player properties dialog closed");
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (priv->properties_dialog);
	priv->properties_dialog = NULL;
}

static void
update_sync_preview_bar (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	char *text;
	char *used;
	char *capacity;
	double frac;

	/* TODO use segmented bar widget here */

	update_sync (source);

	frac = (priv->sync_space_needed/(double) get_capacity (source));
	frac = (frac > 1.0 ? 1.0 : frac);
	frac = (frac < 0.0 ? 0.0 : frac);
	used = g_format_size_for_display (priv->sync_space_needed);
	capacity = g_format_size_for_display (get_capacity (source));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->preview_bar), frac);

	/* Translators: this is used to display the amount of storage space which will be
	 * used and the total storage space on a device after it is synced.
	 */
	text = g_strdup_printf (_("%s of %s"), used, capacity);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->preview_bar), text);
	g_free (text);
	g_free (capacity);
	g_free (used);
}

static void
set_treeview_children (RBMediaPlayerSource *source,
		       GtkTreeIter *parent,
		       const char *category,
		       gboolean value,
		       gboolean apply_to_settings)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GtkTreeIter iter;
	char *group;
	gboolean valid;

	valid = gtk_tree_model_iter_children (GTK_TREE_MODEL (priv->sync_tree_store), &iter, parent);

	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->sync_tree_store), &iter,
				    2, &group,
				    -1);

		if (apply_to_settings) {
			rb_media_player_sync_settings_set_group (priv->sync_settings, category, group, value);
		}
		gtk_tree_store_set (priv->sync_tree_store, &iter,
		/* Active */	    0, value,
				    -1);

		g_free (group);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->sync_tree_store), &iter);
	}
}

static void
sync_entries_changed_cb (GtkCellRendererToggle *cell_renderer,
			 gchar *path,
			 RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GtkTreeIter iter;
	char *group;
	char *category_name;
	gboolean is_category;
	gboolean value;

	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (priv->sync_tree_store), &iter, path) == FALSE) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (priv->sync_tree_store),
			    &iter,
			    2, &group,
			    4, &is_category,
			    5, &category_name,
			    -1);

	value = !gtk_cell_renderer_toggle_get_active (cell_renderer);

	if (is_category) {
		rb_debug ("state for category %s changed to %d", category_name, value);
		rb_media_player_sync_settings_set_category (priv->sync_settings, category_name, value);
		rb_media_player_sync_settings_clear_groups (priv->sync_settings, category_name);

		gtk_tree_store_set (priv->sync_tree_store,
				    &iter,
				    0, value,
				    1, FALSE,		/* category is no longer inconsistent */
				    -1);
		set_treeview_children (source, &iter, category_name, value, FALSE);
	} else {
		gboolean parent_value;
		gboolean parent_inconsistent;
		GtkTreeIter parent;
		rb_debug ("state for group %s in category %s changed to %d", group, category_name, value);

		/* update parent state.  if the parent was previously enabled or disabled, then we
		 * set all the other groups to that state before setting the row that was just changed,
		 * and set the parent to inconsistent state.
		 */
		gtk_tree_model_iter_parent (GTK_TREE_MODEL (priv->sync_tree_store), &parent, &iter);
		gtk_tree_model_get (GTK_TREE_MODEL (priv->sync_tree_store), &parent,
				    0, &parent_value,
				    1, &parent_inconsistent,
				    -1);
		if (parent_inconsistent == FALSE) {
			/* category is now inconsistent */
			rb_debug ("setting category %s to disabled, inconsistent", category_name);
			rb_media_player_sync_settings_set_category (priv->sync_settings, category_name, FALSE);
			gtk_tree_store_set (priv->sync_tree_store,
					    &parent,
					    0, FALSE,
					    1, TRUE,
					    -1);

			/* set all groups in the category to the parent's state */
			set_treeview_children (source, &parent, category_name, parent_value, TRUE);
		}

		rb_media_player_sync_settings_set_group (priv->sync_settings, category_name, group, value);
		gtk_tree_store_set (priv->sync_tree_store, &iter,
				    0, value,
				    -1);

		/* if no groups are enabled, the category is no longer inconsistent */
		if (value == FALSE && rb_media_player_sync_settings_has_enabled_groups (priv->sync_settings, category_name) == FALSE) {
			rb_debug ("no enabled groups left in category %s", category_name);
			gtk_tree_store_set (priv->sync_tree_store, &parent,
					    1, FALSE,
					    -1);
		} else if (value == FALSE) {
			rb_debug ("category %s still has some groups", category_name);
		}
	}

	g_free (category_name);
	g_free (group);
	update_sync_preview_bar (source);
}


void
rb_media_player_source_show_properties (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	GtkBuilder *builder;
	GtkTreeIter tree_iter;
	GtkTreeIter parent_iter;
	GtkTreeModel *query_model;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *col;
	GtkWidget *tree_view;
	GtkWidget *widget;
	const char *ui_file;
	char *used_str;
	char *capacity_str;
	char *name;
	char *text;
	GList *l;
	GList *playlists;
	guint64 capacity;
	guint64 free_space;
	gboolean valid;
	RBShell *shell;
	RhythmDB *db;
	RBPlaylistManager *playlist_manager;

	if (priv->properties_dialog != NULL) {
		gtk_window_present (GTK_WINDOW (priv->properties_dialog));
		return;
	}

	/* load dialog UI */
	ui_file = rb_file ("media-player-properties.ui");
	if (ui_file == NULL) {
		g_warning ("Couldn't find media-player-properties.ui");
		return;
	}

	builder = rb_builder_load (ui_file, NULL);
	if (builder == NULL) {
		g_warning ("Couldn't load media-player-properties.ui");
		return;
	}

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, "playlist-manager", &playlist_manager, NULL);

	priv->properties_dialog = GTK_DIALOG (gtk_builder_get_object (builder, "media-player-properties"));
	g_signal_connect_object (priv->properties_dialog,
				 "response",
				 G_CALLBACK (properties_dialog_response_cb),
				 source, 0);

	g_object_get (source, "name", &name, NULL);
	text = g_strdup_printf (_("%s Properties"), name);
	gtk_window_set_title (GTK_WINDOW (priv->properties_dialog), text);
	g_free (text);
	g_free (name);

	/*
	 * fill in some common details:
	 * - volume usage (need to hook up signals etc. to update this live)
	 */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar-device-usage"));
	capacity = get_capacity (source);
	free_space = get_free_space (source);
	used_str = g_format_size_for_display (capacity - free_space);
	capacity_str = g_format_size_for_display (capacity);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       (double)(capacity - free_space)/(double)capacity);
	/* Translators: this is used to display the amount of storage space
	 * used and the total storage space on an device.
	 */
	text = g_strdup_printf (_("%s of %s"), used_str, capacity_str);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), text);
	g_free (text);
	g_free (capacity_str);
	g_free (used_str);

	/* let the subclass fill in device type specific details (model names, device names,
	 * .. battery levels?) and add more tabs to the notebook to display 'advanced' stuff.
	 */

	if (klass->impl_show_properties) {
		klass->impl_show_properties (source,
					     GTK_WIDGET (gtk_builder_get_object (builder, "device-info-box")),
					     GTK_WIDGET (gtk_builder_get_object (builder, "media-player-notebook")));
	}

	/* set up sync widgetry */
	priv->preview_bar = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar-sync-preview"));

	/* tree_store columns are: active, inconsistent, name, display-name, is-category, category name */
	priv->sync_tree_store = gtk_tree_store_new (6, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);

	/* music library parent */
	gtk_tree_store_append (priv->sync_tree_store, &parent_iter, NULL);
	gtk_tree_store_set (priv->sync_tree_store, &parent_iter,
			    0, rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_MUSIC),
			    1, rb_media_player_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_MUSIC),
			    2, _("Music"),
			    3, _("Music"),
			    4, TRUE,
			    5, SYNC_CATEGORY_MUSIC,
			    -1);

	/* 'all music' entry */
	gtk_tree_store_append (priv->sync_tree_store, &tree_iter, &parent_iter);
	gtk_tree_store_set (priv->sync_tree_store, &tree_iter,
			    0, rb_media_player_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_MUSIC, SYNC_GROUP_ALL_MUSIC),
			    1, FALSE,
			    2, SYNC_GROUP_ALL_MUSIC,
			    3, _("All Music"),
			    4, FALSE,
			    5, SYNC_CATEGORY_MUSIC,
			    -1);

	/* playlist entries */
	playlists = rb_playlist_manager_get_playlists (playlist_manager);
	for (l = playlists; l != NULL; l = l->next) {
		char *name;

		gtk_tree_store_append (priv->sync_tree_store, &tree_iter, &parent_iter);
		/* set playlists data here */
		g_object_get (l->data, "name", &name, NULL);

		/* set this row's data */
		gtk_tree_store_set (priv->sync_tree_store, &tree_iter,
				    0, rb_media_player_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_MUSIC, name),
				    1, FALSE,
				    2, name,
				    3, name,
				    4, FALSE,
				    5, SYNC_CATEGORY_MUSIC,
				    -1);

		g_free (name);
	}

	/* Append the Podcasts parent */
	gtk_tree_store_append (priv->sync_tree_store,
			       &parent_iter,
			       NULL);
	gtk_tree_store_set (priv->sync_tree_store, &parent_iter,
			    0, rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_PODCAST),
			    1, rb_media_player_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_PODCAST),
			    2, _("Podcasts"),
			    3, _("Podcasts"),
			    4, TRUE,
			    5, SYNC_CATEGORY_PODCAST,
			    -1);

	/* this really needs to use a live query model */
	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_query_model_set_sort_order (RHYTHMDB_QUERY_MODEL (query_model),
					     (GCompareDataFunc) rhythmdb_query_model_title_sort_func,
					     NULL, NULL, FALSE);
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				RHYTHMDB_QUERY_END);
	valid = gtk_tree_model_get_iter_first (query_model, &tree_iter);
	while (valid) {
		RhythmDBEntry *entry;
		GtkTreeIter tree_iter2;
		const char *name;
		const char *feed;

		entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), &tree_iter);
		gtk_tree_store_append (priv->sync_tree_store, &tree_iter2, &parent_iter);

		/* set up this row */
		name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
		feed = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		rb_debug ("adding feed %s (name %s)", feed, name);
		gtk_tree_store_set (priv->sync_tree_store, &tree_iter2,
				    0, rb_media_player_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_PODCAST, name),
				    1, FALSE,
				    2, feed,
				    3, name,
				    4, FALSE,
				    5, SYNC_CATEGORY_PODCAST,
				    -1);

		valid = gtk_tree_model_iter_next (query_model, &tree_iter);
	}

	/* Set up the treeview */
	tree_view = GTK_WIDGET (gtk_builder_get_object (builder, "treeview-sync"));

	/* First column */
	renderer = gtk_cell_renderer_toggle_new ();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"active", 0,
							"inconsistent", 1,
							NULL);
	g_signal_connect (G_OBJECT (renderer),
			  "toggled", G_CALLBACK (sync_entries_changed_cb),
			  source);
	gtk_tree_view_append_column(GTK_TREE_VIEW (tree_view), col);

	/* Second column */
	renderer = gtk_cell_renderer_text_new ();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"text", 3,
							NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), col);
	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view),
				 GTK_TREE_MODEL (priv->sync_tree_store));
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view)),
				    GTK_SELECTION_NONE);

	update_sync_preview_bar (source);

	gtk_widget_show (GTK_WIDGET (priv->properties_dialog));

	g_object_unref (builder);
	g_object_unref (playlist_manager);
	g_object_unref (shell);
	g_object_unref (db);
}

typedef struct {
	GHashTable *target;
	GList *result;
} BuildSyncListData;

static void
build_sync_list_cb (char *uuid, RhythmDBEntry *entry, BuildSyncListData *data)
{
	if (!g_hash_table_lookup (data->target, uuid)) {
		rb_debug ("adding %s (%" G_GINT64_FORMAT " bytes); id %s to sync list",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			  rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE),
			  uuid);
		data->result = g_list_prepend (data->result, rhythmdb_entry_ref (entry));
	}
}


static gboolean
entry_is_undownloaded_podcast (RhythmDBEntry *entry)
{
	if (rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		return (!rb_podcast_manager_entry_downloaded (entry));
	}

	return FALSE;
}


static void
update_sync_space_needed (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *list_iter;
	gint64 add_size = 0;
	gint64 remove_size = 0;

	for (list_iter = priv->sync_to_add; list_iter; list_iter = list_iter->next) {
		add_size += rhythmdb_entry_get_uint64 (list_iter->data, RHYTHMDB_PROP_FILE_SIZE);
	}

	for (list_iter = priv->sync_to_remove; list_iter; list_iter = list_iter->next) {
		remove_size += rhythmdb_entry_get_uint64 (list_iter->data, RHYTHMDB_PROP_FILE_SIZE);
	}

	priv->sync_space_needed = get_capacity (source) - get_free_space (source);
	rb_debug ("current space used: %" G_GINT64_FORMAT " bytes; adding %" G_GINT64_FORMAT ", removing %" G_GINT64_FORMAT,
		  priv->sync_space_needed,
		  add_size,
		  remove_size);
	priv->sync_space_needed = priv->sync_space_needed + add_size - remove_size;
	rb_debug ("space used after sync: %" G_GINT64_FORMAT " bytes", priv->sync_space_needed);
}

static gboolean
hash_table_insert_from_tree_model_cb (GtkTreeModel *query_model,
				      GtkTreePath  *path,
				      GtkTreeIter  *iter,
				      GHashTable   *target)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), iter);
	if (!entry_is_undownloaded_podcast (entry)) {
		g_hash_table_insert (target,
				     make_track_uuid (entry),
				     rhythmdb_entry_ref (entry));
	}

	return FALSE;
}
static void
itinerary_insert_all_of_type (RhythmDB *db,
			      RhythmDBEntryType entry_type,
			      GHashTable *target)
{
	GtkTreeModel *query_model;

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_END);

	gtk_tree_model_foreach (query_model,
				(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
				target);
}

static void
itinerary_insert_some_playlists (RBMediaPlayerSource *source,
				 GHashTable *target)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *list_iter;
	GList *playlists;
	RBShell *shell;

	g_object_get (source, "shell", &shell, NULL);
	playlists = rb_playlist_manager_get_playlists ((RBPlaylistManager *) rb_shell_get_playlist_manager (shell));
	g_object_unref (shell);

	for (list_iter = playlists; list_iter; list_iter = list_iter->next) {
		gchar *name;

		g_object_get (G_OBJECT (list_iter->data), "name", &name, NULL);

		/* See if we should sync it */
		if (rb_media_player_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_MUSIC, name)) {
			GtkTreeModel *query_model;

			rb_debug ("adding entries from playlist %s to itinerary", name);
			g_object_get (RB_SOURCE (list_iter->data), "base-query-model", &query_model, NULL);
			gtk_tree_model_foreach (query_model,
						(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
						target);
			g_object_unref (query_model);
		} else {
			rb_debug ("not adding playlist %s to itinerary", name);
		}

		g_free (name);
	}

	g_list_free (playlists);
}

static void
itinerary_insert_some_podcasts (RBMediaPlayerSource *source,
				RhythmDB *db,
				GHashTable *target)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *podcasts;
	GList *i;

	podcasts = rb_media_player_sync_settings_get_enabled_groups (priv->sync_settings, SYNC_CATEGORY_PODCAST);
	for (i = podcasts; i != NULL; i = i->next) {
		GtkTreeModel *query_model;
		rb_debug ("adding entries from podcast %s to itinerary", (char *)i->data);
		query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
		rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_SUBTITLE, i->data,
					RHYTHMDB_QUERY_END);

		/* TODO: exclude undownloaded episodes, sort by post date, set limit, optionally exclude things with play count > 0
		 * RHYTHMDB_QUERY_PROP_NOT_EQUAL, RHYTHMDB_PROP_MOUNTPOINT, NULL,	(will this work?)
		 * RHYTHMDB_QUERY_PROP_NOT_EQUAL, RHYTHMDB_PROP_STATUS, RHYTHMDB_PODCAST_STATUS_ERROR,
		 *
		 * RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_PLAYCOUNT, 0
		 */

		gtk_tree_model_foreach (query_model,
					(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
					target);
		g_object_unref (query_model);
	}
}

static GHashTable *
build_sync_itinerary (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBShell *shell;
	RhythmDB *db;
	GHashTable *itinerary;

	rb_debug ("building itinerary hash");

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	itinerary = g_hash_table_new_full (g_str_hash,
					   g_str_equal,
					   g_free,
					   (GDestroyNotify)rhythmdb_entry_unref);

	if (rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_MUSIC) ||
	    rb_media_player_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_MUSIC, SYNC_GROUP_ALL_MUSIC)) {
		rb_debug ("adding all music to the itinerary");
		itinerary_insert_all_of_type (db, RHYTHMDB_ENTRY_TYPE_SONG, itinerary);
	} else if (rb_media_player_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_MUSIC)) {
		rb_debug ("adding selected playlists to the itinerary");
		itinerary_insert_some_playlists (source, itinerary);
	}

	if (rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		rb_debug ("adding all podcasts to the itinerary");
		/* TODO: when we get #episodes/not-if-played settings, use
		 * equivalent of insert_some_podcasts, iterating through all feeds
		 * (use a query for all entries of type PODCAST_FEED to find them)
		 */
		itinerary_insert_all_of_type (db, RHYTHMDB_ENTRY_TYPE_PODCAST_POST, itinerary);
	} else if (rb_media_player_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		rb_debug ("adding selected podcasts to the itinerary");
		itinerary_insert_some_podcasts (source, db, itinerary);
	}

	g_object_unref (shell);
	g_object_unref (db);

	rb_debug ("finished building itinerary hash; has %d entries", g_hash_table_size (itinerary));
	return itinerary;
}

static void
_g_hash_table_transfer_all (GHashTable *target, GHashTable *source)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, source);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_hash_table_insert (target, key, value);
		g_hash_table_iter_steal (&iter);
	}
}

static GHashTable *
build_device_state (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	GHashTable *device;

	rb_debug ("building device contents hash");

	device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)rhythmdb_entry_unref);

	if (rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_MUSIC)) {
		GHashTable *entries;
		rb_debug ("getting music entries from device");
		entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) rhythmdb_entry_unref);
		klass->impl_get_entries (source, SYNC_CATEGORY_MUSIC, entries);
		_g_hash_table_transfer_all (device, entries);
		g_hash_table_destroy (entries);
		rb_debug ("done getting music entries from device");
	}

	if (rb_media_player_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		GHashTable *podcasts;
		rb_debug ("getting podcast entries from device");
		podcasts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) rhythmdb_entry_unref);
		klass->impl_get_entries (source, SYNC_CATEGORY_PODCAST, podcasts);
		_g_hash_table_transfer_all (device, podcasts);
		g_hash_table_destroy (podcasts);
		rb_debug ("done getting podcast entries from device");
	}

	rb_debug ("done building device contents hash; has %d entries", g_hash_table_size (device));
	return device;
}

static void
update_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GHashTable *device;
	GHashTable *itinerary;
	BuildSyncListData data;

	/* destroy existing sync lists */
	rb_list_destroy_free (priv->sync_to_add, (GDestroyNotify) rhythmdb_entry_unref);
	rb_list_destroy_free (priv->sync_to_remove, (GDestroyNotify) rhythmdb_entry_unref);
	priv->sync_to_add = NULL;
	priv->sync_to_remove = NULL;

	/* figure out what we want on the device and what's already there */
	itinerary = build_sync_itinerary (source);
	device = build_device_state (source);

	/* figure out what to add to the device */
	rb_debug ("building list of files to transfer to device");
	data.target = device;
	data.result = NULL;
	g_hash_table_foreach (itinerary, (GHFunc)build_sync_list_cb, &data);
	priv->sync_to_add = data.result;
	rb_debug ("decided to transfer %d files to the device", g_list_length (priv->sync_to_add));

	/* and what to remove */
	rb_debug ("building list of files to remove from device");
	data.target = itinerary;
	data.result = NULL;
	g_hash_table_foreach (device, (GHFunc)build_sync_list_cb, &data);
	priv->sync_to_remove = data.result;
	rb_debug ("decided to remove %d files from the device", g_list_length (priv->sync_to_remove));

	g_hash_table_destroy (device);
	g_hash_table_destroy (itinerary);

	update_sync_space_needed (source);
}

static void
sync_playlists (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	RBPlaylistManager *playlist_manager;
	RBShell *shell;
	GHashTable *device;
	GList *all_playlists;
	GList *l;

	if (klass->impl_add_playlist == NULL || klass->impl_remove_playlists == NULL) {
		rb_debug ("source class doesn't support playlists");
		return;
	}

	/* build an updated device contents map, so we can find the device entries
	 * corresponding to the entries in the local playlists.
	 */
	device = build_device_state (source);

	/* remove all playlists from the device, then add the synced playlists. */
	klass->impl_remove_playlists (source);

	/* get all local playlists */
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "playlist-manager", &playlist_manager, NULL);
	all_playlists = rb_playlist_manager_get_playlists (playlist_manager);
	g_object_unref (playlist_manager);
	g_object_unref (shell);

	for (l = all_playlists; l != NULL; l = l->next) {
		char *name;
		RBSource *playlist_source = RB_SOURCE (l->data);
		RhythmDBQueryModel *model;
		GList *tracks = NULL;
		GtkTreeIter iter;

		/* is this playlist selected for syncing? */
		g_object_get (playlist_source, "name", &name, NULL);
		if (rb_media_player_sync_settings_group_enabled (priv->sync_settings, SYNC_CATEGORY_MUSIC, name) == FALSE) {
			rb_debug ("not syncing playlist %s", name);
			g_free (name);
			continue;
		}

		/* match playlist entries to entries on the device */
		g_object_get (playlist_source, "base-query-model", &model, NULL);
		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter) == FALSE) {
			rb_debug ("not syncing empty playlist %s", name);
			g_free (name);
			g_object_unref (model);
			continue;
		}

		do {
			char *trackid;
			RhythmDBEntry *entry;
			RhythmDBEntry *device_entry;

			entry = rhythmdb_query_model_iter_to_entry (model, &iter);
			trackid = make_track_uuid (entry);

			device_entry = g_hash_table_lookup (device, trackid);
			if (device_entry != NULL) {
				tracks = g_list_prepend (tracks, device_entry);
			} else {
				rb_debug ("unable to find entry on device for track %s (id %s)",
					  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
					  trackid);
			}
			g_free (trackid);

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));

		tracks = g_list_reverse (tracks);

		/* transfer the playlist to the device */
		rb_debug ("syncing playlist %s", name);
		klass->impl_add_playlist (source, name, tracks);

		g_free (name);
		g_list_free (tracks);
		g_object_unref (model);
	}
}

static gboolean
sync_idle_cb_cleanup (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	rb_debug ("cleaning up after sync process");

	gtk_action_set_sensitive (priv->sync_action, TRUE);

	return FALSE;
}

static gboolean
sync_idle_cb_playlists (RBMediaPlayerSource *source)
{
	/* Transfer the playlists */
	rb_debug ("transferring playlists to the device");
	sync_playlists (source);
	g_idle_add ((GSourceFunc)sync_idle_cb_cleanup, source);
	return FALSE;
}

static void
track_add_done (RBMediaPlayerSource *source, RhythmDBEntry *entry)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *l;
	/* remove the entry from the set of transfers we're waiting for;
	 * if the set is now empty, trigger the next sync stage.
	 */

	l = g_list_find (priv->sync_to_add, entry);
	if (l != NULL) {
		priv->sync_to_add = g_list_remove_link (priv->sync_to_add, l);
		if (priv->sync_to_add == NULL) {
			rb_debug ("finished transferring files to the device");
			g_idle_add ((GSourceFunc) sync_idle_cb_playlists, source);
		}
		rhythmdb_entry_unref (entry);
	}
}

static void
sync_delete_done_cb (RBMediaPlayerSource *source, gpointer dontcare)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("finished deleting %d files from media player", g_list_length (priv->sync_to_remove));
	rb_list_destroy_free (priv->sync_to_remove, (GDestroyNotify) rhythmdb_entry_unref);
	priv->sync_to_remove = NULL;

	/* Transfer needed tracks and podcasts from itinerary to device */
	if (priv->sync_to_add != NULL) {
		rb_debug ("transferring %d files from media player", g_list_length (priv->sync_to_add));
		rb_source_paste (RB_SOURCE (source), priv->sync_to_add);
	} else {
		rb_debug ("no files to transfer to the device");
		g_idle_add ((GSourceFunc) sync_idle_cb_playlists, source);
	}
}

static gboolean
sync_idle_cb_delete_entries (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("deleting %d files from media player", g_list_length (priv->sync_to_remove));
	rb_media_player_source_delete_entries (source,
					       priv->sync_to_remove,
					       (RBMediaPlayerSourceDeleteCallback) sync_delete_done_cb,
					       NULL,
					       NULL);
	return FALSE;
}

static gboolean
sync_idle_cb_update_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	update_sync (source);

	/* Check we have enough space on the device. */
	if (priv->sync_space_needed > get_capacity (source)) {
		rb_debug ("not enough free space on device; need %" G_GINT64_FORMAT ", capacity is %" G_GINT64_FORMAT,
			  priv->sync_space_needed,
			  get_capacity (source));
		rb_error_dialog (NULL,
				 _("Not enough free space to sync"),
				 _("There is not enough free space on this device to transfer the selected music, playlists, and podcasts."));
		g_idle_add ((GSourceFunc)sync_idle_cb_cleanup, source);
		return FALSE;
	}

	g_idle_add ((GSourceFunc)sync_idle_cb_delete_entries, source);
	return FALSE;
}

void
rb_media_player_source_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	gtk_action_set_sensitive (priv->sync_action, FALSE);

	g_idle_add ((GSourceFunc)sync_idle_cb_update_sync, source);
}

static char *
make_track_uuid  (RhythmDBEntry *entry)
{
	/* This function is for hashing the two databases for syncing. */
	GString *str = g_string_new ("");
	char *result;

	/*
	 * possible improvements here:
	 * - use musicbrainz track ID if known (maybe not a great idea?)
	 * - fuzz the duration a bit (round to nearest 5 seconds?) to catch slightly
	 *   different encodings of the same track
	 * - maybe don't include genre, since there's no canonical genre for anything
	 */

	g_string_printf (str, "%s%s%s%s%lu%lu%lu",
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_DURATION),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_TRACK_NUMBER),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_DISC_NUMBER));

	/* not sure why we md5 this.  how does it help? */
	result = g_compute_checksum_for_string (G_CHECKSUM_MD5, str->str, str->len);

	g_string_free (str, TRUE);

	return result;
}

void
_rb_media_player_source_add_to_map (GHashTable *map, RhythmDBEntry *entry)
{
	g_hash_table_insert (map,
			     make_track_uuid (entry),
			     rhythmdb_entry_ref (entry));
}

static void
sync_cmd (GtkAction *action, RBSource *source)
{
	rb_media_player_source_sync (RB_MEDIA_PLAYER_SOURCE (source));
}
