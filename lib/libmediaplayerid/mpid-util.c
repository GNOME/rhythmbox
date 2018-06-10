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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib-object.h>

#include "mediaplayerid.h"
#include "mpid-private.h"

static gboolean debug_enabled = FALSE;

#define ENUM_ENTRY(name, nick) { name, "" #name "", nick }

/**
 * MPIDError:
 * @MPID_ERROR_NONE: Indicates no error has occurred
 * @MPID_ERROR_NO_DEVICE_PATH: Unable to find the device path
 * @MPID_ERROR_MECHANISM_FAILED: The device detection mechanism (e.g. udev) failed
 * @MPID_ERROR_NOT_MEDIA_PLAYER: The device is not a media player
 * @MPID_ERROR_DEVICE_INFO_MISSING: The device detection mechanism identified the device
 *   but was unable to locate its device information
 */

GType
mpid_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY(MPID_ERROR_NONE, "OK"),
			ENUM_ENTRY(MPID_ERROR_NO_DEVICE_PATH, "no-such-device"),
			ENUM_ENTRY(MPID_ERROR_MECHANISM_FAILED, "device-db-failed"),
			ENUM_ENTRY(MPID_ERROR_NOT_MEDIA_PLAYER, "not-media-player"),
			ENUM_ENTRY(MPID_ERROR_DEVICE_INFO_MISSING, "device-info-missing"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MPIDError", values);
	}

	return etype;
}

/**
 * MPIDSource:
 * @MPID_SOURCE_NONE: No device information is available
 * @MPID_SOURCE_SYSTEM: Device information provided by the operating system (e.g. udev)
 * @MPID_SOURCE_OVERRIDE: Device information provided by an override file on the device itself.
 */

GType
mpid_source_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY(MPID_SOURCE_NONE, "no-device-info"),
			ENUM_ENTRY(MPID_SOURCE_SYSTEM, "system-device-info"),
			ENUM_ENTRY(MPID_SOURCE_OVERRIDE, "override-device-info"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MPIDSourceType", values);
	}

	return etype;
}

/**
 * mpid_enable_debug:
 * @debug: whether to enable debug output
 *
 * Enables or disables debug output from the MPID library
 */
void
mpid_enable_debug (gboolean debug)
{
	debug_enabled = debug;
}

void
mpid_debug (const char *format, ...)
{
	va_list args;
	va_start (args, format);
	if (debug_enabled)
		g_vprintf (format, args);
	va_end (args);
}

void
mpid_debug_strv (const char *what, char **strv)
{
	int i;
	if (strv != NULL) {
		mpid_debug ("%s:\n", what);
		for (i = 0; strv[i] != NULL; i++) {
			mpid_debug ("\t%s\n", strv[i]);
		}
	} else {
		mpid_debug ("%s: (none)\n", what);
	}
}

void
mpid_debug_str (const char *what, const char *str)
{
	if (str != NULL) {
		mpid_debug ("%s: %s\n", what, str);
	} else {
		mpid_debug ("%s: (none)\n", what);
	}
}

static GKeyFile *
read_fake_keyfile (const char *path)
{
	const char *fake_group = "[mpid-data]\n";
	char *data;
	char *munged;
	gsize data_size;
	gsize munged_data_size;
	GKeyFile *keyfile;
	GError *error = NULL;

	if (g_file_get_contents (path, &data, &data_size, &error) == FALSE) {
		mpid_debug ("unable to read contents of file %s: %s\n", path, error->message);
		g_clear_error (&error);
		return NULL;
	}

	/* prepend a group name to the file contents */
	munged_data_size = data_size + strlen (fake_group);
	munged = g_malloc0 (munged_data_size + 1);
	strcpy (munged, fake_group);
	memcpy (munged + strlen (fake_group), data, data_size);

	keyfile = g_key_file_new ();
	if (g_key_file_load_from_data (keyfile, munged, munged_data_size, G_KEY_FILE_NONE, &error) == FALSE) {
		mpid_debug ("unable to parse contents of file %s: %s\n", path, error->message);
		g_key_file_free (keyfile);
		keyfile = NULL;

		/* probably do something with this error too */
		g_clear_error (&error);
	}

	g_free (munged);
	return keyfile;
}

