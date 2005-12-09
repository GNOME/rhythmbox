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
#include <libgnomevfs/gnome-vfs.h>
#include <glade/glade.h>

#include "rb-source.h"
#include "rb-library-source.h"

#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"
#include "rb-property-view.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-search-entry.h"
#include "rb-preferences.h"
#include "rb-shell-preferences.h"

typedef enum
{
	RB_LIBRARY_QUERY_TYPE_ALL,
	RB_LIBRARY_QUERY_TYPE_GENRE,
	RB_LIBRARY_QUERY_TYPE_ARTIST,
	RB_LIBRARY_QUERY_TYPE_ALBUM,
	RB_LIBRARY_QUERY_TYPE_SEARCH,
} RBLibraryQueryType;

static void rb_library_source_class_init (RBLibrarySourceClass *klass);
static void rb_library_source_init (RBLibrarySource *source);
static GObject *rb_library_source_constructor (GType type, guint n_construct_properties,
					       GObjectConstructParam *construct_properties);
static void rb_library_source_dispose (GObject *object);
static void rb_library_source_finalize (GObject *object);
static void rb_library_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_library_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void rb_library_source_cmd_choose_genre (GtkAction *action,
						RBShell *shell);
static void rb_library_source_cmd_choose_artist (GtkAction *action,
						 RBShell *shell);
static void rb_library_source_cmd_choose_album (GtkAction *action,
						RBShell *shell);
static void rb_library_source_handle_genre_selection (RBLibrarySource *libsource, GList *genres);
static void rb_library_source_handle_artist_selection (RBLibrarySource *libsource, GList *artists);
static void rb_library_source_handle_album_selection (RBLibrarySource *libsource, GList *albums);

static void genres_selected_cb (RBPropertyView *propview, GList *genres,
			       RBLibrarySource *libsource);
static void genres_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource);
static void artists_selected_cb (RBPropertyView *propview, GList *artists,
			       RBLibrarySource *libsource);
static void artists_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource);
static void albums_selected_cb (RBPropertyView *propview, GList *albums,
			       RBLibrarySource *libsource);
static void albums_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource);
static void songs_view_sort_order_changed_cb (RBEntryView *view, RBLibrarySource *source);
static void rb_library_source_library_location_cb (GtkFileChooser *chooser,
						   RBLibrarySource *source);

static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBLibrarySource *source);
static void rb_library_source_state_pref_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBLibrarySource *source);
static void rb_library_source_ui_pref_changed (GConfClient *client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       RBLibrarySource *source); 
static void rb_library_source_browser_views_changed (GConfClient *client,
						     guint cnxn_id,
						     GConfEntry *entry,
						     RBLibrarySource *source);
static void rb_library_source_library_location_changed (GConfClient *client,
							guint cnxn_id,
							GConfEntry *entry,
							RBLibrarySource *source);
static void rb_library_source_prefs_update (RBShellPreferences *prefs,
					    RBLibrarySource *source);
