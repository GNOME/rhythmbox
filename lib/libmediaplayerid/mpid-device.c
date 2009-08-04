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

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>

#include "mediaplayerid.h"
#include "mpid-private.h"


enum
{
	PROP_0,
	PROP_INPUT_PATH,
	PROP_ERROR,
	PROP_SOURCE,
	PROP_MODEL,
	PROP_VENDOR,
	PROP_DRIVE_TYPE,
	PROP_REQUIRES_EJECT,
	PROP_ACCESS_PROTOCOLS,
	PROP_OUTPUT_FORMATS,
	PROP_INPUT_FORMATS,
	PROP_PLAYLIST_FORMATS,
	PROP_PLAYLIST_PATH,
	PROP_AUDIO_FOLDERS,
	PROP_FOLDER_DEPTH
};

static void	mpid_device_class_init (MPIDDeviceClass *klass);
static void	mpid_device_init (MPIDDevice *config);

G_DEFINE_TYPE (MPIDDevice, mpid_device, G_TYPE_OBJECT)


void
mpid_device_debug (MPIDDevice *device, const char *what)
{
	mpid_debug ("device information (%s)\n", what);
	switch (device->source) {
	case MPID_SOURCE_NONE:
		mpid_debug ("no information source\n");
		break;
	case MPID_SOURCE_SYSTEM:
		mpid_debug ("information read from system device database\n");
		break;
	case MPID_SOURCE_OVERRIDE:
		mpid_debug ("information read from device override file\n");
		break;
	}
	mpid_debug_str ("model", device->model);
	mpid_debug_str ("vendor", device->vendor);
	mpid_debug_str ("drive type", device->drive_type);
	mpid_debug ("requires eject: %s\n", device->requires_eject ? "true" : "false");
	mpid_debug_strv ("access protocols", device->access_protocols);
	mpid_debug_strv ("output formats", device->output_formats);
	mpid_debug_strv ("input formats", device->input_formats);
	mpid_debug_strv ("playlist formats", device->playlist_formats);
	mpid_debug_str ("playlist path", device->playlist_path);
	mpid_debug_strv ("audio folders", device->audio_folders);
	mpid_debug ("folder depth: %d\n", device->folder_depth);
}

char *
mpid_device_get_mount_point (MPIDDevice *device)
{
	char *mount_path = NULL;
	GUnixMountEntry *mount;
	GList *mounts;
	GList *i;

	if (device->input_path == NULL) {
		mpid_debug ("no input path specified, can't find mount point");
		return NULL;
	}

	mount = g_unix_mount_at (device->input_path, NULL);
	if (mount != NULL) {
		/* path is the mount point */
		g_unix_mount_free (mount);
		mpid_debug ("%s is already a mount point\n", device->input_path);
		return g_strdup (device->input_path);
	}

	mounts = g_unix_mounts_get (NULL);
	for (i = mounts; i != NULL; i = i->next) {
		mount = i->data;

		if (g_str_equal (g_unix_mount_get_device_path (mount), device->input_path)) {
			mount_path = g_strdup (g_unix_mount_get_mount_path (mount));
			mpid_debug ("found mount point %s for device path %s\n", mount_path, device->input_path);
		}
		g_unix_mount_free (mount);
	}
	g_list_free (mounts);

	if (mount_path == NULL) {
		mpid_debug ("unable to find mount point for device path %s\n", device->input_path);
	}

	return mount_path;
}

char *
mpid_device_get_device_path (MPIDDevice *device)
{
	GUnixMountEntry *mount;
	char *mount_path;
	char *device_path = NULL;
	GList *mounts;
	GList *i;

	if (device->input_path == NULL) {
		mpid_debug ("no input path specified, can't find device path\n");
		return NULL;
	}

	mount_path = g_strdup (device->input_path);
	if (mount_path[strlen (mount_path) - 1] == '/') {
		mount_path[strlen (mount_path) - 1] = '\0';
	}

	mount = g_unix_mount_at (mount_path, NULL);
	if (mount != NULL) {
		device_path = g_strdup (g_unix_mount_get_device_path (mount));
		g_unix_mount_free (mount);
		mpid_debug ("found device path %s for mount %s\n", device_path, mount_path);
		g_free (mount_path);
		return device_path;
	}
	g_free (mount_path);

	/* it's not a mount point, so check if it's the path to a mounted device */
	mounts = g_unix_mounts_get (NULL);
	for (i = mounts; i != NULL; i = i->next) {
		mount = i->data;

		if (g_str_equal (g_unix_mount_get_device_path (mount), mount_path)) {
			device_path = g_strdup (mount_path);
			mpid_debug ("%s is already a device path\n", device_path);
		}
		g_unix_mount_free (mount);
	}
	g_list_free (mounts);

	if (device_path == NULL) {
		mpid_debug ("unable to find device path for mount point %s\n", device->input_path);
	}

	return device_path;
}



