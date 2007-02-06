/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of generic audio player source object
 *
 *  Copyright (C) 2004 James Livingston  <jrl@ids.org.au>
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

#define __EXTENSIONS__

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#ifdef HAVE_HAL_0_5
#include <libhal.h>
#include <dbus/dbus.h>
#endif
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <totem-pl-parser.h>

#include "eel-gconf-extensions.h"
#include "rb-static-playlist-source.h"
#include "rb-generic-player-source.h"
#include "rb-removable-media-manager.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"
#include "rb-dialog.h"
#include "rb-plugin.h"

static GObject *rb_generic_player_source_constructor (GType type, guint n_construct_properties,
						      GObjectConstructParam *construct_properties);
static void rb_generic_player_source_dispose (GObject *object);
static void rb_generic_player_source_finalize (GObject *object);

static gboolean rb_generic_player_source_load_playlists (RBGenericPlayerSource *source);
static void rb_generic_player_source_load_songs (RBGenericPlayerSource *source);
static void rb_generic_player_source_get_device_info (RBGenericPlayerSource *source);

static gboolean impl_show_popup (RBSource *source);
static void impl_delete_thyself (RBSource *source);
static gboolean impl_can_move_to_trash (RBSource *source);
static gboolean impl_can_paste (RBSource *source);
static GList* impl_get_mime_types (RBRemovableMediaSource *source);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static gchar *default_get_mount_path (RBGenericPlayerSource *source);
static void default_load_playlists (RBGenericPlayerSource *source);
static char * default_transform_playlist_uri (RBGenericPlayerSource *source,
					      const char *uri);

#if HAVE_HAL_0_5
static LibHalContext *get_hal_context (void);
static void cleanup_hal_context (LibHalContext *ctx);
static char * get_hal_udi_for_player (LibHalContext *ctx, GnomeVFSVolume *volume);
static void free_dbus_error (const char *what, DBusError *error);
#endif

typedef struct
{
	RhythmDB *db;

	gint load_playlists_id;
	GList *playlists;

	char *mount_path;

	/* information derived from gnome-vfs volume */
	gboolean read_only;
	gboolean handles_trash;

	/* information derived from HAL */
	char **audio_folders;
	char **output_mime_types;
	gboolean playlist_format_unknown;
	gboolean playlist_format_pls;
	gboolean playlist_format_m3u;
	char *playlist_path;
	gint folder_depth;

} RBGenericPlayerSourcePrivate;


RB_PLUGIN_DEFINE_TYPE(RBGenericPlayerSource, rb_generic_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define GENERIC_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourcePrivate))

static void
rb_generic_player_source_class_init (RBGenericPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);

	object_class->constructor = rb_generic_player_source_constructor;
	object_class->dispose = rb_generic_player_source_dispose;
	object_class->finalize = rb_generic_player_source_finalize;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_move_to_trash = impl_can_move_to_trash;
	source_class->impl_can_paste = impl_can_paste;

	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;

	klass->impl_get_mount_path = default_get_mount_path;
	klass->impl_load_playlists = default_load_playlists;
	klass->impl_transform_playlist_uri = default_transform_playlist_uri;

	g_type_class_add_private (klass, sizeof (RBGenericPlayerSourcePrivate));
}

static void
rb_generic_player_source_init (RBGenericPlayerSource *source)
{

}

static GObject *
rb_generic_player_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBGenericPlayerSource *source;
	RBGenericPlayerSourcePrivate *priv;
	GnomeVFSVolume *volume;
	RBShell *shell;

	source = RB_GENERIC_PLAYER_SOURCE (G_OBJECT_CLASS (rb_generic_player_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);

	g_object_get (G_OBJECT (shell), "db", &priv->db, NULL);
	g_object_unref (G_OBJECT (shell));

	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	priv->handles_trash = gnome_vfs_volume_handles_trash (volume);
	priv->read_only = gnome_vfs_volume_is_read_only (volume);
	g_object_unref (G_OBJECT (volume));

	priv->folder_depth = -1;	/* 0 is a possible value, I guess */
	priv->playlist_format_unknown = TRUE;

	rb_generic_player_source_load_songs (source);

	priv->load_playlists_id =
		g_idle_add ((GSourceFunc)rb_generic_player_source_load_playlists, source);

	return G_OBJECT (source);
}