static void rb_library_source_state_prefs_sync (RBLibrarySource *source);
static void rb_library_source_ui_prefs_sync (RBLibrarySource *source);
static void rb_library_source_preferences_sync (RBLibrarySource *source);
/* source methods */
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static GList *impl_get_extra_views (RBSource *source);
static void impl_delete (RBSource *source);
static void impl_move_to_trash (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_reset_filters (RBSource *source);
static GtkWidget *impl_get_config_widget (RBSource *source, RBShellPreferences *prefs);
static void impl_song_properties (RBSource *source);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_show_popup (RBSource *source);
static const char * impl_get_paned_key (RBLibrarySource *source);
static void rb_library_source_do_query (RBLibrarySource *source, RBLibraryQueryType qtype);

void rb_library_source_browser_views_activated_cb (GtkWidget *widget,
						 RBLibrarySource *source);
static GPtrArray * construct_query_from_selection (RBLibrarySource *source);
static void songs_view_drag_data_received_cb (GtkWidget *widget,
					      GdkDragContext *dc,
					      gint x, gint y,
					      GtkSelectionData *data,
					      guint info, guint time,
					      RBLibrarySource *source);
static void rb_library_source_watch_toggled_cb (GtkToggleButton *button,
						RBLibrarySource *source);


#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_UI_LIBRARY_BROWSER_VIEWS CONF_PREFIX "/ui/library/browser_views"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_LIBRARY_SORTING CONF_PREFIX "/state/library/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/library/show_browser"

struct RBLibrarySourcePrivate
{
	RhythmDB *db;
	
	GtkWidget *browser;
	GtkWidget *vbox;

	GdkPixbuf *pixbuf;

	RBPropertyView *genres;
	RBPropertyView *artists;
	RBPropertyView *albums;
	RBEntryView *songs;

	GtkWidget *paned;

	gboolean lock;

	char *artist;
	char *album;

	RhythmDBQueryModel *cached_all_query;
	RhythmDBPropertyModel *cached_genres_model;
	RhythmDBPropertyModel *cached_artists_model;
	RhythmDBPropertyModel *cached_albums_model;
	
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *active_query;
	RBLibraryQueryType query_type;
	char *search_text;
	GList *selected_genres;
	GList *selected_artists;
	GList *selected_albums;
	
	gboolean loading_prefs;
	RBShellPreferences *shell_prefs;

	GtkActionGroup *action_group;
	GtkWidget *config_widget;
	GSList *browser_views_group;

	GtkFileChooser *library_location_widget;
	GtkWidget *watch_library_check;
	gboolean library_location_change_pending, library_location_handle_pending;
	
	RhythmDBEntryType entry_type;

	char *sorting_key;
	guint ui_dir_notify_id;
	guint state_paned_notify_id;
	guint state_browser_notify_id;
	guint state_sorting_notify_id;
	guint browser_view_notify_id;
	guint library_location_notify_id;
};

#define RB_LIBRARY_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourcePrivate))

static GtkActionEntry rb_library_source_actions [] =
{
	{ "LibrarySrcChooseGenre", NULL, N_("Browse This _Genre"), NULL,
	  N_("Set the browser to view only this genre"),
	  G_CALLBACK (rb_library_source_cmd_choose_genre) },
	{ "LibrarySrcChooseArtist", NULL , N_("Browse This _Artist"), NULL,
	  N_("Set the browser to view only this artist"),
	  G_CALLBACK (rb_library_source_cmd_choose_artist) },
	{ "LibrarySrcChooseAlbum", NULL, N_("Browse This A_lbum"), NULL,
	  N_("Set the browser to view only this album"),
	  G_CALLBACK (rb_library_source_cmd_choose_album) }
};

static const GtkTargetEntry songs_view_drag_types[] = {{  "text/uri-list", 0, 0 }};

enum
{
	PROP_0,
	PROP_ICON,
	PROP_ENTRY_TYPE,
	PROP_SORTING_KEY
};


G_DEFINE_TYPE (RBLibrarySource, rb_library_source, RB_TYPE_SOURCE)

static void
rb_library_source_class_init (RBLibrarySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_library_source_dispose;
	object_class->finalize = rb_library_source_finalize;
	object_class->constructor = rb_library_source_constructor;

	object_class->set_property = rb_library_source_set_property;
	object_class->get_property = rb_library_source_get_property;

	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_extra_views = impl_get_extra_views;
	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_move_to_trash = impl_move_to_trash;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;

	klass->impl_get_paned_key = impl_get_paned_key;
	klass->impl_has_first_added_column = (RBLibrarySourceFeatureFunc) rb_true_function;
	klass->impl_has_drop_support = (RBLibrarySourceFeatureFunc) rb_true_function;

	g_object_class_install_property (object_class,
					 PROP_ICON,
					 g_param_spec_object ("icon",
							      "Icon",
							      "Source Icon",
							      GDK_TYPE_PIXBUF,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_uint ("entry-type",
							    "Entry type",
							    "Type of the entries which should be displayed by this source",
							    0,
							    G_MAXINT,
							    RHYTHMDB_ENTRY_TYPE_SONG,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SORTING_KEY,
					 g_param_spec_string ("sorting-key",
							      "Sorting key",
							      "GConf key for storing sort-order",
							      CONF_STATE_LIBRARY_SORTING,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBLibrarySourcePrivate));
}

static void
update_browser_views_visibility (RBLibrarySource *source)
{
	int views;
	GtkWidget *genres = GTK_WIDGET (source->priv->genres);
	GtkWidget *artists = GTK_WIDGET (source->priv->artists);
	GtkWidget *albums = GTK_WIDGET (source->priv->albums);
	GtkAction *action;

	views = eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS);

	gtk_widget_show (genres);
	action = gtk_action_group_get_action (source->priv->action_group,
					      "LibrarySrcChooseGenre");
	if (action) {
		g_object_set (G_OBJECT (action), "sensitive", TRUE, NULL);
	}


	gtk_widget_show (artists);	
	action = gtk_action_group_get_action (source->priv->action_group,
					      "LibrarySrcChooseArtist"); 
	if (action) {
		g_object_set (G_OBJECT (action), "sensitive", TRUE, NULL);
	}

	gtk_widget_show (albums);
	action = gtk_action_group_get_action (source->priv->action_group,
					      "LibrarySrcChooseAlbum"); 
	if (action) {
		g_object_set (G_OBJECT (action), "sensitive", TRUE, NULL);
	}
	
	switch (views)
	{
	case 0:
		action = gtk_action_group_get_action (source->priv->action_group,
						      "LibrarySrcChooseGenre");
		if (action) {
			g_object_set (G_OBJECT (action), 
				      "sensitive", FALSE, 
				      NULL);
		}
		gtk_widget_hide (genres);
		rb_library_source_handle_genre_selection (source, NULL);
		/* Since this is hidden now, reset the query */
		break;
	case 1:
		action = gtk_action_group_get_action (source->priv->action_group,
						      "LibrarySrcChooseAlbum");
		if (action) {
			g_object_set (G_OBJECT (action), 
				      "sensitive", FALSE, 
				      NULL);
		}
		gtk_widget_hide (albums);
		rb_library_source_handle_album_selection (source, NULL);
		break;
	case 2:
		/* All are shown */
		break;
	default:
		break;
	}
}

static void
rb_library_source_browser_views_changed (GConfClient *client,
					 guint cnxn_id,
					 GConfEntry *entry,
					 RBLibrarySource *source)
{
	update_browser_views_visibility (source);
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
rb_library_source_init (RBLibrarySource *source)
{
	source->priv = RB_LIBRARY_SOURCE_GET_PRIVATE (source);

	/* Drag'n'Drop */

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
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
	eel_gconf_notification_remove (source->priv->browser_view_notify_id);
	eel_gconf_notification_remove (source->priv->library_location_notify_id);
	eel_gconf_notification_remove (source->priv->state_browser_notify_id);
	eel_gconf_notification_remove (source->priv->state_paned_notify_id);
	eel_gconf_notification_remove (source->priv->state_sorting_notify_id);

	g_free (source->priv->sorting_key);
	g_free (source->priv->artist);
	g_free (source->priv->album);

	if (source->priv->cached_all_query)
		g_object_unref (G_OBJECT (source->priv->cached_all_query));

	G_OBJECT_CLASS (rb_library_source_parent_class)->finalize (object);
}

static void
rb_library_source_songs_show_popup_cb (RBEntryView *view,
				       RBLibrarySource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/LibraryViewPopup");
}

static void
register_action_group (RBLibrarySource *source)
{
	GtkUIManager *uimanager;
	GList *actiongroups;
	GList *group;
	RBShell *shell;

	g_object_get (G_OBJECT (source), "ui-manager", &uimanager, NULL);
	actiongroups = gtk_ui_manager_get_action_groups (uimanager);

	/* Don't create the action group if it's already registered */
	for (group = actiongroups; group != NULL; group = group->next) {
		const gchar *name;

		name = gtk_action_group_get_name (GTK_ACTION_GROUP (group->data));
		if (strcmp (name, "LibraryActions") == 0) {
			g_object_unref (G_OBJECT (uimanager));
			source->priv->action_group = GTK_ACTION_GROUP (group->data);
			return;
		}
	}

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	source->priv->action_group = gtk_action_group_new ("LibraryActions");
	gtk_action_group_set_translation_domain (source->priv->action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (source->priv->action_group, 
				      rb_library_source_actions,
				      G_N_ELEMENTS (rb_library_source_actions),
				      shell);
	gtk_ui_manager_insert_action_group (uimanager, 
					    source->priv->action_group, 0);
	g_object_unref (G_OBJECT (uimanager));
	g_object_unref (G_OBJECT (shell));

}

static GObject *
rb_library_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBLibrarySource *source;
	RBLibrarySourceClass *klass;
	RBShell *shell;

	klass = RB_LIBRARY_SOURCE_CLASS (g_type_class_peek (RB_TYPE_LIBRARY_SOURCE));

	source = RB_LIBRARY_SOURCE (G_OBJECT_CLASS (rb_library_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	register_action_group (source);

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	g_object_unref (G_OBJECT (shell));

	source->priv->paned = gtk_vpaned_new ();

	source->priv->browser = gtk_hbox_new (TRUE, 5);

	/* set up songs tree view */
	source->priv->songs = rb_entry_view_new (source->priv->db, source->priv->sorting_key,
						 TRUE, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TRACK_NUMBER);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_YEAR);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION);
/* 	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_QUALITY); */
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_RATING);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LAST_PLAYED);
	if (rb_library_source_has_first_added_column (source))
		rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_FIRST_SEEN);

	g_signal_connect_object (G_OBJECT (source->priv->songs), "show_popup",
				 G_CALLBACK (rb_library_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->songs),
				 "sort-order-changed",
				 G_CALLBACK (songs_view_sort_order_changed_cb),
				 source, 0);

	/* set up drag and drop for the song tree view.
	 * we don't use RBEntryView's DnD support because it does too much.
	 * we just want to be able to drop songs in to add them to the 
	 * library.
	 */
	g_signal_connect_object (G_OBJECT (source->priv->songs), 
				 "drag_data_received",
				 G_CALLBACK (songs_view_drag_data_received_cb),
				 source, 0);

	/* only add drop support for the Library, subclasses can add it themselves */	
	if (rb_library_source_has_drop_support (source)) {
		gtk_drag_dest_set (GTK_WIDGET (source->priv->songs),
				   GTK_DEST_DEFAULT_ALL,
				   songs_view_drag_types, 1,
				   GDK_ACTION_COPY | GDK_ACTION_MOVE);	/* really accept move actions? */
	}

	/* set up genres treeview */
	source->priv->genres = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_GENRE,
						     _("Genre"));
	rb_property_view_set_selection_mode (source->priv->genres, GTK_SELECTION_MULTIPLE);
	g_signal_connect_object (G_OBJECT (source->priv->genres),
				 "properties-selected",
				 G_CALLBACK (genres_selected_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->genres),
				 "property-selection-reset", G_CALLBACK (genres_selection_reset_cb),
				 source, 0);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->genres));

	/* set up artist treeview */
	source->priv->artists = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ARTIST,
						      _("Artist"));
	rb_property_view_set_selection_mode (source->priv->artists, GTK_SELECTION_MULTIPLE);
	g_signal_connect_object (G_OBJECT (source->priv->artists),
				 "properties-selected",
				 G_CALLBACK (artists_selected_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->artists),
				 "property-selection-reset", G_CALLBACK (artists_selection_reset_cb),
				 source, 0);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->artists));

	/* set up albums treeview */
	source->priv->albums = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ALBUM,
						     _("Album"));
	rb_property_view_set_selection_mode (source->priv->albums, GTK_SELECTION_MULTIPLE);
	g_signal_connect_object (G_OBJECT (source->priv->albums),
				 "properties-selected",
				 G_CALLBACK (albums_selected_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->albums),
				 "property-selection-reset", G_CALLBACK (albums_selection_reset_cb),
				 source, 0);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->albums));
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), source->priv->browser, FALSE, FALSE);

	/* this gets emitted when the paned thingie is moved */
	g_signal_connect_object (G_OBJECT (source->priv->songs),
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 source, 0);

	gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->songs), TRUE, FALSE);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

	gtk_widget_show_all (GTK_WIDGET (source));

	rb_library_source_state_prefs_sync (source);
	rb_library_source_ui_prefs_sync (source);
	update_browser_views_visibility (source);

	if (source->priv->sorting_key) {
		source->priv->state_sorting_notify_id =
			eel_gconf_notification_add (source->priv->sorting_key,
					    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
					    source);
	}
	if (rb_library_source_get_paned_key (source)) {
		source->priv->state_paned_notify_id =
			eel_gconf_notification_add (rb_library_source_get_paned_key (source),
					    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
					    source);
	}
	if (rb_source_get_browser_key (RB_SOURCE (source))) {
		source->priv->state_browser_notify_id =
			eel_gconf_notification_add (rb_source_get_browser_key (RB_SOURCE (source)),
					    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
					    source);
	}

	source->priv->ui_dir_notify_id =
		eel_gconf_notification_add (CONF_UI_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_ui_pref_changed, source);
	source->priv->browser_view_notify_id =
		eel_gconf_notification_add (CONF_UI_LIBRARY_BROWSER_VIEWS,
				    (GConfClientNotifyFunc) rb_library_source_browser_views_changed, source);
	source->priv->library_location_notify_id =
		eel_gconf_notification_add (CONF_LIBRARY_LOCATION,
				    (GConfClientNotifyFunc) rb_library_source_library_location_changed, source);

	rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_ALL);
	return G_OBJECT (source);
}


