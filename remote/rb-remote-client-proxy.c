/* 
 * arch-tag: Implementation of Rhythmbox remote client proxy interface
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

#include <config.h>
#include "rb-remote-client-proxy.h"

static void rb_remote_client_proxy_base_init (gpointer g_class);

GType
rb_remote_client_proxy_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo rb_remote_client_proxy_info = {
			sizeof (RBRemoteClientProxyIface), /* class_size */
			rb_remote_client_proxy_base_init,   /* base_init */
			NULL,		/* base_finalize */
			NULL,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			0,
			0,              /* n_preallocs */
			NULL
		};
		
		type = g_type_register_static (G_TYPE_INTERFACE, "RBRemoteClientProxy",
					       &rb_remote_client_proxy_info, 0);

		g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
	}

	return type;
}

static void
rb_remote_client_proxy_base_init (gpointer g_class)
{
	static gboolean initialized = FALSE;
	
	if (!initialized) {
		initialized = TRUE;
	}
}

void
rb_remote_client_proxy_handle_uri (RBRemoteClientProxy *proxy, const char *uri)
{
	(* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->handle_uri) (proxy, uri);
}

RBRemoteSong *
rb_remote_client_proxy_get_playing_song (RBRemoteClientProxy *proxy)
{
	return (* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->get_playing_song) (proxy);
}

void
rb_remote_client_proxy_grab_focus (RBRemoteClientProxy *proxy)
{
	(* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->grab_focus) (proxy);
}

void
rb_remote_client_proxy_toggle_shuffle (RBRemoteClientProxy *proxy)
{
	(* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->toggle_shuffle) (proxy);
}

void
rb_remote_client_proxy_toggle_playing (RBRemoteClientProxy *proxy)
{
	(* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->toggle_playing) (proxy);
}

long
rb_remote_client_proxy_get_playing_time (RBRemoteClientProxy *proxy)
{
	return (* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->get_playing_time) (proxy);
}
void
rb_remote_client_proxy_set_playing_time (RBRemoteClientProxy *proxy, long time)
{
	(* RB_REMOTE_CLIENT_PROXY_GET_IFACE (proxy)->set_playing_time) (proxy, time);
}
