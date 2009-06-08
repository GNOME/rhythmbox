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
 * SECTION:rb-metadata
 * @short_description: metadata reader and writer interface
 *
 * Provides a simple synchronous interface for metadata extraction and updating.
 *
 */

#include <config.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"
#include "rb-util.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib/gi18n.h>

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
static DBusConnection *dbus_connection = NULL;
static GPid metadata_child = 0;
static int metadata_stdout = -1;
static GMainContext *main_context = NULL;
static GStaticMutex conn_mutex = G_STATIC_MUTEX_INIT;

struct RBMetaDataPrivate
{
	char       *uri;
	char       *mimetype;
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

	g_free (md->priv->uri);
	g_free (md->priv->mimetype);
	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

/**
 * rb_metadata_new:
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
		if (dbus_connection_get_is_connected (dbus_connection)) {
			rb_debug ("closing dbus connection");
			dbus_connection_close (dbus_connection);
		} else {
			rb_debug ("dbus connection already closed");
		}
		dbus_connection_unref (dbus_connection);
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
	DBusMessage *message, *response;
	DBusError dbus_error = {0,};

	if (!dbus_connection_get_is_connected (dbus_connection))
		return FALSE;

	message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
						RB_METADATA_DBUS_OBJECT_PATH,
						RB_METADATA_DBUS_INTERFACE,
						"ping");
	if (!message) {
		return FALSE;
	}
	response = dbus_connection_send_with_reply_and_block (dbus_connection,
							      message,
							      RB_METADATA_DBUS_TIMEOUT,
							      &dbus_error);
	dbus_message_unref (message);
	if (dbus_error_is_set (&dbus_error)) {
		/* ignore 'no reply': just means the service is dead */
		if (strcmp (dbus_error.name, DBUS_ERROR_NO_REPLY)) {
			dbus_set_g_error (error, &dbus_error);
		}
		dbus_error_free (&dbus_error);
		return FALSE;
	}
	dbus_message_unref (response);
	return TRUE;
}

static gboolean
start_metadata_service (GError **error)
{
	DBusError dbus_error = {0,};
	GIOChannel *stdout_channel;
	GIOStatus status;
	gchar *dbus_address = NULL;

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
		/*
		 * Normally, we find the metadata helper in the libexec dir,
		 * but when --enable-uninstalled-build is specified, we look
		 * in the directory it's built in.
		 */
#ifdef USE_UNINSTALLED_DIRS
		g_ptr_array_add (argv, METADATA_UNINSTALLED_DIR "/rhythmbox-metadata");
#else
		g_ptr_array_add (argv, LIBEXEC_DIR G_DIR_SEPARATOR_S INSTALLED_METADATA_HELPER);
#endif
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

	dbus_connection = dbus_connection_open_private (dbus_address, &dbus_error);
	g_free (dbus_address);
	if (!dbus_connection) {
		kill_metadata_service ();

		dbus_set_g_error (error, &dbus_error);
		dbus_error_free (&dbus_error);
		return FALSE;
	}
	dbus_connection_set_exit_on_disconnect (dbus_connection, FALSE);

	dbus_connection_setup_with_g_main (dbus_connection, main_context);

	rb_debug ("Metadata process %d started", metadata_child);
	return TRUE;
}

static void
handle_dbus_error (RBMetaData *md, DBusError *dbus_error, GError **error)
{
	/*
	 * If the error is 'no reply within the specified time',
	 * then we assume that either the metadata process died, or
	 * it's stuck in a loop and needs to be killed.
	 */
	if (strcmp (dbus_error->name, DBUS_ERROR_NO_REPLY) == 0) {
		kill_metadata_service ();

		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("Internal GStreamer problem; file a bug"));
	} else {
		dbus_set_g_error (error, dbus_error);
		dbus_error_free (dbus_error);
	}
}

static void
read_error_from_message (RBMetaData *md, DBusMessageIter *iter, GError **error)
{
	guint32 error_code;
	gchar *error_message;

	if (!rb_metadata_dbus_get_uint32 (iter, &error_code) ||
	    !rb_metadata_dbus_get_string (iter, &error_message)) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_INTERNAL,
			     _("D-BUS communication error"));
		return;
	}

	g_set_error (error, RB_METADATA_ERROR,
		     error_code,
		     "%s", error_message);
	g_free (error_message);
}

