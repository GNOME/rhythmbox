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

#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <libgnome/gnome-i18n.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-player.h"
#include "rb-dialog.h"
#include "testview.h"

static void rb_test_view_class_init (RBTestViewClass *klass);
static void rb_test_view_init (RBTestView *view);
static void rb_test_view_finalize (GObject *object);
static void rb_test_view_set_property (GObject *object,
			               guint prop_id,
			               const GValue *value,
			               GParamSpec *pspec);
static void rb_test_view_get_property (GObject *object,
			               guint prop_id,
			               GValue *value,
			               GParamSpec *pspec);
static void album_node_selected_cb (RBNodeView *view,
			            RBNode *node,
			            RBTestView *testview);
static void artist_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBTestView *testview);
static void rb_test_view_player_init (RBViewPlayerIface *iface);
static void rb_test_view_set_shuffle (RBViewPlayer *player,
			              gboolean shuffle);
static void rb_test_view_set_repeat (RBViewPlayer *player,
			             gboolean repeat);
static RBViewPlayerResult rb_test_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_test_view_have_previous (RBViewPlayer *player);
static void rb_test_view_next (RBViewPlayer *player);
static void rb_test_view_previous (RBViewPlayer *player);
static const char *rb_test_view_get_title (RBViewPlayer *player);
static const char *rb_test_view_get_artist (RBViewPlayer *player);
static const char *rb_test_view_get_album (RBViewPlayer *player);
static const char *rb_test_view_get_song (RBViewPlayer *player);
static long rb_test_view_get_duration (RBViewPlayer *player);
static GdkPixbuf *rb_test_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_test_view_get_stream (RBViewPlayer *player);
static void rb_test_view_start_playing (RBViewPlayer *player);
static void rb_test_view_stop_playing (RBViewPlayer *player);
static void rb_test_view_set_playing_node (RBTestView *view,
			                   RBNode *node);
static void song_activated_cb (RBNodeView *view,
		               RBNode *node,
		               RBTestView *test_view);
static void node_view_changed_cb (RBNodeView *view,
		                  RBTestView *test_view);
static void song_eos_cb (MonkeyMediaStream *stream,
	                 RBTestView *view);
static RBNode *rb_test_view_get_previous_node (RBTestView *view);
static RBNode *rb_test_view_get_next_node (RBTestView *view);

struct RBTestViewPrivate
{
	Library *library;

	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	GtkWidget *vbox;

	MonkeyMediaAudioStream *playing_stream;

	char *title;

	gboolean shuffle;
	gboolean repeat;
};

enum
{
	PROP_0,
	PROP_LIBRARY
};

static GObjectClass *parent_class = NULL;