static void
rb_generic_player_source_get_device_info (RBGenericPlayerSource *source)
{
#if HAVE_HAL_0_5
	GnomeVFSVolume *volume;
	LibHalContext *ctx = get_hal_context ();
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	if (ctx) {
		gchar *udi;

		g_object_get (G_OBJECT (source), "volume", &volume, NULL);
		udi = get_hal_udi_for_player (ctx, volume);
		g_object_unref (G_OBJECT (volume));

		if (udi) {
			DBusError error;
			char *prop;
			char **proplist;
			int value;

			/* get audio folders */
			dbus_error_init (&error);
			proplist = libhal_device_get_property_strlist (ctx, udi, "portable_audio_player.audio_folders", &error);
			if (proplist) {
				if (!dbus_error_is_set (&error)) {
					char *dbg;

					priv->audio_folders = g_strdupv (proplist);

					dbg = g_strjoinv(", ", priv->audio_folders);
					rb_debug ("got audio player folder list: %s", dbg);
					g_free (dbg);
				}
				libhal_free_string_array (proplist);
			}
			free_dbus_error ("getting audio folder list", &error);

			/* get supported mime-types */
			dbus_error_init (&error);
			proplist = libhal_device_get_property_strlist (ctx, udi, "portable_audio_player.output_formats", &error);
			if (proplist) {
				if (!dbus_error_is_set (&error)) {
					char *dbg;

					priv->output_mime_types = g_strdupv (proplist);

					dbg = g_strjoinv(", ", priv->output_mime_types);
					rb_debug ("got output mime-type list: %s", dbg);
					g_free (dbg);
				}
				libhal_free_string_array (proplist);
			}
			free_dbus_error ("getting supported mime-type list", &error);

			/* get playlist format */
			dbus_error_init (&error);
			proplist = libhal_device_get_property_strlist (ctx, udi, "portable_audio_player.playlist_format", &error);
			if (proplist) {
				if (!dbus_error_is_set (&error)) {
					int fmt;
					for (fmt = 0; proplist[fmt] != NULL; fmt++) {
						if (strcmp (proplist[fmt], "audio/x-mpegurl") == 0) {
							rb_debug ("device supports M3U playlists");
							priv->playlist_format_unknown = FALSE;
							priv->playlist_format_m3u = TRUE;
						} else if (strcmp (proplist[fmt], "audio/x-scpls") == 0) {
							rb_debug ("device supports PLS playlists");
							priv->playlist_format_unknown = FALSE;
							priv->playlist_format_pls = TRUE;
						} else {
							rb_debug ("unrecognized playlist format: %s", proplist[fmt]);
						}
					}
				}

				if (priv->playlist_format_unknown) {
					rb_debug ("didn't find a playlist format");
				}

				libhal_free_string_array (proplist);
			}
			free_dbus_error ("getting playlist format", &error);

			/* get playlist path */
			dbus_error_init (&error);
			prop = libhal_device_get_property_string (ctx, udi, "portable_audio_player.playlist_path", &error);
			if (prop && !dbus_error_is_set (&error)) {
				rb_debug ("got playlist path: %s", prop);
				priv->playlist_path = g_strdup (prop);
				libhal_free_string (prop);
			}
			free_dbus_error ("getting playlist path", &error);

			/* get max folder depth */
			dbus_error_init (&error);
			value = libhal_device_get_property_int (ctx, udi, "portable_audio_player.folder_depth", &error);
			if (!dbus_error_is_set (&error)) {
				rb_debug ("got max folder depth %d", value);
				priv->folder_depth = value;
			}
			free_dbus_error ("getting max folder depth", &error);

		} else {
			rb_debug ("no player info available (HAL doesn't recognise it as a player");
		}
		g_free (udi);
	}
	cleanup_hal_context (ctx);
#else
	rb_debug ("no player info available (no HAL)");
#endif
}

