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
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "rb-debug.h"

#define RB_REMOTE_DBUS_SERVICE_PATH "org.gnome.Rhythmbox"
#define RB_REMOTE_DBUS_OBJECT_PATH "/org/gnome/Rhythmbox/DBusRemote0"

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

static void rb_remote_dbus_unregister_handler (DBusConnection *connection, void *data);

static DBusHandlerResult rb_remote_dbus_message_handler (DBusConnection *connection,
							 DBusMessage *message,
							 void *user_data);

static DBusObjectPathVTable
rb_remote_dbus_vtable = { &rb_remote_dbus_unregister_handler,
			  &rb_remote_dbus_message_handler,
			  NULL,
			  NULL,
			  NULL,
			  NULL };

static GObjectClass *parent_class;

enum
{
	PROP_NONE,
};

struct RBRemoteDBusPrivate
{
	gboolean disposed;

	DBusConnection *connection;
	
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
	DBusError error;
	dbus->priv = g_new0 (RBRemoteDBusPrivate, 1);

	dbus_error_init (&error);
	dbus->priv->connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
	if (dbus->priv->connection == NULL) {
		g_critical ("couldn't connect to session bus: %s",
			    error.message);
	} else {
		rb_debug ("we're on DBus with the GConnection! Yeah baby!");
		dbus_connection_setup_with_g_main (dbus->priv->connection, NULL);
	}
	dbus_error_free (&error);
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
	DBusError error;
	gboolean ret;

	if (!dbus->priv->connection) {
		rb_debug ("failed to activate, we're not on DBus...");
		return FALSE;
	}
	
	dbus_error_init (&error);
	ret = dbus_bus_service_exists (dbus->priv->connection,
				       RB_REMOTE_DBUS_SERVICE_PATH,
				       &error);
	if (dbus_error_is_set (&error))
		rb_debug ("Couldn't check service: %s", error.message);
	dbus_error_free (&error);
	return ret;
}

static void
shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	g_object_unref (G_OBJECT (data));
}

gboolean
rb_remote_dbus_acquire (RBRemoteDBus *dbus,
			RBRemoteProxy *proxy,
			GError **error)
{
	gboolean acquired;
	DBusError buserror;

	if (!dbus->priv->connection) {
		rb_debug ("failed to register, we're not on DBus...");
		return FALSE;
	}
	
	dbus->priv->proxy = proxy;
	g_object_weak_ref (G_OBJECT (proxy), shell_weak_ref_cb, dbus);

	dbus_error_init (&buserror);
	acquired = dbus_bus_acquire_service (dbus->priv->connection,
					     RB_REMOTE_DBUS_SERVICE_PATH,
					     0, &buserror) != -1;
	if (dbus_error_is_set (&buserror))
		g_set_error (error,
			     RB_REMOTE_DBUS_ERROR,
			     RB_REMOTE_DBUS_ERROR_ACQUISITION_FAILURE,
			     "%s",
			     buserror.message);

	rb_debug ("acquiring service %s with dbus: %s",
		  RB_REMOTE_DBUS_SERVICE_PATH, acquired ? "success" : "failure"); 

	if (dbus_connection_register_object_path (dbus->priv->connection,
						  RB_REMOTE_DBUS_OBJECT_PATH,
						  &rb_remote_dbus_vtable,
						  dbus) == FALSE)
		g_critical ("out of memory registering object path");
	else
		rb_debug ("registered session object: %s", RB_REMOTE_DBUS_OBJECT_PATH);

	dbus_error_free (&buserror);

	return acquired;
}

static void
rb_remote_dbus_unregister_handler (DBusConnection *connection, void *data)
{
	rb_debug ("unregistered!");
}

static DBusHandlerResult
rb_remote_dbus_message_handler (DBusConnection *connection,
				DBusMessage *message,
				void *user_data)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (user_data);

	rb_debug ("got message: %s.%s",
		  dbus_message_get_interface (message),
		  dbus_message_get_member (message));

	if (dbus_message_is_method_call (message,
					 RB_REMOTE_DBUS_SERVICE_PATH,
					 "grabFocus")) {
		rb_debug ("grabbing focus");
		rb_remote_proxy_grab_focus (dbus->priv->proxy);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
/* Client methods */

static DBusMessage *
invoke_noarg_method (DBusConnection *connection, const char *name,
	       gboolean expect_reply)
{
  DBusMessage *message;
  DBusMessage *reply;
  DBusError error;

  dbus_error_init (&error);
  message = dbus_message_new_method_call (RB_REMOTE_DBUS_SERVICE_PATH,
					  RB_REMOTE_DBUS_OBJECT_PATH,
					  RB_REMOTE_DBUS_SERVICE_PATH,
					  name);
  if (message == NULL) {
	  g_critical ("Out of memory in invoke_noarg_method!\n");
	  return NULL;
  }

  if (!expect_reply) {
	  if (!dbus_connection_send (connection, message, NULL))
		  g_critical ("Out of memory in invoke_noarg_method!\n");
	  reply = NULL;
  } else {
	  reply = dbus_connection_send_with_reply_and_block (connection, message,
							     -1, &error);
	  if (dbus_error_is_set (&error)) {
		  fprintf (stderr, "%s raised:\n %s\n\n", error.name, error.message);
		  reply = NULL;
	  }
    }
  dbus_message_unref (message);
  dbus_error_free (&error);
  return reply;
}

static void
rb_remote_dbus_client_handle_uri_impl (RBRemoteClientProxy *proxy, const char *uri)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	invoke_noarg_method (dbus->priv->connection, "handleURI", FALSE); 
}

