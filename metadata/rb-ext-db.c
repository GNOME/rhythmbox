/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011  Jonathan Matthew  <jonathan@d14n.org>
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

#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include <metadata/rb-ext-db.h>
#include <lib/rb-file-helpers.h>
#include <lib/rb-debug.h>
#include <lib/rb-util.h>

/**
 * SECTION:rbextdb
 * @short_description: store for external metadata such as album art
 *
 * This class simplifies searching for and providing external metadata
 * such as album art or lyrics.  A metadata provider connects to a signal
 * on the database and in response provides a URI, a buffer containing the
 * data, or an object representation of the data (such as a GdkPixbuf).
 * A metadata requestor calls rb_ext_db_request and specifies a callback,
 * or alternatively connects to a signal to receive all metadata as it is
 * stored.
 */

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	ADDED,
	REQUEST,
	STORE,
	LOAD,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GList *instances = NULL;

static void rb_ext_db_class_init (RBExtDBClass *klass);
static void rb_ext_db_init (RBExtDB *store);

static void maybe_start_store_request (RBExtDB *store);

struct _RBExtDBPrivate
{
	char *name;

	struct tdb_context *tdb_context;

	GList *requests;
	GList *load_requests;
	GAsyncQueue *store_queue;
	GSimpleAsyncResult *store_op;
};

typedef struct {
	RBExtDBKey *key;
	RBExtDBRequestCallback callback;
	gpointer user_data;
	GDestroyNotify destroy_notify;

	RBExtDBKey *store_key;
	char *filename;
	GValue *data;
} RBExtDBRequest;

typedef struct {
	RBExtDBKey *key;
	RBExtDBSourceType source_type;
	char *uri;
	GValue *data;
	GValue *value;

	char *filename;
	gboolean stored;
} RBExtDBStoreRequest;

G_DEFINE_TYPE (RBExtDB, rb_ext_db, G_TYPE_OBJECT)

static void
free_request (RBExtDBRequest *request)
{
	rb_ext_db_key_free (request->key);
	if (request->store_key)
		rb_ext_db_key_free (request->store_key);

	g_free (request->filename);

	if (request->data) {
		g_value_unset (request->data);
		g_free (request->data);
	}

	if (request->destroy_notify)
		request->destroy_notify (request->user_data);

	g_slice_free (RBExtDBRequest, request);
}

static void
answer_request (RBExtDBRequest *request,
		RBExtDBKey *store_key,
		const char *filename,
		GValue *data)
{
	request->callback (request->key, store_key, filename, data, request->user_data);
	free_request (request);
}

static RBExtDBRequest *
create_request (RBExtDBKey *key,
		RBExtDBRequestCallback callback,
		gpointer user_data,
		GDestroyNotify destroy_notify)
{
	RBExtDBRequest *req = g_slice_new0 (RBExtDBRequest);
	req->key = rb_ext_db_key_copy (key);
	req->callback = callback;
	req->user_data = user_data;
	req->destroy_notify = destroy_notify;
	return req;
}


static RBExtDBStoreRequest *
create_store_request (RBExtDBKey *key,
		      RBExtDBSourceType source_type,
		      const char *uri,
		      GValue *data,
		      GValue *value)
{
	RBExtDBStoreRequest *sreq = g_slice_new0 (RBExtDBStoreRequest);
	g_assert (rb_ext_db_key_is_lookup (key) == FALSE);
	sreq->key = rb_ext_db_key_copy (key);
	sreq->source_type = source_type;
	if (uri != NULL) {
		sreq->uri = g_strdup (uri);
	}
	if (data != NULL) {
		sreq->data = g_new0 (GValue, 1);
		g_value_init (sreq->data, G_VALUE_TYPE (data));
		g_value_copy (data, sreq->data);
	}
	if (value != NULL) {
		sreq->value = g_new0 (GValue, 1);
		g_value_init (sreq->value, G_VALUE_TYPE (value));
		g_value_copy (value, sreq->value);
	}
	return sreq;
}

static void
free_store_request (RBExtDBStoreRequest *sreq)
{
	if (sreq->data != NULL) {
		g_value_unset (sreq->data);
		g_free (sreq->data);
	}
	if (sreq->value != NULL) {
		g_value_unset (sreq->value);
		g_free (sreq->value);
	}
	g_free (sreq->uri);
	g_free (sreq->filename);
	rb_ext_db_key_free (sreq->key);
	g_slice_free (RBExtDBStoreRequest, sreq);
}


