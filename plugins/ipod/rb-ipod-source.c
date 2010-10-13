/*
 *  Copyright (C) 2004, 2007 Christophe Fergeau  <teuf@gnome.org>
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

#include <errno.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gpod/itdb.h>

#include "eel-gconf-extensions.h"
#include "rb-ipod-source.h"
#include "rb-ipod-db.h"
#include "rb-ipod-helpers.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-plugin.h"
#include "rb-removable-media-manager.h"
#include "rb-ipod-static-playlist-source.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rb-cut-and-paste-code.h"
#include "rb-media-player-source.h"
#include "rb-sync-settings.h"
#include "rb-playlist-source.h"
#include "rb-playlist-manager.h"
#include "rb-podcast-manager.h"
#include "rb-podcast-entry-types.h"
#include "rb-stock-icons.h"

#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/ipod/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/ipod/show_browser"

static void rb_ipod_source_constructed (GObject *object);
static void rb_ipod_source_class_init (RBiPodSourceClass *klass);
static void rb_ipod_source_dispose (GObject *object);

static char *impl_get_browser_key (RBSource *source);
static char *impl_get_paned_key (RBBrowserSource *source);

static gboolean impl_show_popup (RBSource *source);
static void impl_delete (RBSource *asource);
static void rb_ipod_load_songs (RBiPodSource *source);
static void impl_delete_thyself (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);

static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
				  guint64 filesize,
				  const char *mimetype);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);
static gchar* ipod_get_filename_for_uri (const gchar *mount_point,
					 const gchar *uri_str,
					 const gchar *mimetype,
					 const gchar *extension);
static gchar* ipod_path_from_unix_path (const gchar *mount_point,
					const gchar *unix_path);
static gboolean rb_ipod_song_artwork_add_cb (RhythmDB *db,
                                             RhythmDBEntry *entry,
                                             const gchar *property_name,
                                             const GValue *metadata,
                                             RBiPodSource *isource);

static guint64 impl_get_capacity (RBMediaPlayerSource *source);
static guint64 impl_get_free_space (RBMediaPlayerSource *source);
static void impl_get_entries (RBMediaPlayerSource *source, const char *category, GHashTable *map);
static void impl_delete_entries (RBMediaPlayerSource *source, GList *entries, RBMediaPlayerSourceDeleteCallback callback, gpointer callback_data, GDestroyNotify destroy_data);
static void impl_add_playlist (RBMediaPlayerSource *source, gchar *name, GList *entries);
static void impl_remove_playlists (RBMediaPlayerSource *source);
static void impl_show_properties (RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook);

static void rb_ipod_source_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void rb_ipod_source_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);


static RhythmDB *get_db_for_source (RBiPodSource *source);

struct _PlayedEntry {
	RhythmDBEntry *entry;
	guint play_count;
};

typedef struct _PlayedEntry PlayedEntry;

typedef struct
{
	RbIpodDb *ipod_db;
	GHashTable *entry_map;

	MPIDDevice *device_info;

	gboolean needs_shuffle_db;
	RBIpodStaticPlaylistSource *podcast_pl;

	guint load_idle_id;

	GHashTable *artwork_request_map;
	guint artwork_notify_id;

	GQueue *offline_plays;
} RBiPodSourcePrivate;

typedef struct {
	RBiPodSourcePrivate *priv;
	GdkPixbuf *pixbuf;
} RBiPodSongArtworkAddData;

enum
{
	PROP_0,
	PROP_DEVICE_INFO,
	PROP_DEVICE_SERIAL
};

RB_PLUGIN_DEFINE_TYPE(RBiPodSource,
		      rb_ipod_source,
		      RB_TYPE_MEDIA_PLAYER_SOURCE)

#define IPOD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourcePrivate))

static void
rb_ipod_source_class_init (RBiPodSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBMediaPlayerSourceClass *mps_class = RB_MEDIA_PLAYER_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->constructed = rb_ipod_source_constructed;
	object_class->dispose = rb_ipod_source_dispose;

	object_class->set_property = rb_ipod_source_set_property;
	object_class->get_property = rb_ipod_source_get_property;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;

	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;

	mps_class->impl_get_entries = impl_get_entries;
	mps_class->impl_get_capacity = impl_get_capacity;
	mps_class->impl_get_free_space = impl_get_free_space;
	mps_class->impl_delete_entries = impl_delete_entries;
	mps_class->impl_add_playlist = impl_add_playlist;
	mps_class->impl_remove_playlists = impl_remove_playlists;
	mps_class->impl_show_properties = impl_show_properties;

	rms_class->impl_should_paste = rb_removable_media_source_should_paste_no_duplicate;
	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;

	browser_source_class->impl_get_paned_key = impl_get_paned_key;

	g_object_class_install_property (object_class,
					 PROP_DEVICE_INFO,
					 g_param_spec_object ("device-info",
							      "device info",
							      "device information object",
							      MPID_TYPE_DEVICE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_override_property (object_class, PROP_DEVICE_SERIAL, "serial");

	g_type_class_add_private (klass, sizeof (RBiPodSourcePrivate));
}

static void
rb_ipod_source_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_DEVICE_INFO:
		priv->device_info = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_ipod_source_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_DEVICE_INFO:
		g_value_set_object (value, priv->device_info);
		break;
	case PROP_DEVICE_SERIAL:
		{
			char *serial;
			g_object_get (priv->device_info, "serial", &serial, NULL);
			g_value_take_string (value, serial);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_ipod_source_set_ipod_name (RBiPodSource *source, const char *name)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	if (priv->ipod_db == NULL) {
		rb_debug ("can't change ipod name with no ipod db");
		return;
	}

	rb_ipod_db_set_ipod_name (priv->ipod_db, name);
}

static void
rb_ipod_source_name_changed_cb (RBiPodSource *source, GParamSpec *spec,
				gpointer data)
{
	char *name;

	g_object_get (source, "name", &name, NULL);
	rb_ipod_source_set_ipod_name (source, name);
	g_free (name);
}

static void
rb_ipod_source_init (RBiPodSource *source)
{
}

static void
rb_ipod_source_constructed (GObject *object)
{
	RBiPodSource *source;
	RBEntryView *songs;
	RhythmDB *db;

	RB_CHAIN_GOBJECT_METHOD (rb_ipod_source_parent_class, constructed, object);
	source = RB_IPOD_SOURCE (object);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
        rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_FIRST_SEEN, FALSE);

	rb_ipod_load_songs (source);

	db = get_db_for_source (source);
	g_signal_connect_object (db,
                                 "entry-extra-metadata-notify::rb:coverArt",
                                 G_CALLBACK (rb_ipod_song_artwork_add_cb),
                                 source, 0);

        g_object_unref (db);

        rb_media_player_source_load (RB_MEDIA_PLAYER_SOURCE (source));
}

static void
rb_ipod_source_dispose (GObject *object)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (object);

	if (priv->ipod_db) {
		g_object_unref (G_OBJECT (priv->ipod_db));
		priv->ipod_db = NULL;
	}

	if (priv->entry_map) {
		g_hash_table_destroy (priv->entry_map);
		priv->entry_map = NULL;
 	}

	if (priv->load_idle_id != 0) {
		g_source_remove (priv->load_idle_id);
		priv->load_idle_id = 0;
	}

	if (priv->artwork_request_map) {
		g_hash_table_destroy (priv->artwork_request_map);
		priv->artwork_request_map = NULL;
 	}

	if (priv->artwork_notify_id) {
		RhythmDB *db = get_db_for_source (RB_IPOD_SOURCE (object));
		g_signal_handler_disconnect (db, priv->artwork_notify_id);
		priv->artwork_notify_id = 0;
		g_object_unref (db);
	}

	if (priv->offline_plays) {
		g_queue_foreach (priv->offline_plays,
				 (GFunc)g_free, NULL);
		g_queue_free (priv->offline_plays);
		priv->offline_plays = NULL;
	}

	G_OBJECT_CLASS (rb_ipod_source_parent_class)->dispose (object);
}

RBMediaPlayerSource *
rb_ipod_source_new (RBPlugin *plugin,
		    RBShell *shell,
		    GMount *mount,
		    MPIDDevice *device_info)
{
	RBiPodSource *source;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	GVolume *volume;
	char *name;
	char *path;

	volume = g_mount_get_volume (mount);
	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (path == NULL)
		path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UUID);
	g_object_unref (volume);

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("ipod: %s", path);
	entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_object_unref (db);
	g_free (name);
	g_free (path);

	source = RB_IPOD_SOURCE (g_object_new (RB_TYPE_IPOD_SOURCE,
				               "plugin", plugin,
					       "entry-type", entry_type,
					       "mount", mount,
					       "shell", shell,
					       "source-group", RB_SOURCE_GROUP_DEVICES,
					       "device-info", device_info,
					       NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);
        g_object_unref (entry_type);

	return RB_MEDIA_PLAYER_SOURCE (source);
}

static void
entry_set_string_prop (RhythmDB *db, RhythmDBEntry *entry,
		       RhythmDBPropType propid, const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static char *
ipod_path_to_uri (const char *mount_point, const char *ipod_path)
{
 	char *rel_pc_path;
 	char *full_pc_path;
 	char *uri;

 	rel_pc_path = g_strdup (ipod_path);
 	itdb_filename_ipod2fs (rel_pc_path);
 	full_pc_path = g_build_filename (mount_point, rel_pc_path, NULL);
 	g_free (rel_pc_path);
 	uri = g_filename_to_uri (full_pc_path, NULL, NULL);
 	g_free (full_pc_path);
 	return uri;
}

static void
playlist_track_removed (RhythmDBQueryModel *m,
			RhythmDBEntry *entry,
			gpointer data)
{
	RBIpodStaticPlaylistSource *playlist = RB_IPOD_STATIC_PLAYLIST_SOURCE (data);
	Itdb_Playlist *ipod_pl = rb_ipod_static_playlist_source_get_itdb_playlist (playlist);
	RBiPodSource *ipod = rb_ipod_static_playlist_source_get_ipod_source (playlist);
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (ipod);
	Itdb_Track *track;

	g_return_if_fail (ipod != NULL);
	g_return_if_fail (ipod_pl != NULL);

	track = g_hash_table_lookup (priv->entry_map, entry);
	g_return_if_fail (track != NULL);
	rb_ipod_db_remove_from_playlist (priv->ipod_db, ipod_pl, track);
}

static void
playlist_track_added (GtkTreeModel *model, GtkTreePath *path,
		      GtkTreeIter *iter, gpointer data)
{
	RBIpodStaticPlaylistSource *playlist = RB_IPOD_STATIC_PLAYLIST_SOURCE (data);
	Itdb_Playlist *ipod_pl = rb_ipod_static_playlist_source_get_itdb_playlist (playlist);
	RBiPodSource *ipod = rb_ipod_static_playlist_source_get_ipod_source (playlist);
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (ipod);
	Itdb_Track *track;
	RhythmDBEntry *entry;

	g_return_if_fail (ipod != NULL);
	g_return_if_fail (ipod_pl != NULL);

	gtk_tree_model_get (model, iter, 0, &entry, -1);
        track = g_hash_table_lookup (priv->entry_map, entry);
	g_return_if_fail (track != NULL);

	rb_ipod_db_add_to_playlist (priv->ipod_db, ipod_pl, track);
}

static void playlist_source_model_connect_signals (RBIpodStaticPlaylistSource *playlist_source)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (RB_IS_IPOD_STATIC_PLAYLIST_SOURCE (playlist_source));

	g_object_get (G_OBJECT (playlist_source),
		      "base-query-model", &model, NULL);
	g_signal_connect (model, "row-inserted",
			  G_CALLBACK (playlist_track_added),
			  playlist_source);
	g_signal_connect (model, "entry-removed",
			  G_CALLBACK (playlist_track_removed),
			  playlist_source);
	g_object_unref (model);
}

static void playlist_source_model_changed (GObject *obj, GParamSpec *pspec, gpointer old_model)
{
	RBIpodStaticPlaylistSource *playlist_source;

	rb_debug ("base model changed for iPod playlist");

	playlist_source = RB_IPOD_STATIC_PLAYLIST_SOURCE (obj);
	g_signal_handlers_disconnect_by_func (G_OBJECT (old_model),
					      G_CALLBACK (playlist_track_added),
					      playlist_source);
	g_signal_handlers_disconnect_by_func (G_OBJECT (old_model),
					      G_CALLBACK (playlist_track_removed),
					      playlist_source);
	playlist_source_model_connect_signals (playlist_source);
}
static void
set_podcast_icon (RBIpodStaticPlaylistSource *source)
{
	GdkPixbuf *pixbuf;
	gint       size;

	gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   RB_STOCK_PODCAST,
					   size,
					   0, NULL);

	if (pixbuf != NULL) {
	    rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	    g_object_unref (pixbuf);
	}
}

static RBIpodStaticPlaylistSource *
add_rb_playlist (RBiPodSource *source, Itdb_Playlist *playlist)
{
	RBShell *shell;
	RBIpodStaticPlaylistSource *playlist_source;
	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType *entry_type;
	RhythmDBQueryModel *model;

	g_object_get (source,
			  "shell", &shell,
			  "entry-type", &entry_type,
			  NULL);

	playlist_source = rb_ipod_static_playlist_source_new (shell,
                                                              source,
                                                              priv->ipod_db,
                                                              playlist,
                                                              entry_type);
	g_object_unref (entry_type);

	for (it = playlist->members; it != NULL; it = it->next) {
		Itdb_Track *song;
		char *filename;
		const char *mount_path;

		song = (Itdb_Track *)it->data;
		mount_path = rb_ipod_db_get_mount_path (priv->ipod_db);
 		filename = ipod_path_to_uri (mount_path, song->ipod_path);
		rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (playlist_source),
							filename, -1);
		g_free (filename);
	}

        /* RBSource derives from GtkWidget so its initial reference is
         * floating. Since we need a ref for ourselves and we don't want it to
         * be stolen by a GtkContainer, we sink the floating reference.
         */
	g_object_ref_sink (G_OBJECT(playlist_source));
	playlist->userdata = playlist_source;
	playlist->userdata_destroy = g_object_unref;
	playlist->userdata_duplicate = g_object_ref;

	g_object_get (G_OBJECT (playlist_source),
		      "base-query-model", &model, NULL);
	g_signal_connect (playlist_source, "notify::base-query-model",
			  G_CALLBACK (playlist_source_model_changed),
			  playlist_source);
	g_object_unref (model);
	playlist_source_model_connect_signals (playlist_source);

	if (itdb_playlist_is_podcasts(playlist)) {
		priv->podcast_pl = playlist_source;
		set_podcast_icon (playlist_source);
	}
	rb_shell_append_source (shell, RB_SOURCE (playlist_source), RB_SOURCE (source));
	g_object_unref (shell);

	return playlist_source;
}

