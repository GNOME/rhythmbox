/*
 *  arch-tag: Implementation of Rhythmbox DBus remoting
 *
 *  Copyright (C) 2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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
#include "rb-remote-dbus.h"
#include "rb-remote-client-proxy.h"
#include <libgnome/libgnome.h>
#include <libgnome/gnome-i18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include "rb-debug.h"

static void rb_remote_dbus_remote_client_proxy_init (RBRemoteClientProxyIface *iface);

G_DEFINE_TYPE_WITH_CODE(RBRemoteDBus, rb_remote_dbus, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_REMOTE_CLIENT_PROXY,
					      rb_remote_dbus_remote_client_proxy_init))

static void rb_remote_dbus_dispose (GObject *object);
static void rb_remote_dbus_finalize (GObject *object);

/* Client methods */
static void rb_remote_dbus_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri);
static RBRemoteSong *rb_remote_dbus_client_get_playing_song_impl (RBRemoteClientProxy *proxy);
static void rb_remote_dbus_client_grab_focus_impl (RBRemoteClientProxy *proxy);
static void rb_remote_dbus_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy);
static void rb_remote_dbus_client_toggle_playing_impl (RBRemoteClientProxy *proxy);
static long rb_remote_dbus_client_get_playing_time_impl (RBRemoteClientProxy *proxy);
static void rb_remote_dbus_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time);

static GObjectClass *parent_class;

enum
{
	PROP_NONE,
};

struct RBRemoteDBusPrivate
{
	gboolean disposed;

	DBusGConnection *connection;
	DBusGProxy *rb_proxy;
	
	RBRemoteProxy *proxy;
};

static void
rb_remote_dbus_class_init (RBRemoteDBusClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        object_class->dispose = rb_remote_dbus_dispose;
        object_class->finalize = rb_remote_dbus_finalize;
}

static void
rb_remote_dbus_remote_client_proxy_init (RBRemoteClientProxyIface *iface)
{
	iface->handle_uri = rb_remote_dbus_client_handle_uri_impl;
	iface->get_playing_song = rb_remote_dbus_client_get_playing_song_impl;
	iface->grab_focus = rb_remote_dbus_client_grab_focus_impl;
	iface->toggle_shuffle = rb_remote_dbus_client_toggle_shuffle_impl;
	iface->toggle_playing = rb_remote_dbus_client_toggle_playing_impl;
	iface->get_playing_time = rb_remote_dbus_client_get_playing_time_impl;
	iface->set_playing_time = rb_remote_dbus_client_set_playing_time_impl;
}

static void
rb_remote_dbus_init (RBRemoteDBus *dbus) 
{
	dbus->priv = g_new0 (RBRemoteDBusPrivate, 1);
}

static void
rb_remote_dbus_dispose (GObject *object)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (object);

	if (dbus->priv->disposed)
		return;
	dbus->priv->disposed = TRUE;
	
	/* FIXME: this is broken */
/*	dbus_connection_unref (dbus->priv->connection); */
}

static void
rb_remote_dbus_finalize (GObject *object)
{
        RBRemoteDBus *dbus = RB_REMOTE_DBUS (object);

	g_free (dbus->priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

RBRemoteDBus *
rb_remote_dbus_new (void)
{
	return g_object_new (RB_TYPE_REMOTE_DBUS, NULL);
}

GQuark
rb_remote_dbus_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_remote_dbus_error");

	return quark;
}

gboolean
rb_remote_dbus_activate (RBRemoteDBus *dbus)
{
	GError *error = NULL;

	dbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (!dbus->priv->connection)
		return FALSE;

	dbus->priv->rb_proxy = dbus_g_proxy_new_for_name_owner (dbus->priv->connection,
								"org.gnome.Rhythmbox",
								"/org/gnome/Rhythmbox/Shell",
								"org.gnome.Rhythmbox.Shell",
								&error);
	if (!dbus->priv->rb_proxy) {
		g_critical ("Couldn't create proxy for org.gnome.Rhythmbox: %s",
			    error->message);
		return FALSE;
	}
	
	return TRUE;
}

static void
rb_remote_dbus_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	dbus_g_proxy_call_no_reply (dbus->priv->rb_proxy, "handleURI", G_TYPE_STRING, uri, G_TYPE_INVALID);
}

static RBRemoteSong *
rb_remote_dbus_client_get_playing_song_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	RBRemoteSong *song;
	GHashTable *table;
	const GValue *val;
	GError *error = NULL;
	
	if (!dbus_g_proxy_call (dbus->priv->rb_proxy, "getPlayingSong", &error, G_TYPE_INVALID,
				dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
				&table, G_TYPE_INVALID))
	  return NULL;
		
	song = g_new0 (RBRemoteSong, 1);
#define TAKE_STRING(key, member) \
	if ((val = g_hash_table_lookup (table, key)) && G_VALUE_TYPE (val) == G_TYPE_STRING) \
		song->member = g_strdup (g_value_get_string (val));
		
	TAKE_STRING ("title", title);
	TAKE_STRING ("artist", artist);
	    
	return song;
}

static void
rb_remote_dbus_client_grab_focus_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);

	dbus_g_proxy_call_no_reply (dbus->priv->rb_proxy, "present",
				    G_TYPE_UINT,
				    gdk_x11_display_get_user_time (gdk_display_get_default ()),
				    G_TYPE_INVALID);
}

static void
rb_remote_dbus_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	GError *error = NULL;
	gboolean shuffle;
	
	if (!(dbus_g_proxy_call (dbus->priv->rb_proxy, "getShuffle", &error,
				 G_TYPE_INVALID, G_TYPE_BOOLEAN, &shuffle,
				 G_TYPE_INVALID)))
		return;
	if (!(dbus_g_proxy_call (dbus->priv->rb_proxy, "setShuffle", &error,
				 G_TYPE_BOOLEAN, !shuffle,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID)))
		return;
}

static void
rb_remote_dbus_client_toggle_playing_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	GError *error = NULL;
	gboolean playing;
	
	if (!(dbus_g_proxy_call (dbus->priv->rb_proxy, "getPlaying", &error,
				 G_TYPE_INVALID, G_TYPE_BOOLEAN, &playing,
				 G_TYPE_INVALID)))
		return;
	if (!(dbus_g_proxy_call (dbus->priv->rb_proxy, "setPlaying", &error,
				 G_TYPE_BOOLEAN, !playing,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID)))
		return;
}

static long
rb_remote_dbus_client_get_playing_time_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	guint64 playing_time;
	GError *error = NULL;
	
	if (!(dbus_g_proxy_call (dbus->priv->rb_proxy, "getPlayingTime", &error,
				 G_TYPE_INVALID, G_TYPE_UINT64, &playing_time,
				 G_TYPE_INVALID)))
		return -1;
	return (long) playing_time;
}

static void
rb_remote_dbus_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);

	dbus_g_proxy_call_no_reply (dbus->priv->rb_proxy, "setPlayingTime", G_TYPE_UINT64, time, G_TYPE_INVALID);
}