static void
rb_library_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ICON:
		if (source->priv->pixbuf) {
			g_object_unref (G_OBJECT (source->priv->pixbuf));
		}
		source->priv->pixbuf = g_value_get_object (value);

		if (source->priv->pixbuf)
			g_object_ref (source->priv->pixbuf);
		break;
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_uint (value);
		break;
	case PROP_SORTING_KEY:
		g_free (source->priv->sorting_key);
		source->priv->sorting_key = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ICON:
		g_value_set_object (value, source->priv->pixbuf);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_uint (value, source->priv->entry_type);
		break;
	case PROP_SORTING_KEY:
		g_value_set_string (value, source->priv->sorting_key);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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
					  "shell", shell,
					  "icon", icon,
					  NULL));
	rb_shell_register_entry_type_for_source (shell, source, 
						 RHYTHMDB_ENTRY_TYPE_SONG);
	return source;
}

static gboolean
string_list_equal (GList *a, GList *b)
{
	GList *sorted_a_keys;
	GList *sorted_b_keys;
	GList *a_ptr, *b_ptr;
	gboolean ret = TRUE;

	if (g_list_length (a) != g_list_length (b))
		return FALSE;

	for (sorted_a_keys = NULL; a; a = a->next) {
		sorted_a_keys = g_list_prepend (sorted_a_keys,
					       g_utf8_collate_key (a->data, -1));
	}
	for (sorted_b_keys = NULL; b; b = b->next) {
		sorted_b_keys = g_list_prepend (sorted_b_keys,
					       g_utf8_collate_key (b->data, -1));
	}
	sorted_a_keys = g_list_sort (sorted_a_keys, (GCompareFunc) strcmp);
	sorted_b_keys = g_list_sort (sorted_b_keys, (GCompareFunc) strcmp);
	
	for (a_ptr = sorted_a_keys, b_ptr = sorted_b_keys;
	     a_ptr && b_ptr; a_ptr = a_ptr->next, b_ptr = b_ptr->next) {
		if (strcmp (a_ptr->data, b_ptr->data)) {
			ret = FALSE;
			break;
		}
	}
	g_list_foreach (sorted_a_keys, (GFunc) g_free, NULL);
	g_list_foreach (sorted_b_keys, (GFunc) g_free, NULL);
	g_list_free (sorted_a_keys);
	g_list_free (sorted_b_keys);
	return ret;
}



