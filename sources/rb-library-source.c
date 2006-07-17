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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#ifdef ENABLE_TRACK_TRANSFER
#include <profiles/gnome-media-profiles.h>
#include <profiles/audio-profile-choose.h>
#endif

#include "rhythmdb.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-glade-helpers.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "eel-gconf-extensions.h"
#include "rb-library-source.h"
#include "rb-removable-media-manager.h"
#include "rb-auto-playlist-source.h"

static void rb_library_source_class_init (RBLibrarySourceClass *klass);
static void rb_library_source_init (RBLibrarySource *source);
static GObject *rb_library_source_constructor (GType type,
					       guint n_construct_properties,
					       GObjectConstructParam *construct_properties);
static void rb_library_source_dispose (GObject *object);
static void rb_library_source_finalize (GObject *object);

/* RBSource implementations */
static gboolean impl_show_popup (RBSource *source);
static GtkWidget *impl_get_config_widget (RBSource *source, RBShellPreferences *prefs);
static char *impl_get_browser_key (RBSource *source);
static const char *impl_get_paned_key (RBBrowserSource *source);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_can_paste (RBSource *asource);
#ifdef ENABLE_TRACK_TRANSFER
static void impl_paste (RBSource *source, GList *entries);
#endif

static void rb_library_source_ui_prefs_sync (RBLibrarySource *source);
static void rb_library_source_preferences_sync (RBLibrarySource *source);

static void rb_library_source_library_location_changed (GConfClient *client,
						    guint cnxn_id,
						    GConfEntry *entry,
						    RBLibrarySource *source);
#ifdef ENABLE_TRACK_TRANSFER
static void rb_library_source_layout_path_changed (GConfClient *client,
						   guint cnxn_id,
						   GConfEntry *entry,
						   RBLibrarySource *source);
static void rb_library_source_layout_filename_changed (GConfClient *client,
						       guint cnxn_id,
						       GConfEntry *entry,
						       RBLibrarySource *source);
static void rb_library_source_edit_profile_clicked_cb (GtkButton *button,
						       RBLibrarySource *source);
#endif
static void rb_library_source_ui_pref_changed (GConfClient *client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       RBLibrarySource *source);
static gboolean rb_library_source_library_location_cb (GtkEntry *entry,
						       GdkEventFocus *event,
						       RBLibrarySource *source);
static void rb_library_source_watch_toggled_cb (GtkToggleButton *button,
						RBLibrarySource *source);
static void rb_library_source_sync_child_sources (RBLibrarySource *source);
#ifdef ENABLE_TRACK_TRANSFER
static void rb_library_source_path_changed_cb (GtkComboBox *box,
						RBLibrarySource *source);
static void rb_library_source_filename_changed_cb (GtkComboBox *box,
						   RBLibrarySource *source);
static void rb_library_source_format_changed_cb (GtkWidget *widget,
						 RBLibrarySource *source);
#endif

#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_LIBRARY_SORTING CONF_PREFIX "/state/library/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/library/show_browser"

#ifdef ENABLE_TRACK_TRANSFER
typedef struct {
	char *title;
	char *path;
} LibraryPathElement;

const LibraryPathElement library_layout_paths[] = {
	{N_("Artist/Artist - Album"), "%aa/%aa - %at"},
	{N_("Artist/Album"), "%aa/%at"},
	{N_("Artist - Album"), "%aa - %at"},
	{N_("Album"), "%at"},
	{N_("Artist"), "%aa"},
};
const int num_library_layout_paths = G_N_ELEMENTS (library_layout_paths);

const LibraryPathElement library_layout_filenames[] = {
	{N_("Number - Title"), "%tN - %tt"},
	{N_("Artist - Title"), "%ta - %tt"},
	{N_("Artist - Number - Title"), "%ta - %tN - %tt"},
	{N_("Artist (Album) - Number - Title"), "%ta (%at) - %tN - %tt"},
	{N_("Title"), "%tt"},
	{N_("Number. Artist - Title"), "%tN. %ta - %tt"},
};
const int num_library_layout_filenames = G_N_ELEMENTS (library_layout_filenames);
#endif