static TDB_DATA
flatten_data (guint64 search_time, const char *filename, RBExtDBSourceType source_type)
{
	GVariantBuilder vb;
	GVariant *v;
	TDB_DATA data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	GVariant *sv;
#endif

	g_variant_builder_init (&vb, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&vb, "{sv}", "time", g_variant_new_uint64 (search_time));
	if (filename != NULL) {
		g_variant_builder_add (&vb, "{sv}", "file", g_variant_new_string (filename));
	}
	if (source_type != RB_EXT_DB_SOURCE_NONE) {
		g_variant_builder_add (&vb, "{sv}", "srctype", g_variant_new_uint32 (source_type));
	}
	v = g_variant_builder_end (&vb);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	sv = g_variant_byteswap (v);
	g_variant_unref (v);
	v = sv;
#endif
	data.dsize = g_variant_get_size (v);
	data.dptr = g_malloc0 (data.dsize);
	g_variant_store (v, data.dptr);
	g_variant_unref (v);
	return data;
}

static void
extract_data (TDB_DATA data, guint64 *search_time, char **filename, RBExtDBSourceType *source_type)
{
	GVariant *v;
	GVariant *sv;
	GVariantIter iter;
	GVariant *value;
	char *key;

	if (data.dptr == NULL || data.dsize == 0) {
		return;
	}

	v = g_variant_new_from_data (G_VARIANT_TYPE ("a{sv}"), data.dptr, data.dsize, FALSE, NULL, NULL);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	sv = g_variant_byteswap (v);
#else
	sv = g_variant_get_normal_form (v);
#endif
	g_variant_unref (v);

	g_variant_iter_init (&iter, sv);
	while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "time") == 0) {
			if (search_time != NULL && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64)) {
				*search_time = g_variant_get_uint64 (value);
			}
		} else if (g_strcmp0 (key, "file") == 0) {
			if (filename != NULL && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)) {
				*filename = g_variant_dup_string (value, NULL);
			}
		} else if (g_strcmp0 (key, "srctype") == 0) {
			if (source_type != NULL && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32)) {
				*source_type = g_variant_get_uint32 (value);
			}
		} else {
			rb_debug ("unknown key %s in metametadata", key);
		}
	}

	g_variant_unref (sv);
}


static GValue *
default_load (RBExtDB *store, GValue *data)
{
	GValue *v = g_new0 (GValue, 1);
	g_value_init (v, G_VALUE_TYPE (data));
	g_value_copy (data, v);
	return v;
}