static void
rb_library_source_gather_hash_keys (char *key, gpointer unused,
				    GList **data)
{
	*data = g_list_prepend (*data, key);
}

static void
rb_library_source_free_list_data (gpointer *data, gpointer unused)
{
	g_free (data);
}

static GList *
rb_library_source_gather_properties (RBLibrarySource *source,
				     RhythmDBPropType prop)
{
	GList *selected, *tem;
	GHashTable *selected_set;

	selected_set = g_hash_table_new (g_str_hash, g_str_equal);
	selected = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = selected; tem; tem = tem->next) {
		RhythmDBEntry *entry = tem->data;
		char *val = g_strdup (rhythmdb_entry_get_string (entry, prop));
		g_hash_table_insert (selected_set, val, NULL);
	}


	g_list_free (selected);
	
	tem = NULL;
	g_hash_table_foreach (selected_set, (GHFunc) rb_library_source_gather_hash_keys,
			      &tem);
	g_hash_table_destroy (selected_set);
	return tem;
}

static void
rb_library_source_cmd_choose_genre (GtkAction *action,
				    RBShell *shell)
{
	GList *props;
	RBLibrarySource *source;

	rb_debug ("choosing genre");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_library_source_gather_properties (source, RHYTHMDB_PROP_GENRE);
	rb_property_view_set_selection (source->priv->genres, props);
	g_list_foreach (props, (GFunc) rb_library_source_free_list_data, NULL);
	g_list_free (props);
	g_object_unref (source);
}

static void
rb_library_source_cmd_choose_artist (GtkAction *action,
				     RBShell *shell)
{
	GList *props;	
	RBLibrarySource *source;

	rb_debug ("choosing artist");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_library_source_gather_properties (source, RHYTHMDB_PROP_ARTIST);
	rb_property_view_set_selection (source->priv->artists, props);
	g_list_foreach (props, (GFunc) rb_library_source_free_list_data, NULL);
	g_list_free (props);
	g_object_unref (source);
}

