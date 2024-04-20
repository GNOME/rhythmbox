/*
 *  Copyright (C) 2015  Jonathan Matthew  <jonathan@d14n.org>
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
#include <errno.h>

#include <glib/gi18n.h>

#include <tdb.h>

#include "rhythmdb-metadata-cache.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-debug.h"

/* is this enough? */
#define CACHE_HASH_SIZE		4096

static RhythmDBPropType cached_properties[] = {
	/* items that get accessed before being applied to an entry go first */
	RHYTHMDB_PROP_MTIME,
	RHYTHMDB_PROP_FILE_SIZE,
	RHYTHMDB_PROP_MEDIA_TYPE,

	RHYTHMDB_PROP_TITLE,
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_TRACK_NUMBER,
	RHYTHMDB_PROP_TRACK_TOTAL,
	RHYTHMDB_PROP_DISC_NUMBER,
	RHYTHMDB_PROP_DISC_TOTAL,
	RHYTHMDB_PROP_DURATION,
	RHYTHMDB_PROP_BITRATE,
	RHYTHMDB_PROP_DATE,
	RHYTHMDB_PROP_MUSICBRAINZ_TRACKID,
	RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID,
	RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID,
	RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID,
	RHYTHMDB_PROP_ARTIST_SORTNAME,
	RHYTHMDB_PROP_ALBUM_SORTNAME,
	RHYTHMDB_PROP_ALBUM_ARTIST,
	RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME,
	RHYTHMDB_PROP_BPM,
	RHYTHMDB_PROP_COMPOSER,
	RHYTHMDB_PROP_COMPOSER_SORTNAME,
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_NAME
};

static void	rhythmdb_metadata_cache_class_init (RhythmDBMetadataCacheClass *klass);
static void	rhythmdb_metadata_cache_init (RhythmDBMetadataCache *cache);

struct _RhythmDBMetadataCachePrivate
{
	RhythmDB *db;
	char *name;

	struct tdb_context *tdb_context;

	const char *purge_prefix;
	guint64 purge_age;
};

G_DEFINE_TYPE (RhythmDBMetadataCache, rhythmdb_metadata_cache, G_TYPE_OBJECT);

static GHashTable *instances = NULL;

/**
 * SECTION:rhythmdbmetadatacache
 * @short_description: file metadata cache
 *
 * Metadata caches store the #RhythmDBEntry fields that are directly derived
 * from the media file outside the #RhythmDB file used to store the local
 * library.
 */

/**
 * rhythmdb_metadata_cache_get:
 * @db: the #RhythmDB object
 * @name: metadata cache name
 *
 * Gets a metadata cache with the given name, creating it if necessary.
 * Can only be called from the main thread.
 *
 * Return value: new #RhythmDBMetadataCache object.
 */
RhythmDBMetadataCache *
rhythmdb_metadata_cache_get (RhythmDB *db, const char *name)
{
	GObject *obj;

	g_assert (rb_is_main_thread ());

	if (instances == NULL)
		instances = g_hash_table_new (g_str_hash, g_str_equal);

	obj = g_hash_table_lookup (instances, name);
	if (obj)
		return RHYTHMDB_METADATA_CACHE (g_object_ref (obj));

	obj = g_object_new (RHYTHMDB_TYPE_METADATA_CACHE,
			    "db", db,
			    "name", name,
			    NULL);
	g_hash_table_insert (instances, g_strdup (name), obj);
	return RHYTHMDB_METADATA_CACHE (obj);
}

static void
free_tdb_data(gpointer data)
{
	free (data);
}

static void
parse_value (TDB_DATA data, guint64 *missing_since, GVariant **metadata)
{
	GVariant *v;

	v = g_variant_new_from_data (G_VARIANT_TYPE ("(ta{sv})"), data.dptr, data.dsize, FALSE, free_tdb_data, data.dptr);

	g_variant_get_child (v, 0, "t", missing_since);
	*metadata = g_variant_get_child_value (v, 1);

	g_variant_unref (v);
}

static void
store_value (struct tdb_context *tdb, const char *key, guint64 missing_since, GVariant *metadata)
{
	GVariantBuilder b;
	GVariant *v;
	TDB_DATA tdbdata;
	TDB_DATA tdbkey;

	g_variant_builder_init (&b, G_VARIANT_TYPE ("(ta{sv})"));
	g_variant_builder_add (&b, "t", missing_since);
	g_variant_builder_add_value (&b, metadata);
	v = g_variant_builder_end (&b);

	tdbdata.dsize = g_variant_get_size (v);
	tdbdata.dptr = g_malloc0 (tdbdata.dsize);
	g_variant_store (v, tdbdata.dptr);
	g_variant_unref (v);

	tdbkey.dsize = strlen(key);
	tdbkey.dptr = (unsigned char *)key;

	tdb_store (tdb, tdbkey, tdbdata, 0);

	g_free (tdbdata.dptr);
}

/**
 * rhythmdb_metadata_cache_load:
 * @cache: a #RhythmDBMetadataCache
 * @key: cache key to load
 * @metadata: returns cached metadata items, if any
 *
 * Fetches metadata from the cache.
 *
 * Return value: %TRUE if metadata was added to the array
 */
