/* 
 * arch-tag: Implementation of Rhythmbox remote proxy interface
 *
 * Copyright (C) 2004 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "rb-remote-proxy.h"
#include <config.h>

enum {
	SONG_CHANGED,
	LAST_SIGNAL
};

static guint rb_remote_proxy_signals[LAST_SIGNAL] = { 0 };

static void rb_remote_proxy_base_init (gpointer g_class);

GType
rb_remote_proxy_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo rb_remote_proxy_info = {
			sizeof (RBRemoteProxyIface), /* class_size */
			rb_remote_proxy_base_init,   /* base_init */
			NULL,		/* base_finalize */
			NULL,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			0,
			0,              /* n_preallocs */
			NULL
		};
		
		type = g_type_register_static (G_TYPE_INTERFACE, "RBRemoteProxy",
					       &rb_remote_proxy_info, 0);

		g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
	}

	return type;
}

static void
rb_remote_proxy_base_init (gpointer g_class)
{
	static gboolean initialized = FALSE;
	
	if (!initialized) {
		rb_remote_proxy_signals[SONG_CHANGED] =
			g_signal_new ("song_changed",
				      RB_TYPE_REMOTE_PROXY,
				      G_SIGNAL_RUN_LAST,
				      G_STRUCT_OFFSET (RBRemoteProxyIface, song_changed),
				      NULL, NULL,
				      g_cclosure_marshal_VOID__POINTER,
				      G_TYPE_NONE, 1,
				      G_TYPE_POINTER);

		initialized = TRUE;
	}
}

void
rb_remote_proxy_load_song (RBRemoteProxy *proxy, const char *uri)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->load_song) (proxy, uri);
}

void
rb_remote_proxy_load_uri (RBRemoteProxy *proxy, const char *uri)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->load_uri) (proxy, uri);
}

void
rb_remote_proxy_select_uri (RBRemoteProxy *proxy, const char *uri)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->select_uri) (proxy, uri);
}

void
rb_remote_proxy_play_uri (RBRemoteProxy *proxy, const char *uri)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->play_uri) (proxy, uri);
}

void
rb_remote_proxy_grab_focus (RBRemoteProxy *proxy)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->grab_focus) (proxy);
}

gboolean
rb_remote_proxy_get_shuffle (RBRemoteProxy *proxy)
{
	return (* RB_REMOTE_PROXY_GET_IFACE (proxy)->get_shuffle) (proxy);
}

void
rb_remote_proxy_set_shuffle (RBRemoteProxy *proxy, gboolean shuffle)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->set_shuffle) (proxy, shuffle);
}

gboolean
rb_remote_proxy_get_repeat (RBRemoteProxy *proxy)
{
	return (* RB_REMOTE_PROXY_GET_IFACE (proxy)->get_repeat) (proxy);
}

void
rb_remote_proxy_set_repeat (RBRemoteProxy *proxy, gboolean repeat)
{
	return (* RB_REMOTE_PROXY_GET_IFACE (proxy)->set_repeat) (proxy, repeat);
}

void
rb_remote_proxy_play (RBRemoteProxy *proxy)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->play) (proxy);
}

void
rb_remote_proxy_pause (RBRemoteProxy *proxy)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->pause) (proxy);
}

gboolean
rb_remote_proxy_playing (RBRemoteProxy *proxy)
{
	return (* RB_REMOTE_PROXY_GET_IFACE (proxy)->playing) (proxy);
}

long
rb_remote_proxy_get_playing_time (RBRemoteProxy *proxy)
{
	return (* RB_REMOTE_PROXY_GET_IFACE (proxy)->get_playing_time) (proxy);
}

void
rb_remote_proxy_set_playing_time (RBRemoteProxy *proxy, long time)
{
	(* RB_REMOTE_PROXY_GET_IFACE (proxy)->set_playing_time) (proxy, time);
}