static GValue *
default_store (RBExtDB *store, GValue *data)
{
	GValue *v = g_new0 (GValue, 1);
	g_value_init (v, G_VALUE_TYPE (data));
	g_value_copy (data, v);
	return v;
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBExtDB *store = RB_EXT_DB (object);
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, store->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBExtDB *store = RB_EXT_DB (object);
	switch (prop_id) {
	case PROP_NAME:
		store->priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
impl_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
	GList *l;
	int i;
	const char *name;
	char *storedir;
	char *tdbfile;
	RBExtDB *store;

	/* check for an existing instance of this metadata store */
	name = NULL;
	for (i = 0; i < n_construct_properties; i++) {
		if (g_strcmp0 (g_param_spec_get_name (construct_properties[i].pspec), "name") == 0) {
			name = g_value_get_string (construct_properties[i].value);
		}
	}
	g_assert (name != NULL);

	for (l = instances; l != NULL; l = l->next) {
		RBExtDB *inst = l->data;
		if (g_strcmp0 (name, inst->priv->name) == 0) {
			rb_debug ("found existing metadata store %s", name);
			return g_object_ref (G_OBJECT (inst));
		}
	}

	rb_debug ("creating new metadata store instance %s", name);
	/* construct the new instance */
	store = RB_EXT_DB (G_OBJECT_CLASS (rb_ext_db_parent_class)->constructor (type, n_construct_properties, construct_properties));

	/* open the cache db */
	storedir = g_build_filename (rb_user_cache_dir (), name, NULL);
	if (g_mkdir_with_parents (storedir, 0700) != 0) {
		/* what can we do now? */
		g_assert_not_reached ();
	} else {
		tdbfile = g_build_filename (storedir, "store.tdb", NULL);
		store->priv->tdb_context = tdb_open (tdbfile, 999, TDB_INCOMPATIBLE_HASH | TDB_SEQNUM, O_RDWR | O_CREAT, 0600);
		if (store->priv->tdb_context == NULL) {
			/* umm */
			g_assert_not_reached ();
		}
		g_free (tdbfile);
	}
	g_free (storedir);

	/* add to instance list */
	instances = g_list_append (instances, store);

	return G_OBJECT (store);
}

static void
impl_finalize (GObject *object)
{
	RBExtDB *store = RB_EXT_DB (object);
	RBExtDBStoreRequest *req;

	g_free (store->priv->name);

	g_list_free_full (store->priv->requests, (GDestroyNotify) free_request);
	while ((req = g_async_queue_try_pop (store->priv->store_queue)) != NULL) {
		free_store_request (req);
	}
	g_async_queue_unref (store->priv->store_queue);

	if (store->priv->tdb_context) {
		tdb_close (store->priv->tdb_context);
	}

	instances = g_list_remove (instances, store);

	G_OBJECT_CLASS (rb_ext_db_parent_class)->finalize (object);
}

static void
rb_ext_db_init (RBExtDB *store)
{
	store->priv = G_TYPE_INSTANCE_GET_PRIVATE (store, RB_TYPE_EXT_DB, RBExtDBPrivate);

	store->priv->store_queue = g_async_queue_new ();
}

static void
rb_ext_db_class_init (RBExtDBClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->constructor = impl_constructor;
	object_class->finalize = impl_finalize;

	klass->load = default_load;
	klass->store = default_store;

	/**
	 * RBExtDB:name:
	 *
	 * Name of the metadata store.  Used to locate instances.
	 */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBExtDB::added:
	 * @store: the #RBExtDB
	 * @key: the #RBExtDBKey that was added
	 * @filename: the filename for the item that was added
	 * @data: the value that was stored
	 *
	 * Emitted when metadata is added to the store.  Metadata consumers
	 * can use this to process metadata they did not specifically
	 * request, for example to update album art stored on an attached
	 * media player.
	 */
	signals[ADDED] =
		g_signal_new ("added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBExtDBClass, added),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      3, RB_TYPE_EXT_DB_KEY, G_TYPE_STRING, G_TYPE_VALUE);
	/**
	 * RBExtDB::request:
	 * @store: the #RBExtDB
	 * @key: the #RBExtDBKey that was requested
	 * @last_time: the last time this item was requested
	 *
	 * Emitted when a metadata request cannot be satisfied from the local
	 * store.  Metadata providers initiate searches in response to this
	 * signal.
	 */
	signals[REQUEST] =
		g_signal_new ("request",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBExtDBClass, request),
			      rb_signal_accumulator_boolean_or, NULL, NULL,
			      G_TYPE_BOOLEAN,
			      2, RB_TYPE_EXT_DB_KEY, G_TYPE_ULONG);
	/**
	 * RBExtDB::store:
	 * @store: the #RBExtDB instance
	 * @data: the data being stored
	 *
	 * Emitted when a metadata item needs to be written to a local file.
	 * This only needs to be used for metadata that needs to be encoded
	 * or compressed, such as images.
	 *
	 * Return value: (transfer full): the value to write to a file
	 */
	signals[STORE] =
		g_signal_new ("store",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBExtDBClass, store),
			      g_signal_accumulator_first_wins, NULL,
			      NULL,
			      G_TYPE_POINTER,
			      1, G_TYPE_VALUE);
	/**
	 * RBExtDB::load:
	 * @store: the #RBExtDB instance
	 * @data: the data being loaded
	 *
	 * Emitted when loading a metadata item from a local file or from a
	 * URI.
	 *
	 * Return value: (transfer full): converted value
	 */
	signals[LOAD] =
		g_signal_new ("load",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBExtDBClass, load),
			      g_signal_accumulator_first_wins, NULL,
			      NULL,
			      G_TYPE_POINTER,
			      1, G_TYPE_VALUE);

	g_type_class_add_private (klass, sizeof (RBExtDBPrivate));
}


/**
 * rb_ext_db_new:
 * @name: name of the metadata store
 *
 * Provides access to a metadata store instance.
 *
 * Return value: named metadata store instance
 */
RBExtDB *
rb_ext_db_new (const char *name)
{
	return RB_EXT_DB (g_object_new (RB_TYPE_EXT_DB, "name", name, NULL));
}

typedef struct {
	RBExtDB *store;
	char **filename;
	RBExtDBKey **store_key;
	guint64 search_time;
	RBExtDBSourceType source_type;
} RBExtDBLookup;

static gboolean
lookup_cb (TDB_DATA data, RBExtDBKey *key, gpointer user_data)
{
	TDB_DATA tdbvalue;
	RBExtDBLookup *lookup = user_data;
	char *fn = NULL;
	RBExtDBSourceType source_type = RB_EXT_DB_SOURCE_NONE;
	guint64 search_time = 0;

	tdbvalue = tdb_fetch (lookup->store->priv->tdb_context, data);
	if (tdbvalue.dptr == NULL) {
		if (rb_debug_here ()) {
			char *str = rb_ext_db_key_to_string (key);
			rb_debug ("lookup for key %s failed", str);
			g_free (str);
		}
		return TRUE;
	}

	extract_data (tdbvalue, &search_time, &fn, &source_type);

	switch (source_type) {
	case RB_EXT_DB_SOURCE_NONE:
		if (lookup->search_time == 0)
			lookup->search_time = search_time;
		break;
	default:
		if (source_type > lookup->source_type) {
			g_free (*lookup->filename);
			*lookup->filename = fn;
			if (lookup->store_key) {
				if (*lookup->store_key)
					rb_ext_db_key_free (*lookup->store_key);
				*lookup->store_key = rb_ext_db_key_copy (key);
			}
			lookup->source_type = source_type;
			lookup->search_time = search_time;
			rb_debug ("found new best match %s, %d", fn ? fn : "none", source_type);
		} else {
			g_free (fn);
			rb_debug ("don't care about match %d", source_type);
		}
		break;
	}
	free (tdbvalue.dptr);
	return TRUE;
}