static void
rb_library_source_cmd_choose_album (GtkAction *action,
				    RBShell *shell)
{
	GList *props;	
	RBLibrarySource *source;

	rb_debug ("choosing album");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_library_source_gather_properties (source, RHYTHMDB_PROP_ALBUM);
	rb_property_view_set_selection (source->priv->albums, props);
	g_list_foreach (props, (GFunc) rb_library_source_free_list_data, NULL);
	g_list_free (props);
	g_object_unref (source);
}

static void
genres_selected_cb (RBPropertyView *propview, GList *genres,
		   RBLibrarySource *libsource)
{
	rb_debug ("genre selected"); 
	rb_library_source_handle_genre_selection (libsource, genres);
}

static void
rb_library_source_handle_genre_selection (RBLibrarySource *libsource, GList *genres)
{
	if (string_list_equal (libsource->priv->selected_genres, genres))
		return;
	g_list_foreach (libsource->priv->selected_genres, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_genres);
	libsource->priv->selected_genres = genres;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_GENRE);

	rb_source_notify_filter_changed (RB_SOURCE (libsource));
}

static void
genres_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource)
{
	rb_debug ("genre selection reset"); 
	g_list_foreach (libsource->priv->selected_genres, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_genres);
	libsource->priv->selected_genres = NULL;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_GENRE);
}

static void
artists_selected_cb (RBPropertyView *propview, GList *artists, 
		     RBLibrarySource *libsource)
{
	rb_library_source_handle_artist_selection (libsource, artists);
}

static void
rb_library_source_handle_artist_selection (RBLibrarySource *libsource, GList *artists)
{

	rb_debug ("artist selected"); 
	if (string_list_equal (libsource->priv->selected_artists, artists))
		return;
	g_list_foreach (libsource->priv->selected_artists, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_artists);
	libsource->priv->selected_artists = artists;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ARTIST);

	rb_source_notify_filter_changed (RB_SOURCE (libsource));
}

static void
artists_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource)
{
	rb_debug ("artist selection reset"); 
	g_list_foreach (libsource->priv->selected_artists, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_artists);
	libsource->priv->selected_artists = NULL;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ALBUM);
}

static void
albums_selected_cb (RBPropertyView *propview, GList *albums, 
		    RBLibrarySource *libsource)
{
	rb_debug ("album selected"); 
	rb_library_source_handle_album_selection (libsource, albums);
}

static void
rb_library_source_handle_album_selection (RBLibrarySource *libsource, GList *albums)
{
	if (string_list_equal (libsource->priv->selected_albums, albums))
		return;
	g_list_foreach (libsource->priv->selected_albums, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_albums);
	libsource->priv->selected_albums = albums;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ALBUM);

	rb_source_notify_filter_changed (RB_SOURCE (libsource));
}

static void
albums_selection_reset_cb (RBPropertyView *propview, RBLibrarySource *libsource)
{
	rb_debug ("album selection reset"); 
	g_list_foreach (libsource->priv->selected_albums, (GFunc) g_free, NULL);
	g_list_free (libsource->priv->selected_albums);
	libsource->priv->selected_albums = NULL;
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ALBUM);
}

static void
songs_view_sort_order_changed_cb (RBEntryView *view, RBLibrarySource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (view);
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return CONF_STATE_SHOW_BROWSER;
}

static const char *
impl_get_paned_key (RBLibrarySource *status)
{
	return CONF_STATE_PANED_POSITION;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	return source->priv->pixbuf;
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	if (search_text == NULL && source->priv->search_text == NULL)
		return;
	if (search_text != NULL &&
	    source->priv->search_text != NULL
	    && !strcmp (search_text, source->priv->search_text))
		return;

	if (search_text[0] == '\0')
		search_text = NULL;

	rb_debug ("doing search for \"%s\"", search_text ? search_text : "(NULL)");

	g_free (source->priv->search_text);
	source->priv->search_text = g_strdup (search_text);
	rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_SEARCH);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}


static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	return source->priv->songs;
}

static GList *
impl_get_extra_views (RBSource *asource)
{
	GList *ret;
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	ret = g_list_prepend (NULL, source->priv->albums);
	ret = g_list_prepend (ret, source->priv->artists);
	ret = g_list_prepend (ret, source->priv->genres);
	return ret;
}

static void
impl_reset_filters (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	gboolean changed = FALSE;

	if (source->priv->selected_genres != NULL)
		changed = TRUE;
	g_list_foreach (source->priv->selected_genres, (GFunc) g_free, NULL);
	g_list_free (source->priv->selected_genres);
	source->priv->selected_genres = NULL;

	if (source->priv->selected_artists != NULL)
		changed = TRUE;
	g_list_foreach (source->priv->selected_artists, (GFunc) g_free, NULL);
	g_list_free (source->priv->selected_artists);
	source->priv->selected_artists = NULL;

	if (source->priv->selected_albums != NULL)
		changed = TRUE;
	g_list_foreach (source->priv->selected_albums, (GFunc) g_free, NULL);
	g_list_free (source->priv->selected_albums);
	source->priv->selected_albums = NULL;

	if (source->priv->search_text != NULL)
		changed = TRUE;
	g_free (source->priv->search_text);
	source->priv->search_text = NULL;

	if (changed) {
		rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_ALL);
		rb_source_notify_filter_changed (RB_SOURCE (source));
	}
}
  
