/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementation of DAAP (iTunes Music Sharing) hashing, parsing, connection
 *
 *  Copyright (C) 2004-2005 Charles Schmidt <cschmidt2@emich.edu>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include <glib/gi18n.h>
#include <gdk/gdk.h>

#include <libsoup/soup.h>
#include <libsoup/soup-connection.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-uri.h>

#include "rb-daap-hash.h"
#include "rb-daap-connection.h"
#include "rb-daap-structure.h"
#include "rb-marshal.h"

#include "rb-debug.h"
#include "rb-util.h"

#define RB_DAAP_USER_AGENT "iTunes/4.6 (Windows; N)"

static void      rb_daap_connection_dispose      (GObject *obj);
static void      rb_daap_connection_set_property (GObject *object,
						  guint prop_id,
						  const GValue *value,
						  GParamSpec *pspec);
static void      rb_daap_connection_get_property (GObject *object,
						  guint prop_id,
						  GValue *value,
						  GParamSpec *pspec);

static gboolean rb_daap_connection_do_something  (RBDAAPConnection *connection);
static void     rb_daap_connection_state_done    (RBDAAPConnection *connection,
						  gboolean           result);

static gboolean emit_progress_idle (RBDAAPConnection *connection);

G_DEFINE_TYPE (RBDAAPConnection, rb_daap_connection, G_TYPE_OBJECT)

#define RB_DAAP_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_CONNECTION, RBDAAPConnectionPrivate))

typedef void (* RBDAAPResponseHandler) (RBDAAPConnection *connection,
					guint status,
					GNode *structure);

struct RBDAAPConnectionPrivate {
	char *name;
	gboolean password_protected;
	char *username;
	char *password;
	char *host;
	guint port;
	
	gboolean is_connected;
	gboolean is_connecting;

	SoupSession *session;
	SoupUri *base_uri;
	gchar *daap_base_uri;
	
	gdouble daap_version;
	guint32 session_id;
	gint revision_number;

	gint request_id;
	gint database_id;

	guint reading_playlist;
	GSList *playlists;
	GHashTable *item_id_to_uri;

	RhythmDB *db;
	RhythmDBEntryType db_type;

	RBDAAPConnectionState state;
	RBDAAPResponseHandler response_handler;
	gboolean use_response_handler_thread;
	float progress;

	guint emit_progress_id;
	guint do_something_id;

	gboolean result;
	char *last_error_message;
};


enum {
	PROP_0,
	PROP_DB,
	PROP_NAME,
	PROP_ENTRY_TYPE,
	PROP_PASSWORD_PROTECTED,
	PROP_HOST,
	PROP_PORT,
};

enum { 
	AUTHENTICATE,
	CONNECTING,
	CONNECTED,
	DISCONNECTED,
	OPERATION_DONE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void
rb_daap_connection_finalize (GObject *object)
{
	RBDAAPConnection *connection;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DAAP_CONNECTION (object));

	connection = RB_DAAP_CONNECTION (object);

	g_return_if_fail (connection->priv != NULL);

	rb_debug ("Finalize");

	G_OBJECT_CLASS (rb_daap_connection_parent_class)->finalize (object);
}

