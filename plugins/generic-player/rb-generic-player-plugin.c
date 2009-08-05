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

#define __EXTENSIONS__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-removable-media-manager.h"
#include "rb-generic-player-source.h"
#include "rb-generic-player-playlist-source.h"
#include "rb-file-helpers.h"
#include "rb-stock-icons.h"
#include "rb-sourcelist.h"
#include "rb-nokia770-source.h"
#include "rb-psp-source.h"


#define RB_TYPE_GENERIC_PLAYER_PLUGIN		(rb_generic_player_plugin_get_type ())
#define RB_GENERIC_PLAYER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GENERIC_PLAYER_PLUGIN, RBGenericPlayerPlugin))
#define RB_GENERIC_PLAYER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GENERIC_PLAYER_PLUGIN, RBGenericPlayerPluginClass))
#define RB_IS_GENERIC_PLAYER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GENERIC_PLAYER_PLUGIN))
#define RB_IS_GENERIC_PLAYER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GENERIC_PLAYER_PLUGIN))
#define RB_GENERIC_PLAYER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GENERIC_PLAYER_PLUGIN, RBGenericPlayerPluginClass))

typedef struct
{
	RBPlugin parent;

	RBShell *shell;
	guint ui_merge_id;

	GList *player_sources;
	GtkActionGroup *actions;
} RBGenericPlayerPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBGenericPlayerPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_generic_player_plugin_get_type		(void) G_GNUC_CONST;

static void rb_generic_player_plugin_init (RBGenericPlayerPlugin *plugin);
static void rb_generic_player_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

static void rb_generic_player_plugin_new_playlist (GtkAction *action,
						   RBGenericPlayerPlugin *plugin);
static void rb_generic_player_plugin_delete_playlist (GtkAction *action,
						      RBGenericPlayerPlugin *plugin);

RB_PLUGIN_REGISTER(RBGenericPlayerPlugin, rb_generic_player_plugin)

static GtkActionEntry rb_generic_player_plugin_actions[] = {
	{ "GenericPlayerSourceNewPlaylist", RB_STOCK_PLAYLIST_NEW, N_("New Playlist"), NULL,
	  N_("Create a new playlist on this device"),
	  G_CALLBACK (rb_generic_player_plugin_new_playlist) },
	{ "GenericPlayerPlaylistDelete", GTK_STOCK_DELETE, N_("Delete Playlist"), NULL,
	  N_("Delete this playlist"),
	  G_CALLBACK (rb_generic_player_plugin_delete_playlist) },
};

static void
rb_generic_player_plugin_class_init (RBGenericPlayerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_generic_player_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	RB_PLUGIN_REGISTER_TYPE(rb_generic_player_source);
	RB_PLUGIN_REGISTER_TYPE(rb_generic_player_playlist_source);
	RB_PLUGIN_REGISTER_TYPE(rb_psp_source);
	RB_PLUGIN_REGISTER_TYPE(rb_nokia770_source);
}

static void
rb_generic_player_plugin_init (RBGenericPlayerPlugin *plugin)
{
	rb_debug ("RBGenericPlayerPlugin initialising");
}

static void
rb_generic_player_plugin_finalize (GObject *object)
{
/*
	RBGenericPlayerPlugin *plugin = RB_GENERIC_PLAYER_PLUGIN (object);
*/
	rb_debug ("RBGenericPlayerPlugin finalising");

	G_OBJECT_CLASS (rb_generic_player_plugin_parent_class)->finalize (object);
}

static void
rb_generic_player_plugin_source_deleted (RBGenericPlayerSource *source, RBGenericPlayerPlugin *plugin)
{
	plugin->player_sources = g_list_remove (plugin->player_sources, source);
}

