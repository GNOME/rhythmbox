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
#include <glade/glade.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "eel-gconf-extensions.h"

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
	GtkWidget *toolbar_check;
	GtkWidget *statusbar_check;
	GtkWidget *sidebar_check;
	GtkWidget *style_optionmenu;
	GtkWidget *artist_check;
	GtkWidget *album_check;
	GtkWidget *genre_check;
	GtkWidget *duration_check;
	GtkWidget *track_check;
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
rb_shell_preferences_init (RBShellPreferences *shell_preferences)
{
	GladeXML *xml;
	
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
	gtk_dialog_set_default_response (GTK_DIALOG (shell_preferences),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (shell_preferences), _("Preferences"));
	gtk_window_set_resizable (GTK_WINDOW (shell_preferences), FALSE);

	xml = rb_glade_xml_new ("preferences.glade",
				"preferences_vbox",
				shell_preferences);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (shell_preferences)->vbox),
			   glade_xml_get_widget (xml, "preferences_vbox"));

	gtk_container_set_border_width (GTK_CONTAINER (shell_preferences), 7);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (shell_preferences)->vbox), 8);
	gtk_dialog_set_has_separator (GTK_DIALOG (shell_preferences), FALSE);

	shell_preferences->priv->toolbar_check =
		glade_xml_get_widget (xml, "toolbar_check");
	shell_preferences->priv->statusbar_check =
		glade_xml_get_widget (xml, "statusbar_check");
	shell_preferences->priv->sidebar_check =
		glade_xml_get_widget (xml, "sidebar_check");
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

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->toolbar_check),
				      eel_gconf_get_boolean (CONF_UI_TOOLBAR_VISIBLE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->statusbar_check),
				      eel_gconf_get_boolean (CONF_UI_STATUSBAR_VISIBLE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->sidebar_check),
				      eel_gconf_get_boolean (CONF_UI_SIDEBAR_VISIBLE));

	columns = eel_gconf_get_string (CONF_UI_COLUMNS_SETUP);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->artist_check),
				      strstr (columns, "RB_TREE_MODEL_NODE_COL_ARTIST") != NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->album_check),
				      strstr (columns, "RB_TREE_MODEL_NODE_COL_ALBUM") != NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->genre_check),
				      strstr (columns, "RB_TREE_MODEL_NODE_COL_GENRE") != NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->duration_check),
				      strstr (columns, "RB_TREE_MODEL_NODE_COL_DURATION") != NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->track_check),
				      strstr (columns, "RB_TREE_MODEL_NODE_COL_TRACK_NUMBER") != NULL);

	style = eel_gconf_get_string (CONF_UI_TOOLBAR_STYLE);
	for (i = 0; i < G_N_ELEMENTS (styles); i++)
	{
		if (style != NULL && strcmp (styles[i], style) == 0)
			index = i;
	}
	gtk_option_menu_set_history (GTK_OPTION_MENU (shell_preferences->priv->style_optionmenu),
				     index);

	g_free (columns);
	g_free (style);
}

static void
ui_pref_changed (GConfClient *client,
		 guint cnxn_id,
		 GConfEntry *entry,
		 RBShellPreferences *shell_preferences)
{
	rb_shell_preferences_sync (shell_preferences);
}

void
style_changed_cb (GtkOptionMenu *menu,
		  RBShellPreferences *prefs)
{
	eel_gconf_set_string (CONF_UI_TOOLBAR_STYLE,
			      styles[gtk_option_menu_get_history (menu)]);
}

void
show_toolbar_toggled_cb (GtkToggleButton *button,
			 RBShellPreferences *prefs)
{
	eel_gconf_set_boolean (CONF_UI_TOOLBAR_VISIBLE,
			       gtk_toggle_button_get_active (button));
}

void
show_sidebar_toggled_cb (GtkToggleButton *button,
			 RBShellPreferences *prefs)
{
	eel_gconf_set_boolean (CONF_UI_SIDEBAR_VISIBLE,
			       gtk_toggle_button_get_active (button));
}

void
show_statusbar_toggled_cb (GtkToggleButton *button,
			   RBShellPreferences *prefs)
{
	eel_gconf_set_boolean (CONF_UI_STATUSBAR_VISIBLE,
			       gtk_toggle_button_get_active (button));
}

void
show_columns_changed_cb (GtkToggleButton *button,
			 RBShellPreferences *prefs)
{
	char *conf = "";

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->artist_check)) == TRUE)
		conf = "RB_TREE_MODEL_NODE_COL_ARTIST";
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->album_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_ALBUM", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->genre_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_GENRE", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->duration_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_DURATION", conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (prefs->priv->track_check)) == TRUE)
		conf = g_strdup_printf ("%s,RB_TREE_MODEL_NODE_COL_TRACK_NUMBER", conf);

	eel_gconf_set_string (CONF_UI_COLUMNS_SETUP, conf);

	g_free (conf);
}
