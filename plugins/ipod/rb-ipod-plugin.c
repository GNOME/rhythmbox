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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
#include "rb-stock-icons.h"
#include "rb-display-page-tree.h"


#define RB_TYPE_IPOD_PLUGIN		(rb_ipod_plugin_get_type ())
#define RB_IPOD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_PLUGIN, RBIpodPlugin))
#define RB_IPOD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_PLUGIN, RBIpodPluginClass))
#define RB_IS_IPOD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_PLUGIN))
#define RB_IS_IPOD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_PLUGIN))
#define RB_IPOD_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_PLUGIN, RBIpodPluginClass))

typedef struct
{
	PeasExtensionBase parent;

	RBShell *shell;
	GtkActionGroup *action_group;
	guint ui_merge_id;

	GList *ipod_sources;
} RBIpodPlugin;

typedef struct
{
	PeasExtensionBaseClass parent_class;
} RBIpodPluginClass;


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_ipod_plugin_init (RBIpodPlugin *plugin);

static RBSource * create_source_cb (RBRemovableMediaManager *rmm,
				    GMount *mount,
				    MPIDDevice *device_info,
				    RBIpodPlugin *plugin);
static void  rb_ipod_plugin_cmd_rename (GtkAction *action, RBSource *source);
static void  rb_ipod_plugin_cmd_playlist_new (GtkAction *action, RBSource *source);
static void  rb_ipod_plugin_cmd_playlist_rename (GtkAction *action, RBSource *source);
static void  rb_ipod_plugin_cmd_playlist_delete (GtkAction *action, RBSource *source);
static void  rb_ipod_plugin_cmd_properties (GtkAction *action, RBSource *source);

RB_DEFINE_PLUGIN(RB_TYPE_IPOD_PLUGIN, RBIpodPlugin, rb_ipod_plugin,)


static GtkActionEntry rb_ipod_plugin_actions [] =
{
	{ "iPodSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename iPod"),
	  G_CALLBACK (rb_ipod_plugin_cmd_rename) },
	{ "iPodProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Display iPod properties"),
	  G_CALLBACK (rb_ipod_plugin_cmd_properties) },
	{ "iPodSourcePlaylistNew", RB_STOCK_PLAYLIST_NEW, N_("_New Playlist"), NULL,
	  N_("Add new playlist to iPod"),
	  G_CALLBACK (rb_ipod_plugin_cmd_playlist_new) },
	{ "iPodPlaylistSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename playlist"),
	  G_CALLBACK (rb_ipod_plugin_cmd_playlist_rename) },
	{ "iPodPlaylistSourceDelete", GTK_STOCK_REMOVE, N_("_Delete"), NULL,
	  N_("Delete playlist"),
	  G_CALLBACK (rb_ipod_plugin_cmd_playlist_delete) },
};

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
	GtkUIManager *uimanager = NULL;
	RBShell *shell;
	gboolean scanned;
	char *file;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);

	rb_media_player_source_init_actions (shell);

	/* add ipod UI */
	plugin->action_group = gtk_action_group_new ("iPodActions");
	gtk_action_group_set_translation_domain (plugin->action_group,
						 GETTEXT_PACKAGE);
	_rb_action_group_add_display_page_actions (plugin->action_group,
						   G_OBJECT (shell),
						   rb_ipod_plugin_actions,
						   G_N_ELEMENTS (rb_ipod_plugin_actions));
	gtk_ui_manager_insert_action_group (uimanager, plugin->action_group, 0);
	file = rb_find_plugin_data_file (G_OBJECT (bplugin), "ipod-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
							       file,
							       NULL);
	g_free (file);

	/* watch for new removable media, and cause a rescan */
	g_signal_connect (G_OBJECT (rmm),
			  "create-source-mount", G_CALLBACK (create_source_cb),
			  plugin);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (rmm);
	g_object_unref (uimanager);
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBIpodPlugin *plugin = RB_IPOD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	GtkUIManager *uimanager = NULL;
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (shell,
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->action_group);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->ipod_sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (plugin->ipod_sources);
	plugin->ipod_sources = NULL;

	g_object_unref (uimanager);
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

static void
rb_ipod_plugin_cmd_properties (GtkAction *action, RBSource *source)
{
	g_return_if_fail (RB_IS_IPOD_SOURCE (source));
	rb_media_player_source_show_properties (RB_MEDIA_PLAYER_SOURCE (source));
}
 
static void
rb_ipod_plugin_cmd_rename (GtkAction *action, RBSource *source)
{
	RBDisplayPageTree *page_tree = NULL;
	RBShell *shell;

	g_return_if_fail (RB_IS_IPOD_SOURCE (source));

	/* FIXME: this is pretty ugly, the sourcelist should automatically add
	 * a "rename" menu item for sources that have can_rename == TRUE.
	 * This is a bit trickier to handle though, since playlists want
	 * to make rename sensitive/unsensitive instead of showing/hiding it
	 */

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "display-page-tree", &page_tree, NULL);

	rb_display_page_tree_edit_source_name (page_tree, source);
	/* Once editing is done, notify::name will be fired on the source, and
	 * we'll catch that in our rename callback
	 */
	g_object_unref (page_tree);
	g_object_unref (shell);
}

static void
rb_ipod_plugin_cmd_playlist_rename (GtkAction *action, RBSource *source)
{
	RBDisplayPageTree *page_tree = NULL;
	RBShell *shell;

	g_return_if_fail (RB_IS_IPOD_STATIC_PLAYLIST_SOURCE (source));

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "display-page-tree", &page_tree, NULL);
	rb_display_page_tree_edit_source_name (page_tree, source);
	g_object_unref (page_tree);
	g_object_unref (shell);
}

static void
rb_ipod_plugin_cmd_playlist_delete (GtkAction *action, RBSource *source)
{
	RBiPodSource *ipod_source;

	g_return_if_fail (RB_IS_IPOD_STATIC_PLAYLIST_SOURCE (source));

	g_object_get (source, "ipod-source", &ipod_source, NULL);
	rb_ipod_source_remove_playlist (ipod_source, source);
	g_object_unref (ipod_source);
}

static void
rb_ipod_plugin_cmd_playlist_new (GtkAction *action, RBSource *source)
{
	g_return_if_fail (RB_IS_IPOD_SOURCE (source));
	rb_ipod_source_new_playlist (RB_IPOD_SOURCE (source));
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
