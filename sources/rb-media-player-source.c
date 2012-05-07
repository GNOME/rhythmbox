/*
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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
#include "rb-transfer-target.h"
#include "rb-sync-settings.h"
#include "rb-sync-settings-ui.h"
#include "rb-sync-state.h"
#include "rb-sync-state-ui.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-playlist-manager.h"
#include "rb-util.h"

typedef struct {
	RBSyncSettings *sync_settings;

	GtkActionGroup *action_group;
	GtkAction *sync_action;

	/* properties dialog bits */
	GtkDialog *properties_dialog;
	RBSyncBarData volume_usage;

	/* sync settings dialog bits */
	GtkWidget *sync_dialog;
	GtkWidget *sync_dialog_label;
	GtkWidget *sync_dialog_error_box;
	guint sync_dialog_update_id;

	/* sync state */
	RBSyncState *sync_state;

	GstEncodingTarget *encoding_target;
} RBMediaPlayerSourcePrivate;

G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_BROWSER_SOURCE);

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

static void sync_cmd (GtkAction *action, RBSource *source);
static gboolean sync_idle_delete_entries (RBMediaPlayerSource *source);

static gboolean impl_receive_drag (RBDisplayPage *page, GtkSelectionData *data);
static void impl_delete_thyself (RBDisplayPage *page);

static char *impl_get_delete_action (RBSource *source);

static GtkActionEntry rb_media_player_source_actions[] = {
	{ "MediaPlayerSourceSync", GTK_STOCK_REFRESH, N_("Sync with Library"), NULL,
	  N_("Synchronize media player with the library"),
	  G_CALLBACK (sync_cmd) },
};

enum
{
	PROP_0,
	PROP_DEVICE_SERIAL,
	PROP_ENCODING_TARGET
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

	_rb_action_group_add_display_page_actions (action_group,
						   G_OBJECT (shell),
						   rb_media_player_source_actions,
						   G_N_ELEMENTS (rb_media_player_source_actions));
}

static void
rb_media_player_source_class_init (RBMediaPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose = rb_media_player_source_dispose;
	object_class->set_property = rb_media_player_source_set_property;
	object_class->get_property = rb_media_player_source_get_property;
	object_class->constructed = rb_media_player_source_constructed;

	page_class->receive_drag = impl_receive_drag;
	page_class->delete_thyself = impl_delete_thyself;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_get_delete_action = impl_get_delete_action;
	source_class->impl_delete = NULL;

	browser_source_class->has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

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
	/**
	 * RBMediaPlayerSource:encoding-target
	 *
	 * The #GstEncodingTarget for this device
	 */
	g_object_class_install_property (object_class,
					 PROP_ENCODING_TARGET,
					 gst_param_spec_mini_object ("encoding-target",
								     "encoding target",
								     "GstEncodingTarget",
								     GST_TYPE_ENCODING_TARGET,
								     G_PARAM_READWRITE));

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

	if (priv->sync_state) {
		g_object_unref (priv->sync_state);
		priv->sync_state = NULL;
	}

	if (priv->encoding_target) {
		gst_encoding_target_unref (priv->encoding_target);
		priv->encoding_target = NULL;
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
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_ENCODING_TARGET:
		if (priv->encoding_target) {
			gst_encoding_target_unref (priv->encoding_target);
		}
		priv->encoding_target = GST_ENCODING_TARGET (gst_value_dup_mini_object (value));
		break;
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
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_DEVICE_SERIAL:
		/* not actually supported in the base class */
		break;
	case PROP_ENCODING_TARGET:
		gst_value_set_mini_object (value, GST_MINI_OBJECT (priv->encoding_target));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
load_status_changed_cb (GObject *object, GParamSpec *pspec, gpointer whatever)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	RBSourceLoadStatus status;

	g_object_get (object, "load-status", &status, NULL);

	gtk_action_set_sensitive (priv->sync_action, (status == RB_SOURCE_LOAD_STATUS_LOADED));
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
	g_signal_connect (object, "notify::load-status", G_CALLBACK (load_status_changed_cb), NULL);
	load_status_changed_cb (object, NULL, NULL);
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

	priv->sync_settings = rb_sync_settings_new (sync_path);
	g_free (sync_path);
}

guint64
rb_media_player_source_get_capacity (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_capacity (source);
}

guint64
rb_media_player_source_get_free_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_free_space (source);
}

/**
 * rb_media_player_source_get_entries:
 * @source: the #RBMediaPlayerSource
 * @category: the sync category name
 * @entries: (element-type utf8 RB.RhythmDBEntry): map to hold the entries
 */
void
rb_media_player_source_get_entries (RBMediaPlayerSource *source,
				    const char *category,
				    GHashTable *entries)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	klass->impl_get_entries (source, category, entries);
}

