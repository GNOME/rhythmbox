/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002 Colin Walters <walters@debian.org>
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
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <bonobo/bonobo-ui-component.h>
#include <string.h>

#include "rb-bonobo-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-iradio-view.h"
#include "rb-new-station-dialog.h"
#include "rb-station-properties-dialog.h"
#include "rb-volume.h"
#include "rb-node-station.h"
#include "rb-glist-wrapper.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-node-filter.h"
#include "rb-search-entry.h"

static void rb_iradio_view_class_init (RBIRadioViewClass *klass);
static void rb_iradio_view_init (RBIRadioView *view);
static void rb_iradio_view_finalize (GObject *object);
static void rb_iradio_view_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_view_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void genre_node_selected_cb (RBNodeView *view,
				     RBNode *node,
				     RBIRadioView *libview);
static void genre_activated_cb (RBNodeView *view,
				RBNode *node,
				RBIRadioView *iradio_view);
static void rb_iradio_view_player_init (RBViewPlayerIface *iface);
static void rb_iradio_view_set_shuffle (RBViewPlayer *player,
			                 gboolean shuffle);
static void rb_iradio_view_set_repeat (RBViewPlayer *player,
			                gboolean repeat);
static gboolean rb_iradio_view_can_pause (RBViewPlayer *player);
static RBViewPlayerResult rb_iradio_view_have_first (RBViewPlayer *player);
static RBViewPlayerResult rb_iradio_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_iradio_view_have_previous (RBViewPlayer *player);
static void rb_iradio_view_next (RBViewPlayer *player);
static void rb_iradio_view_previous (RBViewPlayer *player);
static const char *rb_iradio_view_get_title (RBViewPlayer *player);
static RBViewPlayerResult rb_iradio_view_have_artist_album (RBViewPlayer *player);
static const char *rb_iradio_view_get_artist (RBViewPlayer *player);
static const char *rb_iradio_view_get_album (RBViewPlayer *player);
static RBViewPlayerResult rb_iradio_view_have_url (RBViewPlayer *player);
static void rb_iradio_view_get_url (RBViewPlayer *player, char **text, char **link);
static const char *rb_iradio_view_get_song (RBViewPlayer *player);
static long rb_iradio_view_get_duration (RBViewPlayer *player);
static GdkPixbuf *rb_iradio_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_iradio_view_get_stream (RBViewPlayer *player);
static void rb_iradio_view_start_playing (RBViewPlayer *player);
static void rb_iradio_view_stop_playing (RBViewPlayer *player);
static void rb_iradio_view_set_playing_node (RBIRadioView *view,
			                      RBNode *node);
static void station_activated_cb (RBNodeView *view,
				  RBNode *node,
				  RBIRadioView *iradio_view);
static void node_view_changed_cb (RBNodeView *view,
		                  RBIRadioView *iradio_view);
static void station_eos_cb (MonkeyMediaStream *stream,
			    RBIRadioView *view);
static RBNode *rb_iradio_view_get_first_node (RBIRadioView *view);
static RBNode *rb_iradio_view_get_previous_node (RBIRadioView *view);
static RBNode *rb_iradio_view_get_next_node (RBIRadioView *view);
static void rb_iradio_view_status_init (RBViewStatusIface *iface);
static const char *rb_iradio_view_status_get (RBViewStatus *status);
static void rb_iradio_view_clipboard_init (RBViewClipboardIface *iface);
static gboolean rb_iradio_view_can_cut (RBViewClipboard *clipboard);
static gboolean rb_iradio_view_can_copy (RBViewClipboard *clipboard);
static gboolean rb_iradio_view_can_paste (RBViewClipboard *clipboard);
static gboolean rb_iradio_view_can_delete (RBViewClipboard *clipboard);
static GList *rb_iradio_view_cut (RBViewClipboard *clipboard);
static GList *rb_iradio_view_copy (RBViewClipboard *clipboard);
static void rb_iradio_view_paste (RBViewClipboard *clipboard,
		                   GList *nodes);