static void
load_ipod_playlists (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GList *it;

	for (it = rb_ipod_db_get_playlists (priv->ipod_db);
	     it != NULL;
	     it = it->next) {
		Itdb_Playlist *playlist;

		playlist = (Itdb_Playlist *)it->data;
		if (itdb_playlist_is_mpl (playlist)) {
			continue;
		} else if (playlist->is_spl) {
			continue;
		}

		add_rb_playlist (source, playlist);
	}

}

static Itdb_Track *
create_ipod_song_from_entry (RhythmDBEntry *entry, guint64 filesize, const char *mimetype)
{
	Itdb_Track *track;

	track = itdb_track_new ();

	track->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	track->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	track->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	track->albumartist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	track->sort_artist = rhythmdb_entry_dup_string (entry,
	                                                RHYTHMDB_PROP_ARTIST_SORTNAME);
	track->sort_album = rhythmdb_entry_dup_string (entry,
	                                                RHYTHMDB_PROP_ALBUM_SORTNAME);
	track->sort_albumartist = rhythmdb_entry_dup_string (entry,
	                                       	       	     RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
	track->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	track->filetype = g_strdup (mimetype);
	track->size = filesize;
	track->tracklen = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
	track->tracklen *= 1000;
	track->cd_nr = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
	track->track_nr = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	track->bitrate = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE);
	track->year = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR);
	track->time_added = time (NULL);
	track->time_played = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_LAST_PLAYED);
	track->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);
	track->rating *= ITDB_RATING_STEP;
	track->app_rating = track->rating;
	track->playcount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);

	if (rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		track->mediatype = ITDB_MEDIATYPE_PODCAST;
		track->time_released = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
	} else {
		track->mediatype = ITDB_MEDIATYPE_AUDIO;
	}

	return track;
}