/**
 * rb_media_player_source_delete_entries:
 * @source: the #RBMediaPlayerSource
 * @entries: (element-type RB.RhythmDBEntry) (transfer full): list of entries to delete
 * @callback: callback to call on completion
 * @user_data: (closure) (scope notified): data for callback
 * @destroy_data: callback to free the callback data
 */
void
rb_media_player_source_delete_entries	(RBMediaPlayerSource *source,
					 GList *entries,
					 RBMediaPlayerSourceDeleteCallback callback,
					 gpointer user_data,
					 GDestroyNotify destroy_data)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	klass->impl_delete_entries (source, entries, callback, user_data, destroy_data);
}

static void
update_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	if (priv->sync_state == NULL) {
		priv->sync_state = rb_sync_state_new (source, priv->sync_settings);
	} else {
		rb_sync_state_update (priv->sync_state);
	}
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

void
rb_media_player_source_show_properties (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	GtkBuilder *builder;
	GtkContainer *container;
	const char *ui_file;
	char *name;
	char *text;

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

	priv->properties_dialog = GTK_DIALOG (gtk_builder_get_object (builder, "media-player-properties"));
	g_object_ref (priv->properties_dialog);
	g_signal_connect_object (priv->properties_dialog,
				 "response",
				 G_CALLBACK (properties_dialog_response_cb),
				 source, 0);

	g_object_get (source, "name", &name, NULL);
	text = g_strdup_printf (_("%s Properties"), name);
	gtk_window_set_title (GTK_WINDOW (priv->properties_dialog), text);
	g_free (text);
	g_free (name);

	/* ensure device usage information is available and up to date */
	update_sync (source);

	/*
	 * fill in some common details:
	 * - volume usage (need to hook up signals etc. to update this live)
	 */
	rb_sync_state_ui_create_bar (&priv->volume_usage, rb_media_player_source_get_capacity (source), NULL);
	rb_sync_state_ui_update_volume_usage (&priv->volume_usage, priv->sync_state);

	gtk_widget_show_all (priv->volume_usage.widget);
	container = GTK_CONTAINER (gtk_builder_get_object (builder, "device-usage-container"));
	gtk_container_add (container, priv->volume_usage.widget);


	/* let the subclass fill in device type specific details (model names, device names,
	 * .. battery levels?) and add more tabs to the notebook to display 'advanced' stuff.
	 */

	if (klass->impl_show_properties) {
		klass->impl_show_properties (source,
					     GTK_WIDGET (gtk_builder_get_object (builder, "device-info-box")),
					     GTK_WIDGET (gtk_builder_get_object (builder, "media-player-notebook")));
	}

	/* create sync UI */
	container = GTK_CONTAINER (gtk_builder_get_object (builder, "sync-settings-ui-container"));
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (container), rb_sync_settings_ui_new (source, priv->sync_settings));

	container = GTK_CONTAINER (gtk_builder_get_object (builder, "sync-state-ui-container"));
	gtk_box_pack_start (GTK_BOX (container), rb_sync_state_ui_new (priv->sync_state), TRUE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (container));

	gtk_widget_show (GTK_WIDGET (priv->properties_dialog));

	g_object_unref (builder);
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
	device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)rhythmdb_entry_unref);
	rb_media_player_source_get_entries (source, SYNC_CATEGORY_MUSIC, device);

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
		if (rb_sync_settings_group_enabled (priv->sync_settings, SYNC_CATEGORY_MUSIC, name) == FALSE) {
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
			trackid = rb_sync_state_make_track_uuid (entry);

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

	g_hash_table_destroy (device);
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
transfer_batch_complete_cb (RBTrackTransferBatch *batch, RBMediaPlayerSource *source)
{
	rb_debug ("finished transferring files to the device");
	g_idle_add ((GSourceFunc) sync_idle_cb_playlists, source);
}

static void
transfer_batch_cancelled_cb (RBTrackTransferBatch *batch, RBMediaPlayerSource *source)
{
	/* don't try to update playlists, just clean up */
	rb_debug ("sync file transfer to the device was cancelled");
	g_idle_add ((GSourceFunc) sync_idle_cb_cleanup, source);
}


static void
sync_delete_done_cb (RBMediaPlayerSource *source, gpointer dontcare)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("finished deleting %d files from media player", priv->sync_state->sync_remove_count);

	/* Transfer needed tracks and podcasts from itinerary to device */
	if (priv->sync_state->sync_add_count != 0) {
		RBTrackTransferBatch *batch;

		rb_debug ("transferring %d files to media player", priv->sync_state->sync_add_count);
		batch = rb_source_paste (RB_SOURCE (source), priv->sync_state->sync_to_add);
		if (batch != NULL) {
			g_signal_connect_object (batch, "complete", G_CALLBACK (transfer_batch_complete_cb), source, 0);
			g_signal_connect_object (batch, "cancelled", G_CALLBACK (transfer_batch_cancelled_cb), source, 0);
		} else {
			rb_debug ("weird, transfer was apparently synchronous");
			g_idle_add ((GSourceFunc) sync_idle_cb_playlists, source);
		}
	} else {
		rb_debug ("no files to transfer to the device");
		g_idle_add ((GSourceFunc) sync_idle_cb_playlists, source);
	}
}