static void rb_iradio_view_delete (RBViewClipboard *clipboard);
static void rb_iradio_view_song_info (RBViewClipboard *clipboard);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBIRadioView *view);
static void rb_iradio_view_cmd_select_all (BonoboUIComponent *component,
				            RBIRadioView *view,
				            const char *verbname);
static void rb_iradio_view_cmd_select_none (BonoboUIComponent *component,
				             RBIRadioView *view,
				             const char *verbname);
static void rb_iradio_view_cmd_current_station (BonoboUIComponent *component,
						RBIRadioView *view,
						const char *verbname);
static void rb_iradio_view_cmd_new_station (BonoboUIComponent *component,
					    RBIRadioView *view,
					    const char *verbname);
static void rb_iradio_view_show_browser_changed_cb (BonoboUIComponent *component,
					             const char *path,
					             Bonobo_UIComponent_EventType type,
					             const char *state,
					             RBIRadioView *view);
static void rb_iradio_view_show_browser (RBIRadioView *view, gboolean show);
static const char *impl_get_description (RBView *view);
static GList *impl_get_selection (RBView *view);
static void rb_iradio_view_node_removed_cb (RBNode *node,
					     RBIRadioView *view);
static GtkWidget *rb_iradio_view_get_extra_widget (RBView *base_view);
static void rb_iradio_view_search_cb (RBSearchEntry *search,
			               const char *search_text,
			               RBIRadioView *view);
static void stations_filter (RBIRadioView *view,
			     RBNode *genre);
void info_available_cb(MonkeyMediaStream *stream,
		       MonkeyMediaStreamInfoField field,
		       GValue *value,
		       gpointer data);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_STATION "/commands/CurrentStation"
#define CMD_PATH_STATION_INFO    "/commands/StationInfo"

#define CONF_STATE_PANED_POSITION "/apps/rhythmbox/state/iradio/paned_position"
#define CONF_STATE_SHOW_BROWSER   "/apps/rhythmbox/state/iradio/show_browser"

struct RBIRadioViewPrivate
{
	RBIRadioBackend *backend;

	GtkWidget *vbox;

	RBNodeView *genres;
	RBNodeView *stations;

	MonkeyMediaAudioStream *playing_stream;

	char *title, *url, *song, *name;
	int num_genres, num_stations;

	gboolean shuffle;
	gboolean repeat;

	RBSearchEntry *search;

	char *status;

	GtkWidget *paned;
	int paned_position;

	gboolean show_browser;
	gboolean lock;

	RBNodeFilter *stations_filter;
};

enum
{
	PROP_0,
	PROP_BACKEND
};