static void
rb_daap_connection_class_init (RBDAAPConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize	   = rb_daap_connection_finalize;
	object_class->dispose       = rb_daap_connection_dispose;
	object_class->set_property = rb_daap_connection_set_property;
	object_class->get_property = rb_daap_connection_get_property;

	g_type_class_add_private (klass, sizeof (RBDAAPConnectionPrivate));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
							     "entry type",
							     "RhythmDBEntryType",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_PASSWORD_PROTECTED,
					 g_param_spec_boolean ("password-protected",
							       "password protected",
							       "connection is password protected",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
						 	      "connection name",
							      "connection name",
							      NULL,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_HOST,
					 g_param_spec_string ("host",
						 	      "host",
							      "host",
							      NULL,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PORT,
					 g_param_spec_uint ("port",
							    "port",
							    "port",
							    0, G_MAXINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals [AUTHENTICATE] = g_signal_new ("authenticate",
					       G_TYPE_FROM_CLASS (object_class),
					       G_SIGNAL_RUN_LAST,
					       G_STRUCT_OFFSET (RBDAAPConnectionClass, authenticate),
					       NULL,
					       NULL,
					       rb_marshal_STRING__STRING,
					       G_TYPE_STRING,
					       1, G_TYPE_STRING);
	signals [CONNECTING] = g_signal_new ("connecting",
					     G_TYPE_FROM_CLASS (object_class),
					     G_SIGNAL_RUN_LAST,
					     G_STRUCT_OFFSET (RBDAAPConnectionClass, connecting),
					     NULL,
					     NULL,
					     rb_marshal_VOID__ULONG_FLOAT,
					     G_TYPE_NONE,
					     2, G_TYPE_ULONG, G_TYPE_FLOAT);
	signals [CONNECTED] = g_signal_new ("connected",
					    G_TYPE_FROM_CLASS (object_class),
					    G_SIGNAL_RUN_LAST,
					    G_STRUCT_OFFSET (RBDAAPConnectionClass, connected),
					    NULL,
					    NULL,
					    g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					    0);
	signals [DISCONNECTED] = g_signal_new ("disconnected",
					       G_TYPE_FROM_CLASS (object_class),
					       G_SIGNAL_RUN_LAST,
					       G_STRUCT_OFFSET (RBDAAPConnectionClass, disconnected),
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);
	signals [OPERATION_DONE] = g_signal_new ("operation-done",
						 G_TYPE_FROM_CLASS (object_class),
						 G_SIGNAL_RUN_FIRST,
						 G_STRUCT_OFFSET (RBDAAPConnectionClass, operation_done),
						 NULL,
						 NULL,
						 g_cclosure_marshal_VOID__VOID,
						 G_TYPE_NONE,
						 0);
}

static void
rb_daap_connection_init (RBDAAPConnection *connection)
{
	connection->priv = RB_DAAP_CONNECTION_GET_PRIVATE (connection);

	connection->priv->username = g_strdup_printf ("Rhythmbox_%s", VERSION);
	connection->priv->db_type = RHYTHMDB_ENTRY_TYPE_INVALID;
}


static char *
connection_get_password (RBDAAPConnection *connection)
{
	char *password = NULL;;

	GDK_THREADS_ENTER ();
	g_signal_emit (connection,
		       signals [AUTHENTICATE],
		       0,
		       connection->priv->name,
		       &password);
	GDK_THREADS_LEAVE ();

	return password;
}

static void
connection_connected (RBDAAPConnection *connection)
{
	rb_debug ("Emitting connected");

	connection->priv->is_connected = TRUE;

	g_signal_emit (connection,
		       signals [CONNECTED],
		       0);
}

static void
connection_disconnected (RBDAAPConnection *connection)
{
	rb_debug ("Emitting disconnected");

	connection->priv->is_connected = FALSE;

	g_signal_emit (connection,
		       signals [DISCONNECTED],
		       0);
}

static void
connection_operation_done (RBDAAPConnection *connection)
{
	rb_debug ("Emitting operation done");

	g_signal_emit (connection,
		       signals [OPERATION_DONE],
		       0);
}

static SoupMessage * 
build_message (RBDAAPConnection *connection, 
	       const char       *path, 
	       gboolean          need_hash, 
	       gdouble           version, 
	       gint              req_id, 
	       gboolean          send_close)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	SoupMessage *message = NULL;
	SoupUri *uri = NULL;
	
	uri = soup_uri_new_with_base (priv->base_uri, path);
	if (uri == NULL) {
		return NULL;
	}
	
	message = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	soup_message_set_http_version (message, SOUP_HTTP_1_1);
	
	soup_message_add_header (message->request_headers, "Client-DAAP-Version", 	"3.0");
	soup_message_add_header (message->request_headers, "Accept-Language", 		"en-us, en;q=5.0");
#ifdef HAVE_LIBZ
	soup_message_add_header (message->request_headers, "Accept-Encoding",		"gzip");
#endif
	soup_message_add_header (message->request_headers, "Client-DAAP-Access-Index", 	"2");

	if (priv->password_protected) {
		char *h;
		char *user_pass;
		char *token;

		user_pass = g_strdup_printf ("%s:%s", priv->username, priv->password);
		token = soup_base64_encode (user_pass, strlen (user_pass));
		h = g_strdup_printf ("Basic %s", token);

		g_free (token);
		g_free (user_pass);

		soup_message_add_header (message->request_headers, "Authorization", h);
		g_free (h);
	}
	
	if (need_hash) {
		gchar hash[33] = {0};
		gchar *no_daap_path = (gchar *)path;
		
		if (g_strncasecmp (path, "daap://", 7) == 0) {
			no_daap_path = strstr (path, "/data");
		}

		rb_daap_hash_generate ((short)floor (version), (const guchar*)no_daap_path, 2, (guchar*)hash, req_id);

		soup_message_add_header (message->request_headers, "Client-DAAP-Validation", hash);
	}
	if (send_close) {
		soup_message_add_header (message->request_headers, "Connection", "close");
	}

	soup_uri_free (uri);
	
	return message;
}

#ifdef HAVE_LIBZ
static void
*g_zalloc_wrapper (voidpf opaque, uInt items, uInt size)
{
	if ((items != 0) && (size >= G_MAXUINT/items)) {
		return Z_NULL;
	}
	if ((size != 0) && (items >= G_MAXUINT/size)) {
		return Z_NULL;
	}
	return g_malloc0 (items * size);
}

static void
g_zfree_wrapper (voidpf opaque, voidpf address)
{
	g_free (address);
}
#endif

static void
connection_set_error_message (RBDAAPConnection *connection,
			      const char       *message)
{
	/* FIXME: obtain a lock */
	if (connection->priv->last_error_message != NULL) {
		g_free (connection->priv->last_error_message);
	}
	connection->priv->last_error_message = g_strdup (message);
}

typedef struct {
	SoupMessage *message;
	int status;
	RBDAAPConnection *connection;
} DAAPResponseData;

static void
actual_http_response_handler (DAAPResponseData *data)
{
	RBDAAPConnectionPrivate *priv;
	GNode *structure;
	char *response;
	const char *encoding_header;
	char *message_path;
	int response_length;
	
	priv = data->connection->priv;
	structure = NULL;
	response = data->message->response.body;
	encoding_header = NULL;
	response_length = data->message->response.length;
	
	message_path = soup_uri_to_string (soup_message_get_uri (data->message), FALSE);

	rb_debug ("Received response from %s: %d, %s\n", 
		  message_path,
		  data->message->status_code,
		  data->message->reason_phrase);

	if (data->message->response_headers) {
		encoding_header = soup_message_get_header (data->message->response_headers, "Content-Encoding");
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (data->status) && encoding_header && strcmp (encoding_header, "gzip") == 0) {
#ifdef HAVE_LIBZ
		z_stream stream;
		char *new_response;
		unsigned int factor = 4;
		unsigned int unc_size = response_length * factor;

		stream.next_in = (unsigned char *)response;
		stream.avail_in = response_length;
		stream.total_in = 0;

		new_response = g_malloc (unc_size + 1);
		stream.next_out = (unsigned char *)new_response;
		stream.avail_out = unc_size;
		stream.total_out = 0;
		stream.zalloc = g_zalloc_wrapper;
		stream.zfree = g_zfree_wrapper;
		stream.opaque = NULL;

		rb_profile_start ("decompressing DAAP response");
		
		if (inflateInit2 (&stream, 32 /* auto-detect */ + 15 /* max */ ) != Z_OK) {
			inflateEnd (&stream);
			g_free (new_response);
			rb_debug ("Unable to decompress response from %s",
				  message_path);
			data->status = SOUP_STATUS_MALFORMED;
			rb_profile_end ("decompressing DAAP response (failed)");
		} else {
			do {
				int z_res;
			       
				rb_profile_start ("attempting inflate");
				z_res = inflate (&stream, Z_FINISH);
				if (z_res == Z_STREAM_END) {
					rb_profile_end ("attempting inflate (done)");
					break;
				}
				if ((z_res != Z_OK && z_res != Z_BUF_ERROR) || stream.avail_out != 0 || unc_size > 40*1000*1000) {
					inflateEnd (&stream);
					g_free (new_response);
					new_response = NULL;
					rb_profile_end ("attempting inflate (error)");
					break;
				}

				factor *= 4;
				unc_size = (response_length * factor);
				/* unc_size can't grow bigger than 40MB, so
				 * unc_size can't overflow, and this realloc
				 * call is safe
				 */
				new_response = g_realloc (new_response, unc_size + 1);
				stream.next_out = (unsigned char *)(new_response + stream.total_out);
				stream.avail_out = unc_size - stream.total_out;
				rb_profile_end ("attempting inflate (incomplete)");
			} while (1);
		}
		rb_profile_end ("decompressing DAAP response (successful)");

		if (new_response) {
			response = new_response;
			response_length = stream.total_out;
		}
#else
		rb_debug ("Received compressed response from %s but can't handle it",
			  message_path);
		data->status = SOUP_STATUS_MALFORMED;
#endif
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (data->status)) {
		RBDAAPItem *item;

		if (!rb_is_main_thread ()) {
			priv->progress = -1.0f;
			if (priv->emit_progress_id != 0) {
				g_source_remove (priv->emit_progress_id);
			}
			priv->emit_progress_id = g_idle_add ((GSourceFunc) emit_progress_idle, data->connection);
		}
		rb_profile_start ("parsing DAAP response");
		structure = rb_daap_structure_parse (response, response_length);
		if (structure == NULL) {
			rb_debug ("No daap structure returned from %s",
				  message_path);

			data->status = SOUP_STATUS_MALFORMED;
			rb_profile_end ("parsing DAAP response (failed)");
		} else {
			int dmap_status = 0;
			item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MSTT);
			if (item)
				dmap_status = g_value_get_int (&(item->content));

			if (dmap_status != 200) {
				rb_debug ("Error, dmap.status is not 200 in response from %s",
					  message_path);

				data->status = SOUP_STATUS_MALFORMED;
			}
			rb_profile_end ("parsing DAAP response (successful)");
		}
		if (! rb_is_main_thread ()) {
			priv->progress = 1.0f;
			if (priv->emit_progress_id != 0) {
				g_source_remove (priv->emit_progress_id);
			}
			priv->emit_progress_id = g_idle_add ((GSourceFunc) emit_progress_idle, data->connection);
		}
	} else {
		rb_debug ("Error getting %s: %d, %s\n", 
			  message_path,
			  data->message->status_code,
			  data->message->reason_phrase);
		connection_set_error_message (data->connection, data->message->reason_phrase);
	}

	if (priv->response_handler) {
		RBDAAPResponseHandler h = priv->response_handler;
		priv->response_handler = NULL;
		(*h) (data->connection, data->status, structure);
	}

	if (structure) {
		rb_daap_structure_destroy (structure);
	}

	if (response != data->message->response.body) {
		g_free (response);
	}

	g_free (message_path);
	g_object_unref (G_OBJECT (data->connection));
	g_object_unref (G_OBJECT (data->message));
	g_free (data);
}

static void
http_response_handler (SoupMessage      *message,
		       RBDAAPConnection *connection)
{
	DAAPResponseData *data;
	int response_length;

	if (message->status_code == SOUP_STATUS_CANCELLED) {
		rb_debug ("Message cancelled");
		return;
	}

	data = g_new0 (DAAPResponseData, 1);
	data->status = message->status_code;
	response_length = message->response.length;

	g_object_ref (G_OBJECT (connection));
	data->connection = connection;
	
	g_object_ref (G_OBJECT (message));
	data->message = message;

	if (response_length >= G_MAXUINT/4 - 1) {
		/* If response_length is too big, 
		 * the g_malloc (unc_size + 1) below would overflow 
		 */
		data->status = SOUP_STATUS_MALFORMED;
	}

	/* to avoid blocking the UI, handle big responses in a separate thread */
	if (SOUP_STATUS_IS_SUCCESSFUL (data->status) && connection->priv->use_response_handler_thread) {
		GError *error = NULL;
		rb_debug ("creating thread to handle daap response");
		g_thread_create ((GThreadFunc) actual_http_response_handler,
				 data,
				 FALSE,
				 &error);
		if (error) {
			g_warning ("fuck");
		}
	} else {
		actual_http_response_handler (data);
	}
}

static gboolean
http_get (RBDAAPConnection     *connection, 
	  const char           *path, 
	  gboolean              need_hash, 
	  gdouble               version, 
	  gint                  req_id, 
	  gboolean              send_close,
	  RBDAAPResponseHandler handler,
	  gboolean              use_thread)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	SoupMessage *message;
       
	message = build_message (connection, path, need_hash, version, req_id, send_close);
	if (message == NULL) {
		rb_debug ("Error building message for http://%s:%d/%s", 
			  priv->base_uri->host,
			  priv->base_uri->port,
			  path);
		return FALSE;
	}
	
	priv->use_response_handler_thread = use_thread;
	priv->response_handler = handler;
	soup_session_queue_message (priv->session, message,
				    (SoupMessageCallbackFn) http_response_handler, 
				    connection);
	rb_debug ("Queued message for http://%s:%d/%s",
		  priv->base_uri->host,
		  priv->base_uri->port,
		  path);
	return TRUE;
}

