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
#include <gtk/gtkmain.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-library-view.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-node-song.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"
#include "rb-node-filter.h"
#include "rb-search-entry.h"
#include "rb-view-cmd.h"
#include "rb-preferences.h"

static void rb_library_view_class_init (RBLibraryViewClass *klass);
static void rb_library_view_init (RBLibraryView *view);
static void rb_library_view_finalize (GObject *object);
static void rb_library_view_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_library_view_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void album_node_selected_cb (RBNodeView *view,
			            RBNode *node,
			            RBLibraryView *libview);
static void browser_view_node_activated_cb (RBNodeView *view,
					    RBNode *node,
					    RBLibraryView *library_view);
static void genre_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBLibraryView *libview);
static void artist_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBLibraryView *libview);
static void rb_library_view_player_init (RBViewPlayerIface *iface);
static void rb_library_view_set_shuffle (RBViewPlayer *player,
			                 gboolean shuffle);
static void rb_library_view_set_repeat (RBViewPlayer *player,
			                gboolean repeat);
static RBViewPlayerResult rb_library_view_have_first (RBViewPlayer *player);
static RBViewPlayerResult rb_library_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_library_view_have_previous (RBViewPlayer *player);
static void rb_library_view_next (RBViewPlayer *player);
static void rb_library_view_previous (RBViewPlayer *player);
static void rb_library_view_jump_to_current (RBViewPlayer *player);
static const char *rb_library_view_get_title (RBViewPlayer *player);
static const char *rb_library_view_get_artist (RBViewPlayer *player);
static const char *rb_library_view_get_album (RBViewPlayer *player);
static const char *rb_library_view_get_song (RBViewPlayer *player);
static long rb_library_view_get_duration (RBViewPlayer *player);
static GdkPixbuf *rb_library_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_library_view_get_stream (RBViewPlayer *player);
static void rb_library_view_start_playing (RBViewPlayer *player);
static void rb_library_view_stop_playing (RBViewPlayer *player);
static void rb_library_view_set_playing_node (RBLibraryView *view,
			                      RBNode *node);
static void song_activated_cb (RBNodeView *view,
		               RBNode *node,
		               RBLibraryView *library_view);
static void node_view_changed_cb (RBNodeView *view,
		                  RBLibraryView *library_view);
static void song_eos_cb (MonkeyMediaStream *stream,
	                 RBLibraryView *view);
static RBNode *rb_library_view_get_first_node (RBLibraryView *view);
static RBNode *rb_library_view_get_previous_node (RBLibraryView *view,
						  gboolean just_check);
static RBNode *rb_library_view_get_next_node (RBLibraryView *view,
					      gboolean just_check);
static void rb_library_view_status_init (RBViewStatusIface *iface);
static const char *rb_library_view_status_get (RBViewStatus *status);
static void rb_library_view_clipboard_init (RBViewClipboardIface *iface);
static gboolean rb_library_view_can_cut (RBViewClipboard *clipboard);
static gboolean rb_library_view_can_copy (RBViewClipboard *clipboard);
static gboolean rb_library_view_can_paste (RBViewClipboard *clipboard);
static gboolean rb_library_view_can_delete (RBViewClipboard *clipboard);
static GList *rb_library_view_cut (RBViewClipboard *clipboard);
static GList *rb_library_view_copy (RBViewClipboard *clipboard);
static void rb_library_view_paste (RBViewClipboard *clipboard,
		                   GList *nodes);
static void rb_library_view_delete (RBViewClipboard *clipboard);
static void rb_library_view_song_info (RBViewClipboard *clipboard);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBLibraryView *view);
static void rb_library_view_cmd_select_all (BonoboUIComponent *component,
				            RBLibraryView *view,
				            const char *verbname);
static void rb_library_view_cmd_select_none (BonoboUIComponent *component,
				             RBLibraryView *view,
				             const char *verbname);
static void rb_library_view_cmd_current_song (BonoboUIComponent *component,
				              RBLibraryView *view,
				              const char *verbname);
static void rb_library_view_show_browser_changed_cb (BonoboUIComponent *component,
					             const char *path,
					             Bonobo_UIComponent_EventType type,
					             const char *state,
					             RBLibraryView *view);
