/*
 *  arch-tag: Implementation of ipod source object
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus.h>
#endif
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <gpod/itdb.h>

#include "eel-gconf-extensions.h"
#include "rb-ipod-source.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-plugin.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rb-cut-and-paste-code.h"

#ifdef IPOD_SUPPORT
#define PHONE_VENDOR_ID 0x22b8
#define PHONE_PRODUCT_ID 0x4810
#endif

static GObject *rb_ipod_source_constructor (GType type,
					    guint n_construct_properties,
					    GObjectConstructParam *construct_properties);
static void rb_ipod_source_dispose (GObject *object);

static GObject *rb_ipod_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties);
static void rb_ipod_source_dispose (GObject *object);

static gboolean impl_show_popup (RBSource *source);
static void impl_move_to_trash (RBSource *asource);
static void rb_ipod_load_songs (RBiPodSource *source);
static gchar *rb_ipod_get_mount_path (GnomeVFSVolume *volume);
static void impl_delete_thyself (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);
#ifdef HAVE_HAL
static gboolean hal_udi_is_ipod (const char *udi);
#endif

#ifdef ENABLE_IPOD_WRITING
static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
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
#endif
static void itdb_schedule_save (Itdb_iTunesDB *db);

typedef struct
{
	Itdb_iTunesDB *ipod_db;
	gchar *ipod_mount_path;
	GHashTable *entry_map;

	GList *playlists;

	guint load_idle_id;
} RBiPodSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBiPodSource,
		      rb_ipod_source,
		      RB_TYPE_REMOVABLE_MEDIA_SOURCE)

#define IPOD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourcePrivate))

static void
rb_ipod_source_class_init (RBiPodSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);

	object_class->constructor = rb_ipod_source_constructor;
	object_class->dispose = rb_ipod_source_dispose;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
   	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
   	source_class->impl_move_to_trash = impl_move_to_trash;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_ui_actions = impl_get_ui_actions;

#ifdef ENABLE_IPOD_WRITING
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;
	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;
#else
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	rms_class->impl_track_added = NULL;
#endif

	g_type_class_add_private (klass, sizeof (RBiPodSourcePrivate));
}

static void
rb_ipod_source_set_ipod_name (RBiPodSource *source, const char *name)
{
	Itdb_Playlist *mpl;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	mpl = itdb_playlist_mpl (priv->ipod_db);
	if (mpl != NULL) {
		if (mpl->name != NULL) {
			rb_debug ("Renaming iPod from %s to %s", mpl->name, name);
			if (strcmp (mpl->name, name) == 0) {
				rb_debug ("iPod is already named %s", name);
				return;
			}
		}
		g_free (mpl->name);
		mpl->name = g_strdup (name);
		itdb_schedule_save (priv->ipod_db);
	} else {
		g_warning ("iPod's master playlist is missing");
	}
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
	g_signal_connect (G_OBJECT (source), "notify::name",
			  (GCallback)rb_ipod_source_name_changed_cb, NULL);
}

static GObject *
rb_ipod_source_constructor (GType type, guint n_construct_properties,
			    GObjectConstructParam *construct_properties)
{
	RBiPodSource *source;
	RBEntryView *songs;
	RBiPodSourcePrivate *priv;

	source = RB_IPOD_SOURCE (G_OBJECT_CLASS (rb_ipod_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));
	priv = IPOD_SOURCE_GET_PRIVATE (source);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	rb_ipod_load_songs (source);

	return G_OBJECT (source);
}

static void
rb_ipod_source_dispose (GObject *object)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (object);

 	if (priv->ipod_db != NULL) {
 		itdb_free (priv->ipod_db);
 		priv->ipod_db = NULL;
  	}

	if (priv->ipod_mount_path) {
		g_free (priv->ipod_mount_path);
		priv->ipod_mount_path = NULL;
	}

	if (priv->entry_map) {
		g_hash_table_destroy (priv->entry_map);
		priv->entry_map = NULL;
 	}

	if (priv->load_idle_id != 0) {
		g_source_remove (priv->load_idle_id);
		priv->load_idle_id = 0;
	}

	G_OBJECT_CLASS (rb_ipod_source_parent_class)->dispose (object);
}

RBRemovableMediaSource *
rb_ipod_source_new (RBShell *shell,
                    GnomeVFSVolume *volume)
{
	RBiPodSource *source;
	RhythmDBEntryType entry_type;
	RhythmDB *db;
	char *name;
	char *path;

	g_assert (rb_ipod_is_volume_ipod (volume));

	g_object_get (shell, "db", &db, NULL);
	path = gnome_vfs_volume_get_device_path (volume);
	name = g_strdup_printf ("ipod: %s", path);
	entry_type =  rhythmdb_entry_register_type (db, name);
	entry_type->save_to_disk = FALSE;
	entry_type->category = RHYTHMDB_ENTRY_NORMAL;
	g_object_unref (db);
	g_free (name);
	g_free (path);

	source = RB_IPOD_SOURCE (g_object_new (RB_TYPE_IPOD_SOURCE,
					  "entry-type", entry_type,
					  "volume", volume,
					  "shell", shell,
					  "sourcelist-group", RB_SOURCELIST_GROUP_REMOVABLE,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
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
add_rb_playlist (RBiPodSource *source, Itdb_Playlist *playlist)
{
	RBShell *shell;
	RBSource *playlist_source;
	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;

  	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);

	playlist_source = rb_static_playlist_source_new (shell,
							 playlist->name,
							 FALSE,
							 entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	for (it = playlist->members; it != NULL; it = it->next) {
		Itdb_Track *song;
		char *filename;

		song = (Itdb_Track *)it->data;
 		filename = ipod_path_to_uri (priv->ipod_mount_path,
 					    song->ipod_path);
		rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (playlist_source),
							filename, -1);
		g_free (filename);
	}

	priv->playlists = g_list_prepend (priv->playlists, playlist_source);

	rb_shell_append_source (shell, playlist_source, RB_SOURCE (source));
	g_object_unref (shell);
}

static void
load_ipod_playlists (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GList *it;

	for (it = priv->ipod_db->playlists; it != NULL; it = it->next) {
		Itdb_Playlist *playlist;

		playlist = (Itdb_Playlist *)it->data;
		if (itdb_playlist_is_mpl (playlist)) {
			continue;
		}
		if (playlist->is_spl) {
			continue;
		}

		add_rb_playlist (source, playlist);
	}

}

#ifdef ENABLE_IPOD_WRITING
static Itdb_Track *
create_ipod_song_from_entry (RhythmDBEntry *entry, const char *mimetype)
{
	Itdb_Track *track;

	track = itdb_track_new ();

	track->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	track->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	track->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	track->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	track->filetype = g_strdup (mimetype);
	track->size = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
	track->tracklen = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
	track->tracklen *= 1000;
	track->cd_nr = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
	track->track_nr = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	track->bitrate = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE);
	track->year = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);
	track->time_added = itdb_time_get_mac_time ();
	track->time_played = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_LAST_PLAYED);
	track->time_played = itdb_time_host_to_mac (track->time_played);
	track->rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);
	track->app_rating = track->rating;
	track->playcount = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);

	return track;
}
#endif

static void
add_ipod_song_to_db (RBiPodSource *source, RhythmDB *db, Itdb_Track *song)
{
	RhythmDBEntry *entry;
	RhythmDBEntryType entry_type;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	char *pc_path;

	/* Set URI */
	g_object_get (source, "entry-type", &entry_type,
		      NULL);

	pc_path = ipod_path_to_uri (priv->ipod_mount_path,
				    song->ipod_path);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type,
				    pc_path);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	if (entry == NULL) {
		rb_debug ("cannot create entry %s", pc_path);
		g_free (pc_path);
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
		g_value_set_double (&value, song->rating/20.0);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
					       RHYTHMDB_PROP_RATING,
					       &value);
		g_value_unset (&value);
	}

	/* Set last played */
	if (song->time_played != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, itdb_time_mac_to_host (song->time_played));
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

	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_ALBUM, song->album);

	entry_set_string_prop (RHYTHMDB (db), entry,
			       RHYTHMDB_PROP_GENRE, song->genre);

	g_hash_table_insert (priv->entry_map, entry, song);

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

