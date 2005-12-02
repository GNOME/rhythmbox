/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
 *  arch-tag: Implementation of preferences dialog object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnome/gnome-help.h>

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-source.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"

static void rb_shell_preferences_class_init (RBShellPreferencesClass *klass);
static void rb_shell_preferences_init (RBShellPreferences *shell_preferences);
static void rb_shell_preferences_finalize (GObject *object);
static gboolean rb_shell_preferences_window_delete_cb (GtkWidget *window,
				                       GdkEventAny *event,
				                       RBShellPreferences *shell_preferences);
static void rb_shell_preferences_response_cb (GtkDialog *dialog,
				              int response_id,
				              RBShellPreferences *shell_preferences);
static void rb_shell_preferences_ui_pref_changed (GConfClient *client,
						  guint cnxn_id,
						  GConfEntry *entry,
						  RBShellPreferences *shell_preferences);
static void rb_shell_preferences_sync (RBShellPreferences *shell_preferences);

void rb_shell_preferences_column_check_changed_cb (GtkCheckButton *butt,
						   RBShellPreferences *shell_preferences);


enum
{
	PROP_0,
};

const char *styles[] = { "desktop_default", "both", "both_horiz", "icon", "text" };

struct RBShellPreferencesPrivate
{
	GtkWidget *notebook;

	GtkWidget *config_widget;
	GtkWidget *artist_check;
	GtkWidget *album_check;
	GtkWidget *genre_check;
	GtkWidget *duration_check;
	GtkWidget *track_check;
	GtkWidget *rating_check;
	GtkWidget *play_count_check;
	GtkWidget *last_played_check;
	GtkWidget *first_seen_check;
	GtkWidget *quality_check;
	GtkWidget *year_check;

	gboolean loading;
};

#define RB_SHELL_PREFERENCES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_PREFERENCES, RBShellPreferencesPrivate))

G_DEFINE_TYPE (RBShellPreferences, rb_shell_preferences, GTK_TYPE_DIALOG)

static void
rb_shell_preferences_class_init (RBShellPreferencesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_shell_preferences_finalize;

	g_type_class_add_private (klass, sizeof (RBShellPreferencesPrivate));
}

static void
help_cb (GtkWidget *widget,
	 RBShellPreferences *shell_preferences)
{
	GError *error = NULL;

	gnome_help_display ("rhythmbox.xml", "prefs", &error);

	if (error != NULL) {
		rb_error_dialog (NULL,
				 _("Couldn't display help"),
				 "%s", error->message);

		g_error_free (error);
	}
}

static void
rb_shell_preferences_init (RBShellPreferences *shell_preferences)
{
	GtkWidget *help;
	GladeXML *xml;

	shell_preferences->priv = RB_SHELL_PREFERENCES_GET_PRIVATE (shell_preferences);

	g_signal_connect_object (G_OBJECT (shell_preferences),
				 "delete_event",
				 G_CALLBACK (rb_shell_preferences_window_delete_cb),
				 shell_preferences, 0);
	g_signal_connect_object (G_OBJECT (shell_preferences),
				 "response",
				 G_CALLBACK (rb_shell_preferences_response_cb),
				 shell_preferences, 0);

	gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	help = gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			              GTK_STOCK_HELP,
			              GTK_RESPONSE_HELP);
	g_signal_connect_object (G_OBJECT (help), "clicked",
				 G_CALLBACK (help_cb), shell_preferences, 0);
	gtk_dialog_set_default_response (GTK_DIALOG (shell_preferences),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (shell_preferences), _("Music Player Preferences"));
	gtk_window_set_resizable (GTK_WINDOW (shell_preferences), FALSE);

	shell_preferences->priv->notebook = GTK_WIDGET (gtk_notebook_new ());
	gtk_container_set_border_width (GTK_CONTAINER (shell_preferences->priv->notebook), 5);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (shell_preferences)->vbox),
			   shell_preferences->priv->notebook);

	gtk_container_set_border_width (GTK_CONTAINER (shell_preferences), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (shell_preferences)->vbox), 2);
	gtk_dialog_set_has_separator (GTK_DIALOG (shell_preferences), FALSE);

	xml = rb_glade_xml_new ("general-prefs.glade",
				"general_vbox",
				shell_preferences);

	rb_glade_boldify_label (xml, "visible_columns_label");
	
	/* Columns */
	shell_preferences->priv->artist_check =
		glade_xml_get_widget (xml, "artist_check");
	shell_preferences->priv->album_check =
		glade_xml_get_widget (xml, "album_check");
	shell_preferences->priv->genre_check =
		glade_xml_get_widget (xml, "genre_check");
	shell_preferences->priv->duration_check =
		glade_xml_get_widget (xml, "duration_check");
	shell_preferences->priv->track_check =
		glade_xml_get_widget (xml, "track_check");
	shell_preferences->priv->rating_check =
		glade_xml_get_widget (xml, "rating_check");
	shell_preferences->priv->play_count_check =
		glade_xml_get_widget (xml, "play_count_check");
	shell_preferences->priv->last_played_check =
		glade_xml_get_widget (xml, "last_played_check");
	shell_preferences->priv->quality_check =
		glade_xml_get_widget (xml, "quality_check");
	shell_preferences->priv->year_check =
		glade_xml_get_widget (xml, "year_check");
	shell_preferences->priv->first_seen_check =
		glade_xml_get_widget (xml, "first_seen_check");

	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  glade_xml_get_widget (xml, "general_vbox"),
				  gtk_label_new (_("General")));

	g_object_unref (G_OBJECT (xml));
	
	eel_gconf_notification_add (CONF_UI_DIR,
				    (GConfClientNotifyFunc) rb_shell_preferences_ui_pref_changed,
				    shell_preferences);

	rb_shell_preferences_sync (shell_preferences);
}