static void
rb_generic_player_source_dispose (GObject *object)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	if (priv->load_playlists_id != 0) {
		g_source_remove (priv->load_playlists_id);
		priv->load_playlists_id = 0;
	}

	if (priv->db) {
		g_object_unref (G_OBJECT (priv->db));
		priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_generic_player_source_parent_class)->dispose (object);
}

static void
rb_generic_player_source_finalize (GObject *object)
{
	RBGenericPlayerSourcePrivate *priv;

	g_return_if_fail (RB_IS_GENERIC_PLAYER_SOURCE (object));
	priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	g_free (priv->mount_path);
	g_strfreev (priv->audio_folders);
	g_strfreev (priv->output_mime_types);
	g_free (priv->playlist_path);
}

RBRemovableMediaSource *
rb_generic_player_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	RBGenericPlayerSource *source;
	RhythmDBEntryType entry_type;
	RhythmDB *db;
	char *name;
	char *path;

	g_assert (rb_generic_player_is_volume_player (volume));

	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	path = gnome_vfs_volume_get_device_path (volume);
	name = g_strdup_printf ("generic audio player: %s", path);
	entry_type = rhythmdb_entry_register_type (db, name);
	g_object_unref (db);
	g_free (name);
	g_free (path);

	source = RB_GENERIC_PLAYER_SOURCE (g_object_new (RB_TYPE_GENERIC_PLAYER_SOURCE,
							 "entry-type", entry_type,
							 "volume", volume,
							 "shell", shell,
							 "sourcelist-group", RB_SOURCELIST_GROUP_REMOVABLE,
							 NULL));

	rb_generic_player_source_get_device_info (source);
	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static void
impl_delete_thyself (RBSource *source)
{
	GList *p;
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	for (p = priv->playlists; p != NULL; p = p->next) {
		RBSource *playlist = RB_SOURCE (p->data);
		rb_source_delete_thyself (playlist);
	}
	g_list_free (priv->playlists);
	priv->playlists = NULL;

	RB_SOURCE_CLASS (rb_generic_player_source_parent_class)->impl_delete_thyself (source);
}

static void
rb_generic_player_source_load_songs (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;

	priv->mount_path = rb_generic_player_source_get_mount_path (source);
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);

	/* if HAL gives us a set of folders on the device containing audio files,
	 * load only those folders, otherwise add the whole volume.
	 */
	if (priv->audio_folders) {
		int af;
		for (af=0; priv->audio_folders[af] != NULL; af++) {
			char *path;
			path = rb_uri_append_path (priv->mount_path, priv->audio_folders[af]);
			rhythmdb_add_uri_with_type (priv->db, path, entry_type);
			g_free (path);
		}
	} else {
		rhythmdb_add_uri_with_type (priv->db, priv->mount_path, entry_type);
	}
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
}

char *
rb_generic_player_source_get_mount_path (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_mount_path (source);
}

static gchar *
default_get_mount_path (RBGenericPlayerSource *source)
{
	gchar *uri;
	GnomeVFSVolume *volume;

	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	uri = gnome_vfs_volume_get_activation_uri (volume);
	g_object_unref (G_OBJECT (volume));

	return uri;
}

gboolean
rb_generic_player_is_volume_player (GnomeVFSVolume *volume)
{
	gboolean result = FALSE;
#ifdef HAVE_HAL_0_5
	LibHalContext *ctx;

	ctx = get_hal_context ();
	if (ctx) {
		gchar *udi = get_hal_udi_for_player (ctx, volume);
		if (udi) {
			DBusError error;
			char *prop;

			/* check that it can be accessed as mass-storage */
			dbus_error_init (&error);
			prop = libhal_device_get_property_string (ctx, udi, "portable_audio_player.access_method", &error);
			if (prop != NULL && strcmp (prop, "storage") == 0 && !dbus_error_is_set (&error)) {
				/* the device has passed all tests, so it should be a usable player */
				result = TRUE;
			} else {
				rb_debug ("device cannot be accessed via storage");
			}
			libhal_free_string (prop);

			free_dbus_error ("checking device access method", &error);
		} else {
			rb_debug ("device is not an audio player");
		}
		g_free (udi);
	}
	cleanup_hal_context (ctx);

#endif /* HAVE_HAL_0_5 */

	/* treat as audio player if ".is_audio_player" exists in the root of the volume  */
	if (!result) {
		char *path = gnome_vfs_volume_get_activation_uri (volume);
		char *file = g_build_filename (path, ".is_audio_player", NULL);

		if (rb_uri_is_local (file) && rb_uri_exists (file))
			result = TRUE;

		g_free (file);
		g_free (path);
	}

	return result;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/GenericPlayerSourcePopup");
	return TRUE;
}