static gboolean
sync_has_items_enabled (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	if (rb_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_MUSIC) == FALSE &&
	    rb_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_MUSIC) == FALSE &&
	    rb_sync_settings_sync_category (priv->sync_settings, SYNC_CATEGORY_PODCAST) == FALSE &&
	    rb_sync_settings_has_enabled_groups (priv->sync_settings, SYNC_CATEGORY_PODCAST) == FALSE) {
		rb_debug ("no sync items are enabled");
		return FALSE;
	}

	return TRUE;
}

static gboolean
sync_has_enough_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	if (priv->sync_state->sync_space_needed > rb_media_player_source_get_capacity (source)) {
		rb_debug ("not enough space for selected sync items");
		return FALSE;
	}

	return TRUE;
}

static void
update_sync_settings_dialog (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	gboolean can_continue;

	if (sync_has_items_enabled (source) == FALSE) {
		can_continue = FALSE;
		gtk_label_set_text (GTK_LABEL (priv->sync_dialog_label),
				    _("You have not selected any music, playlists, or podcasts to transfer to this device."));
	} else if (sync_has_enough_space (source) == FALSE) {
		can_continue = FALSE;
		gtk_label_set_text (GTK_LABEL (priv->sync_dialog_label),
				    _("There is not enough space on the device to transfer the selected music, playlists and podcasts."));
	} else {
		can_continue = TRUE;
	}

	gtk_widget_set_visible (priv->sync_dialog_error_box, !can_continue);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (priv->sync_dialog), GTK_RESPONSE_YES, can_continue);
}

static void
sync_dialog_state_update (RBSyncState *state, RBMediaPlayerSource *source)
{
	update_sync_settings_dialog (source);
}

static void
sync_confirm_dialog_cb (GtkDialog *dialog,
			gint response_id,
			RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	g_signal_handler_disconnect (priv->sync_state, priv->sync_dialog_update_id);
	priv->sync_dialog_update_id = 0;

	gtk_widget_destroy (GTK_WIDGET (dialog));
	priv->sync_dialog = NULL;
	priv->sync_dialog_label = NULL;

	if (response_id != GTK_RESPONSE_YES) {
		rb_debug ("user doesn't want to sync");
		g_idle_add ((GSourceFunc)sync_idle_cb_cleanup, source);
	} else {
		rb_debug ("user wants to sync");
		g_idle_add ((GSourceFunc) sync_idle_delete_entries, source);
	}
}


static void
display_sync_settings_dialog (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	GtkWidget *content;
	GtkWidget *widget;
	GtkBuilder *builder;
	const char *ui_file;
	char *name;
	char *title;

	g_object_get (source, "name", &name, NULL);
	title = g_strdup_printf (_("%s Sync Settings"), name);

	priv->sync_dialog = gtk_dialog_new_with_buttons (title,
							 NULL,
							 0,
							 _("Sync with the device"),
							 GTK_RESPONSE_YES,
							 _("Don't sync"),
							 GTK_RESPONSE_CANCEL,
							 NULL);
	g_free (title);

	priv->sync_dialog_update_id = g_signal_connect_object (priv->sync_state,
							       "updated",
							       G_CALLBACK (sync_dialog_state_update),
							       source, 0);
	g_signal_connect_object (priv->sync_dialog,
				 "response",
				 G_CALLBACK (sync_confirm_dialog_cb),
				 source, 0);

	/* display the sync settings, the sync state, and some helpful text indicating why
	 * we're not syncing already
	 */
	content = gtk_dialog_get_content_area (GTK_DIALOG (priv->sync_dialog));

	ui_file = rb_file ("sync-dialog.ui");
	if (ui_file == NULL) {
		g_warning ("Couldn't find sync-state.ui");
		gtk_widget_show_all (priv->sync_dialog);
		return;
	}

	builder = rb_builder_load (ui_file, NULL);
	if (builder == NULL) {
		g_warning ("Couldn't load sync-state.ui");
		gtk_widget_show_all (priv->sync_dialog);
		return;
	}

	priv->sync_dialog_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync-dialog-reason"));
	priv->sync_dialog_error_box = GTK_WIDGET (gtk_builder_get_object (builder, "sync-dialog-message"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-settings-ui-container"));
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (widget), rb_sync_settings_ui_new (source, priv->sync_settings));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-state-ui-container"));
	gtk_box_pack_start (GTK_BOX (widget), rb_sync_state_ui_new (priv->sync_state), TRUE, TRUE, 0);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-dialog"));
	gtk_box_pack_start (GTK_BOX (content), widget, TRUE, TRUE, 0);

	gtk_widget_show_all (priv->sync_dialog);
	update_sync_settings_dialog (source);
	g_object_unref (builder);
}

static gboolean
sync_idle_cb_update_sync (RBMediaPlayerSource *source)
{
	update_sync (source);

	if (sync_has_items_enabled (source) == FALSE || sync_has_enough_space (source) == FALSE) {
		rb_debug ("displaying sync settings dialog");
		display_sync_settings_dialog (source);
		return FALSE;
	}

	rb_debug ("sync settings are acceptable");
	return sync_idle_delete_entries (source);
}

static gboolean
sync_idle_delete_entries (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("deleting %d files from media player", priv->sync_state->sync_remove_count);
	rb_media_player_source_delete_entries (source,
					       priv->sync_state->sync_to_remove,
					       (RBMediaPlayerSourceDeleteCallback) sync_delete_done_cb,
					       NULL,
					       NULL);
	return FALSE;
}

void
rb_media_player_source_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	gtk_action_set_sensitive (priv->sync_action, FALSE);

	g_idle_add ((GSourceFunc)sync_idle_cb_update_sync, source);
}