static gboolean
load_ipod_db_idle_cb (RBiPodSource *source)
{
	RhythmDB *db;
 	GList *it;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);

	GDK_THREADS_ENTER ();

        db = get_db_for_source (source);

  	g_assert (db != NULL);
 	for (it = priv->ipod_db->tracks; it != NULL; it = it->next) {
		add_ipod_song_to_db (source, db, (Itdb_Track *)it->data);
	}

	load_ipod_playlists (source);

	g_object_unref (db);

	GDK_THREADS_LEAVE ();
	priv->load_idle_id = 0;
	return FALSE;
}

static void
rb_ipod_load_songs (RBiPodSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GnomeVFSVolume *volume;

	g_object_get (source, "volume", &volume, NULL);
	priv->ipod_mount_path = rb_ipod_get_mount_path (volume);

 	priv->ipod_db = itdb_parse (priv->ipod_mount_path, NULL);
	priv->entry_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	if ((priv->ipod_db != NULL) && (priv->entry_map != NULL)) {
		Itdb_Playlist *mpl;

		/* FIXME: we could set a different icon depending on the iPod
		 * model
		 */
		mpl = itdb_playlist_mpl (priv->ipod_db);
		if (mpl && mpl->name) {
			g_object_set (RB_SOURCE (source),
				      "name", mpl->name,
				      NULL);
		}
		priv->load_idle_id = g_idle_add ((GSourceFunc)load_ipod_db_idle_cb, source);
	}

        g_object_unref (volume);
}

