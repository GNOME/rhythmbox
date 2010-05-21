/*
 * rb-mtp-plugin.c
 *
 * Copyright (C) 2006 Peter Grundström <pete@openfestis.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <libmtp.h>

#if defined(HAVE_GUDEV)
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#else
#include <hal/libhal.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#endif

#include "rb-source.h"
#include "rb-sourcelist.h"
#include "rb-mtp-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-stock-icons.h"
#include "rb-removable-media-manager.h"


#define RB_TYPE_MTP_PLUGIN		(rb_mtp_plugin_get_type ())
#define RB_MTP_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MTP_PLUGIN, RBMtpPlugin))
#define RB_MTP_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_PLUGIN, RBMtpPluginClass))
#define RB_IS_MTP_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MTP_PLUGIN))
#define RB_IS_MTP_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MTP_PLUGIN))
#define RB_MTP_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MTP_PLUGIN, RBMtpPluginClass))


typedef struct
{
	RBPlugin parent;

	RBShell *shell;
	GtkActionGroup *action_group;
	guint ui_merge_id;

	guint create_device_source_id;

	GList *mtp_sources;

#if !defined(HAVE_GUDEV)
	LibHalContext *hal_context;
	DBusConnection *dbus_connection;
#endif
} RBMtpPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBMtpPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType rb_mtp_plugin_get_type (void) G_GNUC_CONST;

static void rb_mtp_plugin_init (RBMtpPlugin *plugin);
static void rb_mtp_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

#if defined(HAVE_GUDEV)
static RBSource* create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device, RBMtpPlugin *plugin);
#else
static void rb_mtp_plugin_maybe_add_source (RBMtpPlugin *plugin, const char *udi, LIBMTP_raw_device_t *raw_devices, int num);
static void rb_mtp_plugin_device_added (LibHalContext *context, const char *udi);
static void rb_mtp_plugin_device_removed (LibHalContext *context, const char *udi);
static gboolean rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin);
#endif

static void rb_mtp_plugin_rename (GtkAction *action, RBSource *source);
static void rb_mtp_plugin_properties (GtkAction *action, RBSource *source);

GType rb_mtp_src_get_type (void);
GType rb_mtp_sink_get_type (void);

RB_PLUGIN_REGISTER(RBMtpPlugin, rb_mtp_plugin)

static GtkActionEntry rb_mtp_plugin_actions [] =
{
	{ "MTPSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_rename) },
	{ "MTPSourceProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Display device properties"),
	  G_CALLBACK (rb_mtp_plugin_properties) }
};

static void
rb_mtp_plugin_class_init (RBMtpPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_mtp_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	/* register types used by the plugin */
	RB_PLUGIN_REGISTER_TYPE (rb_mtp_source);

	/* ensure the gstreamer elements get linked in */
	rb_mtp_src_get_type ();
	rb_mtp_sink_get_type ();
}

static void
rb_mtp_plugin_init (RBMtpPlugin *plugin)
{
	rb_debug ("RBMtpPlugin initialising");
	LIBMTP_Init ();
}

static void
rb_mtp_plugin_finalize (GObject *object)
{
	rb_debug ("RBMtpPlugin finalising");

	G_OBJECT_CLASS (rb_mtp_plugin_parent_class)->finalize (object);
}

static void
impl_activate (RBPlugin *bplugin, RBShell *shell)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;
	RBRemovableMediaManager *rmm;
	char *file = NULL;
#if defined(HAVE_GUDEV)
	gboolean rmm_scanned = FALSE;
#else
	int num_mtp_devices;
	LIBMTP_raw_device_t *mtp_devices;
#endif

	plugin->shell = shell;

	g_object_get (shell,
		     "ui-manager", &uimanager,
		     "removable-media-manager", &rmm,
		     NULL);

	/* ui */
	rb_media_player_source_init_actions (shell);
	plugin->action_group = gtk_action_group_new ("MTPActions");
	gtk_action_group_set_translation_domain (plugin->action_group,
						 GETTEXT_PACKAGE);
	_rb_action_group_add_source_actions (plugin->action_group,
					     G_OBJECT (plugin->shell),
					     rb_mtp_plugin_actions,
					     G_N_ELEMENTS (rb_mtp_plugin_actions));
	gtk_ui_manager_insert_action_group (uimanager, plugin->action_group, 0);
	file = rb_plugin_find_file (bplugin, "mtp-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager, file, NULL);
	g_object_unref (uimanager);

	/* device detection */
#if defined(HAVE_GUDEV)
	plugin->create_device_source_id =
		g_signal_connect_object (rmm,
					 "create-source-device",
					 G_CALLBACK (create_source_device_cb),
					 plugin,
					 0);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (rmm, "scanned", &rmm_scanned, NULL);
	if (rmm_scanned)
		rb_removable_media_manager_scan (rmm);
#else
	if (rb_mtp_plugin_setup_dbus_hal_connection (plugin) == FALSE) {
		rb_debug ("not scanning for MTP devices because we couldn't get a HAL context");
		g_object_unref (rmm);
		return;
	}

	rb_profile_start ("scanning for MTP devices");
	LIBMTP_Detect_Raw_Devices (&mtp_devices, &num_mtp_devices);
	if (num_mtp_devices > 0) {
		int num_hal_devices;
		char **hal_devices;
		int i;

		rb_debug ("%d MTP devices found", num_mtp_devices);

		hal_devices = libhal_get_all_devices (plugin->hal_context, &num_hal_devices, NULL);
		for (i = 0; i < num_hal_devices; i++) {
			/* should narrow this down a bit - usb only, for a start */
			rb_mtp_plugin_maybe_add_source (plugin, hal_devices[i], mtp_devices, num_mtp_devices);
		}
		libhal_free_string_array (hal_devices);
	}
	if (mtp_devices != NULL) {
		free (mtp_devices);
	}
	rb_profile_end ("scanning for MTP devices");
#endif

	g_object_unref (rmm);
}