static void 
entry_set_string_prop (RhythmDB        *db, 
		       RhythmDBEntry   *entry,
		       RhythmDBPropType propid, 
		       const char      *str)
{
	GValue value = {0,};
	gchar *tmp;

	if (str == NULL || *str == '\0' || !g_utf8_validate (str, -1, NULL)) {
		tmp = g_strdup (_("Unknown"));
	} else {
		tmp = g_strdup (str);
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string_take_ownership (&value, tmp);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static gboolean
emit_progress_idle (RBDAAPConnection *connection)
{
	rb_debug ("Emitting progress");

	GDK_THREADS_ENTER ();
	g_signal_emit (G_OBJECT (connection), signals[CONNECTING], 0, 
		       connection->priv->state,
		       connection->priv->progress);
	connection->priv->emit_progress_id = 0;
	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
handle_server_info (RBDAAPConnection *connection,
		    guint             status,
		    GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPItem *item = NULL;

	if (!SOUP_STATUS_IS_SUCCESSFUL (status) || structure == NULL) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	
	/* get the daap version number */
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_APRO);
	if (item == NULL) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->daap_version = g_value_get_double (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_login (RBDAAPConnection *connection,
	      guint             status,
	      GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPItem *item = NULL;

	if (status == SOUP_STATUS_UNAUTHORIZED || status == SOUP_STATUS_FORBIDDEN) {
		rb_debug ("Incorrect password");
		priv->state = DAAP_GET_PASSWORD;
		if (priv->do_something_id != 0) {
			g_source_remove (priv->do_something_id);
		}
		priv->do_something_id = g_idle_add ((GSourceFunc) rb_daap_connection_do_something, connection);
		return;
	}

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MLID);
	if (item == NULL) {
		rb_debug ("Could not find daap.sessionid item in /login");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->session_id = (guint32) g_value_get_int (&(item->content));

	connection_connected (connection);

	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_update (RBDAAPConnection *connection,
	       guint             status,
	       GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPItem *item;

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	/* get a revision number */
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUSR);
	if (item == NULL) {
		rb_debug ("Could not find daap.serverrevision item in /update");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->revision_number = g_value_get_int (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void 
handle_database_info (RBDAAPConnection *connection,
		      guint             status,
		      GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gint n_databases = 0;

	/* get a list of databases, there should be only 1 */

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	n_databases = g_value_get_int (&(item->content));
	if (n_databases != 1) {
		rb_debug ("Host seems to have more than 1 database, how strange\n");
	}
	
	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (listing_node->children, RB_DAAP_CC_MIID);
	if (item == NULL) {
		rb_debug ("Could not find dmap.itemid item in /databases");
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	
	priv->database_id = g_value_get_int (&(item->content));
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_song_listing (RBDAAPConnection *connection,
		     guint             status,
		     GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPItem *item = NULL;
	GNode *listing_node;
	gint returned_count;
	gint i;
	GNode *n;
	gint specified_total_count;
	gboolean update_type;
	gint commit_batch;

	/* get the songs */
	
	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MRCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.returnedcount item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	returned_count = g_value_get_int (&(item->content));
	if (returned_count > 20) {
		commit_batch = returned_count / 20;
	} else {
		commit_batch = 1;
	}
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MTCO);
	if (item == NULL) {
		rb_debug ("Could not find dmap.specifiedtotalcount item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	specified_total_count = g_value_get_int (&(item->content));
	
	item = rb_daap_structure_find_item (structure, RB_DAAP_CC_MUTY);
	if (item == NULL) {
		rb_debug ("Could not find dmap.updatetype item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}
	update_type = g_value_get_char (&(item->content));

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/items",
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	priv->item_id_to_uri = g_hash_table_new_full ((GHashFunc)g_direct_hash,(GEqualFunc)g_direct_equal, NULL, g_free);
	
	rb_profile_start ("handling song listing");
	priv->progress = 0.0f;
	if (priv->emit_progress_id != 0) {
		g_source_remove (priv->emit_progress_id);
	}
	connection->priv->emit_progress_id = g_idle_add ((GSourceFunc) emit_progress_idle, connection);

	for (i = 0, n = listing_node->children; n; i++, n = n->next) {
		GNode *n2;
		RhythmDBEntry *entry = NULL;
		GValue value = {0,};
		gchar *uri = NULL;
		gint item_id = 0;
		const gchar *title = NULL;
		const gchar *album = NULL;
		const gchar *artist = NULL;
		const gchar *format = NULL;
		const gchar *genre = NULL;
		gint length = 0;
		gint track_number = 0;
		gint disc_number = 0;
		gint year = 0;
		gint size = 0;
		gint bitrate = 0;
		
		for (n2 = n->children; n2; n2 = n2->next) {
			RBDAAPItem *meta_item;
			
			meta_item = n2->data;

			switch (meta_item->content_code) {
				case RB_DAAP_CC_MIID:
					item_id = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_MINM:
					title = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASAL:
					album = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASAR:
					artist = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASFM:
					format = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASGN:
					genre = g_value_get_string (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASTM:
					length = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASTN:
					track_number = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASDN:
					disc_number = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASYR:
					year = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASSZ:
					size = g_value_get_int (&(meta_item->content));
					break;
				case RB_DAAP_CC_ASBR:
					bitrate = g_value_get_int (&(meta_item->content));
					break;
				default:
					break;
			}
		}

		/*if (connection->daap_version == 3.0) {*/
			uri = g_strdup_printf ("%s/databases/%d/items/%d.%s?session-id=%u",
					       priv->daap_base_uri, 
					       priv->database_id, 
					       item_id, format, 
					       priv->session_id);
		/*} else {*/
		/* uri should be 
		 * "/databases/%d/items/%d.%s?session-id=%u&revision-id=%d";
		 * but its not going to work cause the other parts of the code 
		 * depend on the uri to have the ip address so that the
		 * RBDAAPSource can be found to ++request_id
		 * maybe just /dont/ support older itunes.  doesn't seem 
		 * unreasonable to me, honestly
		 */
		/*}*/
		entry = rhythmdb_entry_new (priv->db, priv->db_type, uri);
		if (entry == NULL) {
			rb_debug ("cannot create entry for daap track %s", uri);
			continue;
		}
		g_hash_table_insert (priv->item_id_to_uri, GINT_TO_POINTER (item_id), uri);

		/* year */
		if (year != 0) {
			GDate *date;
			gulong julian;

			/* create dummy date with given year */
			date = g_date_new_dmy (1, G_DATE_JANUARY, year); 
			julian = g_date_get_julian (date);
			g_date_free (date);

			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value,julian);
			rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DATE, &value);
			g_value_unset (&value);
		} 

		/* track number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)track_number);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &value);
		g_value_unset (&value);

		/* disc number */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)disc_number);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
		g_value_unset (&value);

		/* bitrate */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)bitrate);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_BITRATE, &value);
		g_value_unset (&value);
		
		/* length */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,(gulong)length / 1000);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DURATION, &value);
		g_value_unset (&value);

		/* file size */
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64(&value,(gint64)size);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_FILE_SIZE, &value);
		g_value_unset (&value);

		/* title */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_TITLE, title);

		/* album */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ALBUM, album);

		/* artist */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ARTIST, artist);

		/* genre */
		entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_GENRE, genre);

		if (i % commit_batch == 0) {
			connection->priv->progress = ((float)i / (float)returned_count);
			if (priv->emit_progress_id != 0) {
				g_source_remove (connection->priv->emit_progress_id);
			}
			connection->priv->emit_progress_id = g_idle_add ((GSourceFunc) emit_progress_idle, connection);
			rhythmdb_commit (priv->db);
		}
	}
	rb_profile_end ("handling song listing");
		
	rb_daap_connection_state_done (connection, TRUE);
}

