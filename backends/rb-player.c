/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <jrl@ids.org.au>
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

#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-marshal.h"

/* Signals */
enum {
	EOS,
	INFO,
	BUFFERING,
	ERROR,
	TICK,
	EVENT,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rb_player_interface_init (RBPlayerIface *iface)
{
	signals[EOS] =
		g_signal_new ("eos",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerIface, eos),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[INFO] =
		g_signal_new ("info",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, info),
			      NULL, NULL,
			      rb_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE,
			      2, G_TYPE_INT, G_TYPE_VALUE);
	signals[ERROR] =
		g_signal_new ("error",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerIface, error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	signals[TICK] =
		g_signal_new ("tick",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, tick),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__LONG,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_LONG);
	signals[BUFFERING] =
		g_signal_new ("buffering",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, buffering),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_UINT);
	signals[EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      G_STRUCT_OFFSET (RBPlayerIface, event),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
}

GType
rb_player_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RBPlayerIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc)rb_player_interface_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RBPlayer", &our_info, 0);
	}

	return our_type;
}

gboolean
rb_player_open (RBPlayer *player, const char *uri, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->open (player, uri, error);
}

gboolean
rb_player_opened (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->opened (player);
}

gboolean
rb_player_close (RBPlayer *player, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->close (player, error);
}

gboolean
rb_player_play (RBPlayer *player, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->play (player, error);
}

void
rb_player_pause (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->pause (player);
}

gboolean
rb_player_playing (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->playing (player);
}

void
rb_player_set_volume (RBPlayer *player, float volume)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->set_volume (player, volume);
}

float
rb_player_get_volume (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->get_volume (player);
}

void
rb_player_set_replaygain (RBPlayer *player,
			  double track_gain, double track_peak,
			  double album_gain, double album_peak)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->set_replaygain (player, track_gain, track_peak, album_gain, album_peak);
}

gboolean
rb_player_seekable (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->seekable (player);
}

void
rb_player_set_time (RBPlayer *player, long time)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->set_time (player, time);
}

long
rb_player_get_time (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->get_time (player);
}

RBPlayer*
rb_player_new (GError **error)
{
	return rb_player_gst_new (error);
}

void
_rb_player_emit_eos (RBPlayer *player)
{
	g_signal_emit (player, signals[EOS], 0);
}

void
_rb_player_emit_info (RBPlayer *player,
		      RBMetaDataField field,
		      GValue *value)
{
	g_signal_emit (player, signals[INFO], 0, field, value);
}

void
_rb_player_emit_buffering (RBPlayer *player, guint progress)
{
	g_signal_emit (player, signals[BUFFERING], 0, progress);
}

void
_rb_player_emit_error (RBPlayer *player, GError *error)
{
	g_signal_emit (player, signals[ERROR], 0, error);
}

void
_rb_player_emit_tick (RBPlayer *player, long elapsed)
{
	g_signal_emit (player, signals[TICK], 0, elapsed);
}

void
_rb_player_emit_event (RBPlayer *player, const char *name, gpointer data)
{
	g_signal_emit (player, signals[EVENT], g_quark_from_string (name), data);
}

GQuark
rb_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_player_error");

	return quark;
}
