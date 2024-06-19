/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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

#include <string.h>

#include <glib/gi18n.h>

#include "rhythmdb-entry-type.h"
#include "rhythmdb-metadata-cache.h"
#include "rhythmdb-private.h"

#include <metadata/rb-ext-db-key.h>

#include "rb-util.h"


enum
{
	PROP_0,
	PROP_DB,
	PROP_NAME,
	PROP_SAVE_TO_DISK,
	PROP_TYPE_DATA_SIZE,
	PROP_CATEGORY,
	PROP_CACHE_NAME
};

static void rhythmdb_entry_type_class_init (RhythmDBEntryTypeClass *klass);
static void rhythmdb_entry_type_init (RhythmDBEntryType *etype);

struct _RhythmDBEntryTypePrivate
{
	RhythmDB *db;

	char *name;
	gboolean save_to_disk;
	guint entry_type_data_size;
	RhythmDBEntryCategory category;

	char *cache_name;

	RhythmDBMetadataCache *cache;
};


G_DEFINE_TYPE (RhythmDBEntryType, rhythmdb_entry_type, G_TYPE_OBJECT)

/**
 * SECTION:rhythmdbentrytype
 * @short_description: Database entry type base class
 *
 * This is the base class for database entry type classes, which provide
 * some aspects of the behaviour of database entry types.  There are different
 * entry types for songs, radio streams, podcast feeds and episodes, and so on.
 *
 * Plugins written in Python or Vala can create new entry types by subclassing
 * and overriding any methods required.  Plugins written in C can create a new
 * instance of the RhythmDBEntryType base class and use its function pointer
 * members rather than subclassing.
 */

/**
 * rhythmdb_entry_type_get_name:
 * @etype: a #RhythmDBEntryType
 *
 * Returns the name of the entry type
 *
 * Return value: entry type name
 */
const char *
rhythmdb_entry_type_get_name (RhythmDBEntryType *etype)
{
	return etype->priv->name;
}

/**
 * rhythmdb_entry_get_playback_uri:
 * @entry: a #RhythmDBEntry
 *
 * Returns an allocated string containing the playback URI for @entry,
 * or NULL if the entry cannot be played.
 *
 * Return value: playback URI or NULL
 */
char *
rhythmdb_entry_get_playback_uri (RhythmDBEntry *entry)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);

	if (klass->get_playback_uri) {
		return (klass->get_playback_uri) (etype, entry);
	} else {
		return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
	}
}

/**
 * rhythmdb_entry_update_availability:
 * @entry: a #RhythmDBEntry
 * @avail: an availability event
 *
 * Updates @entry to reflect its new availability.
 */
void
rhythmdb_entry_update_availability (RhythmDBEntry *entry, RhythmDBEntryAvailability avail)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);

	if (klass->update_availability) {
		(klass->update_availability) (etype, entry, avail);
	} else {
		/* do nothing? */
	}
}

/**
 * rhythmdb_entry_created:
 * @entry: a newly created #RhythmDBEntry
 *
 * Calls the entry type's post-creation method for @entry.
 */
void
rhythmdb_entry_created (RhythmDBEntry *entry)
{
	RhythmDBEntryType *etype;
	RhythmDBEntryTypeClass *klass;

	etype = rhythmdb_entry_get_entry_type (entry);
	klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);

	if (klass->entry_created) {
		klass->entry_created (etype, entry);
	}
}

/**
 * rhythmdb_entry_pre_destroy:
 * @entry: a #RhythmDBEntry
 *
 * Calls the entry type's pre-deletion method for @entry.
 */
void
rhythmdb_entry_pre_destroy (RhythmDBEntry *entry)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (klass->destroy_entry) {
		klass->destroy_entry (etype, entry);
	}
}

/**
 * rhythmdb_entry_can_sync_metadata:
 * @entry: a #RhythmDBEntry
 *
 * Calls the entry type's method to check if it can sync metadata for @entry.
 * Usually this is only true for entries backed by files, where tag-writing is
 * enabled, and the appropriate tag-writing facilities are available.
 *
 * Return value: %TRUE if the entry can be synced
 */
gboolean
rhythmdb_entry_can_sync_metadata (RhythmDBEntry *entry)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (klass->can_sync_metadata) {
		return klass->can_sync_metadata (etype, entry);
	} else {
		return FALSE;
	}
}

