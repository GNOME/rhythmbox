/* 
 *  arch-tag: Implementation of Internet Radio source object
 *
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <string.h>

#include "rb-iradio-source.h"

#include "rhythmdb-legacy.h"
#include "rb-bonobo-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-station-properties-dialog.h"
#include "rb-volume.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"

typedef enum
{
	RB_IRADIO_QUERY_TYPE_ALL,
	RB_IRADIO_QUERY_TYPE_GENRE,
	RB_IRADIO_QUERY_TYPE_SEARCH,
} RBIRadioQueryType;

static void rb_iradio_source_class_init (RBIRadioSourceClass *klass);
static void rb_iradio_source_init (RBIRadioSource *source);
static GObject *rb_iradio_source_constructor (GType type, guint n_construct_properties,
					      GObjectConstructParam *construct_properties);
static void rb_iradio_source_finalize (GObject *object);
static void rb_iradio_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_source_songs_show_popup_cb (RBEntryView *view,
						  RhythmDBEntry *entry,
						  RBIRadioSource *source);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBIRadioSource *source);
static void rb_iradio_source_state_pref_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBIRadioSource *source);
static void rb_iradio_source_show_browser (RBIRadioSource *source,
					   gboolean show);
static void rb_iradio_source_state_prefs_sync (RBIRadioSource *source);
static void genre_selected_cb (RBPropertyView *propview, const char *name,
			       RBIRadioSource *iradio_source);

/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_delete (RBSource *source);
static void impl_song_properties (RBSource *source);
static RBSourceEOFType impl_handle_eos (RBSource *asource);
static void impl_buffering_done (RBSource *asource);
static void rb_iradio_source_do_query (RBIRadioSource *source, RBIRadioQueryType type);

void rb_iradio_source_show_columns_changed_cb (GtkToggleButton *button,
					     RBIRadioSource *source);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_STATION "/commands/CurrentStation"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"
#define IRADIO_SOURCE_SONGS_POPUP_PATH "/popups/IRadioSongsList"

#define CONF_UI_IRADIO_DIR CONF_PREFIX "/ui/iradio"
#define CONF_UI_IRADIO_COLUMNS_SETUP CONF_PREFIX "/ui/iradio/columns_setup"
#define CONF_STATE_IRADIO_DIR CONF_PREFIX "/state/iradio"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/iradio/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/iradio/show_browser"

struct RBIRadioSourcePrivate
{
	RhythmDB *db;

	GtkWidget *vbox;

	GdkPixbuf *pixbuf;

	RBPropertyView *genres;
	RBEntryView *stations;

	gboolean async_playcount_update_queued;
	guint async_playcount_update_id;

	char *title, *url, *song, *name;
	int num_genres, num_stations;

	gboolean shuffle;
	gboolean repeat;

	char *status;

	GtkWidget *paned;

	gboolean lock;

	gboolean loading_prefs;

	char *search_text;
	char *selected_genre;

	GtkWidget *genre_check;
	GtkWidget *rating_check;
	GtkWidget *play_count_check;
	GtkWidget *last_played_check;
	GtkWidget *quality_check;

	guint async_idlenum;
	gboolean async_entry_destroyed;
	guint async_signum;
	RhythmDB *async_update_entry;
};

enum
{
	PROP_0,
	PROP_DB,
};

static GObjectClass *parent_class = NULL;

GType
rb_iradio_source_get_type (void)
{
	static GType rb_iradio_source_type = 0;

	if (rb_iradio_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_iradio_source_class_init,
			NULL,
			NULL,
			sizeof (RBIRadioSource),
			0,
			(GInstanceInitFunc) rb_iradio_source_init
		};

		rb_iradio_source_type = g_type_register_static (RB_TYPE_SOURCE,
							      "RBIRadioSource",
							      &our_info, 0);
		
	}

	return rb_iradio_source_type;
}

static void
rb_iradio_source_class_init (RBIRadioSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_iradio_source_finalize;
	object_class->constructor = rb_iradio_source_constructor;

	object_class->set_property = rb_iradio_source_set_property;
	object_class->get_property = rb_iradio_source_get_property;

	source_class->impl_get_status  = impl_get_status;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_buffering_done = impl_buffering_done;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
}

static void
rb_iradio_source_init (RBIRadioSource *source)
{
	GtkWidget *dummy = gtk_tree_view_new ();
	source->priv = g_new0 (RBIRadioSourcePrivate, 1);

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
	
	source->priv->song = g_strdup (_("Unknown"));
	source->priv->name = g_strdup (_("Unknown"));
	source->priv->title = g_strdup (_("Unknown"));

	source->priv->pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_IRADIO,
						       GTK_ICON_SIZE_MENU,
						       NULL);
	gtk_widget_destroy (dummy);
}

static void
rb_iradio_source_finalize (GObject *object)
{
	RBIRadioSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_SOURCE (object));

	source = RB_IRADIO_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing iradio source");

	g_free (source->priv->name);
	g_free (source->priv->song);
	g_free (source->priv->title);
	g_free (source->priv->status);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject *
rb_iradio_source_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBIRadioSource *source;
	RBIRadioSourceClass *klass;
	GObjectClass *parent_class;  
	klass = RB_IRADIO_SOURCE_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	source = RB_IRADIO_SOURCE (parent_class->constructor (type, n_construct_properties,
							      construct_properties));

	source->priv->paned = gtk_hpaned_new ();

	/* set up genre entry view */
	source->priv->genres = rb_property_view_new (source->priv->db,
						     RHYTHMDB_PROP_GENRE);
	g_signal_connect (G_OBJECT (source->priv->genres),
			  "property-selected",
			  G_CALLBACK (genre_selected_cb),
			  source);

	g_object_set (G_OBJECT (source->priv->genres), "vscrollbar_policy",
		      GTK_POLICY_AUTOMATIC, NULL);
	g_object_ref (G_OBJECT (source->priv->genres));

	/* set up stations view */
	source->priv->stations = rb_entry_view_new (source->priv->db,
						    rb_file ("rb-entry-view-iradio.xml"));
	g_signal_connect (G_OBJECT (source->priv->stations),
			  "size_allocate",
			  G_CALLBACK (paned_size_allocate_cb),
			  source);
	g_signal_connect (G_OBJECT (source->priv->stations), "show_popup",
			  G_CALLBACK (rb_iradio_source_songs_show_popup_cb), source);

	gtk_paned_pack2 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->stations), TRUE, FALSE);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

	rb_iradio_source_state_prefs_sync (source);
	eel_gconf_notification_add (CONF_STATE_IRADIO_DIR,
				    (GConfClientNotifyFunc) rb_iradio_source_state_pref_changed,
				    source);
	rb_iradio_source_do_query (source, RB_IRADIO_QUERY_TYPE_ALL);
			
	gtk_widget_show_all (GTK_WIDGET (source));
	return G_OBJECT (source);
}

