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
#include <gtk/gtktreeview.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
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
static void folders_selection_changed_cb (GtkTreeSelection *selection,
			                  RBShellPreferences *shell_preferences);
static gboolean get_folders (GtkTreeModel *model,
		             GtkTreePath *path,
		             GtkTreeIter *iter,
		             void **data);
static gboolean get_rows (GtkTreeModel *model,
		          GtkTreePath *path,
		          GtkTreeIter *iter,
		          void **data);
static void rb_shell_preferences_sync (RBShellPreferences *shell_preferences);
static void library_pref_changed (GConfClient *client,
		                  guint cnxn_id,
		                  GConfEntry *entry,
		                  RBShellPreferences *shell_preferences);
static void folders_sync_to_gconf (RBShellPreferences *shell_preferences);
static void folders_row_edited_cb (GtkCellRendererText *renderer,
		                   const char *path_str,
		                   const char *new_text,
		                   RBShellPreferences *shell_preferences);

enum
{
	FOLDERS_COL_URI,
	FOLDERS_COL_EDITABLE,
	FOLDERS_NUM_COLUMNS
};

struct RBShellPreferencesPrivate
{
	GtkWidget *base_folder_entry;

	GtkWidget *folders_treeview;
	GtkListStore *folders_store;
	GtkTreeSelection *folders_selection;
	GtkWidget *folders_remove_button;

	GtkWidget *include_cd_check;
	
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
	GtkCellRenderer *renderer;
	
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

	gtk_window_set_title (GTK_WINDOW (shell_preferences), _("Music Folders"));

	xml = rb_glade_xml_new ("music-folders.glade",
				"music_folders_vbox",
				shell_preferences);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (shell_preferences)->vbox),
			   glade_xml_get_widget (xml, "music_folders_vbox"));

	shell_preferences->priv->base_folder_entry =
		glade_xml_get_widget (xml, "music_base_folder_entry");
	shell_preferences->priv->folders_treeview =
		glade_xml_get_widget (xml, "music_folders_treeview");
	shell_preferences->priv->folders_remove_button =
		glade_xml_get_widget (xml, "remove_folder_button");
	shell_preferences->priv->include_cd_check =
		glade_xml_get_widget (xml, "include_audiocd_check");

	/* set up folders treeview */
	shell_preferences->priv->folders_store = gtk_list_store_new (FOLDERS_NUM_COLUMNS,
								     G_TYPE_STRING,
								     G_TYPE_BOOLEAN);
	gtk_tree_view_set_model (GTK_TREE_VIEW (shell_preferences->priv->folders_treeview),
				 GTK_TREE_MODEL (shell_preferences->priv->folders_store));
	shell_preferences->priv->folders_selection =
		gtk_tree_view_get_selection (GTK_TREE_VIEW (shell_preferences->priv->folders_treeview));
	gtk_tree_selection_set_mode (shell_preferences->priv->folders_selection,
				     GTK_SELECTION_MULTIPLE);
	g_signal_connect (G_OBJECT (shell_preferences->priv->folders_selection),
			  "changed",
			  G_CALLBACK (folders_selection_changed_cb),
			  shell_preferences);
	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer),
			  "edited",
			  G_CALLBACK (folders_row_edited_cb),
			  shell_preferences);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (shell_preferences->priv->folders_treeview),
						     FOLDERS_COL_URI, _("Folder"), renderer,
						     "text", FOLDERS_COL_URI,
						     "editable", FOLDERS_COL_EDITABLE,
						     NULL);
	gtk_widget_set_sensitive (shell_preferences->priv->folders_remove_button, FALSE);

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
	GSList *music_folders, *l;
	gboolean include_cd;

	if (shell_preferences->priv->lock == TRUE)
		return;
	shell_preferences->priv->lock = TRUE;

	base_folder = eel_gconf_get_string (CONF_LIBRARY_BASE_FOLDER);
	music_folders = eel_gconf_get_string_list (CONF_LIBRARY_MUSIC_FOLDERS);
	include_cd = eel_gconf_get_boolean (CONF_LIBRARY_INCLUDE_AUDIO_CD);

	gtk_entry_set_text (GTK_ENTRY (shell_preferences->priv->base_folder_entry),
			    base_folder);
	gtk_list_store_clear (GTK_LIST_STORE (shell_preferences->priv->folders_store));
	for (l = music_folders; l != NULL; l = g_slist_next (l))
	{
		char *uri = l->data;
		GtkTreeIter iter;

		gtk_list_store_append (shell_preferences->priv->folders_store, &iter);
		gtk_list_store_set (shell_preferences->priv->folders_store, &iter,
				    FOLDERS_COL_URI, uri,
				    FOLDERS_COL_EDITABLE, TRUE,
				    -1);
	}
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shell_preferences->priv->include_cd_check),
				      include_cd);

	g_slist_foreach (music_folders, (GFunc) g_free, NULL);
	g_slist_free (music_folders);
	g_free (base_folder);
	
	shell_preferences->priv->lock = FALSE;
}