static void
mpid_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	MPIDDevice *device = MPID_DEVICE (object);

	switch (prop_id) {
	case PROP_INPUT_PATH:
		device->input_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
mpid_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	MPIDDevice *device = MPID_DEVICE (object);

	switch (prop_id) {
	case PROP_INPUT_PATH:
		g_value_set_string (value, device->input_path);
		break;
	case PROP_ERROR:
		g_value_set_enum (value, device->error);
		break;
	case PROP_SOURCE:
		g_value_set_enum (value, device->source);
		break;
	case PROP_MODEL:
		g_value_set_string (value, device->model);
		break;
	case PROP_VENDOR:
		g_value_set_string (value, device->vendor);
		break;
	case PROP_DRIVE_TYPE:
		g_value_set_string (value, device->drive_type);
		break;
	case PROP_REQUIRES_EJECT:
		g_value_set_boolean (value, device->requires_eject);
		break;
	case PROP_ACCESS_PROTOCOLS:
		g_value_set_boxed (value, device->access_protocols);
		break;
	case PROP_OUTPUT_FORMATS:
		g_value_set_boxed (value, device->output_formats);
		break;
	case PROP_INPUT_FORMATS:
		g_value_set_boxed (value, device->input_formats);
		break;
	case PROP_PLAYLIST_FORMATS:
		g_value_set_boxed (value, device->playlist_formats);
		break;
	case PROP_PLAYLIST_PATH:
		g_value_set_string (value, device->playlist_path);
		break;
	case PROP_AUDIO_FOLDERS:
		g_value_set_boxed (value, device->audio_folders);
		break;
	case PROP_FOLDER_DEPTH:
		g_value_set_int (value, device->folder_depth);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
mpid_device_finalize (GObject *object)
{
	MPIDDevice *device = MPID_DEVICE (object);

	g_free (device->model);
	g_free (device->vendor);
	g_free (device->drive_type);

	g_strfreev (device->access_protocols);

	g_strfreev (device->output_formats);
	g_strfreev (device->input_formats);
	g_strfreev (device->playlist_formats);

	g_free (device->playlist_path);
	g_strfreev (device->audio_folders);

	G_OBJECT_CLASS (mpid_device_parent_class)->finalize (object);
}

static void
mpid_device_init (MPIDDevice *device)
{
}

static GObject *
mpid_device_constructor (GType type, guint n_properties, GObjectConstructParam *properties)
{
	GObjectClass *parent_class = G_OBJECT_CLASS (mpid_device_parent_class);
	MPIDDevice *device;

	device = MPID_DEVICE (parent_class->constructor (type, n_properties, properties));

	mpid_device_db_lookup (device);
	if (device->source == MPID_SOURCE_SYSTEM) {
		mpid_device_debug (device, "system database");
	}

	mpid_device_read_override_file (device);
	if (device->source == MPID_SOURCE_OVERRIDE) {
		mpid_device_debug (device, "override file");
	}

	return G_OBJECT (device);
}

static void
mpid_device_class_init (MPIDDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = mpid_device_constructor;
	object_class->finalize = mpid_device_finalize;
	object_class->get_property = mpid_device_get_property;
	object_class->set_property = mpid_device_set_property;

	/* install properties */
	g_object_class_install_property (object_class,
					 PROP_INPUT_PATH,
					 g_param_spec_string ("input-path",
							      "input path",
							      "Input path (either a device path or a mount point)",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ERROR,
					 g_param_spec_enum ("error",
						 	    "error",
							    "error code",
							    MPID_TYPE_ERROR,
							    MPID_ERROR_NONE,
							    G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_enum ("source",
						 	    "information source",
							    "information source",
							    MPID_TYPE_SOURCE,
							    MPID_SOURCE_NONE,
							    G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_string ("model",
							      "device model",
							      "device model name",
							      NULL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_VENDOR,
					 g_param_spec_string ("vendor",
							      "device vendor",
							      "device vendor name",
							      NULL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_DRIVE_TYPE,
					 g_param_spec_string ("drive-type",
							      "drive type",
							      "drive type",
							      NULL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_REQUIRES_EJECT,
					 g_param_spec_boolean ("requires-eject",
							       "requires eject",
							       "flag indicating whether the device requires ejection",
							       FALSE,
							       G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_ACCESS_PROTOCOLS,
					 g_param_spec_boxed ("access-protocols",
							     "access protocols",
							     "names of protocols supported by the device",
							     G_TYPE_STRV,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_OUTPUT_FORMATS,
					 g_param_spec_boxed ("output-formats",
							     "output formats",
							     "MIME types playable by the device",
							     G_TYPE_STRV,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_INPUT_FORMATS,
					 g_param_spec_boxed ("input-formats",
							     "input formats",
							     "MIME types recorded by the device",
							     G_TYPE_STRV,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_FORMATS,
					 g_param_spec_boxed ("playlist-formats",
							     "playlist formats",
							     "playlist MIME supported by the device",
							     G_TYPE_STRV,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_PATH,
					 g_param_spec_string ("playlist-path",
							      "playlist path",
							      "playlist path",
							      NULL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_AUDIO_FOLDERS,
					 g_param_spec_boxed ("audio-folders",
							     "audio folders",
							     "names of folders in which audio files are stored on the device",
							     G_TYPE_STRV,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_FOLDER_DEPTH,
					 g_param_spec_int ("folder-depth",
						 	   "folder depth",
							   "number of levels of folder nesting supported by the device",
							   -1, G_MAXINT, -1,
							   G_PARAM_READABLE));
}

MPIDDevice *
mpid_device_new (const char *path)
{
	return g_object_new (MPID_TYPE_DEVICE, "input-path", path, NULL);
}


