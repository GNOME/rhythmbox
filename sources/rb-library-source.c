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
#include "rb-library-dnd-types.h"
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
static void genre_selected_cb (RBPropertyView *propview, const char *name,
			       RBLibrarySource *libsource);
static void artist_selected_cb (RBPropertyView *propview, const char *name,
			       RBLibrarySource *libsource);
static void album_selected_cb (RBPropertyView *propview, const char *name,
			       RBLibrarySource *libsource);

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

static void songs_view_changed_cb (RBEntryView *view, RBLibrarySource *source);

static void library_status_changed_cb (RBLibrary *library, RBLibrarySource *source);

static void rb_library_source_state_pref_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBLibrarySource *source);
static void rb_library_source_ui_pref_changed (GConfClient *client,
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


#define LIBRARY_SOURCE_SONGS_POPUP_PATH "/popups/LibrarySongsList"
#define LIBRARY_SOURCE_POPUP_PATH "/popups/LibrarySourceList"

#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_UI_LIBRARY_BROWSER_VIEWS CONF_PREFIX "/ui/library/browser_views"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/library/show_browser"

struct RBLibrarySourcePrivate
{
	RhythmDB *db;
	
	RBLibrary *library;

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

	char *search_text;
	char *selected_genre;
	char *selected_artist;
	char *selected_album;
	
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
static const GtkTargetEntry target_uri [] =
		{ { RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST } };
static const GtkTargetEntry target_id [] =
		{ { RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID } };

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
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
			
		break;
		case 1:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_hide (albums);

		break;
		case 2:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
		break;
	}
}

static void
rb_library_source_ui_prefs_sync (RBLibrarySource *source)
{
	update_browser_views_visibility (source);

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
						       GTK_ICON_SIZE_MENU,
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

	g_signal_connect (G_OBJECT (source->priv->library),
			  "status-changed",
			  G_CALLBACK (library_status_changed_cb),
			  source);

	source->priv->paned = gtk_vpaned_new ();

	source->priv->browser = gtk_hbox_new (TRUE, 5);

	/* set up genres treeview */
	source->priv->genres = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_GENRE);
	g_signal_connect (G_OBJECT (source->priv->genres),
			  "property-selected",
			  G_CALLBACK (genre_selected_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->genres));

	/* set up artist treeview */
	source->priv->artists = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ARTIST);
	g_signal_connect (G_OBJECT (source->priv->artists),
			  "property-selected",
			  G_CALLBACK (artist_selected_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->artists));

	/* set up albums treeview */
	source->priv->albums = rb_property_view_new (source->priv->db, RHYTHMDB_PROP_ALBUM);
	g_signal_connect (G_OBJECT (source->priv->albums),
			  "property-selected",
			  G_CALLBACK (album_selected_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->albums));
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), source->priv->browser, FALSE, FALSE);

	/* set up songs tree view */
	source->priv->songs = rb_entry_view_new (source->priv->db, rb_file ("rb-entry-view-library.xml"));

	g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
			  G_CALLBACK (rb_library_source_songs_show_popup_cb), source);
	g_signal_connect (G_OBJECT (source->priv->songs),
			  "changed",
			  G_CALLBACK (songs_view_changed_cb),
			  source);
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
	eel_gconf_notification_add (CONF_STATE_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
				    source);
	eel_gconf_notification_add (CONF_UI_LIBRARY_DIR,
				    (GConfClientNotifyFunc) rb_library_source_ui_pref_changed, source);
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
	case PROP_LIBRARY:
		source->priv->library = g_value_get_object (value);
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
	case PROP_LIBRARY:
		g_value_set_object (value, source->priv->library);
		break;
	case PROP_DB:
		g_value_set_object (value, source->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_library_source_new (RhythmDB *db, RBLibrary *library)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_LIBRARY_SOURCE,
					  "name", _("Library"),
					  "db", db,
					  "library", library,
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

static void
genre_selected_cb (RBPropertyView *propview, const char *name,
		   RBLibrarySource *libsource)
{
	rb_debug ("genre selected"); 
	g_free (libsource->priv->selected_genre);
	libsource->priv->selected_genre = g_strdup (name);
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_GENRE);
}