static void
rb_shell_preferences_finalize (GObject *object)
{
	RBShellPreferences *shell_preferences;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PREFERENCES (object));

	shell_preferences = RB_SHELL_PREFERENCES (object);

	g_return_if_fail (shell_preferences->priv != NULL);

	G_OBJECT_CLASS (rb_shell_preferences_parent_class)->finalize (object);
}

void
rb_shell_references_append_page (RBShellPreferences *prefs,
				 const char *name,
				 GtkWidget *widget)
{
	GtkWidget *label;
		
	label = gtk_label_new (name);
	gtk_notebook_append_page (GTK_NOTEBOOK (prefs->priv->notebook),
				  widget,
				  label);
}

static void
rb_shell_preferences_append_view_page (RBShellPreferences *prefs,
				       const char *name,
				       RBSource *source)
{
	GtkWidget *widget;

	g_return_if_fail (RB_IS_SHELL_PREFERENCES (prefs));
	g_return_if_fail (RB_IS_SOURCE (source));

	widget = rb_source_get_config_widget (source, prefs);
	if (!widget)
		return;

	rb_shell_references_append_page (prefs, name, widget);
}

#ifdef WITH_DAAP_SUPPORT

#define CONF_ENABLE_SHARING "/apps/rhythmbox/sharing/enable_sharing"
#define CONF_NAME "/apps/rhythmbox/sharing/share_name"

static void
share_check_button_toggled_cb (GtkToggleButton *button, GtkWidget *hbox)
{
	gboolean b;

	b = gtk_toggle_button_get_active (button);

	eel_gconf_set_boolean (CONF_ENABLE_SHARING, b);

	gtk_widget_set_sensitive (hbox, b);
	
	return;
}

static gboolean
share_name_entry_focus_out_event_cb (GtkEntry *entry, GdkEventFocus *event, gpointer data)
{
	const gchar *name;
	char *old_name;

	name = gtk_entry_get_text (entry);
	old_name = eel_gconf_get_string (CONF_NAME);

	if (strcmp (name, old_name) != 0)
		eel_gconf_set_string (CONF_NAME, name);

	g_free (old_name);

	return FALSE;
}

static void
add_daap_preferences (RBShellPreferences *shell_preferences)
{
	GladeXML *xml;
	GtkWidget *check;
	GtkWidget *entry;
	GtkWidget *hbox;
	gboolean b;

	gchar *name;

	xml = rb_glade_xml_new ("daap-prefs.glade",
				"daap_vbox",
				shell_preferences);

	check = glade_xml_get_widget (xml, "daap_enable_check");
	entry = glade_xml_get_widget (xml, "daap_name_entry");
	hbox = glade_xml_get_widget (xml, "daap_hbox");
	
	b = eel_gconf_get_boolean (CONF_ENABLE_SHARING);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), b);
	g_signal_connect (G_OBJECT (check), "toggled", G_CALLBACK (share_check_button_toggled_cb), hbox);
	
	name = eel_gconf_get_string (CONF_NAME);
	gtk_entry_set_text (GTK_ENTRY (entry), name);
	g_signal_connect (G_OBJECT (entry), "focus-out-event", G_CALLBACK (share_name_entry_focus_out_event_cb), NULL);	

	gtk_widget_set_sensitive (hbox, b);
	
	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  glade_xml_get_widget (xml, "daap_vbox"),
				  gtk_label_new (_("Sharing")));
}
#endif /* WITH_DAAP_SUPPORT */

GtkWidget *
rb_shell_preferences_new (GList *views)
{
	RBShellPreferences *shell_preferences;

	shell_preferences = g_object_new (RB_TYPE_SHELL_PREFERENCES,
				          NULL, NULL);

	g_return_val_if_fail (shell_preferences->priv != NULL, NULL);

	for (; views; views = views->next)
	{
		const char *name = NULL;
		g_object_get (G_OBJECT (views->data), "name", &name, NULL);
		g_assert (name != NULL);
		rb_shell_preferences_append_view_page (shell_preferences,
						       name,
						       RB_SOURCE (views->data));
	}

#ifdef WITH_DAAP_SUPPORT
	add_daap_preferences (shell_preferences);
#endif /* WITH_DAAP_SUPPORT */

	return GTK_WIDGET (shell_preferences);
}

