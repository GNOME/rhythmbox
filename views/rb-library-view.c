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
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <libgnome/gnome-i18n.h>
#include <bonobo/bonobo-ui-component.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-player.h"
#include "rb-dialog.h"
#include "rb-library-view.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-node-song.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"

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
			            RBLibraryView *testview);
static void artist_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBLibraryView *testview);
static void rb_library_view_player_init (RBViewPlayerIface *iface);
static void rb_library_view_set_shuffle (RBViewPlayer *player,
			                 gboolean shuffle);
static void rb_library_view_set_repeat (RBViewPlayer *player,
			                gboolean repeat);
static RBViewPlayerResult rb_library_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_library_view_have_previous (RBViewPlayer *player);
static void rb_library_view_next (RBViewPlayer *player);
static void rb_library_view_previous (RBViewPlayer *player);
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
static RBNode *rb_library_view_get_previous_node (RBLibraryView *view);
static RBNode *rb_library_view_get_next_node (RBLibraryView *view);
static void rb_library_view_status_init (RBViewStatusIface *iface);
static const char *rb_library_view_status_get (RBViewStatus *status);
static void rb_library_view_clipboard_init (RBViewClipboardIface *iface);
static gboolean rb_library_view_can_cut (RBViewClipboard *clipboard);
static gboolean rb_library_view_can_copy (RBViewClipboard *clipboard);
static gboolean rb_library_view_can_paste (RBViewClipboard *clipboard);
static GList *rb_library_view_cut (RBViewClipboard *clipboard);
static GList *rb_library_view_copy (RBViewClipboard *clipboard);
static void rb_library_view_paste (RBViewClipboard *clipboard,
		                   GList *nodes);
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
static void song_deleted_cb (RBNodeView *view,
		             RBNode *node,
		             RBLibraryView *library_view);
static void rb_library_view_cmd_song_info (BonoboUIComponent *component,
				           RBLibraryView *view,
				           const char *verbname);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_SONG "/commands/CurrentSong"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"

#define CONF_STATE_PANED_POSITION "/apps/rhythmbox/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   "/apps/rhythmbox/state/library/show_browser"

struct RBLibraryViewPrivate
{
	RBLibrary *library;

	GtkWidget *browser;
	GtkWidget *vbox;

	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	MonkeyMediaAudioStream *playing_stream;

	char *title;

	gboolean shuffle;
	gboolean repeat;

	RBPlayer *player;
	RBVolume *volume;

	char *status;

	GtkWidget *paned;
	int paned_position;

	gboolean show_browser;
	gboolean lock;

	char *artist;
	char *album;
	char *song;
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
	BONOBO_UI_VERB ("SongInfo",    (BonoboUIVerbFn) rb_library_view_cmd_song_info),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_library_view_listeners[] = 
{
	RB_BONOBO_UI_LISTENER ("ShowBrowser", (BonoboUIListenerFn) rb_library_view_show_browser_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class = NULL;

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

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_view_finalize;

	object_class->set_property = rb_library_view_set_property;
	object_class->get_property = rb_library_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_library_view_init (RBLibraryView *view)
{
	RBSidebarButton *button;
	GtkWidget *hbox, *align;
	
	view->priv = g_new0 (RBLibraryViewPrivate, 1);

	button = rb_sidebar_button_new ("RbLibraryView",
					_("library"));
	rb_sidebar_button_set (button,
			       RB_STOCK_LIBRARY,
			       _("Library"),
			       TRUE);
	g_object_set_data (G_OBJECT (button), "view", view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

	view->priv->player = rb_player_new (RB_VIEW_PLAYER (view));
	hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (view->priv->player),
			    TRUE, TRUE, 0);

	view->priv->volume = rb_volume_new (RB_VOLUME_CHANNEL_PCM);
	align = gtk_alignment_new (0.0, 0.0, 1.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (view->priv->volume));
	gtk_box_pack_end (GTK_BOX (hbox), align, FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (view->priv->vbox),
			    hbox,
			    FALSE, TRUE, 0);

#if 0
	gtk_box_pack_start (GTK_BOX (view->priv->vbox),
			    GTK_WIDGET (rb_search_entry_new ()),
			    FALSE, TRUE, 0);
#endif
	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);
}

static void
rb_library_view_finalize (GObject *object)
{
	RBLibraryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_VIEW (object));

	view = RB_LIBRARY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv->title);
	g_free (view->priv->status);

	g_free (view->priv->album);
	g_free (view->priv->artist);
	g_free (view->priv->song);

