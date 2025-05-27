/*
 * rb-generic-player-plugin.c
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

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-generic-player-source.h"
#include "rb-generic-player-playlist-source.h"
#include "rb-file-helpers.h"
#include "rb-nokia770-source.h"
#include "rb-psp-source.h"
#include "rb-display-page-tree.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

#define RB_TYPE_GENERIC_PLAYER_PLUGIN		(rb_generic_player_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBGenericPlayerPlugin, rb_generic_player_plugin, RB, GENERIC_PLAYER_PLUGIN, PeasExtensionBase)

struct _RBGenericPlayerPlugin
{
	PeasExtensionBase parent;

	GList *player_sources;
};

struct _RBGenericPlayerPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule  *module);

static void rb_generic_player_plugin_init (RBGenericPlayerPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_GENERIC_PLAYER_PLUGIN, RBGenericPlayerPlugin, rb_generic_player_plugin,)

static void
rb_generic_player_plugin_init (RBGenericPlayerPlugin *plugin)
{
	rb_debug ("RBGenericPlayerPlugin initialising");
}

static void
rb_generic_player_plugin_source_deleted (RBGenericPlayerSource *source, RBGenericPlayerPlugin *plugin)
{
	plugin->player_sources = g_list_remove (plugin->player_sources, source);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm, GMount *mount, MPIDDevice *device_info, RBGenericPlayerPlugin *plugin)
{
	RBSource *source = NULL;
	GType source_type = G_TYPE_NONE;
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *error_type;
	RhythmDBEntryType *ignore_type;
	GtkBuilder *builder;
	GMenu *toolbar;
	GVolume *volume;
	GSettings *settings;
	GFile *root;
	const char *name_prefix;
	char *device_serial;
	char *uri_prefix;
	char *name;
	char *path;

	if (rb_psp_is_mount_player (mount, device_info)) {
		source_type = RB_TYPE_PSP_SOURCE;
		name_prefix = "psp";
	} else if (rb_nokia770_is_mount_player (mount, device_info)) {
		source_type = RB_TYPE_NOKIA770_SOURCE;
		name_prefix = "nokia770";
	} else if (rb_generic_player_is_mount_player (mount, device_info)) {
		source_type = RB_TYPE_GENERIC_PLAYER_SOURCE;
		name_prefix = "generic-player";
	} else {
		return NULL;
	}

	volume = g_mount_get_volume (mount);
	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	g_object_get (device_info, "serial", &device_serial, NULL);

	root = g_mount_get_root (mount);
	uri_prefix = g_file_get_uri (root);
	g_object_unref (root);

	name = g_strdup_printf ("%s: %s", name_prefix, path);
	entry_type = g_object_new (RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   "cache-name", "generic-player",
				   "key-prefix", device_serial,
				   "uri-prefix", uri_prefix,
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_free (name);

	name = g_strdup_printf ("%s (ignore): %s", name_prefix, path);
	ignore_type = g_object_new (RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE,
				    "db", db,
				    "name", name,
				    "save-to-disk", FALSE,
				    "category", RHYTHMDB_ENTRY_VIRTUAL,
				    "cache-name", "generic-player",
				    "key-prefix", device_serial,
				    "uri-prefix", uri_prefix,
				    NULL);
	rhythmdb_register_entry_type (db, ignore_type);
	g_free (name);

	name = g_strdup_printf ("%s (errors): %s", name_prefix, path);
	/* errors aren't cached, so this isn't a media player entry type */
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

	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "generic-player-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "generic-player-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.generic-player");

	source = RB_SOURCE (g_object_new (source_type,
					  "plugin", plugin,
					  "entry-type", entry_type,
					  "ignore-entry-type", ignore_type,
					  "error-entry-type", error_type,
					  "mount", mount,
					  "shell", shell,
					  "device-info", device_info,
					  "load-status", RB_SOURCE_LOAD_STATUS_NOT_LOADED,
					  "settings", g_settings_get_child (settings, "source"),
					  "encoding-settings", g_settings_get_child (settings, "encoding"),
					  "toolbar-menu", toolbar,
					  NULL));

	g_object_unref (settings);
	g_object_unref (builder);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	plugin->player_sources = g_list_prepend (plugin->player_sources, source);
	g_signal_connect_object (G_OBJECT (source),
				 "deleted", G_CALLBACK (rb_generic_player_plugin_source_deleted),
				 plugin, 0);

	g_object_unref (shell);
	return source;
}



static void
impl_activate (PeasActivatable *plugin)
{
	RBGenericPlayerPlugin *pi = RB_GENERIC_PLAYER_PLUGIN (plugin);
	RBRemovableMediaManager *rmm;
	RBShell *shell;
	gboolean scanned;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "removable-media-manager", &rmm, NULL);

	/* watch for new removable media.  use connect_after so
	 * plugins for more specific device types can get in first.
	 */
	g_signal_connect_after (G_OBJECT (rmm),
				"create-source-mount", G_CALLBACK (create_source_cb),
				pi);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (rmm, "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (rmm);
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBGenericPlayerPlugin *plugin = RB_GENERIC_PLAYER_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm;
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      NULL);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->player_sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (plugin->player_sources);
	plugin->player_sources = NULL;

	g_object_unref (rmm);
	g_object_unref (shell);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_generic_player_plugin_register_type (G_TYPE_MODULE (module));
	_rb_generic_player_source_register_type (G_TYPE_MODULE (module));
	_rb_generic_player_playlist_source_register_type (G_TYPE_MODULE (module));
	_rb_nokia770_source_register_type (G_TYPE_MODULE (module));
	_rb_psp_source_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_GENERIC_PLAYER_PLUGIN);
}