void
mpid_override_string_from_keyfile (char **str, GKeyFile *keyfile, const char *group, const char *key)
{
	char *v;
	v = g_key_file_get_string (keyfile, group, key, NULL);
	if (v != NULL) {
		g_free (*str);
		*str = v;
	}
}

void
mpid_override_strv_from_keyfile (char ***strv, GKeyFile *keyfile, const char *group, const char *key)
{
	char **v;
	v = g_key_file_get_string_list (keyfile, group, key, NULL, NULL);
	if (v != NULL) {
		g_strfreev (*strv);
		*strv = v;
	}
}

void
mpid_device_read_override_file (MPIDDevice *device)
{
	GKeyFile *keyfile;
	GError *error = NULL;
	char *mountpoint;
	char *override_path;
	char *start_group;
	char *str;
	int val;

	mountpoint = mpid_device_get_mount_point (device);
	if (mountpoint == NULL) {
		/* maybe set an error if not already set? */
		return;
	}

	override_path = g_build_filename (mountpoint, ".audio_player.mpi", NULL);
	if (g_file_test (override_path, G_FILE_TEST_EXISTS)) {
		mpid_debug ("found override file %s on mount %s\n", override_path, mountpoint);

		device->error = MPID_ERROR_NONE;
		mpid_read_device_file (device, override_path);
		device->source = MPID_SOURCE_OVERRIDE;
		g_free (override_path);
		g_free (mountpoint);
		return;
	}

	override_path = g_build_filename (mountpoint, ".is_audio_player", NULL);
	if (g_file_test (override_path, G_FILE_TEST_EXISTS) == FALSE) {
		mpid_debug ("override file %s not found on mount %s\n", override_path, mountpoint);
		g_free (override_path);
		g_free (mountpoint);
		return;
	}

	keyfile = read_fake_keyfile (override_path);
	g_free (override_path);
	g_free (mountpoint);

	if (keyfile == NULL) {
		/* maybe set an error? */
		return;
	}

	/* forget any previous error */
	device->error = MPID_ERROR_NONE;
	device->source = MPID_SOURCE_OVERRIDE;

	/* ensure we at least have 'storage' protocol and mp3 output.
	 * for mp3-only devices with no playlists, an empty override file should suffice.
	 */
	if (device->access_protocols == NULL) {
		char *p[] = { MPID_PROTOCOL_GENERIC, NULL };
		device->access_protocols = g_strdupv (p);
	}

	if (device->output_formats == NULL) {
		char *f[] = { "audio/mpeg", NULL };
		device->output_formats = g_strdupv (f);
	}

	/* now apply information from the override file */
	start_group = g_key_file_get_start_group (keyfile);
	g_key_file_set_list_separator (keyfile, ',');

	mpid_override_strv_from_keyfile (&device->output_formats, keyfile, start_group, "output_formats");
	mpid_override_strv_from_keyfile (&device->input_formats, keyfile, start_group, "input_formats");
	mpid_override_strv_from_keyfile (&device->playlist_formats, keyfile, start_group, "playlist_formats");
	mpid_override_strv_from_keyfile (&device->audio_folders, keyfile, start_group, "audio_folders");

	str = g_key_file_get_string (keyfile, start_group, "playlist_path", NULL);
	if (str != NULL) {
		g_free (device->playlist_path);
		device->playlist_path = str;
	}

	val = g_key_file_get_integer (keyfile, start_group, "folder_depth", &error);
	if (error == NULL) {
		device->folder_depth = val;
	} else {
		g_clear_error (&error);
	}

	g_key_file_free (keyfile);
}