/**
 * rhythmdb_entry_sync_metadata:
 * @entry: a #RhythmDBEntry
 * @changes: (element-type RB.RhythmDBEntryChange): a list of #RhythmDBEntryChange structures
 * @error: returns error information
 *
 * Calls the entry type's method to sync metadata changes for @entry.
 */
void
rhythmdb_entry_sync_metadata (RhythmDBEntry *entry, GSList *changes, GError **error)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (klass->sync_metadata) {
		klass->sync_metadata (etype, entry, changes, error);
	} else {
		/* default implementation? */
	}
}

/**
 * rhythmdb_entry_type_fetch_metadata:
 * @etype: a #RhythmDBEntryType
 * @uri: uri of the item to fetch
 * @metadata: (element-type RhythmDBEntryChange): returns fetched metadata
 *
 * Fetches metadata for a URI (not an entry yet, at this point) from a cache, if possible.
 *
 * The @metadata array contains RhythmDBEntryChange items with just the 'new' value set.
 *
 * Return value: %TRUE if metadata is returned
 */
gboolean
rhythmdb_entry_type_fetch_metadata (RhythmDBEntryType *etype, const char *uri, GArray *metadata)
{
	char *key;
	gboolean result;

	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (klass->uri_to_cache_key == NULL) {
		return FALSE;
	}

	key = klass->uri_to_cache_key (etype, uri);
	if (key == NULL)
		return FALSE;

	result = rhythmdb_metadata_cache_load (etype->priv->cache, key, metadata);
	g_free (key);
	return result;
}

/**
 * rhythmdb_entry_cache_metadata:
 * @entry: a #RhythmDBEntry
 *
 * Stores metadata for @entry in the metadata cache (if any) for its entry type.
 */
void
rhythmdb_entry_cache_metadata (RhythmDBEntry *entry)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	char *key;

	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (klass->uri_to_cache_key == NULL) {
		return;
	}

	key = klass->uri_to_cache_key (etype, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	if (key == NULL)
		return;

	rhythmdb_metadata_cache_store (etype->priv->cache, key, entry);
}

static RhythmDBPropType default_unknown_properties[] = {
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_COMPOSER
};

/**
 * rhythmdb_entry_apply_cached_metadata:
 * @entry: a #RhythmDBEntry
 * @metadata: (element-type RhythmDBEntryChange): cached metadata to apply
 *
 * Applies a set of metadata properties to @entry.  The metadata should be in the
 * form returned by @rhythmdb_entry_type_fetch_metadata.
 */
void
rhythmdb_entry_apply_cached_metadata (RhythmDBEntry *entry, GArray *metadata)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryChange *fields;
	GValue unknown = {0,};
	int i;

	g_value_init (&unknown, G_TYPE_STRING);
	g_value_set_string (&unknown, _("Unknown"));
	for (i = 0; i < G_N_ELEMENTS(default_unknown_properties); i++) {
		rhythmdb_entry_set_internal (etype->priv->db, entry, TRUE, default_unknown_properties[i], &unknown);
	}
	g_value_unset (&unknown);

	fields = (RhythmDBEntryChange *)metadata->data;
	for (i = 0; i < metadata->len; i++) {
		rhythmdb_entry_set_internal (etype->priv->db, entry, TRUE, fields[i].prop, &fields[i].new);
	}
	rhythmdb_commit (etype->priv->db);
}

static gboolean
metadata_key_valid_cb (const char *key, RhythmDBEntryType *etype)
{
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	char *uri;
	gboolean result = FALSE;	/* or maybe true? */

	uri = klass->cache_key_to_uri (etype, key);
	if (uri != NULL) {
		RhythmDBEntry *entry;
		entry = rhythmdb_entry_lookup_by_location (etype->priv->db, uri);
		result = (entry != NULL);
	}

	g_free (uri);
	return result;
}

/**
 * rhythmdb_entry_type_purge_metadata_cache:
 * @etype: a #RhythmDBEntryType
 * @prefix: a cache key prefix to scan
 * @max_age: maximum age of missing entries to keep
 */
void
rhythmdb_entry_type_purge_metadata_cache (RhythmDBEntryType *etype, const char *prefix, guint64 max_age)
{
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	g_assert (klass->cache_key_to_uri != NULL);
	g_assert (etype->priv->cache != NULL);

	rhythmdb_metadata_cache_purge (etype->priv->cache,
				       prefix,
				       max_age,
				       (RhythmDBMetadataCacheValidFunc) metadata_key_valid_cb,
				       etype,
				       NULL);
}

