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

#include "rb-view-player.h"

static void rb_view_player_base_init (gpointer g_class);

enum
{
	CHANGED,
	START_PLAYING,
	LAST_SIGNAL
};

static guint rb_view_player_signals[LAST_SIGNAL] = { 0 };

GType
rb_view_player_get_type (void)
{
	static GType rb_view_player_type = 0;

	if (rb_view_player_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBViewPlayerIface),
			rb_view_player_base_init,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		rb_view_player_type = g_type_register_static (G_TYPE_INTERFACE,
							      "RBViewPlayer",
							      &our_info, 0);
		g_type_interface_add_prerequisite (rb_view_player_type, G_TYPE_OBJECT);
	}

	return rb_view_player_type;
}

static void
rb_view_player_base_init (gpointer g_iface)
{
	static gboolean initialized = FALSE;

	if (initialized == TRUE)
		return;

	rb_view_player_signals[CHANGED] =
		g_signal_new ("changed",
			      RB_TYPE_VIEW_PLAYER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBViewPlayerIface, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_view_player_signals[START_PLAYING] =
		g_signal_new ("start_playing",
			      RB_TYPE_VIEW_PLAYER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBViewPlayerIface, start_playing),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	initialized = TRUE;
}

void
rb_view_player_set_shuffle (RBViewPlayer *player,
                            gboolean shuffle)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_set_shuffle (player, shuffle);

	rb_view_player_notify_changed (player);
}

void
rb_view_player_set_repeat (RBViewPlayer *player,
			   gboolean repeat)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_set_repeat (player, repeat);

	rb_view_player_notify_changed (player);
}

RBViewPlayerResult
rb_view_player_have_first (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_have_first (player);
}

RBViewPlayerResult
rb_view_player_have_next (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_have_next (player);
}

RBViewPlayerResult
rb_view_player_have_previous (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_have_previous (player);
}

void
rb_view_player_next (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_next (player);

	rb_view_player_notify_changed (player);
}

void
rb_view_player_previous (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_previous (player);

	rb_view_player_notify_changed (player);
}

const char *
rb_view_player_get_title (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_title (player);
}

const char *
rb_view_player_get_artist (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_artist (player);
}

const char *
rb_view_player_get_album (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_album (player);
}

const char *
rb_view_player_get_song (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_song (player);
}

long
rb_view_player_get_duration (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_duration (player);
}

GdkPixbuf *
rb_view_player_get_pixbuf (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_pixbuf (player);
}

MonkeyMediaAudioStream *
rb_view_player_get_stream (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	return iface->impl_get_stream (player);
}

void
rb_view_player_start_playing (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_start_playing (player);

	rb_view_player_notify_changed (player);
}

void
rb_view_player_stop_playing (RBViewPlayer *player)
{
	RBViewPlayerIface *iface = RB_VIEW_PLAYER_GET_IFACE (player);

	iface->impl_stop_playing (player);

	rb_view_player_notify_changed (player);
}

void
rb_view_player_notify_changed (RBViewPlayer *player)
{
	g_signal_emit (G_OBJECT (player), rb_view_player_signals[CHANGED], 0);
}

void
rb_view_player_notify_playing (RBViewPlayer *player)
{
	g_signal_emit (G_OBJECT (player), rb_view_player_signals[START_PLAYING], 0);
}