	g_object_unref (G_OBJECT (view->priv->browser));

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
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
			g_object_ref (G_OBJECT (view->priv->browser));
			view->priv->artists = rb_node_view_new (rb_library_get_all_artists (view->priv->library),
						                rb_file ("rb-node-view-artists.xml"));
			g_signal_connect (G_OBJECT (view->priv->artists),
					  "node_selected",
					  G_CALLBACK (artist_node_selected_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->artists));
			view->priv->albums = rb_node_view_new (rb_library_get_all_albums (view->priv->library),
						               rb_file ("rb-node-view-albums.xml"));
			g_signal_connect (G_OBJECT (view->priv->albums),
					  "node_selected",
					  G_CALLBACK (album_node_selected_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->albums));
			gtk_paned_pack1 (GTK_PANED (view->priv->paned), view->priv->browser, FALSE, FALSE);
			
			view->priv->songs = rb_node_view_new (rb_library_get_all_songs (view->priv->library),
						              rb_file ("rb-node-view-songs.xml"));


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
					  "node_deleted",
					  G_CALLBACK (song_deleted_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->songs),
					  "changed",
					  G_CALLBACK (node_view_changed_cb),
					  view);
			gtk_paned_pack2 (GTK_PANED (view->priv->paned), GTK_WIDGET (view->priv->songs), FALSE, FALSE);

			gtk_box_pack_start_defaults (GTK_BOX (view->priv->vbox), view->priv->paned);

			view->priv->paned_position = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);
			view->priv->show_browser = eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER);

			gtk_paned_set_position (GTK_PANED (view->priv->paned), view->priv->paned_position);
			rb_library_view_show_browser (view, view->priv->show_browser);
			
			rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, FALSE);
			
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

static void
artist_node_selected_cb (RBNodeView *view,
			 RBNode *node,
			 RBLibraryView *testview)
{
	rb_node_view_set_filter (testview->priv->albums, node, NULL);
	rb_node_view_select_node (testview->priv->albums,
				  rb_library_get_all_songs (testview->priv->library));
}

static void
album_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibraryView *testview)
{
	GList *selection = rb_node_view_get_selection (testview->priv->artists);
	rb_node_view_set_filter (testview->priv->songs, node,
				 RB_NODE (selection->data));
}

static void
rb_library_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle   = rb_library_view_set_shuffle;
	iface->impl_set_repeat    = rb_library_view_set_repeat;
	iface->impl_have_next     = rb_library_view_have_next;
	iface->impl_have_previous = rb_library_view_have_previous;
	iface->impl_next          = rb_library_view_next;
	iface->impl_previous      = rb_library_view_previous;
	iface->impl_get_title     = rb_library_view_get_title;
	iface->impl_get_artist    = rb_library_view_get_artist;
	iface->impl_get_album     = rb_library_view_get_album;
	iface->impl_get_song      = rb_library_view_get_song;
	iface->impl_get_duration  = rb_library_view_get_duration;
	iface->impl_get_pixbuf    = rb_library_view_get_pixbuf;
	iface->impl_get_stream    = rb_library_view_get_stream;
	iface->impl_start_playing = rb_library_view_start_playing;
	iface->impl_stop_playing  = rb_library_view_stop_playing;
}

static void
rb_library_view_status_init (RBViewStatusIface *iface)
{
	iface->impl_get = rb_library_view_status_get;
}