static void add_offline_played_entry (RBiPodSource *source,
				      RhythmDBEntry *entry,
				      guint play_count)
{
	PlayedEntry *played_entry;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	if (play_count == 0) {
		return;
	}

	if (priv->offline_plays == NULL) {
		priv->offline_plays = g_queue_new();
	}

	played_entry = g_new0 (PlayedEntry, 1);
	played_entry->entry = entry;
	played_entry->play_count = play_count;

	g_queue_push_tail (priv->offline_plays, played_entry);
}

static void
add_ipod_song_to_db (RBiPodSource *source, RhythmDB *db, Itdb_Track *song)
{
	RhythmDBEntry *entry;
	RhythmDBEntryType *entry_type;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	char *pc_path;
	const char *mount_path;

	/* Set URI */
	g_object_get (source, "entry-type", &entry_type, NULL);
	mount_path = rb_ipod_db_get_mount_path (priv->ipod_db);
	pc_path = ipod_path_to_uri (mount_path, song->ipod_path);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type, pc_path);
	g_object_unref (entry_type);

	if (entry == NULL) {
		rb_debug ("cannot create entry %s", pc_path);
		g_free (pc_path);
		return;
	}

	if ((song->mediatype != ITDB_MEDIATYPE_AUDIO)
	    && (song->mediatype != ITDB_MEDIATYPE_PODCAST)) {
		rb_debug ("iPod track is neither an audio track nor a podcast, skipping");
		return;
	}

	rb_debug ("Adding %s from iPod", pc_path);
	g_free (pc_path);

	/* Set track number */
	if (song->track_nr != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->track_nr);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_TRACK_NUMBER,
					       &value);
		g_value_unset (&value);
	}

	/* Set disc number */
	if (song->cd_nr != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->cd_nr);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_DISC_NUMBER,
					       &value);
		g_value_unset (&value);
	}

	/* Set bitrate */
	if (song->bitrate != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->bitrate);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_BITRATE,
					       &value);
		g_value_unset (&value);
	}

	/* Set length */
	if (song->tracklen != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->tracklen/1000);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_DURATION,
					       &value);
		g_value_unset (&value);
	}

	/* Set file size */
	if (song->size != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64 (&value, song->size);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_FILE_SIZE,
					       &value);
		g_value_unset (&value);
	}

	/* Set playcount */
	if (song->playcount != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->playcount);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_PLAY_COUNT,
					       &value);
		g_value_unset (&value);
	}

	/* Set year */
	if (song->year != 0) {
		GDate *date = NULL;
		GType type;
		GValue value = {0, };

		date = g_date_new_dmy (1, G_DATE_JANUARY, song->year);

		type = rhythmdb_get_property_type (RHYTHMDB(db),
						   RHYTHMDB_PROP_DATE);

		g_value_init (&value, type);
		g_value_set_ulong (&value, (date ? g_date_get_julian (date) : 0));

		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_DATE,
					       &value);
		g_value_unset (&value);
		if (date)
			g_date_free (date);
	}

	/* Set rating */
	if (song->rating != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_DOUBLE);
		g_value_set_double (&value, song->rating/ITDB_RATING_STEP);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_RATING,
					       &value);
		g_value_unset (&value);
	}

	/* Set last added time */
	if (song->time_added != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->time_added);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_FIRST_SEEN,
					       &value);
		g_value_unset (&value);
	}

	/* Set last played */
	if (song->time_played != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, song->time_played);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_LAST_PLAYED,
					       &value);
		g_value_unset (&value);
	}

	/* Set title */
	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_TITLE, song->title);

	/* Set album, artist and genre from iTunesDB */
	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_ARTIST, song->artist);

	if (song->albumartist != NULL) {
                entry_set_string_prop (RHYTHMDB (db), entry,
                                       RHYTHMDB_PROP_ALBUM_ARTIST,
                                       song->albumartist);
	}

        if (song->sort_artist != NULL) {
                entry_set_string_prop (RHYTHMDB (db), entry,
                                       RHYTHMDB_PROP_ARTIST_SORTNAME,
                                       song->sort_artist);
        }

        if (song->sort_album != NULL) {
                entry_set_string_prop (RHYTHMDB (db), entry,
                                       RHYTHMDB_PROP_ALBUM_SORTNAME,
                                       song->sort_album);
        }

	if (song->sort_albumartist != NULL) {
                entry_set_string_prop (RHYTHMDB (db), entry,
                                       RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME,
                                       song->sort_albumartist);
	}

	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_ALBUM, song->album);

	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_GENRE, song->genre);

	g_hash_table_insert (priv->entry_map, entry, song);

	if (song->recent_playcount != 0) {
		add_offline_played_entry (source, entry,
					  song->recent_playcount);
	}

	rhythmdb_commit (RHYTHMDB (db));
}