/**
 * rb_ext_db_lookup:
 * @store: metadata store instance
 * @key: metadata lookup key
 * @store_key: (out) (transfer full) (allow-none): optionally returns the matching storage key
 *
 * Looks up a cached metadata item.
 *
 * Return value: name of the file storing the cached metadata item
 */
char *
rb_ext_db_lookup (RBExtDB *store, RBExtDBKey *key, RBExtDBKey **store_key)
{
	char *fn = NULL;
	RBExtDBLookup lookup;
	char *path;

	lookup.store = store;
	lookup.filename = &fn;
	lookup.store_key = store_key;
	lookup.source_type = RB_EXT_DB_SOURCE_NONE;
	lookup.search_time = 0;

	rb_ext_db_key_lookups (key, lookup_cb, &lookup);

	if (fn == NULL) {
		return NULL;
	}

	path = g_build_filename (rb_user_cache_dir (), store->priv->name, fn, NULL);
	g_free (fn);
	return path;
}

static void
load_request_cb (RBExtDB *store, GAsyncResult *result, gpointer data)
{
	RBExtDBRequest *req;
	req = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	rb_debug ("finished loading %s", req->filename);
	if (req->callback)
		req->callback (req->key, req->store_key, req->filename, req->data, req->user_data);

	store->priv->load_requests = g_list_remove (store->priv->load_requests, req);
	g_object_unref (result);
}

static void
do_load_request (GSimpleAsyncResult *result, GObject *object, GCancellable *cancel)
{
	RBExtDBRequest *req;
	GFile *f;
	char *file_data;
	gsize file_data_size;
	GError *error = NULL;

	req = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	rb_debug ("loading data from %s", req->filename);
	f = g_file_new_for_path (req->filename);
	g_file_load_contents (f, NULL, &file_data, &file_data_size, NULL, &error);
	if (error != NULL) {
		rb_debug ("unable to load item %s: %s", req->filename, error->message);
		g_clear_error (&error);

		/* probably need to delete the item from the db */
	} else {
		GBytes *b;
		GValue d = G_VALUE_INIT;

		/* convert the encoded data into a useful object */
		rb_debug ("converting %" G_GSIZE_FORMAT " bytes of file data", file_data_size);

		b = g_bytes_new_take (file_data, file_data_size);
		g_value_init (&d, G_TYPE_BYTES);
		g_value_take_boxed (&d, b);

		req->data = NULL;
		g_signal_emit (object, signals[LOAD], 0, &d, &req->data);
		g_value_unset (&d);

		if (req->data) {
			rb_debug ("converted data into value of type %s",
				  G_VALUE_TYPE_NAME (req->data));
		} else {
			rb_debug ("data conversion failed");
		}
	}

	g_object_unref (f);
}


/**
 * rb_ext_db_request:
 * @store: metadata store instance
 * @key: metadata lookup key
 * @callback: callback to call with results
 * @user_data: user data to pass to the callback
 * @destroy: destroy function for @user_data
 *
 * Requests a metadata item.  If the item is cached, the callback will be called
 * synchronously.  Otherwise, metadata providers will provide results asynchronously.
 *
 * Return value: %TRUE if results may be provided after returning
 */