static void
rb_library_view_clipboard_init (RBViewClipboardIface *iface)
{
	iface->impl_can_cut   = rb_library_view_can_cut;
	iface->impl_can_copy  = rb_library_view_can_copy;
	iface->impl_can_paste = rb_library_view_can_paste;
	iface->impl_cut       = rb_library_view_cut;
	iface->impl_copy      = rb_library_view_copy;
	iface->impl_paste     = rb_library_view_paste;
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
rb_library_view_have_next (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *next;

	next = rb_library_view_get_next_node (view);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_library_view_have_previous (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *previous;

	previous = rb_library_view_get_previous_node (view);

	return (previous != NULL);
}

static void
rb_library_view_next (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_library_view_get_next_node (view);
	
	rb_library_view_set_playing_node (view, node);
}

static void
rb_library_view_previous (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
	RBNode *node;

	node = rb_library_view_get_previous_node (view);
	
	rb_library_view_set_playing_node (view, node);
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
	{
		g_free (view->priv->artist);
		view->priv->artist = rb_node_song_get_artist (node);
		return (const char *) view->priv->artist;
	}
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
	{
		g_free (view->priv->album);
		view->priv->album = rb_node_song_get_album (node);
		return (const char *) view->priv->album;
	}
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
	{
		GValue value = { 0, };
		rb_node_get_property (node, "name", &value);
		g_free (view->priv->song);
		view->priv->song = g_strdup (g_value_get_string (&value));
		g_value_unset (&value);
		return (const char *) view->priv->song;
	}
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
	{
		GValue value = { 0, };
		long ret;
		rb_node_get_property (node, "duration_raw", &value);
		ret = g_value_get_long (&value);
		g_value_unset (&value);
		return ret;
	}
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

static void
rb_library_view_start_playing (RBViewPlayer *player)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (player);
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
		node = rb_node_view_get_random_node (view->priv->songs);

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
		GValue value = { 0, };

		rb_node_get_property (node, "location", &value);
		uri = g_value_get_string (&value);

		g_assert (uri != NULL);
		
		view->priv->playing_stream = monkey_media_audio_stream_new (uri, &error);
		if (error != NULL)
		{
			rb_error_dialog (_("Failed to create stream for %s, error was:\n%s"),
					 uri, error->message);
			g_error_free (error);
		}
		
		g_signal_connect (G_OBJECT (view->priv->playing_stream),
				  "end_of_stream",
				  G_CALLBACK (song_eos_cb),
				  view);
		
		view->priv->title = g_strdup_printf ("%s - %s", artist, song);
		
		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, TRUE);

		g_value_unset (&value);
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
song_deleted_cb (RBNodeView *view,
		 RBNode *node,
		 RBLibraryView *library_view)
{
	rb_library_remove_node (RB_LIBRARY (library_view->priv->library), node);
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
song_eos_cb (MonkeyMediaStream *stream,
	     RBLibraryView *view)
{
	rb_library_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));
}

static RBNode *
rb_library_view_get_previous_node (RBLibraryView *view)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->songs);
	else
		node = rb_node_view_get_random_node (view->priv->songs);

	return node;
}

static RBNode *
rb_library_view_get_next_node (RBLibraryView *view)
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
		node = rb_node_view_get_random_node (view->priv->songs);

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
	return rb_node_view_have_selection (RB_LIBRARY_VIEW (clipboard)->priv->songs);
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

static GList *
rb_library_view_cut (RBViewClipboard *clipboard)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (clipboard);
	GList *sel, *l;

	sel = g_list_copy (rb_node_view_get_selection (view->priv->songs));
	for (l = sel; l != NULL; l = g_list_next (l))
	{
		rb_library_remove_node (view->priv->library, RB_NODE (l->data));
	}
	g_list_free (sel);
	
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
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBLibraryView *view)
{
	view->priv->paned_position = gtk_paned_get_position (GTK_PANED (view->priv->paned));

	eel_gconf_set_integer (CONF_STATE_PANED_POSITION, view->priv->paned_position);
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
	rb_node_view_scroll_to_node (view->priv->songs,
				     rb_node_view_get_playing_node (view->priv->songs));
	rb_node_view_select_none (view->priv->songs);
	rb_node_view_select_node (view->priv->songs,
				  rb_node_view_get_playing_node (view->priv->songs));
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

	if (show == TRUE && view->priv->browser->parent != view->priv->paned)
	{
		gtk_paned_pack1 (GTK_PANED (view->priv->paned), view->priv->browser, FALSE, FALSE);
		gtk_widget_show_all (view->priv->browser);
	}
	else if (show == FALSE && view->priv->browser->parent == view->priv->paned)
	{
		gtk_widget_hide (view->priv->browser);
		gtk_container_remove (GTK_CONTAINER (view->priv->paned), view->priv->browser);
	}
}

static void
rb_library_view_cmd_song_info (BonoboUIComponent *component,
			       RBLibraryView *view,
			       const char *verbname)
{
	GList *selected_nodes = NULL;
	GtkWidget *song_info = NULL;

	g_return_if_fail (view->priv->songs != NULL);

	/* get the first node and show the song information dialog 
	 * TODO show a different dialog for multiple songs */
	selected_nodes = rb_node_view_get_selection (view->priv->songs);
	if ((selected_nodes != NULL) &&
	    (selected_nodes->data != NULL) &&
	    (RB_IS_NODE (selected_nodes->data)))
	{
		song_info = rb_song_info_new (RB_NODE (selected_nodes->data));
		gtk_widget_show_all (song_info);
	}
}
