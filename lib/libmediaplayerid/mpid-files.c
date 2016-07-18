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

#include <glib.h>
#include <gio/gio.h>

#include "mediaplayerid.h"
#include "mpid-private.h"

static void
mpid_read_keyfile (MPIDDevice *device, GKeyFile *keyfile)
{
	GError *error = NULL;

	mpid_override_strv_from_keyfile (&device->access_protocols, keyfile, "Device", "AccessProtocol");

	mpid_override_strv_from_keyfile (&device->output_formats, keyfile, "Media", "OutputFormats");
	mpid_override_strv_from_keyfile (&device->input_formats, keyfile, "Media", "InputFormats");

	mpid_override_strv_from_keyfile (&device->playlist_formats, keyfile, "Playlist", "Formats");

	mpid_override_strv_from_keyfile (&device->audio_folders, keyfile, "storage", "AudioFolders");

	mpid_override_string_from_keyfile (&device->playlist_path, keyfile, "storage", "PlaylistPath");
	mpid_override_string_from_keyfile (&device->drive_type, keyfile, "storage", "DriveType");

	if (g_key_file_has_key (keyfile, "storage", "RequiresEject", NULL)) {
		device->requires_eject = g_key_file_get_boolean (keyfile, "storage", "RequiresEject", NULL);
	}

	mpid_override_string_from_keyfile (&device->model, keyfile, "Device", "Model");
	mpid_override_string_from_keyfile (&device->vendor, keyfile, "Vendor", "Model");

	if (g_key_file_has_key (keyfile, "storage", "FolderDepth", NULL)) {
		int val = g_key_file_get_integer (keyfile, "storage", "FolderDepth", &error);
		if (error == NULL) {
			device->folder_depth = val;
		} else {
			g_clear_error (&error);
			device->folder_depth = -1;	/* hmm. */
		}
	}

}

void
mpid_read_device_file (MPIDDevice *device, const char *device_info_path)
{
	GError *error = NULL;
	GKeyFile *keyfile;
	GBytes *bytes;
	gsize len;
	const void *data;

	keyfile = g_key_file_new ();
	bytes = g_resources_lookup_data (device_info_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
	if (bytes != NULL) {
		data = g_bytes_get_data (bytes, &len);

		if (g_key_file_load_from_data (keyfile, data, len, G_KEY_FILE_NONE, &error) == FALSE) {
			mpid_debug ("unable to read device info resource %s: %s\n", device_info_path, error->message);
			g_clear_error (&error);
			device->error = MPID_ERROR_DEVICE_INFO_MISSING;
			g_bytes_unref (bytes);
			return;
		}
	} else if (g_key_file_load_from_file (keyfile, device_info_path, G_KEY_FILE_NONE, &error) == FALSE) {
		mpid_debug ("unable to read device info file %s: %s\n", device_info_path, error->message);
		g_clear_error (&error);
		device->error = MPID_ERROR_DEVICE_INFO_MISSING;
		return;
	}

	mpid_read_keyfile (device, keyfile);
	g_key_file_free (keyfile);
}

void
mpid_find_and_read_device_file (MPIDDevice *device, const char *device_file)
{
	const char * const *data_dirs;
	int i;

	data_dirs = g_get_system_data_dirs ();
	for (i = 0; data_dirs[i] != NULL; i++) {
		char *filename;
		char *path;

		filename = g_strdup_printf ("%s.mpi", device_file);
		path = g_build_filename (data_dirs[i], "media-player-info", filename, NULL);
		g_free (filename);
		if (g_file_test (path, G_FILE_TEST_EXISTS)) {
			device->source = MPID_SOURCE_SYSTEM;
			mpid_read_device_file (device, path);
			g_free (path);
			return;
		}
		g_free (path);
	}

	/* device info file is missing */
	mpid_debug ("unable to find device info file %s\n", device_file);
	device->error = MPID_ERROR_DEVICE_INFO_MISSING;
}