/**
 * rhythmdb_entry_create_ext_db_key:
 * @entry: a #RhythmDBEntry
 * @prop: the primary #RhythmDBPropType for metadata lookups
 *
 * Creates a #RBExtDBKey for finding external metadata
 * for a given property.  This is mostly useful for finding album or
 * track related data.
 *
 * Return value: the new #RBExtDBKey
 */
RBExtDBKey *
rhythmdb_entry_create_ext_db_key (RhythmDBEntry *entry, RhythmDBPropType prop)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	RBExtDBKey *key;
	const char *str;


	if (klass->create_ext_db_key)
		return klass->create_ext_db_key (etype, entry, prop);

	switch (prop) {
	case RHYTHMDB_PROP_ALBUM:
		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		if (g_strcmp0 (str, "") != 0 && g_strcmp0 (str, _("Unknown")) != 0) {
			key = rb_ext_db_key_create_lookup ("album", str);
			rb_ext_db_key_add_field (key,
						 "artist",
						 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
			str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
			if (g_strcmp0 (str, "") != 0 && g_strcmp0 (str, _("Unknown")) != 0) {
				rb_ext_db_key_add_field (key, "artist", str);
			}

			str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID);
			if (g_strcmp0 (str, "") != 0 && g_strcmp0 (str, _("Unknown")) != 0) {
				rb_ext_db_key_add_info (key, "musicbrainz-albumid", str);
			}
			break;
		}
		/* fall through if there's no album information */

	case RHYTHMDB_PROP_TITLE:
		key = rb_ext_db_key_create_lookup ("title", rhythmdb_entry_get_string (entry, prop));
		/* maybe these should be info? */
		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
		if (g_strcmp0 (str, "") != 0 && g_strcmp0 (str, _("Unknown")) != 0) {
			rb_ext_db_key_add_field (key, "artist", str);
		}
		str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		if (g_strcmp0 (str, "") != 0 && g_strcmp0 (str, _("Unknown")) != 0) {
			rb_ext_db_key_add_field (key, "album", str);
		}
		break;

	case RHYTHMDB_PROP_ARTIST:
		/* not really sure what this might be useful for */
		key = rb_ext_db_key_create_lookup ("artist", rhythmdb_entry_get_string (entry, prop));
		break;

	default:
		g_assert_not_reached ();
	}

	rb_ext_db_key_add_info (key, "location", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	return key;
}


