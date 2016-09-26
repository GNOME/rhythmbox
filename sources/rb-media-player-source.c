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
#include "rb-encoding-settings.h"

typedef struct {
	char *uri_prefix;
	char *key_prefix;
} RBMediaPlayerEntryTypePrivate;

typedef struct {
	RBSyncSettings *sync_settings;

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

	GAction *sync_action;
	GAction *properties_action;
	gboolean syncing;

	GstEncodingTarget *encoding_target;
	GSettings *encoding_settings;
} RBMediaPlayerSourcePrivate;

G_DEFINE_TYPE (RBMediaPlayerSource, rb_media_player_source, RB_TYPE_BROWSER_SOURCE);

G_DEFINE_TYPE (RBMediaPlayerEntryType, rb_media_player_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

#define MEDIA_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourcePrivate))
#define MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE, RBMediaPlayerEntryTypePrivate))

static void rb_media_player_entry_type_class_init (RBMediaPlayerEntryTypeClass *klass);
static void rb_media_player_entry_type_init (RBMediaPlayerEntryType *etype);

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

static void sync_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static void properties_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data);
static gboolean sync_idle_delete_entries (RBMediaPlayerSource *source);

static gboolean impl_receive_drag (RBDisplayPage *page, GtkSelectionData *data);
static void impl_delete_thyself (RBDisplayPage *page);

static char *impl_get_delete_label (RBSource *source);

enum
{
	PROP_0,
	PROP_DEVICE_SERIAL,
	PROP_ENCODING_TARGET,
	PROP_ENCODING_SETTINGS,
	PROP_URI_PREFIX,
	PROP_KEY_PREFIX,
};

static char *
impl_uri_to_cache_key (RhythmDBEntryType *etype, const char *uri)
{
	RBMediaPlayerEntryTypePrivate *priv = MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE (etype);

	if (g_str_has_prefix (uri, priv->uri_prefix) == FALSE) {
		return NULL;
	}

	return g_strconcat (priv->key_prefix, ":", uri + strlen (priv->uri_prefix), NULL);
}

static char *
impl_cache_key_to_uri (RhythmDBEntryType *etype, const char *key)
{
	RBMediaPlayerEntryTypePrivate *priv = MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE (etype);

	if (g_str_has_prefix (key, priv->key_prefix) == FALSE) {
		return NULL;
	}

	return g_strconcat (priv->uri_prefix, key + strlen (priv->key_prefix) + 1, NULL);
}

static void
impl_entry_type_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBMediaPlayerEntryTypePrivate *priv = MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_URI_PREFIX:
		priv->uri_prefix = g_value_dup_string (value);
		break;
	case PROP_KEY_PREFIX:
		priv->key_prefix = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_entry_type_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBMediaPlayerEntryTypePrivate *priv = MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_URI_PREFIX:
		g_value_set_string (value, priv->uri_prefix);
		break;
	case PROP_KEY_PREFIX:
		g_value_set_string (value, priv->key_prefix);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_entry_type_finalize (GObject *object)
{
	RBMediaPlayerEntryTypePrivate *priv = MEDIA_PLAYER_ENTRY_TYPE_GET_PRIVATE (object);

	g_free (priv->uri_prefix);
	g_free (priv->key_prefix);

	G_OBJECT_CLASS (rb_media_player_entry_type_parent_class)->finalize (object);
}

