/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of local file source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-glade-helpers.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "eel-gconf-extensions.h"
#include "rb-library-source.h"

static void rb_library_source_class_init (RBLibrarySourceClass *klass);
static void rb_library_source_init (RBLibrarySource *source);
static GObject *rb_library_source_constructor (GType type,
					       guint n_construct_properties,
					       GObjectConstructParam *construct_properties);
static void rb_library_source_dispose (GObject *object);
static void rb_library_source_finalize (GObject *object);

/* RBSource implementations */
static GtkWidget *impl_get_config_widget (RBSource *source, RBShellPreferences *prefs);
static const char *impl_get_browser_key (RBSource *source);
static const char *impl_get_paned_key (RBBrowserSource *source);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);

static void rb_library_source_ui_prefs_sync (RBLibrarySource *source);
static void rb_library_source_preferences_sync (RBLibrarySource *source);

static void rb_library_source_library_location_changed (GConfClient *client,
							guint cnxn_id,
							GConfEntry *entry,
							RBLibrarySource *source);
static void rb_library_source_ui_pref_changed (GConfClient *client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       RBLibrarySource *source);
static void rb_library_source_library_location_cb (GtkEntry *entry,
						   GdkEventFocus *event,
						   RBLibrarySource *source);
static void rb_library_source_watch_toggled_cb (GtkToggleButton *button,
						RBLibrarySource *source);
static void rb_library_source_songs_show_popup_cb (RBEntryView *view,
						   gboolean over_entry,
						   RBLibrarySource *source);

/* glade callback */
void rb_library_source_browser_views_activated_cb (GtkWidget *widget,
						   RBLibrarySource *source);

#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_LIBRARY_SORTING CONF_PREFIX "/state/library/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/library/show_browser"

struct RBLibrarySourcePrivate
{
	RhythmDB *db;

	gboolean loading_prefs;
	RBShellPreferences *shell_prefs;

	GtkWidget *config_widget;

	GtkWidget *library_location_entry;
	GtkWidget *watch_library_check;
	GSList *browser_views_group;

	guint library_location_notify_id;
	guint ui_dir_notify_id;
};

#define RB_LIBRARY_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourcePrivate))
G_DEFINE_TYPE (RBLibrarySource, rb_library_source, RB_TYPE_BROWSER_SOURCE)

static void
rb_library_source_class_init (RBLibrarySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose = rb_library_source_dispose;
	object_class->finalize = rb_library_source_finalize;
	object_class->constructor = rb_library_source_constructor;

	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_receive_drag = impl_receive_drag;

	browser_source_class->impl_get_paned_key = impl_get_paned_key;
	browser_source_class->impl_has_first_added_column = (RBBrowserSourceFeatureFunc) rb_true_function;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_true_function;

	g_type_class_add_private (klass, sizeof (RBLibrarySourcePrivate));
}

static void
rb_library_source_init (RBLibrarySource *source)
{
	source->priv = RB_LIBRARY_SOURCE_GET_PRIVATE (source);
}

static void
rb_library_source_dispose (GObject *object)
{
	RBLibrarySource *source;
	source = RB_LIBRARY_SOURCE (object);

	if (source->priv->shell_prefs) {
		g_object_unref (source->priv->shell_prefs);
		source->priv->shell_prefs = NULL;
	}

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}
	
	G_OBJECT_CLASS (rb_library_source_parent_class)->dispose (object);
}

static void
rb_library_source_finalize (GObject *object)
{
	RBLibrarySource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_SOURCE (object));

	source = RB_LIBRARY_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing library source");
	eel_gconf_notification_remove (source->priv->ui_dir_notify_id);
	eel_gconf_notification_remove (source->priv->library_location_notify_id);

	G_OBJECT_CLASS (rb_library_source_parent_class)->finalize (object);
}

static GObject *
rb_library_source_constructor (GType type,
			       guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBLibrarySource *source;
	RBShell *shell;
	RBEntryView *songs;

	source = RB_LIBRARY_SOURCE (G_OBJECT_CLASS (rb_library_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);

	rb_library_source_ui_prefs_sync (source);

	source->priv->library_location_notify_id =
		eel_gconf_notification_add (CONF_LIBRARY_LOCATION,
				    (GConfClientNotifyFunc) rb_library_source_library_location_changed, source);

	source->priv->ui_dir_notify_id =
		eel_gconf_notification_add (CONF_UI_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_ui_pref_changed, source);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_FIRST_SEEN, FALSE);

	g_signal_connect_object (G_OBJECT (rb_source_get_entry_view (RB_SOURCE (source))), "show_popup",
				 G_CALLBACK (rb_library_source_songs_show_popup_cb), source, 0);

	g_object_unref (G_OBJECT (shell));

	return G_OBJECT (source);
}