struct RBLibrarySourcePrivate
{
	RhythmDB *db;

	gboolean loading_prefs;
	RBShellPreferences *shell_prefs;

	GtkWidget *config_widget;

	GList *child_sources;

	GtkWidget *library_location_entry;
	GtkWidget *watch_library_check;
#ifdef ENABLE_TRACK_TRANSFER
	GtkWidget *layout_path_menu;
	GtkWidget *layout_filename_menu;
	GtkWidget *preferred_format_menu;
	GtkWidget *layout_example_label;
#endif

	guint library_location_notify_id;
	guint ui_dir_notify_id;
#ifdef ENABLE_TRACK_TRANSFER
	guint layout_path_notify_id;
	guint layout_filename_notify_id;
#endif
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

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) impl_can_paste;
#ifdef ENABLE_TRACK_TRANSFER
	source_class->impl_paste = impl_paste;
#endif

	browser_source_class->impl_get_paned_key = impl_get_paned_key;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_true_function;

	g_type_class_add_private (klass, sizeof (RBLibrarySourcePrivate));

#ifdef ENABLE_TRACK_TRANSFER
	gnome_media_profiles_init (eel_gconf_client_get_global ());
#endif
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
#ifdef ENABLE_TRACK_TRANSFER
	eel_gconf_notification_remove (source->priv->layout_path_notify_id);
	eel_gconf_notification_remove (source->priv->layout_filename_notify_id);
#endif

	G_OBJECT_CLASS (rb_library_source_parent_class)->finalize (object);
}

static gboolean
add_child_sources_idle (RBLibrarySource *source)
{
	rb_library_source_sync_child_sources (source);

	return FALSE;
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

	g_idle_add ((GSourceFunc)add_child_sources_idle, source);

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

#ifdef ENABLE_TRACK_TRANSFER
static void
rb_library_source_edit_profile_clicked_cb (GtkButton *button, RBLibrarySource *source)
{
	GtkWidget *dialog;

	dialog = gm_audio_profiles_edit_new (eel_gconf_client_get_global (),
					     GTK_WINDOW (source->priv->shell_prefs));
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_widget_show_all (dialog);
	gtk_dialog_run (GTK_DIALOG (dialog));
}
#endif

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
#ifdef ENABLE_TRACK_TRANSFER
	GtkWidget *label;
	int i;
#endif

	if (source->priv->config_widget)
		return source->priv->config_widget;

	g_object_ref (G_OBJECT (prefs));
	source->priv->shell_prefs = prefs;

	xml = rb_glade_xml_new ("library-prefs.glade", "library_vbox", source);
	source->priv->config_widget =
		glade_xml_get_widget (xml, "library_vbox");

	rb_glade_boldify_label (xml, "library_location_label");

	source->priv->library_location_entry = glade_xml_get_widget (xml, "library_location_entry");
	tmp = glade_xml_get_widget (xml, "library_location_button");
	g_signal_connect (G_OBJECT (tmp),
			  "clicked",
			  G_CALLBACK (rb_library_source_location_button_clicked_cb),
			  asource);
	g_signal_connect (G_OBJECT (source->priv->library_location_entry),
			  "focus-out-event",
			  G_CALLBACK (rb_library_source_library_location_cb),
			  asource);

	source->priv->watch_library_check = glade_xml_get_widget (xml, "watch_library_check");
	g_signal_connect (G_OBJECT (source->priv->watch_library_check),
			  "toggled",
			  G_CALLBACK (rb_library_source_watch_toggled_cb),
			  asource);

#ifdef ENABLE_TRACK_TRANSFER
	rb_glade_boldify_label (xml, "library_structure_label");

	tmp = glade_xml_get_widget (xml, "layout_path_menu_box");
	label = glade_xml_get_widget (xml, "layout_path_menu_label");
	source->priv->layout_path_menu = gtk_combo_box_new_text ();
	gtk_box_pack_start_defaults (GTK_BOX (tmp), source->priv->layout_path_menu);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->layout_path_menu);
	g_signal_connect (G_OBJECT (source->priv->layout_path_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_path_changed_cb),
			  asource);
	for (i = 0; i < num_library_layout_paths; i++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (source->priv->layout_path_menu),
					   _(library_layout_paths[i].title));
	}

	tmp = glade_xml_get_widget (xml, "layout_filename_menu_box");
	label = glade_xml_get_widget (xml, "layout_filename_menu_label");
	source->priv->layout_filename_menu = gtk_combo_box_new_text ();
	gtk_box_pack_start_defaults (GTK_BOX (tmp), source->priv->layout_filename_menu);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->layout_filename_menu);
	g_signal_connect (G_OBJECT (source->priv->layout_filename_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_filename_changed_cb),
			  asource);
	for (i = 0; i < num_library_layout_filenames; i++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (source->priv->layout_filename_menu),
					   _(library_layout_filenames[i].title));
	}

	tmp = glade_xml_get_widget (xml, "edit_profile_button");
	g_signal_connect (G_OBJECT (tmp),
			  "clicked",
			  G_CALLBACK (rb_library_source_edit_profile_clicked_cb),
			  asource);

	tmp = glade_xml_get_widget (xml, "preferred_format_menu_box");
	label = glade_xml_get_widget (xml, "preferred_format_menu_label");
	source->priv->preferred_format_menu = gm_audio_profile_choose_new ();
	gtk_box_pack_start_defaults (GTK_BOX (tmp), source->priv->preferred_format_menu);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->preferred_format_menu);
	g_signal_connect (G_OBJECT (source->priv->preferred_format_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_format_changed_cb),
			  asource);

	source->priv->layout_example_label = glade_xml_get_widget (xml, "layout_example_label");