gboolean
rb_ext_db_request (RBExtDB *store,
		   RBExtDBKey *key,
		   RBExtDBRequestCallback callback,
		   gpointer user_data,
		   GDestroyNotify destroy)
{
	RBExtDBRequest *req;
	gboolean result;
	guint64 last_time;
	TDB_DATA tdbvalue;
	TDB_DATA tdbkey;
	char *filename;
	GList *l;
	gboolean emit_request = TRUE;
	gboolean add_request = TRUE;
	RBExtDBKey *store_key = NULL;

	rb_debug ("starting metadata request");

	filename = rb_ext_db_lookup (store, key, &store_key);
	if (store_key != NULL) {
		GSimpleAsyncResult *load_op;

		if (filename == NULL) {
			if (rb_debug_here ()) {
				char *str = rb_ext_db_key_to_string (store_key);
				rb_debug ("found empty match under key %s", str);
				g_free (str);
			}
			callback (key, store_key, NULL, NULL, user_data);
			if (destroy)
				destroy (user_data);
			rb_ext_db_key_free (store_key);
		} else {
			if (rb_debug_here ()) {
				char *str = rb_ext_db_key_to_string (store_key);
				rb_debug ("found cached match %s under key %s", filename, str);
				g_free (str);
			}
			load_op = g_simple_async_result_new (G_OBJECT (store),
							     (GAsyncReadyCallback) load_request_cb,
							     NULL,
							     rb_ext_db_request);

			req = create_request (key, callback, user_data, destroy);
			req->filename = filename;
			req->store_key = store_key;
			g_simple_async_result_set_op_res_gpointer (load_op, req, (GDestroyNotify) free_request);
			store->priv->load_requests = g_list_append (store->priv->load_requests, req);

			g_simple_async_result_run_in_thread (load_op,
							     do_load_request,
							     G_PRIORITY_DEFAULT,
							     NULL);	/* no cancel? */
		}

		if (rb_ext_db_key_is_null_match (key, store_key) == FALSE)
			return FALSE;

		rb_debug ("found null match, continuing to issue requests");
		add_request = FALSE;
	}

	/* discard duplicate requests, combine equivalent requests */
	for (l = store->priv->requests; l != NULL; l = l->next) {
		req = l->data;
		if (rb_ext_db_key_matches (key, req->key) == FALSE)
			continue;

		if (req->callback == callback &&
		    req->user_data == user_data &&
		    req->destroy_notify == destroy) {
			rb_debug ("found matching existing request");
			if (destroy)
				destroy (user_data);
			return TRUE;
		} else {
			rb_debug ("found existing equivalent request");
			emit_request = FALSE;
		}
	}

	/* lookup previous request time */
	tdbkey = rb_ext_db_key_to_store_key (key);

	tdbvalue = tdb_fetch (store->priv->tdb_context, tdbkey);
	if (tdbvalue.dptr != NULL) {
		extract_data (tdbvalue, &last_time, NULL, NULL);
		free (tdbvalue.dptr);
	} else {
		last_time = 0;
	}
	g_free (tdbkey.dptr);

	if (add_request) {
		/* add stuff to list of outstanding requests */
		req = create_request (key, callback, user_data, destroy);
		store->priv->requests = g_list_append (store->priv->requests, req);
	}

	/* and let metadata providers request it */
	if (emit_request) {
		result = FALSE;
		g_signal_emit (store, signals[REQUEST], 0, req->key, (gulong)last_time, &result);
	} else {
		result = TRUE;
	}

	return result;
}

/**
 * rb_ext_db_cancel_requests:
 * @store: metadata store instance
 * @callback: callback function
 * @user_data: user data
 *
 * Cancels all outstanding requests matching the specified callback and user data.
 * In cases where the user data for a request is an object, this should be called
 * early in object's dispose function to ensure that the callback is not called
 * on a partly or fully disposed object.
 */
void
rb_ext_db_cancel_requests (RBExtDB *store, RBExtDBRequestCallback callback, gpointer user_data)
{
	GList *l;

	l = store->priv->requests;
	while (l != NULL) {
		RBExtDBRequest *req = l->data;
		if (req->callback == callback && req->user_data == user_data) {
			GList *n = l->next;
			char *str = rb_ext_db_key_to_string (req->key);
			rb_debug ("cancelling a search request: %s", str);
			g_free (str);
			free_request (req);
			store->priv->requests = g_list_delete_link (store->priv->requests, l);
			l = n;
		} else {
			l = l->next;
		}
	}

	l = store->priv->load_requests;
	while (l != NULL) {
		RBExtDBRequest *req = l->data;
		if (req->callback == callback && req->user_data == user_data) {
			char *str = rb_ext_db_key_to_string (req->key);
			rb_debug ("cancelling a load request: %s", str);
			g_free (str);
			if (req->destroy_notify)
				req->destroy_notify (req->user_data);
			req->callback = NULL;
			req->user_data = NULL;
			req->destroy_notify = NULL;
		} else {
			l = l->next;
		}
	}
}

static void
delete_file (RBExtDB *store, const char *filename)
{
	char *fullname;
	GFile *f;
	GError *error = NULL;

	fullname = g_build_filename (rb_user_cache_dir (), store->priv->name, filename, NULL);
	f = g_file_new_for_path (fullname);
	g_free (fullname);

	g_file_delete (f, NULL, &error);
	if (error) {
		rb_debug ("error deleting %s from %s: %s", filename, store->priv->name, error->message);
		g_clear_error (&error);
	} else {
		rb_debug ("deleted %s from %s", filename, store->priv->name);
	}

}