static BonoboUIVerb rb_iradio_view_verbs[] = 
{
	BONOBO_UI_VERB ("SelectAll",   (BonoboUIVerbFn) rb_iradio_view_cmd_select_all),
	BONOBO_UI_VERB ("SelectNone",  (BonoboUIVerbFn) rb_iradio_view_cmd_select_none),
	BONOBO_UI_VERB ("CurrentStation", (BonoboUIVerbFn) rb_iradio_view_cmd_current_station),
	BONOBO_UI_VERB ("NewStation",  (BonoboUIVerbFn) rb_iradio_view_cmd_new_station),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_iradio_view_listeners[] = 
{
	RB_BONOBO_UI_LISTENER ("ShowBrowser", (BonoboUIListenerFn) rb_iradio_view_show_browser_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class = NULL;

GType
rb_iradio_view_get_type (void)
{
	static GType rb_iradio_view_type = 0;

	if (rb_iradio_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_iradio_view_class_init,
			NULL,
			NULL,
			sizeof (RBIRadioView),
			0,
			(GInstanceInitFunc) rb_iradio_view_init
		};

		static const GInterfaceInfo player_info =
		{
			(GInterfaceInitFunc) rb_iradio_view_player_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo clipboard_info =
		{
			(GInterfaceInitFunc) rb_iradio_view_clipboard_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo status_info =
		{
			(GInterfaceInitFunc) rb_iradio_view_status_init,
			NULL,
			NULL
		};

		rb_iradio_view_type = g_type_register_static (RB_TYPE_VIEW,
							      "RBIRadioView",
							      &our_info, 0);
		
		g_type_add_interface_static (rb_iradio_view_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_iradio_view_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_iradio_view_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_iradio_view_type;
}

static void
rb_iradio_view_class_init (RBIRadioViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBViewClass *view_class = RB_VIEW_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_iradio_view_finalize;

	object_class->set_property = rb_iradio_view_set_property;
	object_class->get_property = rb_iradio_view_get_property;

	view_class->impl_get_description  = impl_get_description;
	view_class->impl_get_selection    = impl_get_selection;
	view_class->impl_get_extra_widget = rb_iradio_view_get_extra_widget;

	g_object_class_install_property (object_class,
					 PROP_BACKEND,
					 g_param_spec_object ("backend",
							      "Backend",
							      "IRadio Backend",
							      RB_TYPE_IRADIO_BACKEND,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_iradio_view_init (RBIRadioView *view)
{
	RBSidebarButton *button;
	
	view->priv = g_new0 (RBIRadioViewPrivate, 1);

	button = rb_sidebar_button_new ("RbIRadioView",
					_("iradio"));
	rb_sidebar_button_set (button,
			       RB_STOCK_IRADIO,
			       _("Internet\nRadio"),
			       TRUE);
	g_object_set_data (G_OBJECT (button), "view", view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);

	view->priv->search = rb_search_entry_new ();
	g_object_ref (G_OBJECT (view->priv->search));
	g_signal_connect (G_OBJECT (view->priv->search),
			  "search",
			  G_CALLBACK (rb_iradio_view_search_cb),
			  view);
}

static void
rb_iradio_view_finalize (GObject *object)
{
	RBIRadioView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_VIEW (object));

	view = RB_IRADIO_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	/* save state */
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION, view->priv->paned_position);

	g_free (view->priv->title);
	g_free (view->priv->status);

	g_object_unref (G_OBJECT (view->priv->stations_filter));
	g_object_unref (G_OBJECT (view->priv->search));

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_iradio_view_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBIRadioView *view = RB_IRADIO_VIEW (object);

	switch (prop_id)
	{
	case PROP_BACKEND:
		{
			view->priv->backend = g_value_get_object (value);

			view->priv->paned = gtk_hpaned_new ();

			/* Initialize the filters */
			view->priv->stations_filter = rb_node_filter_new ();

			/* set up genre node view */
			view->priv->genres = rb_node_view_new (rb_iradio_backend_get_all_genres (view->priv->backend),
							       rb_file ("rb-node-view-iradio-genres.xml"),
							       NULL);
			g_object_set (G_OBJECT (view->priv->genres), "vscrollbar_policy", GTK_POLICY_AUTOMATIC, NULL);
			g_object_ref (G_OBJECT (view->priv->genres));

			g_signal_connect (G_OBJECT (view->priv->genres),
					  "node_selected",
					  G_CALLBACK (genre_node_selected_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->genres),
					  "node_activated",
					  G_CALLBACK (genre_activated_cb),
					  view);

			/* set up stations tree view */
			view->priv->stations = rb_node_view_new (rb_iradio_backend_get_all_stations (view->priv->backend),
								 rb_file ("rb-node-view-iradio-stations.xml"),
								 view->priv->stations_filter);

			g_signal_connect (G_OBJECT (view->priv->stations), "playing_node_removed",
					  G_CALLBACK (rb_iradio_view_node_removed_cb), view);

			/* this gets emitted when the paned thingie is moved */
			g_signal_connect (G_OBJECT (view->priv->stations),
					  "size_allocate",
					  G_CALLBACK (paned_size_allocate_cb),
					  view);

			g_signal_connect (G_OBJECT (view->priv->stations),
					  "node_activated",
					  G_CALLBACK (station_activated_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->stations),
					  "changed",
					  G_CALLBACK (node_view_changed_cb),
					  view);	

			gtk_paned_pack2 (GTK_PANED (view->priv->paned), GTK_WIDGET (view->priv->stations), TRUE, FALSE);

			gtk_box_pack_start_defaults (GTK_BOX (view->priv->vbox), view->priv->paned);

			view->priv->paned_position = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);
			view->priv->show_browser = eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER);

			gtk_paned_set_position (GTK_PANED (view->priv->paned), view->priv->paned_position);
			rb_iradio_view_show_browser (view, view->priv->show_browser);
			
			rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_STATION, FALSE);
			rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_STATION_INFO,
					       rb_node_view_have_selection (view->priv->stations));

/* 			rb_node_view_select_node (view->priv->genres, */
/* 			 		          rb_iradio_backend_get_all_stations (view->priv->backend)); */
			gtk_widget_show_all (GTK_WIDGET (view));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_iradio_view_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBIRadioView *view = RB_IRADIO_VIEW (object);

	switch (prop_id)
	{
	case PROP_BACKEND:
		g_value_set_object (value, view->priv->backend);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBView *
rb_iradio_view_new (BonoboUIContainer *container,
		    RBIRadioBackend *backend)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_IRADIO_VIEW,
				      "ui-file", "rhythmbox-iradio-view.xml",
				      "ui-name", "IRadioView",
				      "container", container,
				      "backend", backend,
				      "verbs", rb_iradio_view_verbs,
				      "listeners", rb_iradio_view_listeners,
				      NULL));

	return view;
}

static void
genre_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBIRadioView *iradioview)
{
	stations_filter (iradioview, node);
	rb_node_view_select_node (iradioview->priv->stations,
				  rb_iradio_backend_get_all_stations (iradioview->priv->backend));
	rb_search_entry_clear (iradioview->priv->search);
}

static void 
genre_activated_cb (RBNodeView *view,
		    RBNode *node,
		    RBIRadioView *iradioview)
{
	RBNode *first_node;

	g_return_if_fail (iradioview != NULL);

	first_node = rb_node_view_get_first_node (iradioview->priv->stations);
	if (first_node != NULL)
	{
		rb_iradio_view_set_playing_node (iradioview, first_node);
		rb_view_player_notify_changed (RB_VIEW_PLAYER (iradioview));
		rb_view_player_notify_playing (RB_VIEW_PLAYER (iradioview));
	}
}

static void
rb_iradio_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle      = rb_iradio_view_set_shuffle;
	iface->impl_set_repeat       = rb_iradio_view_set_repeat;
	iface->impl_can_pause        = rb_iradio_view_can_pause;
	iface->impl_have_first       = rb_iradio_view_have_first;
	iface->impl_have_next        = rb_iradio_view_have_next;
	iface->impl_have_previous    = rb_iradio_view_have_previous;
	iface->impl_next             = rb_iradio_view_next;
	iface->impl_previous         = rb_iradio_view_previous;
	iface->impl_get_title        = rb_iradio_view_get_title;
	iface->impl_have_artist_album = rb_iradio_view_have_artist_album;
	iface->impl_get_artist       = rb_iradio_view_get_artist;
	iface->impl_get_album        = rb_iradio_view_get_album;
	iface->impl_have_url	     = rb_iradio_view_have_url;
	iface->impl_get_url          = rb_iradio_view_get_url;
	iface->impl_get_song         = rb_iradio_view_get_song;
	iface->impl_get_duration     = rb_iradio_view_get_duration;
	iface->impl_get_pixbuf       = rb_iradio_view_get_pixbuf;
	iface->impl_get_stream       = rb_iradio_view_get_stream;
	iface->impl_start_playing    = rb_iradio_view_start_playing;
	iface->impl_stop_playing     = rb_iradio_view_stop_playing;
}

static void
rb_iradio_view_status_init (RBViewStatusIface *iface)
{
	iface->impl_get = rb_iradio_view_status_get;
}

static void
rb_iradio_view_clipboard_init (RBViewClipboardIface *iface)
{
	iface->impl_can_cut    = rb_iradio_view_can_cut;
	iface->impl_can_copy   = rb_iradio_view_can_copy;
	iface->impl_can_paste  = rb_iradio_view_can_paste;
	iface->impl_can_delete = rb_iradio_view_can_delete;
	iface->impl_cut        = rb_iradio_view_cut;
	iface->impl_copy       = rb_iradio_view_copy;
	iface->impl_paste      = rb_iradio_view_paste;
	iface->impl_delete     = rb_iradio_view_delete;
	iface->impl_song_info  = rb_iradio_view_song_info;
}

static void
rb_iradio_view_set_shuffle (RBViewPlayer *player,
			     gboolean shuffle)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	view->priv->shuffle = shuffle;
}

static void
rb_iradio_view_set_repeat (RBViewPlayer *player,
			    gboolean repeat)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	view->priv->repeat = repeat;
}

