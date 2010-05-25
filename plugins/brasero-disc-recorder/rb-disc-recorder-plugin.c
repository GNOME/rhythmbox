/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *  Copyright (C) 2008 Rouquier Philippe <bonfire-app@wanadoo.fr>
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

#define __EXTENSIONS__

#include "config.h"

#include <errno.h>
#include <string.h> /* For strlen */
#include <unistd.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gdk/gdkx.h>
#include <brasero/brasero-media.h>
#include <brasero/brasero-medium-monitor.h>

#include <libxml/xmlerror.h>
#include <libxml/xmlwriter.h>
#include <libxml/parser.h>
#include <libxml/xmlstring.h>
#include <libxml/uri.h>
#include <libxml/xmlsave.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-playlist-source.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"

#define RB_TYPE_DISC_RECORDER_PLUGIN		(rb_disc_recorder_plugin_get_type ())
#define RB_DISC_RECORDER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DISC_RECORDER_PLUGIN, RBDiscRecorderPlugin))
#define RB_DISC_RECORDER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DISC_RECORDER_PLUGIN, RBDiscRecorderPluginClass))
#define RB_IS_DISC_RECORDER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DISC_RECORDER_PLUGIN))
#define RB_IS_DISC_RECORDER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DISC_RECORDER_PLUGIN))
#define RB_DISC_RECORDER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DISC_RECORDER_PLUGIN, RBDiscRecorderPluginClass))

typedef struct
{
	RBPlugin        parent;

	RBShell        *shell;
	GtkActionGroup *action_group;
	guint           ui_merge_id;

	RBSource       *selected_source;
	guint           enabled : 1;
} RBDiscRecorderPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBDiscRecorderPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_disc_recorder_plugin_get_type		(void) G_GNUC_CONST;

static void rb_disc_recorder_plugin_init (RBDiscRecorderPlugin *plugin);
static void rb_disc_recorder_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);
static void cmd_burn_source (GtkAction          *action,
			     RBDiscRecorderPlugin *pi);
static void cmd_duplicate_cd (GtkAction          *action,
			      RBDiscRecorderPlugin *pi);

static GtkActionEntry rb_disc_recorder_plugin_actions [] = {
	{ "MusicPlaylistBurnToDiscPlaylist", "media-optical-audio-new", N_("_Create Audio CD..."), NULL,
	  N_("Create an audio CD from playlist"),
	  G_CALLBACK (cmd_burn_source) },
	{ "MusicAudioCDDuplicate", "media-optical-copy", N_("Duplicate Audio CD..."), NULL,
	  N_("Create a copy of this audio CD"),
	  G_CALLBACK (cmd_duplicate_cd) },
};

RB_PLUGIN_REGISTER(RBDiscRecorderPlugin, rb_disc_recorder_plugin)

#define RB_RECORDER_ERROR rb_disc_recorder_error_quark ()

GQuark rb_disc_recorder_error_quark (void);

GQuark
rb_disc_recorder_error_quark (void)
{
        static GQuark quark = 0;
        if (! quark) {
                quark = g_quark_from_static_string ("rb_disc_recorder_error");
        }

        return quark;
}

typedef enum
{
        RB_RECORDER_ERROR_NONE			= 0,
	RB_RECORDER_ERROR_GENERAL
} RBRecorderError;

