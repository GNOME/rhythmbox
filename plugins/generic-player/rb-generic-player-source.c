/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <gtk/gtktreeview.h>
#include <string.h>
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
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
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"

static GObject *rb_generic_player_source_constructor (GType type, guint n_construct_properties,
						      GObjectConstructParam *construct_properties);
static void rb_generic_player_source_dispose (GObject *object);

static gboolean rb_generic_player_source_load_playlists (RBGenericPlayerSource *source);
static void rb_generic_player_source_load_songs (RBGenericPlayerSource *source);

static gboolean impl_show_popup (RBSource *source);
static gchar *default_get_mount_path (RBGenericPlayerSource *source);
static void default_load_playlists (RBGenericPlayerSource *source);
static char * default_transform_playlist_uri (RBGenericPlayerSource *source,
					      const char *uri);

typedef struct
{
	char *mount_path;
} RBGenericPlayerSourcePrivate;


G_DEFINE_TYPE (RBGenericPlayerSource, rb_generic_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define GENERIC_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourcePrivate))


static void
rb_generic_player_source_class_init (RBGenericPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructor = rb_generic_player_source_constructor;
	object_class->dispose = rb_generic_player_source_dispose;

	source_class->impl_show_popup = impl_show_popup;

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

	source = RB_GENERIC_PLAYER_SOURCE (G_OBJECT_CLASS (rb_generic_player_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	rb_generic_player_source_load_songs (source);
	g_idle_add ((GSourceFunc)rb_generic_player_source_load_playlists, source);

	return G_OBJECT (source);
}

static void 
rb_generic_player_source_dispose (GObject *object)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	if (priv->mount_path) {
		g_free (priv->mount_path);
		priv->mount_path = NULL;
	}
	
	G_OBJECT_CLASS (rb_generic_player_source_parent_class)->dispose (object);
}

RBRemovableMediaSource *
rb_generic_player_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	RBGenericPlayerSource *source;
	RhythmDBEntryType entry_type;

	g_assert (rb_generic_player_is_volume_player (volume));

	entry_type =  rhythmdb_entry_register_type ();

	source = RB_GENERIC_PLAYER_SOURCE (g_object_new (RB_TYPE_GENERIC_PLAYER_SOURCE,
					  "entry-type", entry_type,
					  "volume", volume,
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static void
rb_generic_player_source_load_songs (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType entry_type;

	priv->mount_path = rb_generic_player_source_get_mount_path (source);
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	g_object_unref (G_OBJECT (shell));

	rhythmdb_add_uri_with_type (db, priv->mount_path, entry_type);
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
	gchar *udi = gnome_vfs_volume_get_hal_udi (volume);

	if (udi != NULL) {
		LibHalContext *ctx = NULL;
		DBusConnection *conn = NULL;
		DBusError error;
		char *prop = NULL;

		dbus_error_init (&error);
		ctx = libhal_ctx_new ();
		if (ctx == NULL)
			goto end_hal;

		conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
		if (conn == NULL || dbus_error_is_set (&error))
			goto end_hal;
		libhal_ctx_set_dbus_connection (ctx, conn);
		if (!libhal_ctx_init (ctx, &error) || dbus_error_is_set (&error))
			goto end_hal;

		/* find the udi of the player itself */
		while (!libhal_device_query_capability (ctx, udi, "portable_audio_player", &error) &&
		       !dbus_error_is_set (&error)) {
			char *new_udi = libhal_device_get_property_string (ctx, udi, "info.parent", &error);
			if (dbus_error_is_set (&error))
				goto end_hal;

			if ((new_udi == NULL) || strcmp (new_udi, "/") == 0) {
				rb_debug ("device is not audio player");
				goto end_hal;
			}

			g_free (udi);
			udi = g_strdup (new_udi);
			libhal_free_string (new_udi);
		}
		if (dbus_error_is_set (&error))
			goto end_hal;

		/* check that it can be accessed as mass-storage */
		prop = libhal_device_get_property_string (ctx, udi, "portable_audio_player.access_method", &error);
		if (prop == NULL || strcmp (prop, "storage") != 0 || dbus_error_is_set (&error)) {
			rb_debug ("device cannot be accessed via storage");
			goto end_hal;
		}

		/* the device has passed all tests, so it should be a usable player */
		result = TRUE;
end_hal:
		if (dbus_error_is_set (&error)) {
			rb_debug ("Error: %s\n", error.message);
			dbus_error_free (&error);
			dbus_error_init (&error);
		}

		if (prop)
			libhal_free_string (prop);

		if (ctx) {
			libhal_ctx_shutdown (ctx, &error);
			libhal_ctx_free (ctx);
		}
		dbus_error_free (&error);

		g_free (udi);
	}
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


/* code for playlist loading */

static gboolean
rb_generic_player_source_load_playlists (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	if (klass->impl_load_playlists)
		klass->impl_load_playlists (source);

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
	const char *name;

	local_uri = rb_generic_player_source_transform_playlist_uri (data->player_source, uri);
	if (local_uri == NULL)
		return;
	
	g_object_get (G_OBJECT (data->source), "name", &name, NULL);
	rb_debug ("adding '%s' as '%s' to playlist '%s'", uri, local_uri, name);
	rb_static_playlist_source_add_location (data->source, local_uri, -1);
	g_free (local_uri);
}

static gboolean
visit_playlist_dirs (const gchar *rel_path,
		     GnomeVFSFileInfo *info,
		     gboolean recursing_will_loop,
		     RBGenericPlayerSource *source,
		     gboolean *recurse)
{
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType entry_type;
	char *main_path;
	char *playlist_path;
	TotemPlParser *parser;
	RBStaticPlaylistSource *playlist;
	HandlePlaylistEntryData *data;

	*recurse = TRUE;

	/* add playlist */
	main_path = rb_generic_player_source_get_mount_path (source);
	playlist_path = rb_uri_append_path (main_path, rel_path);
	g_free (main_path);

	if (!g_str_has_suffix (playlist_path, ".m3u") &&
	    !g_str_has_suffix (playlist_path, ".pls")) {
		g_free (playlist_path);
		return TRUE;
	}

	g_object_get (G_OBJECT (source), 
		      "shell", &shell, 
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (G_OBJECT (shell),
		      "db", &db,
		      NULL);

	playlist = RB_STATIC_PLAYLIST_SOURCE (
			rb_static_playlist_source_new (shell, 
						      rel_path, 
						      FALSE,
						      entry_type));

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
		g_object_unref (G_OBJECT (source));
	} else {
		rb_shell_append_source (shell, RB_SOURCE (playlist), RB_SOURCE (source));
	}

	g_object_unref (G_OBJECT (parser));
	g_free (playlist_path);
	g_free (data);

	g_object_unref (G_OBJECT (shell));
	g_object_unref (G_OBJECT (db));

	return TRUE;
}

static void
default_load_playlists (RBGenericPlayerSource *source)
{
	char *mount_path;

	mount_path = rb_generic_player_source_get_mount_path (source);
	gnome_vfs_directory_visit (mount_path,
				   GNOME_VFS_FILE_INFO_DEFAULT,
				   GNOME_VFS_DIRECTORY_VISIT_DEFAULT,
				   (GnomeVFSDirectoryVisitFunc) visit_playlist_dirs,
				   source);
	g_free (mount_path);
}

static char *
default_transform_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	return local_uri = rb_uri_append_uri (rb_generic_player_source_get_mount_path (source), uri);
}