static gboolean
rb_iradio_view_can_pause (RBViewPlayer *player)
{
	return FALSE;
}

static RBViewPlayerResult
rb_iradio_view_have_first (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *first;

	first = rb_iradio_view_get_first_node (view);

	return (first != NULL);
}

static RBViewPlayerResult
rb_iradio_view_have_next (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *next;

	next = rb_iradio_view_get_next_node (view);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_iradio_view_have_previous (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *previous;

	previous = rb_iradio_view_get_previous_node (view);

	return (previous != NULL);
}

static void
rb_iradio_view_next (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *node;

	node = rb_iradio_view_get_next_node (view);
	
	rb_iradio_view_set_playing_node (view, node);
}

static void
rb_iradio_view_previous (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *node;
		
	node = rb_iradio_view_get_previous_node (view);
	
	rb_iradio_view_set_playing_node (view, node);
}

static const char *
rb_iradio_view_get_title (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	fprintf (stderr, "rb_iradio_view_get_title => %s\n", view->priv->title);
	return (const char *) view->priv->title;
}

static RBViewPlayerResult
rb_iradio_view_have_artist_album (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_FALSE;
}

static const char *
rb_iradio_view_get_artist (RBViewPlayer *player)
{
	return NULL;
}

static const char *
rb_iradio_view_get_album (RBViewPlayer *player)
{
	return NULL;
}

static RBViewPlayerResult
rb_iradio_view_have_url (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_TRUE;
}

static void rb_iradio_view_get_url (RBViewPlayer *player, char **text, char **url)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	*text = view->priv->name;
	*url = view->priv->url;
}