/* FIXME
 * what we really should do is only get a list of playlists and their ids
 * then when they are clicked on ('activate'd) by the user, get a list of
 * the files that are actually in them.  This will speed up initial daap 
 * connection times and reduce memory consumption.
 */

static void
handle_playlists (RBDAAPConnection *connection,
		  guint             status,
		  GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	GNode *listing_node;
	gint i;
	GNode *n;
	
	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/containers", 
			  priv->database_id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	for (i = 0, n = listing_node->children; n; n = n->next, i++) {
		RBDAAPItem *item;
		gint id;
		gchar *name;
		RBDAAPPlaylist *playlist;
		
		item = rb_daap_structure_find_item (n, RB_DAAP_CC_ABPL);
		if (item != NULL) {
			continue;
		}

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MIID);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemid item in /databases/%d/containers",
				  priv->database_id);
			continue;
		}
		id = g_value_get_int (&(item->content));

		item = rb_daap_structure_find_item (n, RB_DAAP_CC_MINM);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemname item in /databases/%d/containers",
				  priv->database_id);
			continue;
		}
		name = g_value_dup_string (&(item->content));

		playlist = g_new0 (RBDAAPPlaylist, 1);
		playlist->id = id;
		playlist->name = name;
		rb_debug ("Got playlist %p: name %s, id %d", playlist, playlist->name, playlist->id);

		priv->playlists = g_slist_prepend (priv->playlists, playlist);
	}

	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_playlist_entries (RBDAAPConnection *connection,
			 guint             status,
			 GNode            *structure)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	RBDAAPPlaylist *playlist;
	GNode *listing_node;
	GNode *node;
	gint i;
	GList *playlist_uris = NULL;

	if (structure == NULL || SOUP_STATUS_IS_SUCCESSFUL (status) == FALSE) {
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	playlist = (RBDAAPPlaylist *)g_slist_nth_data (priv->playlists, priv->reading_playlist);
	g_assert (playlist);

	listing_node = rb_daap_structure_find_node (structure, RB_DAAP_CC_MLCL);
	if (listing_node == NULL) {
		rb_debug ("Could not find dmap.listing item in /databases/%d/containers/%d/items", 
			  priv->database_id, playlist->id);
		rb_daap_connection_state_done (connection, FALSE);
		return;
	}

	rb_profile_start ("handling playlist entries");
	for (i = 0, node = listing_node->children; node; node = node->next, i++) {
		gchar *item_uri;
		gint playlist_item_id;
		RBDAAPItem *item;

		item = rb_daap_structure_find_item (node, RB_DAAP_CC_MIID);
		if (item == NULL) {
			rb_debug ("Could not find dmap.itemid item in /databases/%d/containers/%d/items",
				  priv->database_id, playlist->id);
			continue;
		}
		playlist_item_id = g_value_get_int (&(item->content));
	
		item_uri = g_hash_table_lookup (priv->item_id_to_uri, GINT_TO_POINTER (playlist_item_id));
		if (item_uri == NULL) {
			rb_debug ("Entry %d in playlist %s doesn't exist in the database\n", 
				  playlist_item_id, playlist->name);
			continue;
		}
		
		playlist_uris = g_list_prepend (playlist_uris, g_strdup (item_uri));
	}
	rb_profile_end ("handling playlist entries");

	playlist->uris = playlist_uris;
	rb_daap_connection_state_done (connection, TRUE);
}

