/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkstock.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkbox.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkradiobutton.h>
#include <glade/glade.h>
#include <libgnome/gnome-help.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
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
static void rb_shell_preferences_sync (RBShellPreferences *shell_preferences);
static void ui_pref_changed (GConfClient *client,
		             guint cnxn_id,
		             GConfEntry *entry,
		             RBShellPreferences *shell_preferences);

const char *styles[] = { "desktop_default", "both", "both_horiz", "icon", "text" };

struct RBShellPreferencesPrivate
{
	GtkWidget *style_optionmenu;
	GtkWidget *artist_check;
	GtkWidget *album_check;
	GtkWidget *genre_check;
	GtkWidget *duration_check;
	GtkWidget *track_check;
	GtkWidget *rating_check;
	GtkWidget *play_count_check;
	GtkWidget *last_played_check;
	GSList *browser_views_group;

	gboolean loading;
};

static GObjectClass *parent_class = NULL;

GType
rb_shell_preferences_get_type (void)
{
	static GType rb_shell_preferences_type = 0;

	if (rb_shell_preferences_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBShellPreferencesClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_shell_preferences_class_init,
			NULL,
			NULL,
			sizeof (RBShellPreferences),
			0,
			(GInstanceInitFunc) rb_shell_preferences_init
		};

		rb_shell_preferences_type = g_type_register_static (GTK_TYPE_DIALOG,
							            "RBShellPreferences",
							            &our_info, 0);
	}

	return rb_shell_preferences_type;
}

static void
rb_shell_preferences_class_init (RBShellPreferencesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_shell_preferences_finalize;
}

static void
help_cb (GtkWidget *widget,
	 RBShellPreferences *shell_preferences)
{
	GError *error = NULL;

	gnome_help_display ("rhythmbox.xml", "prefs", &error);

	if (error != NULL)
	{
		g_warning (error->message);

		g_error_free (error);
	}
}

static void
rb_shell_preferences_init (RBShellPreferences *shell_preferences)
{
	GladeXML *xml;
	GtkWidget *help;
	GtkWidget *tmp;

	shell_preferences->priv = g_new0 (RBShellPreferencesPrivate, 1);

	eel_gconf_notification_add (CONF_UI_DIR,
				    (GConfClientNotifyFunc) ui_pref_changed,
				    shell_preferences);

	g_signal_connect (G_OBJECT (shell_preferences),
			  "delete_event",
			  G_CALLBACK (rb_shell_preferences_window_delete_cb),
			  shell_preferences);
	g_signal_connect (G_OBJECT (shell_preferences),
			  "response",
			  G_CALLBACK (rb_shell_preferences_response_cb),
			  shell_preferences);

	gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	help = gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			              GTK_STOCK_HELP,
			              GTK_RESPONSE_HELP);
	g_signal_connect (G_OBJECT (help), "clicked",
			  G_CALLBACK (help_cb), shell_preferences);
	gtk_dialog_set_default_response (GTK_DIALOG (shell_preferences),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (shell_preferences), _("Music Player Preferences"));
	gtk_window_set_resizable (GTK_WINDOW (shell_preferences), FALSE);

	xml = rb_glade_xml_new ("preferences.glade",
				"preferences_hbox",
				shell_preferences);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (shell_preferences)->vbox),
			   glade_xml_get_widget (xml, "preferences_hbox"));

	gtk_container_set_border_width (GTK_CONTAINER (shell_preferences), 7);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (shell_preferences)->vbox), 8);
	gtk_dialog_set_has_separator (GTK_DIALOG (shell_preferences), FALSE);

	shell_preferences->priv->style_optionmenu =
		glade_xml_get_widget (xml, "style_optionmenu");
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
	tmp = glade_xml_get_widget (xml, "browser_views_radio");
	shell_preferences->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group 
					(GTK_RADIO_BUTTON (tmp))));


	g_object_unref (G_OBJECT (xml));

	rb_shell_preferences_sync (shell_preferences);
}

static void
rb_shell_preferences_finalize (GObject *object)
{
	RBShellPreferences *shell_preferences;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PREFERENCES (object));

	shell_preferences = RB_SHELL_PREFERENCES (object);

	g_slist_free (shell_preferences->priv->browser_views_group);
		
	g_return_if_fail (shell_preferences->priv != NULL);

	g_free (shell_preferences->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
rb_shell_preferences_new (void)
{
	RBShellPreferences *shell_preferences;

	shell_preferences = g_object_new (RB_TYPE_SHELL_PREFERENCES,
				          NULL);

	g_return_val_if_fail (shell_preferences->priv != NULL, NULL);

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
rb_shell_preferences_sync (RBShellPreferences *shell_preferences)
{
	char *style;
	char *columns;
	int index = 0, i;
	GSList *list;

	shell_preferences->priv->loading = TRUE;

	gtk_widget_set_sensitive (shell_preferences->priv->style_optionmenu,
				  eel_gconf_get_boolean (CONF_UI_TOOLBAR_VISIBLE));

	columns = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);
	if (columns != NULL)
	{
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->artist_check), 
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_ARTIST") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->album_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_ALBUM") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->genre_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_GENRE") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->duration_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_DURATION") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->track_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_TRACK_NUMBER") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->rating_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_RATING") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->play_count_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_PLAY_COUNT") != NULL);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (shell_preferences->priv->last_played_check),
			 strstr (columns, "RB_TREE_MODEL_NODE_COL_LAST_PLAYED") != NULL);
	}

	style = eel_gconf_get_string (CONF_UI_TOOLBAR_STYLE);
	for (i = 0; i < G_N_ELEMENTS (styles); i++)
	{
		if (style != NULL && strcmp (styles[i], style) == 0)
			index = i;
	}
	gtk_option_menu_set_history (GTK_OPTION_MENU (shell_preferences->priv->style_optionmenu),
				     index);

	list = g_slist_nth (shell_preferences->priv->browser_views_group,
			    eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (list->data), TRUE);

	g_free (columns);
	g_free (style);

	shell_preferences->priv->loading = FALSE;
}

static void
ui_pref_changed (GConfClient *client,
		 guint cnxn_id,
		 GConfEntry *entry,
		 RBShellPreferences *shell_preferences)
{
	if (prefs->priv->loading == TRUE)
		return;

	rb_shell_preferences_sync (shell_preferences);
}

void
browser_views_activated_cb (GtkWidget *widget,
			    RBShellPreferences *prefs)
{
	int index;

	if (prefs->priv->loading == TRUE)
		return;

	index = g_slist_index (prefs->priv->browser_views_group, widget);

	eel_gconf_set_integer (CONF_UI_BROWSER_VIEWS, index);
}

void
style_changed_cb (GtkOptionMenu *menu,
		  RBShellPreferences *prefs)
{
	eel_gconf_set_string (CONF_UI_TOOLBAR_STYLE,
			      styles[gtk_option_menu_get_history (menu)]);
}

void
show_columns_changed_cb (GtkToggleButton *button,
			 RBShellPreferences *prefs)
{
	char *conf = g_strdup_printf (" ");

	// FIXME there must be a better way to do that
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->artist_check)) == TRUE)
		conf = g_strdup_printf ("RB_TREE_MODEL_NODE_COL_ARTIST");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->album_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_ALBUM", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->genre_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_GENRE", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->duration_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_DURATION", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->track_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_TRACK_NUMBER", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->rating_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_RATING", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->play_count_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_PLAY_COUNT", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->last_played_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_LAST_PLAYED", conf);


	eel_gconf_set_string (CONF_UI_COLUMNS_SETUP, conf);

	g_free (conf);
}