static void rb_library_view_show_browser (RBLibraryView *view, gboolean show);
static void rb_library_view_drop_cb (GtkWidget        *widget,
				     GdkDragContext   *context,
				     gint              x,
				     gint              y,
				     GtkSelectionData *data,
				     guint             info,
				     guint             time,
				     gpointer          user_data);
static const char *impl_get_description (RBView *view);
static GList *impl_get_selection (RBView *view);
static void rb_library_view_node_removed_cb (RBNode *node,
					     RBLibraryView *view);
static GtkWidget *rb_library_view_get_extra_widget (RBView *base_view);
static void rb_library_view_search_cb (RBSearchEntry *search,
			               const char *search_text,
			               RBLibraryView *view);
static void artists_filter (RBLibraryView *view,
	                    RBNode *genre);
static void albums_filter (RBLibraryView *view,
			   RBNode *genre,
	                   RBNode *artist);
static void songs_filter (RBLibraryView *view,
	                  RBNode *genre,
	                  RBNode *artist,
			  RBNode *album);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_SONG "/commands/CurrentSong"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"
#define LIBRARY_VIEW_SONGS_POPUP_PATH "/popups/LibrarySongsList"

#define CONF_STATE_PANED_POSITION "/apps/rhythmbox/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   "/apps/rhythmbox/state/library/show_browser"

struct RBLibraryViewPrivate
{
	RBLibrary *library;

	GtkWidget *browser;
	GtkWidget *vbox;

	RBNodeView *genres;
	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	MonkeyMediaAudioStream *playing_stream;

	char *title;

	gboolean shuffle;
	gboolean repeat;

	RBSearchEntry *search;

	char *status;

	GtkWidget *paned;
	int paned_position;

	gboolean show_browser;
	gboolean lock;

	RBNodeFilter *artists_filter;
	RBNodeFilter *songs_filter;
	RBNodeFilter *albums_filter;

	gboolean changing_artist;
	gboolean changing_genre;

	guint views_notif;
};

enum
{
	PROP_0,
	PROP_LIBRARY
};

