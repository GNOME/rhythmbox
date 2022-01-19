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
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <brasero-media.h>
#include <brasero-medium-monitor.h>

#include <libxml/xmlerror.h>
#include <libxml/xmlwriter.h>
#include <libxml/parser.h>
#include <libxml/xmlstring.h>
#include <libxml/uri.h>
#include <libxml/xmlsave.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-playlist-source.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-application.h"

#define RB_TYPE_DISC_RECORDER_PLUGIN		(rb_disc_recorder_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBDiscRecorderPlugin, rb_disc_recorder_plugin, RB, DISC_RECORDER_PLUGIN, PeasExtensionBase)

struct _RBDiscRecorderPlugin
{
	PeasExtensionBase parent;

	RBDisplayPage  *selected_page;
	guint           enabled : 1;

	GAction        *burn_action;
	GAction        *copy_action;

};

struct _RBDiscRecorderPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

RB_DEFINE_PLUGIN(RB_TYPE_DISC_RECORDER_PLUGIN, RBDiscRecorderPlugin, rb_disc_recorder_plugin,)

static void rb_disc_recorder_plugin_init (RBDiscRecorderPlugin *plugin);

static void burn_playlist_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void duplicate_cd_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);

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
rb_disc_recorder_plugin_init (RBDiscRecorderPlugin *pi)
{
	rb_debug ("RBDiscRecorderPlugin initialized");
}