static gchar *
rb_ipod_get_mount_path (GnomeVFSVolume *volume)
{
	gchar *path;
	gchar *uri;

	uri = gnome_vfs_volume_get_activation_uri (volume);
	path = g_filename_from_uri (uri, NULL, NULL);
	g_assert (path != NULL);
	g_free (uri);

	return path;
}

static gchar *
rb_ipod_get_itunesdb_path (GnomeVFSVolume *volume)
{
	gchar *mount_point_uri;
	gchar *mount_point;
	gchar *result;

	mount_point_uri = gnome_vfs_volume_get_activation_uri (volume);
	if (mount_point_uri == NULL) {
		return NULL;
	}
	mount_point = g_filename_from_uri (mount_point_uri, NULL, NULL);
	g_free (mount_point_uri);
	if (mount_point == NULL) {
		return NULL;
	}

#ifdef IPOD_SUPPORT
	result = itdb_get_itunesdb_path (mount_point);
#else
	result = g_build_filename (mount_point,
				   "iPod_Control/iTunes/iTunesDB",
				   NULL);
#endif

	g_free (mount_point);
	return result;
}

static gboolean
rb_ipod_volume_has_ipod_db (GnomeVFSVolume *volume)
{
	char *itunesdb_path;
	gboolean result;

	itunesdb_path = rb_ipod_get_itunesdb_path (volume);

	if (itunesdb_path != NULL) {
		result = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);
	} else {
		result = FALSE;
	}
	g_free (itunesdb_path);

	return result;
}

gboolean
rb_ipod_is_volume_ipod (GnomeVFSVolume *volume)
{
#ifdef HAVE_HAL
	gchar *udi;
#endif
	if (gnome_vfs_volume_get_volume_type (volume) != GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		return FALSE;
	}

#ifdef HAVE_HAL
	udi = gnome_vfs_volume_get_hal_udi (volume);
	if (udi != NULL) {
		gboolean result;

		result = hal_udi_is_ipod (udi);
		g_free (udi);
		if (result == FALSE) {
			return FALSE;
		}
	}
#endif

	return rb_ipod_volume_has_ipod_db (volume);
}

#ifdef HAVE_HAL_0_5

static gboolean
hal_udi_is_ipod (const char *udi)
{
	LibHalContext *ctx;
	DBusConnection *conn;
	char *parent_udi;
        char *parent_name;
	gboolean result;
	DBusError error;
	gboolean inited = FALSE;

	result = FALSE;
	dbus_error_init (&error);

	conn = NULL;
        parent_udi = NULL;
        parent_name = NULL;

	ctx = libhal_ctx_new ();
	if (ctx == NULL) {
		/* FIXME: should we return an error somehow so that we can
		 * fall back to a check for iTunesDB presence instead ?
		 */
		rb_debug ("cannot connect to HAL");
		goto end;
	}
	conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (conn == NULL || dbus_error_is_set (&error))
		goto end;

	libhal_ctx_set_dbus_connection (ctx, conn);
	if (!libhal_ctx_init (ctx, &error) || dbus_error_is_set (&error))
		goto end;

	inited = TRUE;
	parent_udi = libhal_device_get_property_string (ctx, udi,
			"info.parent", &error);
	if (parent_udi == NULL || dbus_error_is_set (&error))
		goto end;

	parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"storage.model", &error);