static BonoboUIVerb rb_library_view_verbs[] = 
{
	BONOBO_UI_VERB ("SelectAll",   (BonoboUIVerbFn) rb_library_view_cmd_select_all),
	BONOBO_UI_VERB ("SelectNone",  (BonoboUIVerbFn) rb_library_view_cmd_select_none),
	BONOBO_UI_VERB ("CurrentSong", (BonoboUIVerbFn) rb_library_view_cmd_current_song),
	BONOBO_UI_VERB ("SLCopy", (BonoboUIVerbFn) rb_view_cmd_song_copy),
	BONOBO_UI_VERB ("SLDelete", (BonoboUIVerbFn) rb_view_cmd_song_delete),
	BONOBO_UI_VERB ("SLProperties", (BonoboUIVerbFn) rb_view_cmd_song_properties),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_library_view_listeners[] = 
{
	RB_BONOBO_UI_LISTENER ("ShowBrowser", (BonoboUIListenerFn) rb_library_view_show_browser_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class = NULL;

/* dnd */
static const GtkTargetEntry target_uri [] = 
		{ { RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST } };
static const GtkTargetEntry target_id [] = 
		{ { RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID } };

GType
rb_library_view_get_type (void)
{
	static GType rb_library_view_type = 0;

	if (rb_library_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_view_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryView),
			0,
			(GInstanceInitFunc) rb_library_view_init
		};

		static const GInterfaceInfo player_info =
		{
			(GInterfaceInitFunc) rb_library_view_player_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo clipboard_info =
		{
			(GInterfaceInitFunc) rb_library_view_clipboard_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo status_info =
		{
			(GInterfaceInitFunc) rb_library_view_status_init,
			NULL,
			NULL
		};

		rb_library_view_type = g_type_register_static (RB_TYPE_VIEW,
							       "RBLibraryView",
							       &our_info, 0);
		
		g_type_add_interface_static (rb_library_view_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_library_view_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_library_view_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_library_view_type;
}

static void
rb_library_view_class_init (RBLibraryViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBViewClass *view_class = RB_VIEW_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_view_finalize;

	object_class->set_property = rb_library_view_set_property;
	object_class->get_property = rb_library_view_get_property;

	view_class->impl_get_description  = impl_get_description;
	view_class->impl_get_selection    = impl_get_selection;
	view_class->impl_get_extra_widget = rb_library_view_get_extra_widget;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
update_browser_views_visibility (RBLibraryView *view)
{
	int views;
	GtkWidget *genres = GTK_WIDGET (view->priv->genres);
	GtkWidget *artists = GTK_WIDGET (view->priv->artists);
	GtkWidget *albums = GTK_WIDGET (view->priv->albums);
	
	views = eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS);

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
browser_views_notifier (GConfClient *client,
 			guint cnxn_id,
 			GConfEntry *entry,
 			RBLibraryView *view)
{
	rb_node_view_select_node (view->priv->genres,
				  rb_library_get_all_artists (view->priv->library));
	
	update_browser_views_visibility (view);
}

static void
rb_library_view_init (RBLibraryView *view)
{
	RBSidebarButton *button;
	
	view->priv = g_new0 (RBLibraryViewPrivate, 1);

	button = rb_sidebar_button_new ("RbLibraryView",
					_("library"));
	rb_sidebar_button_set (button,
			       RB_STOCK_LIBRARY,
			       _("Library"),
			       TRUE);
	g_object_set_data (G_OBJECT (button), "view", view);


	/* Drag'n'Drop */
	rb_sidebar_button_add_dnd_targets (button,
					   target_uri, 1);
	g_signal_connect (G_OBJECT (button), "drag_data_received",
			  G_CALLBACK (rb_library_view_drop_cb), view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);

	view->priv->search = rb_search_entry_new ();
	g_object_ref (G_OBJECT (view->priv->search));
	g_signal_connect (G_OBJECT (view->priv->search),
			  "search",
			  G_CALLBACK (rb_library_view_search_cb),
			  view);
	
	view->priv->views_notif = eel_gconf_notification_add 
		(CONF_UI_BROWSER_VIEWS, (GConfClientNotifyFunc) browser_views_notifier, view);
}

static void
rb_library_view_finalize (GObject *object)
{
	RBLibraryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_VIEW (object));

	view = RB_LIBRARY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	/* save state */
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION, view->priv->paned_position);

	g_free (view->priv->title);
	g_free (view->priv->status);

	g_object_unref (G_OBJECT (view->priv->artists_filter));
	g_object_unref (G_OBJECT (view->priv->songs_filter));
	g_object_unref (G_OBJECT (view->priv->albums_filter));
	
	g_object_unref (G_OBJECT (view->priv->search));

	eel_gconf_notification_remove (view->priv->views_notif);
	
	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_library_view_songs_show_popup_cb (RBNodeView *view,
				     RBNode *node,
		   		     RBLibraryView *library_view)
{
	GtkWidget *menu;
	GtkWidget *window;
	
	window = gtk_widget_get_ancestor (GTK_WIDGET (view), 
					  BONOBO_TYPE_WINDOW);
	
	menu = gtk_menu_new ();
	
	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu), 
			         LIBRARY_VIEW_SONGS_POPUP_PATH);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static void
rb_library_view_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		{
			view->priv->library = g_value_get_object (value);

			view->priv->paned = gtk_vpaned_new ();

			view->priv->browser = gtk_hbox_new (TRUE, 5);

			/* Initialize the filters */
			view->priv->artists_filter = rb_node_filter_new ();
			view->priv->songs_filter = rb_node_filter_new ();
			view->priv->albums_filter = rb_node_filter_new ();

			/* set up genres treeview */
			view->priv->genres = rb_node_view_new 
				(rb_library_get_all_genres (view->priv->library),
				 rb_file ("rb-node-view-genres.xml"),
				 NULL);
			g_signal_connect (G_OBJECT (view->priv->genres),
					  "node_selected",
					  G_CALLBACK (genre_node_selected_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->genres),
					  "node_activated",
					  G_CALLBACK (browser_view_node_activated_cb),
					  view);


			gtk_box_pack_start_defaults (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->genres));


			/* set up artist treeview */
			view->priv->artists = rb_node_view_new (rb_library_get_all_artists (view->priv->library),
						                rb_file ("rb-node-view-artists.xml"),
								view->priv->artists_filter);
			g_signal_connect (G_OBJECT (view->priv->artists),
					  "node_selected",
					  G_CALLBACK (artist_node_selected_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->artists),
					  "node_activated",
					  G_CALLBACK (browser_view_node_activated_cb),
					  view);


			gtk_box_pack_start_defaults (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->artists));

			/* set up albums treeview */
			view->priv->albums = rb_node_view_new (rb_library_get_all_albums (view->priv->library),
						               rb_file ("rb-node-view-albums.xml"),
							       view->priv->albums_filter);
			g_signal_connect (G_OBJECT (view->priv->albums),
					  "node_selected",
					  G_CALLBACK (album_node_selected_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->albums),
					  "node_activated",
					  G_CALLBACK (browser_view_node_activated_cb),
					  view);

			gtk_box_pack_start_defaults (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->albums));
			gtk_paned_pack1 (GTK_PANED (view->priv->paned), view->priv->browser, FALSE, FALSE);
			
			/* set up songs tree view */
			view->priv->songs = rb_node_view_new (rb_library_get_all_songs (view->priv->library),
						              rb_file ("rb-node-view-songs.xml"),
							      view->priv->songs_filter);

			g_signal_connect (G_OBJECT (view->priv->songs), "playing_node_removed",
					  G_CALLBACK (rb_library_view_node_removed_cb), view);
			g_signal_connect (G_OBJECT (view->priv->songs), "show_popup",
					  G_CALLBACK (rb_library_view_songs_show_popup_cb), view);
			
			/* Drag'n'Drop for songs view */
			g_signal_connect (G_OBJECT (view->priv->songs), "drag_data_received",
					  G_CALLBACK (rb_library_view_drop_cb), view);
			gtk_drag_dest_set (GTK_WIDGET (view->priv->songs), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (view->priv->songs, target_uri, 1);

			/* Drag'n'Drop for albums view */
			g_signal_connect (G_OBJECT (view->priv->albums), "drag_data_received",
					  G_CALLBACK (rb_library_view_drop_cb), view);
			gtk_drag_dest_set (GTK_WIDGET (view->priv->albums), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (view->priv->albums, target_id, 1);

			/* Drag'n'Drop for artists view */
			g_signal_connect (G_OBJECT (view->priv->artists), "drag_data_received",
					  G_CALLBACK (rb_library_view_drop_cb), view);
			gtk_drag_dest_set (GTK_WIDGET (view->priv->artists), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (view->priv->artists, target_id, 1);
	
			/* this gets emitted when the paned thingie is moved */
			g_signal_connect (G_OBJECT (view->priv->songs),
					  "size_allocate",
					  G_CALLBACK (paned_size_allocate_cb),
					  view);

			g_signal_connect (G_OBJECT (view->priv->songs),
					  "node_activated",
					  G_CALLBACK (song_activated_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->songs),
					  "changed",
					  G_CALLBACK (node_view_changed_cb),
					  view);	

			gtk_paned_pack2 (GTK_PANED (view->priv->paned), GTK_WIDGET (view->priv->songs), TRUE, FALSE);

			gtk_box_pack_start_defaults (GTK_BOX (view->priv->vbox), view->priv->paned);

			view->priv->paned_position = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);
			view->priv->show_browser = eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER);

			gtk_widget_show_all (GTK_WIDGET (view));

			gtk_paned_set_position (GTK_PANED (view->priv->paned), view->priv->paned_position);
			rb_library_view_show_browser (view, view->priv->show_browser);
			
			rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, FALSE);
			rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_SONG_INFO,
					       rb_node_view_have_selection (view->priv->songs));

			update_browser_views_visibility (view);
			
			rb_node_view_select_node (view->priv->artists,
			 		          rb_library_get_all_albums (view->priv->library));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_view_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, view->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBView *
rb_library_view_new (BonoboUIContainer *container,
		     RBLibrary *library)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_LIBRARY_VIEW,
				      "ui-file", "rhythmbox-library-view.xml",
				      "ui-name", "LibraryView",
				      "container", container,
				      "library", library,
				      "verbs", rb_library_view_verbs,
				      "listeners", rb_library_view_listeners,
				      NULL));

	return view;
}