static void
rb_disc_recorder_plugin_class_init (RBDiscRecorderPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_disc_recorder_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

static void
rb_disc_recorder_plugin_init (RBDiscRecorderPlugin *pi)
{
	rb_debug ("RBDiscRecorderPlugin initialized");
}

static void
rb_disc_recorder_plugin_finalize (GObject *object)
{
	rb_debug ("RBDiscRecorderPlugin finalized");

	G_OBJECT_CLASS (rb_disc_recorder_plugin_parent_class)->finalize (object);
}

static gboolean
rb_disc_recorder_plugin_start_burning (RBDiscRecorderPlugin *pi,
					  const char *path,
					  gboolean copy)
{
	GtkWidget *main_window;
	GdkScreen *screen;
	GdkWindow *window;
	GPtrArray *array;
	char **args, *xid_str;
	GError *error = NULL;
	gboolean ret;
	
	array = g_ptr_array_new ();
	g_ptr_array_add (array, "brasero");
	if (copy != FALSE)
		g_ptr_array_add (array, "-c");
	else
		g_ptr_array_add (array, "-r");
	g_ptr_array_add (array, (gpointer) path);

	main_window = gtk_widget_get_toplevel (GTK_WIDGET (pi->selected_source));
	screen = gtk_widget_get_screen (main_window);
	window = gtk_widget_get_window (main_window);
	if (window) {
		int xid;
		xid = gdk_x11_drawable_get_xid (GDK_DRAWABLE (window));
		xid_str = g_strdup_printf ("%d", xid);
		g_ptr_array_add (array, "-x");
		g_ptr_array_add (array, xid_str);
	} else {
		xid_str = NULL;
	}
	
	g_ptr_array_add (array, NULL);
	args = (char **) g_ptr_array_free (array, FALSE);

	ret = TRUE;
	if (!gdk_spawn_on_screen (screen, NULL, args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
		if (copy != FALSE) {
			rb_error_dialog (GTK_WINDOW (main_window),
					 _("Rhythmbox could not duplicate the disc"),
					 "%s", error->message);

		} else {
			rb_error_dialog (GTK_WINDOW (main_window),
					 _("Rhythmbox could not record the audio disc"),
					 "%s", error->message);
		}
		ret = FALSE;
		g_error_free (error);
	}
	
	g_free (xid_str);
	g_free (args);
	
	return ret;
}

static gchar*
rb_disc_recorder_plugin_write_audio_project (const gchar 	*name,
					     GtkTreeModel       *model,
					     GError		**error)
{
        GtkTreeIter iter;
	xmlTextWriter *project;
	xmlDocPtr doc = NULL;
	xmlSaveCtxt *save;
	gint success;
    	gchar *path;
	int fd;
	int use_errno = 0;

        if (! gtk_tree_model_get_iter_first (model, &iter)) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Unable to build an audio track list"));
                return NULL;
        }

	/* get a temporary path */
	path = g_build_filename (g_get_tmp_dir (), "brasero-tmp-project-XXXXXX",  NULL);
	fd = g_mkstemp (path);
	if (fd == -1) {
		g_set_error (error,
                 	     RB_RECORDER_ERROR,
                     	     RB_RECORDER_ERROR_GENERAL,
                    	     _("Unable to write audio project file %s: %s"),
			     path,
			     g_strerror (errno));
		rb_debug ("g_mkstemp failed");

		g_free (path);
		return NULL;
	}

	project = xmlNewTextWriterDoc (&doc, 0);
	if (!project) {
		g_remove (path);
		g_free (path);
		close (fd);

		g_set_error (error,
                 	     RB_RECORDER_ERROR,
                     	     RB_RECORDER_ERROR_GENERAL,
                    	     _("Unable to write audio project"));

		return NULL;
	}

	xmlTextWriterSetIndent (project, 1);
	xmlTextWriterSetIndentString (project, (xmlChar *) "\t");

	success = xmlTextWriterStartDocument (project,
					      NULL,
					      "UTF8",
					      NULL);
	if (success < 0)
		goto error;

	success = xmlTextWriterStartElement (project, (xmlChar *) "braseroproject");
	if (success < 0)
		goto error;

	/* write the name of the version */
	success = xmlTextWriterWriteElement (project,
					     (xmlChar *) "version",
					     (xmlChar *) "0.2");
	if (success < 0)
		goto error;

	if (name) {
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "label",
						     (xmlChar *) name);
		if (success < 0)
			goto error;
	}

	success = xmlTextWriterStartElement (project, (xmlChar *) "track");
	if (success < 0)
		goto error;

        do {
		RhythmDBEntry  *entry;
		const char *str;
		xmlChar *escaped;

		success = xmlTextWriterStartElement (project, (xmlChar *) "audio");
		if (success < 0)
			goto error;

		gtk_tree_model_get (model, &iter, 0, &entry, -1);

		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		escaped = (xmlChar *) g_uri_escape_string (str, NULL, FALSE);
		success = xmlTextWriterWriteElement (project,
						    (xmlChar *) "uri",
						     escaped);
		g_free (escaped);

		if (success == -1)
			goto error;

		/* start of the song always 0 */
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "start",
						     (xmlChar *) "0");
		if (success == -1)
			goto error;

		/* end of the song = duration (in seconds while brasero likes it
		 * in nanoseconds =( ) */
		/* Disable this for the moment and let brasero check the size
		 * itself. In case the user chooses on the fly burning we need
		 * a more precise duration or we'd end up burning the track
		 * incompletely or with a big padding */
		/*
		end = g_strdup_printf ("%"G_GINT64_FORMAT, (gint64) (song->duration * 1000000000LL));
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "end",
						     (xmlChar *) end);

		g_free (end);
		if (success == -1)
			goto error;
		*/

		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
		if (str) {
			escaped = (xmlChar *) g_uri_escape_string (str, NULL, FALSE);
			success = xmlTextWriterWriteElement (project,
							    (xmlChar *) "title",
							     escaped);
			g_free (escaped);

			if (success == -1)
				goto error;
		}

		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
		if (str) {
			escaped = (xmlChar *) g_uri_escape_string (str, NULL, FALSE);
			success = xmlTextWriterWriteElement (project,
							    (xmlChar *) "artist",
							     escaped);
			g_free (escaped);

			if (success == -1)
				goto error;
		}

		/*
		if (song->composer) {
			escaped = (unsigned char *) g_uri_escape_string (song->composer, NULL, FALSE);
			success = xmlTextWriterWriteElement (project,
							    (xmlChar *) "composer",
							     escaped);
			g_free (escaped);

			if (success == -1)
				goto error;
		}
		*/

		success = xmlTextWriterEndElement (project); /* audio */
		if (success < 0)
			goto error;
        } while (gtk_tree_model_iter_next (model, &iter));

	success = xmlTextWriterEndElement (project); /* track */
	if (success < 0)
		goto error;

	success = xmlTextWriterEndElement (project); /* braseroproject */
	if (success < 0)
		goto error;

	success = xmlTextWriterEndDocument (project);
	if (success < 0)
		goto end_error;

	xmlFreeTextWriter (project);

	save = xmlSaveToFd (fd, "UTF8", XML_SAVE_FORMAT);
	if (save == NULL)
		goto save_error;

	if (xmlSaveDoc (save, doc) == -1)
		goto save_error;

	if (xmlSaveClose (save) == -1) {
		use_errno = errno;
		rb_debug ("xmlSaveClose failed");
		goto save_error;
	}

	xmlFreeDoc (doc);

	if (close (fd) == -1) {
		use_errno = errno;
		rb_debug ("close() failed");
		goto save_error;
	}

	return path;

