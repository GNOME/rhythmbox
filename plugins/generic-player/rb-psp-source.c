/*
 *  arch-tag: Implementation of PSP source object
 *
 *  Copyright (C) 2006 James Livingston  <jrl@ids.org.au>
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
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

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

static GObject *rb_psp_source_constructor (GType type, guint n_construct_properties,
						      GObjectConstructParam *construct_properties);
static gboolean rb_psp_source_create_playlists (RBPspSource *source);
static gchar *impl_get_mount_path (RBGenericPlayerSource *source);

typedef struct
{

} RBPspSourcePrivate;


G_DEFINE_TYPE (RBPspSource, rb_psp_source, RB_TYPE_GENERIC_PLAYER_SOURCE)
#define PSP_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PSP_SOURCE, RBPspSourcePrivate))


static void
rb_psp_source_class_init (RBPspSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBGenericPlayerSourceClass *generic_class = RB_GENERIC_PLAYER_SOURCE_CLASS (klass);

	object_class->constructor = rb_psp_source_constructor;

	generic_class->impl_get_mount_path = impl_get_mount_path;

	g_type_class_add_private (klass, sizeof (RBPspSourcePrivate));
}

static void
rb_psp_source_init (RBPspSource *source)
{

}

static GObject *
rb_psp_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBPspSource *source; 

	source = RB_PSP_SOURCE (G_OBJECT_CLASS (rb_psp_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_idle_add ((GSourceFunc)rb_psp_source_create_playlists, source);

	return G_OBJECT (source);
}

RBRemovableMediaSource *
rb_psp_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	RBPspSource *source;
	RhythmDBEntryType entry_type;

	g_assert (rb_psp_is_volume_player (volume));

	entry_type =  rhythmdb_entry_register_type ();

	source = RB_PSP_SOURCE (g_object_new (RB_TYPE_PSP_SOURCE,
					  "entry-type", entry_type,
					  "volume", volume,
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static gchar *
impl_get_mount_path (RBGenericPlayerSource *source)
{
	gchar *uri, *path;
	GnomeVFSVolume *volume;

	g_object_get (G_OBJECT (source), "volume", &volume, NULL);
	uri = gnome_vfs_volume_get_activation_uri (volume);
	g_object_unref (G_OBJECT (volume));

	path = rb_uri_append_path (uri, "PSP/MUSIC");

	return path;
}

static gboolean
visit_playlist_dirs (const gchar *rel_path,
		     GnomeVFSFileInfo *info,
		     gboolean recursing_will_loop,
		     RBPspSource *source,
		     gboolean *recurse)
{
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType entry_type;
	char *main_path;
	char *playlist_path;
	RBSource *playlist;
	GPtrArray *query;

	*recurse = FALSE;

	/* add playlist */
	main_path = rb_generic_player_source_get_mount_path (RB_GENERIC_PLAYER_SOURCE (source));
	playlist_path = rb_uri_append_path (main_path, rel_path);
	g_free (main_path);

	if (!rb_uri_is_directory (playlist_path)) {
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

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, entry_type,
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_LOCATION, playlist_path,
				      RHYTHMDB_QUERY_END);
	g_free (playlist_path);

	playlist = rb_auto_playlist_source_new (shell, rel_path, FALSE);
	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (playlist), query,
					  0, 0, 0, NULL, 0);
	rb_shell_append_source (shell, playlist, RB_SOURCE (source));
	g_object_unref (G_OBJECT (shell));
	g_object_unref (G_OBJECT (db));

	return TRUE;
}


static gboolean
rb_psp_source_create_playlists (RBPspSource *source)
{
	char *mount_path;

	mount_path = rb_generic_player_source_get_mount_path (RB_GENERIC_PLAYER_SOURCE (source));
	gnome_vfs_directory_visit (mount_path,
				   GNOME_VFS_FILE_INFO_DEFAULT,
				   GNOME_VFS_DIRECTORY_VISIT_DEFAULT,
				   (GnomeVFSDirectoryVisitFunc) visit_playlist_dirs,
				   source);
	g_free (mount_path);
	return FALSE;
}

#ifdef HAVE_HAL_0_5

static gboolean
hal_udi_is_psp (const char *udi)
{
	LibHalContext *ctx;
	DBusConnection *conn;
	char *parent_udi, *parent_name;
	gboolean result;
	DBusError error;

	result = FALSE;
	dbus_error_init (&error);
	
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

	parent_udi = libhal_device_get_property_string (ctx, udi,
			"info.parent", &error);
	if (parent_udi == NULL || dbus_error_is_set (&error))
		goto end;
		
	parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"storage.model", &error);
	g_free (parent_udi);
	if (parent_name == NULL || dbus_error_is_set (&error))
		goto end;

	if (strcmp (parent_name, "PSP") == 0) {
		result = TRUE;
	}

	g_free (parent_name);
end:
	if (dbus_error_is_set (&error)) {
		rb_debug ("Error: %s\n", error.message);
		dbus_error_free (&error);
		dbus_error_init (&error);
	}

	if (ctx) {
		libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free(ctx);
	}

	dbus_error_free (&error);

	return result;
}

#elif HAVE_HAL_0_2

static gboolean
hal_udi_is_psp (const char *udi)
{
	LibHalContext *ctx;
	char *parent_udi, *parent_name;
	gboolean result;

	result = FALSE;
	ctx = hal_initialize (NULL, FALSE);
	if (ctx == NULL) {
		return FALSE;
	}
	parent_udi = hal_device_get_property_string (ctx, udi,
			"info.parent");
	parent_name = hal_device_get_property_string (ctx, parent_udi,
			"storage.model");
	g_free (parent_udi);

	if (parent_name != NULL && strcmp (parent_name, "PSP") == 0) {
		result = TRUE;
	}

	g_free (parent_name);
	hal_shutdown (ctx);

	return result;
}

#endif

gboolean
rb_psp_is_volume_player (GnomeVFSVolume *volume)
{
	gboolean result = FALSE;
	gchar *str;

	if (gnome_vfs_volume_get_volume_type (volume) != GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		return FALSE;
	}

#ifndef HAVE_HAL
	str = gnome_vfs_volume_get_activation_uri (volume);
	if (str) {
		char *path;

		path = rb_uri_append_path (str, "PSP/MUSIC");
		g_free (str);
		result = rb_uri_exists (path);
		g_free (path);
		return result;
	}
#else
	str = gnome_vfs_volume_get_hal_udi (volume);
	if (str != NULL) {
		gboolean result;

		result = hal_udi_is_psp (str);
		g_free (str);
		return result;
	}
#endif
	return result;
}