/**
 * rb_metadata_load:
 * @md: a #RBMetaData
 * @uri: URI from which to load metadata
 * @error: returns error information
 *
 * Reads metadata information from the specified URI.
 * Once this has returned successfully (with *error == NULL),
 * rb_metadata_get, rb_metadata_get_mime, rb_metadata_has_missing_plugins,
 * and rb_metadata_get_missing_plugins can usefully be called.
 */
void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	DBusMessage *message = NULL;
	DBusMessage *response = NULL;
	DBusMessageIter iter;
	DBusError dbus_error = {0,};
	gboolean ok;
	GError *fake_error = NULL;
	GError *dbus_gerror;

	dbus_gerror = g_error_new (RB_METADATA_ERROR,
				   RB_METADATA_ERROR_INTERNAL,
				   _("D-BUS communication error"));

	if (error == NULL)
		error = &fake_error;

	g_free (md->priv->mimetype);
	md->priv->mimetype = NULL;

	g_free (md->priv->uri);
	md->priv->uri = g_strdup (uri);
	if (uri == NULL)
		return;

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rb_value_free);

	g_static_mutex_lock (&conn_mutex);

	start_metadata_service (error);

	if (*error == NULL) {
		message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
							RB_METADATA_DBUS_OBJECT_PATH,
							RB_METADATA_DBUS_INTERFACE,
							"load");
		if (!message) {
			g_propagate_error (error, dbus_gerror);
		} else if (!dbus_message_append_args (message, DBUS_TYPE_STRING, &uri, DBUS_TYPE_INVALID)) {
			g_propagate_error (error, dbus_gerror);
		}
	}

	if (*error == NULL) {
		rb_debug ("sending metadata load request");
		response = dbus_connection_send_with_reply_and_block (dbus_connection,
								      message,
								      RB_METADATA_DBUS_TIMEOUT,
								      &dbus_error);

		if (!response)
			handle_dbus_error (md, &dbus_error, error);
	}

	if (*error == NULL) {
		if (!dbus_message_iter_init (response, &iter)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't read response message");
		}
	}
	
	if (*error == NULL) {
		if (!rb_metadata_dbus_get_strv (&iter, &md->priv->missing_plugins)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get missing plugin data from response message");
		}
	}

	if (*error == NULL) {
		if (!rb_metadata_dbus_get_strv (&iter, &md->priv->plugin_descriptions)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get missing plugin descriptions from response message");
		}
	}

	/* if we're missing some plugins, we'll need to make sure the
	 * metadata helper rereads the registry before the next load.
	 * the easiest way to do this is to kill it.
	 */
	if (*error == NULL && md->priv->missing_plugins != NULL) {
		rb_debug ("missing plugins; killing metadata service to force registry reload");
		kill_metadata_service ();
	}

	if (*error == NULL) {
		if (!rb_metadata_dbus_get_boolean (&iter, &ok)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get success flag from response message");
		} else if (ok == FALSE) {
			read_error_from_message (md, &iter, error);
		}
	}

	if (*error == NULL) {
		if (!rb_metadata_dbus_get_boolean (&iter, &md->priv->has_audio)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get has-audio flag from response message");
		} else {
			rb_debug ("has audio: %d", md->priv->has_audio);
		}
	}

	if (*error == NULL) {
		if (!rb_metadata_dbus_get_boolean (&iter, &md->priv->has_video)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get has-video flag from response message");
		} else {
			rb_debug ("has video: %d", md->priv->has_video);
		}
	}

	if (*error == NULL) {
		if (!rb_metadata_dbus_get_boolean (&iter, &md->priv->has_other_data)) {
			g_propagate_error (error, dbus_gerror);
			rb_debug ("couldn't get has-other-data flag from response message");
		} else {
			rb_debug ("has other data: %d", md->priv->has_other_data);
		}
	}

	if (ok && *error == NULL) {
		if (!rb_metadata_dbus_get_string (&iter, &md->priv->mimetype)) {
			g_propagate_error (error, dbus_gerror);
		} else {
			rb_debug ("got mimetype: %s", md->priv->mimetype);
		}
	}

	if (ok && *error == NULL) {
		rb_metadata_dbus_read_from_message (md, md->priv->metadata, &iter);
	}

	if (message)
		dbus_message_unref (message);
	if (response)
		dbus_message_unref (response);
	if (*error != dbus_gerror)
		g_error_free (dbus_gerror);
	if (fake_error)
		g_error_free (fake_error);

	g_static_mutex_unlock (&conn_mutex);
}