#else
	tmp = glade_xml_get_widget (xml, "library_structure_vbox");
	gtk_widget_set_no_show_all (tmp, TRUE);
	gtk_widget_hide (tmp);
#endif

	g_object_unref (G_OBJECT (xml));

	rb_library_source_preferences_sync (source);

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

	rb_library_source_sync_child_sources (source);
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
#ifdef ENABLE_TRACK_TRANSFER
	const char *str;
	GConfClient *gconf_client;
#endif

	rb_debug ("syncing pref dialog state");

	/* library location */
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

	/* watch checkbox */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check),
				      eel_gconf_get_boolean (CONF_MONITOR_LIBRARY));

#ifdef ENABLE_TRACK_TRANSFER
	/* preferred format */
	str = eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT);
	if (str)
		gm_audio_profile_choose_set_active (source->priv->preferred_format_menu, str);

	source->priv->layout_path_notify_id =
		eel_gconf_notification_add (CONF_LIBRARY_LAYOUT_PATH,
				    (GConfClientNotifyFunc) rb_library_source_layout_path_changed, source);
	source->priv->layout_filename_notify_id =
		eel_gconf_notification_add (CONF_LIBRARY_LAYOUT_FILENAME,
				    (GConfClientNotifyFunc) rb_library_source_layout_filename_changed, source);

	gconf_client = eel_gconf_client_get_global ();
	/* layout path */
	rb_library_source_layout_path_changed (gconf_client, -1,
					       gconf_client_get_entry (gconf_client, CONF_LIBRARY_LAYOUT_PATH, NULL, TRUE, NULL),
					       source);
	/* layout filename */
	rb_library_source_layout_filename_changed (gconf_client, -1,
						   gconf_client_get_entry (gconf_client, CONF_LIBRARY_LAYOUT_FILENAME, NULL, TRUE, NULL),
						   source);
#endif
}

static gboolean
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

	return FALSE;
}

static void
rb_library_source_watch_toggled_cb (GtkToggleButton *button, RBLibrarySource *source)
{
	gboolean active;

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check));
	eel_gconf_set_boolean (CONF_MONITOR_LIBRARY, active);
}