static gboolean
impl_can_move_to_trash (RBSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	return priv->handles_trash;
}

/* code for playlist loading */

void
rb_generic_player_source_add_playlist (RBGenericPlayerSource *source,
				       RBShell *shell,
				       RBSource *playlist)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	priv->playlists = g_list_prepend (priv->playlists, playlist);

	rb_shell_append_source (shell, playlist, RB_SOURCE (source));
}

static gboolean
rb_generic_player_source_load_playlists (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	GDK_THREADS_ENTER ();

	priv->load_playlists_id = 0;

	if (klass->impl_load_playlists)
		klass->impl_load_playlists (source);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static char *
rb_generic_player_source_transform_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_transform_playlist_uri (source, uri);
}


typedef struct {
	RBGenericPlayerSource *player_source;
	RBStaticPlaylistSource *source;
} HandlePlaylistEntryData;

static void
handle_playlist_entry_cb (TotemPlParser *playlist, const char *uri,
			  const char *title,
			  const char *genre, HandlePlaylistEntryData *data)
{
	char *local_uri;
	char *name;

	local_uri = rb_generic_player_source_transform_playlist_uri (data->player_source, uri);
	if (local_uri == NULL)
		return;

	g_object_get (G_OBJECT (data->source), "name", &name, NULL);
	rb_debug ("adding '%s' as '%s' to playlist '%s'", uri, local_uri, name);
	rb_static_playlist_source_add_location (data->source, local_uri, -1);
	g_free (local_uri);
	g_free (name);
}

static void
load_playlist_file (RBGenericPlayerSource *source,
		    const char *playlist_path,
		    const char *rel_path)
{
	RhythmDBEntryType entry_type;
	RBStaticPlaylistSource *playlist;
	TotemPlParser *parser;
	HandlePlaylistEntryData *data;
	RBShell *shell;

	g_object_get (G_OBJECT (source),
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);

	playlist = RB_STATIC_PLAYLIST_SOURCE (
			rb_static_playlist_source_new (shell,
						      rel_path,
						      FALSE,
						      entry_type));
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	data = g_new0 (HandlePlaylistEntryData, 1);
	data->source = playlist;
	data->player_source = source;

	parser = totem_pl_parser_new ();

	g_signal_connect (parser,
			  "entry", G_CALLBACK (handle_playlist_entry_cb),
			  data);
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (parser), "recurse"))
		g_object_set (G_OBJECT (parser), "recurse", FALSE, NULL);

	if (totem_pl_parser_parse (parser, playlist_path, TRUE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
		rb_debug ("unable to parse %s as playlist", playlist_path);
		g_object_unref (G_OBJECT (playlist));
	} else {
		rb_generic_player_source_add_playlist (source, shell, RB_SOURCE (playlist));
	}

	g_object_unref (G_OBJECT (parser));
	g_free (data);

	g_object_unref (G_OBJECT (shell));
}

static gboolean
visit_playlist_dirs (const gchar *rel_path,
		     GnomeVFSFileInfo *info,
		     gboolean recursing_will_loop,
		     RBGenericPlayerSource *source,
		     gboolean *recurse)
{
	char *main_path;
	char *playlist_path;
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	*recurse = TRUE;

	main_path = rb_generic_player_source_get_mount_path (source);
	playlist_path = rb_uri_append_path (main_path, rel_path);
	g_free (main_path);

	/* don't add playlists in formats we know the player can't handle  */
	if ((g_str_has_suffix (playlist_path, ".m3u") && (priv->playlist_format_unknown || priv->playlist_format_m3u)) ||
	   (g_str_has_suffix (playlist_path, ".pls") && (priv->playlist_format_unknown || priv->playlist_format_pls))) {
		load_playlist_file (source, playlist_path, rel_path);
	}
	g_free (playlist_path);

	return TRUE;
}