error:
	/* cleanup */
	xmlTextWriterEndDocument (project);

end_error:
	xmlFreeTextWriter (project);

save_error:
	if (use_errno != 0) {
		g_set_error (error,
			     RB_RECORDER_ERROR,
			     RB_RECORDER_ERROR_GENERAL,
			     _("Unable to write audio project file %s: %s"),
			     path,
			     g_strerror (use_errno));
	} else {
		g_set_error (error,
			     RB_RECORDER_ERROR,
			     RB_RECORDER_ERROR_GENERAL,
			     _("Unable to write audio project"));
	}

	g_remove (path);
	g_free (path);
	close (fd);

	return NULL;
}

static void
source_burn (RBDiscRecorderPlugin *pi,
	     RBSource *source)
{
	GtkWidget    *parent;
	char         *name;
	char         *path;
	GError       *error = NULL;
	GtkTreeModel *model;

	g_object_get (source, "query-model", &model, NULL);

	/* don't burn if the source is empty */
	if (gtk_tree_model_iter_n_children (model, NULL) == 0) {
		g_object_unref (model);
		return;
	}

	name = NULL;
	g_object_get (source, "name", &name, NULL);
	rb_debug ("Burning playlist %s", name);

	parent = gtk_widget_get_toplevel (GTK_WIDGET (source));

	rb_debug ("Creating audio project");
	path = rb_disc_recorder_plugin_write_audio_project (name, model, &error);
	g_free (name);

	if (! path) {
		rb_error_dialog (GTK_WINDOW (parent),
				 _("Unable to create audio CD project"),
				 "%s", error->message);
		g_error_free (error);
		return;
	}

	rb_debug ("Starting brasero");
	rb_disc_recorder_plugin_start_burning (pi, path, FALSE);
	g_free (path);
}