static void
folders_selection_changed_cb (GtkTreeSelection *selection,
			      RBShellPreferences *shell_preferences)
{
	GSList *folders = NULL;

	gtk_tree_selection_selected_foreach (shell_preferences->priv->folders_selection,
					     (GtkTreeSelectionForeachFunc) get_folders,
					     (void **) &folders);

	gtk_widget_set_sensitive (shell_preferences->priv->folders_remove_button,
				  (g_slist_length (folders) > 0));

	g_slist_free (folders);
}

static gboolean
get_folders (GtkTreeModel *model,
	     GtkTreePath *path,
	     GtkTreeIter *iter,
	     void **data)
{
	GSList **ret = (GSList **) data;
	GValue val = { 0, };

	gtk_tree_model_get_value (model, iter, FOLDERS_COL_URI, &val);

	*ret = g_slist_append (*ret, (char *) g_value_get_string (&val));

	return FALSE;
}

static gboolean
get_rows (GtkTreeModel *model,
	  GtkTreePath *path,
	  GtkTreeIter *iter,
	  void **data)
{
	GSList **ret = (GSList **) data;
	
	*ret = g_slist_append (*ret, gtk_tree_row_reference_new (model, path));

	return FALSE;
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

void
add_folder_clicked_cb (GtkWidget *button,
		       RBShellPreferences *shell_preferences)
{
	GtkTreeIter iter;
	char *uri;

	uri = rb_ask_file (_("Choose a folder"), GTK_WINDOW (shell_preferences));

	if (uri == NULL || strlen (uri) < 1)
	{
		g_free (uri);
		return;
	}

	gtk_list_store_append (shell_preferences->priv->folders_store, &iter);
	gtk_list_store_set (shell_preferences->priv->folders_store, &iter,
			    FOLDERS_COL_URI, uri,
			    FOLDERS_COL_EDITABLE, TRUE,
			    -1);

	g_free (uri);
	
	folders_sync_to_gconf (shell_preferences);
}

void
remove_folder_clicked_cb (GtkWidget *button,
			  RBShellPreferences *shell_preferences)
{
	GSList *list = NULL, *l;
	
	gtk_tree_selection_selected_foreach (shell_preferences->priv->folders_selection,
					     (GtkTreeSelectionForeachFunc) get_rows,
					     (void **) &list);

	for (l = list; l != NULL; l = g_slist_next (l))
	{
		GtkTreeRowReference *ref = l->data;
		GtkTreeIter iter;
		gtk_tree_model_get_iter (GTK_TREE_MODEL (shell_preferences->priv->folders_store),
					 &iter, gtk_tree_row_reference_get_path (ref));
		gtk_list_store_remove (shell_preferences->priv->folders_store, &iter);
		gtk_tree_row_reference_free (ref);
	}

	g_slist_free (list);
	
	folders_sync_to_gconf (shell_preferences);
}

void
include_audiocd_check_toggled_cb (GtkToggleButton *button,
				  RBShellPreferences *shell_preferences)
{
	if (shell_preferences->priv->lock == TRUE)
		return;

	eel_gconf_set_boolean (CONF_LIBRARY_INCLUDE_AUDIO_CD, button->active);
}

static void
library_pref_changed (GConfClient *client,
		      guint cnxn_id,
		      GConfEntry *entry,
		      RBShellPreferences *shell_preferences)
{
	rb_shell_preferences_sync (shell_preferences);
}

static void
folders_sync_to_gconf (RBShellPreferences *shell_preferences)
{
	GSList *folders = NULL;

	gtk_tree_model_foreach (GTK_TREE_MODEL (shell_preferences->priv->folders_store),
				(GtkTreeModelForeachFunc) get_folders, (void **) &folders);

	eel_gconf_set_string_list (CONF_LIBRARY_MUSIC_FOLDERS,
				   folders);

	g_slist_foreach (folders, (GFunc) g_free, NULL);
	g_slist_free (folders);
}

static void
folders_row_edited_cb (GtkCellRendererText *renderer,
		       const char *path_str,
		       const char *new_text,
		       RBShellPreferences *shell_preferences)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	path = gtk_tree_path_new_from_string (path_str);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (shell_preferences->priv->folders_store),
				 &iter, path);
	gtk_list_store_set (shell_preferences->priv->folders_store, &iter,
			    FOLDERS_COL_URI, new_text, -1);
	gtk_tree_path_free (path);
	
	folders_sync_to_gconf (shell_preferences);
}

gboolean
folders_treeview_key_press_event_cb (GtkWidget *widget,
				     GdkEventKey *event,
				     RBShellPreferences *shell_preferences)
{
	if (event->keyval != GDK_Delete)
		return TRUE;
	
	remove_folder_clicked_cb (widget, shell_preferences);

	return FALSE;
}
