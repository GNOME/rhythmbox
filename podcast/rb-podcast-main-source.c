/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include "rb-podcast-settings.h"
#include "rb-podcast-main-source.h"
#include "rb-podcast-entry-types.h"
#include "rb-shell.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-application.h"
#include "rb-debug.h"

struct _RBPodcastMainSourcePrivate
{
	GtkWidget *config_widget;
};

G_DEFINE_TYPE (RBPodcastMainSource, rb_podcast_main_source, RB_TYPE_PODCAST_SOURCE)


RBSource *
rb_podcast_main_source_new (RBShell *shell, RBPodcastManager *podcast_manager)
{
	RBSource *source;
	RhythmDBQuery *base_query;
	RhythmDB *db;
	GSettings *settings;
	GtkBuilder *builder;
	GMenu *toolbar;

	g_object_get (shell, "db", &db, NULL);
	base_query = rhythmdb_query_parse (db,
					   RHYTHMDB_QUERY_PROP_EQUALS,
					   RHYTHMDB_PROP_TYPE,
					   RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					   RHYTHMDB_QUERY_END);
	g_object_unref (db);

	settings = g_settings_new (PODCAST_SETTINGS_SCHEMA);

	builder = rb_builder_load ("podcast-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "podcast-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	source = RB_SOURCE (g_object_new (RB_TYPE_PODCAST_MAIN_SOURCE,
					  "name", _("Podcasts"),
					  "shell", shell,
					  "entry-type", RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					  "podcast-manager", podcast_manager,
					  "base-query", base_query,
					  "settings", g_settings_get_child (settings, "source"),
					  "toolbar-menu", toolbar,
					  "show-all-feeds", TRUE,
					  NULL));
	g_object_unref (settings);
	g_object_unref (builder);

	rhythmdb_query_free (base_query);

	rb_shell_register_entry_type_for_source (shell, source,
						 RHYTHMDB_ENTRY_TYPE_PODCAST_FEED);
	rb_shell_register_entry_type_for_source (shell, source,
						 RHYTHMDB_ENTRY_TYPE_PODCAST_POST);

	return source;
}

void
rb_podcast_main_source_add_subsources (RBPodcastMainSource *source)
{
	RhythmDBQuery *query;
	RBSource *podcast_subsource;
	RBPodcastManager *podcast_mgr;
	RhythmDB *db;
	RBShell *shell;

	g_object_get (source,
		      "shell", &shell,
		      "podcast-manager", &podcast_mgr,
		      NULL);
	g_object_get (shell, "db", &db, NULL);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				      RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN,
				      RHYTHMDB_PROP_FIRST_SEEN,
				      3600 * 24 * 7,
				      RHYTHMDB_QUERY_END);

	podcast_subsource = rb_podcast_source_new (shell,
						   podcast_mgr,
						   query,
						   _("New Episodes"),
						   "document-open-recent-symbolic");
	rhythmdb_query_free (query);
	rb_source_set_hidden_when_empty (podcast_subsource, TRUE);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (podcast_subsource), RB_DISPLAY_PAGE (source));

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				      RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN,
				      RHYTHMDB_PROP_LAST_SEEN,
				      3600 * 24 * 7,
				      RHYTHMDB_QUERY_END);

	podcast_subsource = rb_podcast_source_new (shell,
						   podcast_mgr,
						   query,
						   _("New Downloads"),		/* better name? */
						   "folder-download-symbolic");
	rhythmdb_query_free (query);
	rb_source_set_hidden_when_empty (podcast_subsource, TRUE);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (podcast_subsource), RB_DISPLAY_PAGE (source));

	g_object_unref (db);
	g_object_unref (shell);
}

static void
start_download_cb (RBPodcastManager *pd,
		   RhythmDBEntry *entry,
		   RBPodcastMainSource *source)
{
	RBShell *shell;
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);

	g_object_get (source, "shell", &shell, NULL);
	rb_shell_notify_custom (shell, 4000, _("Downloading podcast"), podcast_name, NULL, FALSE);
	g_object_unref (shell);

	g_free (podcast_name);
}