static void
cmd_burn_source (GtkAction            *action,
		 RBDiscRecorderPlugin *pi)
{
	if (pi->selected_source != NULL) {
		source_burn (pi, pi->selected_source);
	}
}

static void
cmd_duplicate_cd (GtkAction         	*action,
		  RBDiscRecorderPlugin	*pi)
{
	gchar *device;
	GVolume *volume;

	if (!pi->selected_source)
		return;

	g_object_get (pi->selected_source, "volume", &volume, NULL);
	if (G_IS_VOLUME (volume))
		device = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	else
		device = NULL;

	g_object_unref (volume);

	rb_disc_recorder_plugin_start_burning (pi, device, TRUE);
	g_free (device);
}

static void
playlist_entries_changed (GtkTreeModel         *model,
			  RhythmDBEntry        *entry,
			  RBDiscRecorderPlugin *pi)
{
	int        num_tracks;
	GtkAction *action;

	num_tracks = gtk_tree_model_iter_n_children (model, NULL);

	action = gtk_action_group_get_action (pi->action_group, "MusicPlaylistBurnToDiscPlaylist");
	gtk_action_set_sensitive (action, (num_tracks > 0));
}

static void
playlist_row_inserted_cb (GtkTreeModel       *model,
			  GtkTreePath        *path,
			  GtkTreeIter        *iter,
			  RBDiscRecorderPlugin *pi)
{
	RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);

	playlist_entries_changed (model, entry, pi);

	rhythmdb_entry_unref (entry);
}

static gboolean
rb_disc_recorder_has_burner (RBDiscRecorderPlugin *pi)
{
	BraseroMediumMonitor *monitor;
	GSList		     *drives;

	/* Find all drives and check capabilities */
	monitor = brasero_medium_monitor_get_default ();
	drives = brasero_medium_monitor_get_drives (monitor, BRASERO_DRIVE_TYPE_WRITER);
	g_object_unref (monitor);

	g_slist_foreach (drives, (GFunc) g_object_unref, NULL);
	g_slist_free (drives);

	if (drives != NULL)
		return TRUE;

	return FALSE;
}


static gboolean
is_copy_available (RBDiscRecorderPlugin *pi)
{
	char *cmd;

	if (!rb_disc_recorder_has_burner (pi))
		return FALSE;

	cmd = g_find_program_in_path ("brasero");
	if (cmd == NULL)
		return FALSE;

	g_free (cmd);
	return TRUE;
}

static void
update_source (RBDiscRecorderPlugin *pi,
	       RBShell            *shell)
{
	GtkAction *burn_action, *copy_action;
	gboolean   playlist_active, is_audiocd_active;
	RBSource  *selected_source;
	const char *source_type;

	if (pi->selected_source != NULL) {
		RhythmDBQueryModel *model;

		g_object_get (pi->selected_source, "query-model", &model, NULL);
		g_signal_handlers_disconnect_by_func (model, playlist_row_inserted_cb, pi);
		g_signal_handlers_disconnect_by_func (model, playlist_entries_changed, pi);
		g_object_unref (model);
	}

	g_object_get (shell, "selected-source", &selected_source, NULL);

	/* for now restrict to playlist sources */
	playlist_active = RB_IS_PLAYLIST_SOURCE (selected_source);

	source_type = G_OBJECT_TYPE_NAME (selected_source);
	is_audiocd_active = g_str_equal (source_type, "RBAudioCdSource");

	burn_action = gtk_action_group_get_action (pi->action_group,
						   "MusicPlaylistBurnToDiscPlaylist");
	copy_action = gtk_action_group_get_action (pi->action_group,
						   "MusicAudioCDDuplicate");

	if (pi->enabled && playlist_active && rb_disc_recorder_has_burner (pi)) {
		RhythmDBQueryModel *model;

		g_object_get (selected_source, "query-model", &model, NULL);
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
		gtk_action_set_visible (burn_action, TRUE);
	} else {
		gtk_action_set_visible (burn_action, FALSE);
	}

	if (pi->enabled && is_audiocd_active && is_copy_available (pi)) {
		gtk_action_set_visible (copy_action, TRUE);
	} else {
		gtk_action_set_visible (copy_action, FALSE);
	}

	if (pi->selected_source != NULL) {
		g_object_unref (pi->selected_source);
	}
	pi->selected_source = selected_source;
}