static RBRemoteSong *
rb_remote_dbus_client_get_playing_song_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	RBRemoteSong *song;
	DBusMessage *reply;
	DBusMessageIter song_dict;
	DBusError error;
	
	reply = invoke_noarg_method (dbus->priv->connection,
			       "getPlayingSong", TRUE);
	if (!reply)
		return NULL;

	dbus_error_init (&error);

	song = NULL;
	if (!dbus_message_get_args (reply, &error,
				    DBUS_TYPE_DICT, &song_dict,
				    DBUS_TYPE_INVALID)) {
		g_warning ("Couldn't parse arguments");
		goto out;
	}

	song = g_new0 (RBRemoteSong, 1);
								
	while (dbus_message_iter_get_arg_type (&song_dict) != DBUS_TYPE_INVALID) {
		const char *key;

		key = dbus_message_iter_get_dict_key (&song_dict);
		dbus_message_iter_next (&song_dict);
		if (!strcmp (key, "Title")) {
			song->title = g_strdup (dbus_message_iter_get_string (&song_dict));
			dbus_message_iter_next (&song_dict);
		} else if (!strcmp (key, "Artist")) {
			song->artist = g_strdup (dbus_message_iter_get_string (&song_dict));
			dbus_message_iter_next (&song_dict);
		}
	}

out:
	dbus_error_free (&error);
	dbus_message_unref (reply);
	return song;
}

static void
rb_remote_dbus_client_grab_focus_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	invoke_noarg_method (dbus->priv->connection, "grabFocus", FALSE);
}

static void
rb_remote_dbus_client_toggle_shuffle_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	DBusMessage *message;
	DBusMessage *reply;
	DBusError error;
	gboolean shuffle;
	
	reply = invoke_noarg_method (dbus->priv->connection, "getShuffle", TRUE);
	if (!reply)
		return;

	shuffle = FALSE;
	dbus_error_init (&error);
	if (!dbus_message_get_args (reply, &error,
				    DBUS_TYPE_BOOLEAN, &shuffle,
				    DBUS_TYPE_INVALID)) {
		g_warning ("Couldn't parse arguments");
		goto out;
	}

	message = dbus_message_new_method_call (RB_REMOTE_DBUS_SERVICE_PATH,
						RB_REMOTE_DBUS_OBJECT_PATH,
						RB_REMOTE_DBUS_SERVICE_PATH,
						"setShuffle");
	dbus_message_append_args (message, DBUS_TYPE_BOOLEAN, !shuffle, DBUS_TYPE_INVALID);
	dbus_connection_send (dbus->priv->connection, message, NULL);
	dbus_message_unref (message);

out:
	dbus_error_free (&error);
	dbus_message_unref (reply);
}

static void
rb_remote_dbus_client_toggle_playing_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	DBusMessage *message;
	DBusMessage *reply;
	DBusError error;
	dbus_bool_t playing;
	
	reply = invoke_noarg_method (dbus->priv->connection, "getPlaying", TRUE);
	if (!reply)
		return;

	playing = FALSE;
	dbus_error_init (&error);
	if (!dbus_message_get_args (reply, &error,
				    DBUS_TYPE_BOOLEAN, &playing,
				    DBUS_TYPE_INVALID)) {
		g_warning ("Couldn't parse arguments");
		goto out;
	}

	message = dbus_message_new_method_call (RB_REMOTE_DBUS_SERVICE_PATH,
						RB_REMOTE_DBUS_OBJECT_PATH,
						RB_REMOTE_DBUS_SERVICE_PATH,
						"setPlaying");
	dbus_message_append_args (message, DBUS_TYPE_BOOLEAN, !playing, DBUS_TYPE_INVALID);
	dbus_connection_send (dbus->priv->connection, message, NULL);
	dbus_message_unref (message);

out:
	dbus_error_free (&error);
	dbus_message_unref (reply);
}

static long
rb_remote_dbus_client_get_playing_time_impl (RBRemoteClientProxy *proxy)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	DBusMessage *reply;
	DBusError error;
	dbus_int64_t playing_time;
	
	reply = invoke_noarg_method (dbus->priv->connection, "getPlayingTime", TRUE);
	if (!reply)
		return -1;
	playing_time = FALSE;
	dbus_error_init (&error);
	if (!dbus_message_get_args (reply, &error,
				    DBUS_TYPE_INT64, &playing_time,
				    DBUS_TYPE_INVALID)) {
		g_warning ("Couldn't parse arguments");
	}

	dbus_error_free (&error);
	dbus_message_unref (reply);
	return (long) playing_time;
}

static void
rb_remote_dbus_client_set_playing_time_impl (RBRemoteClientProxy *proxy, long time)
{
	RBRemoteDBus *dbus = RB_REMOTE_DBUS (proxy);
	DBusMessage *message;
	DBusError error;

	dbus_error_init (&error);
	message = dbus_message_new_method_call (RB_REMOTE_DBUS_SERVICE_PATH,
						RB_REMOTE_DBUS_OBJECT_PATH,
						RB_REMOTE_DBUS_SERVICE_PATH,
						"setPlayingTime");
	dbus_message_append_args (message, DBUS_TYPE_INT64, time, DBUS_TYPE_INVALID);
	dbus_connection_send (dbus->priv->connection, message, NULL);
	dbus_message_unref (message);
	dbus_error_free (&error);
}

