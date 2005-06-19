/* 
 * arch-tag: Header for Rhythmbox remote proxy interface
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

#ifndef __RB_REMOTE_PROXY_H__
#define __RB_REMOTE_PROXY_H__

#include <glib-object.h>
#include "rb-remote-common.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOTE_PROXY (rb_remote_proxy_get_type ())
#define RB_REMOTE_PROXY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_REMOTE_PROXY, RBRemoteProxy))
#define RB_IS_REMOTE_PROXY(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_REMOTE_PROXY))
#define RB_REMOTE_PROXY_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_REMOTE_PROXY, RBRemoteProxyIface))

typedef struct _RBRemoteProxy        RBRemoteProxy; /* Dummy typedef */
typedef struct _RBRemoteProxyIface RBRemoteProxyIface;

struct _RBRemoteProxy
{
	int dummy;
};

struct _RBRemoteProxyIface
{
	GTypeInterface g_iface;
	
	/* Signals */
	void (*song_changed) (RBRemoteProxy *proxy,
			      const RBRemoteSong *song);
	void (*visibility_changed) (RBRemoteProxy *proxy,
				    gboolean visible);

	/* Methods */
	void (*load_uri) (RBRemoteProxy *proxy, const char *uri, gboolean play);
	void (*select_uri) (RBRemoteProxy *proxy, const char *uri);
	void (*play_uri) (RBRemoteProxy *proxy, const char *uri);

	void (*grab_focus) (RBRemoteProxy *proxy);
	void (*set_visibility) (RBRemoteProxy *proxy, gboolean visible);
	gboolean (*get_visibility) (RBRemoteProxy *proxy);

	gboolean (*get_shuffle) (RBRemoteProxy *proxy);
	void (*set_shuffle) (RBRemoteProxy *proxy, gboolean shuffle);
	gboolean (*get_repeat) (RBRemoteProxy *proxy);
	void (*set_repeat) (RBRemoteProxy *proxy, gboolean repeat);

	void (*play) (RBRemoteProxy *proxy);
	void (*pause) (RBRemoteProxy *proxy);
	gboolean (*playing) (RBRemoteProxy *proxy);

	long (*get_playing_time) (RBRemoteProxy *proxy);
	void (*set_playing_time) (RBRemoteProxy *proxy, long time);
	void (*seek) (RBRemoteProxy *proxy, long offset);

	gchar* (*get_playing_uri) (RBRemoteProxy *proxy);
	gboolean (*get_song_info) (RBRemoteProxy *proxy, const gchar *uri, RBRemoteSong *song);
	void (*set_rating) (RBRemoteProxy *proxy, double rating);

	void (*jump_next) (RBRemoteProxy *proxy);
	void (*jump_previous) (RBRemoteProxy *proxy);

	void (*toggle_mute) (RBRemoteProxy *proxy);

	void (*quit) (RBRemoteProxy *proxy);

	GParamSpec *(*find_player_property) (RBRemoteProxy *proxy, const gchar *property);
	void (*player_notify_handler) (RBRemoteProxy *proxy, GCallback c_handler, gpointer gobject);
	void (*get_player_property) (RBRemoteProxy *proxy, const gchar *property, GValue *value);
	void (*set_player_property) (RBRemoteProxy *proxy, const gchar *property, GValue *value);

	gchar* (*get_playing_source) (RBRemoteProxy *proxy);

};

GType rb_remote_proxy_get_type (void) G_GNUC_CONST;

void rb_remote_proxy_load_uri (RBRemoteProxy *proxy,
			       const char *uri,
			       gboolean play);
void rb_remote_proxy_select_uri (RBRemoteProxy *proxy,
				 const char *uri);
void rb_remote_proxy_play_uri (RBRemoteProxy *proxy,
			       const char *uri);
void rb_remote_proxy_grab_focus (RBRemoteProxy *proxy);
void rb_remote_proxy_set_visibility (RBRemoteProxy *proxy, gboolean visible);
gboolean rb_remote_proxy_get_visibility (RBRemoteProxy *proxy);

gboolean rb_remote_proxy_get_shuffle (RBRemoteProxy *proxy);
void rb_remote_proxy_set_shuffle (RBRemoteProxy *proxy, gboolean shuffle);
gboolean rb_remote_proxy_get_repeat (RBRemoteProxy *proxy);
void rb_remote_proxy_set_repeat (RBRemoteProxy *proxy, gboolean repeat);

void rb_remote_proxy_play (RBRemoteProxy *proxy);
void rb_remote_proxy_pause (RBRemoteProxy *proxy);
gboolean rb_remote_proxy_playing (RBRemoteProxy *proxy);

long rb_remote_proxy_get_playing_time (RBRemoteProxy *proxy);
void rb_remote_proxy_set_playing_time (RBRemoteProxy *proxy, long time);
void rb_remote_proxy_seek (RBRemoteProxy *proxy, long offset);
void rb_remote_proxy_set_rating (RBRemoteProxy *proxy, double rating);

gchar *rb_remote_proxy_get_playing_uri (RBRemoteProxy *proxy);
gboolean rb_remote_proxy_get_song_info (RBRemoteProxy *proxy, const gchar *uri, RBRemoteSong *song);

void rb_remote_proxy_jump_next (RBRemoteProxy *proxy);
void rb_remote_proxy_jump_previous (RBRemoteProxy *proxy);

void rb_remote_proxy_toggle_mute (RBRemoteProxy *proxy);

void rb_remote_proxy_quit (RBRemoteProxy *proxy);

GParamSpec *rb_remote_proxy_find_player_property (RBRemoteProxy *proxy, const gchar *property);
void rb_remote_proxy_player_notify_handler (RBRemoteProxy *proxy, GCallback c_handler, gpointer gobject);
void rb_remote_proxy_set_player_property (RBRemoteProxy *proxy, const gchar *property, GValue *value);
void rb_remote_proxy_get_player_property (RBRemoteProxy *proxy, const gchar *property, GValue *value);

gchar *rb_remote_proxy_get_playing_source (RBRemoteProxy *proxy);

G_END_DECLS

#endif /* __RB_REMOTE_PROXY_H__ */
