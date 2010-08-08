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

#include "rhythmdb-entry-type.h"
#include "rhythmdb-private.h"

enum
{
	PROP_0,
	PROP_DB,
	PROP_NAME,
	PROP_SAVE_TO_DISK,
	PROP_TYPE_DATA_SIZE,
	PROP_CATEGORY,
	PROP_HAS_PLAYLISTS		/* temporary */
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
	gboolean has_playlists;
};

G_DEFINE_TYPE (RhythmDBEntryType, rhythmdb_entry_type, G_TYPE_OBJECT)

/**
 * SECTION:rhythmdb-entry-type
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

	if (etype->get_playback_uri) {
		return (etype->get_playback_uri) (etype, entry);
	} else if (klass->get_playback_uri) {
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

	if (etype->update_availability) {
		(etype->update_availability) (etype, entry, avail);
	} else if (klass->get_playback_uri) {
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

	if (etype->entry_created) {
		etype->entry_created (etype, entry);
	} else if (klass->entry_created) {
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
	if (etype->destroy_entry) {
		etype->destroy_entry (etype, entry);
	} else if (klass->destroy_entry) {
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
	if (etype->can_sync_metadata) {
		return etype->can_sync_metadata (etype, entry);
	} else if (klass->can_sync_metadata) {
		return klass->can_sync_metadata (etype, entry);
	} else {
		return FALSE;
	}
}

/**
 * rhythmdb_entry_sync_metadata:
 * @entry: a #RhythmDBEntry
 * @changes: a list of #RhythmDBEntryChange structures
 * @error: returns error information
 *
 * Calls the entry type's method to sync metadata changes for @entry.
 */
void
rhythmdb_entry_sync_metadata (RhythmDBEntry *entry, GSList *changes, GError **error)
{
	RhythmDBEntryType *etype = rhythmdb_entry_get_entry_type (entry);
	RhythmDBEntryTypeClass *klass = RHYTHMDB_ENTRY_TYPE_GET_CLASS (etype);
	if (etype->sync_metadata) {
		etype->sync_metadata (etype, entry, changes, error);
	} else if (klass->sync_metadata) {
		klass->sync_metadata (etype, entry, changes, error);
	} else {
		/* default implementation? */
	}
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
	case PROP_HAS_PLAYLISTS:
		etype->priv->has_playlists = g_value_get_boolean (value);
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
	case PROP_HAS_PLAYLISTS:
		g_value_set_boolean (value, etype->priv->has_playlists);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_finalize (GObject *object)
{
	RhythmDBEntryType *etype = RHYTHMDB_ENTRY_TYPE (object);

	g_free (etype->priv->name);

	G_OBJECT_CLASS (rhythmdb_entry_type_parent_class)->finalize (object);
}

static void
rhythmdb_entry_type_class_init (RhythmDBEntryTypeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->finalize = impl_finalize;

	/**
	 * RhythmDBEntryTYpe:db
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
	 * RhythmDBEntryType:name
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
	 * RhythmDBEntryType:save-to-disk
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
	 * RhythmDBEntryType:type-data-size
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
	 * RhythmDBEntryType:category
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
	 * RhythmDBEntryType:has-playlists
	 *
	 * If %TRUE, entries of this type can be added to playlists.
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_PLAYLISTS,
					 g_param_spec_boolean ("has-playlists",
							       "has playlists",
							       "whether this type of entry has playlists",
							       FALSE,
							       G_PARAM_READWRITE));

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
