/*
 * rb-ipod-plugin.c
 *
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "mediaplayerid.h"

#include "rb-ipod-helpers.h"
#include "rb-removable-media-manager.h"
#include "rb-media-player-source.h"
#include "rb-source.h"
#include "rb-ipod-source.h"
#include "rb-ipod-static-playlist-source.h"
#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-display-page-tree.h"


#define RB_TYPE_IPOD_PLUGIN		(rb_ipod_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBIpodPlugin, rb_ipod_plugin, RB, IPOD_PLUGIN, PeasExtensionBase)

struct _RBIpodPlugin
{
	PeasExtensionBase parent;

	RBShell *shell;

	GList *ipod_sources;
};

struct _RBIpodPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_ipod_plugin_init (RBIpodPlugin *plugin);

static RBSource * create_source_cb (RBRemovableMediaManager *rmm,
				    GMount *mount,
				    MPIDDevice *device_info,
				    RBIpodPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_IPOD_PLUGIN, RBIpodPlugin, rb_ipod_plugin,)


static void
rb_ipod_plugin_init (RBIpodPlugin *plugin)
{
	rb_debug ("RBIpodPlugin initialising");
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBIpodPlugin *plugin = RB_IPOD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	RBShell *shell;
	gboolean scanned;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      NULL);

	/* watch for new removable media, and cause a rescan */
	g_signal_connect (G_OBJECT (rmm),
			  "create-source-mount", G_CALLBACK (create_source_cb),
			  plugin);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (rmm);
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBIpodPlugin *plugin = RB_IPOD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      NULL);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->ipod_sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (plugin->ipod_sources);
	plugin->ipod_sources = NULL;

	g_object_unref (rmm);
	g_object_unref (shell);
}

static void
rb_ipod_plugin_source_deleted (RBiPodSource *source, RBIpodPlugin *plugin)
{
	plugin->ipod_sources = g_list_remove (plugin->ipod_sources, source);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm, GMount *mount, MPIDDevice *device_info, RBIpodPlugin *plugin)
{
	RBSource *src;
	RBShell *shell;

	if (!rb_ipod_helpers_is_ipod (mount, device_info)) {
		return NULL;
	}

	g_object_get (plugin, "object", &shell, NULL);
	src = RB_SOURCE (rb_ipod_source_new (G_OBJECT (plugin),
					     shell,
					     mount,
					     device_info));
	g_object_unref (shell);

	plugin->ipod_sources = g_list_prepend (plugin->ipod_sources, src);
	g_signal_connect_object (G_OBJECT (src),
				 "deleted", G_CALLBACK (rb_ipod_plugin_source_deleted),
				 plugin, 0);
	return src;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_ipod_plugin_register_type (G_TYPE_MODULE (module));
	_rb_ipod_source_register_type (G_TYPE_MODULE (module));
	_rb_ipod_static_playlist_source_register_type (G_TYPE_MODULE (module));
	_rb_ipod_db_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_IPOD_PLUGIN);
}