gboolean
rhythmdb_metadata_cache_load (RhythmDBMetadataCache *cache,
			      const char *key,
			      GArray *metadata)
{
	TDB_DATA tdbkey;
	TDB_DATA tdbvalue;
	GVariant *cached_metadata;
	GVariantIter iter;
	RhythmDBPropType prop;
	RhythmDBEntryChange *fields;
	GVariant *value;
	guint64 missing_since;
	GType proptype;
	guint64 u64;
	char *pkey;
	int i;

	tdbkey.dptr = (unsigned char *)key;
	tdbkey.dsize = strlen(key);
	tdbvalue = tdb_fetch (cache->priv->tdb_context, tdbkey);
	if (tdbvalue.dptr == NULL)
		return FALSE;

	parse_value (tdbvalue, &missing_since, &cached_metadata);

	/* reset missing-since, if necessary */
	if (missing_since != 0) {
		store_value (cache->priv->tdb_context, key, 0, cached_metadata);
	}

	metadata->len = g_variant_n_children (cached_metadata);
	fields = g_new0 (RhythmDBEntryChange, metadata->len);
	metadata->data = (char *)fields;

	i = 0;
	g_variant_iter_init (&iter, cached_metadata);
	while (g_variant_iter_loop (&iter, "{sv}", &pkey, &value)) {
		prop = rhythmdb_propid_from_nice_elt_name (cache->priv->db, (xmlChar *)pkey);
		if (prop == -1) {
			rb_debug ("unknown property %s found in cache", pkey);
			continue;
		}

		fields[i].prop = prop;
		proptype = rhythmdb_get_property_type (cache->priv->db, prop);
		g_value_init (&fields[i].new, proptype);

		switch (proptype) {
		case G_TYPE_STRING:
			g_value_set_string (&fields[i].new, g_variant_get_string (value, NULL));
			break;
		case G_TYPE_BOOLEAN:
			g_value_set_boolean (&fields[i].new, g_variant_get_boolean (value));
			break;
		case G_TYPE_ULONG:
			/* we always store longs as uint64, so check for overflow */
			u64 = g_variant_get_uint64 (value);
			if (u64 > G_MAXULONG) {
				rb_debug ("value %" G_GUINT64_FORMAT " overflows", u64);
				u64 = G_MAXULONG;
			}
			g_value_set_ulong (&fields[i].new, u64);
			break;
		case G_TYPE_UINT64:
			g_value_set_uint64 (&fields[i].new, g_variant_get_uint64 (value));
			break;
		case G_TYPE_DOUBLE:
			g_value_set_double (&fields[i].new, g_variant_get_double (value));
			break;
		default:
			g_assert_not_reached ();
			break;
		}
		i++;
	}

	g_variant_unref (cached_metadata);
	return TRUE;
}

/**
 * rhythmdb_metadata_cache_store:
 * @cache: a #RhythmDBMetadataCache
 * @key: cache key to store
 * @entry: entry to store
 *
 * Stores metadata in the cache.
 */
void
rhythmdb_metadata_cache_store (RhythmDBMetadataCache *cache,
			       const char *key,
			       RhythmDBEntry *entry)
{
	GVariantBuilder vb;
	int i;

	g_variant_builder_init (&vb, G_VARIANT_TYPE ("a{sv}"));
	for (i = 0; i < G_N_ELEMENTS(cached_properties); i++) {
		GType proptype;
		const char *str;
		gulong ulong;
		guint64 u64;
		GVariant *v;

		v = NULL;
		proptype = rhythmdb_get_property_type (cache->priv->db, cached_properties[i]);
		switch (proptype) {
		case G_TYPE_STRING:
			str = rhythmdb_entry_get_string (entry, cached_properties[i]);
			if (str != NULL && str[0] != '\0' && g_str_equal (str, _("Unknown")) == FALSE) {
				v = g_variant_new_string (str);
			}
			break;

		case G_TYPE_ULONG:
			ulong = rhythmdb_entry_get_ulong (entry, cached_properties[i]);
			if (ulong != 0) {
				/* use uint64 for longs, even on ilp32 */
				v = g_variant_new_uint64 (ulong);
			}
			break;

		case G_TYPE_UINT64:
			u64 = rhythmdb_entry_get_uint64 (entry, cached_properties[i]);
			if (u64 != 0) {
				v = g_variant_new_uint64 (u64);
			}
			break;

		case G_TYPE_BOOLEAN:
			v = g_variant_new_boolean (rhythmdb_entry_get_boolean (entry, cached_properties[i]));
			break;

		case G_TYPE_DOUBLE:
			v = g_variant_new_double (rhythmdb_entry_get_double (entry, cached_properties[i]));
			break;

		default:
			g_assert_not_reached ();
		}

		if (v != NULL) {
			const char *tag;

			tag = (const char *)rhythmdb_nice_elt_name_from_propid (cache->priv->db, cached_properties[i]);
			g_variant_builder_add (&vb, "{sv}", tag, v);
		}
	}

	store_value (cache->priv->tdb_context, key, 0, g_variant_builder_end (&vb));
}