#ifdef IPOD_SUPPORT
	{
		char *spider_udi;
		int vnd_id = 0;
		int product_id = 0;

		spider_udi = g_strdup(parent_udi);
		while (vnd_id == 0 && product_id == 0 && spider_udi != NULL) {
			char *old_udi = spider_udi;
			spider_udi =  libhal_device_get_property_string (ctx, spider_udi,
					"info.parent", &error);
			if (dbus_error_is_set (&error)) {
				dbus_error_free (&error);
				dbus_error_init (&error);
				spider_udi = NULL;
				break;
			}
			g_free(old_udi);

			vnd_id = libhal_device_get_property_int (ctx, spider_udi,
				"usb.vendor_id", &error);
			if (dbus_error_is_set(&error)) {
				dbus_error_free (&error);
				dbus_error_init (&error);
				vnd_id = 0;
			}

			product_id = libhal_device_get_property_int (ctx, spider_udi,
				"usb.product_id", &error);
			if (dbus_error_is_set(&error)) {
				dbus_error_free (&error);
				dbus_error_init (&error);
				product_id = 0;
			}
		}
		g_free (spider_udi);

		if (vnd_id == PHONE_VENDOR_ID && product_id == PHONE_PRODUCT_ID) {
			result = TRUE;
		}
	}
#endif
	if (parent_name == NULL || dbus_error_is_set (&error))
		goto end;

	if (strcmp (parent_name, "iPod") == 0)
		result = TRUE;

end:
	g_free (parent_udi);
	g_free (parent_name);

	if (dbus_error_is_set (&error)) {
		rb_debug ("Error: %s\n", error.message);
		dbus_error_free (&error);
		dbus_error_init (&error);
	}

	if (ctx) {
		if (inited)
			libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free(ctx);
	}

	dbus_error_free (&error);

	return result;
}

#elif HAVE_HAL_0_2

static gboolean
hal_udi_is_ipod (const char *udi)
{
	LibHalContext *ctx;
	char *parent_udi;
        char *parent_name;
	gboolean result;

	result = FALSE;
	ctx = hal_initialize (NULL, FALSE);
	if (ctx == NULL) {
		/* FIXME: should we return an error somehow so that we can
		 * fall back to a check for iTunesDB presence instead ?
		 */
		return FALSE;
	}
	parent_udi = hal_device_get_property_string (ctx, udi,
			"info.parent");
	parent_name = hal_device_get_property_string (ctx, parent_udi,
			"storage.model");
	g_free (parent_udi);

	if (parent_name != NULL && strcmp (parent_name, "iPod") == 0) {
		result = TRUE;
	}

	g_free (parent_name);
	hal_shutdown (ctx);

	return result;
}

#endif

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));

	return actions;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/iPodSourcePopup");
	return TRUE;
}

static void
remove_track_from_db (Itdb_Track *track)
{
	GList *it;

	for (it = track->itdb->playlists; it != NULL; it = it->next) {
		itdb_playlist_remove_track ((Itdb_Playlist *)it->data, track);
	}
	itdb_track_remove (track);
}

static void
impl_move_to_trash (RBSource *asource)
{
	GList *sel, *tem;
	RBEntryView *songs;
	RhythmDB *db;
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (asource);
	RBiPodSource *source = RB_IPOD_SOURCE (asource);

        db = get_db_for_source (source);

	songs = rb_source_get_entry_view (RB_SOURCE (asource));
	sel = rb_entry_view_get_selected_entries (songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		RhythmDBEntry *entry;
		const gchar *uri;
		Itdb_Track *track;

		entry = (RhythmDBEntry *)tem->data;
		uri = rhythmdb_entry_get_string (entry,
						 RHYTHMDB_PROP_LOCATION);
		track = g_hash_table_lookup (priv->entry_map, entry);
		if (track == NULL) {
			g_warning ("Couldn't find track on ipod! (%s)", uri);
			continue;
		}

 		remove_track_from_db (track);
		g_hash_table_remove (priv->entry_map, entry);
		rhythmdb_entry_move_to_trash (db, entry);
		rhythmdb_commit (db);
	}

	if (sel != NULL) {
                itdb_schedule_save (RB_IPOD_SOURCE (asource));
	}

  	g_object_unref (db);

	g_list_free (sel);
}

