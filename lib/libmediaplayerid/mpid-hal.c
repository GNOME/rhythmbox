/*
 * Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <dbus/dbus.h>
#include <libhal.h>

#include "mediaplayerid.h"
#include "mpid-private.h"

static void
free_dbus_error (const char *what, DBusError *error)
{
	if (dbus_error_is_set (error)) {
		mpid_debug ("%s: %s\n", what, error->message);
		dbus_error_free (error);
	}
}

static LibHalContext *
create_hal_context ()
{
	LibHalContext *ctx = NULL;
	DBusConnection *conn = NULL;
	DBusError error;
	gboolean result = FALSE;

	dbus_error_init (&error);
	ctx = libhal_ctx_new ();
	if (ctx == NULL) {
		mpid_debug ("unable to create hal context\n");
		return NULL;
	}

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
destroy_hal_context (LibHalContext *ctx)
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
find_portable_audio_player_udi (LibHalContext *context, MPIDDevice *device, const char *device_path)
{
	char *udi;
	char **udis;
	int num_udis;
	DBusError error;

	udis = libhal_manager_find_device_string_match (context,
							"block.device", device_path,
							&num_udis, NULL);
	if (udis == NULL || num_udis < 1) {
		mpid_debug ("unable to find hal UDI for device %s", device_path);
		device->error = MPID_ERROR_MECHANISM_FAILED;
		libhal_free_string_array (udis);
		destroy_hal_context (context);
		return NULL;
	}

	udi = g_strdup (udis[0]);
	libhal_free_string_array (udis);

	/* walk up the device hierarchy until we find something with the portable_audio_player
	 * capability.  if we don't find anything, give up.
	 */
	dbus_error_init (&error);
	while (!libhal_device_query_capability (context, udi, "portable_audio_player", &error) && !dbus_error_is_set (&error)) {
		char *new_udi;

		new_udi = libhal_device_get_property_string (context, udi, "info.parent", &error);
		if (dbus_error_is_set (&error))
			break;

		g_free (udi);
		udi = NULL;

		if (new_udi == NULL) {
			break;
		} else if (g_str_equal (new_udi, "/")) {
			libhal_free_string (new_udi);
			break;
		}

		udi = g_strdup (new_udi);
		libhal_free_string (new_udi);
	}
	free_dbus_error ("finding portable_audio_player device", &error);

	if (udi == NULL) {
		mpid_debug ("unable to find portable_audio_player device in hal\n");
		device->error = MPID_ERROR_NOT_MEDIA_PLAYER;
	}
	return udi;
}

static char **
get_property_list (LibHalContext *context, const char *udi, const char *hal_property)
{
	DBusError error;
	char **hal_proplist;
	char **proplist = NULL;

	dbus_error_init (&error);

	hal_proplist = libhal_device_get_property_strlist (context, udi, hal_property, &error);
	if (hal_proplist) {
		if (!dbus_error_is_set (&error)) {
			proplist = g_strdupv (hal_proplist);
		}
		libhal_free_string_array (hal_proplist);
	}
	free_dbus_error ("getting string list property", &error);
	return proplist;
}

static char *
get_property_string (LibHalContext *context, const char *udi, const char *hal_property)
{
	DBusError error;
	char *hal_prop;
	char *prop = NULL;

	dbus_error_init (&error);

	hal_prop = libhal_device_get_property_string (context, udi, hal_property, &error);
	if (hal_prop) {
		if (!dbus_error_is_set (&error)) {
			prop = g_strdup (hal_prop);
		}
		libhal_free_string (hal_prop);
	}
	free_dbus_error ("getting string property", &error);
	return prop;
}

static int
get_property_int (LibHalContext *context, const char *udi, const char *hal_property)
{
	DBusError error;
	int value = 0;

	dbus_error_init (&error);

	value = libhal_device_get_property_int (context, udi, hal_property, &error);
	free_dbus_error ("getting int property", &error);
	return value;
}

static gboolean
get_property_boolean (LibHalContext *context, const char *udi, const char *hal_property)
{
	DBusError error;
	dbus_bool_t hal_value;

	dbus_error_init (&error);

	hal_value = libhal_device_get_property_bool (context, udi, hal_property, &error);
	free_dbus_error ("getting int property", &error);
	return (hal_value != 0);
}

void
mpid_device_db_lookup (MPIDDevice *device)
{
	LibHalContext *context;
	char *devpath;
	char *udi;

	devpath = mpid_device_get_device_path (device);
	if (devpath == NULL) {
		device->error = MPID_ERROR_NO_DEVICE_PATH;
		return;
	}

	context = create_hal_context ();
	if (context != NULL) {
		udi = find_portable_audio_player_udi (context, device, devpath);
		if (udi != NULL) {
			device->source = MPID_SOURCE_SYSTEM;
			mpid_debug ("reading device info from hal udi %s", udi);

			device->model = get_property_string (context, udi, "storage.model");
			device->vendor = get_property_string (context, udi, "storage.vendor");
			device->drive_type = get_property_string (context, udi, "storage.drive_type");
			device->requires_eject = get_property_boolean (context, udi, "storage.requires_eject");

			device->access_protocols = get_property_list (context, udi, "portable_audio_player.access_method.protocols");

			device->output_formats = get_property_list (context, udi, "portable_audio_player.output_formats");
			device->input_formats = get_property_list (context, udi, "portable_audio_player.input_formats");
			device->playlist_formats = get_property_list (context, udi, "portable_audio_player.playlist_format");

			/* need to try both string and strlist for playlist path */
			device->playlist_path = get_property_string (context, udi, "portable_audio_player.playlist_path");
			if (device->playlist_path == NULL) {
				char **pp = get_property_list (context, udi, "portable_audio_player.playlist_path");
				if (pp != NULL) {
					device->playlist_path = g_strdup (pp[0]);
					g_strfreev (pp);
				}
			}

			device->audio_folders = get_property_list (context, udi, "portable_audio_player.audio_folders");
			device->folder_depth = get_property_int (context, udi, "portable_audio_player.folder_depth");
		} else {
			device->error = MPID_ERROR_NOT_MEDIA_PLAYER;
		}
	} else {
		device->error = MPID_ERROR_MECHANISM_FAILED;
	}

	destroy_hal_context (context);

	g_free (devpath);
}
