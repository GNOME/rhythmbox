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

/**
 * SECTION:rb-shell-preferences
 * @short_description: preferences dialog
 *
 * The preferences dialog is built around a #GtkNotebook widget, with two built-in
 * pages and additional pages for various sources.
 *
 * The 'general' preferences page controls the set of browser views that are visible
 * (artist and album; genre and artist; or genre, artist, and album), the columns
 * that are visible, and the appearance of buttons in the main toolbar.  The browser
 * and column settings apply to all sources.
 *
 * The 'playback' preferences page controls whether the crossfading player backend is used,
 * and if enabled, the crossfade duration and network buffer size.
 *
 * Currently, the library and podcast sources add pages to the notebook, for configuring the
 * location and layout of the library and the podcast download location and update frequency.
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#if !GTK_CHECK_VERSION(2,14,0)
#include <libgnome/gnome-help.h>
#endif

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-source.h"
#include "rb-builder-helpers.h"
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
void rb_shell_preferences_browser_views_activated_cb (GtkWidget *widget,
						      RBShellPreferences *shell_preferences);
static void rb_shell_preferences_toolbar_style_cb (GtkComboBox *box,
						   RBShellPreferences *preferences);
static void rb_shell_preferences_player_backend_cb (GtkToggleButton *button,
						    RBShellPreferences *preferences);
static void rb_shell_preferences_transition_duration_cb (GtkRange *range,
							 RBShellPreferences *preferences);
static void rb_shell_preferences_network_buffer_size_cb (GtkRange *range,
							 RBShellPreferences *preferences);
static void update_playback_prefs_sensitivity (RBShellPreferences *preferences);

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
	GtkWidget *location_check;

	GtkWidget *xfade_backend_check;
	GtkWidget *transition_duration;
	GtkWidget *network_buffer_size;

	GSList *browser_views_group;

	GtkWidget *toolbar_style_menu;

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

#if GTK_CHECK_VERSION(2,14,0)
	gtk_show_uri (gtk_widget_get_screen (widget),
		      "ghelp:rhythmbox?prefs",
		      gtk_get_current_event_time (),
		      &error);
#else
	gnome_help_display ("rhythmbox.xml", "prefs", &error);
#endif

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
	GtkWidget *tmp;
	GtkBuilder *builder;

	shell_preferences->priv = RB_SHELL_PREFERENCES_GET_PRIVATE (shell_preferences);

	g_signal_connect_object (shell_preferences,
				 "delete_event",
				 G_CALLBACK (rb_shell_preferences_window_delete_cb),
				 shell_preferences, 0);
	g_signal_connect_object (shell_preferences,
				 "response",
				 G_CALLBACK (rb_shell_preferences_response_cb),
				 shell_preferences, 0);

	gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	tmp = gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			              GTK_STOCK_HELP,
			              GTK_RESPONSE_HELP);
	g_signal_connect_object (tmp, "clicked",
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

	builder = rb_builder_load ("general-prefs.ui", shell_preferences);

	rb_builder_boldify_label (builder, "visible_columns_label");

	/* Columns */
	shell_preferences->priv->artist_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "artist_check"));
	shell_preferences->priv->album_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "album_check"));
	shell_preferences->priv->genre_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "genre_check"));
	shell_preferences->priv->duration_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "duration_check"));
	shell_preferences->priv->track_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "track_check"));
	shell_preferences->priv->rating_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "rating_check"));
	shell_preferences->priv->play_count_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "play_count_check"));
	shell_preferences->priv->last_played_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "last_played_check"));
	shell_preferences->priv->quality_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "quality_check"));
	shell_preferences->priv->year_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "year_check"));
	shell_preferences->priv->first_seen_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "first_seen_check"));
	shell_preferences->priv->location_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "location_check"));

	/* browser options */
	rb_builder_boldify_label (builder, "browser_views_label");

	tmp = GTK_WIDGET (gtk_builder_get_object (builder, "library_browser_views_radio"));
	shell_preferences->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group
					       (GTK_RADIO_BUTTON (tmp))));

	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  GTK_WIDGET (gtk_builder_get_object (builder, "general_vbox")),
				  gtk_label_new (_("General")));

	/* toolbar button style */
	rb_builder_boldify_label (builder, "toolbar_style_label");
	shell_preferences->priv->toolbar_style_menu =
		GTK_WIDGET (gtk_builder_get_object (builder, "toolbar_style_menu"));
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (shell_preferences->priv->toolbar_style_menu),
					      rb_combo_box_hyphen_separator_func,
					      NULL, NULL);
	g_signal_connect_object (G_OBJECT (shell_preferences->priv->toolbar_style_menu),
				 "changed", G_CALLBACK (rb_shell_preferences_toolbar_style_cb),
				 shell_preferences, 0);

	eel_gconf_notification_add (CONF_UI_DIR,
				    (GConfClientNotifyFunc) rb_shell_preferences_ui_pref_changed,
				    shell_preferences);
	g_object_unref (builder);

	/* playback preferences */
	builder = rb_builder_load ("playback-prefs.ui", shell_preferences);

	rb_builder_boldify_label (builder, "backend_label");
	rb_builder_boldify_label (builder, "duration_label");
	rb_builder_boldify_label (builder, "buffer_size_label");

	shell_preferences->priv->xfade_backend_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "use_xfade_backend"));
	shell_preferences->priv->transition_duration =
		GTK_WIDGET (gtk_builder_get_object (builder, "duration"));
	shell_preferences->priv->network_buffer_size =
		GTK_WIDGET (gtk_builder_get_object (builder, "network_buffer_size"));

	g_signal_connect_object (shell_preferences->priv->xfade_backend_check,
				 "toggled",
				 G_CALLBACK (rb_shell_preferences_player_backend_cb),
				 shell_preferences, 0);

	g_signal_connect_object (shell_preferences->priv->transition_duration,
				 "value-changed",
				 G_CALLBACK (rb_shell_preferences_transition_duration_cb),
				 shell_preferences, 0);

	g_signal_connect_object (shell_preferences->priv->network_buffer_size,
				 "value-changed",
				 G_CALLBACK (rb_shell_preferences_network_buffer_size_cb),
				 shell_preferences, 0);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  GTK_WIDGET (gtk_builder_get_object (builder, "playback_prefs_box")),
				  gtk_label_new (_("Playback")));
	g_object_unref (builder);

	eel_gconf_notification_add (CONF_PLAYER_DIR,
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

/**
 * rb_shell_preferences_append_page:
 * @prefs: the #RBShellPreferences instance
 * @name: name of the page to append
 * @widget: the #GtkWidget to use as the contents of the page
 *
 * Appends a new page to the preferences dialog notebook.
 */
void
rb_shell_preferences_append_page (RBShellPreferences *prefs,
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

	rb_shell_preferences_append_page (prefs, name, widget);
}

/**
 * rb_shell_preferences_new:
 * @views: list of #RBSource objects to check for preferences pages
 *
 * Creates the #RBShellPreferences instance, populating it with the
 * preferences pages for the sources in the list.
 *
 * Return value: the #RBShellPreferences instance
 */
GtkWidget *
rb_shell_preferences_new (GList *views)
{
	RBShellPreferences *shell_preferences;

	shell_preferences = g_object_new (RB_TYPE_SHELL_PREFERENCES,
				          NULL, NULL);

	g_return_val_if_fail (shell_preferences->priv != NULL, NULL);

	for (; views; views = views->next)
	{
		char *name = NULL;
		g_object_get (views->data, "name", &name, NULL);
		if (name == NULL) {
			g_warning ("Source %p of type %s has no name",
				   views->data,
				   G_OBJECT_TYPE_NAME (views->data));
			continue;
		}
		rb_shell_preferences_append_view_page (shell_preferences,
						       name,
						       RB_SOURCE (views->data));
		g_free (name);
	}

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

/**
 * rb_shell_preferences_column_check_changed_cb:
 * @butt: the #GtkCheckButton that was changed
 * @shell_preferences: the #RBShellPreferences instance
 *
 * Signal handler used for the checkboxes used to configure the set of visible columns.
 * This updates the GConf key that contains the list of visible columns.
 */
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
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->quality_check))
		colname = "RHYTHMDB_PROP_BITRATE";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->first_seen_check))
		colname = "RHYTHMDB_PROP_FIRST_SEEN";
	else if (butt == GTK_CHECK_BUTTON (shell_preferences->priv->location_check))
		colname = "RHYTHMDB_PROP_LOCATION";
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
	GSList  *l;
	gint view, i;
	gboolean b;
	float duration;
	int buffer_size;

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
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->quality_check,
							 columns, "RHYTHMDB_PROP_BITRATE");
		rb_shell_preferences_sync_column_button (shell_preferences,
			       				 shell_preferences->priv->location_check,
							 columns, "RHYTHMDB_PROP_LOCATION");
	}

	g_free (columns);

	view = eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS);
	for (l = shell_preferences->priv->browser_views_group, i = 0; l != NULL; l = g_slist_next (l), i++)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l->data), (i == view));

	/* toolbar style */
	g_signal_handlers_block_by_func (G_OBJECT (shell_preferences->priv->toolbar_style_menu),
					 G_CALLBACK (rb_shell_preferences_toolbar_style_cb),
					 shell_preferences);

	view = eel_gconf_get_integer (CONF_UI_TOOLBAR_STYLE);
	/* skip the separator row */
	if (view >= 1)
		view++;
	gtk_combo_box_set_active (GTK_COMBO_BOX (shell_preferences->priv->toolbar_style_menu), view);

	g_signal_handlers_unblock_by_func (G_OBJECT (shell_preferences->priv->toolbar_style_menu),
					   G_CALLBACK (rb_shell_preferences_toolbar_style_cb),
					   shell_preferences);

	/* player preferences */
	b = eel_gconf_get_boolean (CONF_PLAYER_USE_XFADE_BACKEND);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->xfade_backend_check), b);

	duration = eel_gconf_get_float (CONF_PLAYER_TRANSITION_TIME);
	gtk_range_set_value (GTK_RANGE (shell_preferences->priv->transition_duration), duration);

	buffer_size = eel_gconf_get_integer (CONF_PLAYER_NETWORK_BUFFER_SIZE);
	gtk_range_set_value (GTK_RANGE (shell_preferences->priv->network_buffer_size), buffer_size);

	update_playback_prefs_sensitivity (shell_preferences);

	shell_preferences->priv->loading = FALSE;
}

