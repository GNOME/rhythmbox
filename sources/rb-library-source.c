/*
 *  arch-tag: Implementation of local file source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <glade/glade.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <string.h>

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
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-search-entry.h"
#include "rb-preferences.h"

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
static void rb_library_source_finalize (GObject *object);
static void rb_library_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_library_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
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

static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBLibrarySource *source);
/* static void rb_library_source_drop_cb (GtkWidget        *widget, */
/* 				       GdkDragContext   *context, */
/* 				       gint              x, */
/* 				       gint              y, */
/* 				       GtkSelectionData *data, */
/* 				       guint             info, */
/* 				       guint             time, */
/* 				       gpointer          user_data); */

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
static void rb_library_source_state_prefs_sync (RBLibrarySource *source);
static void rb_library_source_ui_prefs_sync (RBLibrarySource *source);
static void rb_library_source_preferences_sync (RBLibrarySource *source);
/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static GList *impl_get_extra_views (RBSource *source);
static void impl_delete (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_reset_filters (RBSource *source);
static GtkWidget *impl_get_config_widget (RBSource *source);
static void impl_song_properties (RBSource *source);
static const char * impl_get_artist (RBSource *player);
static const char * impl_get_album (RBSource *player);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_show_popup (RBSource *source);
static void rb_library_source_do_query (RBLibrarySource *source, RBLibraryQueryType qtype);
static void query_complete_cb (RhythmDBQueryModel *model, RBLibrarySource *source);

void rb_library_source_browser_views_activated_cb (GtkWidget *widget,
						 RBLibrarySource *source);
static GPtrArray * construct_query_from_selection (RBLibrarySource *source);


#define LIBRARY_SOURCE_SONGS_POPUP_PATH "/popups/LibrarySongsList"
#define LIBRARY_SOURCE_POPUP_PATH "/popups/LibrarySourceList"

#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_UI_LIBRARY_BROWSER_VIEWS CONF_PREFIX "/ui/library/browser_views"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_LIBRARY_SORTING CONF_PREFIX "/ui/library/sorting"
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

	char *status;
	char *artist;
	char *album;

	RhythmDBQueryModel *cached_all_query;
	RhythmDBPropertyModel *cached_genres_model;
	RhythmDBPropertyModel *cached_artists_model;
	RhythmDBPropertyModel *cached_albums_model;
	char *cached_sorting_type;
	
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *active_query;
	RBLibraryQueryType query_type;
	char *search_text;
	GList *selected_genres;
	GList *selected_artists;
	GList *selected_albums;
	
	gboolean loading_prefs;

	GtkWidget *config_widget;
	GSList *browser_views_group;
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_LIBRARY,
};

static GObjectClass *parent_class = NULL;

/* dnd */
static const GtkTargetEntry target_uri [] = { { "text/uri-list", 0, 0 } };

GType
rb_library_source_get_type (void)
{
	static GType rb_library_source_type = 0;

	if (rb_library_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibrarySourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_source_class_init,
			NULL,
			NULL,
			sizeof (RBLibrarySource),
			0,
			(GInstanceInitFunc) rb_library_source_init
		};

		rb_library_source_type = g_type_register_static (RB_TYPE_SOURCE,
								 "RBLibrarySource",
								 &our_info, 0);

	}

	return rb_library_source_type;
}