static gboolean
rb_disc_recorder_plugin_start_burning (RBDiscRecorderPlugin *pi,
					  const char *path,
					  gboolean copy)
{
	GtkWidget *main_window;
	GdkWindow *window;
	GPtrArray *array;
	char **args, *xid_str = NULL;
	GError *error = NULL;
	gboolean ret;
	RBShell *shell;
	
	array = g_ptr_array_new ();
	g_ptr_array_add (array, "brasero");
	if (copy != FALSE)
		g_ptr_array_add (array, "-c");
	else
		g_ptr_array_add (array, "-r");
	g_ptr_array_add (array, (gpointer) path);

	g_object_get (pi, "object", &shell, NULL);
	g_object_get (shell, "window", &main_window, NULL);
	g_object_unref (shell);

	window = gtk_widget_get_window (main_window);
	if (window && GDK_IS_X11_WINDOW (window)) {
		int xid;
		xid = gdk_x11_window_get_xid (window);
		xid_str = g_strdup_printf ("%d", xid);
		g_ptr_array_add (array, "-x");
		g_ptr_array_add (array, xid_str);
	}
	
	g_ptr_array_add (array, NULL);
	args = (char **) g_ptr_array_free (array, FALSE);

	ret = TRUE;
	if (!g_spawn_async (NULL, args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
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
burn_playlist_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (data);
	source_burn (pi, RB_SOURCE (pi->selected_page));
}

static void
duplicate_cd_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (data);
	gchar *device;
	GVolume *volume;

	if (pi->selected_page == NULL || RB_IS_SOURCE (pi->selected_page) == FALSE)
		return;

	g_object_get (pi->selected_page, "volume", &volume, NULL);
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
	int num_tracks;

	num_tracks = gtk_tree_model_iter_n_children (model, NULL);

	g_simple_action_set_enabled (G_SIMPLE_ACTION (pi->burn_action), (num_tracks > 0));
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
	gboolean   playlist_active, is_audiocd_active;
	RBDisplayPage *selected_page;
	const char *page_type;

	if (pi->selected_page != NULL && RB_IS_PLAYLIST_SOURCE (pi->selected_page)) {
		RhythmDBQueryModel *model;

		g_object_get (pi->selected_page, "query-model", &model, NULL);
		g_signal_handlers_disconnect_by_func (model, playlist_row_inserted_cb, pi);
		g_signal_handlers_disconnect_by_func (model, playlist_entries_changed, pi);
		g_object_unref (model);
	}

	g_object_get (shell, "selected-page", &selected_page, NULL);

	/* for now restrict to playlist sources */
	playlist_active = RB_IS_PLAYLIST_SOURCE (selected_page);

	if (selected_page != NULL) {
		page_type = G_OBJECT_TYPE_NAME (selected_page);
		is_audiocd_active = g_str_equal (page_type, "RBAudioCdSource");
	} else {
		is_audiocd_active = FALSE;
	}

	if (pi->enabled && playlist_active && rb_disc_recorder_has_burner (pi)) {
		RhythmDBQueryModel *model;

		g_object_get (selected_page, "query-model", &model, NULL);
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
	} else {
		g_simple_action_set_enabled (G_SIMPLE_ACTION (pi->burn_action), FALSE);
	}

	if (pi->enabled && is_audiocd_active && is_copy_available (pi)) {
		g_simple_action_set_enabled (G_SIMPLE_ACTION (pi->copy_action), TRUE);
	} else {
		g_simple_action_set_enabled (G_SIMPLE_ACTION (pi->copy_action), FALSE);
	}

	if (pi->selected_page != NULL) {
		g_object_unref (pi->selected_page);
	}
	pi->selected_page = selected_page;
}

static void
shell_selected_page_notify_cb (RBShell            *shell,
			       GParamSpec         *param,
			       RBDiscRecorderPlugin *pi)
{
	rb_debug ("RBDiscRecorderPlugin selected page changed");
	update_source (pi, shell);
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (plugin);
	GMenuItem            *item;
	RBShell              *shell;
	GApplication         *app;

	GActionEntry actions[] = {
		{ "burn-playlist", burn_playlist_action_cb },
		{ "burn-duplicate-cd", duplicate_cd_action_cb }
	};

	g_object_get (pi, "object", &shell, NULL);

	pi->enabled = TRUE;

	rb_debug ("RBDiscRecorderPlugin activating");

	brasero_media_library_start ();

	g_signal_connect_object (G_OBJECT (shell),
				 "notify::selected-page",
				 G_CALLBACK (shell_selected_page_notify_cb),
				 pi, 0);

	app = g_application_get_default ();
	g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), pi);
	pi->burn_action = g_action_map_lookup_action (G_ACTION_MAP (app), "burn-playlist");
	pi->copy_action = g_action_map_lookup_action (G_ACTION_MAP (app), "burn-duplicate-cd");

	item = g_menu_item_new (_("Create Audio CD..."), "app.burn-playlist");
	rb_application_add_plugin_menu_item (RB_APPLICATION (g_application_get_default ()),
					     "playlist-menu",
					     "burn-playlist",
					     item);

	item = g_menu_item_new (_("Duplicate Audio CD..."), "app.burn-duplicate-cd");
	rb_application_add_plugin_menu_item (RB_APPLICATION (g_application_get_default ()),
					     "audiocd-toolbar",
					     "burn-duplicate-cd",
					     item);

	update_source (pi, shell);

	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *plugin)
{
	RBDiscRecorderPlugin *pi = RB_DISC_RECORDER_PLUGIN (plugin);
	RBShell              *shell;

	g_object_get (pi, "object", &shell, NULL);

	pi->enabled = FALSE;

	rb_debug ("RBDiscRecorderPlugin deactivating");

	update_source (pi, shell);

	if (pi->selected_page) {
		g_object_unref (pi->selected_page);
		pi->selected_page = NULL;
	}

	g_signal_handlers_disconnect_by_func (shell, shell_selected_page_notify_cb, pi);

	/* NOTE: don't deactivate libbrasero-media as it could be in use somewhere else */
	rb_application_remove_plugin_menu_item (RB_APPLICATION (g_application_get_default ()),
						"playlist-menu",
						"burn-playlist");
	rb_application_remove_plugin_menu_item (RB_APPLICATION (g_application_get_default ()),
						"audiocd-toolbar",
						"burn-duplicate-cd");

	g_object_unref (shell);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_disc_recorder_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_DISC_RECORDER_PLUGIN);
}