static char *
impl_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
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
	GList *list, *i;
	GList *entries = NULL;

	rb_debug ("parsing uri list");
	list = rb_uri_list_parse ((const char *) data->data);

	for (i = list; i != NULL; i = g_list_next (i)) {
		if (i->data != NULL) {
			char *uri = i->data;
			RhythmDBEntry *entry;

			entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);

			if (entry == NULL) {
				/* add to the library */
				rhythmdb_add_uri (source->priv->db, uri);
			} else {
				/* add to list of entries to copy */
				entries = g_list_prepend (entries, entry);
			}

			g_free (uri);
		}
	}

	if (entries) {
		entries = g_list_reverse (entries);
		if (rb_source_can_paste (asource))
			rb_source_paste (asource, entries);
		g_list_free (entries);
	}

	g_list_free (list);
	return TRUE;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (source, "/LibrarySourcePopup");
	return TRUE;
}

#ifdef ENABLE_TRACK_TRANSFER
static void
rb_library_source_path_changed_cb (GtkComboBox *box, RBLibrarySource *source)
{
	const char *path;
	gint index;

	index = gtk_combo_box_get_active (box);
	path = (index >= 0) ? library_layout_paths[index].path : "";
	eel_gconf_set_string (CONF_LIBRARY_LAYOUT_PATH, path);
}

static void
rb_library_source_filename_changed_cb (GtkComboBox *box, RBLibrarySource *source)
{
	const char *filename;
	gint index;

	index = gtk_combo_box_get_active (box);
	filename = (index >= 0) ? library_layout_filenames[index].path : "";
	eel_gconf_set_string (CONF_LIBRARY_LAYOUT_FILENAME, filename);
}

static void
rb_library_source_format_changed_cb (GtkWidget *widget, RBLibrarySource *source)
{
	GMAudioProfile *profile;

	profile = gm_audio_profile_choose_get_active (widget);
	eel_gconf_set_string (CONF_LIBRARY_PREFERRED_FORMAT, gm_audio_profile_get_id (profile));
}

/**
 * Perform magic on a path to make it safe.
 *
 * This will always replace '/' with ' ', and optionally make the file name
 * shell-friendly. This involves removing [?*\ ] and replacing with '_'.  Also
 * any leading periods are removed so that the files don't end up being hidden.
 */
static char *
sanitize_path (const char *str)
{
	gchar *res = NULL;
	gchar *s;

	/* Skip leading periods, otherwise files disappear... */
	while (*str == '.')
		str++;

	s = g_strdup(str);
	/* Replace path seperators with a hyphen */
	g_strdelimit (s, "/", '-');
	if (eel_gconf_get_boolean (CONF_LIBRARY_STRIP_CHARS)) {
		/* Replace separators with a hyphen */
		g_strdelimit (s, "\\:|", '-');
		/* Replace all other weird characters to whitespace */
		g_strdelimit (s, "*?&!\'\"$()`>{}", ' ');
		/* Replace all whitespace with underscores */
		/* TODO: I'd like this to compress whitespace aswell */
		g_strdelimit (s, "\t ", '_');
	}
	res = g_filename_from_utf8(s, -1, NULL, NULL, NULL);
	g_free(s);
	return res ? res : g_strdup(str);
}

/**
 * Parse a filename pattern and replace markers with values from a TrackDetails
 * structure.
 *
 * Valid markers so far are:
 * %at -- album title
 * %aa -- album artist
 * %aA -- album artist (lowercase)
 * %as -- album artist sortname
 * %aS -- album artist sortname (lowercase)
 * %tn -- track number (i.e 8)
 * %tN -- track number, zero padded (i.e 08)
 * %tt -- track title
 * %ta -- track artist
 * %tA -- track artist (lowercase)
 * %ts -- track artist sortname
 * %tS -- track artist sortname (lowercase)
 */