static void
handle_logout (RBDAAPConnection *connection,
	       guint             status,
	       GNode            *structure)
{
	connection_disconnected (connection);

	/* is there any point handling errors here? */
	rb_daap_connection_state_done (connection, TRUE);
}
	
RBDAAPConnection * 
rb_daap_connection_new (const char       *name,
			const char       *host,
			int               port, 
			gboolean          password_protected,
			RhythmDB         *db, 
			RhythmDBEntryType type)
{
	return g_object_new (RB_TYPE_DAAP_CONNECTION,
			     "name", name,
			     "entry-type", type,
			     "password-protected", password_protected,
			     "db", db,
			     "host", host,
			     "port", port,
			     NULL);
}

gboolean
rb_daap_connection_is_connected (RBDAAPConnection *connection)
{
	g_return_val_if_fail (RB_IS_DAAP_CONNECTION (connection), FALSE);

	return connection->priv->is_connected;
}

typedef struct {
	RBDAAPConnection        *connection;
	RBDAAPConnectionCallback callback;
	gpointer                 data;
	GDestroyNotify           destroy;
} ConnectionResponseData;

static void
connection_response_data_free (gpointer data)
{
	ConnectionResponseData *rdata = data;

	g_object_unref (rdata->connection);
	g_free (rdata);
}