static RhythmDB *
get_db_for_source (RBiPodSource *source)
{
	RBShell *shell;
	RhythmDB *db;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

static gint
compare_timestamps (gconstpointer a, gconstpointer b, gpointer data)
{
	PlayedEntry *lhs = (PlayedEntry *)a;
	PlayedEntry *rhs = (PlayedEntry *)b;

	gulong lhs_timestamp;
	gulong rhs_timestamp;

	lhs_timestamp =  rhythmdb_entry_get_ulong (lhs->entry,
						   RHYTHMDB_PROP_LAST_PLAYED);

	rhs_timestamp =  rhythmdb_entry_get_ulong (rhs->entry,
						   RHYTHMDB_PROP_LAST_PLAYED);


	return (int) (lhs_timestamp - rhs_timestamp);
}

static void
remove_playcount_file (RBiPodSource *source)
{
        RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
        char *itunesdb_dir;
        char *playcounts_file;
        int result;
	const char *mountpoint;

	mountpoint = rb_ipod_db_get_mount_path (priv->ipod_db);
        itunesdb_dir = itdb_get_itunes_dir (mountpoint);
        playcounts_file = itdb_get_path (itunesdb_dir, "Play Counts");
        result = g_unlink (playcounts_file);
        if (result == 0) {
                rb_debug ("iPod Play Counts file successfully deleted");
        } else {
                rb_debug ("Failed to remove iPod Play Counts file: %s",
                          strerror (errno));
        }
        g_free (itunesdb_dir);
        g_free (playcounts_file);

}

static void
send_offline_plays_notification (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	GValue val = {0, };

	if (priv->offline_plays == NULL) {
		return;
	}

	/* audioscrobbler expects data to arrive with increasing timestamps,
	 * dunno if the sorting should be done in the audioscrobbler plugin,
	 * or if this kind of "insider knowledge" is OK here
	 */
	g_queue_sort (priv->offline_plays,
		      (GCompareDataFunc)compare_timestamps,
		      NULL);

	db = get_db_for_source (source);
	g_value_init (&val, G_TYPE_ULONG);

	while (!g_queue_is_empty (priv->offline_plays)) {
		gulong last_play;
		PlayedEntry *entry;
		entry = (PlayedEntry*)g_queue_pop_head (priv->offline_plays);
		last_play = rhythmdb_entry_get_ulong (entry->entry,
						      RHYTHMDB_PROP_LAST_PLAYED);
		g_value_set_ulong (&val, last_play);
		rhythmdb_emit_entry_extra_metadata_notify (db, entry->entry,
							   "rb:offlinePlay",
							   &val);
		g_free (entry);
	}
	g_value_unset (&val);
	g_object_unref (G_OBJECT (db));

	remove_playcount_file (source);
}

static void
rb_ipod_source_entry_changed_cb (RhythmDB *db,
				 RhythmDBEntry *entry,
				 GValueArray *changes,
				 RBiPodSource *source)
{
	int i;

	/* Ignore entries which are not iPod entries */
	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *ipod_entry_type;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	entry_type = rhythmdb_entry_get_entry_type (entry);
	g_object_get (source, "entry-type", &ipod_entry_type, NULL);
	if (entry_type != ipod_entry_type) {
		g_object_unref (ipod_entry_type);
		return;
	}
	g_object_unref (ipod_entry_type);

	/* If an interesting property was changed, update it on the iPod */
	/* If the iPod database is being saved in a separate thread, this
	 * might not be 100% thread-safe, but at worst we'll modify a field
	 * at the time it's being saved which will get a wrong value, but
	 * that's the worst that can happen and that's pretty theoretical,
	 * I don't think avoiding it is worth the effort.
	 */
	for (i = 0; i < changes->n_values; i++) {
		GValue *v = g_value_array_get_nth (changes, i);
		RhythmDBEntryChange *change = g_value_get_boxed (v);
		switch (change->prop) {
		case RHYTHMDB_PROP_RATING: {
			Itdb_Track *track;
			double old_rating;
			double new_rating;

			old_rating = g_value_get_double (&change->old);
			new_rating = g_value_get_double (&change->new);
			if (old_rating != new_rating) {
				track = g_hash_table_lookup (priv->entry_map,
							     entry);
				track->rating = new_rating * ITDB_RATING_STEP;
				track->app_rating = track->rating;
				rb_debug ("rating changed, saving db");
				rb_ipod_db_save_async (priv->ipod_db);
			} else {
				rb_debug ("rating didn't change");
			}
			break;
		}
		case RHYTHMDB_PROP_PLAY_COUNT: {
			Itdb_Track *track;
			gulong old_playcount;
			gulong new_playcount;

			old_playcount = g_value_get_ulong (&change->old);
			new_playcount = g_value_get_ulong (&change->new);
			if (old_playcount != new_playcount) {
				track = g_hash_table_lookup (priv->entry_map,
							     entry);
				track->playcount = new_playcount;
				rb_debug ("playcount changed, saving db");
				rb_ipod_db_save_async (priv->ipod_db);
			} else {
				rb_debug ("playcount didn't change");
			}
			break;
		}
		case RHYTHMDB_PROP_LAST_PLAYED: {
			Itdb_Track *track;
			gulong old_lastplay;
			gulong new_lastplay;

			old_lastplay = g_value_get_ulong (&change->old);
			new_lastplay = g_value_get_ulong (&change->new);
			if (old_lastplay != new_lastplay) {
				track = g_hash_table_lookup (priv->entry_map,
							     entry);
				track->time_played = new_lastplay;
				rb_debug ("last play time changed, saving db");
				rb_ipod_db_save_async (priv->ipod_db);
			} else {
				rb_debug ("last play time didn't change");
			}
			break;
		}
		default:
			rb_debug ("Ignoring property %d", change->prop);
			break;
		}
	}
}

static gboolean
load_ipod_db_idle_cb (RBiPodSource *source)
{
	RhythmDB *db;
 	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	GDK_THREADS_ENTER ();

	db = get_db_for_source (source);

	g_assert (db != NULL);
 	for (it = rb_ipod_db_get_tracks (priv->ipod_db);
	     it != NULL;
	     it = it->next) {
		add_ipod_song_to_db (source, db, (Itdb_Track *)it->data);
	}

	load_ipod_playlists (source);
	send_offline_plays_notification (source);

	g_signal_connect_object(G_OBJECT(db), "entry-changed",
				G_CALLBACK (rb_ipod_source_entry_changed_cb),
				source, 0);

	g_object_unref (db);

	GDK_THREADS_LEAVE ();
	priv->load_idle_id = 0;
	return FALSE;
}

static void
rb_ipod_load_songs (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GMount *mount;

	g_object_get (source, "mount", &mount, NULL);
 	priv->ipod_db = rb_ipod_db_new (mount);
	priv->entry_map = g_hash_table_new (g_direct_hash, g_direct_equal);

	if ((priv->ipod_db != NULL) && (priv->entry_map != NULL)) {
		const char *name;
		name = rb_ipod_db_get_ipod_name (priv->ipod_db);
		if (name) {
			g_object_set (RB_SOURCE (source),
				      "name", name,
				      NULL);
		}
                g_signal_connect (G_OBJECT (source), "notify::name",
		  	          (GCallback)rb_ipod_source_name_changed_cb,
                                  NULL);
		priv->load_idle_id = g_idle_add ((GSourceFunc)load_ipod_db_idle_cb, source);
	}
	g_object_unref (mount);
}

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));
	actions = g_list_prepend (actions, g_strdup ("MediaPlayerSourceSync"));

	return actions;
}