static const char *
rb_iradio_view_get_song (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	return view->priv->song;
}

static long
rb_iradio_view_get_duration (RBViewPlayer *player)
{
	return -1;
}

static GdkPixbuf *
rb_iradio_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_iradio_view_get_stream (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	return view->priv->playing_stream;
}

static GtkWidget *
rb_iradio_view_get_extra_widget (RBView *base_view)
{
	RBIRadioView *view = RB_IRADIO_VIEW (base_view);

	return GTK_WIDGET (view->priv->search);
}

static void
rb_iradio_view_start_playing (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);
	RBNode *node;

	node = rb_iradio_view_get_first_node (view);

	rb_iradio_view_set_playing_node (view, node);
}

static void
rb_iradio_view_stop_playing (RBViewPlayer *player)
{
	RBIRadioView *view = RB_IRADIO_VIEW (player);

	rb_iradio_view_set_playing_node (view, NULL);
}

static void
rb_iradio_view_set_playing_node (RBIRadioView *view,
				 RBNode *node)
{
	rb_node_view_set_playing_node (view->priv->stations, node);

	g_free (view->priv->title);

	if (node == NULL)
	{
		view->priv->playing_stream = NULL;

		view->priv->title = NULL;

		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_STATION, FALSE);
	}
	else
	{
		GError *error = NULL;
		const char *uri;
		GList *alt_locations;

		uri = rb_node_get_property_string (node, RB_NODE_PROP_LOCATION);

		g_assert (uri != NULL);
		fprintf(stderr, "opening \"%s\"\n", uri);

		view->priv->playing_stream = monkey_media_audio_stream_new (uri, &error);
		if (error != NULL)
		{
			RBGListWrapper *listwrapper = RB_GLIST_WRAPPER (rb_node_get_property_object (node, RB_NODE_PROP_ALT_LOCATIONS));
			alt_locations = rb_glist_wrapper_get_list (listwrapper);
			while (error != NULL && alt_locations != NULL)
			{
				g_error_free (error);
				error = NULL;
				view->priv->playing_stream = monkey_media_audio_stream_new ((char *) alt_locations->data,
											    &error);
				alt_locations = alt_locations->next;
			}
			if (error != NULL)
			{
				rb_error_dialog (_("Failed to create stream for %s, error was:\n%s"),
						 uri, error->message);
				g_error_free (error);
				return;
			}
		}
		rb_node_update_play_statistics (node);
		
		g_signal_connect (G_OBJECT (view->priv->playing_stream),
				  "end_of_stream",
				  G_CALLBACK (station_eos_cb),
				  view);
		g_signal_connect (G_OBJECT (view->priv->playing_stream),
				  "info_available",
				  G_CALLBACK (info_available_cb),
				  view);
		
		g_free (view->priv->song);
		view->priv->song = g_strdup (_("Unknown"));
		g_free (view->priv->name);
		view->priv->name = g_strdup (rb_node_get_property_string (node, RB_NODE_PROP_NAME));
		g_free (view->priv->title);
		view->priv->title = g_strdup_printf("%s (%s)", view->priv->song, view->priv->name);

		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_STATION, TRUE);
	}
}