static void
rb_media_player_entry_type_class_init (RBMediaPlayerEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_entry_type_set_property;
	object_class->get_property = impl_entry_type_get_property;
	object_class->finalize = impl_entry_type_finalize;

	/* sync_metadata? */
	etype_class->uri_to_cache_key = impl_uri_to_cache_key;
	etype_class->cache_key_to_uri = impl_cache_key_to_uri;

	g_object_class_install_property (object_class,
					 PROP_KEY_PREFIX,
					 g_param_spec_string ("key-prefix",
							      "key prefix",
							      "metadata cache key prefix",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_URI_PREFIX,
					 g_param_spec_string ("uri-prefix",
							      "uri prefix",
							      "uri prefix for entries",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBMediaPlayerEntryTypePrivate));
}

static void
rb_media_player_entry_type_init (RBMediaPlayerEntryType *etype)
{
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

	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->get_delete_label = impl_get_delete_label;
	source_class->delete_selected = NULL;

	browser_source_class->has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	klass->get_entries = NULL;
	klass->get_capacity = NULL;
	klass->get_free_space = NULL;
	klass->add_playlist = NULL;
	klass->remove_playlists = NULL;
	klass->show_properties = NULL;

	g_object_class_install_property (object_class,
					 PROP_DEVICE_SERIAL,
					 g_param_spec_string ("serial",
							      "serial",
							      "device serial number",
							      NULL,
							      G_PARAM_READABLE));
	/**
	 * RBMediaPlayerSource:encoding-target:
	 *
	 * The #GstEncodingTarget for this device
	 */
	g_object_class_install_property (object_class,
					 PROP_ENCODING_TARGET,
					 g_param_spec_object ("encoding-target",
							      "encoding target",
							      "GstEncodingTarget",
							      GST_TYPE_ENCODING_TARGET,
							      G_PARAM_READWRITE));
	/**
	 * RBMediaPlayerSource:encoding-settings
	 *
	 * The #GSettings instance holding encoding settings for this device
	 */
	g_object_class_install_property (object_class,
					 PROP_ENCODING_SETTINGS,
					 g_param_spec_object ("encoding-settings",
							      "encoding settings",
							      "GSettings holding encoding settings",
							      G_TYPE_SETTINGS,
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
		priv->encoding_target = GST_ENCODING_TARGET (g_value_dup_object (value));
		break;
	case PROP_ENCODING_SETTINGS:
		if (priv->encoding_settings) {
			g_object_unref (priv->encoding_settings);
		}
		priv->encoding_settings = g_value_dup_object (value);
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
		g_value_set_object (value, priv->encoding_target);
		break;
	case PROP_ENCODING_SETTINGS:
		g_value_set_object (value, priv->encoding_settings);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
update_actions (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	RBSourceLoadStatus status;
	gboolean selected;

	g_object_get (source,
		      "load-status", &status,
		      "selected", &selected,
		      NULL);

	if (selected) {
		g_simple_action_set_enabled (G_SIMPLE_ACTION (priv->sync_action),
					     (status == RB_SOURCE_LOAD_STATUS_LOADED) && (priv->syncing == FALSE));
		g_simple_action_set_enabled (G_SIMPLE_ACTION (priv->properties_action),
					     (status == RB_SOURCE_LOAD_STATUS_LOADED));
	}
}

static void
load_status_changed_cb (GObject *object, GParamSpec *pspec, gpointer whatever)
{
	update_actions (RB_MEDIA_PLAYER_SOURCE (object));
}

static void
selected_changed_cb (GObject *object, GParamSpec *pspec, gpointer whatever)
{
	update_actions (RB_MEDIA_PLAYER_SOURCE (object));
}

static void
rb_media_player_source_constructed (GObject *object)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (object);
	RBShell *shell;
	GApplication *app;
	GActionEntry actions[] = {
		{ "media-player-sync", sync_action_cb },
		{ "media-player-properties", properties_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_media_player_source_parent_class, constructed, object);

	app = g_application_get_default ();
	g_object_get (object, "shell", &shell, NULL);
	_rb_add_display_page_actions (G_ACTION_MAP (app), G_OBJECT (shell), actions, G_N_ELEMENTS (actions));
	g_object_unref (shell);

	priv->sync_action = g_action_map_lookup_action (G_ACTION_MAP (app), "media-player-sync");
	priv->properties_action = g_action_map_lookup_action (G_ACTION_MAP (app), "media-player-properties");
	g_signal_connect (object, "notify::load-status", G_CALLBACK (load_status_changed_cb), NULL);
	g_signal_connect (object, "notify::selected", G_CALLBACK (selected_changed_cb), NULL);
	update_actions (RB_MEDIA_PLAYER_SOURCE (object));
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

	return klass->get_capacity (source);
}

guint64
rb_media_player_source_get_free_space (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	return klass->get_free_space (source);
}

/**
 * rb_media_player_source_get_entries:
 * @source: the #RBMediaPlayerSource
 * @category: the sync category name
 * @map: (element-type utf8 RB.RhythmDBEntry): map to hold the entries
 */
void
rb_media_player_source_get_entries (RBMediaPlayerSource *source,
				    const char *category,
				    GHashTable *map)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);
	klass->get_entries (source, category, map);
}

/**
 * rb_media_player_source_delete_entries:
 * @source: the #RBMediaPlayerSource
 * @entries: (element-type RB.RhythmDBEntry) (transfer full): list of entries to delete
 * @callback: callback to call on completion
 * @data: data for callback
 */
void
rb_media_player_source_delete_entries	(RBMediaPlayerSource *source,
					 GList *entries,
					 GAsyncReadyCallback callback,
					 gpointer data)
{
	RBMediaPlayerSourceClass *klass = RB_MEDIA_PLAYER_SOURCE_GET_CLASS (source);

	klass->delete_entries (source, entries, callback, data);
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
	char *name;
	char *text;

	if (priv->properties_dialog != NULL) {
		gtk_window_present (GTK_WINDOW (priv->properties_dialog));
		return;
	}

	/* load dialog UI */
	builder = rb_builder_load ("media-player-properties.ui", NULL);
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

	if (klass->show_properties) {
		klass->show_properties (source,
					     GTK_WIDGET (gtk_builder_get_object (builder, "device-info-box")),
					     GTK_WIDGET (gtk_builder_get_object (builder, "media-player-notebook")));
	}

	/* create sync UI */
	container = GTK_CONTAINER (gtk_builder_get_object (builder, "sync-settings-ui-container"));
	gtk_container_add (container, rb_sync_settings_ui_new (source, priv->sync_settings));

	container = GTK_CONTAINER (gtk_builder_get_object (builder, "sync-state-ui-container"));
	gtk_box_pack_start (GTK_BOX (container), rb_sync_state_ui_new (priv->sync_state), TRUE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (container));

	/* create encoding settings UI */
	if (priv->encoding_settings) {
		container = GTK_CONTAINER (gtk_builder_get_object (builder, "encoding-settings-container"));
		gtk_container_add (container, rb_encoding_settings_new (priv->encoding_settings, priv->encoding_target, TRUE));
		gtk_widget_show_all (GTK_WIDGET (container));
	} else {
		container = GTK_CONTAINER (gtk_builder_get_object (builder, "encoding-settings-frame"));
		gtk_widget_hide (GTK_WIDGET (container));
		gtk_widget_set_no_show_all (GTK_WIDGET (container), TRUE);
	}

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

	if (klass->add_playlist == NULL || klass->remove_playlists == NULL) {
		rb_debug ("source class doesn't support playlists");
		return;
	}

	/* build an updated device contents map, so we can find the device entries
	 * corresponding to the entries in the local playlists.
	 */
	device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)rhythmdb_entry_unref);
	rb_media_player_source_get_entries (source, SYNC_CATEGORY_MUSIC, device);

	/* remove all playlists from the device, then add the synced playlists. */
	klass->remove_playlists (source);

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
		if (rb_sync_settings_sync_group (priv->sync_settings, SYNC_CATEGORY_MUSIC, name) == FALSE) {
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
		klass->add_playlist (source, name, tracks);

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

	priv->syncing = FALSE;
	update_actions (source);

	/* release the ref taken at the start of the sync */
	g_object_unref (source);

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
sync_delete_done_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBMediaPlayerSource *source = RB_MEDIA_PLAYER_SOURCE (source_object);
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);
	rb_debug ("finished deleting %d files from media player", priv->sync_state->sync_remove_count);

	/* Transfer needed tracks and podcasts from itinerary to device */
	if (priv->sync_state->sync_add_count != 0) {
		RBTrackTransferBatch *batch;

		rb_debug ("transferring %d files to media player", priv->sync_state->sync_add_count);
		batch = rb_source_paste (RB_SOURCE (source), priv->sync_state->sync_to_add);
		if (batch != NULL) {
			char *name;
			char *label;

			g_object_get (source, "name", &name, NULL);
			label = g_strdup_printf (_("Syncing tracks to %s"), name);
			g_free (name);

			g_object_set (batch, "task-label", label, NULL);
			g_free (label);

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
	gboolean show_error;

	if (sync_has_items_enabled (source) == FALSE) {
		can_continue = FALSE;
		show_error = TRUE;
		gtk_label_set_text (GTK_LABEL (priv->sync_dialog_label),
				    _("You have not selected any music, playlists, or podcasts to transfer to this device."));
	} else if (sync_has_enough_space (source) == FALSE) {
		can_continue = TRUE;
		show_error = TRUE;
		gtk_label_set_text (GTK_LABEL (priv->sync_dialog_label),
				    _("There is not enough space on the device to transfer the selected music, playlists and podcasts."));
	} else {
		can_continue = TRUE;
		show_error = FALSE;
	}

	gtk_widget_set_visible (priv->sync_dialog_error_box, show_error);
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

	builder = rb_builder_load ("sync-dialog.ui", NULL);
	if (builder == NULL) {
		g_warning ("Couldn't load sync-dialog.ui");
		gtk_widget_show_all (priv->sync_dialog);
		return;
	}

	priv->sync_dialog_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync-dialog-reason"));
	priv->sync_dialog_error_box = GTK_WIDGET (gtk_builder_get_object (builder, "sync-dialog-message"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "sync-settings-ui-container"));
	gtk_container_add (GTK_CONTAINER (widget), rb_sync_settings_ui_new (source, priv->sync_settings));

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
					       sync_delete_done_cb,
					       NULL);
	return FALSE;
}

void
rb_media_player_source_sync (RBMediaPlayerSource *source)
{
	RBMediaPlayerSourcePrivate *priv = MEDIA_PLAYER_SOURCE_GET_PRIVATE (source);

	priv->syncing = TRUE;
	update_actions (source);

	/* ref the source for the duration of the sync operation */
	g_idle_add ((GSourceFunc)sync_idle_cb_update_sync, g_object_ref (source));
}

void
_rb_media_player_source_add_to_map (GHashTable *map, RhythmDBEntry *entry)
{
	g_hash_table_insert (map,
			     rb_sync_state_make_track_uuid (entry),
			     rhythmdb_entry_ref (entry));
}

void
rb_media_player_source_purge_metadata_cache (RBMediaPlayerSource *source)
{
	RhythmDBEntryType *entry_type;
	GSettings *settings;
	char *prefix;
	gulong max_age;

	settings = g_settings_new ("org.gnome.rhythmbox.rhythmdb");
	max_age = g_settings_get_int (settings, "grace-period");
	g_object_unref (settings);

	if (max_age > 0 && max_age < 20000) {
		g_object_get (source, "entry-type", &entry_type, NULL);
		g_object_get (entry_type, "key-prefix", &prefix, NULL);
		rhythmdb_entry_type_purge_metadata_cache (entry_type, prefix, max_age * 60 * 60 * 24);
		g_object_unref (entry_type);
		g_free (prefix);
	}
}

static void
sync_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	rb_media_player_source_sync (RB_MEDIA_PLAYER_SOURCE (data));
}

static void
properties_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	rb_media_player_source_show_properties (RB_MEDIA_PLAYER_SOURCE (data));
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
impl_get_delete_label (RBSource *source)
{
	return g_strdup (_("Delete"));
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
