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

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mediaplayerid.h"
#include "mpid-private.h"

void
mpid_device_db_lookup (MPIDDevice *device)
{
	GUdevClient *client;
	GUdevDevice *udevice = NULL;
	char *devpath;
	const char *device_file;
	char *subsystems[] = { "usb", NULL };

	devpath = mpid_device_get_device_path (device);
	if (devpath == NULL) {
		device->error = MPID_ERROR_NO_DEVICE_PATH;
		return;
	}
	
	client = g_udev_client_new ((const char * const *)subsystems);
	if (client != NULL) {
		udevice = g_udev_client_query_by_device_file (client, devpath);
		if (udevice != NULL) {

			/* get vendor and model names, UUID, and serial */
			device->model = g_strdup (g_udev_device_get_property (udevice, "ID_MODEL"));
			device->vendor = g_strdup (g_udev_device_get_property (udevice, "ID_VENDOR"));
			device->fs_uuid = g_strdup (g_udev_device_get_property (udevice, "ID_FS_UUID"));
			device->serial = g_strdup (g_udev_device_get_property (udevice, "ID_SERIAL"));

			/* get media player information */
			device_file = g_udev_device_get_property (udevice, "ID_MEDIA_PLAYER");
			if (device_file != NULL) {
				mpid_debug ("found ID_MEDIA_PLAYER tag %s for device %s\n", device_file, devpath);
				mpid_find_and_read_device_file (device, device_file);
			} else {
				mpid_debug ("device %s has no ID_MEDIA_PLAYER tag in udev\n", devpath);
				device->error = MPID_ERROR_NOT_MEDIA_PLAYER;
			}
		} else {
			mpid_debug ("unable to find device %s in udev\n", devpath);
			device->error = MPID_ERROR_MECHANISM_FAILED;
		}
	} else {
		mpid_debug ("unable to create udev client\n");
		device->error = MPID_ERROR_MECHANISM_FAILED;
	}

	g_free (devpath);
	if (udevice != NULL) {
		g_object_unref (udevice);
	}
	if (client != NULL) {
		g_object_unref (client);
	}
}