typedef struct {
	struct tdb_context *tdb;

	const char *prefix;
	guint64 time;
	guint64 before;

	RhythmDBMetadataCacheValidFunc valid_func;
	gpointer valid_func_data;
} RhythmDBMetadataCachePurge;

static int
purge_traverse_cb (struct tdb_context *tdb, TDB_DATA tdbkey, TDB_DATA tdbdata, RhythmDBMetadataCachePurge *purge)
{
	guint64 missing_since;
	GVariant *metadata;
	char *key;
	TDB_DATA aligndata;

	key = g_strndup ((const char *)tdbkey.dptr, tdbkey.dsize);
	if (g_str_has_prefix (key, purge->prefix) == FALSE) {
		g_free (key);
		return 0;
	}

	aligndata.dptr = g_memdup (tdbdata.dptr, tdbdata.dsize);
	aligndata.dsize = tdbdata.dsize;

	parse_value (aligndata, &missing_since, &metadata);
	if (missing_since == 0) {
		if (purge->valid_func (key, purge->valid_func_data) == FALSE) {
			store_value (purge->tdb, key, purge->time, metadata);
		}
	} else if (missing_since < purge->before) {
		rb_debug ("entry %s is too old, deleting", key);
		tdb_delete (tdb, tdbkey);
	}
	g_variant_unref (metadata); /* also frees aligndata.dptr */
	g_free (key);

	return 0;
}

/**
 * rhythmdb_metadata_cache_purge:
 * @cache: a #RhythmDBMetadataCache
 * @prefix: prefix to scan
 * @max_age: maximum age of entries to keep (in seconds)
 *
 * Purges cache entries that have not been used for some amount of time.
 */
void
rhythmdb_metadata_cache_purge (RhythmDBMetadataCache *cache,
			       const char *prefix,
			       gulong max_age,
			       RhythmDBMetadataCacheValidFunc cb,
			       gpointer cb_data,
			       GDestroyNotify cb_data_destroy)
{
	RhythmDBMetadataCachePurge purge;

	time_t now;
	time (&now);
	purge.time = now;
	purge.before = now - max_age;
	purge.prefix = prefix;
	purge.valid_func = cb;
	purge.valid_func_data = cb_data;
	purge.tdb = cache->priv->tdb_context;
	tdb_traverse (cache->priv->tdb_context, (tdb_traverse_func)purge_traverse_cb, &purge);

	if (cb_data_destroy && cb_data)
		cb_data_destroy (cb_data);
}



static void
rhythmdb_metadata_cache_init (RhythmDBMetadataCache *cache)
{
	cache->priv = G_TYPE_INSTANCE_GET_PRIVATE (cache,
						   RHYTHMDB_TYPE_METADATA_CACHE,
						   RhythmDBMetadataCachePrivate);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RhythmDBMetadataCache *cache = RHYTHMDB_METADATA_CACHE (object);

	switch (prop_id) {
	case PROP_DB:
		cache->priv->db = RHYTHMDB (g_value_dup_object (value));
		break;
	case PROP_NAME:
		cache->priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RhythmDBMetadataCache *cache = RHYTHMDB_METADATA_CACHE (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, cache->priv->db);
		break;
	case PROP_NAME:
		g_value_set_string (value, cache->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_constructed (GObject *object)
{
	RhythmDBMetadataCache *cache;
	char *cachedir;
	char *tdbfile;
	char *tdbpath;

	RB_CHAIN_GOBJECT_METHOD (rhythmdb_metadata_cache_parent_class, constructed, object);

	cache = RHYTHMDB_METADATA_CACHE (object);
	cachedir = g_build_filename (rb_user_cache_dir (), "metadata", NULL);
	if (g_mkdir_with_parents (cachedir, 0700) != 0) {
		rb_debug ("unable to create metadata cache directory %s", cachedir);
	} else {
		tdbfile = g_strdup_printf ("%s.tdb", cache->priv->name);
		tdbpath = g_build_filename (cachedir, tdbfile, NULL);
		cache->priv->tdb_context = tdb_open (tdbpath, CACHE_HASH_SIZE, TDB_INCOMPATIBLE_HASH, O_RDWR | O_CREAT, 0600);
		if (cache->priv->tdb_context == NULL) {
			rb_debug ("unable to open metadata cache %s: %s", tdbpath, strerror(errno));
		}
		g_free (tdbfile);
		g_free (tdbpath);
	}
	g_free (cachedir);
}


static void
impl_dispose (GObject *object)
{
	RhythmDBMetadataCache *cache = RHYTHMDB_METADATA_CACHE (object);

	g_clear_object (&cache->priv->db);

	G_OBJECT_CLASS (rhythmdb_metadata_cache_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RhythmDBMetadataCache *cache = RHYTHMDB_METADATA_CACHE (object);

	g_free (cache->priv->name);

	G_OBJECT_CLASS (rhythmdb_metadata_cache_parent_class)->finalize (object);
}

static void
rhythmdb_metadata_cache_class_init (RhythmDBMetadataCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "cache file name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RhythmDBMetadataCachePrivate));
}