static char *
filepath_parse_pattern (const char *pattern, RhythmDBEntry *entry)
{
	/* p is the pattern iterator, i is a general purpose iterator */
	const char *p;
	char *temp;
	GString *s;

	if (pattern == NULL || pattern[0] == 0)
		return g_strdup (" ");

	s = g_string_new (NULL);

	p = pattern;
	while (*p) {
		char *string = NULL;

		/* If not a % marker, copy and continue */
		if (*p != '%') {
			g_string_append_c (s, *p++);
			/* Explicit increment as we continue past the increment */
			continue;
		}

		/* Is a % marker, go to next and see what to do */
		switch (*++p) {
		case '%':
			/*
			 * Literal %
			 */
			g_string_append_c (s, '%');
			break;
		case 'a':
			/*
			 * Album tag
			 */
			switch (*++p) {
			case 't':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
				break;
			case 'T':
				temp = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_FOLDED));
				break;
			case 'a':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
				break;
			case 'A':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_FOLDED));
				break;
			/*case 's':
				string = sanitize_path (album sort name);
				break;
			case 'S':
				char *t = g_utf8_strdown (album sort name)
				string = sanitize_path (t);
				g_free (t);
				break;*/
			default:
				string = g_strdup_printf ("%%a%c", *p);
			}

			break;

		case 't':
			/*
			 * Track tag
			 */
			switch (*++p) {
			case 't':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
				break;
			case 'T':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED));
				break;
			case 'a':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
				break;
			case 'A':
				string = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_FOLDED));
				break;
			/*case 's':
				string = sanitize_path (artist sort name);
				break;
			case 'S':
				char *t = g_utf8_strdown (artist sort name)
				string = sanitize_path (t);
				g_free (t);
				break;*/
			case 'n':
				/* Track number */
				string = g_strdup_printf ("%u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
				break;
			case 'N':
				/* Track number, zero-padded */
				string = g_strdup_printf ("%02u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
				break;
			default:
				string = g_strdup_printf ("%%t%c", *p);
 			}

			break;

		default:
			string = g_strdup_printf ("%%%c", *p);
		}

		if (string)
			g_string_append (s, string);
		g_free (string);

		++p;
	}

	temp = s->str;
	g_string_free (s, FALSE);
	return temp;
}

static void
layout_example_label_update (RBLibrarySource *source)
{
	char *file_pattern;
	char *path_pattern;
	char *file_value;
	char *path_value;
	char *example;
	char *format;
	GMAudioProfile *profile;
	RhythmDBEntry *sample_entry;

  	profile = gm_audio_profile_choose_get_active (source->priv->preferred_format_menu);

	/* TODO: sucky. Replace with get-gconf-key-with-default mojo */
	file_pattern = eel_gconf_get_string (CONF_LIBRARY_LAYOUT_FILENAME);
	if (file_pattern == NULL) {
		file_pattern = g_strdup (library_layout_filenames[0].path);
	}
	path_pattern = eel_gconf_get_string (CONF_LIBRARY_LAYOUT_PATH);
	if (path_pattern == NULL) {
		path_pattern = g_strdup (library_layout_paths[0].path);
	}

	sample_entry = rhythmdb_entry_example_new (source->priv->db, RHYTHMDB_ENTRY_TYPE_SONG, NULL);
	file_value = filepath_parse_pattern (file_pattern, sample_entry);
	path_value = filepath_parse_pattern (path_pattern, sample_entry);
	rhythmdb_entry_unref (sample_entry);

	example = g_build_filename (G_DIR_SEPARATOR_S, path_value, file_value, NULL);
	g_free (file_value);
	g_free (file_pattern);
	g_free (path_value);
	g_free (path_pattern);

	format = g_strconcat ("<small><i><b>Example Path:</b> ",
			      example,
			      ".",
			      profile ? gm_audio_profile_get_extension (profile) : "ogg",
			      "</i></small>", NULL);
	g_free (example);

	gtk_label_set_markup (GTK_LABEL (source->priv->layout_example_label), format);
	g_free (format);
}