static void
default_load_playlists (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	char *mount_path;
	char *playlist_path = NULL;

	mount_path = rb_generic_player_source_get_mount_path (source);
	if (priv->playlist_path) {
		/* If the device only supports a single playlist, just load that */
		if (strstr (priv->playlist_path, "%File") == NULL) {
			char *playlist_path = rb_uri_append_path (mount_path, priv->playlist_path);
			if (rb_uri_exists (playlist_path)) {
				load_playlist_file (source, playlist_path, priv->playlist_path);
			}

			return;
		}

		/* Otherwise, limit the search to the playlist folder.
		 * The HAL spec is pretty vague on what the playlist path
		 * actually means when it includes '%File'.  All current
		 * examples are 'folder/%File', so we'll just make it work
		 * for that.
		 */
		if (g_str_has_suffix (priv->playlist_path, "/%File")) {
			char *playlist_folder = g_strdup (priv->playlist_path);
			playlist_folder[strlen (playlist_folder) - strlen("/%File")] = '\0';
			playlist_path = rb_uri_append_path (mount_path, playlist_folder);
			g_free (playlist_folder);
			rb_debug ("constructed playlist search path %s", playlist_path);
		} else {
			/* fall back to searching the whole volume */
			rb_debug ("Unsupported playlist path format: %s", priv->playlist_path);
		}
	}

	gnome_vfs_directory_visit (playlist_path ? playlist_path : mount_path,
				   GNOME_VFS_FILE_INFO_DEFAULT,
				   GNOME_VFS_DIRECTORY_VISIT_DEFAULT,
				   (GnomeVFSDirectoryVisitFunc) visit_playlist_dirs,
				   source);

	g_free (playlist_path);
	g_free (mount_path);
}

static char *
default_transform_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	char *mount_uri;
	char *full_uri;

	mount_uri = rb_generic_player_source_get_mount_path (source);
	full_uri = rb_uri_append_uri (mount_uri, uri);
	g_free (mount_uri);
	return full_uri;
}

static gboolean
impl_can_paste (RBSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	return (priv->read_only == FALSE);
}

/* probably should move this somewhere common */
static char *
sanitize_path (const char *str)
{
	gchar *res = NULL;
	gchar *s;

	/* Skip leading periods, otherwise files disappear... */
	while (*str == '.')
		str++;

	s = g_strdup(str);
	/* Replace path seperators with a hyphen */
	g_strdelimit (s, "/", '-');

	/* Replace separators with a hyphen */
	g_strdelimit (s, "\\:|", '-');
	/* Replace all other weird characters to whitespace */
	g_strdelimit (s, "*?&!\'\"$()`>{}", ' ');
	/* Replace all whitespace with underscores */
	/* TODO: I'd like this to compress whitespace aswell */
	g_strdelimit (s, "\t ", '_');

	res = g_filename_from_utf8(s, -1, NULL, NULL, NULL);
	g_free(s);
	return res ? res : g_strdup(str);
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *list = NULL;
	char **mime;

	for (mime = priv->output_mime_types; mime && *mime != NULL; mime++) {
		list = g_list_prepend (list, g_strdup (*mime));
	}
	return g_list_reverse (list);
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	const char *mime;
	char *artist, *album, *title;
	gulong track_number, disc_number;
	const char *folders;
	char *number;
	char *file = NULL;
	char *path;
	char *ext;

	rb_debug ("building dest uri for entry at %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

	if (extension != NULL) {
		ext = g_strconcat (".", extension, NULL);
	} else {
		ext = g_strdup ("");
	}

	artist = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	album = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	title = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));

	/* we really do need to fix this so untagged entries actually have NULL rather than
	 * a translated string.
	 */
	if (strcmp (artist, _("Unknown")) == 0 && strcmp (album, _("Unknown")) == 0 &&
	    g_str_has_suffix (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), title)) {
		/* file isn't tagged, so just use the filename as-is */
		file = g_strdup (title);
	}

	mime = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);

	/* hackish mapping of gstreamer media types to mime types; this
	 * should be easier when we do proper (deep) typefinding.
	 */
	if (strcmp (mime, "audio/x-wav") == 0) {
		/* if it has a bitrate, assume it's mp3-in-wav */
		if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
			mime = "audio/mpeg";
	} else if (strcmp (mime, "application/x-id3") == 0) {
		mime = "audio/mpeg";
	}

	if (file == NULL) {
		track_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
		disc_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
		if (disc_number > 0)
			number = g_strdup_printf ("%u.%u", (guint)disc_number, (guint)track_number);
		else
			number = g_strdup_printf ("%u", (guint)track_number);

		switch (priv->folder_depth) {
		case 0:
			/* artist - album - number - title */
			file = g_strdup_printf ("%s - %s - %s - %s%s",
						artist, album, number, title, ext);
			break;

		case 1:
			/* artist - album/number - title */
			file = g_strdup_printf ("%s - %s" G_DIR_SEPARATOR_S "%s - %s%s",
						artist, album, number, title, ext);
			break;

		default: /* use this for players that don't care */
		case 2:
			/* artist/album/number - title */
			file = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s" G_DIR_SEPARATOR_S "%s - %s%s",
						artist, album, number, title, ext);
			break;
		}
		g_free (number);
	}

	g_free (artist);
	g_free (album);
	g_free (title);
	g_free (ext);

	if (file == NULL)
		return NULL;

	if (priv->audio_folders && priv->audio_folders[0])
		folders = priv->audio_folders[0];
	else
		folders = "";

	path = g_build_filename (priv->mount_path, folders, file, NULL);
	g_free (file);

	/* TODO: check for duplicates, or just overwrite by default? */
	rb_debug ("dest file is %s", path);
	return path;
}