static void
finish_download_cb (RBPodcastManager *pd,
		    RhythmDBEntry *entry,
		    GError *error,
		    RBPodcastMainSource *source)
{
	RBShell *shell;
	char *podcast_name;
	char *primary, *secondary;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);

	g_object_get (source, "shell", &shell, NULL);

	if (error) {
		primary = _("Error downloading podcast");
		secondary = g_strdup_printf ("%s\n\n%s", podcast_name, error->message);
	} else {
		primary = _("Finished downloading podcast");
		secondary = g_strdup_printf ("%s", podcast_name);
	}

	rb_shell_notify_custom (shell, 4000, primary, secondary, NULL, FALSE);
	g_object_unref (shell);

	g_free (podcast_name);
	g_free (secondary);
}

static void
error_dialog_response_cb (GtkDialog *dialog, int response, RBPodcastMainSource *source)
{
	const char *url = g_object_get_data (G_OBJECT (dialog), "feed-url");

	if (response == GTK_RESPONSE_YES) {
		RBPodcastManager *pd;
		g_object_get (source, "podcast-manager", &pd, NULL);
		rb_podcast_manager_insert_feed_url (pd, url);
		g_object_unref (pd);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
feed_update_status_cb (RBPodcastManager *mgr, const char *url, RBPodcastFeedUpdateStatus status, const char *error, gpointer data)
{
	RBPodcastSource *source;
	RhythmDBEntry *entry;
	RBShell *shell;
	char *podcast_name;
	char *nice_error;
	GtkWidget *dialog;
	RhythmDB *db;

	source = data;
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	entry = rhythmdb_entry_lookup_by_location (db, url);

	switch (status) {
	case RB_PODCAST_FEED_UPDATE_ERROR:
		/* if the podcast feed doesn't already exist in the db,
		 * ask if the user wants to add it anyway; if it already
		 * exists, there's nothing to do besides reporting the error.
		 */
		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source))),
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 (entry != NULL) ? GTK_BUTTONS_OK : GTK_BUTTONS_YES_NO,
						 _("Error in podcast"));

		nice_error = g_strdup_printf (_("There was a problem adding this podcast: %s.  Please verify the URL: %s"), error, url);
		if (entry != NULL) {
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
								  "%s", nice_error);
		} else {
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
								  _("%s. Would you like to add the podcast feed anyway?"), nice_error);
		}
		g_free (nice_error);

		gtk_window_set_title (GTK_WINDOW (dialog), "");
		gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

		g_object_set_data_full (G_OBJECT (dialog), "feed-url", g_strdup (url), g_free);
		g_signal_connect (dialog, "response", G_CALLBACK (error_dialog_response_cb), source);

		gtk_widget_show_all (dialog);

		break;

	case RB_PODCAST_FEED_UPDATE_UPDATED:
		podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);

		rb_shell_notify_custom (shell, 4000, _("New updates available from"), podcast_name, NULL, FALSE);

		g_free (podcast_name);
		break;

	default:
		break;
	}

	g_object_unref (shell);
	g_object_unref (db);
}

static void
rb_podcast_main_source_btn_file_change_cb (GtkFileChooserButton *widget, RBPodcastSource *source)
{
	GSettings *settings;
	char *uri;

	settings = g_settings_new (PODCAST_SETTINGS_SCHEMA);

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (widget));
	g_settings_set_string (settings, PODCAST_DOWNLOAD_DIR_KEY, uri);
	g_free (uri);

	g_object_unref (settings);
}