static void
station_activated_cb (RBNodeView *view,
		      RBNode *node,
		      RBIRadioView *iradioview)
{
	rb_iradio_view_set_playing_node (iradioview, node);

	rb_view_player_notify_changed (RB_VIEW_PLAYER (iradioview));
	rb_view_player_notify_playing (RB_VIEW_PLAYER (iradioview));
}

static void
node_view_changed_cb (RBNodeView *view,
		      RBIRadioView *iradioview)
{
	rb_view_player_notify_changed (RB_VIEW_PLAYER (iradioview));
	rb_view_status_notify_changed (RB_VIEW_STATUS (iradioview));
	rb_view_clipboard_notify_changed (RB_VIEW_CLIPBOARD (iradioview));
	rb_view_set_sensitive (RB_VIEW (iradioview), CMD_PATH_STATION_INFO,
			       rb_node_view_have_selection (view));
}

/* static void */
/* station_update_statistics (RBIRadioView *view) */
/* { */
/* 	RBNode *node; */

/* 	node = rb_node_view_get_playing_node (view->priv->stations); */
/* 	rb_node_update_play_statistics (node); */
/* } */

static void
station_eos_cb (MonkeyMediaStream *stream,
		RBIRadioView *view)
{

	GDK_THREADS_ENTER ();

	rb_iradio_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));

	GDK_THREADS_LEAVE ();
}

static RBNode *
rb_iradio_view_get_previous_node (RBIRadioView *view)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->stations);
	else
		node = rb_node_view_get_previous_random_node (view->priv->stations);

	return node;
}

static RBNode *
rb_iradio_view_get_first_node (RBIRadioView *view)
{
	RBNode *node;

	if (view->priv->shuffle == FALSE)
	{
		GList *sel = rb_node_view_get_selection (view->priv->stations);

		if (sel == NULL)
			node = rb_node_view_get_first_node (view->priv->stations);
		else
		{
			GList *first = g_list_first (sel);
			node = RB_NODE (first->data);
		}
	}
	else
		node = rb_node_view_get_next_random_node (view->priv->stations);

	return node;
}

static RBNode *
rb_iradio_view_get_next_node (RBIRadioView *view)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
	{
		node = rb_node_view_get_next_node (view->priv->stations);
		if (node == NULL && view->priv->repeat == TRUE)
		{
			node = rb_node_view_get_first_node (view->priv->stations);
		}
	}
	else
		node = rb_node_view_get_next_random_node (view->priv->stations);

	return node;
}

