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
#include <locale.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gio/gio.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"
#include "rb-util.h"

/* number of seconds to hang around doing nothing */
#define ATTENTION_SPAN		30

typedef struct {
	GDBusServer *server;
	GDBusConnection *connection;
	GDBusNodeInfo *node_info;
	GMainLoop *loop;
	time_t last_active;
	RBMetaData *metadata;
	gboolean external;
} ServiceData;

static void
rb_metadata_dbus_load (GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       ServiceData *svc)
{
	const char *uri;
	GError *error = NULL;
	GVariant *response;
	const char *nothing[] = { NULL };
	char **missing_plugins = NULL;
	char **plugin_descriptions = NULL;
	const char *mediatype;

	g_variant_get (parameters, "(&s)", &uri);

	rb_debug ("loading metadata from %s", uri);
	rb_metadata_load (svc->metadata, uri, &error);
	mediatype = rb_metadata_get_media_type (svc->metadata);
	rb_debug ("metadata load finished (type %s)", mediatype);

	rb_metadata_get_missing_plugins (svc->metadata, &missing_plugins, &plugin_descriptions);

	response = g_variant_new ("(^as^asbbbsbisa{iv})",
				  missing_plugins ? missing_plugins : (char **)nothing,
				  plugin_descriptions ? plugin_descriptions : (char **)nothing,
				  rb_metadata_has_audio (svc->metadata),
				  rb_metadata_has_video (svc->metadata),
				  rb_metadata_has_other_data (svc->metadata),
				  mediatype ? mediatype : "",
				  (error == NULL),
				  (error != NULL ? error->code : 0),
				  (error != NULL ? error->message : ""),
				  rb_metadata_dbus_get_variant_builder (svc->metadata));
	g_strfreev (missing_plugins);
	g_strfreev (plugin_descriptions);

	g_dbus_method_invocation_return_value (invocation, response);
}

static void
rb_metadata_dbus_get_saveable_types (GVariant *parameters,
				     GDBusMethodInvocation *invocation,
				     ServiceData *svc)
{
	char **saveable_types;

	saveable_types = rb_metadata_get_saveable_types (svc->metadata);
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(^as)", saveable_types));
	g_strfreev (saveable_types);
}

static void
rb_metadata_dbus_save (GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       ServiceData *svc)
{
	const char *uri;
	GError *error = NULL;
	GVariantIter *metadata;
	RBMetaDataField key;
	GVariant *value;

	g_variant_get (parameters, "(&sa{iv})", &uri, &metadata);

	/* pass metadata to real metadata instance */
	rb_metadata_reset (svc->metadata);
	while (g_variant_iter_next (metadata, "{iv}", &key, &value)) {
		GValue val = {0,};

		switch (rb_metadata_get_field_type (key)) {
		case G_TYPE_STRING:
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, g_variant_get_string (value, NULL));
			break;
		case G_TYPE_ULONG:
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, g_variant_get_uint32 (value));
			break;
		case G_TYPE_DOUBLE:
			g_value_init (&val, G_TYPE_DOUBLE);
			g_value_set_double (&val, g_variant_get_double (value));
			break;
		default:
			g_assert_not_reached ();
			break;
		}
		rb_metadata_set (svc->metadata, key, &val);
		g_variant_unref (value);
		g_value_unset (&val);
	}

	rb_metadata_save (svc->metadata, uri, &error);
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("(bis)",
							      (error == NULL),
							      (error != NULL ? error->code : 0),
							      (error != NULL ? error->message : "")));
}

