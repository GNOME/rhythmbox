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

#include <gtk/gtklabel.h>

#include "rb-stock-icons.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "testview2.h"

static void rb_test_view2_class_init (RBTestView2Class *klass);
static void rb_test_view2_init (RBTestView2 *view);
static void rb_test_view2_finalize (GObject *object);
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

struct RBTestView2Private
{
	gpointer bla;
};

static GObjectClass *parent_class = NULL;

GType
rb_test_view2_get_type (void)
{
	static GType rb_test_view2_type = 0;

	if (rb_test_view2_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTestView2Class),
			NULL,
			NULL,
			(GClassInitFunc) rb_test_view2_class_init,
			NULL,
			NULL,
			sizeof (RBTestView2),
			0,
			(GInstanceInitFunc) rb_test_view2_init
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

		rb_test_view2_type = g_type_register_static (RB_TYPE_VIEW,
							    "RBTestView2",
							    &our_info, 0);

		g_type_add_interface_static (rb_test_view2_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_test_view2_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_test_view2_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_test_view2_type;
}

static void
rb_test_view2_class_init (RBTestView2Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_test_view2_finalize;
}

static void
rb_test_view2_init (RBTestView2 *view)
{
	RBSidebarButton *button;
	char *id;
	static int counter = 0;
	
	view->priv = g_new0 (RBTestView2Private, 1);

	gtk_container_add (GTK_CONTAINER (view), gtk_label_new ("WHAHAheeeeja!"));

	id = g_strdup_printf ("%d", counter);
	counter++;
	button = rb_sidebar_button_new (id);
	rb_sidebar_button_set (button,
			       RB_STOCK_PLAYLIST,
			       "Playlist",
			       FALSE);
	g_object_set_data (G_OBJECT (button), "view", view);
	g_free (id);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);
}

static void
rb_test_view2_finalize (GObject *object)
{
	RBTestView2 *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TEST_VIEW (object));

	view = RB_TEST_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBView *
rb_test_view2_new (BonoboUIComponent *component)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_TEST_VIEW,
				      "ui-file", "rhythmbox-test-view-2.xml",
				      "ui-name", "TestView2",
				      "component", component,
				      NULL));

	return view;
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
	return NULL;
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
