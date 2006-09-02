/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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

#define __EXTENSIONS__

#include "config.h"

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-playlist-source.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"

#include "rb-recorder.h"
#include "rb-playlist-source-recorder.h"

#include <nautilus-burn-drive.h>
#ifndef NAUTILUS_BURN_CHECK_VERSION
#define NAUTILUS_BURN_CHECK_VERSION(a,b,c) FALSE
#endif

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
#include <nautilus-burn.h>
#endif

#define RB_TYPE_CD_RECORDER_PLUGIN		(rb_cd_recorder_plugin_get_type ())
#define RB_CD_RECORDER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_CD_RECORDER_PLUGIN, RBCdRecorderPlugin))
#define RB_CD_RECORDER_PLUGIN_CLASS(k)	        (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_CD_RECORDER_PLUGIN, RBCdRecorderPluginClass))
#define RB_IS_CD_RECORDER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_CD_RECORDER_PLUGIN))
#define RB_IS_CD_RECORDER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_CD_RECORDER_PLUGIN))
#define RB_CD_RECORDER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_CD_RECORDER_PLUGIN, RBCdRecorderPluginClass))

typedef struct
{
	RBPlugin        parent;

	RBShell        *shell;
	GtkActionGroup *action_group;
	guint           ui_merge_id;

	RBSource       *selected_source;
	guint           enabled : 1;
} RBCdRecorderPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBCdRecorderPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_cd_recorder_plugin_get_type		(void) G_GNUC_CONST;

static void rb_cd_recorder_plugin_init (RBCdRecorderPlugin *plugin);
static void rb_cd_recorder_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);
static void cmd_burn_source (GtkAction          *action,
			     RBCdRecorderPlugin *pi);

static GtkActionEntry rb_cd_recorder_plugin_actions [] = {
	{ "MusicPlaylistBurnPlaylist", GTK_STOCK_CDROM, N_("_Create Audio CD..."), NULL,
	  N_("Create an audio CD from playlist"),
	  G_CALLBACK (cmd_burn_source) }
};

RB_PLUGIN_REGISTER(RBCdRecorderPlugin, rb_cd_recorder_plugin)

static void
rb_cd_recorder_plugin_class_init (RBCdRecorderPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_cd_recorder_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

static void
rb_cd_recorder_plugin_init (RBCdRecorderPlugin *plugin)
{
	rb_debug ("RBCdRecorderPlugin initializing");

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
	nautilus_burn_init ();
#endif
}

static void
rb_cd_recorder_plugin_finalize (GObject *object)
{
/*
	RBCdRecorderPlugin *plugin = RB_CD_RECORDER_PLUGIN (object);
*/
	rb_debug ("RBCdRecorderPlugin finalizing");

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
	nautilus_burn_shutdown ();
#endif

	G_OBJECT_CLASS (rb_cd_recorder_plugin_parent_class)->finalize (object);
}

static gboolean
burn_source_iter_func (GtkTreeModel *model,
		       GtkTreeIter  *iter,
		       char        **uri,
		       char        **artist,
		       char        **title,
		       gulong       *duration)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	*uri = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
	*title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	*artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	*duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

	return TRUE;
}

static void
source_burn (RBSource *source)
{
	GtkWidget    *recorder;
	GtkWidget    *parent;
	char         *name;
	RBShell      *shell;
	gboolean      res;
	GError       *error;
	GtkTreeModel *model;

	g_object_get (G_OBJECT (source), "query-model", &model, NULL);

	/* don't burn if the source is empty */
	if (gtk_tree_model_iter_n_children (model, NULL) == 0) {
		g_object_unref (model);
		return;
	}

	rb_debug ("burning source");

	g_object_get (source, "name", &name, "shell", &shell, NULL);

	parent = gtk_widget_get_toplevel (GTK_WIDGET (source));
	recorder = rb_playlist_source_recorder_new (parent,
						    shell,
						    name);
	g_object_unref (shell);
	g_free (name);

	error = NULL;
	res = rb_playlist_source_recorder_add_from_model (RB_PLAYLIST_SOURCE_RECORDER (recorder),
							  model,
							  burn_source_iter_func,
							  &error);
	g_object_unref (model);

	if (! res) {
		rb_error_dialog (GTK_WINDOW (parent),
				 _("Unable to create audio CD"),
				 "%s", error->message);
		g_error_free (error);

		gtk_widget_destroy (recorder);

		return;
	}

        g_signal_connect (recorder,
			  "response",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	gtk_widget_show (recorder);
}

static void
cmd_burn_source (GtkAction          *action,
		 RBCdRecorderPlugin *pi)
{
	if (pi->selected_source != NULL) {
		source_burn (pi->selected_source);
	}
}

static void
playlist_entries_changed (GtkTreeModel       *model,
			  RhythmDBEntry      *entry,
			  RBCdRecorderPlugin *pi)
{
	int        num_tracks;
	GtkAction *action;

	num_tracks = gtk_tree_model_iter_n_children (model, NULL);

	action = gtk_action_group_get_action (pi->action_group, "MusicPlaylistBurnPlaylist");
	gtk_action_set_sensitive (action, (num_tracks > 0));
}

static void
playlist_row_inserted_cb (GtkTreeModel       *model,
			  GtkTreePath        *path,
			  GtkTreeIter        *iter,
			  RBCdRecorderPlugin *pi)
{
	RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);

	playlist_entries_changed (model, entry, pi);

	rhythmdb_entry_unref (entry);
}