static void
shell_selected_source_notify_cb (RBShell            *shell,
				 GParamSpec         *param,
				 RBDiscRecorderPlugin *pi)
{
	rb_debug ("RBDiscRecorderPlugin selected source changed");

	update_source (pi, shell);
}

static struct ui_paths {
	const char *path;
	gboolean for_burn;
	gboolean for_copy;
} ui_paths[] = {
	{ "/MenuBar/MusicMenu/PlaylistMenu/PluginPlaceholder", TRUE, FALSE },
	{ "/MenuBar/MusicMenu/PluginPlaceholder", FALSE, TRUE },
	{ "/ToolBar/PluginPlaceholder", TRUE, TRUE },
	{ "/PlaylistSourcePopup/PluginPlaceholder", TRUE, FALSE },
	{ "/AutoPlaylistSourcePopup/PluginPlaceholder", TRUE, FALSE },
	{ "/QueueSourcePopup/PluginPlaceholder", TRUE, FALSE },
	{ "/AudioCdSourcePopup/PluginPlaceholder", FALSE, TRUE },
};

static void
impl_activate (RBPlugin *plugin,
	       RBShell  *shell)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (plugin);
	GtkUIManager         *uimanager = NULL;
	GtkAction            *action;
	int                   i;

	pi->enabled = TRUE;

	rb_debug ("RBDiscRecorderPlugin activating");

	brasero_media_library_start ();

	pi->shell = shell;

	g_object_get (shell,
		      "ui-manager", &uimanager,
		      NULL);

	g_signal_connect_object (G_OBJECT (shell),
				 "notify::selected-source",
				 G_CALLBACK (shell_selected_source_notify_cb),
				 pi, 0);

	/* add UI */
	pi->action_group = gtk_action_group_new ("DiscRecorderActions");
	gtk_action_group_set_translation_domain (pi->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (pi->action_group,
				      rb_disc_recorder_plugin_actions, G_N_ELEMENTS (rb_disc_recorder_plugin_actions),
				      pi);
	gtk_ui_manager_insert_action_group (uimanager, pi->action_group, 0);
	pi->ui_merge_id = gtk_ui_manager_new_merge_id (uimanager);
	for (i = 0; i < G_N_ELEMENTS (ui_paths); i++) {
		if (ui_paths[i].for_burn)
			gtk_ui_manager_add_ui (uimanager,
					       pi->ui_merge_id,
					       ui_paths[i].path,
					       "MusicPlaylistBurnToDiscPlaylistMenu",
					       "MusicPlaylistBurnToDiscPlaylist",
					       GTK_UI_MANAGER_AUTO,
					       FALSE);
		if (ui_paths[i].for_copy)
			gtk_ui_manager_add_ui (uimanager,
					       pi->ui_merge_id,
					       ui_paths[i].path,
					       "MusicAudioCDDuplicateMenu",
					       "MusicAudioCDDuplicate",
					       GTK_UI_MANAGER_AUTO,
					       FALSE);
	}
	g_object_unref (uimanager);

        action = gtk_action_group_get_action (pi->action_group, "MusicPlaylistBurnToDiscPlaylist");
	/* Translators: this is the toolbar button label for */
	/* Create Audio CD action                            */
	g_object_set (action, "short-label", _("Burn"), NULL);

        action = gtk_action_group_get_action (pi->action_group,
					      "MusicAudioCDDuplicate");
	/* Translators: this is the toolbar button label for */
	/* Duplicate Audio CD action                         */
	g_object_set (action, "short-label", _("Copy CD"), NULL);

	update_source (pi, shell);
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell  *shell)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (plugin);
	GtkUIManager       *uimanager = NULL;

	pi->enabled = FALSE;

	rb_debug ("RBDiscRecorderPlugin deactivating");

	update_source (pi, shell);

	if (pi->selected_source) {
		g_object_unref (pi->selected_source);
		pi->selected_source = NULL;
	}

	g_signal_handlers_disconnect_by_func (shell, shell_selected_source_notify_cb, pi);

	g_object_get (shell,
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, pi->ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, pi->action_group);

	g_object_unref (uimanager);

	/* NOTE: don't deactivate libbrasero-media as it could be in use somewhere else */
}