static RBNode *
ensure_node_selection (RBNodeView *view,
		       RBNode *all_node,
		       gboolean *changing_flag)
{
	GList *selection = rb_node_view_get_selection (view);

	if (selection == NULL)
	{
		*changing_flag = TRUE;
		rb_node_view_select_node (view, all_node);
		*changing_flag = FALSE;
		selection = rb_node_view_get_selection (view);
	}

	return RB_NODE (selection->data);
}


static void
genre_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibraryView *libview)
{
	if (libview->priv->changing_genre == TRUE)
		return;

	artists_filter (libview, node);
	rb_node_view_select_node (libview->priv->artists,
				  rb_library_get_all_albums (libview->priv->library));

	rb_search_entry_clear (libview->priv->search);
}

static void
artist_node_selected_cb (RBNodeView *view,
			 RBNode *node,
			 RBLibraryView *libview)
{
	RBNode *genre;

	if (libview->priv->changing_artist == TRUE)
		return;

	genre = ensure_node_selection (libview->priv->genres,
			               rb_library_get_all_artists (libview->priv->library),
				       &libview->priv->changing_genre);

	albums_filter (libview, genre, node);
	rb_node_view_select_node (libview->priv->albums,
				  rb_library_get_all_songs (libview->priv->library));

	rb_search_entry_clear (libview->priv->search);
}