static void
update_source (RBCdRecorderPlugin *pi,
	       RBShell            *shell)
{
	GtkAction *action;
	gboolean   playlist_active;
	RBSource  *selected_source;

	if (pi->selected_source != NULL) {
		RhythmDBQueryModel *model;

		g_object_get (G_OBJECT (pi->selected_source), "query-model", &model, NULL);
		g_signal_handlers_disconnect_by_func (model, playlist_row_inserted_cb, pi);
		g_signal_handlers_disconnect_by_func (model, playlist_entries_changed, pi);
		g_object_unref (model);
	}

	g_object_get (G_OBJECT (shell), "selected-source", &selected_source, NULL);

	/* for now restrict to playlist sources */
	playlist_active = RB_IS_PLAYLIST_SOURCE (selected_source);

	action = gtk_action_group_get_action (pi->action_group,
					      "MusicPlaylistBurnPlaylist");

	if (pi->enabled && playlist_active && rb_recorder_enabled ()) {
		RhythmDBQueryModel *model;

		g_object_get (G_OBJECT (selected_source), "query-model", &model, NULL);
		/* monitor for changes, to enable/disable the burn menu item */
		g_signal_connect_object (G_OBJECT (model),
					 "row_inserted",
					 G_CALLBACK (playlist_row_inserted_cb),
					 pi, 0);
		g_signal_connect_object (G_OBJECT (model),
					 "post-entry-delete",
					 G_CALLBACK (playlist_entries_changed),
					 pi, 0);

		playlist_entries_changed (GTK_TREE_MODEL (model), NULL, pi);
		g_object_unref (model);
		gtk_action_set_visible (action, TRUE);
	} else {
		gtk_action_set_visible (action, FALSE);
	}


	if (pi->selected_source != NULL) {
		g_object_unref (pi->selected_source);
	}
	pi->selected_source = selected_source;
}

static void
shell_selected_source_notify_cb (RBShell            *shell,
				 GParamSpec         *param,
				 RBCdRecorderPlugin *pi)
{
	rb_debug ("RBCdRecorderPlugin selected source changed");

	update_source (pi, shell);
}

static const char *ui_paths [] = {
	"/MenuBar/MusicMenu/PlaylistMenu/MusicPlaylistPlaylistMenuPluginPlaceholder",
	"/ToolBar/ToolBarPluginPlaceholder",
	"/PlaylistSourcePopup/PlaylistSourcePopupPluginPlaceholder",
	"/AutoPlaylistSourcePopup/AutoPlaylistSourcePopupPluginPlaceholder",
	"/QueueSourcePopup/QueueSourcePopupPluginPlaceholder",
};

static void
impl_activate (RBPlugin *plugin,
	       RBShell  *shell)
{
	RBCdRecorderPlugin *pi = RB_CD_RECORDER_PLUGIN (plugin);
	GtkUIManager       *uimanager = NULL;
	int                 i;

	pi->enabled = TRUE;

	rb_debug ("RBCdRecorderPlugin activating");

	pi->shell = shell;

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);

	g_signal_connect_object (G_OBJECT (shell),
				 "notify::selected-source",
				 G_CALLBACK (shell_selected_source_notify_cb),
				 pi, 0);

	/* add UI */
	pi->action_group = gtk_action_group_new ("CdRecorderActions");
	gtk_action_group_set_translation_domain (pi->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (pi->action_group,
				      rb_cd_recorder_plugin_actions, G_N_ELEMENTS (rb_cd_recorder_plugin_actions),
				      pi);
	gtk_ui_manager_insert_action_group (uimanager, pi->action_group, 0);


	pi->ui_merge_id = gtk_ui_manager_new_merge_id (uimanager);
	for (i = 0; i < G_N_ELEMENTS (ui_paths); i++) {
		gtk_ui_manager_add_ui (uimanager,
				       pi->ui_merge_id,
				       ui_paths [i],
				       "MusicPlaylistBurnPlaylistMenu",
				       "MusicPlaylistBurnPlaylist",
				       GTK_UI_MANAGER_AUTO,
				       FALSE);
	}

	update_source (pi, shell);
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell  *shell)
{
	RBCdRecorderPlugin *pi = RB_CD_RECORDER_PLUGIN (plugin);
	GtkUIManager       *uimanager = NULL;

	pi->enabled = FALSE;

	rb_debug ("RBCdRecorderPlugin deactivating");

	update_source (pi, shell);

	if (pi->selected_source) {
		pi->selected_source = NULL;
	}

	g_signal_handlers_disconnect_by_func (shell, shell_selected_source_notify_cb, pi);

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, pi->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, pi->action_group);

	g_object_unref (G_OBJECT (uimanager));
}