static void
artist_selected_cb (RBPropertyView *propview, const char *name,
		   RBLibrarySource *libsource)
{
	rb_debug ("artist selected"); 
	g_free (libsource->priv->selected_artist);
	libsource->priv->selected_artist = g_strdup (name);
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ARTIST);
}

static void
album_selected_cb (RBPropertyView *propview, const char *name,
		   RBLibrarySource *libsource)
{
	rb_debug ("album selected"); 
	g_free (libsource->priv->selected_album);
	libsource->priv->selected_album = g_strdup (name);
	rb_library_source_do_query (libsource, RB_LIBRARY_QUERY_TYPE_ALBUM);
}

static const char *
impl_get_status (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	g_free (source->priv->status);
	/* RHYTHMDB FIXME */
/* 	source->priv->status = rb_library_compute_status (source->priv->library, */
/* 							  rb_library_get_all_songs (source->priv->library), */
/* 							  source->priv->songs_filter); */
/* 	return source->priv->status; */
	return "Not implemented yet";
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
	source->priv->search_text = g_utf8_casefold (search_text, -1);
	rb_library_source_do_query (source, RB_LIBRARY_QUERY_TYPE_SEARCH);
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
	const char *ret;

	entry = rb_entry_view_get_playing_entry (source->priv->songs);

	if (entry != NULL) {
		rhythmdb_read_lock (source->priv->db);
		
		ret = rhythmdb_entry_get_string (source->priv->db, entry, RHYTHMDB_PROP_ARTIST);

		rhythmdb_read_unlock (source->priv->db);

		return ret;
	} else {
		return NULL;
	}
}

static const char *
impl_get_album (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RhythmDBEntry *entry;
	const char *ret;

	entry = rb_entry_view_get_playing_entry (source->priv->songs);

	if (entry != NULL) {
		rhythmdb_read_lock (source->priv->db);

		ret = rhythmdb_entry_get_string (source->priv->db, entry, RHYTHMDB_PROP_ALBUM);
		
		rhythmdb_read_unlock (source->priv->db);

		return ret;
	} else {
		return NULL;
	}
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

	for (l = rb_entry_view_get_selected_entries (source->priv->songs); l != NULL; l = g_list_next (l))
		rhythmdb_entry_unref (source->priv->db, l->data);
}

