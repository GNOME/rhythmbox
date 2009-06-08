/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/*
 * Rhythmbox out-of-process metadata reader.
 */

#include <config.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gst/gst.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"
#include "rb-util.h"

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

enum {
	ERROR_FLAG = 1,
	MISSING_PLUGINS = 2
};

static DBusHandlerResult
_send_error (DBusConnection *connection,
	     DBusMessage *request,
	     int details,
	     char **missing_plugins,
	     char **plugin_descriptions,
	     gint error_type,
	     const char *message)
{
	DBusMessage *reply = dbus_message_new_method_return (request);
	DBusMessageIter iter;

	if (!message) {
		message = "";
		rb_debug ("attempting to return error with no message");
	} else {
		rb_debug ("attempting to return error: %s", message);
	}

	dbus_message_iter_init_append (reply, &iter);
	
	if (details & MISSING_PLUGINS) {
		if (!rb_metadata_dbus_add_strv (&iter, missing_plugins)) {
			rb_debug ("couldn't append missing plugins data");
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		if (!rb_metadata_dbus_add_strv (&iter, plugin_descriptions)) {
			rb_debug ("couldn't append missing plugin descriptions");
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
	}

	if (details & ERROR_FLAG) {
		gboolean ok = FALSE;
		if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &ok)) {
			rb_debug ("couldn't append error flag");
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
	}

	if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &error_type) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &message)) {
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
	char **missing_plugins = NULL;
	char **plugin_descriptions = NULL;
	gboolean has_audio;
	gboolean has_video;
	gboolean has_other_data;

	if (!dbus_message_iter_init (message, &iter)) {
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	if (!rb_metadata_dbus_get_string (&iter, &uri)) {
		/* make translatable? */
		return _send_error (connection, message,
				    ERROR_FLAG | MISSING_PLUGINS,
				    NULL, NULL,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to read URI from request");
	}

	rb_debug ("loading metadata from %s", uri);
	rb_metadata_load (svc->metadata, uri, &error);
	g_free (uri);

	rb_metadata_get_missing_plugins (svc->metadata, &missing_plugins, &plugin_descriptions);

	if (error != NULL) {
		DBusHandlerResult r;
		rb_debug ("metadata error: %s", error->message);

		r = _send_error (connection, message, 
				 ERROR_FLAG | MISSING_PLUGINS,
				 missing_plugins, plugin_descriptions,
				 error->code, error->message);
		g_clear_error (&error);
		g_strfreev (missing_plugins);
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
	has_audio = rb_metadata_has_audio (svc->metadata);
	has_video = rb_metadata_has_video (svc->metadata);
	has_other_data = rb_metadata_has_other_data (svc->metadata);
	dbus_message_iter_init_append (reply, &iter);
	
	if (!rb_metadata_dbus_add_strv (&iter, missing_plugins)) {
		rb_debug ("out of memory adding data to return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (!rb_metadata_dbus_add_strv (&iter, plugin_descriptions)) {
		rb_debug ("out of memory adding data to return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &ok) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &has_audio) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &has_video) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &has_other_data) ||
	    !dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &mimetype)) {
		rb_debug ("out of memory adding data to return message");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!rb_metadata_dbus_add_to_message (svc->metadata, &iter)) {
		/* make translatable? */
		return _send_error (connection, message,
				    ERROR_FLAG | MISSING_PLUGINS,
				    missing_plugins, plugin_descriptions,
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
		return _send_error (connection, message, ERROR_FLAG,
				    NULL, NULL,
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
	data = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rb_value_free);
	if (!rb_metadata_dbus_read_from_message (svc->metadata,
						 data,
						 &iter)) {
		/* make translatable? */
		return _send_error (connection, message, 0, NULL, NULL,
				    RB_METADATA_ERROR_INTERNAL,
				    "Unable to read metadata from message");
	}

	/* pass to real metadata instance, and save it */
	g_hash_table_foreach_remove (data, (GHRFunc) _set_metadata, svc->metadata);
	g_hash_table_destroy (data);

	rb_metadata_save (svc->metadata, &error);

	if (error) {
		DBusHandlerResult r;
		rb_debug ("metadata error: %s", error->message);

		r = _send_error (connection, message, 0, NULL, NULL, error->code, error->message);
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
	DBusHandlerResult result;
	rb_debug ("handling metadata service message");

	if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "load")) {
		result = rb_metadata_dbus_load (connection, message, svc);
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "canSave")) {
		result = rb_metadata_dbus_can_save (connection, message, svc);
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "save")) {
		result = rb_metadata_dbus_save (connection, message, svc);
	} else if (dbus_message_is_method_call (message, RB_METADATA_DBUS_INTERFACE, "ping")) {
		result = rb_metadata_dbus_ping (connection, message, svc);
	} else {
		result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	svc->last_active = time (NULL);
	return result;
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
	char **missing_plugins;
	char **plugin_descriptions;

	md = rb_metadata_new ();
	rb_metadata_load (md, uri, &error);
	if (error) {
		if (error->code == RB_METADATA_ERROR_NOT_AUDIO_IGNORE) {
			g_print ("%s is not an audio stream (ignored)\n", uri);
		} else if (error->code == RB_METADATA_ERROR_NOT_AUDIO) {
			g_print ("%s is not an audio stream\n", uri);
		} else {
			g_print ("Error loading metadata from %s: %s\n", uri, error->message);
		}
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

	g_print ("has audio: %d\n", rb_metadata_has_audio (md));
	g_print ("has video: %d\n", rb_metadata_has_video (md));
	g_print ("has other: %d\n", rb_metadata_has_other_data (md));

	if (rb_metadata_get_missing_plugins (md, &missing_plugins, &plugin_descriptions)) {
		int i = 0;
		g_print ("missing plugins:\n");
		while (missing_plugins[i] != NULL) {
			g_print ("\t%s (%s)\n", missing_plugins[i], plugin_descriptions[i]);
			i++;
		}
		g_strfreev (missing_plugins);
	}

	g_object_unref (G_OBJECT (md));
	return rv;
}

int
main (int argc, char **argv)
{
	ServiceData svc = {0,};
	DBusError dbus_error = {0,};
	const char *address = NULL;

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif
	g_type_init ();
	gst_init (NULL, NULL);
	g_set_prgname ("rhythmbox-metadata");

	if (argv[1] != NULL && strcmp(argv[1], "--debug") == 0) {
		argv++;
		rb_debug_init (TRUE);
	} else if (argv[1] != NULL && strcmp (argv[1], "--debug-match") == 0) {
		rb_debug_init_match (argv[2]);
		argv += 2;
	} else {
		rb_debug_init (FALSE);
	}

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
		g_timeout_add_seconds (ATTENTION_SPAN / 2, (GSourceFunc) electromagnetic_shotgun, &svc);

	g_main_loop_run (svc.loop);

	if (svc.connection) {
		dbus_connection_close (svc.connection);
		dbus_connection_unref (svc.connection);
	}

	g_object_unref (svc.metadata);
	g_main_loop_unref (svc.loop);

	dbus_server_disconnect (svc.server);
	dbus_server_unref (svc.server);
	gst_deinit ();

	return 0;
}
