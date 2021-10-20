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
#include <glib-object.h>

#ifndef MEDIA_PLAYER_ID__H
#define MEDIA_PLAYER_ID__H

G_BEGIN_DECLS

#define MPID_PROTOCOL_IPOD		"ipod"
#define MPID_PROTOCOL_GENERIC		"storage"

/* enum types */

typedef enum {
	MPID_ERROR_NONE,
	MPID_ERROR_NO_DEVICE_PATH,			/* unable to find the device path */
	MPID_ERROR_MECHANISM_FAILED,			/* mechanism (udev) not available */
	MPID_ERROR_NOT_MEDIA_PLAYER,			/* device is not a media player */
	MPID_ERROR_DEVICE_INFO_MISSING			/* the device info file is missing */
} MPIDError;

typedef enum {
	MPID_SOURCE_NONE,
	MPID_SOURCE_SYSTEM,
	MPID_SOURCE_OVERRIDE
} MPIDSource;

GType mpid_error_get_type (void);
GType mpid_source_get_type (void);

#define MPID_TYPE_ERROR (mpid_error_get_type ())
#define MPID_TYPE_SOURCE (mpid_source_get_type ())


/* device object */

#define MPID_TYPE_DEVICE		(mpid_device_get_type ())
#define MPID_DEVICE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), MPID_TYPE_DEVICE, MPIDDevice))
#define MPID_DEVICE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), MPID_TYPE_DEVICE, MPIDDeviceClass))
#define MPID_IS_DEVICE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), MPID_TYPE_DEVICE))
#define MPID_IS_DEVICE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), MPID_TYPE_DEVICE))
#define MPID_DEVICE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), MPID_TYPE_DEVICE, MPIDDeviceClass))

typedef struct _MPIDDevice MPIDDevice;
typedef struct _MPIDDeviceClass MPIDDeviceClass;

struct _MPIDDeviceClass
{
	/*< private >*/
	GObjectClass parent_class;
};

struct _MPIDDevice
{
	/*< private >*/
	GObject parent;

	char *input_path;
	char *mpi_file;

	MPIDError error;
	MPIDSource source;

	char *model;
	char *vendor;
	char *fs_uuid;
	char *serial;
	char *drive_type;
	gboolean requires_eject;

	char **access_protocols;

	char **output_formats;
	char **input_formats;
	char **playlist_formats;

	char *playlist_path;
	char **audio_folders;
	int folder_depth;
};

GType			mpid_device_get_type (void);

void			mpid_enable_debug (gboolean debug);

MPIDDevice *		mpid_device_new (const char *path);
MPIDDevice *		mpid_device_new_from_mpi_file (const char *path);

G_END_DECLS

#endif /* MEDIA_PLAYER_ID__H */
