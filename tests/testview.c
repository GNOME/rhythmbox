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

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-player.h"
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
static void album_node_activated_cb (RBNodeView *view,
			             RBNode *node,
			             RBTestView *testview);
static void artist_node_activated_cb (RBNodeView *view,
			              RBNode *node,
			              RBTestView *testview);
static void rb_test_view_player_init (RBViewPlayerIface *iface);
static RBViewPlayerResult rb_test_view_get_shuffle (RBViewPlayer *player);
static RBViewPlayerResult rb_test_view_get_repeat (RBViewPlayer *player);
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
static GdkPixbuf *rb_test_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_test_view_get_stream (RBViewPlayer *player);
static void rb_test_view_start_playing (RBViewPlayer *player);
static void rb_test_view_stop_playing (RBViewPlayer *player);

struct RBTestViewPrivate
{
	Library *library;

	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	GtkWidget *vbox;
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
					  G_CALLBACK (artist_node_activated_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (hbox), GTK_WIDGET (view->priv->artists));
			view->priv->albums = rb_node_view_new (library_get_all_albums (view->priv->library),
						               rb_file ("rb-node-view-albums.xml"));
			g_signal_connect (G_OBJECT (view->priv->albums),
					  "node_selected",
					  G_CALLBACK (album_node_activated_cb),
					  view);
			gtk_box_pack_start_defaults (GTK_BOX (hbox), GTK_WIDGET (view->priv->albums));
			gtk_paned_add1 (GTK_PANED (vpaned), hbox);

			view->priv->songs = rb_node_view_new (library_get_all_songs (view->priv->library),
						              rb_file ("rb-node-view-songs.xml"));
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
artist_node_activated_cb (RBNodeView *view,
			  RBNode *node,
			  RBTestView *testview)
{
	rb_node_view_set_filter_root (testview->priv->albums, node);
}

static void
album_node_activated_cb (RBNodeView *view,
			 RBNode *node,
			 RBTestView *testview)
{
	rb_node_view_set_filter_root (testview->priv->songs, node);
}

static void
rb_test_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_get_shuffle   = rb_test_view_get_shuffle;
	iface->impl_get_repeat    = rb_test_view_get_repeat;
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
	iface->impl_get_pixbuf    = rb_test_view_get_pixbuf;
	iface->impl_get_stream    = rb_test_view_get_stream;
	iface->impl_start_playing = rb_test_view_start_playing;
	iface->impl_stop_playing  = rb_test_view_stop_playing;
}

static RBViewPlayerResult
rb_test_view_get_shuffle (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_NOT_SUPPORTED;
}

static RBViewPlayerResult
rb_test_view_get_repeat (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_NOT_SUPPORTED;
}

static void
rb_test_view_set_shuffle (RBViewPlayer *player,
			  gboolean shuffle)
{
}

static void
rb_test_view_set_repeat (RBViewPlayer *player,
			 gboolean repeat)
{
}

static RBViewPlayerResult
rb_test_view_have_next (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_NOT_SUPPORTED;
}

static RBViewPlayerResult
rb_test_view_have_previous (RBViewPlayer *player)
{
	return RB_VIEW_PLAYER_NOT_SUPPORTED;
}

static void
rb_test_view_next (RBViewPlayer *player)
{
}

static void
rb_test_view_previous (RBViewPlayer *player)
{
}

static const char *
rb_test_view_get_title (RBViewPlayer *player)
{
	return g_strdup ("Hadjaha!");
}

static const char *
rb_test_view_get_artist (RBViewPlayer *player)
{
	return NULL;
}

static const char *
rb_test_view_get_album (RBViewPlayer *player)
{
	return NULL;
}

static const char *
rb_test_view_get_song (RBViewPlayer *player)
{
	return NULL;
}

static GdkPixbuf *
rb_test_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_test_view_get_stream (RBViewPlayer *player)
{
	return NULL;
}

static void
rb_test_view_start_playing (RBViewPlayer *player)
{
}

static void
rb_test_view_stop_playing (RBViewPlayer *player)
{
}