RBSource *
rb_library_source_new (RBShell *shell)
{
	RBSource *source;
	GdkPixbuf *icon;
	gint size;

	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					 "stock_music-library",
					 size,
					 0, NULL);

	source = RB_SOURCE (g_object_new (RB_TYPE_LIBRARY_SOURCE,
					  "name", _("Library"),
					  "entry-type", RHYTHMDB_ENTRY_TYPE_SONG,
					  "sorting-key", CONF_STATE_LIBRARY_SORTING,
					  "shell", shell,
					  "icon", icon,
					  NULL));
	rb_shell_register_entry_type_for_source (shell, source,
						 RHYTHMDB_ENTRY_TYPE_SONG);
	return source;
}

static void
rb_library_source_location_button_clicked_cb (GtkButton *button, RBLibrarySource *source)
{
	GtkWidget *dialog;
	
	dialog = rb_file_chooser_new (_("Choose Library Location"), GTK_WINDOW (source->priv->shell_prefs),
				      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, FALSE);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *uri;
		char *path;

		uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
		if (uri == NULL) {
			uri = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
		}
		
		path = gnome_vfs_format_uri_for_display (uri);
		
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), path);
		rb_library_source_library_location_cb (GTK_ENTRY (source->priv->library_location_entry),
						       NULL, source);
		g_free (uri);
		g_free (path);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static GtkWidget *
impl_get_config_widget (RBSource *asource, RBShellPreferences *prefs)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkWidget *tmp;
	GladeXML *xml;

	if (source->priv->config_widget)
		return source->priv->config_widget;
	
	g_object_ref (G_OBJECT (prefs));
	source->priv->shell_prefs = prefs;
	
	xml = rb_glade_xml_new ("library-prefs.glade", "library_vbox", source);
	source->priv->config_widget =
		glade_xml_get_widget (xml, "library_vbox");
	tmp = glade_xml_get_widget (xml, "library_browser_views_radio");
	source->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group
					       (GTK_RADIO_BUTTON (tmp))));

	source->priv->library_location_entry = glade_xml_get_widget (xml, "library_location_entry");
	source->priv->watch_library_check = glade_xml_get_widget (xml, "watch_library_check");
	tmp = glade_xml_get_widget (xml, "library_location_button");
	g_signal_connect (G_OBJECT (tmp),
			  "clicked",
			  G_CALLBACK (rb_library_source_location_button_clicked_cb),
			  asource);

	rb_glade_boldify_label (xml, "browser_views_label");
	rb_glade_boldify_label (xml, "library_location_label");
	g_object_unref (G_OBJECT (xml));
	
	rb_library_source_preferences_sync (source);
	g_signal_connect (G_OBJECT (source->priv->library_location_entry),
			  "focus-out-event",
			  G_CALLBACK (rb_library_source_library_location_cb),
			  asource);
	g_signal_connect (G_OBJECT (source->priv->watch_library_check),
			  "toggled",
			  G_CALLBACK (rb_library_source_watch_toggled_cb),
			  asource);

	return source->priv->config_widget;
}

static void
rb_library_source_library_location_changed (GConfClient *client,
					    guint cnxn_id,
					    GConfEntry *entry,
					    RBLibrarySource *source)
{
	if (source->priv->config_widget)
		rb_library_source_preferences_sync (source);
}

static void
rb_library_source_ui_prefs_sync (RBLibrarySource *source)
{
	if (source->priv->config_widget)
		rb_library_source_preferences_sync (source);
}

static void
rb_library_source_ui_pref_changed (GConfClient *client,
				   guint cnxn_id,
				   GConfEntry *entry,
				   RBLibrarySource *source)
{
	rb_debug ("ui pref changed");
	rb_library_source_ui_prefs_sync (source);
}

static void
rb_library_source_preferences_sync (RBLibrarySource *source)
{
	GSList *list;

	rb_debug ("syncing pref dialog state");

	list = g_slist_nth (source->priv->browser_views_group,
			    eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (list->data), TRUE);

	list = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);

	/* don't trigger the change notification */
	g_signal_handlers_block_by_func (G_OBJECT (source->priv->library_location_entry),
					 G_CALLBACK (rb_library_source_library_location_cb),
					 source);

	if (g_slist_length (list) == 1) {
		char *path;

		gtk_widget_set_sensitive (source->priv->library_location_entry, TRUE);

		path = gnome_vfs_format_uri_for_display (list->data);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), path);
		g_free (path);
	} else if (g_slist_length (list) == 0) {
		/* no library directories */
		gtk_widget_set_sensitive (source->priv->library_location_entry, TRUE);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), "");
	} else {
		/* multiple library directories */
		gtk_widget_set_sensitive (source->priv->library_location_entry, FALSE);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), _("Multiple locations set"));
	}

	g_signal_handlers_unblock_by_func (G_OBJECT (source->priv->library_location_entry),
					   G_CALLBACK (rb_library_source_library_location_cb),
					   source);

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check),
				      eel_gconf_get_boolean (CONF_MONITOR_LIBRARY));
}