static char *
impl_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static char *
impl_get_paned_key (RBBrowserSource *source)
{
	return g_strdup (CONF_STATE_PANED_POSITION);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/iPodSourcePopup");
	return TRUE;
}

typedef struct {
	RBMediaPlayerSource *source;
	RBMediaPlayerSourceDeleteCallback callback;
	gpointer callback_data;
	GDestroyNotify destroy_data;
	GList *files;
} DeleteFileData;

static gboolean
delete_done_cb (DeleteFileData *data)
{
	if (data->callback) {
		data->callback (data->source, data->callback_data);
	}
	if (data->destroy_data) {
		data->destroy_data (data->callback_data);
	}
	g_object_unref (data->source);
	rb_list_deep_free (data->files);
	return FALSE;
}

static gpointer
delete_thread (DeleteFileData *data)
{
	GList *i;
	rb_debug ("deleting %d files", g_list_length (data->files));
	for (i = data->files; i != NULL; i = i->next) {
		g_unlink ((const char *)i->data);
	}
	rb_debug ("done deleting %d files", g_list_length (data->files));
	g_idle_add ((GSourceFunc) delete_done_cb, data);
	return NULL;
}

static void
impl_delete_entries (RBMediaPlayerSource *source, GList *entries, RBMediaPlayerSourceDeleteCallback callback, gpointer cb_data, GDestroyNotify destroy_data)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDB *db = get_db_for_source ((RBiPodSource *)source);
	GList *i;
	GList *filenames = NULL;
	DeleteFileData *data = g_new0 (DeleteFileData, 1);

	for (i = entries; i != NULL; i = i->next) {
		const char *uri;
		char *filename;
		Itdb_Track *track;
		RhythmDBEntry *entry;

		entry = i->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		track = g_hash_table_lookup (priv->entry_map, entry);
		if (track == NULL) {
			g_warning ("Couldn't find track on ipod! (%s)", uri);
			continue;
		}

		rb_ipod_db_remove_track (priv->ipod_db, track);
		g_hash_table_remove (priv->entry_map, entry);
		filename = g_filename_from_uri (uri, NULL, NULL);

		if (filename != NULL) {
			filenames = g_list_prepend (filenames, filename);
		}
		rhythmdb_entry_delete (db, entry);
	}

	rhythmdb_commit (db);
	g_object_unref (db);

	data->source = g_object_ref (source);
	data->callback = callback;
	data->callback_data = cb_data;
	data->destroy_data = destroy_data;
	data->files = filenames;

	g_thread_create ((GThreadFunc) delete_thread, data, FALSE, NULL);
}

static void
impl_delete (RBSource *source)
{
	GList *sel;
	RBEntryView *songs;

	songs = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (songs);
	impl_delete_entries (RB_MEDIA_PLAYER_SOURCE (source), sel, NULL, NULL, NULL);
	rb_list_destroy_free (sel, (GDestroyNotify) rhythmdb_entry_unref);
}

static void
impl_add_playlist (RBMediaPlayerSource *source,
		   char *name,
		   GList *entries)	/* GList of RhythmDBEntry * on the device to go into the playlist */
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RBIpodStaticPlaylistSource *playlist_source;
	Itdb_Playlist *ipod_playlist;
	GList *iter;

	ipod_playlist = itdb_playlist_new (name, FALSE);
	rb_ipod_db_add_playlist (priv->ipod_db, ipod_playlist);
	playlist_source = add_rb_playlist (RB_IPOD_SOURCE (source), ipod_playlist);

	for (iter = entries; iter != NULL; iter = iter->next) {
		rb_static_playlist_source_add_entry (RB_STATIC_PLAYLIST_SOURCE (playlist_source), iter->data, -1);
	}
}

static void
impl_remove_playlists (RBMediaPlayerSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GList *playlists;
	GList *p;

	playlists = rb_ipod_db_get_playlists (priv->ipod_db);

	for (p = playlists; p != NULL; p = p->next) {
		Itdb_Playlist *playlist = (Itdb_Playlist *)p->data;
		/* XXX might need to exclude more playlists here.. */
		if (!itdb_playlist_is_mpl (playlist) &&
		    !itdb_playlist_is_podcasts (playlist) &&
		    !playlist->is_spl) {

			/* destroy the playlist source */
			RBSource *rb_playlist = RB_SOURCE (playlist->userdata);
			rb_source_delete_thyself (rb_playlist);

			/* remove playlist from ipod */
			rb_ipod_db_remove_playlist (priv->ipod_db, playlist);
		}
	}

	g_list_free (playlists);
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	const char *uri;
	char *dest;
	const char *mount_path;

	if (priv->ipod_db == NULL) {
		return NULL;
	}

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	mount_path = rb_ipod_db_get_mount_path (priv->ipod_db);
	dest = ipod_get_filename_for_uri (mount_path,  uri,
					  mimetype, extension);
	if (dest != NULL) {
		char *dest_uri;

		dest_uri = g_filename_to_uri (dest, NULL, NULL);
		g_free (dest);
		return dest_uri;
	}

	return NULL;
}

static void
artwork_notify_cb (RhythmDB *db,
		   RhythmDBEntry *entry,
		   const gchar *property_name,
		   const GValue *metadata,
		   RBiPodSource *isource)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (isource);
	Itdb_Track *song;
	GdkPixbuf *pixbuf;

	g_return_if_fail (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF));
	pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

	song = g_hash_table_lookup (priv->artwork_request_map, entry);
	if (song == NULL)
		return;

	rb_ipod_db_set_thumbnail (priv->ipod_db, song, pixbuf);
	g_hash_table_remove (priv->artwork_request_map, entry);
}

static gboolean
rb_add_artwork_whole_album_cb (GtkTreeModel *query_model,
			       GtkTreePath *path,
			       GtkTreeIter *iter,
			       RBiPodSongArtworkAddData *artwork_data)
{
	RhythmDBEntry *entry;
	Itdb_Track *song;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), iter);

	song = g_hash_table_lookup (artwork_data->priv->entry_map, entry);
	rhythmdb_entry_unref (entry);
	g_return_val_if_fail (song != NULL, FALSE);

	if (song->has_artwork == 0x01) {
		return FALSE;
	}

	rb_ipod_db_set_thumbnail (artwork_data->priv->ipod_db, song, artwork_data->pixbuf);

	return FALSE;
}