static void
rb_library_source_class_init (RBLibrarySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_source_finalize;
	object_class->constructor = rb_library_source_constructor;

	object_class->set_property = rb_library_source_set_property;
	object_class->get_property = rb_library_source_get_property;

	source_class->impl_get_status = impl_get_status;
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
	source_class->impl_delete = impl_delete;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_artist = impl_get_artist;
	source_class->impl_get_album = impl_get_album;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
update_browser_views_visibility (RBLibrarySource *source)
{
	int views;
	GtkWidget *genres = GTK_WIDGET (source->priv->genres);
	GtkWidget *artists = GTK_WIDGET (source->priv->artists);
	GtkWidget *albums = GTK_WIDGET (source->priv->albums);

	views = eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS);
	
	switch (views)
	{
		case 0:
			gtk_widget_hide (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
			/* Since this is hidden now, reset the query */
			rb_library_source_handle_genre_selection (source, NULL);
		break;
		case 1:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_hide (albums);
			rb_library_source_handle_artist_selection (source, NULL);

		break;
		case 2:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
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
	GtkWidget *dummy = gtk_tree_view_new ();
	source->priv = g_new0 (RBLibrarySourcePrivate, 1);

	/* Drag'n'Drop */

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);

	source->priv->pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_LIBRARY,
						       GTK_ICON_SIZE_LARGE_TOOLBAR,
						       NULL);

	gtk_widget_destroy (dummy);
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

	g_free (source->priv->status);
	g_free (source->priv->artist);
	g_free (source->priv->album);

	g_free (source->priv->cached_sorting_type);
	if (source->priv->cached_all_query)
		g_object_unref (G_OBJECT (source->priv->cached_all_query));

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_library_source_songs_show_popup_cb (RBEntryView *view,
				       RBLibrarySource *library_source)
{
	GtkWidget *menu;
	GtkWidget *window;

	window = gtk_widget_get_ancestor (GTK_WIDGET (view),
					  BONOBO_TYPE_WINDOW);

	menu = gtk_menu_new ();

	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu),
			         LIBRARY_SOURCE_SONGS_POPUP_PATH);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static GObject *
rb_library_source_constructor (GType type, guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBLibrarySource *source;
	RBLibrarySourceClass *klass;
	GObjectClass *parent_class;  

	klass = RB_LIBRARY_SOURCE_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	source = RB_LIBRARY_SOURCE (parent_class->constructor (type, n_construct_properties,
							       construct_properties));

	source->priv->paned = gtk_vpaned_new ();

	source->priv->browser = gtk_hbox_new (TRUE, 5);

	/* set up songs tree view */
	source->priv->songs = rb_entry_view_new (source->priv->db, CONF_STATE_LIBRARY_SORTING,
						 TRUE, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TRACK_NUMBER);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_QUALITY);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_RATING);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LAST_PLAYED);

	g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
			  G_CALLBACK (rb_library_source_songs_show_popup_cb), source);
	g_signal_connect (G_OBJECT (source->priv->songs),
			  "sort-order-changed",
			  G_CALLBACK (songs_view_sort_order_changed_cb),
			  source);

	/* set up genres treeview */
	source->priv->genres = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_GENRE,
						     _("Genre"));
	rb_property_view_set_selection_mode (source->priv->genres, GTK_SELECTION_MULTIPLE);
	g_signal_connect (G_OBJECT (source->priv->genres),
			  "properties-selected",
			  G_CALLBACK (genres_selected_cb),
			  source);
	g_signal_connect (G_OBJECT (source->priv->genres),
			  "property-selection-reset", G_CALLBACK (genres_selection_reset_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->genres));

	/* set up artist treeview */
	source->priv->artists = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ARTIST,
						      _("Artist"));
	rb_property_view_set_selection_mode (source->priv->artists, GTK_SELECTION_MULTIPLE);
	g_signal_connect (G_OBJECT (source->priv->artists),
			  "properties-selected",
			  G_CALLBACK (artists_selected_cb),
			  source);
	g_signal_connect (G_OBJECT (source->priv->artists),
			  "property-selection-reset", G_CALLBACK (artists_selection_reset_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->artists));

	/* set up albums treeview */
	source->priv->albums = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ALBUM,
						     _("Album"));
	rb_property_view_set_selection_mode (source->priv->albums, GTK_SELECTION_MULTIPLE);
	g_signal_connect (G_OBJECT (source->priv->albums),
			  "properties-selected",
			  G_CALLBACK (albums_selected_cb),
			  source);
	g_signal_connect (G_OBJECT (source->priv->albums),
			  "property-selection-reset", G_CALLBACK (albums_selection_reset_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->albums));
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), source->priv->browser, FALSE, FALSE);

	rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_ALL);

	/* Drag'n'Drop for songs view */
/* 	g_signal_connect (G_OBJECT (source->priv->songs), "drag_data_received", */
/* 			  G_CALLBACK (rb_library_source_drop_cb), source); */
/* 	gtk_drag_dest_set (GTK_WIDGET (source->priv->songs), GTK_DEST_DEFAULT_ALL, */
/* 			   target_uri, 1, GDK_ACTION_COPY); */
/* 	rb_node_view_enable_drag_source (source->priv->songs, target_uri, 1); */