static void
rb_generic_player_plugin_new_playlist (GtkAction *action, RBGenericPlayerPlugin *plugin)
{
	RBSource *source;
	RBSourceList *sourcelist;

	g_object_get (plugin->shell,
		      "selected-source", &source,
		      "sourcelist", &sourcelist,
		      NULL);
	if (source != NULL && RB_IS_GENERIC_PLAYER_SOURCE (source)) {
		RBSource *playlist;
		RhythmDBEntryType entry_type;

		g_object_get (source, "entry-type", &entry_type, NULL);

		playlist = rb_generic_player_playlist_source_new (plugin->shell, RB_GENERIC_PLAYER_SOURCE (source), NULL, NULL, entry_type);
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

		rb_generic_player_source_add_playlist (RB_GENERIC_PLAYER_SOURCE (source),
						       plugin->shell,
						       playlist);

		rb_sourcelist_edit_source_name (sourcelist, playlist);
	}

	if (source != NULL) {
		g_object_unref (source);
	}
	g_object_unref (sourcelist);
}

static void
rb_generic_player_plugin_delete_playlist (GtkAction *actino, RBGenericPlayerPlugin *plugin)
{
	RBSource *source;
	
	g_object_get (plugin->shell,
		      "selected-source", &source,
		      NULL);
	if (source != NULL && RB_IS_GENERIC_PLAYER_PLAYLIST_SOURCE (source)) {
		if (RB_IS_GENERIC_PLAYER_PLAYLIST_SOURCE (source)) {
			rb_generic_player_playlist_delete_from_player (RB_GENERIC_PLAYER_PLAYLIST_SOURCE (source));
			rb_source_delete_thyself (source);
		}

		g_object_unref (source);
	}
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm, GMount *mount, MPIDDevice *device_info, RBGenericPlayerPlugin *plugin)
{
	RBSource *source = NULL;

	if (rb_psp_is_mount_player (mount, device_info))
		source = RB_SOURCE (rb_psp_source_new (plugin->shell, mount, device_info));
	if (source == NULL && rb_nokia770_is_mount_player (mount, device_info))
		source = RB_SOURCE (rb_nokia770_source_new (plugin->shell, mount, device_info));
	if (source == NULL && rb_generic_player_is_mount_player (mount, device_info))
		source = RB_SOURCE (rb_generic_player_source_new (plugin->shell, mount, device_info));

	if (plugin->actions == NULL) {
		plugin->actions = gtk_action_group_new ("GenericPlayerActions");
		gtk_action_group_set_translation_domain (plugin->actions, GETTEXT_PACKAGE);
		gtk_action_group_add_actions (plugin->actions,
					      rb_generic_player_plugin_actions,
					      G_N_ELEMENTS (rb_generic_player_plugin_actions),
					      plugin);
	}

	if (source) {
		if (plugin->ui_merge_id == 0) {
			GtkUIManager *uimanager = NULL;
			char *file = NULL;

			g_object_get (G_OBJECT (plugin->shell), "ui-manager", &uimanager, NULL);

			gtk_ui_manager_insert_action_group (uimanager, plugin->actions, 0);

			file = rb_plugin_find_file (RB_PLUGIN (plugin), "generic-player-ui.xml");
			plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
									       file,
									       NULL);
			g_free (file);
			g_object_unref (G_OBJECT (uimanager));
		}

		plugin->player_sources = g_list_prepend (plugin->player_sources, source);
		g_signal_connect_object (G_OBJECT (source),
					 "deleted", G_CALLBACK (rb_generic_player_plugin_source_deleted),
					 plugin, 0);
	}

	return source;
}



static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	RBGenericPlayerPlugin *pi = RB_GENERIC_PLAYER_PLUGIN (plugin);
	RBRemovableMediaManager *rmm;
	gboolean scanned;

	pi->shell = shell;
	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      NULL);

	/* watch for new removable media.  use connect_after so
	 * plugins for more specific device types can get in first.
	 */
	g_signal_connect_after (G_OBJECT (rmm),
				"create-source-mount", G_CALLBACK (create_source_cb),
				pi);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (G_OBJECT (rmm));
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBGenericPlayerPlugin *plugin = RB_GENERIC_PLAYER_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	GtkUIManager *uimanager = NULL;

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->player_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (plugin->player_sources);
	plugin->player_sources = NULL;

	if (plugin->ui_merge_id) {
		gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
		plugin->ui_merge_id = 0;
	}

	g_object_unref (G_OBJECT (uimanager));
	g_object_unref (G_OBJECT (rmm));
}