static gboolean
rb_ipod_song_artwork_add_cb (RhythmDB *db,
			     RhythmDBEntry *entry,
			     const gchar *property_name,
			     const GValue *metadata,
			     RBiPodSource *isource)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (isource);
	Itdb_Device *device;
	Itdb_Track *song;
	GdkPixbuf *pixbuf;
	GtkTreeModel *query_model;
	RBiPodSongArtworkAddData artwork_data;
        RhythmDBEntryType *entry_type;

	if (metadata == NULL) {
		return FALSE;
        }

        if (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF) == FALSE) {
		return FALSE;
	}

	song = g_hash_table_lookup (priv->entry_map, entry);
        if (song == NULL) {
                return FALSE;
        }

	device = rb_ipod_db_get_device (priv->ipod_db);
	if (device == NULL || itdb_device_supports_artwork (device) == FALSE) {
		return FALSE;
	}

	if (song->album == NULL || song->artist == NULL) {
		return FALSE;
	}

        g_object_get (isource, "entry-type", &entry_type, NULL);

	pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));

	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
                                RHYTHMDB_QUERY_PROP_EQUALS,
                                RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ARTIST, song->artist,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, song->album,
				RHYTHMDB_QUERY_END);

	artwork_data.priv = priv;
	artwork_data.pixbuf = pixbuf;

	gtk_tree_model_foreach (query_model,
				(GtkTreeModelForeachFunc) rb_add_artwork_whole_album_cb,
				&artwork_data);
        g_object_unref (entry_type);
	g_object_unref(query_model);
	return FALSE;
}

static void
request_artwork (RBiPodSource *isource,
		 RhythmDBEntry *entry,
		 RhythmDB *db,
		 Itdb_Track *song)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (isource);
	GValue *metadata;

	if (priv->artwork_request_map == NULL) {
		priv->artwork_request_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	}

	g_hash_table_insert (priv->artwork_request_map, entry, song);

	if (priv->artwork_notify_id == 0) {
		priv->artwork_notify_id = g_signal_connect_object (db, "entry-extra-metadata-notify::rb:coverArt",
								   (GCallback)artwork_notify_cb, isource, 0);
	}

	metadata = rhythmdb_entry_request_extra_metadata (db, entry, "rb:coverArt");
	if (metadata) {
		artwork_notify_cb (db, entry, "rb:coverArt", metadata, isource);
	}
}

Itdb_Playlist *
rb_ipod_source_get_playlist (RBiPodSource *source,
			     gchar *name)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	Itdb_Playlist *ipod_playlist;

	ipod_playlist = rb_ipod_db_get_playlist_by_name (priv->ipod_db, name);

	/* Playlist doesn't exist on the iPod, create it */
	if (ipod_playlist == NULL) {
		ipod_playlist = itdb_playlist_new (name, FALSE);
		rb_ipod_db_add_playlist (priv->ipod_db, ipod_playlist);
		add_rb_playlist (source, ipod_playlist);
	}

	return ipod_playlist;
}

static void
add_to_podcasts (RBiPodSource *source, Itdb_Track *song)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	gchar *filename;
	const gchar *mount_path;

        /* Set various flags indicating the Itdb_Track is a podcast */
        song->skip_when_shuffling = 0x01;
        song->remember_playback_position = 0x01;
        song->mark_unplayed = 0x02;
        song->flag4 = 0x03;

	if (priv->podcast_pl == NULL) {
		/* No Podcast playlist on the iPod, create a new one */
		Itdb_Playlist *ipod_playlist;
		ipod_playlist = itdb_playlist_new (_("Podcasts"), FALSE);
		itdb_playlist_set_podcasts (ipod_playlist);
		rb_ipod_db_add_playlist (priv->ipod_db, ipod_playlist);
		add_rb_playlist (source, ipod_playlist);
	}

	mount_path = rb_ipod_db_get_mount_path (priv->ipod_db);
  	filename = ipod_path_to_uri (mount_path, song->ipod_path);
 	rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (priv->podcast_pl), filename, -1);
	g_free (filename);
}

static gboolean
impl_track_added (RBRemovableMediaSource *source,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 filesize,
		  const char *mimetype)
{
	RBiPodSource *isource = RB_IPOD_SOURCE (source);
	RhythmDB *db;
	Itdb_Track *song;

	db = get_db_for_source (isource);

	song = create_ipod_song_from_entry (entry, filesize, mimetype);
	if (song != NULL) {
		RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
		char *filename;
		const char *mount_path;
		Itdb_Device *device;

		filename = g_filename_from_uri (dest, NULL, NULL);
		mount_path = rb_ipod_db_get_mount_path (priv->ipod_db);
		song->ipod_path = ipod_path_from_unix_path (mount_path,
							    filename);
		g_free (filename);

		if (song->mediatype == ITDB_MEDIATYPE_PODCAST) {
			add_to_podcasts (isource, song);
		}
		device = rb_ipod_db_get_device (priv->ipod_db);
		if (device && itdb_device_supports_artwork (device)) {
			request_artwork (isource, entry, db, song);
		}
		add_ipod_song_to_db (isource, db, song);
		rb_ipod_db_add_track (priv->ipod_db, song);
	}

	g_object_unref (db);

	return FALSE;
}

/* Generation of the filename for the ipod */

#define IPOD_MAX_PATH_LEN 56

static gboolean
test_dir_on_ipod (const char *mountpoint, const char *dirname)
{
	char *fullpath;
	gboolean result;

	fullpath  = g_build_filename (mountpoint, dirname, NULL);
	result = g_file_test (fullpath, G_FILE_TEST_IS_DIR);
	g_free (fullpath);

	return result;
}

static int
ipod_mkdir_with_parents (const char *mountpoint, const char *dirname)
{
	char *fullpath;
	int result;

	fullpath  = g_build_filename (mountpoint, dirname, NULL);
	result = g_mkdir_with_parents (fullpath, 0770);
	g_free (fullpath);

	return result;
}

static gchar *
build_ipod_dir_name (const char *mountpoint)
{
	/* FAT sucks, filename can be lowercase or uppercase, and if we try to
	 * open the wrong one, we lose :-/
	 */
	char *dirname;
	char *relpath;
	char *ctrl_path, *ctrl_dir;
	gint32 suffix;

	/* Get the control directory first */
	ctrl_path = itdb_get_control_dir (mountpoint);
	if (ctrl_path == NULL) {
		g_debug ("Couldn't find control directory for iPod at '%s'", mountpoint);
		return NULL;
	}
	ctrl_dir = g_path_get_basename (ctrl_path);
	if (ctrl_dir == NULL || *ctrl_dir == '.') {
		g_free (ctrl_dir);
		g_debug ("Couldn't find control directory for iPod at '%s' (got full path '%s'", mountpoint, ctrl_path);
		g_free (ctrl_path);
		return NULL;
	}
	g_free (ctrl_path);

	suffix = g_random_int_range (0, 49);
	dirname = g_strdup_printf ("F%02d", suffix);
	relpath = g_build_filename (G_DIR_SEPARATOR_S, ctrl_dir,
				    "Music", dirname, NULL);
	g_free (dirname);

	if (test_dir_on_ipod (mountpoint, relpath) != FALSE) {
		g_free (ctrl_dir);
		return relpath;
	}

	g_free (relpath);
	dirname = g_strdup_printf ("f%02d", suffix);
	relpath = g_build_filename (G_DIR_SEPARATOR_S, ctrl_dir,
				    "Music", dirname, NULL);
	g_free (dirname);
	g_free (ctrl_dir);

	if (test_dir_on_ipod (mountpoint, relpath) != FALSE) {
		return relpath;
	}

	if (ipod_mkdir_with_parents (mountpoint, relpath) == 0) {
		return relpath;
	}

	g_free (relpath);
	return NULL;
}