/**
 * rb_metadata_get_mime:
 * @md: a #RBMetaData
 *
 * Returns the type of the file from which metadata was read.
 * This isn't really a MIME type, but it looks like one.
 *
 * Return value: MIME type-ish string
 */
const char *
rb_metadata_get_mime (RBMetaData *md)
{
	return md->priv->mimetype;
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
	return (md->priv->missing_plugins != NULL);
}

/**
 * rb_metadata_get_missing_plugins:
 * @md: a #RBMetaData
 * @missing_plugins: returns machine-readable missing plugin information
 * @plugin_descriptions: returns human-readable missing plugin descriptions
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
	if (md->priv->missing_plugins == NULL) {
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
 * @val: returns the field value
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
 * @val: the vaule to set
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
 * @mimetype: the MIME type-ish string to check
 *
 * Checks if the metadata writer is capable of updating file metadata
 * for a given media type.
 *
 * Return value: TRUE if the file metadata for the given media type can be updated
 */
gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
	GError *error = NULL;
	DBusMessage *message = NULL;
	DBusMessage *response = NULL;
	gboolean can_save = FALSE;
	DBusError dbus_error = {0,};
	DBusMessageIter iter;
	gboolean ok = TRUE;

	g_static_mutex_lock (&conn_mutex);

	if (start_metadata_service (&error) == FALSE) {
		g_error_free (error);
		ok = FALSE;
	}

	if (ok) {
		message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
							RB_METADATA_DBUS_OBJECT_PATH,
							RB_METADATA_DBUS_INTERFACE,
							"canSave");
		if (!message) {
			ok = FALSE;
		} else if (!dbus_message_append_args (message, DBUS_TYPE_STRING, &mimetype, DBUS_TYPE_INVALID)) {
			ok = FALSE;
		}
	}

	if (ok) {
		response = dbus_connection_send_with_reply_and_block (dbus_connection,
								      message,
								      RB_METADATA_DBUS_TIMEOUT,
								      &dbus_error);
		if (!response) {
			dbus_error_free (&dbus_error);
			ok = FALSE;
		} else if (dbus_message_iter_init (response, &iter)) {
			rb_metadata_dbus_get_boolean (&iter, &can_save);
		}
	}

	if (message)
		dbus_message_unref (message);
	if (response)
		dbus_message_unref (response);
	g_static_mutex_unlock (&conn_mutex);

	return can_save;
}

/**
 * rb_metadata_save:
 * @md: a #RBMetaData
 * @error: returns error information
 *
 * Saves all metadata changes made with rb_metadata_set to the
 * target URI.
 */
void
rb_metadata_save (RBMetaData *md, GError **error)
{
	GError *fake_error = NULL;
	DBusMessage *message = NULL;
	DBusMessage *response = NULL;
	DBusError dbus_error = {0,};
	DBusMessageIter iter;

	if (error == NULL)
		error = &fake_error;

	g_static_mutex_lock (&conn_mutex);

	start_metadata_service (error);

	if (*error == NULL) {
		message = dbus_message_new_method_call (RB_METADATA_DBUS_NAME,
							RB_METADATA_DBUS_OBJECT_PATH,
							RB_METADATA_DBUS_INTERFACE,
							"save");
		if (!message) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("D-BUS communication error"));
		}
	}

	if (*error == NULL) {
		dbus_message_iter_init_append (message, &iter);
		if (!rb_metadata_dbus_add_to_message (md, &iter)) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("D-BUS communication error"));
		}
	}

	if (*error == NULL) {
		response = dbus_connection_send_with_reply_and_block (dbus_connection,
								      message,
								      RB_METADATA_SAVE_DBUS_TIMEOUT,
								      &dbus_error);
		if (!response) {
			handle_dbus_error (md, &dbus_error, error);
		} else if (dbus_message_iter_init (response, &iter)) {
			/* if there's any return data at all, it'll be an error */
			read_error_from_message (md, &iter, error);
		}
	}

	if (message)
		dbus_message_unref (message);
	if (response)
		dbus_message_unref (response);
	if (fake_error)
		g_error_free (fake_error);

	g_static_mutex_unlock (&conn_mutex);
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