static void
itdb_schedule_save (Itdb_iTunesDB *db)
{
       /* FIXME: should probably be delayed a bit to avoid doing
        * it after each file when we are copying several files
        * consecutively
        * FIXME: or this function could be called itdb_set_dirty, and we'd
        * have a timeout firing every 5 seconds and saving the db if it's
        * dirty
        */
       itdb_write (db, NULL);
}

#ifdef ENABLE_IPOD_WRITING
static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	const char *uri;
	char *dest;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	dest = ipod_get_filename_for_uri (priv->ipod_mount_path,  uri, mimetype, extension);
	if (dest != NULL) {
		char *dest_uri;

		dest_uri = g_filename_to_uri (dest, NULL, NULL);
		g_free (dest);
		return dest_uri;
	}

	return NULL;
}

static gboolean
impl_track_added (RBRemovableMediaSource *source,
		  RhythmDBEntry *entry,
		  const char *dest,
		  const char *mimetype)
{
	RBiPodSource *isource = RB_IPOD_SOURCE (source);
	RhythmDB *db;
	Itdb_Track *song;

        db = get_db_for_source (isource);

	song = create_ipod_song_from_entry (entry, mimetype);
	if (song != NULL) {
		RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
		char *filename;

		filename = g_filename_from_uri (dest, NULL, NULL);
		song->ipod_path = ipod_path_from_unix_path (priv->ipod_mount_path, filename);
		g_free (filename);
		itdb_track_add (priv->ipod_db, song, -1);
		itdb_playlist_add_track (itdb_playlist_mpl (priv->ipod_db),
					 song, -1);

		add_ipod_song_to_db (isource, db, song);
		itdb_schedule_save (priv->ipod_db);
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
	gint32 suffix;

	suffix = g_random_int_range (0, 100);
	dirname = g_strdup_printf ("F%02d", suffix);
	relpath = g_build_filename (G_DIR_SEPARATOR_S, "iPod_Control",
				    "Music", dirname, NULL);
	g_free (dirname);

	if (test_dir_on_ipod (mountpoint, relpath) != FALSE) {
		return relpath;
	}

	g_free (relpath);
	dirname = g_strdup_printf ("f%02d", g_random_int_range (0, 100));
	relpath = g_build_filename (G_DIR_SEPARATOR_S, "iPod_Control",
				    "Music", dirname, NULL);
	g_free (dirname);

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
		char *ext;

		ext = strrchr (result, '.');
		if (ext == NULL) {
			result [IPOD_MAX_PATH_LEN - 1] = '\0';
		} else {
			memmove (&result[IPOD_MAX_PATH_LEN - strlen (ext) - 1] ,
				 ext, strlen (ext) + 1);
		}
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
	filename = gnome_vfs_unescape_string (escaped, G_DIR_SEPARATOR_S);
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

	escaped = g_strdup_printf ("%s.%s", filename, extension);
	g_free (filename);


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
#endif

static void
impl_delete_thyself (RBSource *source)
{
	RBiPodSourcePrivate *priv = IPOD_SOURCE_GET_PRIVATE (source);
	GList *p;

	for (p = priv->playlists; p != NULL; p = p->next) {
		RBSource *playlist = RB_SOURCE (p->data);
		rb_source_delete_thyself (playlist);
	}
	g_list_free (priv->playlists);
	priv->playlists = NULL;

	itdb_free (priv->ipod_db);
	priv->ipod_db = NULL;

	RB_SOURCE_CLASS (rb_ipod_source_parent_class)->impl_delete_thyself (source);
}

#ifdef ENABLE_IPOD_WRITING

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	GList *ret = NULL;

	/* FIXME: we should really query HAL for this */
	ret = g_list_prepend (ret, g_strdup ("audio/aac"));
	ret = g_list_prepend (ret, g_strdup ("audio/mpeg"));

	return ret;
}

#endif
