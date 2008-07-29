/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of PSP source object
 *
 *  Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

#define __EXTENSIONS__

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus.h>
#endif

#include "eel-gconf-extensions.h"
#include "rb-psp-source.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-auto-playlist-source.h"
#include "rhythmdb.h"
#include "rb-plugin.h"

static void rb_psp_source_create_playlists (RBGenericPlayerSource *source);
static gchar *impl_get_mount_path (RBGenericPlayerSource *source);

typedef struct
{
#ifdef __SUNPRO_C
  char x; /* To build with Solaris forte compiler. */
#endif
} RBPspSourcePrivate;


RB_PLUGIN_DEFINE_TYPE (RBPspSource, rb_psp_source, RB_TYPE_GENERIC_PLAYER_SOURCE)
#define PSP_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PSP_SOURCE, RBPspSourcePrivate))


static void
rb_psp_source_class_init (RBPspSourceClass *klass)
{
	RBGenericPlayerSourceClass *generic_class = RB_GENERIC_PLAYER_SOURCE_CLASS (klass);

	generic_class->impl_get_mount_path = impl_get_mount_path;
	generic_class->impl_load_playlists = rb_psp_source_create_playlists;

	g_type_class_add_private (klass, sizeof (RBPspSourcePrivate));
}

static void
rb_psp_source_init (RBPspSource *source)
{

}

RBRemovableMediaSource *
rb_psp_source_new (RBShell *shell, GMount *mount)
{
	RBPspSource *source;
	RhythmDBEntryType entry_type;
	RhythmDB *db;
	char *name;
	char *path;
	GVolume *volume;

	g_assert (rb_psp_is_mount_player (mount));

	volume = g_mount_get_volume (mount);

	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	name = g_strdup_printf ("psp: %s", path);
	entry_type = rhythmdb_entry_register_type (db, name);
	g_object_unref (db);
	g_free (name);
	g_free (path);
	g_object_unref (volume);

	source = RB_PSP_SOURCE (g_object_new (RB_TYPE_PSP_SOURCE,
					  "entry-type", entry_type,
					  "mount", mount,
					  "shell", shell,
					  "source-group", RB_SOURCE_GROUP_DEVICES,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static char *
find_music_dir (GMount *mount)
{
	char *path = NULL;
	GFile *root;
	GFile *music_dir;

	root = g_mount_get_root (mount);
	if (root != NULL) {
		int i;
		char *paths[] = {
			"PSP/MUSIC",
			"MUSIC",
			NULL
		};

		i = 0;
		while (paths[i] != NULL && path == NULL) {
			music_dir = g_file_resolve_relative_path (root, "PSP/MUSIC");
			if (g_file_query_exists (music_dir, NULL)) {
				path = g_file_get_path (music_dir);
			}
			g_object_unref (music_dir);
		}

		g_object_unref (root);
	}

	return path;
}

static char *
impl_get_mount_path (RBGenericPlayerSource *source)
{
	GMount *mount;
	char *path;

	g_object_get (G_OBJECT (source), "mount", &mount, NULL);

	path = find_music_dir (mount);
	g_object_unref (mount);

	return path;
}

static gboolean
visit_playlist_dirs (GFile *file,
		     gboolean dir,
		     RBPspSource *source)
{
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType entry_type;
	char *playlist_path;
	char *playlist_name;
	RBSource *playlist;
	GPtrArray *query;

	if (dir == FALSE) {
		return TRUE;
	}

	playlist_path = g_file_get_uri (file);		/* or _get_path? */

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell,
		      "db", &db,
		      NULL);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, entry_type,
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_LOCATION, playlist_path,
				      RHYTHMDB_QUERY_END);
	g_free (playlist_path);
        g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	playlist_name = g_file_get_basename (file);
	playlist = rb_auto_playlist_source_new (shell, playlist_name, FALSE);
	g_free (playlist_name);

	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (playlist), query,
					   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
					   NULL, 0);
	rb_generic_player_source_add_playlist (RB_GENERIC_PLAYER_SOURCE (source), shell, RB_SOURCE (playlist));
	rhythmdb_query_free (query);

	g_object_unref (shell);
	g_object_unref (db);

	return TRUE;
}


static void
rb_psp_source_create_playlists (RBGenericPlayerSource *source)
{
	char *mount_path;

	mount_path = rb_generic_player_source_get_mount_path (source);
	rb_uri_handle_recursively (mount_path,
				   NULL,
				   (RBUriRecurseFunc) visit_playlist_dirs,
				   source);
	g_free (mount_path);
}

#ifdef HAVE_HAL

static gboolean
hal_udi_is_psp (const char *udi)
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

	parent_udi = NULL;
	parent_name = NULL;

	conn = NULL;
	ctx = libhal_ctx_new ();
	if (ctx == NULL) {
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
	if (parent_name == NULL || dbus_error_is_set (&error))
		goto end;

	if (strcmp (parent_name, "PSP") == 0) {
		result = TRUE;
	}

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
#endif

gboolean
rb_psp_is_mount_player (GMount *mount)
{
#ifndef HAVE_HAL
	char *music_dir;
#else
	GVolume *volume;
#endif
	gboolean result = FALSE;
	gchar *str;

#ifndef HAVE_HAL
	music_dir = find_music_dir (mount);
	result = (music_dir != NULL);
	g_free (music_dir);
#else
	volume = g_mount_get_volume (mount);
	if (volume != NULL) {
		str = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_HAL_UDI);
		if (str != NULL) {
			result = hal_udi_is_psp (str);
			g_free (str);
		}
		g_object_unref (volume);
	}
#endif
	return result;
}