static void
rb_library_source_layout_path_changed (GConfClient *client,
				       guint cnxn_id,
				       GConfEntry *entry,
				       RBLibrarySource *source)
{
	char *value;
	int i = 0;

	g_return_if_fail (strcmp (entry->key, CONF_LIBRARY_LAYOUT_PATH) == 0);

	rb_debug ("layout path changed");

	if (entry->value == NULL) {
		value = g_strdup (library_layout_paths[0].path);
	} else if (entry->value->type == GCONF_VALUE_STRING) {
		value = g_strdup (gconf_value_get_string (entry->value));
	} else {
		return;
	}

	while (library_layout_paths[i].path && strcmp (library_layout_paths[i].path, value) != 0) {
		i++;
	}

	g_free (value);
	gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->layout_path_menu), i);

	layout_example_label_update (source);
}

static void
rb_library_source_layout_filename_changed (GConfClient *client,
					   guint cnxn_id,
					   GConfEntry *entry,
					   RBLibrarySource *source)
{
	char *value;
	int i = 0;

	g_return_if_fail (strcmp (entry->key, CONF_LIBRARY_LAYOUT_FILENAME) == 0);

	rb_debug ("layout filename changed");

	if (entry->value == NULL) {
		value = g_strdup (library_layout_filenames[0].path);
	} else if (entry->value->type == GCONF_VALUE_STRING) {
		value = g_strdup (gconf_value_get_string (entry->value));
	} else {
		return;
	}

	while (library_layout_filenames[i].path && strcmp (library_layout_filenames[i].path, value) != 0) {
		i++;
	}

	g_free (value);
	gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->layout_filename_menu), i);

	layout_example_label_update (source);
}

/**
 * Build the absolute filename for the specified track.
 *
 * The base path is the extern variable 'base_path', the format to use
 * is the extern variable 'file_pattern'. Free the result when you
 * have finished with it.
 *
 * Stolen from Sound-Juicer
 */
static char*
build_filename (RBLibrarySource *source, RhythmDBEntry *entry)
{
	GnomeVFSURI *uri;
	GnomeVFSURI *new;
	char *realfile;
	char *realpath;
	char *filename;
	char *string;
	char *extension = NULL;
	GSList *list;
	const char *layout_path;
	const char *layout_filename;
	const char *preferred_format;

	list = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);
	layout_path = eel_gconf_get_string (CONF_LIBRARY_LAYOUT_PATH);
	layout_filename = eel_gconf_get_string (CONF_LIBRARY_LAYOUT_FILENAME);
	preferred_format = eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT);

	if (list == NULL || layout_path == NULL || layout_filename == NULL || preferred_format == NULL) {
		/* emit warning */
		rb_debug ("Could not retrieve settings from GConf");
		g_slist_free (list);
		return NULL;
	}

	uri = gnome_vfs_uri_new ((const char *)list->data);
	g_slist_free (list);

	realpath = filepath_parse_pattern (layout_path, entry);
	new = gnome_vfs_uri_append_path (uri, realpath);
	gnome_vfs_uri_unref (uri);
	uri = new;
	g_free (realpath);

	if (g_str_has_prefix (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE), "audio/x-raw")) {
		GMAudioProfile *profile;
		profile = gm_audio_profile_lookup (preferred_format);
		if (profile)
			extension = g_strdup (gm_audio_profile_get_extension (profile));
	}

	if (extension == NULL) {
		char *tmp;

		/* use the old extension. strip anything after a '?' for http/daap/etc */
		extension = g_strdup (g_utf8_strrchr (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), -1, '.') + 1);

		tmp = g_utf8_strchr (extension, -1, '?');
		if (tmp)
			*tmp = '\0';
	}

	realfile = filepath_parse_pattern (layout_filename, entry);
	if (extension) {
		filename = g_strdup_printf ("%s.%s", realfile, extension);
		g_free (realfile);
	} else {
		filename = realfile;
	}

	new = gnome_vfs_uri_append_file_name (uri, filename);
	gnome_vfs_uri_unref (uri);
	uri = new;
	g_free (extension);
	g_free (filename);

	string = gnome_vfs_uri_to_string (uri, 0);
	gnome_vfs_uri_unref (uri);
	return string;
}
#endif