static void
store_request_cb (RBExtDB *store, GAsyncResult *result, gpointer data)
{
	RBExtDBStoreRequest *sreq;
	sreq = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	if (sreq == NULL) {
		/* do nothing */
	} else if (sreq->stored) {
		GList *l;

		/* answer any matching queries */
		l = store->priv->requests;
		while (l != NULL) {
			RBExtDBRequest *req = l->data;
			if (rb_ext_db_key_matches (sreq->key, req->key)) {
				GList *n = l->next;
				rb_debug ("answering metadata request %p", req);
				answer_request (req, sreq->key, sreq->filename, sreq->value);
				store->priv->requests = g_list_delete_link (store->priv->requests, l);
				l = n;
			} else {
				l = l->next;
			}
		}

		/* let passive metadata consumers see it too */
		rb_debug ("added; filename = %s, value type = %s", sreq->filename, sreq->value ? G_VALUE_TYPE_NAME (sreq->value) : "<none>");
		g_signal_emit (store, signals[ADDED], 0, sreq->key, sreq->filename, sreq->value);
	} else {
		rb_debug ("no metadata was stored");
	}

	g_object_unref (store->priv->store_op);
	store->priv->store_op = NULL;

	/* start another store request if we have one */
	maybe_start_store_request (store);
}