/* generic HAL-related code */

#ifdef HAVE_HAL_0_5
static LibHalContext *
get_hal_context (void)
{
	LibHalContext *ctx = NULL;
	DBusConnection *conn = NULL;
	DBusError error;
	gboolean result = FALSE;

	dbus_error_init (&error);
	ctx = libhal_ctx_new ();
	if (ctx == NULL)
		return NULL;

	conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (conn != NULL && !dbus_error_is_set (&error)) {
		libhal_ctx_set_dbus_connection (ctx, conn);
		if (libhal_ctx_init (ctx, &error))
			result = TRUE;
	}

	if (dbus_error_is_set (&error)) {
		free_dbus_error ("setting up hal context", &error);
		result = FALSE;
	}

	if (!result) {
		libhal_ctx_free (ctx);
		ctx = NULL;
	}
	return ctx;
}

static void
cleanup_hal_context (LibHalContext *ctx)
{
	DBusError error;
	if (ctx == NULL)
		return;

	dbus_error_init (&error);
	libhal_ctx_shutdown (ctx, &error);
	libhal_ctx_free (ctx);
	free_dbus_error ("cleaning up hal context", &error);
}

static char *
get_hal_udi_for_player (LibHalContext *ctx, GnomeVFSVolume *volume)
{
	DBusError error;
	gchar *udi = gnome_vfs_volume_get_hal_udi (volume);
	if (udi == NULL)
		return NULL;

	dbus_error_init (&error);
	/* find the udi of the player itself */
	rb_debug ("searching for player udi from %s", udi);
	while (!libhal_device_query_capability (ctx, udi, "portable_audio_player", &error) &&
	       !dbus_error_is_set (&error)) {
		char *new_udi = libhal_device_get_property_string (ctx, udi, "info.parent", &error);
		if (dbus_error_is_set (&error))
			break;

		rb_debug ("parent of udi %s: %s", udi, new_udi);
		g_free (udi);
		udi = NULL;
		if ((new_udi == NULL) || strcmp (new_udi, "/") == 0) {
			break;
		}

		udi = g_strdup (new_udi);
		libhal_free_string (new_udi);
	}
	if (dbus_error_is_set (&error)) {
		g_free (udi);
		udi = NULL;
		free_dbus_error ("finding audio player udi", &error);
	}
	return udi;
}

static void
free_dbus_error (const char *what, DBusError *error)
{
	if (dbus_error_is_set (error)) {
		rb_debug ("%s: dbus error: %s", what, error->message);
		dbus_error_free (error);
	}
}


#endif