static void
rhythmdb_entry_type_init (RhythmDBEntryType *etype)
{
	etype->priv = G_TYPE_INSTANCE_GET_PRIVATE (etype,
						   RHYTHMDB_TYPE_ENTRY_TYPE,
						   RhythmDBEntryTypePrivate);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RhythmDBEntryType *etype = RHYTHMDB_ENTRY_TYPE (object);

	switch (prop_id) {
	case PROP_DB:
		etype->priv->db = g_value_get_object (value);
		break;
	case PROP_NAME:
		etype->priv->name = g_value_dup_string (value);
		break;
	case PROP_SAVE_TO_DISK:
		etype->priv->save_to_disk = g_value_get_boolean (value);
		break;
	case PROP_TYPE_DATA_SIZE:
		etype->priv->entry_type_data_size = g_value_get_uint (value);
		break;
	case PROP_CATEGORY:
		etype->priv->category = g_value_get_enum (value);
		break;
	case PROP_CACHE_NAME:
		etype->priv->cache_name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RhythmDBEntryType *etype = RHYTHMDB_ENTRY_TYPE (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, etype->priv->db);
		break;
	case PROP_NAME:
		g_value_set_string (value, etype->priv->name);
		break;
	case PROP_SAVE_TO_DISK:
		g_value_set_boolean (value, etype->priv->save_to_disk);
		break;
	case PROP_TYPE_DATA_SIZE:
		g_value_set_uint (value, etype->priv->entry_type_data_size);
		break;
	case PROP_CATEGORY:
		g_value_set_enum (value, etype->priv->category);
		break;
	case PROP_CACHE_NAME:
		g_value_set_string (value, etype->priv->cache_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_constructed (GObject *object)
{
	RhythmDBEntryType *etype;
	RhythmDBEntryTypeClass *klass;

	RB_CHAIN_GOBJECT_METHOD (rhythmdb_entry_type_parent_class, constructed, object);

	etype = RHYTHMDB_ENTRY_TYPE (object);
	klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);

	if (etype->priv->cache_name) {
		g_assert (klass->uri_to_cache_key != NULL);

		etype->priv->cache = rhythmdb_metadata_cache_get (etype->priv->db, etype->priv->cache_name);
	}
}

static void
impl_dispose (GObject *object)
{
	RhythmDBEntryType *etype = RHYTHMDB_ENTRY_TYPE (object);

	g_clear_object (&etype->priv->cache);

	G_OBJECT_CLASS (rhythmdb_entry_type_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RhythmDBEntryType *etype = RHYTHMDB_ENTRY_TYPE (object);

	g_free (etype->priv->name);
	g_free (etype->priv->cache_name);

	G_OBJECT_CLASS (rhythmdb_entry_type_parent_class)->finalize (object);
}

static void
rhythmdb_entry_type_class_init (RhythmDBEntryTypeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	/**
	 * RhythmDBEntryType:db:
	 *
	 * The #RhythmDB instance.
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RhythmDBEntryType:name:
	 *
	 * Entry type name.  This must be unique.
	 */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "entry type name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RhythmDBEntryType:save-to-disk:
	 *
	 * If %TRUE, entries of this type should be written to the
	 * on-disk database.
	 */
	g_object_class_install_property (object_class,
					 PROP_SAVE_TO_DISK,
					 g_param_spec_boolean ("save-to-disk",
							       "save to disk",
							       "whether to save this type of entry to disk",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RhythmDBEntryType:type-data-size:
	 *
	 * The size of the type-specific data structure to allocate for each
	 * entry of this type.
	 */
	g_object_class_install_property (object_class,
					 PROP_TYPE_DATA_SIZE,
					 g_param_spec_uint ("type-data-size",
							    "type data size",
							    "size of entry type specific data",
							    0, G_MAXUINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RhythmDBEntryType:category:
	 *
	 * The #RhythmDBEntryCategory that this entry type fits into.
	 */
	g_object_class_install_property (object_class,
					 PROP_CATEGORY,
					 g_param_spec_enum ("category",
							    "category",
							    "RhythmDBEntryCategory for the entry type",
							    RHYTHMDB_TYPE_ENTRY_CATEGORY,
							    RHYTHMDB_ENTRY_NORMAL,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RhythmDBEntryType:cache-name:
	 *
	 * Metadata cache name.  For entry types created by a plugin, should match the plugin name.
	 * If this is set, the entry type must also implement the uri_to_cache_key method.
	 */
	g_object_class_install_property (object_class,
					 PROP_CACHE_NAME,
					 g_param_spec_string ("cache-name",
							      "cache name",
							      "metadata cache name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RhythmDBEntryTypePrivate));
}



#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

/**
 * RhythmDBEntryCategory:
 * @RHYTHMDB_ENTRY_NORMAL: Normal files on disk
 * @RHYTHMDB_ENTRY_STREAM: Endless streams (eg shoutcast)
 * @RHYTHMDB_ENTRY_CONTAINER: Containers for other entries (eg podcast feeds)
 * @RHYTHMDB_ENTRY_VIRTUAL: Things Rhythmbox shouldn't normally deal with
 *
 * Categories used to group entry types.  These are used in a few places to control
 * handling of entries.
 */

GType
rhythmdb_entry_category_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (RHYTHMDB_ENTRY_NORMAL, "normal"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_STREAM, "stream"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_CONTAINER, "container"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_VIRTUAL, "virtual"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBEntryCategory", values);
	}

	return etype;
}

/**
 * RhythmDBEntryAvailability:
 * @RHYTHMDB_ENTRY_AVAIL_CHECKED: File was checked and found present
 * @RHYTHMDB_ENTRY_AVAIL_MOUNTED: Filesystem holding the file was mounted
 * @RHYTHMDB_ENTRY_AVAIL_UNMOUNTED: Filesystem holding the file was unmounted
 * @RHYTHMDB_ENTRY_AVAIL_NOT_FOUND: File was checked or played and could not be found
 *
 * Various events that can result in changes to the entry's availability.
 */

GType
rhythmdb_entry_availability_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (RHYTHMDB_ENTRY_AVAIL_CHECKED, "checked"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_AVAIL_MOUNTED, "mounted"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_AVAIL_UNMOUNTED, "unmounted"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_AVAIL_NOT_FOUND, "not-found"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBEntryAvailability", values);
	}

	return etype;
}