static void
connected_cb (RBDAAPConnection       *connection,
	      ConnectionResponseData *rdata)
{
	gboolean result;

	rb_debug ("Connected callback");

	connection->priv->is_connecting = FALSE;

	g_signal_handlers_disconnect_by_func (connection,
					      G_CALLBACK (connected_cb),
					      rdata);

	/* if connected then we succeeded */
	result = rb_daap_connection_is_connected (connection);
	
	if (rdata->callback) {
		rdata->callback (rdata->connection,
				 result,
				 rdata->connection->priv->last_error_message,
				 rdata->data);
	}

	if (rdata->destroy) {
		rdata->destroy (rdata);
	}
}

void
rb_daap_connection_connect (RBDAAPConnection        *connection,
			    RBDAAPConnectionCallback callback,
			    gpointer                 user_data)
{
	ConnectionResponseData *rdata;
	char                   *path;

	g_return_if_fail (RB_IS_DAAP_CONNECTION (connection));
	g_return_if_fail (connection->priv->state == DAAP_GET_INFO);

	rb_debug ("Creating new DAAP connection to %s:%d", connection->priv->host, connection->priv->port);

	connection->priv->session = soup_session_async_new ();

	path = g_strdup_printf ("http://%s:%d", connection->priv->host, connection->priv->port);
	connection->priv->base_uri = soup_uri_new (path);
	g_free (path);

	if (connection->priv->base_uri == NULL) {
		rb_debug ("Error parsing http://%s:%d", connection->priv->host, connection->priv->port);
		/* FIXME: do callback */
		return;
	}

	connection->priv->daap_base_uri = g_strdup_printf ("daap://%s:%d", connection->priv->host, connection->priv->port);

	rdata = g_new (ConnectionResponseData, 1);
	rdata->connection = g_object_ref (connection);
	rdata->callback = callback;
	rdata->data = user_data;
	rdata->destroy = connection_response_data_free;
	g_signal_connect (connection, "operation-done", G_CALLBACK (connected_cb), rdata);

	if (connection->priv->do_something_id != 0) {
		g_source_remove (connection->priv->do_something_id);
	}

	connection->priv->is_connecting = TRUE;
	connection->priv->do_something_id = g_idle_add ((GSourceFunc) rb_daap_connection_do_something, connection);
}

static void
disconnected_cb (RBDAAPConnection       *connection,
		 ConnectionResponseData *rdata)
{
	gboolean result;

	rb_debug ("Disconnected callback");

	g_signal_handlers_disconnect_by_func (connection,
					      G_CALLBACK (disconnected_cb),
					      rdata);

	/* if not connected then we succeeded */
	result = ! rb_daap_connection_is_connected (connection);

	if (rdata->callback) {
		rdata->callback (rdata->connection,
				 result,
				 rdata->connection->priv->last_error_message,
				 rdata->data);
	}

	if (rdata->destroy) {
		rdata->destroy (rdata);
	}
}

static void
rb_daap_connection_finish (RBDAAPConnection *connection)
{
	g_return_if_fail (RB_IS_DAAP_CONNECTION (connection));

	rb_debug ("DAAP finish");
	connection->priv->state = DAAP_DONE;
	connection->priv->progress = 1.0f;

	connection_operation_done (connection);
}

void
rb_daap_connection_disconnect (RBDAAPConnection        *connection,
			       RBDAAPConnectionCallback callback,
			       gpointer                 user_data)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	ConnectionResponseData  *rdata;

	g_return_if_fail (RB_IS_DAAP_CONNECTION (connection));

	rb_debug ("Disconnecting");

	if (connection->priv->is_connecting) {
		/* this is a special case where the async connection
		   hasn't returned yet so we need to force the connection
		   to finish */
		priv->state = DAAP_DONE;
		rb_daap_connection_finish (connection);
	}

	rdata = g_new (ConnectionResponseData, 1);
	rdata->connection = g_object_ref (connection);
	rdata->callback = callback;
	rdata->data = user_data;
	rdata->destroy = connection_response_data_free;

	g_signal_connect (connection, "operation-done", G_CALLBACK (disconnected_cb), rdata);

	if (priv->do_something_id != 0) {
		g_source_remove (priv->do_something_id);
	}

	if (! connection->priv->is_connected) {
		priv->state = DAAP_DONE;
		rb_daap_connection_finish (connection);
	} else {
		priv->state = DAAP_LOGOUT;

		priv->do_something_id = g_idle_add ((GSourceFunc) rb_daap_connection_do_something, connection);
	}
}