static void
do_store_request (GSimpleAsyncResult *result, GObject *object, GCancellable *cancel)
{
	RBExtDB *store = RB_EXT_DB (object);
	RBExtDBStoreRequest *req;
	RBExtDBSourceType last_source_type = RB_EXT_DB_SOURCE_NONE;
	guint64 last_time = 0;
	const char *file_data;
	char *filename = NULL;
	gssize file_data_size;
	GTimeVal now;
	TDB_DATA tdbkey;
	TDB_DATA tdbdata;
	gboolean ignore;

	req = g_async_queue_try_pop (store->priv->store_queue);
	if (req == NULL) {
		rb_debug ("nothing to do");
		g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);
		return;
	}
	g_simple_async_result_set_op_res_gpointer (result, req, (GDestroyNotify)free_store_request);

	/* convert key to storage blob */
	if (rb_debug_here()) {
		char *str = rb_ext_db_key_to_string (req->key);
		rb_debug ("storing %s; source = %d", str, req->source_type);
		g_free (str);
	}
	tdbkey = rb_ext_db_key_to_store_key (req->key);

	/* fetch current contents, if any */
	tdbdata = tdb_fetch (store->priv->tdb_context, tdbkey);
	extract_data (tdbdata, &last_time, &filename, &last_source_type);

	if (req->source_type == last_source_type) {
		/* ignore new data if it just comes from a search,
		 * but otherwise update.
		 */
		ignore = (last_source_type == RB_EXT_DB_SOURCE_SEARCH);
	} else {
		/* ignore if from a lower priority source */
		ignore = (req->source_type < last_source_type);
	}

	if (ignore) {
		/* don't replace it */
		rb_debug ("existing result is from a higher or equal priority source");
		g_free (filename);
		g_free (tdbkey.dptr);
		if (tdbdata.dptr != NULL)
			free (tdbdata.dptr);
		return;
	}

	/* if the metadata item is specified by a uri, retrieve the data */
	if (req->uri != NULL) {
		GFile *f;
		GError *error = NULL;
		char *data;
		gsize data_size;

		rb_debug ("fetching uri %s", req->uri);
		f = g_file_new_for_uri (req->uri);
		g_file_load_contents (f, NULL, &data, &data_size, NULL, &error);
		if (error != NULL) {
			/* complain a bit */
			rb_debug ("unable to read %s: %s", req->uri, error->message);
			g_clear_error (&error);
			/* leave req->data alone so we fall into the failure branch? */
		} else {
			GBytes *b;
			rb_debug ("got %" G_GSIZE_FORMAT " bytes from uri %s", data_size, req->uri);
			b = g_bytes_new_take (data, data_size);
			req->data = g_new0 (GValue, 1);
			g_value_init (req->data, G_TYPE_BYTES);
			g_value_take_boxed (req->data, b);
		}

		g_object_unref (f);
	}

	if (req->data != NULL && req->value != NULL) {
		/* how did this happen? */
	} else if (req->data != NULL) {
		/* we got encoded data from somewhere; load it so we can
		 * pass it out to metadata consumers
		 */
		g_signal_emit (store, signals[LOAD], 0, req->data, &req->value);
		if (req->value != NULL) {
			rb_debug ("converted encoded data into value of type %s", G_VALUE_TYPE_NAME (req->value));
		} else {
			rb_debug ("failed to convert encoded data");
		}
	} else if (req->value != NULL) {
		/* we got an object representing the data; store it so we
		 * can write it to a file
		 */
		g_signal_emit (store, signals[STORE], 0, req->value, &req->data);

		if (req->data != NULL) {
			rb_debug ("stored value into encoded data of type %s", G_VALUE_TYPE_NAME (req->data));
		} else {
			rb_debug ("failed to store value");
		}
	} else {
		/* indicates we actually didn't get anything, as opposed to communication errors etc.
		 * providers just shouldn't call rb_ext_db_store_* in that case.
		 */
		if (req->source_type != RB_EXT_DB_SOURCE_USER_EXPLICIT)
			req->source_type = RB_EXT_DB_SOURCE_NONE;
	}

	/* get data to write to file */
	file_data = NULL;
	file_data_size = 0;
	if (req->data == NULL) {
		/* do nothing */
	} else if (G_VALUE_HOLDS_STRING (req->data)) {
		file_data = g_value_get_string (req->data);
		file_data_size = strlen (file_data);
	} else if (G_VALUE_HOLDS (req->data, G_TYPE_BYTE_ARRAY)) {
		GByteArray *bytes = g_value_get_boxed (req->data);
		file_data = (const char *)bytes->data;
		file_data_size = bytes->len;
	} else if (G_VALUE_HOLDS (req->data, G_TYPE_GSTRING)) {
		GString *str = g_value_get_boxed (req->data);
		file_data = (const char *)str->str;
		file_data_size = str->len;
	} else if (G_VALUE_HOLDS (req->data, G_TYPE_BYTES)) {
		GBytes *bytes = g_value_get_boxed (req->data);
		gsize nbytes;

		file_data = g_bytes_get_data (bytes, &nbytes);
		file_data_size = nbytes;
	} else {
		/* warning? */
		rb_debug ("don't know how to save data of type %s", G_VALUE_TYPE_NAME (req->data));
	}

	if (file_data != NULL && file_data_size > 0) {
		GFile *f;
		GError *error = NULL;
		char *subdir = NULL;
		char *basename = NULL;
		char *fullsubdir;

		if (filename == NULL) {
			int seq;

			seq = tdb_get_seqnum (store->priv->tdb_context);
			if (seq > 0xffffff) {
				subdir = g_strdup_printf ("d%3.3x%sd%3.3x", seq >> 24, G_DIR_SEPARATOR_S, (seq >> 12) & 0xfff);
			} else if (seq > 0xfff) {
				subdir = g_strdup_printf ("d%3.3x", seq >> 12);
			} else {
				subdir = g_strdup (".");
			}
			basename = g_strdup_printf ("%3.3x", seq & 0xfff);
			rb_debug ("generated filename %s, subdir %s", basename, subdir ? subdir : "none");
			filename = g_build_filename (subdir, basename, NULL);
		} else {
			basename = g_path_get_basename (filename);
			subdir = g_path_get_dirname (filename);
			rb_debug ("using existing filename %s (basename %s, subdir %s)", filename, basename, subdir);
		}

		fullsubdir = g_build_filename (rb_user_cache_dir (), store->priv->name, subdir, NULL);
		/* ignore errors, g_file_replace_contents will fail too, and it'll explain */
		g_mkdir_with_parents (fullsubdir, 0770);
		g_free (fullsubdir);

		req->filename = g_build_filename (rb_user_cache_dir (),
						  store->priv->name,
						  subdir,
						  basename,
						  NULL);

		f = g_file_new_for_path (req->filename);

		g_file_replace_contents (f,
					 file_data,
					 file_data_size,
					 NULL,
					 FALSE,
					 G_FILE_CREATE_REPLACE_DESTINATION,
					 NULL,
					 NULL,
					 &error);
		if (error != NULL) {
			rb_debug ("error saving %s: %s", req->filename, error->message);
			g_clear_error (&error);
		} else {
			req->stored = TRUE;
		}

		g_free (basename);
		g_free (subdir);

		g_object_unref (f);
	} else if (req->source_type == RB_EXT_DB_SOURCE_USER_EXPLICIT) {
		if (filename != NULL) {
			delete_file (store, filename);
			g_free (filename);
			filename = NULL;
		}
		req->stored = TRUE;
	} else if (req->source_type == RB_EXT_DB_SOURCE_NONE) {
		req->stored = TRUE;
	}

	if (req->stored) {
		TDB_DATA store_data;

		g_get_current_time (&now);
		rb_debug ("actually storing; time = %lu, filename = %s, source = %d", now.tv_sec, filename, req->source_type);
		store_data = flatten_data (now.tv_sec, filename, req->source_type);
		tdb_store (store->priv->tdb_context, tdbkey, store_data, 0);
		/* XXX warn on error.. */
		g_free (store_data.dptr);
	}

	if (tdbdata.dptr) {
		free (tdbdata.dptr);
		tdbdata.dptr = NULL;
	}

	g_free (filename);

	g_free (tdbkey.dptr);
}

