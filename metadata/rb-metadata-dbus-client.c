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
 * Client for out-of-process metadata reader communicating via D-BUS.
 *
 * How this works:
 * - spawn rb-metadata process, with pipes
 * - child process sets up its dbus server or whatever
 * - if successful, child writes dbus server address to stdout; otherwise, dies.
 * - parent opens dbus connection
 *
 * For each request, the parent checks if the dbus connection is still alive,
 * and pings the child to see if it's still responding.  If the child has
 * exited or is not responding, the parent starts a new metadata helper as
 * described above.
 *
 * The child process exits after a certain period of inactivity (30s
 * currently), so the ping message serves two purposes - it checks that the
 * child is still capable of handling messages, and it ensures the child
 * doesn't time out between when we check the child is still running and when
 * we actually send it the request.
 */

/**
 * SECTION:rbmetadata
 * @short_description: metadata reader and writer interface
 *
 * Provides a simple synchronous interface for metadata extraction and updating.
 */

#include <config.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"
#include "rb-util.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

static void rb_metadata_class_init (RBMetaDataClass *klass);
static void rb_metadata_init (RBMetaData *md);
static void rb_metadata_finalize (GObject *object);

static gboolean tried_env_address = FALSE;
static GDBusConnection *dbus_connection = NULL;
static GPid metadata_child = 0;
static int metadata_stdout = -1;
static GMainContext *main_context = NULL;
static GMutex conn_mutex;
static char **saveable_types = NULL;

struct RBMetaDataPrivate
{
	char       *media_type;
	char      **missing_plugins;
	char      **plugin_descriptions;
	gboolean    has_audio;
	gboolean    has_video;
	gboolean    has_other_data;
	GHashTable *metadata;
};

G_DEFINE_TYPE (RBMetaData, rb_metadata, G_TYPE_OBJECT)

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;

	g_type_class_add_private (object_class, sizeof (RBMetaDataPrivate));

	main_context = g_main_context_new ();	/* maybe not needed? */
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = G_TYPE_INSTANCE_GET_PRIVATE (md, RB_TYPE_METADATA, RBMetaDataPrivate);
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);

	g_free (md->priv->media_type);
	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

/**
 * rb_metadata_new:
 *
 * Creates a new metadata backend instance.
 *
 * Return value: new #RBMetaData instance
 */
RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

static void
kill_metadata_service (void)
{
	if (dbus_connection) {
		if (g_dbus_connection_is_closed (dbus_connection) == FALSE) {
			rb_debug ("closing dbus connection");
			g_dbus_connection_close_sync (dbus_connection, NULL, NULL);
		} else {
			rb_debug ("dbus connection already closed");
		}
		g_object_unref (dbus_connection);
		dbus_connection = NULL;
	}

	if (metadata_child) {
		rb_debug ("killing child process");
		kill (metadata_child, SIGINT);
		g_spawn_close_pid (metadata_child);
		metadata_child = 0;
	}

	if (metadata_stdout != -1) {
		rb_debug ("closing metadata child process stdout pipe");
		close (metadata_stdout);
		metadata_stdout = -1;
	}
}

static gboolean
ping_metadata_service (GError **error)
{
	GDBusMessage *message;
	GDBusMessage *response;

	if (g_dbus_connection_is_closed (dbus_connection))
		return FALSE;

	message = g_dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						  RB_METADATA_DBUS_OBJECT_PATH,
						  RB_METADATA_DBUS_INTERFACE,
						  "ping");
	response = g_dbus_connection_send_message_with_reply_sync (dbus_connection,
								   message,
								   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
								   RB_METADATA_DBUS_TIMEOUT,
								   NULL,
								   NULL,
								   error);
	g_object_unref (message);

	if (*error != NULL) {
		/* ignore 'no reply', just means the service is dead */
		if ((*error)->domain == G_DBUS_ERROR && (*error)->code == G_DBUS_ERROR_NO_REPLY) {
			g_clear_error (error);
		}
		return FALSE;
	}
	g_object_unref (response);
	return TRUE;
}