static void
rb_daap_connection_state_done (RBDAAPConnection *connection,
			       gboolean          result)
{
	RBDAAPConnectionPrivate *priv = connection->priv;

	rb_debug ("Transitioning to next state from %d", priv->state);

	if (result == FALSE) {
		priv->state = DAAP_DONE;
		priv->result = FALSE;
	} else {
		switch (priv->state) {
		case DAAP_GET_PLAYLISTS:
			if (priv->playlists == NULL)
				priv->state = DAAP_DONE;
			else
				priv->state = DAAP_GET_PLAYLIST_ENTRIES;
			break;
		case DAAP_GET_PLAYLIST_ENTRIES:
			/* keep reading playlists until we've got them all */
			if (++priv->reading_playlist >= g_slist_length (priv->playlists))
				priv->state = DAAP_DONE;
			break;

		case DAAP_LOGOUT:
			priv->state = DAAP_DONE;
			break;

		case DAAP_DONE:
			/* uhh.. */
			rb_debug ("This should never happen.");
			break;

		default:
			/* in most states, we just move on to the next */
			if (priv->state > DAAP_DONE) {
				rb_debug ("This should REALLY never happen.");
				return;
			}
			priv->state++;
			break;
		}

		priv->progress = 1.0f;
		if (connection->priv->emit_progress_id != 0) {
			g_source_remove (connection->priv->emit_progress_id);
		}
		connection->priv->emit_progress_id = g_idle_add ((GSourceFunc) emit_progress_idle, connection);
	}

	if (priv->do_something_id != 0) {
		g_source_remove (priv->do_something_id);
	}
	priv->do_something_id = g_idle_add ((GSourceFunc) rb_daap_connection_do_something, connection);
}

static gboolean
rb_daap_connection_do_something (RBDAAPConnection *connection)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	char *path;

	rb_debug ("Doing something for state: %d", priv->state);

	priv->do_something_id = 0;

	switch (priv->state) {
	case DAAP_GET_INFO:
		rb_debug ("Getting DAAP server info");
		if (! http_get (connection, "/server-info", FALSE, 0.0, 0, FALSE, 
				(RBDAAPResponseHandler) handle_server_info, FALSE)) {
			rb_debug ("Could not get DAAP connection info");
			rb_daap_connection_state_done (connection, FALSE);
		}
		break;
	
	case DAAP_GET_PASSWORD:
		if (priv->password_protected) {
			/* FIXME this bit is still synchronous */
			rb_debug ("Need a password for %s", priv->name);
			g_free (priv->password);
			priv->password = connection_get_password (connection);

			if (priv->password == NULL || priv->password[0] == '\0') {
				rb_debug ("Password entry cancelled");
				priv->result = FALSE;
				priv->state = DAAP_DONE;
				rb_daap_connection_do_something (connection);
				return FALSE;
			}

			/* If the share went away while we were asking for the password,
			 * don't bother trying to log in.
			 */
			if (priv->state != DAAP_GET_PASSWORD) {
				return FALSE;
			}
		}

		/* otherwise, fall through */
		priv->state = DAAP_LOGIN;
		
	case DAAP_LOGIN:
		rb_debug ("Logging into DAAP server");
		if (! http_get (connection, "/login", FALSE, 0.0, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_login, FALSE)) {
			rb_debug ("Could not login to DAAP server");
			/* FIXME: set state back to GET_PASSWORD to try again */
			rb_daap_connection_state_done (connection, FALSE);
		}

		break;

	case DAAP_GET_REVISION_NUMBER:
		rb_debug ("Getting DAAP server database revision number");
		path = g_strdup_printf ("/update?session-id=%u&revision-number=1", priv->session_id);
		if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_update, FALSE)) {
			rb_debug ("Could not get server database revision number");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_DB_INFO:
		rb_debug ("Getting DAAP database info");
		path = g_strdup_printf ("/databases?session-id=%u&revision-number=%d", 
					priv->session_id, priv->revision_number);
		if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_database_info, FALSE)) {
			rb_debug ("Could not get DAAP database info");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_SONGS:
		rb_debug ("Getting DAAP song listing");
		path = g_strdup_printf ("/databases/%i/items?session-id=%u&revision-number=%i"
				        "&meta=dmap.itemid,dmap.itemname,daap.songalbum,"
					"daap.songartist,daap.daap.songgenre,daap.songsize,"
					"daap.songtime,daap.songtrackcount,daap.songtracknumber,"
					"daap.songyear,daap.songformat,daap.songgenre,"
					"daap.songbitrate", 
					priv->database_id, 
					priv->session_id, 
					priv->revision_number);
		if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_song_listing, TRUE)) {
			rb_debug ("Could not get DAAP song listing");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_PLAYLISTS:
		rb_debug ("Getting DAAP playlists");
		path = g_strdup_printf ("/databases/%d/containers?session-id=%u&revision-number=%d", 
					priv->database_id, 
					priv->session_id, 
					priv->revision_number);
		if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
			       (RBDAAPResponseHandler) handle_playlists, TRUE)) {
			rb_debug ("Could not get DAAP playlists");
			rb_daap_connection_state_done (connection, FALSE);
		}
		g_free (path);
		break;

	case DAAP_GET_PLAYLIST_ENTRIES:
		{
			RBDAAPPlaylist *playlist = 
				(RBDAAPPlaylist *) g_slist_nth_data (priv->playlists, 
								     priv->reading_playlist);
			g_assert (playlist);
			rb_debug ("Reading DAAP playlist %d entries", priv->reading_playlist);
			path = g_strdup_printf ("/databases/%d/containers/%d/items?session-id=%u&revision-number=%d&meta=dmap.itemid", 
						priv->database_id, 
						playlist->id,
						priv->session_id, priv->revision_number);
			if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE, 
				       (RBDAAPResponseHandler) handle_playlist_entries, TRUE)) {
				rb_debug ("Could not get entries for DAAP playlist %d", 
					  priv->reading_playlist);
				rb_daap_connection_state_done (connection, FALSE);
			}
			g_free (path);
		}
		break;

	case DAAP_LOGOUT:
		rb_debug ("Logging out of DAAP server");
		path = g_strdup_printf ("/logout?session-id=%u", priv->session_id);
		if (! http_get (connection, path, TRUE, priv->daap_version, 0, FALSE,
			       (RBDAAPResponseHandler) handle_logout, FALSE)) {
			rb_debug ("Could not log out of DAAP server");
			rb_daap_connection_state_done (connection, FALSE);
		}

		g_free (path);
		break;

	case DAAP_DONE:
		rb_debug ("DAAP done");

		rb_daap_connection_finish (connection);

		break;
	}

	return FALSE;
}