static gchar *
get_ipod_filename (const char *mount_point, const char *filename)
{
	char *dirname;
	char *result;
	char *tmp;

	dirname = build_ipod_dir_name (mount_point);
	if (dirname == NULL) {
		return NULL;
	}
	result = g_build_filename (dirname, filename, NULL);
	g_free (dirname);

	if (strlen (result) >= IPOD_MAX_PATH_LEN) {
		char *ext, *suffix;

		ext = strrchr (result, '.');
		if (ext == NULL) {
			suffix = result + IPOD_MAX_PATH_LEN - 4;
			result [IPOD_MAX_PATH_LEN - 1] = '\0';
		} else {
			suffix = result + IPOD_MAX_PATH_LEN - 4 - strlen(ext);
			memmove (&result[IPOD_MAX_PATH_LEN - strlen (ext) - 1] ,
				 ext, strlen (ext) + 1);
		}

		/* Add suffix to reduce the chance of a name collision with truncated name */
		suffix[0] = '~';
		suffix[1] = 'A' + g_random_int_range (0, 26);
		suffix[2] = 'A' + g_random_int_range (0, 26);
	}

	tmp = g_build_filename (mount_point, result, NULL);
	g_free (result);
	return tmp;
}

#define MAX_TRIES 5

/* Strips non UTF8 characters from a string replacing them with _ */
static gchar *
utf8_to_ascii (const gchar *utf8)
{
	GString *string;
	const guchar *it = (const guchar *)utf8;

	string = g_string_new ("");
	while ((it != NULL) && (*it != '\0')) {
		/* Do we have a 7 bit char ? */
		if (*it < 0x80) {
			g_string_append_c (string, *it);
		} else {
			g_string_append_c (string, '_');
		}
		it = (const guchar *)g_utf8_next_char (it);
	}

	return g_string_free (string, FALSE);
}

static gchar *
generate_ipod_filename (const gchar *mount_point, const gchar *filename)
{
	gchar *ipod_filename = NULL;
	gchar *pc_filename;
	gchar *tmp;
	gint tries = 0;

	/* First, we need a valid UTF-8 filename, strip all non-UTF-8 chars */
	tmp = rb_make_valid_utf8 (filename, '_');
	/* The iPod doesn't seem to recognize non-ascii chars in filenames,
	 * so we strip them
	 */
	pc_filename = utf8_to_ascii (tmp);
	g_free (tmp);

	g_assert (g_utf8_validate (pc_filename, -1, NULL));
	/* Now we have a valid UTF-8 filename, try to find out where to put
	 * it on the iPod
	 */
	do {
		g_free (ipod_filename);
		ipod_filename = get_ipod_filename (mount_point, pc_filename);
		tries++;
		if (tries > MAX_TRIES) {
			break;
		}
	} while ((ipod_filename == NULL)
		 || (g_file_test (ipod_filename, G_FILE_TEST_EXISTS)));

	g_free (pc_filename);

	if (tries > MAX_TRIES) {
		/* FIXME: should create a unique filename */
		return NULL;
	} else {
		return ipod_filename;
	}
}

static gchar *
ipod_get_filename_for_uri (const gchar *mount_point,
			   const gchar *uri_str,
			   const gchar *mimetype,
			   const gchar *extension)
{
	gchar *escaped;
	gchar *filename;
	gchar *result;

	escaped = rb_uri_get_short_path_name (uri_str);
	if (escaped == NULL) {
		return NULL;
	}
	filename = g_uri_unescape_string (escaped, NULL);
	g_free (escaped);
	if (filename == NULL) {
		return NULL;
	}

	/* replace the old extension or append it */
	/* FIXME: we really need a mapping (audio/mpeg->mp3) and not
	 * just rely on the user's audio profile havign the "right" one */
	escaped = g_utf8_strrchr (filename, -1, '.');
	if (escaped != NULL) {
		*escaped = 0;
	}

	if (extension != NULL) {
		escaped = g_strdup_printf ("%s.%s", filename, extension);
		g_free (filename);
	} else {
		escaped = filename;
	}

	result = generate_ipod_filename (mount_point, escaped);
	g_free (escaped);

	return result;
}

/* End of generation of the filename on the iPod */

static gchar *
ipod_path_from_unix_path (const gchar *mount_point, const gchar *unix_path)
{
	gchar *ipod_path;

	g_assert (g_utf8_validate (unix_path, -1, NULL));

	if (!g_str_has_prefix (unix_path, mount_point)) {
		return NULL;
	}

	ipod_path = g_strdup (unix_path + strlen (mount_point));
	if (*ipod_path != G_DIR_SEPARATOR) {
		gchar *tmp;
		tmp = g_strdup_printf ("/%s", ipod_path);
		g_free (ipod_path);
		ipod_path = tmp;
	}

	/* Make sure the filename doesn't contain any ':' */
	g_strdelimit (ipod_path, ":", ';');

	/* Convert path to a Mac path where the dir separator is ':' */
	itdb_filename_fs2ipod (ipod_path);

	return ipod_path;
}

static void
impl_delete_thyself (RBSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	GList *p;

        if (priv->ipod_db == NULL) {
            RB_SOURCE_CLASS (rb_ipod_source_parent_class)->impl_delete_thyself (source);
            return;
        }

	db = get_db_for_source (RB_IPOD_SOURCE (source));
	g_signal_handlers_disconnect_by_func (db,
					      G_CALLBACK (rb_ipod_song_artwork_add_cb),
					      RB_IPOD_SOURCE (source));
	g_object_unref (db);

	for (p = rb_ipod_db_get_playlists (priv->ipod_db);
	     p != NULL;
	     p = p->next) {
		Itdb_Playlist *playlist = (Itdb_Playlist *)p->data;
		if (!itdb_playlist_is_mpl (playlist) && !playlist->is_spl) {
			RBSource *rb_playlist;
			RhythmDBQueryModel *model;

			rb_playlist = RB_SOURCE (playlist->userdata);
			g_object_get (G_OBJECT (rb_playlist),
				      "base-query-model", &model, NULL);

			/* remove these to ensure they aren't called during source deletion */
			g_signal_handlers_disconnect_by_func (model,
							      G_CALLBACK (playlist_track_added),
							      rb_playlist);
			g_signal_handlers_disconnect_by_func (model,
							      G_CALLBACK (playlist_track_removed),
							      rb_playlist);

			g_object_unref (model);
			rb_source_delete_thyself (rb_playlist);
		}
	}

	g_object_unref (G_OBJECT (priv->ipod_db));
	priv->ipod_db = NULL;

	RB_SOURCE_CLASS (rb_ipod_source_parent_class)->impl_delete_thyself (source);
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	GList *ret = NULL;

	/* FIXME: we should really query MPID for this */
	ret = g_list_prepend (ret, g_strdup ("audio/aac"));
	ret = g_list_prepend (ret, g_strdup ("audio/mpeg"));

	return ret;
}