static void
impl_song_properties (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
/* 	GtkWidget *song_info = NULL; */

	g_return_if_fail (source->priv->songs != NULL);

	/* RHYTHMDB FIXME */
/* 	song_info = rb_song_info_new (source->priv->songs); */
/* 	if (song_info) */
/* 		gtk_widget_show_all (song_info); */
/* 	else */
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
library_status_changed_cb (RBLibrary *library, RBLibrarySource *source)
{
	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
songs_view_changed_cb (RBEntryView *view, RBLibrarySource *source)
{
/* 	rb_debug ("got node view change"); */
/* 	if (source->priv->filter_changed) */
/* 		rb_source_notify_status_changed (RB_SOURCE (source)); */
/* 	source->priv->filter_changed = FALSE; */
}

void
rb_library_source_add_location (RBLibrarySource *source, GtkWindow *win)
{
	GladeXML *xml = rb_glade_xml_new ("uri.glade",
					  "open_uri_dialog_content",
					  source);
	GtkWidget *content, *uri_widget;
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Add Location"),
							 win,
							 GTK_DIALOG_MODAL,
							 GTK_STOCK_CANCEL,
							 GTK_RESPONSE_CANCEL,
							 GTK_STOCK_OPEN,
							 GTK_RESPONSE_OK,
							 NULL);

	g_return_if_fail (dialog != NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);

	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	content = glade_xml_get_widget (xml, "open_uri_dialog_content");

	g_return_if_fail (content != NULL);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    content, FALSE, FALSE, 0);

	uri_widget = glade_xml_get_widget (xml, "uri");

	g_return_if_fail (uri_widget != NULL);

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:
	{
		char *uri = gtk_editable_get_chars (GTK_EDITABLE (uri_widget), 0, -1);
		if (uri != NULL) {
			GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
			if (vfsuri != NULL) {
				rb_library_add_uri_async (source->priv->library, uri);
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
			rb_library_add_uri_async (source->priv->library, uri);

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
entry_added_cb (RBEntryView *view, RhythmDBEntry *entry,
		RBPropertyView *propview)
{
	rb_property_view_handle_entry_addition (propview, entry);
}

static void
rb_library_source_do_query (RBLibrarySource *source, RBLibraryQueryType qtype)
{
	RhythmDBQueryModel *query_model;
	GtkTreeModel *model;
	GPtrArray *query;
	GPtrArray *genre_query;
	GPtrArray *artist_query;
	GPtrArray *album_query;

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_END);

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
	}

	g_signal_handlers_disconnect_matched (G_OBJECT (source->priv->songs),
					      G_SIGNAL_MATCH_FUNC,
					      g_signal_lookup ("entry-added",
							       RB_TYPE_ENTRY_VIEW),
					      0,
					      NULL,
					      G_CALLBACK (entry_added_cb),
					      NULL);


	if (qtype < RB_LIBRARY_QUERY_TYPE_GENRE) {
		rb_property_view_reset (source->priv->genres);
		g_free (source->priv->selected_genre);
		source->priv->selected_genre = NULL;
		g_signal_connect (G_OBJECT (source->priv->songs),
				  "entry-added", G_CALLBACK (entry_added_cb),
				  source->priv->genres);
	}
	if (qtype < RB_LIBRARY_QUERY_TYPE_ARTIST) {
		rb_property_view_reset (source->priv->artists);
		g_free (source->priv->selected_artist);
		source->priv->selected_artist = NULL;
		g_signal_connect (G_OBJECT (source->priv->songs),
				  "entry-added", G_CALLBACK (entry_added_cb),
				  source->priv->artists);
	}
	if (qtype < RB_LIBRARY_QUERY_TYPE_ALBUM) {
		rb_property_view_reset (source->priv->albums);
		g_free (source->priv->selected_album);
		source->priv->selected_album = NULL;
		g_signal_connect (G_OBJECT (source->priv->songs),
				  "entry-added", G_CALLBACK (entry_added_cb),
				  source->priv->albums);
	}

	genre_query = rhythmdb_query_copy (query);

	if (source->priv->selected_genre)
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       RHYTHMDB_PROP_GENRE,
				       source->priv->selected_genre,
				       RHYTHMDB_QUERY_END);
	artist_query = rhythmdb_query_copy (query);

	if (source->priv->selected_artist)
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       RHYTHMDB_PROP_ARTIST,
				       source->priv->selected_artist,
				       RHYTHMDB_QUERY_END);
	album_query = rhythmdb_query_copy (query);	

	if (source->priv->selected_album)
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       RHYTHMDB_PROP_ALBUM,
				       source->priv->selected_album,
				       RHYTHMDB_QUERY_END);

	query_model = rhythmdb_query_model_new_empty (source->priv->db);
	model = GTK_TREE_MODEL (query_model);

	g_signal_connect (G_OBJECT (query_model),
			  "complete", G_CALLBACK (query_complete_cb),
			  source);

	rhythmdb_do_full_query_async_parsed (source->priv->db, model, query);

	rhythmdb_query_free (genre_query);
	rhythmdb_query_free (artist_query);
	rhythmdb_query_free (album_query);
	rhythmdb_query_free (query);
	
	rb_entry_view_set_query_model (source->priv->songs, query_model);
}

static void
query_complete_cb (RhythmDBQueryModel *model, RBLibrarySource *source)
{
	rb_debug ("query complete");
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