static GtkWidget *
impl_get_config_widget (RBSource *asource, RBShellPreferences *prefs)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkWidget *tmp;
	GladeXML *xml;

	if (source->priv->config_widget)
		return source->priv->config_widget;
	
	source->priv->shell_prefs = prefs;
	
	xml = rb_glade_xml_new ("library-prefs.glade", "library_vbox", source);
	source->priv->config_widget =
		glade_xml_get_widget (xml, "library_vbox");
	tmp = glade_xml_get_widget (xml, "library_browser_views_radio");
	source->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group 
					       (GTK_RADIO_BUTTON (tmp))));

	source->priv->library_location_widget =
		(GtkFileChooser*) glade_xml_get_widget (xml, "library_location_chooser");
	source->priv->watch_library_check = glade_xml_get_widget (xml, "watch_library_check");

	rb_glade_boldify_label (xml, "browser_views_label");
	rb_glade_boldify_label (xml, "library_location_label");
	g_object_unref (G_OBJECT (xml));
	
	rb_library_source_preferences_sync (source);
	g_signal_connect (G_OBJECT (source->priv->library_location_widget),
			  "selection-changed",
			  G_CALLBACK (rb_library_source_library_location_cb),
			  asource);
	g_signal_connect (G_OBJECT (source->priv->shell_prefs),
			  "hide",
			  G_CALLBACK (rb_library_source_prefs_update),
			  asource);
	g_signal_connect (G_OBJECT (source->priv->watch_library_check),
			  "toggled",
			  G_CALLBACK (rb_library_source_watch_toggled_cb),
			  asource);

	return source->priv->config_widget;
}

static void
rb_library_source_preferences_sync (RBLibrarySource *source)
{
	GSList *list;

	rb_debug ("syncing pref dialog state");

	list = g_slist_nth (source->priv->browser_views_group,
			    eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (list->data), TRUE);

	list = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);

	if (g_slist_length (list) == 1) {
		/* the uri might be missing the trailing slash */
		gchar *s = g_strconcat (list->data, G_DIR_SEPARATOR_S, NULL);
		gtk_file_chooser_set_uri (source->priv->library_location_widget, s);
		rb_debug ("syncing library location %s", s);
		g_free (s);
	} else {
		/* either no or multiple folders are chosen. make the widget blank*/
		/*gtk_file_chooser_set_uri (source->priv->library_location_widget,
					  "");*/
	}

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check),
				      eel_gconf_get_boolean (CONF_MONITOR_LIBRARY));
}

void
rb_library_source_browser_views_activated_cb (GtkWidget *widget,
					    RBLibrarySource *source)
{
	int index;

	if (source->priv->loading_prefs == TRUE)
		return;

	index = g_slist_index (source->priv->browser_views_group, widget);

	eel_gconf_set_integer (CONF_UI_LIBRARY_BROWSER_VIEWS, index);
}

static void
impl_delete (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_delete (source->priv->db, tem->data);
		rhythmdb_commit (source->priv->db);
	}
	g_list_free (sel);
}

static void
impl_move_to_trash (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_move_to_trash (source->priv->db,
					       (RhythmDBEntry *) tem->data);
		rhythmdb_commit (source->priv->db);
	}
	g_list_free (sel);
}

