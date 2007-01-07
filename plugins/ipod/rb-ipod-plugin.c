/*
 * rb-ipod-plugin.c
 *
 * Copyright (C) 2006 James Livingston <jrl@ids.org.au>
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

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <libgnomevfs/gnome-vfs.h>

#include "rb-removable-media-manager.h"
#include "rb-source.h"
#include "rb-ipod-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-shell.h"


#define RB_TYPE_IPOD_PLUGIN		(rb_ipod_plugin_get_type ())
#define RB_IPOD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_PLUGIN, RBIpodPlugin))
#define RB_IPOD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_PLUGIN, RBIpodPluginClass))
#define RB_IS_IPOD_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_PLUGIN))
#define RB_IS_IPOD_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_PLUGIN))
#define RB_IPOD_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_PLUGIN, RBIpodPluginClass))

typedef struct
{
	RBPlugin parent;

	RBShell *shell;
	GtkActionGroup *action_group;
	guint ui_merge_id;

	GList *ipod_sources;
} RBIpodPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBIpodPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_ipod_plugin_get_type		(void) G_GNUC_CONST;

static void rb_ipod_plugin_init (RBIpodPlugin *plugin);
static void rb_ipod_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

static RBSource * create_source_cb (RBRemovableMediaManager *rmm,
				    GnomeVFSVolume *volume,
				    RBIpodPlugin *plugin);
static void  rb_ipod_plugin_cmd_rename (GtkAction *action,
					RBIpodPlugin *plugin);

RB_PLUGIN_REGISTER(RBIpodPlugin, rb_ipod_plugin)


static GtkActionEntry rb_ipod_plugin_actions [] =
{
	{ "iPodSourceRename", NULL, N_("_Rename"), NULL,
	  N_("Rename iPod"),
	  G_CALLBACK (rb_ipod_plugin_cmd_rename) }
};


static void
rb_ipod_plugin_class_init (RBIpodPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_ipod_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	/* register types used by the plugin */
	RB_PLUGIN_REGISTER_TYPE(rb_ipod_source);
}

static void
rb_ipod_plugin_init (RBIpodPlugin *plugin)
{
	rb_debug ("RBIpodPlugin initialising");
}

static void
rb_ipod_plugin_finalize (GObject *object)
{
	/*RBIpodPlugin *plugin = RB_IPOD_PLUGIN (object);*/

	rb_debug ("RBIpodPlugin finalising");

	G_OBJECT_CLASS (rb_ipod_plugin_parent_class)->finalize (object);
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBIpodPlugin *plugin = RB_IPOD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	GtkUIManager *uimanager = NULL;
	gboolean scanned;
	char *file;

	plugin->shell = shell;

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);

	/* add ipod UI */
	plugin->action_group = gtk_action_group_new ("iPodActions");
	gtk_action_group_set_translation_domain (plugin->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (plugin->action_group,
				      rb_ipod_plugin_actions, G_N_ELEMENTS (rb_ipod_plugin_actions),
				      plugin);
	gtk_ui_manager_insert_action_group (uimanager, plugin->action_group, 0);
	file = rb_plugin_find_file (bplugin, "ipod-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
							       file,
							       NULL);
	g_free (file);

	/* watch for new removable media, and cause a rescan */
	g_signal_connect (G_OBJECT (rmm),
			  "create-source", G_CALLBACK (create_source_cb),
			  plugin);

	/* only scan if we're being loaded after the initial scan has been done */
	g_object_get (G_OBJECT (rmm), "scanned", &scanned, NULL);
	if (scanned)
		rb_removable_media_manager_scan (rmm);

	g_object_unref (G_OBJECT (rmm));
	g_object_unref (G_OBJECT (uimanager));
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBIpodPlugin *plugin = RB_IPOD_PLUGIN (bplugin);
	RBRemovableMediaManager *rmm = NULL;
	GtkUIManager *uimanager = NULL;

	g_object_get (G_OBJECT (shell),
		      "removable-media-manager", &rmm,
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->action_group);

	g_signal_handlers_disconnect_by_func (G_OBJECT (rmm), create_source_cb, plugin);

	g_list_foreach (plugin->ipod_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (plugin->ipod_sources);
	plugin->ipod_sources = NULL;

	g_object_unref (G_OBJECT (uimanager));
	g_object_unref (G_OBJECT (rmm));
}

static void
rb_ipod_plugin_source_deleted (RBiPodSource *source, RBIpodPlugin *plugin)
{
	plugin->ipod_sources = g_list_remove (plugin->ipod_sources, source);
}

static RBSource *
create_source_cb (RBRemovableMediaManager *rmm, GnomeVFSVolume *volume, RBIpodPlugin *plugin)
{
	if (rb_ipod_is_volume_ipod (volume)) {
		RBSource *src;
		src = RB_SOURCE (rb_ipod_source_new (plugin->shell, volume));

		plugin->ipod_sources = g_list_prepend (plugin->ipod_sources, src);
		g_signal_connect_object (G_OBJECT (src),
					 "deleted", G_CALLBACK (rb_ipod_plugin_source_deleted),
					 plugin, 0);

		return src;
	}

	return NULL;
}

static void
rb_ipod_plugin_cmd_rename (GtkAction *action,
			   RBIpodPlugin *plugin)
{
	RBRemovableMediaManager *manager = NULL;
	RBSourceList *sourcelist = NULL;
	RBSource *source = NULL;

	/* FIXME: this is pretty ugly, the sourcelist should automatically add
	 * a "rename" menu item for sources that have can_rename == TRUE.
	 * This is a bit trickier to handle though, since playlists want
	 * to make rename sensitive/unsensitive instead of showing/hiding it
	 */
	g_object_get (G_OBJECT (plugin->shell),
		      "selected-source", &source,
		      "removable-media-manager", &manager,
		      NULL);
	if ((source == NULL) || !RB_IS_IPOD_SOURCE (source)) {
		g_object_unref (G_OBJECT (manager));
		g_critical ("got iPodSourceRename action for non-ipod source");
		return;
	}

	g_object_get (G_OBJECT (manager), "sourcelist", &sourcelist, NULL);
	g_object_unref (G_OBJECT (manager));

	rb_sourcelist_edit_source_name (sourcelist, RB_SOURCE (source));
	/* Once editing is done, notify::name will be fired on the source, and
	 * we'll catch that in our rename callback
	 */
	g_object_unref (G_OBJECT (sourcelist));
}