GType
rb_test_view_get_type (void)
{
	static GType rb_test_view_type = 0;

	if (rb_test_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTestViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_test_view_class_init,
			NULL,
			NULL,
			sizeof (RBTestView),
			0,
			(GInstanceInitFunc) rb_test_view_init
		};

		static const GInterfaceInfo player_info =
		{
			(GInterfaceInitFunc) rb_test_view_player_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo clipboard_info =
		{
			NULL,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo status_info =
		{
			NULL,
			NULL,
			NULL
		};

		rb_test_view_type = g_type_register_static (RB_TYPE_VIEW,
							    "RBTestView",
							    &our_info, 0);
		
		g_type_add_interface_static (rb_test_view_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_test_view_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_test_view_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_test_view_type;
}

static void
rb_test_view_class_init (RBTestViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_test_view_finalize;

	object_class->set_property = rb_test_view_set_property;
	object_class->get_property = rb_test_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_test_view_init (RBTestView *view)
{
	RBSidebarButton *button;
	RBPlayer *player;
	GtkWidget *align;
	
	view->priv = g_new0 (RBTestViewPrivate, 1);

	button = rb_sidebar_button_new ("RbLibraryView");
	rb_sidebar_button_set (button,
			       RB_STOCK_LIBRARY,
			       "Library",
			       TRUE);
	g_object_set_data (G_OBJECT (button), "view", view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

	align = gtk_alignment_new (0.0, 0.5, 1.0, 1.0);
	player = rb_player_new (RB_VIEW_PLAYER (view));
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (player));
	gtk_box_pack_start (GTK_BOX (view->priv->vbox),
			    align,
			    FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (view->priv->vbox),
			    GTK_WIDGET (rb_search_entry_new ()),
			    FALSE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);
}

static void
rb_test_view_finalize (GObject *object)
{
	RBTestView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TEST_VIEW (object));

	view = RB_TEST_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv->title);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_test_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBTestView *view = RB_TEST_VIEW (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		{
			GtkWidget *vpaned;
			GtkWidget *hbox;

			view->priv->library = g_value_get_object (value);

			vpaned = gtk_vpaned_new ();

			hbox = gtk_hbox_new (TRUE, 5);
			view->priv->artists = rb_node_view_new (library_get_root (view->priv->library),
						                rb_file ("rb-node-view-artists.xml"));
			g_signal_connect (G_OBJECT (view->priv->artists),
					  "node_selected",
					  G_CALLBACK (artist_node_selected_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (hbox), GTK_WIDGET (view->priv->artists));
			view->priv->albums = rb_node_view_new (library_get_all_albums (view->priv->library),
						               rb_file ("rb-node-view-albums.xml"));
			g_signal_connect (G_OBJECT (view->priv->albums),
					  "node_selected",
					  G_CALLBACK (album_node_selected_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (hbox), GTK_WIDGET (view->priv->albums));
			gtk_paned_add1 (GTK_PANED (vpaned), hbox);

			view->priv->songs = rb_node_view_new (library_get_all_songs (view->priv->library),
						              rb_file ("rb-node-view-songs.xml"));
			g_signal_connect (G_OBJECT (view->priv->songs),
					  "node_activated",
					  G_CALLBACK (song_activated_cb),
					  view);
			g_signal_connect (G_OBJECT (view->priv->songs),
					  "changed",
					  G_CALLBACK (node_view_changed_cb),
					  view);
			gtk_paned_add2 (GTK_PANED (vpaned), GTK_WIDGET (view->priv->songs));

			gtk_box_pack_start_defaults (GTK_BOX (view->priv->vbox), vpaned);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_test_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBTestView *view = RB_TEST_VIEW (object);

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
rb_test_view_new (BonoboUIComponent *component, Library *library)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_TEST_VIEW,
				      "ui-file", "rhythmbox-test-view.xml",
				      "ui-name", "TestView",
				      "component", component,
				      "library", library,
				      NULL));

	return view;
}

static void
artist_node_selected_cb (RBNodeView *view,
			 RBNode *node,
			 RBTestView *testview)
{
	rb_node_view_set_filter_root (testview->priv->albums, node);
}

static void
album_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBTestView *testview)
{
	rb_node_view_set_filter_root (testview->priv->songs, node);
}

static void
rb_test_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle   = rb_test_view_set_shuffle;
	iface->impl_set_repeat    = rb_test_view_set_repeat;
	iface->impl_have_next     = rb_test_view_have_next;
	iface->impl_have_previous = rb_test_view_have_previous;
	iface->impl_next          = rb_test_view_next;
	iface->impl_previous      = rb_test_view_previous;
	iface->impl_get_title     = rb_test_view_get_title;
	iface->impl_get_artist    = rb_test_view_get_artist;
	iface->impl_get_album     = rb_test_view_get_album;
	iface->impl_get_song      = rb_test_view_get_song;
	iface->impl_get_duration  = rb_test_view_get_duration;
	iface->impl_get_pixbuf    = rb_test_view_get_pixbuf;
	iface->impl_get_stream    = rb_test_view_get_stream;
	iface->impl_start_playing = rb_test_view_start_playing;
	iface->impl_stop_playing  = rb_test_view_stop_playing;
}

static void
rb_test_view_set_shuffle (RBViewPlayer *player,
			  gboolean shuffle)
{
	RBTestView *view = RB_TEST_VIEW (player);

	view->priv->shuffle = shuffle;
}

static void
rb_test_view_set_repeat (RBViewPlayer *player,
			 gboolean repeat)
{
	RBTestView *view = RB_TEST_VIEW (player);

	view->priv->repeat = repeat;
}

static RBViewPlayerResult
rb_test_view_have_next (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *next;

	next = rb_test_view_get_next_node (view);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_test_view_have_previous (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *previous;

	previous = rb_test_view_get_previous_node (view);

	return (previous != NULL);
}

static void
rb_test_view_next (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_test_view_get_next_node (view);
	
	rb_test_view_set_playing_node (view, node);
}

static void
rb_test_view_previous (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_test_view_get_previous_node (view);
	
	rb_test_view_set_playing_node (view, node);
}

static const char *
rb_test_view_get_title (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);

	return (const char *) view->priv->title;
}

static const char *
rb_test_view_get_artist (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
	{
		node = rb_node_get_grandparent (node);
		return rb_node_get_string_property (node, NODE_PROPERTY_NAME);
	}
	else
		return NULL;
}

static const char *
rb_test_view_get_album (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
	{
		node = rb_node_get_parent (node);
		return rb_node_get_string_property (node, NODE_PROPERTY_NAME);
	}
	else
		return NULL;
}

static const char *
rb_test_view_get_song (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
	{
		return rb_node_get_string_property (node, NODE_PROPERTY_NAME);
	}
	else
		return NULL;
}

static long
rb_test_view_get_duration (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
	{
		return rb_node_get_int_property (node, SONG_PROPERTY_DURATION);
	}
	else
		return -1;
}

static GdkPixbuf *
rb_test_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_test_view_get_stream (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);

	return view->priv->playing_stream;
}

static void
rb_test_view_start_playing (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_first_node (view->priv->songs);

	rb_test_view_set_playing_node (view, node);
}

static void
rb_test_view_stop_playing (RBViewPlayer *player)
{
	RBTestView *view = RB_TEST_VIEW (player);

	rb_test_view_set_playing_node (view, NULL);
}

static void
rb_test_view_set_playing_node (RBTestView *view,
			       RBNode *node)
{
	rb_node_view_set_playing_node (view->priv->songs, node);

	g_free (view->priv->title);

	if (node == NULL)
	{
		view->priv->playing_stream = NULL;

		view->priv->title = NULL;
	}
	else
	{
		GError *error = NULL;
		const char *uri = rb_node_get_string_property (node, SONG_PROPERTY_URI);
		const char *artist = rb_node_get_string_property (rb_node_get_grandparent (node), NODE_PROPERTY_NAME);
		const char *song = rb_node_get_string_property (node, NODE_PROPERTY_NAME);

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
		
		/* FIXME shorten when necessary */
		view->priv->title = g_strdup_printf ("%s - %s", artist, song);
	}
}

static void
song_activated_cb (RBNodeView *view,
		   RBNode *node,
		   RBTestView *test_view)
{
	rb_test_view_set_playing_node (test_view, node);

	rb_view_player_notify_changed (RB_VIEW_PLAYER (test_view));
	rb_view_player_notify_playing (RB_VIEW_PLAYER (test_view));
}

static void
node_view_changed_cb (RBNodeView *view,
		      RBTestView *test_view)
{

	rb_view_player_notify_changed (RB_VIEW_PLAYER (test_view));
}

static void
song_eos_cb (MonkeyMediaStream *stream,
	     RBTestView *view)
{
	rb_test_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));
}

static RBNode *
rb_test_view_get_previous_node (RBTestView *view)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->songs);
	else
		node = rb_node_view_get_random_node (view->priv->songs);

	return node;
}

static RBNode *
rb_test_view_get_next_node (RBTestView *view)
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