static void
rb_iradio_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (object);

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
rb_iradio_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (object);

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
rb_iradio_source_new (RhythmDB *db)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_IRADIO_SOURCE,
					  "name", _("Radio"),
					  "db", db,
					  NULL));

	return source;
}

void
rb_iradio_source_add_station (RBIRadioSource *source,
			      const char *uri, const char *title, const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = { 0, };

	rhythmdb_write_lock (source->priv->db);

	entry = rhythmdb_entry_new (source->priv->db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION, uri);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, title);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_reset (&val);
	g_value_set_string (&val, genre);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);
		
	rhythmdb_write_unlock (source->priv->db);
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	return source->priv->pixbuf;
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	g_free (source->priv->search_text);
	source->priv->search_text = g_utf8_casefold (search_text, -1);
	rb_iradio_source_do_query (source, RB_IRADIO_QUERY_TYPE_SEARCH);
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	return source->priv->stations;
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_ERROR;
}

struct RBIRadioAsyncPlayStatisticsData
{
	RBIRadioSource *source;
	RhythmDBEntry *entry;
};

static gboolean
rb_iradio_source_async_update_play_statistics (gpointer data)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (data);
	RhythmDBEntry *playing_entry;

	rb_debug ("entering async handler");

	GDK_THREADS_ENTER ();

	playing_entry = rb_entry_view_get_playing_entry (source->priv->stations);
	rb_debug ("async updating play statistics, entry: %p playing entry: %p",
		  source->priv->async_update_entry, playing_entry);
	if (source->priv->async_update_entry == playing_entry)
		rb_source_update_play_statistics (RB_SOURCE (source),
						  source->priv->db,
						  source->priv->async_update_entry);
	rhythmdb_entry_unref (source->priv->db, source->priv->async_update_entry);
	source->priv->async_update_entry = NULL;
		
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
impl_buffering_done (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	RhythmDBEntry *entry = rb_entry_view_get_playing_entry (source->priv->stations);

	rb_debug ("queueing async play statistics update, entry: %p", entry);

	if (source->priv->async_update_entry != NULL) {
		rb_debug ("async handler already queued, removing");
		rhythmdb_entry_unref (source->priv->db, source->priv->async_update_entry);
		g_source_remove (source->priv->async_idlenum);
	}

	source->priv->async_entry_destroyed = FALSE;
	source->priv->async_update_entry = entry;
	rhythmdb_entry_ref (source->priv->db, source->priv->async_update_entry);
	source->priv->async_idlenum =
		g_timeout_add (6000, (GSourceFunc) rb_iradio_source_async_update_play_statistics,
			       source);
}

static const char *
impl_get_status (RBSource *asource)
{
 	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	char *ret;
	ret = g_strdup_printf (_("<b>%d</b> total stations in <b>%d</b> distinct genres"),
			       rb_entry_view_get_num_entries (source->priv->stations),
			       rb_property_view_get_num_properties (source->priv->genres));
	return ret;
}

static const char *
impl_get_browser_key (RBSource *asource)
{
	return CONF_STATE_SHOW_BROWSER;
}

static void
impl_delete (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GList *l;

	for (l = rb_entry_view_get_selected_entries (source->priv->stations); l != NULL;
	     l = g_list_next (l))
		rhythmdb_entry_unref (source->priv->db, l->data);
}

