/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011  Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "rb-device-source.h"
#include "rb-source.h"
#include "rb-debug.h"
#include "rb-dialog.h"

G_DEFINE_INTERFACE (RBDeviceSource, rb_device_source, 0)

/**
 * SECTION:rbdevicesource
 * @short_description: interface for sources based on physical devices
 * @include: rb-device-source.h
 *
 * Sources that represent physical devices should implement this interface.
 * It exposes the ability to eject the device, and also can be used to
 * implement some #RBSource methods by using details from a #GVolume or
 * #GMount accessed via 'volume' and 'mount' properties on the source object.
 * Devices that are not based on a #GVolume or #GMount can still use the
 * interface, but they must provide implementations of the @can_eject and
 * @eject methods.
 */

static gboolean
default_can_eject (RBDeviceSource *source)
{
	gboolean result = FALSE;
	GVolume *volume = NULL;
	GMount *mount = NULL;

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "volume")) {
		g_object_get (source, "volume", &volume, NULL);
	}
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "mount")) {
		g_object_get (source, "mount", &mount, NULL);
	}

	if (volume != NULL) {
		result = g_volume_can_eject (volume);

		g_object_unref (volume);
		if (mount != NULL) {
			g_object_unref (mount);
		}
	} else if (mount != NULL) {
		result = g_mount_can_eject (mount) || g_mount_can_unmount (mount);

		if (mount != NULL) {
			g_object_unref (mount);
		}
	}

	return result;
}

static void
eject_cb (GObject *object, GAsyncResult *result, gpointer nothing)
{
	GError *error = NULL;

	if (G_IS_VOLUME (object)) {
		GVolume *volume = G_VOLUME (object);

		rb_debug ("finishing ejection of volume");
		g_volume_eject_with_operation_finish (volume, result, &error);
	} else if (G_IS_MOUNT (object)) {
		GMount *mount = G_MOUNT (object);

		rb_debug ("finishing ejection of mount");
		g_mount_eject_with_operation_finish (mount, result, &error);
	}

	if (error != NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
			rb_error_dialog (NULL, _("Unable to eject"), "%s", error->message);
		} else {
			rb_debug ("eject failure has already been handled");
		}
		g_error_free (error);
	}
}

static void
unmount_cb (GObject *object, GAsyncResult *result, gpointer nothing)
{
	GMount *mount = G_MOUNT (object);
	GError *error = NULL;

	rb_debug ("finishing unmount of mount");
	g_mount_unmount_with_operation_finish (mount, result, &error);
	if (error != NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
			rb_error_dialog (NULL, _("Unable to unmount"), "%s", error->message);
		} else {
			rb_debug ("unmount failure has already been handled");
		}
		g_error_free (error);
	}
}

/**
 * rb_device_source_default_eject:
 * @source: a #RBDeviceSource
 *
 * Default method for ejecting devices.  Implementations can
 * perform any required work before ejecting, then call this do
 * eject the device.
 */
void
rb_device_source_default_eject (RBDeviceSource *source)
{
	GVolume *volume = NULL;
	GMount *mount = NULL;

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "volume")) {
		g_object_get (source, "volume", &volume, NULL);
	}
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "mount")) {
		g_object_get (source, "mount", &mount, NULL);
	}

	/* try ejecting based on volume first, then based on the mount,
	 * and finally try unmounting.
	 */
	if (volume != NULL) {
		if (g_volume_can_eject (volume)) {
			rb_debug ("ejecting volume");
			g_volume_eject_with_operation (volume,
						       G_MOUNT_UNMOUNT_NONE,
						       NULL,
						       NULL,
						       (GAsyncReadyCallback) eject_cb,
						       NULL);
		} else {
			/* this should never happen; the eject command will be
			 * insensitive if the selected source cannot be ejected.
			 */
			rb_debug ("don't know what to do with this volume");
		}
	} else if (mount != NULL) {
		if (g_mount_can_eject (mount)) {
			rb_debug ("ejecting mount");
			g_mount_eject_with_operation (mount,
						      G_MOUNT_UNMOUNT_NONE,
						      NULL,
						      NULL,
						      (GAsyncReadyCallback) eject_cb,
						      NULL);
		} else if (g_mount_can_unmount (mount)) {
			rb_debug ("unmounting mount");
			g_mount_unmount_with_operation (mount,
							G_MOUNT_UNMOUNT_NONE,
							NULL,
							NULL,
							(GAsyncReadyCallback) unmount_cb,
							NULL);
		} else {
			/* this should never happen; the eject command will be
			 * insensitive if the selected source cannot be ejected.
			 */
			rb_debug ("don't know what to do with this mount");
		}
	}

	if (volume != NULL) {
		g_object_unref (volume);
	}
	if (mount != NULL) {
		g_object_unref (mount);
	}
}

/**
 * rb_device_can_eject:
 * @source: a #RBDeviceSource
 *
 * Checks if the device that the source represents can be ejected.
 *
 * Return value: %TRUE if the device can be ejected
 */
