/*
 * rb-android-plugin.c
 *
 * Copyright (C) 2006  Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <gudev/gudev.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-file-helpers.h"
#include "rb-display-page-tree.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"
#include "rb-android-source.h"


#define RB_TYPE_ANDROID_PLUGIN		(rb_android_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBAndroidPlugin, rb_android_plugin, RB, ANDROID_PLUGIN, PeasExtensionBase)

struct _RBAndroidPlugin
{
	PeasExtensionBase parent;

	GList *sources;
};

struct _RBAndroidPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule  *module);

static void rb_android_plugin_init (RBAndroidPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_ANDROID_PLUGIN, RBAndroidPlugin, rb_android_plugin,)

static void
rb_android_plugin_init (RBAndroidPlugin *plugin)
{
	rb_debug ("RBAndroidPlugin initialising");
}

static void
source_deleted_cb (RBAndroidSource *source, RBAndroidPlugin *plugin)
{
	plugin->sources = g_list_remove (plugin->sources, source);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm, GVolume *volume, RBAndroidPlugin *plugin)
{
	RBSource *source = NULL;
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *error_type;
	RhythmDBEntryType *ignore_type;
	GObject *dev;
	GUdevDevice *gudev_device;
	GtkBuilder *builder;
	GMenu *toolbar;
	GSettings *settings;
	GFile *root;
	MPIDDevice *device_info;
	const char *device_serial;
	const char *mpi_file;
	char *uri_prefix;
	char *name;
	char *path;

	dev = rb_removable_media_manager_get_gudev_device (rmm, volume);
	if (dev == NULL) {
		return NULL;
	}
	gudev_device = G_UDEV_DEVICE (dev);

	if (rb_removable_media_manager_device_is_android (rmm, G_OBJECT (gudev_device)) == FALSE) {
		g_object_unref (gudev_device);
		return NULL;
	}

	mpi_file = "/org/gnome/Rhythmbox/android/android.mpi";
	device_info = mpid_device_new_from_mpi_file (mpi_file);

	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	device_serial = g_udev_device_get_property (gudev_device, "ID_SERIAL");

	root = g_volume_get_activation_root (volume);
	uri_prefix = g_file_get_uri (root);

	rb_debug ("metadata cache mapping: %s <=> %s", uri_prefix, device_serial);

	name = g_strdup_printf ("android: %s", path);
	entry_type = g_object_new (RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   "cache-name", "android-mtp",
				   "key-prefix", device_serial,
				   "uri-prefix", uri_prefix,
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_free (name);

	name = g_strdup_printf ("android (ignore): %s", path);
	ignore_type = g_object_new (RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE,
				    "db", db,
				    "name", name,
				    "save-to-disk", FALSE,
				    "category", RHYTHMDB_ENTRY_VIRTUAL,
				    "cache-name", "android-mtp",
				    "key-prefix", device_serial,
				    "uri-prefix", uri_prefix,
				    NULL);
	rhythmdb_register_entry_type (db, ignore_type);
	g_free (name);

	name = g_strdup_printf ("android (errors): %s", path);
	error_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_VIRTUAL,
				   NULL);
	rhythmdb_register_entry_type (db, error_type);
	g_free (name);

	g_free (uri_prefix);
	g_object_unref (db);

	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "android-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "android-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.android");

	source = RB_SOURCE (g_object_new (RB_TYPE_ANDROID_SOURCE,
					  "plugin", plugin,
					  "entry-type", entry_type,
					  "ignore-entry-type", ignore_type,
					  "error-entry-type", error_type,
					  "volume", volume,
					  "mount-root", root,
					  "shell", shell,
					  "device-info", device_info,
					  "load-status", RB_SOURCE_LOAD_STATUS_NOT_LOADED,
					  "settings", g_settings_get_child (settings, "source"),
					  "encoding-settings", g_settings_get_child (settings, "encoding"),
					  "toolbar-menu", toolbar,
					  "gudev-device", gudev_device,
					  NULL));

	g_object_unref (settings);
	g_object_unref (builder);
	g_object_unref (gudev_device);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	plugin->sources = g_list_prepend (plugin->sources, source);
	g_signal_connect_object (G_OBJECT (source),
				 "deleted", G_CALLBACK (source_deleted_cb),
				 plugin, 0);

	g_object_unref (shell);
	return source;
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBAndroidPlugin *pi = RB_ANDROID_PLUGIN (plugin);
	RBRemovableMediaManager *rmm;
	RBShell *shell;
	gboolean scanned;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "removable-media-manager", &rmm, NULL);

	g_signal_connect_object (rmm, "create-source-volume", G_CALLBACK (create_source_cb), pi, 0);

	g_object_get (rmm, "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (rmm);
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBAndroidPlugin *plugin = RB_ANDROID_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm;
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      NULL);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (plugin->sources);
	plugin->sources = NULL;

	g_object_unref (rmm);
	g_object_unref (shell);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_android_plugin_register_type (G_TYPE_MODULE (module));
	_rb_android_source_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_ANDROID_PLUGIN);
}