static gboolean
rb_shell_preferences_window_delete_cb (GtkWidget *window,
				       GdkEventAny *event,
				       RBShellPreferences *shell_preferences)
{
	gtk_widget_hide (GTK_WIDGET (shell_preferences));

	return TRUE;
}

static void
rb_shell_preferences_response_cb (GtkDialog *dialog,
				  int response_id,
				  RBShellPreferences *shell_preferences)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_widget_hide (GTK_WIDGET (shell_preferences));
}

static void
rb_shell_preferences_ui_pref_changed (GConfClient *client,
				      guint cnxn_id,
				      GConfEntry *entry,
				      RBShellPreferences *shell_preferences)
{
	if (shell_preferences->priv->loading == TRUE)
		return;

	rb_shell_preferences_sync (shell_preferences);
}

void
rb_shell_preferences_column_check_changed_cb (GtkCheckButton *butt,
					      RBShellPreferences *shell_preferences)
{
	GString *newcolumns = g_string_new ("");
	char *currentcols = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);
	char **colnames = currentcols ? g_strsplit (currentcols, ",", 0) : NULL;
	char *colname = NULL;
	int i;

	if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->artist_check))
		colname = "RHYTHMDB_PROP_ARTIST";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->album_check))
		colname = "RHYTHMDB_PROP_ALBUM";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->genre_check))
		colname = "RHYTHMDB_PROP_GENRE";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->duration_check))
		colname = "RHYTHMDB_PROP_DURATION";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->track_check))
		colname = "RHYTHMDB_PROP_TRACK_NUMBER";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->rating_check))
		colname = "RHYTHMDB_PROP_RATING";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->play_count_check))
		colname = "RHYTHMDB_PROP_PLAY_COUNT";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->last_played_check))
		colname = "RHYTHMDB_PROP_LAST_PLAYED";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->year_check))
		colname = "RHYTHMDB_PROP_DATE";
	else if (shell_preferences->priv->quality_check
		 && butt == GTK_CHECK_BUTTON (shell_preferences->priv->quality_check))
		colname = "RHYTHMDB_PROP_BITRATE";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->first_seen_check))
		colname = "RHYTHMDB_PROP_FIRST_SEEN";
	else
		g_assert_not_reached ();

	rb_debug ("\"%s\" changed, current cols are \"%s\"", colname, currentcols);
	
	/* Append this if we want it */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (butt))) {
		g_string_append (newcolumns, colname);
		g_string_append (newcolumns, ",");
	}

	/* Append everything else */
	for (i = 0; colnames != NULL && colnames[i] != NULL; i++) {
		if (strcmp (colnames[i], colname)) {
			g_string_append (newcolumns, colnames[i]);
			if (colnames[i+1] != NULL)
				g_string_append (newcolumns, ",");				
		}
	}

	eel_gconf_set_string (CONF_UI_COLUMNS_SETUP, newcolumns->str);
	g_string_free (newcolumns, TRUE);
}

static void
rb_shell_preferences_sync_column_button (RBShellPreferences *preferences,
					 GtkWidget *button,
					 const char *columns,
					 const char *propid)
{
	g_signal_handlers_block_by_func (G_OBJECT (button),
					 rb_shell_preferences_column_check_changed_cb,
					 preferences);
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), 
				      strstr (columns, propid) != NULL);

	g_signal_handlers_unblock_by_func (G_OBJECT (button),
					   rb_shell_preferences_column_check_changed_cb,
					   preferences);
}

static void
rb_shell_preferences_sync (RBShellPreferences *shell_preferences)
{
	char *columns;

	shell_preferences->priv->loading = TRUE;

	rb_debug ("syncing prefs");
	
	columns = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);
	if (columns != NULL)
	{
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->artist_check,
							 columns, "RHYTHMDB_PROP_ARTIST");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->album_check,
							 columns, "RHYTHMDB_PROP_ALBUM");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->genre_check,
							 columns, "RHYTHMDB_PROP_GENRE");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->duration_check,
							 columns, "RHYTHMDB_PROP_DURATION");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->track_check,
							 columns, "RHYTHMDB_PROP_TRACK_NUMBER");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->rating_check,
							 columns, "RHYTHMDB_PROP_RATING");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->play_count_check,
							 columns, "RHYTHMDB_PROP_PLAY_COUNT");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->last_played_check,
							 columns, "RHYTHMDB_PROP_LAST_PLAYED");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->year_check,
							 columns, "RHYTHMDB_PROP_DATE");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->first_seen_check,
							 columns, "RHYTHMDB_PROP_FIRST_SEEN");
		if (shell_preferences->priv->quality_check)
			rb_shell_preferences_sync_column_button (shell_preferences,
				       				 shell_preferences->priv->quality_check,
								 columns, "RHYTHMDB_PROP_BITRATE");
	}

	g_free (columns);

	shell_preferences->priv->loading = FALSE;
}