gboolean
rb_device_source_can_eject (RBDeviceSource *source)
{
	RBDeviceSourceInterface *iface = RB_DEVICE_SOURCE_GET_IFACE (source);
	return iface->can_eject (source);
}

/**
 * rb_device_source_eject:
 * @source: a #RBDeviceSource
 *
 * Ejects the device that the source represents.
 */
void
rb_device_source_eject (RBDeviceSource *source)
{
	RBDeviceSourceInterface *iface = RB_DEVICE_SOURCE_GET_IFACE (source);
	iface->eject (source);
}

/**
 * rb_device_source_want_uri:
 * @source: a #RBDeviceSource
 * @uri: a URI to consider
 *
 * Checks whether @uri identifies a path underneath the
 * device's mount point.  Should be used to implement
 * the #RBSource want_uri method.
 *
 * Return value: URI match strength
 */
guint
rb_device_source_want_uri (RBSource *source, const char *uri)
{
	GMount *mount = NULL;
	GVolume *volume = NULL;
	GFile *file;
	char *device_path, *uri_path;
	int retval;
	int len;

	retval = 0;

	file = g_file_new_for_uri (uri);

	/* Deal with the mount root being passed, eg. file:///media/IPODNAME */
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "mount")) {
		g_object_get (source, "mount", &mount, NULL);
	}
	if (mount != NULL) {
		GFile *root;

		root = g_mount_get_root (mount);
		retval = g_file_equal (root, file) ? 100 : 0;
		g_object_unref (root);
		if (retval) {
			g_object_unref (file);
			g_object_unref (mount);
			return retval;
		}
		volume = g_mount_get_volume (mount);
		g_object_unref (mount);
	} else {
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "volume")) {
			g_object_get (source, "volume", &volume, NULL);
		}
	}

	/* ignore anything that isn't a local file or doesn't have a volume */
	if (g_file_has_uri_scheme (file, "file") == FALSE || volume == NULL) {
		g_object_unref (file);
		return 0;
	}

	/* Deal with the path to the device node being passed */
	device_path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	g_object_unref (volume);
	if (device_path == NULL) {
		g_object_unref (file);
		return 0;
	}

	uri_path = g_file_get_path (file);
	g_object_unref (file);
	if (uri_path == NULL)
		return 0;
	len = strlen (uri_path);
	if (uri_path[len - 1] == '/') {
		if (strncmp (uri_path, device_path, len - 1) == 0) {
			retval = 100;
		}
	} else if (strcmp (uri_path, device_path) == 0) {
		retval = 100;
	}

	g_free (device_path);
	g_free (uri_path);
	return retval;
}

/**
 * rb_device_source_uri_is_source:
 * @source: a #RBDeviceSource
 * @uri: a URI to check
 *
 * Returns %TRUE if @uri matches @source.  This should be
 * used to implement the uri_is_source #RBSource method.
 *
 * Return value: %TRUE if @uri matches @source
 */
gboolean
rb_device_source_uri_is_source (RBSource *source, const char *uri)
{
	return (rb_device_source_want_uri (source, uri) == 100);
}

/**
 * rb_device_source_set_display_details:
 * @source: a #RBDeviceSource
 *
 * Sets the icon and display name for a device-based source.
 * The details come from the mount and/or volume.  This should
 * be called in the source's constructed method.
 */
void
rb_device_source_set_display_details (RBDeviceSource *source)
{
	GMount *mount = NULL;
	GVolume *volume = NULL;
	GIcon *icon = NULL;
	char *display_name;

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "volume")) {
		g_object_get (source, "volume", &volume, NULL);
	}
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "mount")) {
		g_object_get (source, "mount", &mount, NULL);
	}

	/* prefer mount details to volume details, as the nautilus sidebar does */
	if (mount != NULL) {
		mount = g_object_ref (mount);
	} else if (volume != NULL) {
		mount = g_volume_get_mount (volume);
	} else {
		mount = NULL;
	}

	if (mount != NULL) {
		display_name = g_mount_get_name (mount);
		icon = g_mount_get_symbolic_icon (mount);
		rb_debug ("details from mount: display name = %s, icon = %p", display_name, icon);
	} else if (volume != NULL) {
		display_name = g_volume_get_name (volume);
		icon = g_volume_get_symbolic_icon (volume);
		rb_debug ("details from volume: display name = %s, icon = %p", display_name, icon);
	} else {
		display_name = g_strdup ("Unknown Device");
		icon = g_themed_icon_new ("multimedia-player-symbolic");
	}

	g_object_set (source, "name", display_name, "icon", icon, NULL);
	g_free (display_name);

	g_clear_object (&mount);
	g_clear_object (&volume);
	g_clear_object (&icon);
}

static void
rb_device_source_default_init (RBDeviceSourceInterface *interface)
{
	interface->can_eject = default_can_eject;
	interface->eject = rb_device_source_default_eject;
}
