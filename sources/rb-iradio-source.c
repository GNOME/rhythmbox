/* 
 *  arch-tag: Implementation of Internet Radio source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#include <config.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <string.h>

#include "rb-iradio-source.h"

#include "rb-bonobo-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-station-properties-dialog.h"
#include "rb-volume.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-node-filter.h"

static void rb_iradio_source_class_init (RBIRadioSourceClass *klass);
static void rb_iradio_source_init (RBIRadioSource *source);
static void rb_iradio_source_finalize (GObject *object);
static void rb_iradio_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void genre_node_selected_cb (RBNodeView *view,
				     RBNode *node,
				     RBIRadioSource *source);
static void rb_iradio_source_songs_show_popup_cb (RBNodeView *view,
						RBNode *node,
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

/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBNodeView *impl_get_node_view (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_delete (RBSource *source);
static void impl_song_properties (RBSource *source);
static RBSourceEOFType impl_handle_eos (RBSource *asource);
static void impl_buffering_done (RBSource *asource);

static void stations_filter (RBIRadioSource *source,
			     RBNode *genre);
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
	RBIRadioBackend *backend;

	GtkWidget *vbox;

	GdkPixbuf *pixbuf;

	RBNodeView *genres;
	RBNodeView *stations;

	gboolean async_playcount_update_queued;
	guint async_playcount_update_id;

	char *title, *url, *song, *name;
	int num_genres, num_stations;

	gboolean shuffle;
	gboolean repeat;

	char *status;

	GtkWidget *paned;

	gboolean lock;

	gboolean changing_genre;
	RBNodeFilter *stations_filter;

	gboolean loading_prefs;

	GtkWidget *genre_check;
	GtkWidget *rating_check;
	GtkWidget *play_count_check;
	GtkWidget *last_played_check;
	GtkWidget *quality_check;

	guint async_idlenum;
	gboolean async_node_destroyed;
	guint async_signum;
	RBNode *async_update_node;
};

enum
{
	PROP_0,
	PROP_BACKEND
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

	object_class->set_property = rb_iradio_source_set_property;
	object_class->get_property = rb_iradio_source_get_property;

	source_class->impl_get_status  = impl_get_status;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_node_view = impl_get_node_view;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_buffering_done = impl_buffering_done;

	g_object_class_install_property (object_class,
					 PROP_BACKEND,
					 g_param_spec_object ("backend",
							      "Backend",
							      "IRadio Backend",
							      RB_TYPE_IRADIO_BACKEND,
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

	source->priv->changing_genre = FALSE;

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

	g_object_unref (G_OBJECT (source->priv->stations_filter));

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
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
	case PROP_BACKEND:
	{
		source->priv->backend = g_value_get_object (value);

		source->priv->paned = gtk_hpaned_new ();

		/* Initialize the filters */
		source->priv->stations_filter = rb_node_filter_new ();

		/* set up genre node view */
		source->priv->genres = rb_node_view_new (rb_iradio_backend_get_all_genres (source->priv->backend),
						       rb_file ("rb-node-view-iradio-genres.xml"),
						       NULL);

		g_object_set (G_OBJECT (source->priv->genres), "vscrollbar_policy", GTK_POLICY_AUTOMATIC, NULL);
		g_object_ref (G_OBJECT (source->priv->genres));

		g_signal_connect (G_OBJECT (source->priv->genres),
				  "node_selected",
				  G_CALLBACK (genre_node_selected_cb),
				  source);

		/* set up stations tree view */
		source->priv->stations = rb_node_view_new (rb_iradio_backend_get_all_stations (source->priv->backend),
							 rb_file ("rb-node-view-iradio-stations.xml"),
							 source->priv->stations_filter);
		g_signal_connect (G_OBJECT (source->priv->stations),
				  "size_allocate",
				  G_CALLBACK (paned_size_allocate_cb),
				  source);
		g_signal_connect (G_OBJECT (source->priv->stations), "show_popup",
				  G_CALLBACK (rb_iradio_source_songs_show_popup_cb), source);

		gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->stations), TRUE, FALSE);

		gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

		rb_iradio_source_state_prefs_sync (source);
		eel_gconf_notification_add (CONF_STATE_IRADIO_DIR,
					    (GConfClientNotifyFunc) rb_iradio_source_state_pref_changed,
					    source);
			
		gtk_widget_show_all (GTK_WIDGET (source));
		break;
	}
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
	case PROP_BACKEND:
		g_value_set_object (value, source->priv->backend);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_iradio_source_new (RBIRadioBackend *backend)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_IRADIO_SOURCE,
					  "name", _("Radio"),
					  "backend", backend,
					  NULL));

	return source;
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

	/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0) {
		rb_node_view_select_node (source->priv->genres,
		 		          rb_iradio_backend_get_all_stations (source->priv->backend));
	} else {
		rb_node_view_select_none (source->priv->genres);

		rb_node_filter_empty (source->priv->stations_filter);
		rb_node_filter_add_expression (source->priv->stations_filter,
					       rb_node_filter_expression_new (source->priv->stations_filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_done_changing (source->priv->stations_filter);
	}
}

static RBNodeView *
impl_get_node_view (RBSource *asource)
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
	RBNode *node;
};

