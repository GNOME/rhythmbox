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
#include <glade/glade.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-library-preferences.h"
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
static void library_pref_changed (GConfClient *client,
		                  guint cnxn_id,
		                  GConfEntry *entry,
		                  RBShellPreferences *shell_preferences);

struct RBShellPreferencesPrivate
{
	GtkWidget *base_folder_entry;

	gboolean lock;
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

	eel_gconf_notification_add (CONF_LIBRARY_DIR,
				    (GConfClientNotifyFunc) library_pref_changed,
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

	xml = rb_glade_xml_new ("music-folders.glade",
				"music_folders_vbox",
				shell_preferences);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (shell_preferences)->vbox),
			   glade_xml_get_widget (xml, "music_folders_vbox"));

	shell_preferences->priv->base_folder_entry =
		glade_xml_get_widget (xml, "music_base_folder_entry");

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
	char *base_folder;

	if (shell_preferences->priv->lock == TRUE)
		return;
	shell_preferences->priv->lock = TRUE;

	base_folder = eel_gconf_get_string (CONF_LIBRARY_BASE_FOLDER);

	gtk_entry_set_text (GTK_ENTRY (shell_preferences->priv->base_folder_entry),
			    base_folder);
	g_free (base_folder);
	
	shell_preferences->priv->lock = FALSE;
}

void
music_base_folder_entry_changed_cb (GtkEditable *editable,
				    RBShellPreferences *shell_preferences)
{
	if (shell_preferences->priv->lock == TRUE)
		return;

	eel_gconf_set_string (CONF_LIBRARY_BASE_FOLDER,
			      gtk_entry_get_text (GTK_ENTRY (editable)));
}

void
music_base_folder_browse_clicked_cb (GtkWidget *button,
				     RBShellPreferences *shell_preferences)
{
	char *ret = rb_ask_file (_("Choose a folder"), GTK_WINDOW (shell_preferences));
	if (ret != NULL)
		gtk_entry_set_text (GTK_ENTRY (shell_preferences->priv->base_folder_entry), ret);
	g_free (ret);
}

static void
library_pref_changed (GConfClient *client,
		      guint cnxn_id,
		      GConfEntry *entry,
		      RBShellPreferences *shell_preferences)
{
	rb_shell_preferences_sync (shell_preferences);
}
