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
#include <libgnomevfs/gnome-vfs.h>
#include <libmtp.h>
#include <hal/libhal.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "rb-source.h"
#include "rb-sourcelist.h"
#include "rb-mtp-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-stock-icons.h"


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

	GList *mtp_sources;

	LibHalContext *hal_context;
	DBusConnection *dbus_connection;
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

static void rb_mtp_plugin_device_added (LibHalContext *context, const char *udi);
static void rb_mtp_plugin_device_removed (LibHalContext *context, const char *udi);
static void rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin);

static RBSource* create_source_cb (RBMtpPlugin *plugin, LIBMTP_mtpdevice_t *device, const char *udi);
/*static RBSource * create_source_cb (RBRemovableMediaManager *rmm,
				    GnomeVFSVolume *volume,
				    RBMtpPlugin *plugin);*/
static void rb_mtp_plugin_eject  (GtkAction *action, RBMtpPlugin *plugin);
static void rb_mtp_plugin_rename (GtkAction *action, RBMtpPlugin *plugin);

RB_PLUGIN_REGISTER(RBMtpPlugin, rb_mtp_plugin)

static GtkActionEntry rb_mtp_plugin_actions [] =
{
	{ "MTPSourceEject", GNOME_MEDIA_EJECT, N_("_Eject"), NULL,
	  N_("Eject MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_eject) },
	{ "MTPSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename MTP-device"),
	  G_CALLBACK (rb_mtp_plugin_rename) }
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
	char *file = NULL;
	int num, i, ret;
	char **devices;
	LIBMTP_device_entry_t *entries;
	int numentries;

	plugin->shell = shell;

	g_object_get (G_OBJECT (shell),
		     "ui-manager", &uimanager,
		     NULL);

	/* ui */
	plugin->action_group = gtk_action_group_new ("MTPActions");
	gtk_action_group_set_translation_domain (plugin->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (plugin->action_group,
				      rb_mtp_plugin_actions, G_N_ELEMENTS (rb_mtp_plugin_actions),
				      plugin);
	gtk_ui_manager_insert_action_group (uimanager, plugin->action_group, 0);
	file = rb_plugin_find_file (bplugin, "mtp-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager, file, NULL);

	/*device detection*/

	rb_mtp_plugin_setup_dbus_hal_connection (plugin);

	devices = libhal_get_all_devices (plugin->hal_context, &num, NULL);
	ret = LIBMTP_Get_Supported_Devices_List (&entries, &numentries);
	if (ret == 0) {
		int  p;

		for (p = 0; p < numentries; p++) {
			for (i = 0; i < num; i++) {
				int vendor_id;
				int product_id;
				const char *tmpudi;

				tmpudi = devices[i];
				vendor_id = libhal_device_get_property_int (plugin->hal_context, tmpudi, "usb.vendor_id", NULL);
				product_id = libhal_device_get_property_int (plugin->hal_context, tmpudi, "usb.product_id", NULL);

				if (entries[p].vendor_id == vendor_id && entries[p].product_id == product_id) {
					LIBMTP_mtpdevice_t *device = LIBMTP_Get_First_Device ();
					if (device != NULL) {
						create_source_cb (plugin, device, tmpudi);
					} else {
						rb_debug ("error, could not get a hold on the device. Reset and Restart");
					}
				}
			}
		}
	} else {
		rb_debug ("Couldn't list mtp devices");
	}
	libhal_free_string_array (devices);

	g_object_unref (G_OBJECT (uimanager));
}

static void
impl_deactivate (RBPlugin *bplugin, RBShell *shell)
{
	RBMtpPlugin *plugin = RB_MTP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;
	DBusError error;

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->action_group);

	g_list_foreach (plugin->mtp_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (plugin->mtp_sources);
	plugin->mtp_sources = NULL;

	dbus_error_init (&error);
	libhal_ctx_shutdown (plugin->hal_context, &error);
	libhal_ctx_free (plugin->hal_context);
	dbus_connection_unref (plugin->dbus_connection);
	dbus_error_free (&error);

	g_object_unref (G_OBJECT (uimanager));
}

static void
rb_mtp_plugin_source_deleted (RBMtpSource *source, RBMtpPlugin *plugin)
{
	plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
}

static RBSource *
create_source_cb (RBMtpPlugin *plugin, LIBMTP_mtpdevice_t *device, const char *udi)
{
	RBSource *source;

	source = RB_SOURCE (rb_mtp_source_new (plugin->shell, device, udi));

	rb_shell_append_source (plugin->shell, source, NULL);
	plugin->mtp_sources = g_list_prepend (plugin->mtp_sources, source);

	g_signal_connect_object (G_OBJECT (source),
				"deleted", G_CALLBACK (rb_mtp_plugin_source_deleted),
				plugin, 0);

	return source;
}

static void
rb_mtp_plugin_eject (GtkAction *action,
			   RBMtpPlugin *plugin)
{
	RBSourceList *sourcelist = NULL;
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell),
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_critical ("got MTPSourceEject action for non-mtp source");
		if (source != NULL)
			g_object_unref (source);
		return;
	}

	g_object_get (plugin->shell, "sourcelist", &sourcelist, NULL);

	rb_source_delete_thyself (source);

	g_object_unref (sourcelist);
	g_object_unref (source);
}

static void
rb_mtp_plugin_rename (GtkAction *action,
			   RBMtpPlugin *plugin)
{
	RBSourceList *sourcelist = NULL;
	RBSource *source = NULL;

	g_object_get (G_OBJECT (plugin->shell),
		      "selected-source", &source,
		      NULL);
	if ((source == NULL) || !RB_IS_MTP_SOURCE (source)) {
		g_critical ("got MTPSourceEject action for non-mtp source");
		if (source != NULL)
			g_object_unref (source);
		return;
	}

	g_object_get (plugin->shell, "sourcelist", &sourcelist, NULL);

	rb_sourcelist_edit_source_name (sourcelist, source);

	g_object_unref (sourcelist);
	g_object_unref (source);
}

static void
rb_mtp_plugin_device_added (LibHalContext *context, const char *udi)
{
	RBMtpPlugin *plugin = (RBMtpPlugin *) libhal_ctx_get_user_data (context);
	LIBMTP_device_entry_t *entries;
	int numentries;
	int vendor_id;
	int product_id;
	int ret;

	if (g_list_length (plugin->mtp_sources) > 0) {
		rb_debug ("plugin only supports one device at the time right now.");
		return;
	}

	vendor_id = libhal_device_get_property_int (context, udi, "usb.vendor_id", NULL);
	product_id = libhal_device_get_property_int (context, udi, "usb.product_id", NULL);

	ret = LIBMTP_Get_Supported_Devices_List (&entries, &numentries);
	if (ret == 0) {
		int i, p;

		for (i = 0; i < numentries; i++) {
			if ((entries[i].vendor_id==vendor_id) && (entries[i].product_id == product_id)) {
				/*
				 * FIXME:
				 *
				 * It usualy takes a while for the device o set itself up.
				 * Solving that by trying 10 times with some sleep in between.
				 * There is probably a better solution, but this works.
				 */
				rb_debug ("adding device source");
				for (p = 0; p < 10; p++) {
					LIBMTP_mtpdevice_t *device = LIBMTP_Get_First_Device ();
					if (device != NULL) {
						create_source_cb (plugin, device, udi);
						break;
					}
					usleep (200000);
				}
			}
	}
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
		if ((source != NULL) && (rb_mtp_source_is_udi (RB_MTP_SOURCE (source), udi) != 0)) {
			rb_debug ("removing device %s, %p", udi, source);
			plugin->mtp_sources = g_list_remove (plugin->mtp_sources, source);
			rb_source_delete_thyself (source);
			break;
		}
	}
}

static void
rb_mtp_plugin_setup_dbus_hal_connection (RBMtpPlugin *plugin)
{
	DBusError error;

	dbus_error_init (&error);
	plugin->dbus_connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (plugin->dbus_connection == NULL) {
		rb_debug ("error: dbus_bus_get: %s: %s\n", error.name, error.message);
		return;
	}

	dbus_connection_setup_with_g_main (plugin->dbus_connection, NULL);

	rb_debug ("connected to: %s", dbus_bus_get_unique_name (plugin->dbus_connection));

	plugin->hal_context = libhal_ctx_new ();
	if (plugin->hal_context == NULL) {
		return;
	}
	libhal_ctx_set_dbus_connection (plugin->hal_context, plugin->dbus_connection);

	libhal_ctx_set_user_data (plugin->hal_context, (void *)plugin);
	libhal_ctx_set_device_added (plugin->hal_context, rb_mtp_plugin_device_added);
	libhal_ctx_set_device_removed (plugin->hal_context, rb_mtp_plugin_device_removed);
	libhal_device_property_watch_all (plugin->hal_context, &error);

	if (!libhal_ctx_init (plugin->hal_context, &error)) {
		rb_debug ("error: libhal_ctx_init: %s: %s\n", error.name, error.message);
		dbus_error_free (&error);
		return;
	}

	dbus_error_free (&error);
}