static gboolean
impl_can_paste (RBSource *asource)
{
#ifdef ENABLE_TRACK_TRANSFER
	GSList *list;
	gboolean can_paste = TRUE;

	list = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);
	can_paste = ((list != NULL) &&
		     (eel_gconf_get_string (CONF_LIBRARY_LAYOUT_PATH) != NULL ) &&
		     (eel_gconf_get_string (CONF_LIBRARY_LAYOUT_FILENAME) != NULL) &&
		     (eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT) != NULL));

	g_slist_free (list);
	return can_paste;
#else
	return FALSE;
#endif
}

#ifdef ENABLE_TRACK_TRANSFER
static void
completed_cb (RhythmDBEntry *entry, const char *dest, RBLibrarySource *source)
{
	rhythmdb_add_uri (source->priv->db, dest);
}

static void
impl_paste (RBSource *asource, GList *entries)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RBRemovableMediaManager *rm_mgr;
	GList *l;
	GSList *sl;
	RBShell *shell;

	sl = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);
	if ((sl == NULL) ||
	    (eel_gconf_get_string (CONF_LIBRARY_LAYOUT_PATH) == NULL )||
	    (eel_gconf_get_string (CONF_LIBRARY_LAYOUT_FILENAME) == NULL) ||
	    (eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT) == NULL)) {
		g_slist_free (sl);
		g_warning ("RBLibrarySource impl_paste called when gconf keys unset");
		return;
	}

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "removable-media-manager", &rm_mgr, NULL);
	g_object_unref (G_OBJECT (shell));

	for (l = entries; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;
		RhythmDBEntryType entry_type;
		char *dest;

		rb_debug ("pasting entry %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

		entry_type = rhythmdb_entry_get_entry_type (entry);
		if (entry_type == RHYTHMDB_ENTRY_TYPE_SONG)
			/* copying to ourselves would be silly */
			continue;

		/* see if the responsible source lets us copy */
		if (!rb_source_can_copy (rb_shell_get_source_by_entry_type (shell, entry_type)))
			continue;

		dest = build_filename (source, entry);
		if (dest == NULL) {
			rb_debug ("could not create destination path for entry");
			continue;
		}

		rb_removable_media_manager_queue_transfer (rm_mgr, entry,
							  dest, NULL,
							  (RBTranferCompleteCallback)completed_cb, source);
	}

	g_object_unref (G_OBJECT (rm_mgr));
}
#endif

static void
rb_library_source_add_child_source (const char *path, RBLibrarySource *library_source)
{
	RBSource *source;
	GPtrArray *query;
	RBShell *shell;
	GnomeVFSURI *uri;
	char *name;
	GdkPixbuf *icon;

	g_object_get (G_OBJECT (library_source), "shell", &shell, NULL);
	uri = gnome_vfs_uri_new (path);
	name = gnome_vfs_uri_extract_short_name (uri);
	gnome_vfs_uri_unref (uri);

	source = rb_auto_playlist_source_new (shell, name, FALSE);
	query = rhythmdb_query_parse (library_source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_LOCATION, path,
				      RHYTHMDB_QUERY_END);
	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (source), query,
					   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
					   NULL, 0);
	rhythmdb_query_free (query);

	g_object_get (G_OBJECT (library_source), "icon", &icon, NULL);
	g_object_set (G_OBJECT (source), "icon", icon, NULL);

	rb_shell_append_source (shell, source, RB_SOURCE (library_source));
	library_source->priv->child_sources = g_list_prepend (library_source->priv->child_sources, source);

	g_object_unref (shell);
	g_free (name);
}

static void
rb_library_source_sync_child_sources (RBLibrarySource *source)
{
	GSList *list;

	list = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);

	/* FIXME: don't delete and re-create sources that are still there */
	g_list_foreach (source->priv->child_sources, (GFunc)rb_source_delete_thyself, NULL);
	g_list_free (source->priv->child_sources);
	source->priv->child_sources = NULL;

	if (g_slist_length (list) > 1)
		g_slist_foreach (list, (GFunc)rb_library_source_add_child_source, source);
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);
}
