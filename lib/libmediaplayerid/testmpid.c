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

#include "mediaplayerid.h"

int
main (int argc, char **argv)
{
	MPIDDevice *device;

	mpid_enable_debug (TRUE);
	device = mpid_device_new (argv[1]);

	switch (device->error) {
	case MPID_ERROR_NONE:
		break;
	case MPID_ERROR_NO_DEVICE_PATH:
		g_print ("unable to get device path\n");
		break;
	case MPID_ERROR_MECHANISM_FAILED:
		g_print ("device database mechanism failed\n");
		break;
	case MPID_ERROR_NOT_MEDIA_PLAYER:
		g_print ("device is not a media player\n");
		break;
	case MPID_ERROR_DEVICE_INFO_MISSING:
		g_print ("device info is missing from database\n");
		break;
	}

	g_object_unref (device);
	return 0;
}

