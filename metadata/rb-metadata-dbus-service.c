/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
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

/*
 * Rhythmbox out-of-process metadata reader.
 */

#include <config.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <libgnomeui/gnome-authentication-manager.h>
#include <libgnomevfs/gnome-vfs.h>
#include <gst/gst.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"

/* number of seconds to hang around doing nothing */
#define ATTENTION_SPAN		30

typedef struct {
	DBusServer *server;
	DBusConnection *connection;
	GMainLoop *loop;
	time_t last_active;
	RBMetaData *metadata;
	gboolean external;
} ServiceData;

static DBusHandlerResult
_send_error (DBusConnection *connection,
	     DBusMessage *request,
	     gboolean include_flag,
	     gint error_type,
	     const char *message)
{
	DBusMessage *reply = dbus_message_new_method_return (request);

	rb_debug ("attempting to return error: %s", message);
	if (!message)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (include_flag) {
		gboolean ok = FALSE;
		if (!dbus_message_append_args (reply, DBUS_TYPE_BOOLEAN, &ok, DBUS_TYPE_INVALID)) {
			rb_debug ("couldn't append error flag");
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
	}

	if (!dbus_message_append_args (reply,
				       DBUS_TYPE_UINT32, &error_type,
				       DBUS_TYPE_STRING, &message,
				       DBUS_TYPE_INVALID)) {
		rb_debug ("couldn't append error data");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
rb_metadata_dbus_load (DBusConnection *connection,
		       DBusMessage *message,
		       ServiceData *svc)
{
	char *uri;
	DBusMessageIter iter;
	DBusMessage *reply;
	GError *error = NULL;
	gboolean ok = TRUE;
	const char *mimetype = NULL;

	if (!dbus_message_iter_init (message, &iter)) {
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	if (!rb_metadata_dbus_get_string (&iter, &uri)) {
		/* make translatable? */
		return _send_error (connection, message, TRUE,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to read URI from request");
	}
	
	rb_debug ("loading metadata from %s", uri);

	rb_metadata_load (svc->metadata, uri, &error);
	g_free (uri);
	if (error != NULL) {
		DBusHandlerResult r;
		rb_debug ("metadata error: %s", error->message);

		r = _send_error (connection, message, TRUE, error->code, error->message);
		g_clear_error (&error);
		return r;
	}
	rb_debug ("metadata load finished; mimetype = %s", rb_metadata_get_mime (svc->metadata));

	/* construct reply */
	reply = dbus_message_new_method_return (message);
	if (!reply) {
		rb_debug ("out of memory creating return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	mimetype = rb_metadata_get_mime (svc->metadata);
	dbus_message_iter_init_append (reply, &iter);

	if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &ok) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &mimetype)) {
		rb_debug ("out of memory adding data to return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!rb_metadata_dbus_add_to_message (svc->metadata, &iter)) {
		/* make translatable? */
		return _send_error (connection, message, TRUE,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to add metadata to return message");
	}

	if (!dbus_connection_send (connection, reply, NULL)) {
		rb_debug ("failed to send return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	dbus_message_unref (reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
rb_metadata_dbus_can_save (DBusConnection *connection,
			   DBusMessage *message,
			   ServiceData *svc)
{
	char *mimetype;
	DBusMessageIter iter;
	DBusMessage *reply;
	gboolean can_save;

	if (!dbus_message_iter_init (message, &iter)) {
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	if (!rb_metadata_dbus_get_string (&iter, &mimetype)) {
		/* make translatable? */
		return _send_error (connection, message, TRUE,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to read MIME type from request");
	}

	can_save = rb_metadata_can_save (svc->metadata, mimetype);
	g_free (mimetype);

	/* construct reply */
	reply = dbus_message_new_method_return (message);
	if (!reply) {
		rb_debug ("out of memory creating return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	dbus_message_iter_init_append (reply, &iter);
	if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &can_save)) {
		rb_debug ("out of memory adding data to return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!dbus_connection_send (connection, reply, NULL)) {
		rb_debug ("failed to send return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	
	dbus_message_unref (reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static gboolean
_set_metadata (gpointer key, GValue *data, RBMetaData *metadata)
{
	RBMetaDataField field = GPOINTER_TO_INT (key);
	rb_metadata_set (metadata, field, data);
	return TRUE;
}

static void
_metadata_value_free (gpointer data)
{
	GValue *v = (GValue *)data;
	g_value_unset (v);
	g_free (v);
}

static DBusHandlerResult
rb_metadata_dbus_save (DBusConnection *connection,
		       DBusMessage *message,
		       ServiceData *svc)
{
	DBusMessageIter iter;
	DBusMessage *reply;
	GHashTable *data;
	GError *error = NULL;

	/* get metadata from message */
	if (!dbus_message_iter_init (message, &iter)) {
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}
	data = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, _metadata_value_free);
	if (!rb_metadata_dbus_read_from_message (svc->metadata,
						 data,
						 &iter)) {
		/* make translatable? */
		return _send_error (connection, message, FALSE,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to read metadata from message");
	}
	
	/* pass to real metadata instance, and save it */
	g_hash_table_foreach_remove (data, (GHRFunc) _set_metadata, svc->metadata);

	rb_metadata_save (svc->metadata, &error);

	if (error) {
		DBusHandlerResult r;
		rb_debug ("metadata error: %s", error->message);

		r = _send_error (connection, message, FALSE, error->code, error->message);
		g_clear_error (&error);
		return r;
	}
	
	reply = dbus_message_new_method_return (message);
	if (!reply) {
		rb_debug ("out of memory creating return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!dbus_connection_send (connection, reply, NULL)) {
		rb_debug ("failed to send return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	
	dbus_message_unref (reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
rb_metadata_dbus_ping (DBusConnection *connection,
		       DBusMessage *message,
		       ServiceData *svc)
{
	gboolean ok = TRUE;

	rb_debug ("ping");

	DBusMessage *reply = dbus_message_new_method_return (message);
	if (!message)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	if (!dbus_message_append_args (reply,
				       DBUS_TYPE_BOOLEAN, &ok,
				       DBUS_TYPE_INVALID)) {
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static gboolean
electromagnetic_shotgun (gpointer data)
{
	ServiceData *c = (ServiceData *)data;
	GTime now = time(NULL);
	int idle = now - c->last_active;

	/* quit if we haven't done anything for a while */
	if (idle > ATTENTION_SPAN) {
		rb_debug ("shutting down (%ds idle)", idle);
		g_main_loop_quit (c->loop);
	}

	return TRUE;
}

static void
_unregister_handler (DBusConnection *connection, void *data)
{
	/* nothing? */
}

static DBusHandlerResult
_handle_message (DBusConnection *connection, DBusMessage *message, void *data)
{
	ServiceData *svc = (ServiceData *)data;
	rb_debug ("handling metadata service message");

	svc->last_active = time (NULL);
	if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "load")) {
		return rb_metadata_dbus_load (connection, message, svc);		
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "canSave")) {
		return rb_metadata_dbus_can_save (connection, message, svc);		
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "save")) {
		return rb_metadata_dbus_save (connection, message, svc);		
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "ping")) {
		return rb_metadata_dbus_ping (connection, message, svc);		
	} else {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
}

static void
_new_connection (DBusServer *server,
		 DBusConnection *connection,
		 void *data)
{
	ServiceData *svc = (ServiceData *)data;
	DBusObjectPathVTable vt = {
		_unregister_handler,
		_handle_message,
		NULL, NULL, NULL, NULL
	};

	rb_debug ("new connection to metadata service");

	/* don't allow more than one connection at a time */
	if (svc->connection) {
		rb_debug ("metadata service already has a client.  go away.");
		return;
	}

	dbus_connection_register_object_path (connection,
					      RB_METADATA_DBUS_OBJECT_PATH,
					      &vt,
					      svc);
	dbus_connection_ref (connection);
	dbus_connection_setup_with_g_main (connection, 
					   g_main_loop_get_context (svc->loop));
	if (!svc->external)
		dbus_connection_set_exit_on_disconnect (connection, TRUE);
}

static int
test_can_save (const char *mimetype)
{
	RBMetaData *md;
	gboolean can_save;

#ifdef HAVE_GSTREAMER
	gst_init (NULL, NULL);
#endif
	md = rb_metadata_new ();
	can_save = rb_metadata_can_save (md, mimetype);
	g_print ("%s save %s\n", can_save ? "Can" : "Can't", mimetype);
	g_object_unref (G_OBJECT (md));
	return 0;
}

static int
test_load (const char *uri)
{
	RBMetaData *md;
	GError *error = NULL;
	int rv = 0;

#ifdef HAVE_GSTREAMER
	gst_init (NULL, NULL);
#endif

	md = rb_metadata_new ();
	rb_metadata_load (md, uri, &error);
	if (error) {
		g_print ("Error loading metadata from %s: %s\n", uri, error->message);
		g_clear_error (&error);
		rv = -1;
	} else {
		int i;
		g_print ("mimetype: %s\n", rb_metadata_get_mime (md));
		for (i=0; i<RB_METADATA_FIELD_LAST; i++) {
			GValue v = {0,};
			GValue sv = {0,};
			if (rb_metadata_get (md, i, &v)) {
				g_value_init (&sv, G_TYPE_STRING);
				g_value_transform (&v, &sv);

				g_print ("%s: %s\n", rb_metadata_get_field_name (i), g_value_get_string (&sv));

				g_value_unset (&v);
				g_value_unset (&sv);
			}
		}
	}
	g_object_unref (G_OBJECT (md));
	return rv;
}

int
main (int argc, char **argv)
{
	ServiceData svc = {0,};
	DBusError dbus_error = {0,};
	gboolean debug = FALSE;
	const char *address = NULL;

	g_type_init ();

	if (argv[1] != NULL && strcmp(argv[1], "--debug") == 0) {
		argv++;
		debug = TRUE;
	}
	rb_debug_init (debug);

	/* bug report modes */
	if (argv[1] != NULL && strcmp(argv[1], "--load") == 0) {
		return test_load (argv[2]);
	}
	if (argv[1] != NULL && strcmp(argv[1], "--can-save") == 0) {
		return test_can_save (argv[2]);
	}

	if (argv[1] != NULL && strcmp (argv[1], "--external") == 0) {
		argv++;
		svc.external = TRUE;
	}
	if (argv[1] == NULL) {
		address = "unix:tmpdir=/tmp";
	} else {
		address = argv[1];
	}

	rb_debug ("initializing metadata service; pid = %d; address = %s", getpid (), address);
	
	gnome_vfs_init ();
	gnome_authentication_manager_init ();
#ifdef HAVE_GSTREAMER
	gst_init (NULL, NULL);
#endif
	svc.metadata = rb_metadata_new ();

	/* set up D-BUS server */
	svc.server = dbus_server_listen (address, &dbus_error);
	if (!svc.server) {
		rb_debug ("D-BUS server init failed: %s", dbus_error.message);
		return -1;
	}

	dbus_server_set_new_connection_function (svc.server,
						 _new_connection,
						 (gpointer) &svc,
						 NULL);

	/* write the server address back to the parent process */
	{
		char *addr;
		addr = dbus_server_get_address (svc.server);
		rb_debug ("D-BUS server listening on address %s", addr);
		printf ("%s\n", addr);
		fflush (stdout);
		free (addr);
	}

	/* run main loop until we get bored */
	svc.loop = g_main_loop_new (NULL, TRUE);
	dbus_server_setup_with_g_main (svc.server,
				       g_main_loop_get_context (svc.loop));
	
	if (!svc.external)
		g_timeout_add (ATTENTION_SPAN * 500, (GSourceFunc) electromagnetic_shotgun, &svc);

	g_main_loop_run (svc.loop);

	if (svc.connection) {
		dbus_connection_disconnect (svc.connection);
		dbus_connection_unref (svc.connection);
	}

	dbus_server_disconnect (svc.server);
	dbus_server_unref (svc.server);
#ifdef HAVE_GSTREAMER_0_10
	gst_deinit ();
#endif
	return 0;
}