static void
impl_deactivate (RBPlugin *bplugin, RBShell *shell)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;
	RBRemovableMediaManager *rmm = NULL;

	g_object_get (shell,
		      "ui-manager", &uimanager,
		      "removable-media-manager", &rmm,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->action_group);

	g_list_foreach (plugin->mtp_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (plugin->mtp_sources);
	plugin->mtp_sources = NULL;

#if defined(HAVE_GUDEV)
	g_signal_handler_disconnect (rmm, plugin->create_device_source_id);
	plugin->create_device_source_id = 0;
#else
	if (plugin->hal_context != NULL) {
		DBusError error;
		dbus_error_init (&error);
		libhal_ctx_shutdown (plugin->hal_context, &error);
		libhal_ctx_free (plugin->hal_context);
		dbus_error_free (&error);

		plugin->hal_context = NULL;
	}

	if (plugin->dbus_connection != NULL) {
		dbus_connection_unref (plugin->dbus_connection);
		plugin->dbus_connection = NULL;
	}
#endif

	g_object_unref (uimanager);
	g_object_unref (rmm);
}

static void
rb_mtp_plugin_rename (GtkAction *action, RBSource *source)
{
	RBShell *shell;
	RBSourceList *sourcelist;

	g_return_if_fail (RB_IS_MTP_SOURCE (source));

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "sourcelist", &sourcelist, NULL);

	rb_sourcelist_edit_source_name (sourcelist, source);

	g_object_unref (sourcelist);
	g_object_unref (shell);
}

static void
rb_mtp_plugin_properties (GtkAction *action, RBSource *source)
{
	g_return_if_fail (RB_IS_MTP_SOURCE (source));
	rb_media_player_source_show_properties (RB_MEDIA_PLAYER_SOURCE (source));
}


#if defined(HAVE_GUDEV)
static void
source_deleted_cb (RBMtpSource *source, RBMtpPlugin *plugin)
{
	plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
}

static RBSource *
create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device_obj, RBMtpPlugin *plugin)
{
	GUdevDeviceNumber device_number;
	GUdevDevice *device = G_UDEV_DEVICE (device_obj);
	int i;
	int num_raw_devices;
	const char *devnum_str;
	int devnum;
	LIBMTP_raw_device_t *raw_devices;

	/* check subsystem == usb? */
	if (g_strcmp0 (g_udev_device_get_subsystem (device), "usb") != 0) {
		rb_debug ("device %s is not a USB device", g_udev_device_get_name (device));
		return NULL;
	}

	/* check that it's not an iPhone or iPod Touch */
	if (g_udev_device_get_property_as_boolean (device, "USBMUX_SUPPORTED")) {
		rb_debug ("device %s is supported through AFC, ignore", g_udev_device_get_name (device));
		return NULL;
	}

	device_number = g_udev_device_get_device_number (device);
	if (device_number == 0) {
		rb_debug ("can't get udev device number for device %s", g_udev_device_get_name (device));
		return NULL;
	}
	/* fun thing: usb device numbers are zero padded, which causes strtol to
	 * interpret them as octal if you don't specify a base.
	 */
	devnum_str = g_udev_device_get_property (device, "DEVNUM");
	if (devnum_str == NULL) {
		rb_debug ("device %s doesn't have a USB device number", g_udev_device_get_name (device));
		return NULL;
	}
	devnum = strtol (devnum_str, NULL, 10);

	rb_debug ("trying to match device %"G_GINT64_MODIFIER"x (usb device %d) against detected mtp devices",
		  (guint64)device_number, devnum);

	/* see what devices libmtp can find */
	if (LIBMTP_Detect_Raw_Devices (&raw_devices, &num_raw_devices) == 0) {
		for (i = 0; i < num_raw_devices; i++) {
			RBSource *source;

			rb_debug ("detected mtp device: device number %d", raw_devices[i].devnum);
			if (devnum != raw_devices[i].devnum) {
				rb_debug ("device number mismatches: %d vs %d", devnum, raw_devices[i].devnum);
				continue;
			}

			rb_debug ("device matched, creating a source");
			source = rb_mtp_source_new (plugin->shell, RB_PLUGIN (plugin), device, &raw_devices[i]);

			plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);
			g_signal_connect_object (G_OBJECT (source),
						"deleted", G_CALLBACK (source_deleted_cb),
						plugin, 0);
			free (raw_devices);
			return source;
		}
	}
	free (raw_devices);

	rb_debug ("device didn't match anything");
	return NULL;
}

