/* 
 * arch-tag: Header for Rhythmbox remote client proxy interface
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
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __RB_REMOTE_CLIENT_PROXY_H__
#define __RB_REMOTE_CLIENT_PROXY_H__

#include <glib-object.h>
#include "rb-remote-common.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOTE_CLIENT_PROXY (rb_remote_client_proxy_get_type ())
#define RB_REMOTE_CLIENT_PROXY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_REMOTE_CLIENT_PROXY, RBRemoteClientProxy))
#define RB_IS_REMOTE_CLIENT_PROXY(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_REMOTE_CLIENT_PROXY))
#define RB_REMOTE_CLIENT_PROXY_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_REMOTE_CLIENT_PROXY, RBRemoteClientProxyIface))

typedef struct _RBRemoteClientProxy        RBRemoteClientProxy; /* Dummy typedef */
typedef struct _RBRemoteClientProxyIface RBRemoteClientProxyIface;

struct _RBRemoteClientProxy
{
	int dummy;
};

struct _RBRemoteClientProxyIface
{
	GTypeInterface g_iface;
	
	/* Methods */
	void (*handle_uri) (RBRemoteClientProxy *proxy, const char *uri);

	RBRemoteSong *(*get_playing_song) (RBRemoteClientProxy *proxy);

	void (*grab_focus) (RBRemoteClientProxy *proxy);

	void (*toggle_shuffle) (RBRemoteClientProxy *proxy);

	void (*toggle_playing) (RBRemoteClientProxy *proxy);

	long (*get_playing_time) (RBRemoteClientProxy *proxy);
	void (*set_playing_time) (RBRemoteClientProxy *proxy, long time);
};

GType rb_remote_client_proxy_get_type (void) G_GNUC_CONST;

void rb_remote_client_proxy_handle_uri (RBRemoteClientProxy *proxy, const char *uri);

RBRemoteSong *rb_remote_client_proxy_get_playing_song (RBRemoteClientProxy *proxy);

void rb_remote_client_proxy_grab_focus (RBRemoteClientProxy *proxy);

void rb_remote_client_proxy_toggle_shuffle (RBRemoteClientProxy *proxy);

void rb_remote_client_proxy_toggle_playing (RBRemoteClientProxy *proxy);

long rb_remote_client_proxy_get_playing_time (RBRemoteClientProxy *proxy);
void rb_remote_client_proxy_set_playing_time (RBRemoteClientProxy *proxy, long time);

G_END_DECLS

#endif /* __RB_REMOTE_CLIENT_PROXY_H__ */
