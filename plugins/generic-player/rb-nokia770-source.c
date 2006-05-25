/*
 *  arch-tag: Implementation of Nokia 770 source object
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <gtk/gtktreeview.h>
#include <string.h>
#include "rhythmdb.h"
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <libhal.h>
#include <dbus/dbus.h>

#include "eel-gconf-extensions.h"
#include "rb-nokia770-source.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"
#include "rb-plugin.h"


static char * impl_transform_playlist_uri (RBGenericPlayerSource *source, const char *uri);


typedef struct {
#ifdef __SUNPRO_C
   int x;  /* To build with Solaris forte compiler */
#endif
} RBNokia770SourcePrivate;

RB_PLUGIN_DEFINE_TYPE (RBNokia770Source, rb_nokia770_source, RB_TYPE_GENERIC_PLAYER_SOURCE)
#define NOKIA770_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_NOKIA770_SOURCE, RBNokia770SourcePrivate))


#define NOKIA_INTERNAL_MOUNTPOINT "file:///media/mmc1/"

static void
rb_nokia770_source_class_init (RBNokia770SourceClass *klass)
{
	RBGenericPlayerSourceClass *generic_class = RB_GENERIC_PLAYER_SOURCE_CLASS (klass);

	generic_class->impl_transform_playlist_uri = impl_transform_playlist_uri;

	g_type_class_add_private (klass, sizeof (RBNokia770SourcePrivate));
}

static void
rb_nokia770_source_init (RBNokia770Source *source)
{

}

RBRemovableMediaSource *
rb_nokia770_source_new (RBShell *shell, GnomeVFSVolume *volume)
{
	RBNokia770Source *source;
	RhythmDBEntryType entry_type;

	g_assert (rb_nokia770_is_volume_player (volume));

	entry_type =  rhythmdb_entry_register_type ();

	source = RB_NOKIA770_SOURCE (g_object_new (RB_TYPE_NOKIA770_SOURCE,
					  "entry-type", entry_type,
					  "volume", volume,
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static char *
impl_transform_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	const char *path;
	char *local_uri;

	if (!g_str_has_prefix (uri, NOKIA_INTERNAL_MOUNTPOINT)) {
		rb_debug ("found playlist uri with unexpected mountpoint");
		return NULL;
	}

	path = uri + strlen (NOKIA_INTERNAL_MOUNTPOINT);
	local_uri = rb_uri_append_uri (rb_generic_player_source_get_mount_path (source), path);
	return local_uri;
}


#ifdef HAVE_HAL_0_5

static gboolean
hal_udi_is_nokia770 (const char *udi)
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


	rb_debug ("Nokia detection: info.parent=%s", parent_udi);
	parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"info.vendor", &error);
	rb_debug ("Nokia detection: info.vendor=%s", parent_name);
	if (parent_name == NULL || dbus_error_is_set (&error))
		goto end;

	if (strcmp (parent_name, "Nokia") == 0) {
		g_free (parent_name);

		parent_name = libhal_device_get_property_string (ctx, parent_udi,
			"info.product", &error);
		rb_debug ("Nokia detection: info.product=%s", parent_name);
		if (parent_name == NULL || dbus_error_is_set (&error))
			goto end;

		if (strcmp (parent_name, "770") == 0) {
			result = TRUE;
		}
	}
	
	g_free (parent_name);
		
	g_free (parent_udi);
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
hal_udi_is_nokia770 (const char *udi)
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
			"info.vendor");
	if (parent_name != NULL && strcmp (parent_name, "Nokia") == 0) {
		g_free (parent_udi);
		parent_name = hal_device_get_property_string (ctx, parent_udi,
				"info.product");
		if (parent_name != NULL && strcmp (parent_name, "770") == 0) {
			result = TRUE;
		}
	}

	g_free (parent_name);
	g_free (parent_udi);

	hal_shutdown (ctx);

	return result;
}

#endif

gboolean
rb_nokia770_is_volume_player (GnomeVFSVolume *volume)
{
	gboolean result = FALSE;
	gchar *str;

	if (gnome_vfs_volume_get_volume_type (volume) != GNOME_VFS_VOLUME_TYPE_MOUNTPOINT) {
		return FALSE;
	}

	str = gnome_vfs_volume_get_hal_udi (volume);
	if (str != NULL) {
		gboolean result;

		result = hal_udi_is_nokia770 (str);
		g_free (str);
		return result;
	}

	return result;
}