static GtkWidget *
impl_get_config_widget (RBDisplayPage *page, RBShellPreferences *prefs)
{
	RBPodcastMainSource *source = RB_PODCAST_MAIN_SOURCE (page);
	RBPodcastManager *podcast_mgr;
	GtkBuilder *builder;
	GtkWidget *update_interval;
	GtkWidget *btn_file;
	GSettings *settings;
	char *download_dir;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	builder = rb_builder_load ("podcast-prefs.ui", source);
	source->priv->config_widget = GTK_WIDGET (gtk_builder_get_object (builder, "podcast_vbox"));

	btn_file = GTK_WIDGET (gtk_builder_get_object (builder, "location_chooser"));
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (btn_file),
					      rb_music_dir (),
					      NULL);

	g_object_get (source,
		      "podcast-manager", &podcast_mgr,
		      NULL);
	download_dir = rb_podcast_manager_get_podcast_dir (podcast_mgr);

	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (btn_file),
						 download_dir);
	g_object_unref (podcast_mgr);
	g_free (download_dir);

	g_signal_connect_object (btn_file,
				 "selection-changed",
				 G_CALLBACK (rb_podcast_main_source_btn_file_change_cb),
				 source, 0);

	update_interval = GTK_WIDGET (gtk_builder_get_object (builder, "update_interval"));
	g_object_set (update_interval, "id-column", 1, NULL);

	settings = g_settings_new (PODCAST_SETTINGS_SCHEMA);
	g_settings_bind (settings, PODCAST_DOWNLOAD_INTERVAL,
			 update_interval, "active-id",
			 G_SETTINGS_BIND_DEFAULT);
	g_object_unref (settings);

	return source->priv->config_widget;
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	if (g_str_has_prefix (uri, "http://") == FALSE)
		return 0;

	if (g_str_has_suffix (uri, ".xml") ||
	    g_str_has_suffix (uri, ".rss"))
		return 100;

	return 0;
}

static void
impl_add_uri (RBSource *source,
	      const char *uri,
	      const char *title,
	      const char *genre,
	      RBSourceAddCallback callback,
	      gpointer data,
	      GDestroyNotify destroy_data)
{
	RBPodcastManager *podcast_mgr;

	g_object_get (source, "podcast-manager", &podcast_mgr, NULL);
	rb_podcast_manager_subscribe_feed (podcast_mgr, uri, FALSE);
	g_object_unref (podcast_mgr);

	if (callback != NULL) {
		callback (source, uri, data);
		if (destroy_data != NULL) {
			destroy_data (data);
		}
	}
}

static void
impl_constructed (GObject *object)
{
	RBPodcastMainSource *source;
	RBPodcastManager *podcast_mgr;

	RB_CHAIN_GOBJECT_METHOD (rb_podcast_main_source_parent_class, constructed, object);
	source = RB_PODCAST_MAIN_SOURCE (object);

	g_object_get (source, "podcast-manager", &podcast_mgr, NULL);

	g_signal_connect_object (podcast_mgr,
			        "start_download",
				G_CALLBACK (start_download_cb),
				source, 0);

	g_signal_connect_object (podcast_mgr,
				"finish_download",
				G_CALLBACK (finish_download_cb),
				source, 0);

	g_signal_connect_object (podcast_mgr,
				 "feed-update-status",
				 G_CALLBACK (feed_update_status_cb),
				 source, 0);

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "application-rss+xml-symbolic");
}

static void
impl_dispose (GObject *object)
{
	RBPodcastMainSource *source;

	source = RB_PODCAST_MAIN_SOURCE (object);

	g_clear_object (&source->priv->config_widget);

	G_OBJECT_CLASS (rb_podcast_main_source_parent_class)->dispose (object);
}

static void
rb_podcast_main_source_init (RBPodcastMainSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source,
						    RB_TYPE_PODCAST_MAIN_SOURCE,
						    RBPodcastMainSourcePrivate);
}

static void
rb_podcast_main_source_class_init (RBPodcastMainSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->constructed = impl_constructed;

	page_class->get_config_widget = impl_get_config_widget;

	source_class->want_uri = impl_want_uri;
	source_class->add_uri = impl_add_uri;

	g_type_class_add_private (klass, sizeof (RBPodcastMainSourcePrivate));
}