static void
album_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibraryView *libview)
{
	RBNode *artist;
	RBNode *genre;

	genre = ensure_node_selection (libview->priv->genres,
			               rb_library_get_all_artists (libview->priv->library),
				       &libview->priv->changing_genre);
	artist = ensure_node_selection (libview->priv->artists,
			                rb_library_get_all_albums (libview->priv->library),
				        &libview->priv->changing_artist);

	rb_search_entry_clear (libview->priv->search);

	songs_filter (libview,
		      genre,
		      artist,
		      node);
}

static void 
browser_view_node_activated_cb (RBNodeView *view,
			   	RBNode *node,
			   	RBLibraryView *library_view)
{
	RBNode *first_node;

	g_return_if_fail (library_view != NULL);

	first_node = rb_node_view_get_first_node (library_view->priv->songs);
	if (first_node != NULL)
	{
		rb_library_view_set_playing_node (library_view, first_node);

		rb_view_player_notify_changed (RB_VIEW_PLAYER (library_view));
		rb_view_player_notify_playing (RB_VIEW_PLAYER (library_view));
	}
}

static void
rb_library_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle      = rb_library_view_set_shuffle;
	iface->impl_set_repeat       = rb_library_view_set_repeat;
	iface->impl_have_first       = rb_library_view_have_first;
	iface->impl_have_next        = rb_library_view_have_next;
	iface->impl_have_previous    = rb_library_view_have_previous;
	iface->impl_next             = rb_library_view_next;
	iface->impl_previous         = rb_library_view_previous;
	iface->impl_jump_to_current  = rb_library_view_jump_to_current;
	iface->impl_get_title        = rb_library_view_get_title;
	iface->impl_get_artist       = rb_library_view_get_artist;
	iface->impl_get_album        = rb_library_view_get_album;
	iface->impl_get_song         = rb_library_view_get_song;
	iface->impl_get_duration     = rb_library_view_get_duration;
	iface->impl_get_pixbuf       = rb_library_view_get_pixbuf;
	iface->impl_get_stream       = rb_library_view_get_stream;
	iface->impl_start_playing    = rb_library_view_start_playing;
	iface->impl_stop_playing     = rb_library_view_stop_playing;
}

static void
rb_library_view_status_init (RBViewStatusIface *iface)
{
	iface->impl_get = rb_library_view_status_get;
}

static void
rb_library_view_clipboard_init (RBViewClipboardIface *iface)
{
	iface->impl_can_cut    = rb_library_view_can_cut;
	iface->impl_can_copy   = rb_library_view_can_copy;
	iface->impl_can_paste  = rb_library_view_can_paste;
	iface->impl_can_delete = rb_library_view_can_delete;
	iface->impl_cut        = rb_library_view_cut;
	iface->impl_copy       = rb_library_view_copy;
	iface->impl_paste      = rb_library_view_paste;
	iface->impl_delete     = rb_library_view_delete;
	iface->impl_song_info  = rb_library_view_song_info;
}

static void
rb_library_view_set_shuffle (RBViewPlayer *player,
			     gboolean shuffle)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);

	view->priv->shuffle = shuffle;
}

static void
rb_library_view_set_repeat (RBViewPlayer *player,
			    gboolean repeat)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);

	view->priv->repeat = repeat;
}

static RBViewPlayerResult
rb_library_view_have_first (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *first;

	first = rb_library_view_get_first_node (view);

	return (first != NULL);
}