char * 
rb_daap_connection_get_headers (RBDAAPConnection *connection, 
				const gchar *uri, 
				gint64 bytes)
{
	RBDAAPConnectionPrivate *priv = connection->priv;
	GString *headers;
	char hash[33] = {0};
	char *norb_daap_uri = (char *)uri;
	char *s;
	
	priv->request_id++;
	
	if (g_strncasecmp (uri, "daap://", 7) == 0) {
		norb_daap_uri = strstr (uri, "/data");
	}

	rb_daap_hash_generate ((short)floorf (priv->daap_version), 
			       (const guchar*)norb_daap_uri, 2, 
			       (guchar*)hash, 
			       priv->request_id);

	headers = g_string_new ("Accept: */*\r\n"
				"Cache-Control: no-cache\r\n"
				"User-Agent: " RB_DAAP_USER_AGENT "\r\n"
				"Accept-Language: en-us, en;q=5.0\r\n"
				"Client-DAAP-Access-Index: 2\r\n"
				"Client-DAAP-Version: 3.0\r\n");
	g_string_append_printf (headers, 
				"Client-DAAP-Validation: %s\r\n"
				"Client-DAAP-Request-ID: %d\r\n"
				"Connection: close\r\n", 
				hash, priv->request_id);

	if (priv->password_protected) {
		char *user_pass;
		char *token;

		user_pass = g_strdup_printf ("%s:%s", priv->username, priv->password);
		token = soup_base64_encode (user_pass, strlen (user_pass));
		g_string_append_printf (headers, "Authentication: Basic %s\r\n", token);
		g_free (token);
		g_free (user_pass);
	}

	if (bytes != 0) {
		g_string_append_printf (headers,"Range: bytes=%"G_GINT64_FORMAT"-\r\n", bytes);
	}
	
	s = headers->str;
	g_string_free (headers, FALSE);

	return s;
}

GSList * 
rb_daap_connection_get_playlists (RBDAAPConnection *connection)
{
	return connection->priv->playlists;
}

static void 
rb_daap_connection_dispose (GObject *object)
{
	RBDAAPConnectionPrivate *priv = RB_DAAP_CONNECTION (object)->priv;
	GSList *l;

	rb_debug ("DAAP connection dispose");

	if (priv->emit_progress_id != 0) {
		g_source_remove (priv->emit_progress_id);
		priv->emit_progress_id = 0;
	}

	if (priv->do_something_id != 0) {
		g_source_remove (priv->do_something_id);
		priv->do_something_id = 0;
	}

	if (priv->name) {
		g_free (priv->name);
		priv->name = NULL;
	}
	
	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}
	
	if (priv->host) {
		g_free (priv->host);
		priv->host = NULL;
	}
	
	if (priv->playlists) {
		for (l = priv->playlists; l; l = l->next) {
			RBDAAPPlaylist *playlist = l->data;

			g_list_foreach (playlist->uris, (GFunc)g_free, NULL);
			g_list_free (playlist->uris);
			g_free (playlist->name);
			g_free (playlist);
			l->data = NULL;
		}
		g_slist_free (priv->playlists);
		priv->playlists = NULL;
	}

	if (priv->item_id_to_uri) {
		g_hash_table_destroy (priv->item_id_to_uri);
		priv->item_id_to_uri = NULL;
	}
	
	if (priv->session) {
		rb_debug ("Aborting all pending requests");
		soup_session_abort (priv->session);
		g_object_unref (G_OBJECT (priv->session));
		priv->session = NULL;
	}

	if (priv->base_uri) {
		soup_uri_free (priv->base_uri);
		priv->base_uri = NULL;
	}

	if (priv->daap_base_uri) {
		g_free (priv->daap_base_uri);
		priv->daap_base_uri = NULL;
	}

	if (priv->db) {
		g_object_unref (G_OBJECT (priv->db));
		priv->db = NULL;
	}

	if (priv->last_error_message != NULL) {
		g_free (priv->last_error_message);
		priv->last_error_message = NULL;
	}
	
	G_OBJECT_CLASS (rb_daap_connection_parent_class)->dispose (object);
}

static void
rb_daap_connection_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBDAAPConnectionPrivate *priv = RB_DAAP_CONNECTION (object)->priv;

	switch (prop_id) {
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	case PROP_DB:
		priv->db = RHYTHMDB (g_value_dup_object (value));
		break;
	case PROP_PASSWORD_PROTECTED:
		priv->password_protected = g_value_get_boolean (value);
		break;
	case PROP_ENTRY_TYPE:
		priv->db_type = g_value_get_boxed (value);
		break;
	case PROP_HOST:
		g_free (priv->host);
		priv->host = g_value_dup_string (value);
		break;
	case PROP_PORT:
		priv->port = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_daap_connection_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBDAAPConnectionPrivate *priv = RB_DAAP_CONNECTION (object)->priv;

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, priv->db);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_boxed (value, priv->db_type);
		break;
	case PROP_PASSWORD_PROTECTED:
		g_value_set_boolean (value, priv->password_protected);
		break;
	case PROP_HOST:
		g_value_set_string (value, priv->host);
		break;
	case PROP_PORT:
		g_value_set_uint (value, priv->port);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