static void
impl_song_properties (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
 	GtkWidget *song_info = NULL; 

	g_return_if_fail (source->priv->songs != NULL);
        
 	song_info = rb_song_info_new (source->priv->songs); 

        g_return_if_fail (song_info != NULL);

 	if (song_info) 
 		gtk_widget_show_all (song_info); 
 	else 
		rb_debug ("failed to create dialog, or no selection!");
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBLibrarySource *source)
{
	const char *key = rb_library_source_get_paned_key (source);;

	/* save state */
	rb_debug ("paned size allocate");
	if (key)
		eel_gconf_set_integer (key, gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_library_source_state_prefs_sync (RBLibrarySource *source)
{
	const char *key;

	rb_debug ("syncing state");
	key = rb_library_source_get_paned_key (source);
	if (key)
		gtk_paned_set_position (GTK_PANED (source->priv->paned),
					eel_gconf_get_integer (key));

	key = rb_source_get_browser_key (RB_SOURCE (source));
	if (key && eel_gconf_get_boolean (key))
			gtk_widget_show (source->priv->browser);
		else
			gtk_widget_hide (source->priv->browser);
}

static void
rb_library_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBLibrarySource *source)
{
	rb_debug ("state prefs changed");
	rb_library_source_state_prefs_sync (source);
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

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:
	{
		char *uri = gtk_editable_get_chars (GTK_EDITABLE (uri_widget), 0, -1);
		if (uri != NULL) {
			GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
			if (vfsuri != NULL) {
				rhythmdb_add_uri (source->priv->db, uri);
				gnome_vfs_uri_unref (vfsuri);
			} else
				rb_debug ("invalid uri: \"%s\"", uri);

		}
		break;
	}
	default:
		rb_debug ("Location add cancelled");
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

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/LibrarySourcePopup");
	return TRUE;
}

const char *
rb_library_source_get_paned_key (RBLibrarySource *source)
{
	RBLibrarySourceClass *klass = RB_LIBRARY_SOURCE_GET_CLASS (source);

	if (klass->impl_get_paned_key)
		return klass->impl_get_paned_key (source);
	else
		return NULL;
}

gboolean
rb_library_source_has_first_added_column (RBLibrarySource *source)
{
	RBLibrarySourceClass *klass = RB_LIBRARY_SOURCE_GET_CLASS (source);

	return klass->impl_has_first_added_column (source);
}

gboolean
rb_library_source_has_drop_support (RBLibrarySource *source)
{
	RBLibrarySourceClass *klass = RB_LIBRARY_SOURCE_GET_CLASS (source);

	return klass->impl_has_drop_support (source);
}


static void
push_multi_equals_query (RhythmDB *db, GPtrArray *query, guint propid, GList *items)
{
	GPtrArray *subquery = g_ptr_array_new ();

	if (!items) {
		g_ptr_array_free (subquery, TRUE);
		return;
	}

	rhythmdb_query_append (db,
			       subquery,
			       RHYTHMDB_QUERY_PROP_EQUALS,
			       propid,
			       items->data,
			       RHYTHMDB_QUERY_END);
	items = items->next;
	while (items) {
		rhythmdb_query_append (db,
				       subquery,
				       RHYTHMDB_QUERY_DISJUNCTION,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       propid,
				       items->data,
				       RHYTHMDB_QUERY_END);
		items = items->next;
	}
	rhythmdb_query_append (db, query, RHYTHMDB_QUERY_SUBQUERY, subquery,
			       RHYTHMDB_QUERY_END);
}

static GPtrArray *
construct_query_from_selection (RBLibrarySource *source)
{
	GPtrArray *query;
	RhythmDBEntryType entry_type;

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);

	/* select where type="song"
	 */

	if (source->priv->search_text) {
		GPtrArray *subquery = rhythmdb_query_parse (source->priv->db,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_SEARCH_MATCH,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_END);
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY,
				       subquery,
				       RHYTHMDB_QUERY_END);
		/* select where type="song" and
		 *  (genre like "foo" or artist like "foo" or album like "foo")
		 */
	}

	if (source->priv->selected_genres) {
		if (source->priv->selected_genres->next == NULL)
			rhythmdb_query_append (source->priv->db,
					       query,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       RHYTHMDB_PROP_GENRE,
					       source->priv->selected_genres->data,
					       RHYTHMDB_QUERY_END);
		else 
			push_multi_equals_query (source->priv->db,
						 query,
						 RHYTHMDB_PROP_GENRE,
						 source->priv->selected_genres);
	}

	if (source->priv->selected_artists) {
		if (source->priv->selected_artists->next == NULL)
			rhythmdb_query_append (source->priv->db,
					       query,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       RHYTHMDB_PROP_ARTIST,
					       source->priv->selected_artists->data,
					       RHYTHMDB_QUERY_END);
		else 
			push_multi_equals_query (source->priv->db,
						 query,
						 RHYTHMDB_PROP_ARTIST,
						 source->priv->selected_artists);
	}

	if (source->priv->selected_albums) {
		if (source->priv->selected_albums->next == NULL)
			rhythmdb_query_append (source->priv->db,
					       query,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       RHYTHMDB_PROP_ALBUM,
					       source->priv->selected_albums->data,
					       RHYTHMDB_QUERY_END);
		else 
			push_multi_equals_query (source->priv->db,
						 query,
						 RHYTHMDB_PROP_ALBUM,
						 source->priv->selected_albums);
	}
	return query;
}

