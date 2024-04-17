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

#include <config.h>

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <libmtp.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "rb-plugin-macros.h"
#include "rb-source.h"
#include "rb-display-page-group.h"
#include "rb-display-page-tree.h"
#include "rb-mtp-source.h"
#include "rb-mtp-thread.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-removable-media-manager.h"
#include "rb-mtp-gst.h"


#define RB_TYPE_MTP_PLUGIN		(rb_mtp_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBMtpPlugin, rb_mtp_plugin, RB, MTP_PLUGIN, PeasExtensionBase)


struct _RBMtpPlugin
{
	PeasExtensionBase parent;

	guint create_device_source_id;

	GList *mtp_sources;
};

struct _RBMtpPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_mtp_plugin_init (RBMtpPlugin *plugin);

static RBSource* create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device, RBMtpPlugin *plugin);

GType rb_mtp_src_get_type (void);
GType rb_mtp_sink_get_type (void);

RB_DEFINE_PLUGIN(RB_TYPE_MTP_PLUGIN, RBMtpPlugin, rb_mtp_plugin,)

static void
rb_mtp_plugin_init (RBMtpPlugin *plugin)
{
	rb_debug ("RBMtpPlugin initialising");
	LIBMTP_Init ();
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm;
	RBShell *shell;
	gboolean rmm_scanned = FALSE;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "removable-media-manager", &rmm, NULL);
	g_object_unref (shell);

	/* device detection */
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

	g_object_unref (rmm);
}

static void
impl_deactivate (PeasActivatable *bplugin)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      NULL);

	g_list_foreach (plugin->mtp_sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (plugin->mtp_sources);
	plugin->mtp_sources = NULL;

	g_signal_handler_disconnect (rmm, plugin->create_device_source_id);
	plugin->create_device_source_id = 0;

	g_object_unref (rmm);
	g_object_unref (shell);
}

static void
source_deleted_cb (RBMtpSource *source, RBMtpPlugin *plugin)
{
	plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
}

static int
get_property_as_int (GUdevDevice *device, const char *property, int base)
{
	const char *strvalue;

	strvalue = g_udev_device_get_property (device, property);
	if (strvalue == NULL) {
		return 0;
	}

	return strtol (strvalue, NULL, base);
}

static RBSource *
create_source_device_cb (RBRemovableMediaManager *rmm, GObject *device_obj, RBMtpPlugin *plugin)
{
	GUdevDevice *device = G_UDEV_DEVICE (device_obj);
	LIBMTP_device_entry_t *device_list;
	int numdevs;
	int vendor;
	int model;
	int busnum;
	int devnum;
	int i;

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

	/* check that it's not an android device */
	if (rb_removable_media_manager_device_is_android (rmm, device_obj)) {
		rb_debug ("device %s is android based, android plugin should handle it",
			  g_udev_device_get_name (device));
		return NULL;
	}

	/* get device info */
	vendor = get_property_as_int (device, "ID_VENDOR_ID", 16);
	model = get_property_as_int (device, "ID_MODEL_ID", 16);
	busnum = get_property_as_int (device, "BUSNUM", 10);
	devnum = get_property_as_int (device, "DEVNUM", 10);
	if (vendor == 0 || model == 0) {
		rb_debug ("couldn't get vendor or model ID for device (%x:%x)", vendor, model);
		return NULL;
	}

	rb_debug ("matching device %x:%x against libmtp device list", vendor, model);
	LIBMTP_Get_Supported_Devices_List(&device_list, &numdevs);
	for (i = 0; i < numdevs; i++) {
		if (device_list[i].vendor_id == vendor &&
		    device_list[i].product_id == model) {
			LIBMTP_raw_device_t rawdevice;
			RBSource *source;
			RBShell *shell;

			rb_debug ("found libmtp device list entry (vendor: %s, model: %s)",
				  device_list[i].vendor, device_list[i].product);

			rawdevice.device_entry = device_list[i];
			rawdevice.bus_location = busnum;
			rawdevice.devnum = devnum;

			g_object_get (plugin, "object", &shell, NULL);
			source = rb_mtp_source_new (shell, G_OBJECT (plugin), device, &rawdevice);

			plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);
			g_signal_connect_object (G_OBJECT (source),
						"deleted", G_CALLBACK (source_deleted_cb),
						plugin, 0);
			g_object_unref (shell);
			return source;
		}
	}

	rb_debug ("device didn't match anything");
	return NULL;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_mtp_plugin_register_type (G_TYPE_MODULE (module));
	_rb_mtp_source_register_type (G_TYPE_MODULE (module));
	_rb_mtp_thread_register_type (G_TYPE_MODULE (module));

	rb_mtp_gst_init ();

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_MTP_PLUGIN);
}