static RBViewPlayerResult
rb_library_view_have_next (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *next;

	next = rb_library_view_get_next_node (view, TRUE);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_library_view_have_previous (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *previous;

	previous = rb_library_view_get_previous_node (view, TRUE);

	return (previous != NULL);
}

static void
rb_library_view_next (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_library_view_get_next_node (view, FALSE);
	
	rb_library_view_set_playing_node (view, node);
}

static void
rb_library_view_previous (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;
		
	node = rb_library_view_get_previous_node (view, FALSE);
	
	rb_library_view_set_playing_node (view, node);
}

static void 
rb_library_view_jump_to_current (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);
	if (node != NULL)
	{
		rb_node_view_scroll_to_node (view->priv->songs, node);
	}
}

static const char *
rb_library_view_get_title (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);

	return (const char *) view->priv->title;
}

static const char *
rb_library_view_get_artist (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_SONG_PROP_ARTIST);
	else
		return NULL;
}

static const char *
rb_library_view_get_album (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_SONG_PROP_ALBUM);
	else
		return NULL;
}

static const char *
rb_library_view_get_song (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_NAME);
	else
		return NULL;
}

static long
rb_library_view_get_duration (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_long (node, RB_NODE_SONG_PROP_REAL_DURATION);
	else
		return -1;
}

static GdkPixbuf *
rb_library_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_library_view_get_stream (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);

	return view->priv->playing_stream;
}

static GtkWidget *
rb_library_view_get_extra_widget (RBView *base_view)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (base_view);

	return GTK_WIDGET (view->priv->search);
}

static void
rb_library_view_start_playing (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_library_view_get_first_node (view);

	rb_library_view_set_playing_node (view, node);
}

static void
rb_library_view_stop_playing (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);

	rb_library_view_set_playing_node (view, NULL);
}

static void
rb_library_view_set_playing_node (RBLibraryView *view,
			          RBNode *node)
{
	rb_node_view_set_playing_node (view->priv->songs, node);

	g_free (view->priv->title);

	if (node == NULL)
	{
		view->priv->playing_stream = NULL;

		view->priv->title = NULL;

		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, FALSE);
	}
	else
	{
		GError *error = NULL;
		const char *artist = rb_library_view_get_artist (RB_VIEW_PLAYER (view));
		const char *song = rb_library_view_get_song (RB_VIEW_PLAYER (view));
		const char *uri;

		uri = rb_node_get_property_string (node, 
				                   RB_NODE_SONG_PROP_LOCATION);

		g_assert (uri != NULL);
		
		view->priv->playing_stream = monkey_media_audio_stream_new (uri, &error);
		if (error != NULL)
		{
			rb_error_dialog (_("Failed to create stream for %s, error was:\n%s"),
					 uri, error->message);
			g_error_free (error);
			return;
		}
		
		g_signal_connect (G_OBJECT (view->priv->playing_stream),
				  "end_of_stream",
				  G_CALLBACK (song_eos_cb),
				  view);
		
		view->priv->title = g_strdup_printf ("%s - %s", artist, song);
		
		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, TRUE);
	}
}

static void
song_activated_cb (RBNodeView *view,
		   RBNode *node,
		   RBLibraryView *library_view)
{
	rb_library_view_set_playing_node (library_view, node);

	rb_view_player_notify_changed (RB_VIEW_PLAYER (library_view));
	rb_view_player_notify_playing (RB_VIEW_PLAYER (library_view));
}

static void
node_view_changed_cb (RBNodeView *view,
		      RBLibraryView *library_view)
{
	rb_view_player_notify_changed (RB_VIEW_PLAYER (library_view));
	rb_view_status_notify_changed (RB_VIEW_STATUS (library_view));
	rb_view_clipboard_notify_changed (RB_VIEW_CLIPBOARD (library_view));
	rb_view_set_sensitive (RB_VIEW (library_view), CMD_PATH_SONG_INFO,
			       rb_node_view_have_selection (view));
}

static void
song_update_statistics (RBLibraryView *view)
{
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);
	rb_node_song_update_play_statistics (node);
}

static void
song_eos_cb (MonkeyMediaStream *stream,
	     RBLibraryView *view)
{

	GDK_THREADS_ENTER ();

	song_update_statistics (view);
	
	rb_library_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));

	GDK_THREADS_LEAVE ();
}