void
_rb_media_player_source_add_to_map (GHashTable *map, RhythmDBEntry *entry)
{
	g_hash_table_insert (map,
			     rb_sync_state_make_track_uuid (entry),
			     rhythmdb_entry_ref (entry));
}

static void
sync_cmd (GtkAction *action, RBSource *source)
{
	rb_media_player_source_sync (RB_MEDIA_PLAYER_SOURCE (source));
}

static RhythmDB *
get_db_for_source (RBSource *source)
{
	RBShell *shell;
	RhythmDB *db;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

gboolean
impl_receive_drag (RBDisplayPage *page, GtkSelectionData *data)
{
	GList *entries;
	RhythmDB *db;
	char *type;

	entries = NULL;
	type = gdk_atom_name (gtk_selection_data_get_data_type (data));
        db = get_db_for_source (RB_SOURCE (page));

	if (strcmp (type, "text/uri-list") == 0) {
		GList *list;
		GList *i;

		rb_debug ("parsing uri list");
		list = rb_uri_list_parse ((const char *) gtk_selection_data_get_data (data));

		for (i = list; i != NULL; i = g_list_next (i)) {
			char *uri;
			RhythmDBEntry *entry;

			if (i->data == NULL)
				continue;

			uri = i->data;
			entry = rhythmdb_entry_lookup_by_location (db, uri);

			if (entry == NULL) {
				/* add to the library */
				rb_debug ("received drop of unknown uri: %s", uri);
			} else {
				/* add to list of entries to copy */
				entries = g_list_prepend (entries, entry);
			}
			g_free (uri);
		}
		g_list_free (list);
	} else if (strcmp (type, "application/x-rhythmbox-entry") == 0) {
		char **list;
		char **i;

		rb_debug ("parsing entry ids");
		list = g_strsplit ((const char*) gtk_selection_data_get_data (data), "\n", -1);
		for (i = list; *i != NULL; i++) {
			RhythmDBEntry *entry;
			gulong id;

			id = atoi (*i);
			entry = rhythmdb_entry_lookup_by_id (db, id);
			if (entry != NULL)
				entries = g_list_prepend (entries, entry);
		}

		g_strfreev (list);
	} else {
		rb_debug ("received unknown drop type");
	}

	g_object_unref (db);
	g_free (type);

	if (entries) {
		entries = g_list_reverse (entries);
		if (rb_source_can_paste (RB_SOURCE (page))) {
			rb_source_paste (RB_SOURCE (page), entries);
		}
		g_list_free (entries);
	}

	return TRUE;
}

static char *
impl_get_delete_action (RBSource *source)
{
	return g_strdup ("EditDelete");
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
	RhythmDB *db;
	RBShell *shell;
	RhythmDBEntryType *entry_type;

	g_object_get (page, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	g_object_get (page, "entry-type", &entry_type, NULL);
	rb_debug ("deleting all entries of type '%s'", rhythmdb_entry_type_get_name (entry_type));
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_object_unref (entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);
}

/* annotations for methods */

/**
 * impl_delete_entries:
 * @source: the source
 * @entries: (element-type RB.RhythmDBEntry) (transfer full): list of entries to delete
 * @callback: callback to call on completion
 * @data: (closure) (scope notified): callback data
 * @destroy_data: callback to free callback data
 */

/**
 * impl_add_playlist:
 * @source: the source
 * @name: new playlist name
 * @entries: (element-type RB.RhythmDBEntry) (transfer full): list of entries to add
 */