/* 	/\* Drag'n'Drop for albums view *\/ */
/* 	g_signal_connect (G_OBJECT (source->priv->albums), "drag_data_received", */
/* 			  G_CALLBACK (rb_library_source_drop_cb), source); */
/* 	gtk_drag_dest_set (GTK_WIDGET (source->priv->albums), GTK_DEST_DEFAULT_ALL, */
/* 			   target_uri, 1, GDK_ACTION_COPY); */
/* 	rb_node_view_enable_drag_source (source->priv->albums, target_id, 1); */

/* 	/\* Drag'n'Drop for artists view *\/ */
/* 	g_signal_connect (G_OBJECT (source->priv->artists), "drag_data_received", */
/* 			  G_CALLBACK (rb_library_source_drop_cb), source); */
/* 	gtk_drag_dest_set (GTK_WIDGET (source->priv->artists), GTK_DEST_DEFAULT_ALL, */
/* 			   target_uri, 1, GDK_ACTION_COPY); */
/* 	rb_node_view_enable_drag_source (source->priv->artists, target_id, 1); */

	/* this gets emitted when the paned thingie is moved */
	g_signal_connect (G_OBJECT (source->priv->songs),
			  "size_allocate",
			  G_CALLBACK (paned_size_allocate_cb),
			  source);

	gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->songs), TRUE, FALSE);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

	gtk_widget_show_all (GTK_WIDGET (source));

	rb_library_source_state_prefs_sync (source);
	rb_library_source_ui_prefs_sync (source);
	update_browser_views_visibility (source);
	eel_gconf_notification_add (CONF_STATE_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
				    source);
	eel_gconf_notification_add (CONF_UI_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_ui_pref_changed, source);
	eel_gconf_notification_add (CONF_UI_LIBRARY_BROWSER_VIEWS,
				    (GConfClientNotifyFunc) rb_library_source_browser_views_changed, source);
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
	case PROP_DB:
		source->priv->db = g_value_get_object (value);
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
	case PROP_DB:
		g_value_set_object (value, source->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_library_source_new (RhythmDB *db)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_LIBRARY_SOURCE,
					  "name", _("Library"),
					  "db", db,
					  NULL));

	return source;
}

/* static RBNode * */
/* ensure_node_selection (RBEntryView *source, */
/* 		       RBNode *all_node, */
/* 		       gboolean *changing_flag) */
/* { */
/* 	GList *selection = rb_node_view_get_selection (source); */

/* 	if (selection == NULL) { */
/* 		*changing_flag = TRUE; */
/* 		rb_node_view_select_node (source, all_node); */
/* 		*changing_flag = FALSE; */
/* 		selection = rb_node_view_get_selection (source); */
/* 	} */

/* 	return selection->data; */
/* } */

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
		sorted_a_keys = g_list_append (sorted_a_keys,
					       g_utf8_collate_key (a->data, -1));
	}
	for (sorted_b_keys = NULL; b; b = b->next) {
		sorted_b_keys = g_list_append (sorted_b_keys,
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
rb_library_source_sync_query (RBLibrarySource *source)
{
	GPtrArray *query = construct_query_from_selection (source);
	g_object_set (G_OBJECT (source->priv->model), "query", query, NULL);
	rhythmdb_query_free (query);
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
	rb_library_source_sync_query (libsource);
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
	rb_library_source_sync_query (libsource);
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
	rb_library_source_sync_query (libsource);
}

static void
songs_view_sort_order_changed_cb (RBEntryView *view, RBLibrarySource *source)
{
	rb_debug ("sort order changed");
	rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_SEARCH);
}

static const char *
impl_get_status (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	g_free (source->priv->status);
	source->priv->status = rhythmdb_compute_status_normal (rb_entry_view_get_num_entries (source->priv->songs),
							       rb_entry_view_get_duration (source->priv->songs),
							       rb_entry_view_get_total_size (source->priv->songs));
	return source->priv->status;
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return CONF_STATE_SHOW_BROWSER;
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

	rb_debug ("doing search for \"%s\"", search_text);

	g_free (source->priv->search_text);
	source->priv->search_text = search_text != NULL ? g_utf8_casefold (search_text, -1) : NULL;
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

	ret = g_list_append (NULL, source->priv->genres);
	ret = g_list_append (ret, source->priv->artists);
	ret = g_list_append (ret, source->priv->albums);
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
impl_get_config_widget (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkWidget *tmp;
	GtkWidget *browser_views_label;
	PangoAttrList *pattrlist;
	PangoAttribute *attr;
	GladeXML *xml;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	xml = rb_glade_xml_new ("library-prefs.glade", "library_vbox", source);
	source->priv->config_widget =
		glade_xml_get_widget (xml, "library_vbox");
	tmp = glade_xml_get_widget (xml, "library_browser_views_radio");
	source->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group 
					       (GTK_RADIO_BUTTON (tmp))));

	pattrlist = pango_attr_list_new ();
	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (pattrlist, attr);
	browser_views_label = glade_xml_get_widget (xml, "browser_views_label");
	gtk_label_set_attributes (GTK_LABEL (browser_views_label), pattrlist);
	pango_attr_list_unref (pattrlist);

	g_object_unref (G_OBJECT (xml));
	
	rb_library_source_preferences_sync (source);
	return source->priv->config_widget;
}