static RBNode *
rb_library_view_get_previous_node (RBLibraryView *view,
				   gboolean just_check)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->songs);
	else
	{
		if (just_check == FALSE)
			node = rb_node_view_get_previous_random_node (view->priv->songs);
		else
			node = rb_node_view_get_first_node (view->priv->songs);
	}

	return node;
}

static RBNode *
rb_library_view_get_first_node (RBLibraryView *view)
{
	RBNode *node;

	if (view->priv->shuffle == FALSE)
	{
		GList *sel = rb_node_view_get_selection (view->priv->songs);

		if (sel == NULL)
			node = rb_node_view_get_first_node (view->priv->songs);
		else
		{
			GList *first = g_list_first (sel);
			node = RB_NODE (first->data);
		}
	}
	else
		node = rb_node_view_get_next_random_node (view->priv->songs);

	return node;
}

static RBNode *
rb_library_view_get_next_node (RBLibraryView *view,
			       gboolean just_check)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
	{
		node = rb_node_view_get_next_node (view->priv->songs);
		if (node == NULL && view->priv->repeat == TRUE)
		{
			node = rb_node_view_get_first_node (view->priv->songs);
		}
	}
	else
	{
		if (just_check == TRUE)
			node = rb_node_view_get_first_node (view->priv->songs);
		else
			node = rb_node_view_get_next_random_node (view->priv->songs);
	}

	return node;
}

static const char *
rb_library_view_status_get (RBViewStatus *status)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (status);

	g_free (view->priv->status);
	view->priv->status = rb_node_view_get_status (view->priv->songs);

	return (const char *) view->priv->status;
}

static gboolean
rb_library_view_can_cut (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_library_view_can_copy (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_LIBRARY_VIEW (clipboard)->priv->songs);
}

static gboolean
rb_library_view_can_paste (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_library_view_can_delete (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_LIBRARY_VIEW (clipboard)->priv->songs);
}

static GList *
rb_library_view_cut (RBViewClipboard *clipboard)
{
	return NULL;
}

static GList *
rb_library_view_copy (RBViewClipboard *clipboard)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (clipboard);

	return g_list_copy (rb_node_view_get_selection (view->priv->songs));
}

static void
rb_library_view_paste (RBViewClipboard *clipboard,
		       GList *nodes)
{
}

static void
rb_library_view_delete (RBViewClipboard *clipboard)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (clipboard);
	GList *sel, *l;

	sel = g_list_copy (rb_node_view_get_selection (view->priv->songs));
	for (l = sel; l != NULL; l = g_list_next (l))
	{
		rb_library_remove_node (view->priv->library, RB_NODE (l->data));
	}
	g_list_free (sel);
}

static void
rb_library_view_song_info (RBViewClipboard *clipboard)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (clipboard);
	GtkWidget *song_info = NULL;

	g_return_if_fail (view->priv->songs != NULL);

	song_info = rb_song_info_new (view->priv->songs);
	gtk_widget_show_all (song_info);
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBLibraryView *view)
{
	view->priv->paned_position = gtk_paned_get_position (GTK_PANED (view->priv->paned));
}

static void
rb_library_view_cmd_select_all (BonoboUIComponent *component,
				RBLibraryView *view,
				const char *verbname)
{
	rb_node_view_select_all (view->priv->songs);
}

static void
rb_library_view_cmd_select_none (BonoboUIComponent *component,
				 RBLibraryView *view,
				 const char *verbname)
{
	rb_node_view_select_none (view->priv->songs);
}

static void
rb_library_view_cmd_current_song (BonoboUIComponent *component,
				  RBLibraryView *view,
				  const char *verbname)
{
	RBNode *node = rb_node_view_get_playing_node (view->priv->songs);
	
	if (rb_node_view_get_node_visible (view->priv->songs, node) == FALSE)
	{
		/* adjust filtering to show it */
		rb_node_view_scroll_to_node (view->priv->artists,
					     rb_node_song_get_artist (RB_NODE_SONG (node)));
		rb_node_view_scroll_to_node (view->priv->albums,
					     rb_node_song_get_album (RB_NODE_SONG (node)));
	}

	rb_node_view_scroll_to_node (view->priv->songs, node);
}