void info_available_cb (MonkeyMediaStream *stream,
			MonkeyMediaStreamInfoField field,
			GValue *value,
			gpointer data)
{
 	RBIRadioView *view = RB_IRADIO_VIEW (data);
	gboolean changed = FALSE;
	switch (field)
	{
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
	{
		char *song = g_value_dup_string(value);
		fprintf (stderr, "Got song info: %s\n", song);
		if (strcmp(song, view->priv->song))
		{
			changed = TRUE;
			g_free(view->priv->song);
			view->priv->song = song;
		}
		else
		{
			g_free(song);
		}
		break;
	}
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	{
		char *url = g_value_dup_string(value);
		fprintf (stderr, "Got url info: %s\n", url);
		if (!view->priv->url || strcmp(url, view->priv->url))
		{
			changed = TRUE;
			if (view->priv->url)
				g_free(view->priv->url);
			view->priv->url = url;
		}
		else
		{
			g_free(url);
		}
		break;
	}
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
	{
		GValue newval = { 0, };
		int bitrate = g_value_get_int (value) / 1000;
		char *qualitystr;
		MonkeyMediaAudioQuality quality = monkey_media_audio_quality_from_bit_rate (bitrate);
		qualitystr = monkey_media_audio_quality_to_string (quality);
		fprintf(stderr, "got bitrate: %d => quality %s\n", bitrate, qualitystr);
		g_value_init (&newval, G_TYPE_STRING);
		g_value_set_string_take_ownership (&newval, qualitystr);
		rb_node_set_property (RB_NODE (rb_node_view_get_playing_node (view->priv->stations)),
				      RB_NODE_PROP_QUALITY,
				      &newval);
		g_value_unset (&newval);
		break;
	}
/* 	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST: */
/* 		fprintf(stderr, "artist: %s\n", g_value_get_string(value)); */
/* 		break; */
/* 	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM: */
/* 		fprintf(stderr, "album: %s\n", g_value_get_string(value)); */
/* 		break; */
	default:
	{
		GEnumValue *enumvalue = g_enum_get_value(g_type_class_peek(MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD),
							 field);
		fprintf(stderr, "unused info field: %s\n", enumvalue->value_name);
		return;
	}
	}

	if (changed)
	{
		g_free(view->priv->title);
		view->priv->title = g_strdup_printf("%s (%s)", view->priv->song, view->priv->name);
		fprintf (stderr, "Setting title: song: %s name: %s\n", view->priv->song, view->priv->name);
		rb_view_player_notify_changed (RB_VIEW_PLAYER (data));
	}
}

static const char *
rb_iradio_view_status_get (RBViewStatus *status)
{
 	RBIRadioView *view = RB_IRADIO_VIEW (status);
	char *ret;
	ret = g_strdup_printf (_("%d total stations in %d distinct genres"),
			       rb_iradio_backend_get_station_count (view->priv->backend),
			       rb_iradio_backend_get_genre_count (view->priv->backend));
	return ret;
}

static gboolean
rb_iradio_view_can_cut (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_iradio_view_can_copy (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_IRADIO_VIEW (clipboard)->priv->stations);
}

static gboolean
rb_iradio_view_can_paste (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_iradio_view_can_delete (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_IRADIO_VIEW (clipboard)->priv->stations);
}

static GList *
rb_iradio_view_cut (RBViewClipboard *clipboard)
{
	return NULL;
}

static GList *
rb_iradio_view_copy (RBViewClipboard *clipboard)
{
	RBIRadioView *view = RB_IRADIO_VIEW (clipboard);

	return g_list_copy (rb_node_view_get_selection (view->priv->stations));
}

static void
rb_iradio_view_paste (RBViewClipboard *clipboard,
		       GList *nodes)
{
}

static void
rb_iradio_view_delete (RBViewClipboard *clipboard)
{
	RBIRadioView *view = RB_IRADIO_VIEW (clipboard);
	GList *sel, *l;

	sel = g_list_copy (rb_node_view_get_selection (view->priv->stations));
	for (l = sel; l != NULL; l = g_list_next (l))
	{
		rb_iradio_backend_remove_node (view->priv->backend, RB_NODE (l->data));
	}
	g_list_free (sel);
}

static void
rb_iradio_view_song_info (RBViewClipboard *clipboard)
{
	RBIRadioView *view = RB_IRADIO_VIEW (clipboard);
	GtkWidget *dialog = rb_station_properties_dialog_new (view->priv->stations, view->priv->backend);
	gtk_widget_show_all (dialog);
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBIRadioView *view)
{
	view->priv->paned_position = gtk_paned_get_position (GTK_PANED (view->priv->paned));
}