static void
rb_library_source_library_location_cb (GtkEntry *entry,
				       GdkEventFocus *event,
				       RBLibrarySource *source)
{
	GSList *list = NULL;
	const char *path;
	char *uri;

	path = gtk_entry_get_text (entry);
	uri = gnome_vfs_make_uri_from_input (path);

	if (uri && uri[0])
		list = g_slist_prepend (NULL, (gpointer)uri);

	eel_gconf_set_string_list (CONF_LIBRARY_LOCATION, list);

	g_free (uri);
	if (list)
		g_slist_free (list);
	
	/* don't do the first-run druid if the user sets the library location */
	if (list)
		eel_gconf_set_boolean (CONF_FIRST_TIME, TRUE);
}

static void
rb_library_source_watch_toggled_cb (GtkToggleButton *button, RBLibrarySource *source)
{
	gboolean active;

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check));
	eel_gconf_set_boolean (CONF_MONITOR_LIBRARY, active);
}


void
rb_library_source_browser_views_activated_cb (GtkWidget *widget,
					      RBLibrarySource *source)
{
	int index;

	if (source->priv->loading_prefs == TRUE)
		return;

	index = g_slist_index (source->priv->browser_views_group, widget);

	eel_gconf_set_integer (CONF_UI_BROWSER_VIEWS, index);
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return CONF_STATE_SHOW_BROWSER;
}

static const char *
impl_get_paned_key (RBBrowserSource *status)
{
	return CONF_STATE_PANED_POSITION;
}


static void
rb_library_source_add_location_entry_changed_cb (GtkEntry *entry,
						 GtkWidget *target)
{
	gtk_widget_set_sensitive (target, g_utf8_strlen (gtk_entry_get_text (GTK_ENTRY (entry)), -1) > 0);
}

void
rb_library_source_add_location (RBLibrarySource *source, GtkWindow *win)
{
	GladeXML *xml = rb_glade_xml_new ("uri.glade",
					  "open_uri_dialog_content",
					  source);
	GtkWidget *content, *uri_widget, *open_button;
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Add Location"),
							 win,
							 GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
							 GTK_STOCK_CANCEL,
							 GTK_RESPONSE_CANCEL,
							 NULL);

	g_return_if_fail (dialog != NULL);

	open_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
					     GTK_STOCK_OPEN,
					     GTK_RESPONSE_OK);
	gtk_widget_set_sensitive (open_button, FALSE);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	content = glade_xml_get_widget (xml, "open_uri_dialog_content");

	g_return_if_fail (content != NULL);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    content, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (content), 5);

	uri_widget = glade_xml_get_widget (xml, "uri");

	g_return_if_fail (uri_widget != NULL);

	g_signal_connect_object (G_OBJECT (uri_widget),
				 "changed",
				 G_CALLBACK (rb_library_source_add_location_entry_changed_cb),
				 open_button, 0);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		char *uri = gtk_editable_get_chars (GTK_EDITABLE (uri_widget), 0, -1);
		if (uri != NULL) {
			GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
			if (vfsuri != NULL) {
				rhythmdb_add_uri (source->priv->db, uri);
				gnome_vfs_uri_unref (vfsuri);
			} else {
				rb_debug ("invalid uri: \"%s\"", uri);
			}

		}
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));

}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *list, *uri_list, *i;

	rb_debug ("parsing uri list");
	list = gnome_vfs_uri_list_parse ((char *) data->data);

	if (list == NULL)
		return FALSE;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_prepend (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return FALSE;
	
	rb_debug ("adding uris");

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL)
			rhythmdb_add_uri (source->priv->db, uri);

		g_free (uri);
	}

	g_list_free (uri_list);
	return TRUE;
}

static void
rb_library_source_songs_show_popup_cb (RBEntryView *view,
				       gboolean over_entry,
				       RBLibrarySource *source)
{
	if (!over_entry)
		_rb_source_show_popup (RB_SOURCE (source), "/LibrarySourcePopup");
}