static void
rb_library_view_show_browser_changed_cb (BonoboUIComponent *component,
					 const char *path,
					 Bonobo_UIComponent_EventType type,
					 const char *state,
					 RBLibraryView *view)
{
	if (view->priv->lock == TRUE)
		return;

	view->priv->show_browser = rb_view_get_active (RB_VIEW (view), CMD_PATH_SHOW_BROWSER);

	eel_gconf_set_boolean (CONF_STATE_SHOW_BROWSER, view->priv->show_browser);

	rb_library_view_show_browser (view, view->priv->show_browser);
}

static void
rb_library_view_show_browser (RBLibraryView *view,
			      gboolean show)
{
	view->priv->lock = TRUE;

	rb_view_set_active (RB_VIEW (view), CMD_PATH_SHOW_BROWSER, show);

	view->priv->lock = FALSE;

	if (show)
		gtk_widget_show (view->priv->browser);
	else
		gtk_widget_hide (view->priv->browser);
}

static void
rb_library_view_drop_cb (GtkWidget *widget,
			 GdkDragContext *context,
			 gint x,
			 gint y,
			 GtkSelectionData *data,
			 guint info,
			 guint time,
			 gpointer user_data)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (user_data);
	GList *list, *uri_list, *i;
	GtkTargetList *tlist;
	gboolean ret;

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
	{
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));
	}
	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = uri_list; i != NULL; i = i->next)
	{
		char *uri = i->data;

		if (uri != NULL)
		{
			rb_library_add_uri (view->priv->library, uri);
		}

		g_free (uri);
	}

	g_list_free (uri_list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static const char *
impl_get_description (RBView *view)
{
	return _("Library");
}

static GList *
impl_get_selection (RBView *view)
{
	RBLibraryView *library_view = RB_LIBRARY_VIEW (view);

	return rb_node_view_get_selection (library_view->priv->songs);
}

static void
rb_library_view_node_removed_cb (RBNode *node,
				 RBLibraryView *view)
{
	rb_library_view_set_playing_node (view, NULL);
}

static void
rb_library_view_search_cb (RBSearchEntry *search,
			   const char *search_text,
			   RBLibraryView *view)
{
	GDK_THREADS_ENTER ();

	/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0)
	{
		rb_node_view_select_node (view->priv->genres,
				          rb_library_get_all_artists (view->priv->library));
	}
	else
	{
		rb_node_view_select_none (view->priv->genres);
		rb_node_view_select_none (view->priv->artists);
		rb_node_view_select_none (view->priv->albums);

		artists_filter (view, rb_library_get_all_artists (view->priv->library));
		albums_filter (view, rb_library_get_all_artists (view->priv->library),
			             rb_library_get_all_albums (view->priv->library));

		rb_node_filter_empty (view->priv->songs_filter);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_SONG_PROP_ARTIST,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_SONG_PROP_ALBUM,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_SONG_PROP_GENRE,
									      search_text),
					       0);

		rb_node_filter_done_changing (view->priv->songs_filter);
	}

	GDK_THREADS_LEAVE ();
}

static void
artists_filter (RBLibraryView *view,
	        RBNode *genre)
{
	rb_node_filter_empty (view->priv->artists_filter);
	rb_node_filter_add_expression (view->priv->artists_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_albums (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->artists_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      genre),
				       0);
	rb_node_filter_done_changing (view->priv->artists_filter);
}

static void
albums_filter (RBLibraryView *view,
	       RBNode *genre,
	       RBNode *artist)
{
	rb_node_filter_empty (view->priv->albums_filter);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_CHILD_PROP_EQUALS,
								      RB_NODE_SONG_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (view->priv->library)),
				       1);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      artist),
				       1);
	rb_node_filter_done_changing (view->priv->albums_filter);
}

static void
songs_filter (RBLibraryView *view,
	      RBNode *genre,
	      RBNode *artist,
	      RBNode *album)
{
	rb_node_filter_empty (view->priv->songs_filter);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_SONG_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      artist, rb_library_get_all_albums (view->priv->library)),
				       1);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_SONG_PROP_REAL_ARTIST, artist),
				       1);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      album, rb_library_get_all_songs (view->priv->library)),
				       2);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_SONG_PROP_REAL_ALBUM, album),
				       2);
	rb_node_filter_done_changing (view->priv->songs_filter);
}