static void
maybe_start_store_request (RBExtDB *store)
{
	if (store->priv->store_op != NULL) {
		rb_debug ("already doing something");
		return;
	}

	if (g_async_queue_length (store->priv->store_queue) < 1) {
		rb_debug ("nothing to do");
		return;
	}

	store->priv->store_op = g_simple_async_result_new (G_OBJECT (store),
							   (GAsyncReadyCallback) store_request_cb,
							   NULL,
							   maybe_start_store_request);
	g_simple_async_result_run_in_thread (store->priv->store_op,
					     do_store_request,
					     G_PRIORITY_DEFAULT,
					     NULL);	/* no cancel? */
}

static void
store_metadata (RBExtDB *store, RBExtDBStoreRequest *req)
{
	g_async_queue_push (store->priv->store_queue, req);
	rb_debug ("now %d entries in store queue", g_async_queue_length (store->priv->store_queue));
	maybe_start_store_request (store);
}


/**
 * rb_ext_db_store_uri:
 * @store: metadata store instance
 * @key: metadata storage key
 * @source_type: metadata source type
 * @uri: (allow-none): URI of the item to store
 *
 * Stores an item identified by @uri in the metadata store so that
 * lookups matching @key will return it.
 */
void
rb_ext_db_store_uri (RBExtDB *store,
		     RBExtDBKey *key,
		     RBExtDBSourceType source_type,
		     const char *uri)
{
	rb_debug ("storing uri %s", uri);
	store_metadata (store, create_store_request (key, source_type, uri, NULL, NULL));
}

/**
 * rb_ext_db_store:
 * @store: metadata store instance
 * @key: metadata storage key
 * @source_type: metadata source type
 * @data: (allow-none): data to store
 *
 * Stores an item in the metadata store so that lookups matching @key will
 * return it.  @data should contain an object that must be transformed using
 * the RBExtDB::store signal before being stored.  For example,
 * the album art cache expects #GdkPixbuf objects here, rather than buffers
 * containing JPEG encoded files.
 */
void
rb_ext_db_store (RBExtDB *store,
		 RBExtDBKey *key,
		 RBExtDBSourceType source_type,
		 GValue *data)
{
	rb_debug ("storing value of type %s", data ? G_VALUE_TYPE_NAME (data) : "<none>");
	store_metadata (store, create_store_request (key, source_type, NULL, NULL, data));
}

/**
 * rb_ext_db_store_raw:
 * @store: metadata store instance
 * @key: metadata storage key
 * @source_type: metadata source type
 * @data: (allow-none): data to store
 *
 * Stores an item in the metadata store so that lookpus matching @key
 * will return it.  @data should contain the data to be written to the
 * store, either as a string or as a #GByteArray.
 */
void
rb_ext_db_store_raw (RBExtDB *store,
		     RBExtDBKey *key,
		     RBExtDBSourceType source_type,
		     GValue *data)
{
	rb_debug ("storing encoded data of type %s", data ? G_VALUE_TYPE_NAME (data) : "<none>");
	store_metadata (store, create_store_request (key, source_type, NULL, data, NULL));
}

/**
 * rb_ext_db_delete:
 * @store: metadata store instance
 * @key: metadata storage key
 *
 * Deletes the item stored in the metadata store under the specified storage key.
 */
void
rb_ext_db_delete (RBExtDB *store, RBExtDBKey *key)
{
	TDB_DATA k;
	TDB_DATA value;

	k = rb_ext_db_key_to_store_key (key);
	if (rb_debug_here ()) {
		char *str = rb_ext_db_key_to_string (key);
		rb_debug ("deleting key %s", str);
		g_free (str);
	}

	value = tdb_fetch (store->priv->tdb_context, k);
	if (value.dptr != NULL) {
		char *fn = NULL;

		extract_data (value, NULL, &fn, NULL);
		if (fn != NULL) {
			delete_file (store, fn);
			g_free (fn);
		}

		tdb_delete (store->priv->tdb_context, k);
		free (value.dptr);

		g_signal_emit (store, signals[ADDED], 0, key, NULL, NULL);
	}
	g_free (k.dptr);
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_ext_db_source_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY(RB_EXT_DB_SOURCE_NONE, "none"),
			ENUM_ENTRY(RB_EXT_DB_SOURCE_SEARCH, "search"),
			ENUM_ENTRY(RB_EXT_DB_SOURCE_EMBEDDED, "embedded"),
			ENUM_ENTRY(RB_EXT_DB_SOURCE_USER, "user"),
			ENUM_ENTRY(RB_EXT_DB_SOURCE_USER_EXPLICIT, "user-explicit"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("RBExtDBSourceType", values);
	}

	return etype;
}