static void
rb_shell_preferences_toolbar_style_cb (GtkComboBox *box, RBShellPreferences *preferences)
{
	int selection;

	selection = gtk_combo_box_get_active (box);

	/* skip the separator row */
	if (selection >= 1)
		selection--;

	eel_gconf_set_integer (CONF_UI_TOOLBAR_STYLE, selection);
}

/**
 * rb_shell_preferences_browser_views_activated_cb:
 * @widget: the radio button that was selected
 * @shell_preferences: the #RBShellPreferences instance
 *
 * Signal handler used for the radio buttons used to configure the
 * visible browser views.
 */
void
rb_shell_preferences_browser_views_activated_cb (GtkWidget *widget,
						 RBShellPreferences *shell_preferences)
{
	int index;

	if (shell_preferences->priv->loading)
		return;

	index = g_slist_index (shell_preferences->priv->browser_views_group, widget);

	eel_gconf_set_integer (CONF_UI_BROWSER_VIEWS, index);
}

static void
update_playback_prefs_sensitivity (RBShellPreferences *preferences)
{
	gboolean backend;

	backend = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (preferences->priv->xfade_backend_check));

	gtk_widget_set_sensitive (preferences->priv->transition_duration, backend);
}

static void
rb_shell_preferences_player_backend_cb (GtkToggleButton *button,
					RBShellPreferences *preferences)
{
	update_playback_prefs_sensitivity (preferences);

	eel_gconf_set_boolean (CONF_PLAYER_USE_XFADE_BACKEND,
			       gtk_toggle_button_get_active (button));
}

static void
rb_shell_preferences_transition_duration_cb (GtkRange *range,
					     RBShellPreferences *preferences)
{
	gdouble v;

	v = gtk_range_get_value (range);
	eel_gconf_set_float (CONF_PLAYER_TRANSITION_TIME, (float)v);
}

static void
rb_shell_preferences_network_buffer_size_cb (GtkRange *range,
					     RBShellPreferences *preferences)
{
	gdouble v;

	v = gtk_range_get_value (range);
	eel_gconf_set_integer (CONF_PLAYER_NETWORK_BUFFER_SIZE, (int)v);
}