#else

static void
source_deleted_cb (RBMtpSource *source, RBMtpPlugin *plugin)
{
	plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
}

static void
rb_mtp_plugin_maybe_add_source (RBMtpPlugin *plugin, const char *udi, LIBMTP_raw_device_t *raw_devices, int num_raw_devices)
{
	int i;
	int device_num = 0;
	DBusError error;

	rb_debug ("checking if UDI %s matches an MTP device", udi);

	/* get device number */
	dbus_error_init (&error);
	device_num = libhal_device_get_property_int (plugin->hal_context, udi, "usb.linux.device_number", &error);
	if (dbus_error_is_set (&error)) {
		rb_debug ("unable to get USB device number: %s", error.message);
		dbus_error_free (&error);
		return;
	}

	rb_debug ("USB device number: %d", device_num);

	for (i = 0; i < num_raw_devices; i++) {
		rb_debug ("detected MTP device: device number %d (bus location %u)", raw_devices[i].devnum, raw_devices[i].bus_location);
		if (raw_devices[i].devnum == device_num) {
			RBSource *source;

			rb_debug ("device matched, creating a source");
			source = RB_SOURCE (rb_mtp_source_new (plugin->shell, RB_PLUGIN (plugin), udi, &raw_devices[i]));

			rb_shell_append_source (plugin->shell, source, NULL);
			plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);
			g_signal_connect_object (source,
						"deleted", G_CALLBACK (source_deleted_cb),
						plugin, 0);
		}
	}
}

static void
rb_mtp_plugin_device_added (LibHalContext *context, const char *udi)
{
	RBMtpPlugin *plugin = (RBMtpPlugin *) libhal_ctx_get_user_data (context);
	LIBMTP_raw_device_t *mtp_devices;
	int num_mtp_devices;

	LIBMTP_Detect_Raw_Devices (&mtp_devices, &num_mtp_devices);
	if (mtp_devices != NULL) {
		rb_mtp_plugin_maybe_add_source (plugin, udi, mtp_devices, num_mtp_devices);
		free (mtp_devices);
	}
}

static void
rb_mtp_plugin_device_removed (LibHalContext *context, const char *udi)
{
	RBMtpPlugin *plugin = (RBMtpPlugin *) libhal_ctx_get_user_data (context);
	GList *list = plugin->mtp_sources;
	GList *tmp;

	for (tmp = list; tmp != NULL; tmp = tmp->next) {
		RBSource *source = (RBSource *)tmp->data;
		char *source_udi;

		g_object_get (source, "udi", &source_udi, NULL);
		if (strcmp (udi, source_udi) == 0) {
			rb_debug ("removing device %s, %p", udi, source);
			plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
			rb_source_delete_thyself (source);
		}
		g_free (source_udi);
	}
}

static gboolean
rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin)
{
	DBusError error;

	dbus_error_init (&error);
	plugin->dbus_connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (plugin->dbus_connection == NULL) {
		rb_debug ("error: dbus_bus_get: %s: %s\n", error.name, error.message);
		dbus_error_free (&error);
		return FALSE;
	}

	dbus_connection_setup_with_g_main (plugin->dbus_connection, NULL);

	rb_debug ("connected to: %s", dbus_bus_get_unique_name (plugin->dbus_connection));

	plugin->hal_context = libhal_ctx_new ();
	if (plugin->hal_context == NULL) {
		dbus_error_free (&error);
		return FALSE;
	}
	libhal_ctx_set_dbus_connection (plugin->hal_context, plugin->dbus_connection);

	libhal_ctx_set_user_data (plugin->hal_context, (void *)plugin);
	libhal_ctx_set_device_added (plugin->hal_context, rb_mtp_plugin_device_added);
	libhal_ctx_set_device_removed (plugin->hal_context, rb_mtp_plugin_device_removed);
	libhal_device_property_watch_all (plugin->hal_context, &error);

	if (!libhal_ctx_init (plugin->hal_context, &error)) {
		rb_debug ("error: libhal_ctx_init: %s: %s\n", error.name, error.message);
		dbus_error_free (&error);
		return FALSE;
	}

	dbus_error_free (&error);
	return TRUE;
}

#endif