static const char *
impl_get_artist (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RhythmDBEntry *entry;

	g_free (source->priv->artist);

	entry = rb_entry_view_get_playing_entry (source->priv->songs);

	if (entry != NULL) {
		rhythmdb_read_lock (source->priv->db);
		
		source->priv->artist =
			g_strdup (rhythmdb_entry_get_string (source->priv->db, entry, RHYTHMDB_PROP_ARTIST));

		rhythmdb_read_unlock (source->priv->db);
	} else {
		source->priv->artist = NULL;
	}

	return source->priv->artist;
}

static const char *
impl_get_album (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RhythmDBEntry *entry;

	g_free (source->priv->artist);

	entry = rb_entry_view_get_playing_entry (source->priv->songs);

	if (entry != NULL) {
		rhythmdb_read_lock (source->priv->db);

		source->priv->album =
			g_strdup (rhythmdb_entry_get_string (source->priv->db, entry, RHYTHMDB_PROP_ALBUM));
		
		rhythmdb_read_unlock (source->priv->db);
	} else {
		source->priv->album = NULL;
	}
	return source->priv->album;
}


static void
rb_library_source_preferences_sync (RBLibrarySource *source)
{
	GSList *list;

	rb_debug ("syncing pref dialog state");

	list = g_slist_nth (source->priv->browser_views_group,
			    eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (list->data), TRUE);
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
	GList *l;

	for (l = rb_entry_view_get_selected_entries (source->priv->songs); l != NULL; l = g_list_next (l)) {
		rhythmdb_write_lock (source->priv->db);
		rhythmdb_entry_delete (source->priv->db, l->data);
		rhythmdb_write_unlock (source->priv->db);
	}
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
	/* save state */
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_library_source_state_prefs_sync (RBLibrarySource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	
	if (eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER))
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

	g_signal_connect (G_OBJECT (uri_widget),
			  "changed",
			  G_CALLBACK (rb_library_source_add_location_entry_changed_cb),
			  open_button);

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:
	{
		char *uri = gtk_editable_get_chars (GTK_EDITABLE (uri_widget), 0, -1);
		if (uri != NULL) {
			GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
			if (vfsuri != NULL) {
				rhythmdb_add_uri_async (source->priv->db, uri);
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
	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL)
		return FALSE;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return FALSE;
	
	rb_debug ("adding uris");

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL)
			rhythmdb_add_uri_async (source->priv->db, uri);

		g_free (uri);
	}

	g_list_free (uri_list);
	return TRUE;
}