static void
impl_song_properties (RBSource *asource)
{
/* 	RBIRadioSource *source = RB_IRADIO_SOURCE (asource); */
/* 	GtkWidget *dialog = rb_station_properties_dialog_new (source->priv->stations, source->priv->db); */
/* 	rb_debug ("in song properties"); */
/* 	if (dialog) */
/* 		gtk_widget_show_all (dialog); */
/* 	else */
/* 		rb_debug ("no selection!"); */
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBIRadioSource *source)
{
	/* save state */
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_iradio_source_state_prefs_sync (RBIRadioSource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	rb_iradio_source_show_browser (source,
				       eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER));
}

static void
rb_iradio_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBIRadioSource *source)
{
	rb_debug ("state prefs changed");
	rb_iradio_source_state_prefs_sync (source);
}


static void
rb_iradio_source_songs_show_popup_cb (RBEntryView *view,
				      RhythmDBEntry *entry,
				      RBIRadioSource *source)
{
	GtkWidget *menu;
	GtkWidget *window;

	window = gtk_widget_get_ancestor (GTK_WIDGET (view),
					  BONOBO_TYPE_WINDOW);

	menu = gtk_menu_new ();

	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu),
			         IRADIO_SOURCE_SONGS_POPUP_PATH);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static void
genre_selected_cb (RBPropertyView *propview, const char *name,
		   RBIRadioSource *iradio_source)
{
	g_free (iradio_source->priv->selected_genre);
	iradio_source->priv->selected_genre = g_strdup (name);
	rb_iradio_source_do_query (iradio_source, RB_IRADIO_QUERY_TYPE_GENRE);
}

static void
rb_iradio_source_show_browser (RBIRadioSource *source,
			       gboolean show)
{
	GtkWidget *genreswidget = GTK_WIDGET (source->priv->genres);

	if (show == TRUE) {
		gtk_paned_pack1 (GTK_PANED (source->priv->paned), genreswidget, FALSE, FALSE);
		gtk_widget_show_all (genreswidget);
	} else if (show == FALSE) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (source->priv->paned));
		gtk_widget_hide (genreswidget);
		if (g_list_find (children, genreswidget))
		    gtk_container_remove (GTK_CONTAINER (source->priv->paned), genreswidget);
		g_list_free (children);
	}
}

static void
entry_added_cb (RBEntryView *view, RhythmDBEntry *entry,
		RBPropertyView *propview)
{
	rb_property_view_handle_entry_addition (propview, entry);
}

static void
rb_iradio_source_do_query (RBIRadioSource *source, RBIRadioQueryType qtype)
{
	RhythmDBQueryModel *query_model;
	GtkTreeModel *model;
	GPtrArray *genre_query;
	GPtrArray *query;

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_IRADIO_STATION,
				      RHYTHMDB_QUERY_END);

	if (source->priv->search_text) {
		GPtrArray *subquery = rhythmdb_query_parse (source->priv->db,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_GENRE,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_END);
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY,
				       subquery,
				       RHYTHMDB_QUERY_END);
	}

	g_signal_handlers_disconnect_matched (G_OBJECT (source->priv->stations),
					      G_SIGNAL_MATCH_FUNC,
					      g_signal_lookup ("entry-added",
							       RB_TYPE_ENTRY_VIEW),
					      0,
					      NULL,
					      G_CALLBACK (entry_added_cb),
					      NULL);

	if (qtype < RB_IRADIO_QUERY_TYPE_ALL) {
		rb_property_view_reset (source->priv->genres);
		g_free (source->priv->selected_genre);
		source->priv->selected_genre = NULL;
		g_signal_connect (G_OBJECT (source->priv->stations),
				  "entry-added", G_CALLBACK (entry_added_cb),
				  source->priv->genres);
	}

	genre_query = rhythmdb_query_copy (query);

	if (source->priv->selected_genre)
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       RHYTHMDB_PROP_GENRE,
				       source->priv->selected_genre,
				       RHYTHMDB_QUERY_END);

	query_model = rhythmdb_query_model_new_empty (source->priv->db);
	model = GTK_TREE_MODEL (query_model);

	rhythmdb_read_lock (source->priv->db);

	rhythmdb_do_full_query_parsed (source->priv->db, model, query);

	rhythmdb_read_unlock (source->priv->db);

	rhythmdb_query_free (genre_query);
	rhythmdb_query_free (query);
	
	rb_entry_view_set_query_model (source->priv->stations, query_model);
}

void
rb_iradio_source_load_legacy (RhythmDB *db)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	char *libname = g_build_filename (rb_dot_dir (), "iradio-2.2.xml", NULL);

	if (!g_file_test (libname, G_FILE_TEST_EXISTS)) {
		g_free (libname);
		return;
	}
	
	doc = xmlParseFile (libname);
	g_free (libname);

	if (doc == NULL)
		return;

	root = xmlDocGetRootElement (doc);
	for (child = root->children; child != NULL; child = child->next) {
		rhythmdb_legacy_parse_rbnode (db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION,
					      child);
	}
	xmlFreeDoc (doc);
}