static void
rb_metadata_dbus_ping (GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       ServiceData *svc)
{
	rb_debug ("ping");
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
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
handle_method_call (GDBusConnection *connection,
		    const char *sender,
		    const char *object_path,
		    const char *interface_name,
		    const char *method_name,
		    GVariant *parameters,
		    GDBusMethodInvocation *invocation,
		    ServiceData *svc)
{
	rb_debug ("handling metadata service message: %s.%s", interface_name, method_name);
	if (g_strcmp0 (method_name, "ping") == 0) {
		rb_metadata_dbus_ping (parameters, invocation, svc);
	} else if (g_strcmp0 (method_name, "load") == 0) {
		rb_metadata_dbus_load (parameters, invocation, svc);
	} else if (g_strcmp0 (method_name, "getSaveableTypes") == 0) {
		rb_metadata_dbus_get_saveable_types (parameters, invocation, svc);
	} else if (g_strcmp0 (method_name, "save") == 0) {
		rb_metadata_dbus_save (parameters, invocation, svc);
	}
	svc->last_active = time (NULL);
}

static const GDBusInterfaceVTable metadata_vtable = {
	(GDBusInterfaceMethodCallFunc) handle_method_call,
	NULL,
	NULL
};

static void
connection_closed_cb (GDBusConnection *connection,
		      gboolean remote_peer_vanished,
		      GError *error,
		      ServiceData *svc)
{
	rb_debug ("client connection closed");
	g_assert (connection == svc->connection);
	svc->connection = NULL;
}

static gboolean
new_connection_cb (GDBusServer *server,
		   GDBusConnection *connection,
		   ServiceData *svc)
{
	GError *error = NULL;
	rb_debug ("new connection to metadata service");

	/* don't allow more than one connection at a time */
	if (svc->connection) {
		rb_debug ("metadata service already has a client.  go away.");
		return FALSE;
	}
	g_dbus_connection_register_object (connection,
					   RB_METADATA_DBUS_OBJECT_PATH,
					   g_dbus_node_info_lookup_interface (svc->node_info, RB_METADATA_DBUS_INTERFACE),
					   &metadata_vtable,
					   svc,
					   NULL,
					   &error);
	if (error != NULL) {
		rb_debug ("unable to register metadata object: %s", error->message);
		g_clear_error (&error);
		return FALSE;
	} else {
		svc->connection = g_object_ref (connection);
		g_signal_connect (connection, "closed", G_CALLBACK (connection_closed_cb), svc);

		g_dbus_connection_set_exit_on_close (connection, (svc->external == FALSE));
		return TRUE;
	}
}

static int
test_saveable_types (void)
{
	RBMetaData *md;
	char **saveable;
	int i;

	md = rb_metadata_new ();
	saveable = rb_metadata_get_saveable_types (md);
	g_object_unref (md);

	for (i = 0; saveable[i] != NULL; i++) {
		g_print ("%s\n", saveable[i]);
	}
	g_strfreev (saveable);

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
		g_print ("Error loading metadata from %s: %s\n", uri, error->message);
		g_clear_error (&error);
		g_print ("media type: %s\n", rb_metadata_get_media_type (md));
		rv = -1;
	} else {
		int i;
		g_print ("media type: %s\n", rb_metadata_get_media_type (md));
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

static gboolean
allow_method_cb (GDBusAuthObserver *observer, const char *mechanism, gpointer data)
{
	if (g_strcmp0 (mechanism, "EXTERNAL") == 0)
		return TRUE;
	return FALSE;
}

static gboolean
auth_cb (GDBusAuthObserver *observer, GIOStream *stream, GCredentials *credentials, gpointer data)
{
	gboolean result;
	GCredentials *own_cred;

	if (credentials == NULL)
		return FALSE;

	own_cred = g_credentials_new ();
	result = g_credentials_is_same_user (credentials, own_cred, NULL);
	g_object_unref (own_cred);

	return result;
}

int
main (int argc, char **argv)
{
	ServiceData svc = {0,};
	GError *error = NULL;
	GDBusAuthObserver *auth;
	const char *address = NULL;
	char *guid;

	setlocale (LC_ALL, "");

	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

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
	if (argv[1] != NULL && strcmp(argv[1], "--saveable-types") == 0) {
		return test_saveable_types ();
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
	svc.loop = g_main_loop_new (NULL, TRUE);
	svc.last_active = time (NULL);

	/* create the server */
	guid = g_dbus_generate_guid ();
	auth = g_dbus_auth_observer_new ();
	g_signal_connect (auth, "allow-mechanism", G_CALLBACK (allow_method_cb), NULL);
	g_signal_connect (auth, "authorize-authenticated-peer", G_CALLBACK (auth_cb), NULL);
	svc.server = g_dbus_server_new_sync (address,
					     G_DBUS_SERVER_FLAGS_NONE,
					     guid,
					     auth,
					     NULL,
					     &error);
	g_free (guid);
	if (error != NULL) {
		g_warning ("D-Bus server init failed: %s", error->message);
		return -1;
	}

	/* set up interface info */
	svc.node_info = g_dbus_node_info_new_for_xml (rb_metadata_iface_xml, &error);
	if (error != NULL) {
		g_warning ("D-Bus server init failed: %s", error->message);
		return -1;
	}

	g_signal_connect (svc.server, "new-connection", G_CALLBACK (new_connection_cb), &svc);
	g_dbus_server_start (svc.server);

	/* write the server address back to the parent process */
	{
		const char *addr;
		addr = g_dbus_server_get_client_address (svc.server);
		rb_debug ("D-BUS server listening on address %s", addr);
		printf ("%s\n", addr);
		fflush (stdout);
	}

	/* run main loop until we get bored */
	if (!svc.external)
		g_timeout_add_seconds (ATTENTION_SPAN / 2, (GSourceFunc) electromagnetic_shotgun, &svc);

	g_main_loop_run (svc.loop);

	if (svc.connection) {
		g_dbus_connection_close_sync (svc.connection, NULL, NULL);
		g_object_unref (svc.connection);
	}

	g_object_unref (svc.metadata);
	g_main_loop_unref (svc.loop);

	g_dbus_server_stop (svc.server);
	g_object_unref (svc.server);
	g_object_unref (auth);
	gst_deinit ();

	return 0;
}
