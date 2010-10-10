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

#include "rb-podcast-main-source.h"
#include "rb-podcast-entry-types.h"
#include "rb-shell.h"
#include "eel-gconf-extensions.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-stock-icons.h"

#define CONF_STATE_PODCAST_PREFIX		CONF_PREFIX "/state/podcast"
#define CONF_STATE_PODCAST_DOWNLOAD_INTERVAL	CONF_STATE_PODCAST_PREFIX "/download_interval"
#define CONF_STATE_PODCAST_DOWNLOAD_DIR		CONF_STATE_PODCAST_PREFIX "/download_prefix"

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

	g_object_get (shell, "db", &db, NULL);
	base_query = rhythmdb_query_parse (db,
					   RHYTHMDB_QUERY_PROP_EQUALS,
					   RHYTHMDB_PROP_TYPE,
					   RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					   RHYTHMDB_QUERY_END);
	g_object_unref (db);

	source = RB_SOURCE (g_object_new (RB_TYPE_PODCAST_MAIN_SOURCE,
					  "name", _("Podcasts"),
					  "shell", shell,
					  "entry-type", RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					  "source-group", RB_SOURCE_GROUP_LIBRARY,
					  "search-type", RB_SOURCE_SEARCH_INCREMENTAL,
					  "podcast-manager", podcast_manager,
					  "base-query", base_query,
					  NULL));

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
						   RB_STOCK_AUTO_PLAYLIST);
	rhythmdb_query_free (query);
	rb_source_set_hidden_when_empty (podcast_subsource, TRUE);
	rb_shell_append_source (shell, podcast_subsource, RB_SOURCE (source));

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
						   RB_STOCK_AUTO_PLAYLIST);
	rhythmdb_query_free (query);
	rb_source_set_hidden_when_empty (podcast_subsource, TRUE);
	rb_shell_append_source (shell, podcast_subsource, RB_SOURCE (source));

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
		    RBPodcastMainSource *source)
{
	RBShell *shell;
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);

	g_object_get (source, "shell", &shell, NULL);
	rb_shell_notify_custom (shell, 4000, _("Finished downloading podcast"), podcast_name, NULL, FALSE);
	g_object_unref (shell);

	g_free (podcast_name);
}

static void
feed_updates_available_cb (RBPodcastManager *pd,
			   RhythmDBEntry *entry,
			   RBPodcastMainSource *source)
{
	RBShell *shell;
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);

	g_object_get (source, "shell", &shell, NULL);
	rb_shell_notify_custom (shell, 4000, _("New updates available from"), podcast_name, NULL, FALSE);
	g_object_unref (shell);

	g_free (podcast_name);

}

static gboolean
feed_error_cb (RBPodcastManager *pd,
	       const char *error,
	       gboolean existing,
	       RBPodcastMainSource *source)
{
	GtkWidget *dialog;
	int result;

	/* if the podcast feed doesn't already exist in the db,
	 * ask if the user wants to add it anyway; if it already
	 * exists, there's nothing to do besides reporting the error.
	 */
	dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source))),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 existing ? GTK_BUTTONS_OK : GTK_BUTTONS_YES_NO,
					 _("Error in podcast"));

	if (existing) {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", error);
	} else {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("%s. Would you like to add the podcast feed anyway?"), error);
	}

	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	/* in the existing feed case, the response will be _OK or _NONE.
	 * we want to return FALSE here in this case, so this check works.
	 */
	return (result == GTK_RESPONSE_YES);
}

static void
rb_podcast_main_source_btn_file_change_cb (GtkFileChooserButton *widget, const char *key)
{
	char *uri;
	
	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (widget));
	eel_gconf_set_string (key, uri);
	g_free (uri);
}

static void
rb_podcast_main_source_cb_interval_changed_cb (GtkComboBox *box, gpointer cb_data)
{
	RBPodcastManager *podcast_mgr;

	guint index = gtk_combo_box_get_active (box);
	eel_gconf_set_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL,
			       index);

	g_object_get (cb_data, "podcast-manager", &podcast_mgr, NULL);
	rb_podcast_manager_start_sync (podcast_mgr);
	g_object_unref (podcast_mgr);
}

static GtkWidget *
impl_get_config_widget (RBSource *asource, RBShellPreferences *prefs)
{
	RBPodcastMainSource *source = RB_PODCAST_MAIN_SOURCE (asource);
	RBPodcastManager *podcast_mgr;
	GtkBuilder *builder;
	GtkWidget *cb_update_interval;
	GtkWidget *btn_file;
	char *download_dir;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	builder = rb_builder_load ("podcast-prefs.ui", source);
	source->priv->config_widget = GTK_WIDGET (gtk_builder_get_object (builder, "podcast_vbox"));

	btn_file = GTK_WIDGET (gtk_builder_get_object (builder, "location_chooser"));
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (btn_file),
					      rb_music_dir (),
					      NULL);

	g_object_get (source, "podcast-manager", &podcast_mgr, NULL);
	download_dir = rb_podcast_manager_get_podcast_dir (podcast_mgr);
	g_object_unref (podcast_mgr);

	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (btn_file),
						 download_dir);
	g_free (download_dir);

	g_signal_connect (btn_file,
			  "selection-changed",
			  G_CALLBACK (rb_podcast_main_source_btn_file_change_cb),
			  CONF_STATE_PODCAST_DOWNLOAD_DIR);

	cb_update_interval = GTK_WIDGET (gtk_builder_get_object (builder, "cb_update_interval"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (cb_update_interval),
				  eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL));
	g_signal_connect (cb_update_interval,
			  "changed",
			  G_CALLBACK (rb_podcast_main_source_cb_interval_changed_cb),
			  source);

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
	GdkPixbuf *pixbuf;
	gint size;
	
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
			  	"feed_updates_available",
 			  	G_CALLBACK (feed_updates_available_cb),
			  	source, 0);

	g_signal_connect_object (podcast_mgr,
			  	 "process_error",
			 	 G_CALLBACK (feed_error_cb),
			  	 source, 0);

	gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   RB_STOCK_PODCAST,
					   size,
					   0, NULL);

	if (pixbuf != NULL) {
		rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
		g_object_unref (pixbuf);
	}
}

static void
impl_dispose (GObject *object)
{
	RBPodcastMainSource *source;

	source = RB_PODCAST_MAIN_SOURCE (object);
	if (source->priv->config_widget != NULL) {
		g_object_unref (source->priv->config_widget);
		source->priv->config_widget = NULL;
	}

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
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->constructed = impl_constructed;

	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_add_uri = impl_add_uri;
	
	g_type_class_add_private (klass, sizeof (RBPodcastMainSourcePrivate));
}