static gboolean
impl_show_popup (RBSource *source)
{
	rb_bonobo_show_popup (GTK_WIDGET (source), LIBRARY_SOURCE_POPUP_PATH);
	return TRUE;
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

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_END);
	/* select where type="song"
	 */

	if (source->priv->search_text) {
		GPtrArray *subquery = rhythmdb_query_parse (source->priv->db,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_GENRE_FOLDED,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_DISJUNCTION,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_ARTIST_FOLDED,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_DISJUNCTION,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_ALBUM_FOLDED,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_DISJUNCTION,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_TITLE_FOLDED,
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
	gboolean is_all_query, sorting_matches;
	const char *current_sorting_type;

	/* Unlocked */
	rb_debug ("preparing to read lock for query");
	rhythmdb_read_lock (source->priv->db);

	if (source->priv->active_query) {
		rb_debug ("killing active query");
		g_signal_handlers_disconnect_by_func (G_OBJECT (source->priv->active_query),
						      G_CALLBACK (query_complete_cb),
						      source);
		rhythmdb_query_model_cancel (RHYTHMDB_QUERY_MODEL (source->priv->active_query));
	}

	is_all_query = (source->priv->selected_genres == NULL &&
			source->priv->selected_artists == NULL &&	    
			source->priv->selected_albums == NULL &&	    
			source->priv->search_text == NULL);
	current_sorting_type = rb_entry_view_get_sorting_type (source->priv->songs);
	sorting_matches = source->priv->cached_sorting_type
		&& !strcmp (source->priv->cached_sorting_type, current_sorting_type);
	rb_debug ("current sorting: %s, match: %s", current_sorting_type, sorting_matches ? "TRUE" : "FALSE" );
	if (is_all_query) {
		if (sorting_matches) {
			rb_debug ("cached query hit");
			source->priv->model = source->priv->cached_all_query;
			source->priv->active_query = NULL;
			rb_entry_view_set_model (source->priv->songs,
						 RHYTHMDB_QUERY_MODEL (source->priv->cached_all_query));
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
			rb_entry_view_poll_model (source->priv->songs);
			rhythmdb_read_unlock (source->priv->db);
			return;
		} else if (source->priv->cached_all_query) {
			rb_debug ("sorting mismatch, freeing cached query");
			g_object_unref (source->priv->cached_all_query);
			g_object_unref (source->priv->cached_genres_model);
			g_object_unref (source->priv->cached_artists_model);
			g_object_unref (source->priv->cached_albums_model);
			g_free (source->priv->cached_sorting_type);
		}
	}

	source->priv->query_type = qtype;
	rb_debug ("query type: %d", qtype);

	if (source->priv->cached_all_query == NULL
	    || (is_all_query && !sorting_matches)) {
		rb_debug ("caching new query");
		query_model = source->priv->active_query = source->priv->model = 
			source->priv->cached_all_query = rhythmdb_query_model_new_empty (source->priv->db);
		source->priv->cached_sorting_type = g_strdup (current_sorting_type);
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
		g_object_ref (G_OBJECT (source->priv->cached_all_query));
		g_object_ref (G_OBJECT (source->priv->cached_genres_model));
		g_object_ref (G_OBJECT (source->priv->cached_artists_model));
		g_object_ref (G_OBJECT (source->priv->cached_albums_model));
	} else {
		rb_debug ("query is not special");
		if (qtype < RB_LIBRARY_QUERY_TYPE_GENRE) {
			rb_debug ("resetting genres view");
			rb_property_view_reset (source->priv->genres);
			g_list_foreach (source->priv->selected_genres, (GFunc) g_free, NULL);
			g_list_free (source->priv->selected_genres);
			source->priv->selected_genres = NULL;
		}
		genre_model = rb_property_view_get_model (source->priv->genres);
		if (qtype < RB_LIBRARY_QUERY_TYPE_ARTIST) {
			rb_debug ("resetting artist view");
			rb_property_view_reset (source->priv->artists);
			g_list_foreach (source->priv->selected_artists, (GFunc) g_free, NULL);
			g_list_free (source->priv->selected_artists);
			source->priv->selected_artists = NULL;
		}
		artist_model = rb_property_view_get_model (source->priv->artists);
		if (qtype < RB_LIBRARY_QUERY_TYPE_ALBUM) {
			rb_debug ("resetting album view");
			rb_property_view_reset (source->priv->albums);
			g_list_foreach (source->priv->selected_albums, (GFunc) g_free, NULL);
			g_list_free (source->priv->selected_albums);
			source->priv->selected_albums = NULL;
		}
		album_model = rb_property_view_get_model (source->priv->albums);

		source->priv->active_query = source->priv->model = 
			query_model = rhythmdb_query_model_new_empty (source->priv->db);
		if (qtype < RB_LIBRARY_QUERY_TYPE_GENRE)
			g_object_set (G_OBJECT (genre_model), "query-model", query_model, NULL);
		else
			g_object_set (G_OBJECT (genre_model), "query-model", NULL, NULL);
	
		if (qtype < RB_LIBRARY_QUERY_TYPE_ARTIST)
			g_object_set (G_OBJECT (artist_model), "query-model", query_model, NULL);
		else
			g_object_set (G_OBJECT (artist_model), "query-model", NULL, NULL);

		if (qtype < RB_LIBRARY_QUERY_TYPE_ALBUM)
			g_object_set (G_OBJECT (album_model), "query-model", query_model, NULL);
		else
			g_object_set (G_OBJECT (album_model), "query-model", NULL, NULL);
	}

	model = GTK_TREE_MODEL (query_model);

	g_signal_connect_object (G_OBJECT (query_model),
				 "complete", G_CALLBACK (query_complete_cb),
				 source, 0);
	
	rb_debug ("setting empty model");
	rb_entry_view_set_model (source->priv->songs, RHYTHMDB_QUERY_MODEL (query_model));

	query = construct_query_from_selection (source);
	
	rb_debug ("doing query");
	rhythmdb_do_full_query_async_parsed (source->priv->db, model, query);
		
	rhythmdb_query_free (query);
	g_object_unref (G_OBJECT (query_model));

	rb_debug ("polling");
	rb_entry_view_poll_model (source->priv->songs);
	rb_debug ("done polling");
}

static void
query_complete_cb (RhythmDBQueryModel *model, RBLibrarySource *source)
{
	RhythmDBPropertyModel *genre_model = rb_property_view_get_model (source->priv->genres);
	RhythmDBPropertyModel *artist_model = rb_property_view_get_model (source->priv->artists);
	RhythmDBPropertyModel *album_model = rb_property_view_get_model (source->priv->albums);

	rb_debug ("query complete; resetting data models");

	if (source->priv->query_type >= RB_LIBRARY_QUERY_TYPE_GENRE)
		g_object_set (G_OBJECT (genre_model), "query-model", model, NULL);
	if (source->priv->query_type >= RB_LIBRARY_QUERY_TYPE_ARTIST)
		g_object_set (G_OBJECT (artist_model), "query-model", model, NULL);
	if (source->priv->query_type >= RB_LIBRARY_QUERY_TYPE_ALBUM)
		g_object_set (G_OBJECT (album_model), "query-model", model, NULL);
	if (genre_model != source->priv->cached_genres_model)
		g_object_set (G_OBJECT (source->priv->cached_genres_model),
			      "query-model", model, NULL);
	if (artist_model != source->priv->cached_artists_model)
		g_object_set (G_OBJECT (source->priv->cached_artists_model),
			      "query-model", model, NULL);
	if (album_model != source->priv->cached_albums_model)
		g_object_set (G_OBJECT (source->priv->cached_albums_model),
			      "query-model", model, NULL);
	source->priv->active_query = NULL;
}

/* static void */
/* rb_library_source_drop_cb (GtkWidget *widget, */
/* 			 GdkDragContext *context, */
/* 			 gint x, */
/* 			 gint y, */
/* 			 GtkSelectionData *data, */
/* 			 guint info, */
/* 			 guint time, */
/* 			 gpointer user_data) */
/* { */
/* 	RBLibrarySource *source = RB_LIBRARY_SOURCE (user_data); */
/* 	GtkTargetList *tlist; */
/* 	gboolean ret; */

/* 	rb_debug ("checking target"); */

/* 	tlist = gtk_target_list_new (target_uri, 1); */
/* 	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE); */
/* 	gtk_target_list_unref (tlist); */

/* 	if (ret == FALSE) */
/* 		return; */

/* 	if (!impl_receive_drag (RB_SOURCE (source), data)) { */
/* 		gtk_drag_finish (context, FALSE, FALSE, time); */
/* 		return; */
/* 	} */

/* 	gtk_drag_finish (context, TRUE, FALSE, time); */
/* } */