static gboolean
start_metadata_service (GError **error)
{
	GIOChannel *stdout_channel;
	GIOStatus status;
	gchar *dbus_address = NULL;
	char *saveable_type_list;
	GVariant *response_body;

	if (dbus_connection) {
		if (ping_metadata_service (error))
			return TRUE;

		/* Metadata service is broken.  Kill it, and if we haven't run
		 * into any errors yet, we can try to restart it.
		 */
		kill_metadata_service ();

		if (*error)
			return FALSE;
	}

	if (!tried_env_address) {
		const char *addr = g_getenv ("RB_DBUS_METADATA_ADDRESS");
		tried_env_address = TRUE;
		if (addr) {
			rb_debug ("trying metadata service address %s (from environment)", addr);
			dbus_address = g_strdup (addr);
			metadata_child = 0;
		}
	}

	if (dbus_address == NULL) {
		GPtrArray *argv;
		gboolean res;
		char **debug_args;
		GError *local_error;
		int i;

		argv = g_ptr_array_new ();
		g_ptr_array_add (argv, LIBEXEC_DIR G_DIR_SEPARATOR_S INSTALLED_METADATA_HELPER);
		debug_args = rb_debug_get_args ();
		i = 0;
		while (debug_args[i] != NULL) {
			g_ptr_array_add (argv, debug_args[i]);
			i++;
		}

		g_ptr_array_add (argv, "unix:tmpdir=/tmp");
		g_ptr_array_add (argv, NULL);

		local_error = NULL;
		res = g_spawn_async_with_pipes (NULL,
						(char **)argv->pdata,
						NULL,
						0,
						NULL, NULL,
						&metadata_child,
						NULL,
						&metadata_stdout,
						NULL,
						&local_error);
		g_ptr_array_free (argv, TRUE);
		g_strfreev (debug_args);

		if (res == FALSE) {
			g_propagate_error (error, local_error);
			return FALSE;
		}

		stdout_channel = g_io_channel_unix_new (metadata_stdout);
		status = g_io_channel_read_line (stdout_channel, &dbus_address, NULL, NULL, error);
		g_io_channel_unref (stdout_channel);
		if (status != G_IO_STATUS_NORMAL) {
			kill_metadata_service ();
			return FALSE;
		}

		g_strchomp (dbus_address);
		rb_debug ("Got metadata helper D-BUS address %s", dbus_address);
	}

	dbus_connection = g_dbus_connection_new_for_address_sync (dbus_address,
								  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
								  NULL,
								  NULL,
								  error);
	g_free (dbus_address);
	if (*error != NULL) {
		kill_metadata_service ();
		return FALSE;
	}

	g_dbus_connection_set_exit_on_close (dbus_connection, FALSE);

	rb_debug ("Metadata process %d started", metadata_child);

	/* now ask it what types it can re-tag */
	if (saveable_types != NULL) {
		g_strfreev (saveable_types);
	}

	response_body = g_dbus_connection_call_sync (dbus_connection,
						     RB_METADATA_DBUS_NAME,
						     RB_METADATA_DBUS_OBJECT_PATH,
						     RB_METADATA_DBUS_INTERFACE,
						     "getSaveableTypes",
						     NULL,
						     NULL,
						     G_DBUS_CALL_FLAGS_NONE,
						     RB_METADATA_DBUS_TIMEOUT,
						     NULL,
						     error);
	if (response_body == NULL) {
		rb_debug ("saveable type query failed: %s", (*error)->message);
		return FALSE;
	}

	g_variant_get (response_body, "(^as)", &saveable_types);
	if (saveable_types != NULL) {
		saveable_type_list = g_strjoinv (", ", saveable_types);
		rb_debug ("saveable types from metadata helper: %s", saveable_type_list);
		g_free (saveable_type_list);
	} else {
		rb_debug ("unable to save metadata for any file types");
	}
	g_variant_unref (response_body);

	return TRUE;
}

/**
 * rb_metadata_reset:
 * @md: a #RBMetaData
 *
 * Resets the state of the metadata interface.  Call this before
 * setting tags to be written to a file.
 */
void
rb_metadata_reset (RBMetaData *md)
{
	g_free (md->priv->media_type);
	md->priv->media_type = NULL;

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new_full (g_direct_hash,
						    g_direct_equal,
						    NULL,
						    (GDestroyNotify)rb_value_free);
}

/**
 * rb_metadata_load:
 * @md: a #RBMetaData
 * @uri: URI from which to load metadata
 * @error: returns error information
 *
 * Reads metadata information from the specified URI.
 * Once this has returned successfully (with *error == NULL),
 * rb_metadata_get, rb_metadata_get_media_type, rb_metadata_has_missing_plugins,
 * and rb_metadata_get_missing_plugins can usefully be called.
 */