static void
async_node_update_destroyed_cb (RBNode *node, gpointer data)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (data);
	rb_debug ("node to be updated was destroyed");

	source->priv->async_node_destroyed = TRUE;
}

static gboolean
rb_iradio_source_async_update_play_statistics (gpointer data)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (data);
	RBNode *playing_node;

	rb_debug ("entering async handler");

	gdk_threads_enter ();


	if (!source->priv->async_node_destroyed) {
		playing_node = rb_node_view_get_playing_node (source->priv->stations);
		rb_debug ("async updating play statistics, node: %p playing node: %p",
			  source->priv->async_update_node, playing_node);
		if (source->priv->async_update_node == playing_node)
			rb_node_update_play_statistics (source->priv->async_update_node);

		rb_node_signal_disconnect (source->priv->async_update_node,
					   source->priv->async_signum);
	} else {
		rb_debug ("async node destroyed");
	}
	source->priv->async_update_node = NULL;
		
	gdk_threads_leave ();
	return FALSE;
}

static void
impl_buffering_done (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	RBNode *node = rb_node_view_get_playing_node (source->priv->stations);

	rb_debug ("queueing async play statistics update, node: %p", node);

	if (source->priv->async_update_node != NULL) {
		rb_debug ("async handler already queued, removing");
		rb_node_signal_disconnect (source->priv->async_update_node,
					   source->priv->async_signum);
		g_source_remove (source->priv->async_idlenum);
	}

	source->priv->async_node_destroyed = FALSE;
	source->priv->async_update_node = node;
 	source->priv->async_signum =
		rb_node_signal_connect_object (node, RB_NODE_DESTROY,
					       (RBNodeCallback) async_node_update_destroyed_cb,
					       G_OBJECT (source));
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
			       rb_iradio_backend_get_station_count (source->priv->backend),
			       rb_iradio_backend_get_genre_count (source->priv->backend));
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

	for (l = rb_node_view_get_selection (source->priv->stations); l != NULL; l = g_list_next (l))
		rb_iradio_backend_remove_node (source->priv->backend, l->data);
}

static void
impl_song_properties (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GtkWidget *dialog = rb_station_properties_dialog_new (source->priv->stations,
							      source->priv->backend);
	rb_debug ("in song properties");
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");
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
rb_iradio_source_songs_show_popup_cb (RBNodeView *view,
				      RBNode *node,
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

static RBNode *
ensure_node_selection (RBNodeView *view,
		       RBNode *all_node,
		       gboolean *changing_flag)
{
	GList *selection = rb_node_view_get_selection (view);

	if (selection == NULL) {
		*changing_flag = TRUE;
		rb_node_view_select_node (view, all_node);
		*changing_flag = FALSE;
		selection = rb_node_view_get_selection (view);
	}

	return selection->data;
}

static void
genre_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBIRadioSource *source)
{
	RBNode *genre = NULL;
	if (source->priv->changing_genre == TRUE)
		return;

	genre = ensure_node_selection (view,
				       rb_iradio_backend_get_all_genres(source->priv->backend),
				       &source->priv->changing_genre);

	rb_source_notify_filter_changed (RB_SOURCE (source));
	stations_filter (source, genre);
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
stations_filter (RBIRadioSource *source,
		 RBNode *parent)
{
	rb_node_filter_empty (source->priv->stations_filter);
	rb_node_filter_add_expression (source->priv->stations_filter,
				       rb_node_filter_expression_new (source->priv->stations_filter,
								      RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_iradio_backend_get_all_stations (source->priv->backend)),
				       0);
	rb_node_filter_add_expression (source->priv->stations_filter,
				       rb_node_filter_expression_new (source->priv->stations_filter,
								      RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      parent),
				       0);
	rb_node_filter_done_changing (source->priv->stations_filter);
}