Itdb_Playlist *
rb_ipod_source_new_playlist (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	Itdb_Playlist *ipod_playlist;

	if (priv->ipod_db == NULL) {
		rb_debug ("can't create new ipod playlist with no ipod db");
		return NULL;
	}

	ipod_playlist = itdb_playlist_new (_("New playlist"), FALSE);
	rb_ipod_db_add_playlist (priv->ipod_db, ipod_playlist);
	add_rb_playlist (source, ipod_playlist);
	return ipod_playlist;
}

void
rb_ipod_source_remove_playlist (RBiPodSource *ipod_source,
				RBSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (ipod_source);
	RBIpodStaticPlaylistSource *playlist_source = RB_IPOD_STATIC_PLAYLIST_SOURCE (source);

	rb_source_delete_thyself (source);
	rb_ipod_db_remove_playlist (priv->ipod_db, rb_ipod_static_playlist_source_get_itdb_playlist (playlist_source));
}

static gboolean
ipod_name_changed_cb (GtkWidget     *widget,
 		      GdkEventFocus *event,
		      gpointer       user_data)
{
	g_object_set (RB_SOURCE (user_data), "name",
		      gtk_entry_get_text (GTK_ENTRY (widget)),
		      NULL);
	return FALSE;
}


static void
impl_show_properties (RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GHashTableIter iter;
	int num_podcasts;
	gpointer key, value;
	GtkBuilder *builder;
	GtkWidget *widget;
	char *text;
	const gchar *mp;
	char *builder_file;
	Itdb_Device *ipod_dev;
	RBPlugin *plugin;
	GList *output_formats;
	GList *t;
	GString *str;

	/* probably should display an error on the basic page in most of these error cases.. */

	if (priv->ipod_db == NULL) {
		rb_debug ("can't show ipod properties with no ipod db");
		return;
	}

	g_object_get (source, "plugin", &plugin, NULL);
	builder_file = rb_plugin_find_file (plugin, "ipod-info.ui");
	g_object_unref (plugin);

	if (builder_file == NULL) {
		g_warning ("Couldn't find ipod-info.ui");
		return;
	}

	builder = rb_builder_load (builder_file, NULL);
	g_free (builder_file);

 	if (builder == NULL) {
 		rb_debug ("Couldn't load ipod-info.ui");
 		return;
 	}

	ipod_dev = rb_ipod_db_get_device (priv->ipod_db);

	/* basic tab stuff */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-basic-info"));
	gtk_box_pack_start (GTK_BOX (info_box), widget, TRUE, TRUE, 0);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-name-entry"));
	gtk_entry_set_text (GTK_ENTRY (widget), rb_ipod_db_get_ipod_name (priv->ipod_db));
	g_signal_connect (widget, "focus-out-event",
 			  (GCallback)ipod_name_changed_cb, source);

	num_podcasts = 0;
	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		Itdb_Track *track = value;
		if (track->mediatype == ITDB_MEDIATYPE_PODCAST) {
			num_podcasts++;
		}
	}

	/* TODO these need to be updated as entries are added and removed. */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-num-tracks"));
	text = g_strdup_printf ("%d", g_hash_table_size (priv->entry_map) - num_podcasts);
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-num-podcasts"));
	text = g_strdup_printf ("%d", num_podcasts);
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	/* TODO probably needs to ignore the master playlist? */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-num-playlists"));
	text = g_strdup_printf ("%d", g_list_length (rb_ipod_db_get_playlists (priv->ipod_db)));
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	/* 'advanced' tab stuff */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipod-advanced-tab"));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), widget, gtk_label_new (_("Advanced")));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-mount-point-value"));
	mp = rb_ipod_db_get_mount_path (priv->ipod_db);
	gtk_label_set_text (GTK_LABEL (widget), mp);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-device-node-value"));
	text = rb_ipod_helpers_get_device (RB_SOURCE(source));
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-ipod-model-value"));
	gtk_label_set_text (GTK_LABEL (widget), itdb_device_get_sysinfo (ipod_dev, "ModelNumStr"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-database-version-value"));
	text = g_strdup_printf ("%u", rb_ipod_db_get_database_version (priv->ipod_db));
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	g_object_get (priv->device_info, "serial", &text, NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-serial-number-value"));
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-firmware-version-value"));
	gtk_label_set_text (GTK_LABEL (widget), itdb_device_get_sysinfo (ipod_dev, "VisibleBuildID"));

	str = g_string_new ("");
	output_formats = rb_removable_media_source_get_format_descriptions (RB_REMOVABLE_MEDIA_SOURCE (source));
	for (t = output_formats; t != NULL; t = t->next) {
		if (t != output_formats) {
			g_string_append (str, "\n");
		}
		g_string_append (str, t->data);
	}
	rb_list_deep_free (output_formats);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label-audio-formats-value"));
	gtk_label_set_text (GTK_LABEL (widget), str->str);
	g_string_free (str, TRUE);

	g_object_unref (builder);
}

static const gchar *
get_mount_point	(RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	return rb_ipod_db_get_mount_path (priv->ipod_db);
}

static guint64
impl_get_capacity (RBMediaPlayerSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	if (priv->ipod_db) {
		return rb_ipod_helpers_get_capacity (get_mount_point (RB_IPOD_SOURCE (source)));
	} else {
		return 0;
	}
}

static guint64
impl_get_free_space (RBMediaPlayerSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	if (priv->ipod_db) {
		return rb_ipod_helpers_get_free_space (get_mount_point (RB_IPOD_SOURCE (source)));
	} else {
		return 0;
	}
}

static void
impl_get_entries (RBMediaPlayerSource *source, const char *category, GHashTable *map)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GHashTableIter iter;
	gpointer key, value;
	Itdb_Mediatype media_type;

	/* map the sync category to an itdb media type */
	if (g_str_equal (category, SYNC_CATEGORY_MUSIC)) {
		media_type = ITDB_MEDIATYPE_AUDIO;
	} else if (g_str_equal (category, SYNC_CATEGORY_PODCAST)) {
		media_type = ITDB_MEDIATYPE_PODCAST;
	} else {
		g_warning ("unsupported ipod sync category %s", category);
		return;
	}

	/* extract all entries matching the media type for the sync category */
	g_hash_table_iter_init (&iter, priv->entry_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		Itdb_Track *track = value;
		if (track->mediatype == media_type) {
			RhythmDBEntry *entry = key;
			_rb_media_player_source_add_to_map (map, entry);
		}
	}
}