void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	GVariant *response;
	GError *fake_error = NULL;

	if (error == NULL)
		error = &fake_error;

	rb_metadata_reset (md);
	if (uri == NULL)
		return;
	g_mutex_lock (&conn_mutex);

	start_metadata_service (error);

	if (*error == NULL) {
		rb_debug ("sending metadata load request: %s", uri);
		response = g_dbus_connection_call_sync (dbus_connection,
							RB_METADATA_DBUS_NAME,
							RB_METADATA_DBUS_OBJECT_PATH,
							RB_METADATA_DBUS_INTERFACE,
							"load",
							g_variant_new ("(s)", uri),
							NULL, /* complicated return type */
							G_DBUS_CALL_FLAGS_NONE,
							RB_METADATA_DBUS_TIMEOUT,
							NULL,
							error);
	}

	if (*error == NULL) {
		GVariantIter *metadata;
		gboolean ok = FALSE;
		int error_code;
		char *error_string = NULL;

		g_variant_get (response,
			       "(^as^asbbbsbisa{iv})",
			       &md->priv->missing_plugins,
			       &md->priv->plugin_descriptions,
			       &md->priv->has_audio,
			       &md->priv->has_video,
			       &md->priv->has_other_data,
			       &md->priv->media_type,
			       &ok,
			       &error_code,
			       &error_string,
			       &metadata);

		if (ok) {
			guint32 key;
			GVariant *value;

			while (g_variant_iter_next (metadata, "{iv}", &key, &value)) {
				GValue *val = g_slice_new0 (GValue);

				switch (rb_metadata_get_field_type (key)) {
				case G_TYPE_STRING:
					g_value_init (val, G_TYPE_STRING);
					g_value_set_string (val, g_variant_get_string (value, NULL));
					break;
				case G_TYPE_ULONG:
					g_value_init (val, G_TYPE_ULONG);
					g_value_set_ulong (val, g_variant_get_uint32 (value));
					break;
				case G_TYPE_DOUBLE:
					g_value_init (val, G_TYPE_DOUBLE);
					g_value_set_double (val, g_variant_get_double (value));
					break;
				default:
					g_assert_not_reached ();
					break;
				}
				g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (key), val);
				g_variant_unref (value);
			}

		} else {
			g_set_error (error, RB_METADATA_ERROR,
				     error_code,
				     "%s", error_string);
		}
		g_variant_iter_free (metadata);

		/* if we're missing some plugins, we'll need to make sure the
		 * metadata helper rereads the registry before the next load.
		 * the easiest way to do this is to kill it.
		 */
		if (*error == NULL && g_strv_length (md->priv->missing_plugins) > 0) {
			rb_debug ("missing plugins; killing metadata service to force registry reload");
			kill_metadata_service ();
		}
	}
	if (fake_error)
		g_error_free (fake_error);

	g_mutex_unlock (&conn_mutex);
}

/**
 * rb_metadata_get_media_type:
 * @md: a #RBMetaData
 *
 * Returns the type of the file from which metadata was read.
 * This may look like a MIME type, but it isn't.
 *
 * Return value: media type string
 */
const char *
rb_metadata_get_media_type (RBMetaData *md)
{
	return md->priv->media_type;
}

/**
 * rb_metadata_has_missing_plugins:
 * @md: a #RBMetaData
 *
 * If the metadata reader could not decode the file it was asked to
 * because one or more media framework plugins (specifically, for the
 * existing implementations, GStreamer plugins) required are missing,
 * this will return TRUE.
 *
 * Return value: TRUE if required plugins are missing
 */
gboolean
rb_metadata_has_missing_plugins (RBMetaData *md)
{
	return (md->priv->missing_plugins != NULL &&
	        g_strv_length (md->priv->missing_plugins) > 0);
}

/**
 * rb_metadata_get_missing_plugins:
 * @md: a #RBMetaData
 * @missing_plugins: (out) (array zero-terminated=1): returns machine-readable
 * missing plugin information
 * @plugin_descriptions: (out) (array zero-terminated=1): returns human-readable
 * missing plugin descriptions
 *
 * This function returns the information used to request automatic
 * installation of media framework plugins required to decode the target URI.
 * Use g_strfreev() to free the returned information arrays.
 *
 * Return value: TRUE if missing plugin information was returned
 */
gboolean
rb_metadata_get_missing_plugins (RBMetaData *md,
				 char ***missing_plugins,
				 char ***plugin_descriptions)
{
	if (rb_metadata_has_missing_plugins (md) == FALSE) {
		return FALSE;
	}

	*missing_plugins = g_strdupv (md->priv->missing_plugins);
	*plugin_descriptions = g_strdupv (md->priv->plugin_descriptions);
	return TRUE;
}


/**
 * rb_metadata_get:
 * @md: a #RBMetaData
 * @field: the #RBMetaDataField to retrieve
 * @val: (out caller-allocates) (transfer full): returns the field value
 *
 * Retrieves the value of a metadata field extracted from the target URI.
 * If the target URI contained no value for the field, returns FALSE.
 *
 * Return value: TRUE if a value was returned
 */
gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field, GValue *ret)
{
	GValue *val;
	if (!md->priv->metadata)
		return FALSE;

	if ((val = g_hash_table_lookup (md->priv->metadata,
					GINT_TO_POINTER (field)))) {
		g_value_init (ret, G_VALUE_TYPE (val));
		g_value_copy (val, ret);
		return TRUE;
	}
	return FALSE;
}

/**
 * rb_metadata_set:
 * @md: a #RBMetaData
 * @field: the #RBMetaDataField to set
 * @val: the value to set
 *
 * Sets a metadata field value.  The value is only stored inside the
 * #RBMetaData object until rb_metadata_save is called.
 *
 * Return value: TRUE if the field is valid
 */
gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field,
		 const GValue *val)
{
	GValue *newval;
	GType type;

	type = rb_metadata_get_field_type (field);
	g_return_val_if_fail (type == G_VALUE_TYPE (val), FALSE);

	newval = g_slice_new0 (GValue);
	g_value_init (newval, type);
	g_value_copy (val, newval);

	g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (field),
			     newval);
	return TRUE;
}

/**
 * rb_metadata_can_save:
 * @md: a #RBMetaData
 * @media_type: the media type string to check
 *
 * Checks if the metadata writer is capable of updating file metadata
 * for a given media type.
 *
 * Return value: TRUE if the file metadata for the given media type can be updated
 */
gboolean
rb_metadata_can_save (RBMetaData *md, const char *media_type)
{
	GError *error = NULL;
	gboolean result = FALSE;
	int i = 0;

	g_mutex_lock (&conn_mutex);

	if (saveable_types == NULL) {
		if (start_metadata_service (&error) == FALSE) {
			g_warning ("unable to start metadata service: %s", error->message);
			g_mutex_unlock (&conn_mutex);
			g_error_free (error);
			return FALSE;
		}
	}

	if (saveable_types != NULL) {
		for (i = 0; saveable_types[i] != NULL; i++) {
			if (g_str_equal (media_type, saveable_types[i])) {
				result = TRUE;
				break;
			}
		}
	}

	g_mutex_unlock (&conn_mutex);
	return result;
}

/**
 * rb_metadata_get_saveable_types:
 * @md: a #RBMetaData
 *
 * Constructs a list of the media types for which the metadata backend
 * implements tag saving.
 *
 * Return value: (transfer full) (array zero-terminated=1): a NULL-terminated
 * array of media type strings.  Use g_strfreev to free it.
 */
char **
rb_metadata_get_saveable_types (RBMetaData *md)
{
	return g_strdupv (saveable_types);
}

/**
 * rb_metadata_save:
 * @md: a #RBMetaData
 * @uri: the target URI
 * @error: returns error information
 *
 * Saves all metadata changes made with rb_metadata_set to the
 * target URI.
 */
void
rb_metadata_save (RBMetaData *md, const char *uri, GError **error)
{
	GVariant *response;
	GError *fake_error = NULL;

	if (error == NULL)
		error = &fake_error;

	g_mutex_lock (&conn_mutex);

	start_metadata_service (error);

	if (*error == NULL) {
		response = g_dbus_connection_call_sync (dbus_connection,
							RB_METADATA_DBUS_NAME,
							RB_METADATA_DBUS_OBJECT_PATH,
							RB_METADATA_DBUS_INTERFACE,
							"save",
							g_variant_new ("(sa{iv})",
								       uri,
								       rb_metadata_dbus_get_variant_builder (md)),
							NULL,
							G_DBUS_CALL_FLAGS_NONE,
							RB_METADATA_SAVE_DBUS_TIMEOUT,
							NULL,
							error);
	}

	if (*error == NULL) {
		gboolean ok = TRUE;
		int error_code;
		const char *error_message;

		g_variant_get (response, "(bis)", &ok, &error_code, &error_message);
		if (ok == FALSE) {
			g_set_error (error, RB_METADATA_ERROR,
				     error_code,
				     "%s", error_message);
		}

		g_variant_unref (response);
	}

	if (fake_error)
		g_error_free (fake_error);

	g_mutex_unlock (&conn_mutex);
}

gboolean
rb_metadata_has_audio (RBMetaData *md)
{
	return md->priv->has_audio;
}

gboolean
rb_metadata_has_video (RBMetaData *md)
{
	return md->priv->has_video;
}

gboolean
rb_metadata_has_other_data (RBMetaData *md)
{
	return md->priv->has_other_data;
}