static void
rb_iradio_view_cmd_select_all (BonoboUIComponent *component,
				RBIRadioView *view,
				const char *verbname)
{
	rb_node_view_select_all (view->priv->stations);
}

static void
rb_iradio_view_cmd_select_none (BonoboUIComponent *component,
				 RBIRadioView *view,
				 const char *verbname)
{
	rb_node_view_select_none (view->priv->stations);
}

static void
rb_iradio_view_cmd_current_station (BonoboUIComponent *component,
				    RBIRadioView *view,
				    const char *verbname)
{
	RBNode *node = rb_node_view_get_playing_node (view->priv->stations);
	
	if (rb_node_view_get_node_visible (view->priv->stations, node) == FALSE)
	{
		/* adjust filtering to show it */
		rb_node_view_select_node (view->priv->genres,
					  rb_node_station_get_genre (RB_NODE_STATION (node)));
	}

	rb_node_view_scroll_to_node (view->priv->stations, node);
	rb_node_view_select_node (view->priv->stations, node);
}

static void
rb_iradio_view_cmd_new_station (BonoboUIComponent *component,
				RBIRadioView *view,
				const char *verbname)
{
	GtkWidget *dialog = rb_new_station_dialog_new (view->priv->stations, view->priv->backend);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
rb_iradio_view_show_browser_changed_cb (BonoboUIComponent *component,
					 const char *path,
					 Bonobo_UIComponent_EventType type,
					 const char *state,
					 RBIRadioView *view)
{
	if (view->priv->lock == TRUE)
		return;

	view->priv->show_browser = rb_view_get_active (RB_VIEW (view), CMD_PATH_SHOW_BROWSER);

	eel_gconf_set_boolean (CONF_STATE_SHOW_BROWSER, view->priv->show_browser);

	rb_iradio_view_show_browser (view, view->priv->show_browser);
}

static void
rb_iradio_view_show_browser (RBIRadioView *view,
			      gboolean show)
{
	GtkWidget *genreswidget = GTK_WIDGET (view->priv->genres);
	view->priv->lock = TRUE;

	rb_view_set_active (RB_VIEW (view), CMD_PATH_SHOW_BROWSER, show);

	view->priv->lock = FALSE;

	if (show == TRUE)
	{
		gtk_paned_pack1 (GTK_PANED (view->priv->paned), genreswidget, FALSE, FALSE);
		gtk_widget_show_all (genreswidget);
	}
	else if (show == FALSE)
	{
		gtk_widget_hide (genreswidget);
		gtk_container_remove (GTK_CONTAINER (view->priv->paned), genreswidget);
	}
}

static const char *
impl_get_description (RBView *view)
{
	return _("Internet\nRadio");
}

static GList *
impl_get_selection (RBView *view)
{
	RBIRadioView *iradioview = RB_IRADIO_VIEW (view);

	return rb_node_view_get_selection (iradioview->priv->stations);
}

static void
rb_iradio_view_node_removed_cb (RBNode *node,
				 RBIRadioView *view)
{
	rb_iradio_view_set_playing_node (view, NULL);
}

static void
rb_iradio_view_search_cb (RBSearchEntry *search,
			   const char *search_text,
			   RBIRadioView *view)
{
	GDK_THREADS_ENTER ();

	/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0)
	{
		rb_node_view_select_node (view->priv->genres,
		 		          rb_iradio_backend_get_all_genres (view->priv->backend));
	}
	else
	{
		rb_node_view_select_none (view->priv->genres);

		rb_node_filter_empty (view->priv->stations_filter);
		rb_node_filter_add_expression (view->priv->stations_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_done_changing (view->priv->stations_filter);
	}

	GDK_THREADS_LEAVE ();
}

static void
stations_filter (RBIRadioView *view,
		 RBNode *parent)
{
	rb_node_filter_empty (view->priv->stations_filter);
	rb_node_filter_add_expression (view->priv->stations_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_iradio_backend_get_all_stations (view->priv->backend)),
				       0);
	rb_node_filter_add_expression (view->priv->stations_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      parent),
				       0);
	rb_node_filter_done_changing (view->priv->stations_filter);
}