static void
rb_library_source_do_query (RBLibrarySource *source, RBLibraryQueryType qtype)
{
	RhythmDBQueryModel *query_model;
	RhythmDBPropertyModel *genre_model = NULL;
	RhythmDBPropertyModel *artist_model = NULL;
	RhythmDBPropertyModel *album_model = NULL;
	GtkTreeModel *model;
	GPtrArray *query;
	gboolean is_all_query;

	/* Unlocked */
	rb_debug ("preparing to read lock for query");

	is_all_query = (source->priv->selected_genres == NULL &&
			source->priv->selected_artists == NULL &&	    
			source->priv->selected_albums == NULL &&	    
			source->priv->search_text == NULL);

	if (is_all_query && source->priv->cached_all_query) {
		rb_debug ("sorting mismatch, freeing cached query");
		g_object_unref (source->priv->cached_all_query);
		g_object_unref (source->priv->cached_genres_model);
		g_object_unref (source->priv->cached_artists_model);
		g_object_unref (source->priv->cached_albums_model);
	}

	source->priv->query_type = qtype;
	rb_debug ("query type: %d", qtype);

	if ((source->priv->cached_all_query == NULL) || is_all_query ) {
		rb_debug ("caching new query");
		query_model = source->priv->active_query = source->priv->model = 
			source->priv->cached_all_query = rhythmdb_query_model_new_empty (source->priv->db);
		source->priv->cached_genres_model = rhythmdb_property_model_new (source->priv->db, RHYTHMDB_PROP_GENRE);
		source->priv->cached_artists_model = rhythmdb_property_model_new (source->priv->db, RHYTHMDB_PROP_ARTIST);
		source->priv->cached_albums_model = rhythmdb_property_model_new (source->priv->db, RHYTHMDB_PROP_ALBUM);
		g_object_set (G_OBJECT (source->priv->cached_genres_model),
			      "query-model", source->priv->cached_all_query, NULL);
		g_object_set (G_OBJECT (source->priv->cached_artists_model),
			      "query-model", source->priv->cached_all_query, NULL);
		g_object_set (G_OBJECT (source->priv->cached_albums_model),
			      "query-model", source->priv->cached_all_query, NULL);
		rb_property_view_set_model (source->priv->genres,
					    source->priv->cached_genres_model);
		rb_property_view_set_model (source->priv->artists,
					    source->priv->cached_artists_model);
		rb_property_view_set_model (source->priv->albums,
					    source->priv->cached_albums_model);
	} else {
		rb_debug ("query is not special");
		source->priv->active_query = source->priv->model = 
			query_model = rhythmdb_query_model_new_empty (source->priv->db);

		if (source->priv->selected_genres == NULL) {
			rb_property_view_set_model (source->priv->genres,
						    source->priv->cached_genres_model);
			if (source->priv->selected_artists == NULL) {
				rb_property_view_set_model (source->priv->artists,
							    source->priv->cached_artists_model);
				if (source->priv->selected_albums == NULL)
					rb_property_view_set_model (source->priv->albums,
								    source->priv->cached_albums_model);
			}
		}
		switch (qtype) {
		case RB_LIBRARY_QUERY_TYPE_ALL:
			rb_debug ("resetting genres view");
			g_list_foreach (source->priv->selected_genres,
					(GFunc) g_free, NULL);
			g_list_free (source->priv->selected_genres);
			source->priv->selected_genres = NULL;

			rb_property_view_reset (source->priv->genres);
			genre_model = rb_property_view_get_model (source->priv->genres);
			g_object_set (G_OBJECT (genre_model), "query-model",
				      query_model, NULL);
			g_object_unref (G_OBJECT (genre_model));
		case RB_LIBRARY_QUERY_TYPE_GENRE:
			rb_debug ("resetting artist view");
			g_list_foreach (source->priv->selected_artists,
					(GFunc) g_free, NULL);
			g_list_free (source->priv->selected_artists);
			source->priv->selected_artists = NULL;

			rb_property_view_reset (source->priv->artists);
			artist_model = rb_property_view_get_model (source->priv->artists);
			g_object_set (G_OBJECT (artist_model), "query-model",
				      query_model, NULL);
			g_object_unref (G_OBJECT (artist_model));
		case RB_LIBRARY_QUERY_TYPE_ARTIST:
			rb_debug ("resetting album view");
			g_list_foreach (source->priv->selected_albums,
					(GFunc) g_free, NULL);
			g_list_free (source->priv->selected_albums);
			source->priv->selected_albums = NULL;

			rb_property_view_reset (source->priv->albums);
			album_model = rb_property_view_get_model (source->priv->albums);
			g_object_set (G_OBJECT (album_model), "query-model",
				      query_model, NULL);
			g_object_unref (G_OBJECT (album_model));
		case RB_LIBRARY_QUERY_TYPE_ALBUM:
		case RB_LIBRARY_QUERY_TYPE_SEARCH:
			break;
		}
	}

	model = GTK_TREE_MODEL (query_model);

	rb_debug ("setting empty model");
	rb_entry_view_set_model (source->priv->songs, query_model);
	g_object_set (RB_SOURCE (source), "query-model", query_model, NULL);

	query = construct_query_from_selection (source);
	
	rb_debug ("doing query");
	rhythmdb_do_full_query_async_parsed (source->priv->db, model, query);
		
	rhythmdb_query_free (query);
	
	if (!is_all_query)
		g_object_unref (G_OBJECT (query_model));
}

static void
songs_view_drag_data_received_cb (GtkWidget *widget,
				  GdkDragContext *dc,
				  gint x, gint y,
				  GtkSelectionData *selection_data,
				  guint info, guint time,
				  RBLibrarySource *source)
{
	rb_debug ("data dropped on the library source song view");
	rb_source_receive_drag (RB_SOURCE (source), selection_data);
}

static gboolean
rb_library_source_process_library_location_change (RBLibrarySource *source)
{
	GSList *list;
	
	if (!source->priv->library_location_change_pending)
		return FALSE;

	/* process the change */
	list = gtk_file_chooser_get_uris (source->priv->library_location_widget);
	eel_gconf_set_string_list (CONF_LIBRARY_LOCATION, list);

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	source->priv->library_location_change_pending = FALSE;

	return FALSE;
}

static void
rb_library_source_prefs_update (RBShellPreferences *prefs,
				RBLibrarySource *source)
{
	rb_library_source_process_library_location_change (source);
}

static gboolean
rb_library_source_process_library_handle_selection (RBLibrarySource *source)
{
	GSList *list;
	
	if (!source->priv->library_location_handle_pending)
		return FALSE;

	/* this can't be processed immediately, because we sometimes get intemediate signals */
	for (list = gtk_file_chooser_get_uris (source->priv->library_location_widget);
	     list != NULL ; list = g_slist_next (list)) {
		if ((strcmp (list->data, "file:///") == 0) ||
		    (strcmp (list->data, "file://") == 0)) {
			rb_error_dialog (GTK_WINDOW (source->priv->shell_prefs), _("Cannot select library location"),
					 _("The root filesystem cannot be chosen as your library location. Please choose a different location"));
		} else {
			source->priv->library_location_change_pending = TRUE;
		}
	}

	source->priv->library_location_handle_pending = FALSE;
	return FALSE;
}

static void
rb_library_source_library_location_cb (GtkFileChooser *chooser,
				       RBLibrarySource *source)
{
	source->priv->library_location_handle_pending = TRUE;

	g_idle_add ((GSourceFunc)rb_library_source_process_library_handle_selection, source);
}

static void
rb_library_source_watch_toggled_cb (GtkToggleButton *button, RBLibrarySource *source)
{
	gboolean active;

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (source->priv->watch_library_check));
	eel_gconf_set_boolean (CONF_MONITOR_LIBRARY, active);
}

