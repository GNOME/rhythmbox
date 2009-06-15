/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
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

#define	G_IMPLEMENT_INLINES 1
#define	__RHYTHMDB_C__
#include "rhythmdb.h"
#undef G_IMPLEMENT_INLINES

#include <string.h>
#include <libxml/tree.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gobject/gvaluecollector.h>
#include <gdk/gdk.h>
#include <gconf/gconf-client.h>


#include "rb-marshal.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rhythmdb-private.h"
#include "rhythmdb-property-model.h"
#include "rb-dialog.h"
#include "rb-string-value-map.h"
#include "rb-async-queue-watch.h"


#define RB_PARSE_NICK_START (xmlChar *) "["
#define RB_PARSE_NICK_END (xmlChar *) "]"

GType rhythmdb_property_type_map[RHYTHMDB_NUM_PROPERTIES];

#define RHYTHMDB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RHYTHMDB_TYPE, RhythmDBPrivate))
G_DEFINE_ABSTRACT_TYPE(RhythmDB, rhythmdb, G_TYPE_OBJECT)

/* file attributes requested in RHYTHMDB_ACTION_STAT and RHYTHMDB_ACTION_LOAD */
#define RHYTHMDB_FILE_INFO_ATTRIBUTES			\
	G_FILE_ATTRIBUTE_STANDARD_SIZE ","		\
	G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","	\
	G_FILE_ATTRIBUTE_STANDARD_TYPE ","		\
	G_FILE_ATTRIBUTE_TIME_MODIFIED

/* file attributes requested in RHYTHMDB_ACTION_ENUM_DIR */
#define RHYTHMDB_FILE_CHILD_INFO_ATTRIBUTES		\
	RHYTHMDB_FILE_INFO_ATTRIBUTES ","		\
	G_FILE_ATTRIBUTE_STANDARD_NAME

/*
 * Filters for MIME/media types to ignore.
 * The only complication here is that there are some application/ types that
 * are used for audio/video files.  Otherwise, we'd ignore everything except
 * audio/ and video/.
 */
struct media_type_filter {
	const char *prefix;
	gboolean ignore;
} media_type_filters[] = {
	{ "image/", TRUE },
	{ "text/", TRUE },
	{ "application/ogg", FALSE },
	{ "application/x-id3", FALSE },
	{ "application/x-apetag", FALSE },
	{ "application/x-3gp", FALSE },
	{ "application/", TRUE },
};

/*
 * File size below which we will simply ignore files that can't be identified.
 * This is mostly here so we ignore the various text files that are packaged
 * with many netlabel releases and other downloads.
 */
#define REALLY_SMALL_FILE_SIZE	(4096)


typedef struct
{
	RhythmDB *db;
	GPtrArray *query;
	guint propid;
	RhythmDBQueryResults *results;
	gboolean cancel;
} RhythmDBQueryThreadData;

typedef struct
{
	RhythmDB *db;
	RhythmDBEntryType type;
	RhythmDBEntryType ignore_type;
	RhythmDBEntryType error_type;
} RhythmDBAddThreadData;

typedef struct
{
	enum {
		RHYTHMDB_ACTION_STAT,
		RHYTHMDB_ACTION_LOAD,
		RHYTHMDB_ACTION_ENUM_DIR,
		RHYTHMDB_ACTION_SYNC,
		RHYTHMDB_ACTION_QUIT,
	} type;
	RBRefString *uri;
 	RhythmDBEntryType entry_type;
	RhythmDBEntryType ignore_type;
	RhythmDBEntryType error_type;
} RhythmDBAction;

static void rhythmdb_dispose (GObject *object);
static void rhythmdb_finalize (GObject *object);
static void rhythmdb_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rhythmdb_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
static void rhythmdb_thread_create (RhythmDB *db,
				    GThreadPool *pool,
				    GThreadFunc func,
				    gpointer data);
static void rhythmdb_read_enter (RhythmDB *db);
static void rhythmdb_read_leave (RhythmDB *db);
static void rhythmdb_process_one_event (RhythmDBEvent *event, RhythmDB *db);
static gpointer action_thread_main (RhythmDB *db);
static gpointer query_thread_main (RhythmDBQueryThreadData *data);
static void rhythmdb_entry_set_mount_point (RhythmDB *db,
 					    RhythmDBEntry *entry,
 					    const gchar *realuri);

static gboolean rhythmdb_idle_save (RhythmDB *db);
static void library_location_changed_cb (GConfClient *client,
					  guint cnxn_id,
					  GConfEntry *entry,
					  RhythmDB *db);
static void rhythmdb_sync_library_location (RhythmDB *db);
static void rhythmdb_entry_sync_mirrored (RhythmDBEntry *entry,
					  guint propid);
static void rhythmdb_register_core_entry_types (RhythmDB *db);
static gboolean rhythmdb_entry_extra_metadata_accumulator (GSignalInvocationHint *ihint,
							   GValue *return_accu,
							   const GValue *handler_return,
							   gpointer data);

static void rhythmdb_monitor_library_changed_cb (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RhythmDB *db);
static void rhythmdb_event_free (RhythmDB *db, RhythmDBEvent *event);
static void rhythmdb_add_to_stat_list (RhythmDB *db,
				       const char *uri,
				       RhythmDBEntry *entry,
				       RhythmDBEntryType type,
				       RhythmDBEntryType ignore_type,
				       RhythmDBEntryType error_type);

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DRY_RUN,
	PROP_NO_UPDATE,
};

enum
{
	ENTRY_ADDED,
	ENTRY_CHANGED,
	ENTRY_DELETED,
	ENTRY_KEYWORD_ADDED,
	ENTRY_KEYWORD_REMOVED,
	ENTRY_EXTRA_METADATA_REQUEST,
	ENTRY_EXTRA_METADATA_NOTIFY,
	ENTRY_EXTRA_METADATA_GATHER,
	LOAD_COMPLETE,
	SAVE_COMPLETE,
	SAVE_ERROR,
	READ_ONLY,
	MISSING_PLUGINS,
	CREATE_MOUNT_OP,
	LAST_SIGNAL
};

static guint rhythmdb_signals[LAST_SIGNAL] = { 0 };

static void
rhythmdb_class_init (RhythmDBClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rhythmdb_dispose;
	object_class->finalize = rhythmdb_finalize;

	object_class->set_property = rhythmdb_set_property;
	object_class->get_property = rhythmdb_get_property;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_DRY_RUN,
					 g_param_spec_boolean ("dry-run",
							       "dry run",
							       "Whether or not changes should be saved",
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_NO_UPDATE,
					 g_param_spec_boolean ("no-update",
							       "no update",
							       "Whether or not to update the database",
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * RhythmDB::entry-added:
	 * @db: the #RhythmDB
	 * @entry: the newly added #RhythmDBEntry
	 *
	 * Emitted when a new entry is added to the database.
	 */
	rhythmdb_signals[ENTRY_ADDED] =
		g_signal_new ("entry_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);

	/**
	 * RhythmDB::entry-deleted:
	 * @db: the #RhythmDB
	 * @entry: the deleted #RhythmDBEntry
	 *
	 * Emitted when an entry is deleted from the database.
	 */
	rhythmdb_signals[ENTRY_DELETED] =
		g_signal_new ("entry_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);

	/**
	 * RhythmDB::entry-changed:
	 * @db: the #RhythmDB
	 * @entry: the changed #RhythmDBEntry
	 * @changes: a #GSList of #RhythmDBEntryChanges structures describing the changes
	 *
	 * Emitted when a database entry is modified.  The @changes list
	 * contains a structure for each entry property that has been modified.
	 */
	rhythmdb_signals[ENTRY_CHANGED] =
		g_signal_new ("entry_changed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_changed),
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_POINTER,
			      G_TYPE_NONE, 2,
			      RHYTHMDB_TYPE_ENTRY, G_TYPE_POINTER);

	/**
	 * RhythmDB::entry-keyword-added:
	 * @db: the #RhythmDB
	 * @entry: the #RhythmDBEntry to which a keyword has been added
	 * @keyword: the keyword that was added
	 *
	 * Emitted when a keyword is added to an entry.
	 */
	rhythmdb_signals[ENTRY_KEYWORD_ADDED] =
		g_signal_new ("entry_keyword_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_added),
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_BOXED,
			      G_TYPE_NONE,
			      2, RHYTHMDB_TYPE_ENTRY, RB_TYPE_REFSTRING);

	/**
	 * RhythmDB::entry-keyword-removed:
	 * @db: the #RhythmDB
	 * @entry: the #RhythmDBEntry from which a keyword has been removed
	 * @keyword: the keyword that was removed
	 *
	 * Emitted when a keyword is removed from an entry.
	 */
	rhythmdb_signals[ENTRY_KEYWORD_REMOVED] =
		g_signal_new ("entry_keyword_removed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_BOXED,
			      G_TYPE_NONE,
			      2, RHYTHMDB_TYPE_ENTRY, RB_TYPE_REFSTRING);

	/**
	 * RhythmDB::entry-extra-metadata-request:
	 * @db: the #RhythmDB
	 * @entry: the #RhythmDBEntry for which extra metadata is being requested
	 *
	 * This signal is emitted to allow extra (transient) metadata to be supplied
	 * for the given entry.  The detail of the signal invocation describes the
	 * specific metadata value being requested.  If the object handling the signal
	 * can provide the requested item, but it isn't immediately available, it can
	 * initiate an attempt to retrieve it.  If successful, it would call
	 * @rhythmdb_emit_entry_extra_metadata_notify when the metadata is available.
	 *
	 * Return value: the extra metadata value
	 */
	rhythmdb_signals[ENTRY_EXTRA_METADATA_REQUEST] =
		g_signal_new ("entry_extra_metadata_request",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_extra_metadata_request),
			      rhythmdb_entry_extra_metadata_accumulator, NULL,
			      rb_marshal_BOXED__BOXED,
			      G_TYPE_VALUE, 1,
			      RHYTHMDB_TYPE_ENTRY);

	/**
	 * RhythmDB::entry-extra-metadata-notify:
	 * @db: the #RhythmDB
	 * @entry: the #RhythmDBEntry for which extra metadata has been supplied
	 * @field: the extra metadata field being supplied
	 * @metadata: the extra metadata value
	 *
	 * This signal is emitted when an extra metadata value is provided for a specific
	 * entry independantly of an extra metadata request.
	 */
	rhythmdb_signals[ENTRY_EXTRA_METADATA_NOTIFY] =
		g_signal_new ("entry_extra_metadata_notify",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_extra_metadata_notify),
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_STRING_BOXED,
			      G_TYPE_NONE, 3,
			      RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_VALUE);

	/**
	 * RhythmDB::entry-extra-metadata-gather:
	 * @db: the #RhythmDB
	 * @entry: the #RhythmDBEntry for which to gather metadata
	 * @data: a #RBStringValueMap to hold the gathered metadata
	 *
	 * Emitted to gather all available extra metadata for a database entry.
	 * Handlers for this signal should insert any metadata they can provide
	 * into the string-value map.  Only immediately available metadata
	 * items should be returned.  If one or more metadata items is not
	 * immediately available, the handler should not initiate an attempt to
	 * retrieve them.
	 */
	rhythmdb_signals[ENTRY_EXTRA_METADATA_GATHER] =
		g_signal_new ("entry_extra_metadata_gather",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_extra_metadata_gather),
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_OBJECT,
			      G_TYPE_NONE, 2,
			      RHYTHMDB_TYPE_ENTRY, RB_TYPE_STRING_VALUE_MAP);

	/**
	 * RhythmDB::load-complete:
	 * @db: the #RhythmDB
	 *
	 * Emitted when the database is fully loaded.
	 */
	rhythmdb_signals[LOAD_COMPLETE] =
		g_signal_new ("load_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	/**
	 * RhythmDB::save-completed:
	 * @db: the #RhythmDB
	 *
	 * Emitted when the database has been saved.
	 */
	rhythmdb_signals[SAVE_COMPLETE] =
		g_signal_new ("save_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, save_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	/**
	 * RhythmDB::save-error:
	 * @db: the #RhythmDB
	 * @uri: URI of the database file
	 * @error: the error that occurred
	 *
	 * Emitted when an error occurs while saving the database.
	 */
	rhythmdb_signals[SAVE_ERROR] =
		g_signal_new ("save-error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, save_error),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_POINTER);

	/**
	 * RhythmDB::read-only:
	 * @db: the #RhythmDB
	 * @readonly: %TRUE if the database is read-only
	 *
	 * Emitted when the database becomes temporarily read-only, or becomes
	 * writeable after being read-only.
	 */
	rhythmdb_signals[READ_ONLY] =
		g_signal_new ("read-only",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, read_only),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	/**
	 * RhythmDB::missing-plugins:
	 * @db: the #RhythmDB
	 * @details: a NULL-terminated array of missing plugin detail strings
	 * @descriptions: a NULL-terminated array of missing plugin description strings
	 * @closure: a #GClosure to be invoked when missing plugin processing is finished
	 *
	 * Emitted to request installation of GStreamer plugins required to import a file
	 * into the database.
	 */
	rhythmdb_signals[MISSING_PLUGINS] =
		g_signal_new ("missing-plugins",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,		/* no need for an internal handler */
			      NULL, NULL,
			      rb_marshal_BOOLEAN__POINTER_POINTER_POINTER,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRV, G_TYPE_STRV, G_TYPE_CLOSURE);

	/**
	 * RhythmDB::create-mount-op:
	 * @db: the #RhythmDB
	 *
	 * Emitted to request creation of a #GMountOperation to use to mount a volume.
	 *
	 * Return value: a #GMountOperation (usually actually a #GtkMountOperation)
	 */
	rhythmdb_signals[CREATE_MOUNT_OP] =
		g_signal_new ("create-mount-op",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,		/* no need for an internal handler */
			      rb_signal_accumulator_object_handled, NULL,
			      rb_marshal_OBJECT__VOID,
			      G_TYPE_MOUNT_OPERATION,
			      0);

	g_type_class_add_private (klass, sizeof (RhythmDBPrivate));
}

static void
rhythmdb_push_event (RhythmDB *db, RhythmDBEvent *event)
{
	g_async_queue_push (db->priv->event_queue, event);
	g_main_context_wakeup (g_main_context_default ());
}

static gboolean
metadata_field_from_prop (RhythmDBPropType prop,
			  RBMetaDataField *field)
{
	switch (prop) {
	case RHYTHMDB_PROP_TITLE:
		*field = RB_METADATA_FIELD_TITLE;
		return TRUE;
	case RHYTHMDB_PROP_ARTIST:
		*field = RB_METADATA_FIELD_ARTIST;
		return TRUE;
	case RHYTHMDB_PROP_ALBUM:
		*field = RB_METADATA_FIELD_ALBUM;
		return TRUE;
	case RHYTHMDB_PROP_GENRE:
		*field = RB_METADATA_FIELD_GENRE;
		return TRUE;
	case RHYTHMDB_PROP_TRACK_NUMBER:
		*field = RB_METADATA_FIELD_TRACK_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_DISC_NUMBER:
		*field = RB_METADATA_FIELD_DISC_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_DATE:
		*field = RB_METADATA_FIELD_DATE;
		return TRUE;
	case RHYTHMDB_PROP_TRACK_GAIN:
		*field = RB_METADATA_FIELD_TRACK_GAIN;
		return TRUE;
	case RHYTHMDB_PROP_TRACK_PEAK:
		*field = RB_METADATA_FIELD_TRACK_PEAK;
		return TRUE;
	case RHYTHMDB_PROP_ALBUM_GAIN:
		*field = RB_METADATA_FIELD_ALBUM_GAIN;
		return TRUE;
	case RHYTHMDB_PROP_ALBUM_PEAK:
		*field = RB_METADATA_FIELD_ALBUM_PEAK;
		return TRUE;
	case RHYTHMDB_PROP_MUSICBRAINZ_TRACKID:
		*field = RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
		return TRUE;
	case RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID:
		*field = RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID;
		return TRUE;
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID:
		*field = RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID;
		return TRUE;
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID:
		*field = RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID;
		return TRUE;
	case RHYTHMDB_PROP_ARTIST_SORTNAME:
		*field = RB_METADATA_FIELD_ARTIST_SORTNAME;
		return TRUE;
	case RHYTHMDB_PROP_ALBUM_SORTNAME:
		*field = RB_METADATA_FIELD_ALBUM_SORTNAME;
		return TRUE;
	default:
		return FALSE;
	}
}

static GType
extract_gtype_from_enum_entry (RhythmDB *db,
			       GEnumClass *klass,
			       guint i)
{
	GType ret;
	GEnumValue *value;
	RBMetaDataField field;
	char *typename;
	char *typename_end;

	value = g_enum_get_value (klass, i);

	typename = strstr (value->value_nick, "(");
	g_assert (typename != NULL);

	typename_end = strstr (typename, ")");
	g_assert (typename_end);

	typename++;
	typename = g_strndup (typename, typename_end-typename);
	ret = g_type_from_name (typename);
	g_free (typename);

	/* Check to see whether this is a property that maps to
	   a RBMetaData property. */
	if (metadata_field_from_prop (value->value, &field))
		g_assert (ret == rb_metadata_get_field_type (field));
	return ret;
}

static xmlChar *
extract_nice_name_from_enum_entry (RhythmDB *db,
				   GEnumClass *klass,
				   guint i)
{
	GEnumValue *value;
	xmlChar *nick;
	const xmlChar *name;
	const xmlChar *name_end;

	value = g_enum_get_value (klass, i);
	nick = BAD_CAST value->value_nick;

	name = xmlStrstr (nick, RB_PARSE_NICK_START);
	g_return_val_if_fail (name != NULL, NULL);
	name_end = xmlStrstr (name, RB_PARSE_NICK_END);
	name++;

	return xmlStrndup (name, name_end - name);
}

static void
rhythmdb_init (RhythmDB *db)
{
	guint i;
	GEnumClass *prop_class;

	db->priv = RHYTHMDB_GET_PRIVATE (db);

	db->priv->action_queue = g_async_queue_new ();
	db->priv->event_queue = g_async_queue_new ();
	db->priv->delayed_write_queue = g_async_queue_new ();
	db->priv->event_queue_watch_id = rb_async_queue_watch_new (db->priv->event_queue,
								   G_PRIORITY_LOW,		/* really? */
								   (RBAsyncQueueWatchFunc) rhythmdb_process_one_event,
								   db,
								   NULL,
								   NULL);

	db->priv->restored_queue = g_async_queue_new ();

	db->priv->query_thread_pool = g_thread_pool_new ((GFunc)query_thread_main,
							 NULL,
							 -1, FALSE, NULL);

	db->priv->metadata = rb_metadata_new ();
	db->priv->metadata_blocked = FALSE;
	db->priv->metadata_cond = g_cond_new ();
	db->priv->metadata_lock = g_mutex_new ();

	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP_TYPE);

	g_assert (prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);
	db->priv->column_xml_names = g_new0 (xmlChar *, RHYTHMDB_NUM_PROPERTIES);

	/* Now, extract the GType and XML tag of each column from the
	 * enum descriptions, and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++) {
		rhythmdb_property_type_map[i] = extract_gtype_from_enum_entry (db, prop_class, i);
		g_assert (rhythmdb_property_type_map[i] != G_TYPE_INVALID);

		db->priv->column_xml_names[i] = extract_nice_name_from_enum_entry (db, prop_class, i);
		g_assert (db->priv->column_xml_names[i]);
	}

	g_type_class_unref (prop_class);

	db->priv->propname_map = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		const xmlChar *name = rhythmdb_nice_elt_name_from_propid (db, i);
		g_hash_table_insert (db->priv->propname_map, (gpointer) name, GINT_TO_POINTER (i));
	}

	db->priv->entry_type_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	db->priv->entry_type_map_mutex = g_mutex_new ();
	db->priv->entry_type_mutex = g_mutex_new ();
	rhythmdb_register_core_entry_types (db);

 	db->priv->stat_mutex = g_mutex_new ();

	db->priv->change_mutex = g_mutex_new ();

	db->priv->changed_entries = g_hash_table_new_full (NULL,
							   NULL,
							   (GDestroyNotify) rhythmdb_entry_unref,
							   NULL);
	db->priv->added_entries = g_hash_table_new_full (NULL,
							 NULL,
							 (GDestroyNotify) rhythmdb_entry_unref,
							 NULL);
	db->priv->deleted_entries = g_hash_table_new_full (NULL,
							   NULL,
							   (GDestroyNotify) rhythmdb_entry_unref,
							   NULL);

	db->priv->saving_condition = g_cond_new ();
	db->priv->saving_mutex = g_mutex_new ();

	db->priv->can_save = TRUE;
	db->priv->exiting = g_cancellable_new ();
	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	db->priv->empty_string = rb_refstring_new ("");
	db->priv->octet_stream_str = rb_refstring_new ("application/octet-stream");

	db->priv->next_entry_id = 1;

	rhythmdb_init_monitoring (db);

	db->priv->monitor_notify_id = 
		eel_gconf_notification_add (CONF_MONITOR_LIBRARY,
					   (GConfClientNotifyFunc)rhythmdb_monitor_library_changed_cb,
					   db);
}

static GError *
make_access_failed_error (const char *uri, GError *access_error)
{
	char *unescaped;
	char *utf8ised;
	GError *error;

	/* make sure the URI we put in the error message is valid utf8 */
	unescaped = g_uri_unescape_string (uri, NULL);
	utf8ised = rb_make_valid_utf8 (unescaped, '?');

	error = g_error_new (RHYTHMDB_ERROR,
			     RHYTHMDB_ERROR_ACCESS_FAILED,
			     _("Couldn't access %s: %s"),
			     utf8ised,
			     access_error->message);
	rb_debug ("got error on %s: %s", uri, error->message);
	g_free (unescaped);
	g_free (utf8ised);
	return error;
}

static gboolean
rhythmdb_ignore_media_type (const char *media_type)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (media_type_filters); i++) {
		if (g_str_has_prefix (media_type, media_type_filters[i].prefix)) {
			return media_type_filters[i].ignore;
		}
	}
	return FALSE;
}

typedef struct {
	RhythmDBEvent *event;
	GMutex *mutex;
	GCond *cond;
	GError **error;
} RhythmDBStatThreadMountData;

static void
stat_thread_mount_done_cb (GObject *source, GAsyncResult *result, RhythmDBStatThreadMountData *data)
{
	g_mutex_lock (data->mutex);
	g_file_mount_enclosing_volume_finish (G_FILE (source), result, data->error);

	g_cond_signal (data->cond);
	g_mutex_unlock (data->mutex);
}

typedef struct {
	RhythmDB *db;
	GList *stat_list;
} RhythmDBStatThreadData;

static gpointer
stat_thread_main (RhythmDBStatThreadData *data)
{
	GList *i;
	GError *error = NULL;
	RhythmDBEvent *result;
	int count = 0;

	rb_debug ("entering stat thread: %d to process", g_list_length (data->stat_list));
	for (i = data->stat_list; i != NULL; i = i->next) {
		RhythmDBEvent *event = (RhythmDBEvent *)i->data;
		GFile *file;

		/* if we've been cancelled, just free the event.  this will
		 * clean up the list and then we'll exit the thread.
		 */
		if (g_cancellable_is_cancelled (data->db->priv->exiting)) {
			rhythmdb_event_free (data->db, event);
			count = 0;
			continue;
		}

		if (count > 0 && count % 1000 == 0) {
			rb_debug ("%d file info queries done", count);
		}

		file = g_file_new_for_uri (rb_refstring_get (event->uri));
		event->real_uri = rb_refstring_ref (event->uri);		/* what? */
		event->file_info = g_file_query_info (file,
						      G_FILE_ATTRIBUTE_TIME_MODIFIED,	/* anything else? */
						      G_FILE_QUERY_INFO_NONE,
						      data->db->priv->exiting,
						      &error);
		if (error != NULL) {
			if (g_error_matches (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_MOUNTED)) {
				GMountOperation *mount_op = NULL;

				rb_debug ("got not-mounted error for %s", rb_refstring_get (event->uri));

				/* check if we've tried and failed to mount this location before */

				g_signal_emit (event->db, rhythmdb_signals[CREATE_MOUNT_OP], 0, &mount_op);
				if (mount_op != NULL) {
					RhythmDBStatThreadMountData mount_data;

					mount_data.event = event;
					mount_data.cond = g_cond_new ();
					mount_data.mutex = g_mutex_new ();
					mount_data.error = &error;

					g_mutex_lock (mount_data.mutex);

					g_file_mount_enclosing_volume (file,
								       G_MOUNT_MOUNT_NONE,
								       mount_op,
								       data->db->priv->exiting,
								       (GAsyncReadyCallback) stat_thread_mount_done_cb,
								       &mount_data);
					g_clear_error (&error);

					/* wait for the mount to complete.  the callback occurs on the main
					 * thread (not this thread), so we can just block until it is called.
					 */
					g_cond_wait (mount_data.cond, mount_data.mutex);
					g_mutex_unlock (mount_data.mutex);

					g_mutex_free (mount_data.mutex);
					g_cond_free (mount_data.cond);

					if (error == NULL) {
						rb_debug ("mount op successful, retrying stat");
						event->file_info = g_file_query_info (file,
										      G_FILE_ATTRIBUTE_TIME_MODIFIED,
										      G_FILE_QUERY_INFO_NONE,
										      data->db->priv->exiting,
										      &error);
					}
				} else {
					rb_debug ("but couldn't create a mount op.");
				}
			}

			if (error != NULL) {
				event->error = make_access_failed_error (rb_refstring_get (event->uri), error);
				g_clear_error (&error);
			}
		}

		if (event->error != NULL) {
			if (event->file_info != NULL) {
				g_object_unref (event->file_info);
				event->file_info = NULL;
			}
		}

		g_async_queue_push (data->db->priv->event_queue, event);
		g_object_unref (file);
		count++;
	}

	g_list_free (data->stat_list);

	data->db->priv->stat_thread_running = FALSE;
	
	rb_debug ("exiting stat thread");
	result = g_slice_new0 (RhythmDBEvent);
	result->db = data->db;			/* need to unref? */
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (data->db, result);

	g_free (data);
	return NULL;
}

void
rhythmdb_start_action_thread (RhythmDB *db)
{
	g_mutex_lock (db->priv->stat_mutex);
	db->priv->action_thread_running = TRUE;
	rhythmdb_thread_create (db, NULL, (GThreadFunc) action_thread_main, db);

	if (db->priv->stat_list != NULL) {
		RhythmDBStatThreadData *data;
		data = g_new0 (RhythmDBStatThreadData, 1);
		data->db = g_object_ref (db);
		data->stat_list = db->priv->stat_list;
		db->priv->stat_list = NULL;

		db->priv->stat_thread_running = TRUE;
		rhythmdb_thread_create (db, NULL, (GThreadFunc) stat_thread_main, data);
	}

	g_mutex_unlock (db->priv->stat_mutex);
}

static void
rhythmdb_action_free (RhythmDB *db,
		      RhythmDBAction *action)
{
	rb_refstring_unref (action->uri);
	g_slice_free (RhythmDBAction, action);
}

static void
rhythmdb_event_free (RhythmDB *db,
		     RhythmDBEvent *result)
{
	switch (result->type) {
	case RHYTHMDB_EVENT_THREAD_EXITED:
		g_object_unref (db);
		g_assert (g_atomic_int_dec_and_test (&db->priv->outstanding_threads) >= 0);
		g_async_queue_unref (db->priv->action_queue);
		g_async_queue_unref (db->priv->event_queue);
		break;
	case RHYTHMDB_EVENT_STAT:
	case RHYTHMDB_EVENT_METADATA_LOAD:
	case RHYTHMDB_EVENT_DB_LOAD:
	case RHYTHMDB_EVENT_DB_SAVED:
	case RHYTHMDB_EVENT_QUERY_COMPLETE:
	case RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED:
	case RHYTHMDB_EVENT_FILE_DELETED:
		break;
	case RHYTHMDB_EVENT_ENTRY_SET:
		g_value_unset (&result->change.new);
		break;
	}
	if (result->error)
		g_error_free (result->error);
	rb_refstring_unref (result->uri);
	rb_refstring_unref (result->real_uri);
	if (result->file_info)
		g_object_unref (result->file_info);
	if (result->metadata)
		g_object_unref (result->metadata);
	if (result->results)
		g_object_unref (result->results);
	if (result->entry != NULL) {
		rhythmdb_entry_unref (result->entry);
	}
	g_slice_free (RhythmDBEvent, result);
}

static void
_shutdown_foreach_swapped (RhythmDBEvent *event, RhythmDB *db)
{
	rhythmdb_event_free (db, event);
}

/**
 * rhythmdb_shutdown:
 * @db: the #RhythmDB
 *
 * Ceases all #RhythmDB operations, including stopping all directory monitoring, and
 * removing all actions and events currently queued.
 **/
void
rhythmdb_shutdown (RhythmDB *db)
{
	RhythmDBEvent *result;
	RhythmDBAction *action;

	g_return_if_fail (RHYTHMDB_IS (db));

	g_cancellable_cancel (db->priv->exiting);

	/* force the action thread to wake up and exit */
	action = g_slice_new0 (RhythmDBAction);
	action->type = RHYTHMDB_ACTION_QUIT;
	g_async_queue_push (db->priv->action_queue, action);

	eel_gconf_notification_remove (db->priv->library_location_notify_id);
	db->priv->library_location_notify_id = 0;
	g_slist_foreach (db->priv->library_locations, (GFunc) g_free, NULL);
	g_slist_free (db->priv->library_locations);
	db->priv->library_locations = NULL;

	eel_gconf_notification_remove (db->priv->monitor_notify_id);
	db->priv->monitor_notify_id = 0;

	/* abort all async io operations */
	g_mutex_lock (db->priv->stat_mutex);
	g_list_foreach (db->priv->outstanding_stats, (GFunc)_shutdown_foreach_swapped, db);
	g_list_free (db->priv->outstanding_stats);
	db->priv->outstanding_stats = NULL;
	g_mutex_unlock (db->priv->stat_mutex);

	rb_debug ("%d outstanding threads", g_atomic_int_get (&db->priv->outstanding_threads));
	while (g_atomic_int_get (&db->priv->outstanding_threads) > 0) {
		result = g_async_queue_pop (db->priv->event_queue);
		rhythmdb_event_free (db, result);
	}

	/* FIXME */
	while ((result = g_async_queue_try_pop (db->priv->event_queue)) != NULL)
		rhythmdb_event_free (db, result);
	while ((result = g_async_queue_try_pop (db->priv->delayed_write_queue)) != NULL)
		rhythmdb_event_free (db, result);

	while ((action = g_async_queue_try_pop (db->priv->action_queue)) != NULL) {
		rhythmdb_action_free (db, action);
	}

	g_object_unref (db->priv->exiting);
}

static void
rhythmdb_dispose (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	rb_debug ("disposing rhythmdb");
	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	rhythmdb_dispose_monitoring (db);

	if (db->priv->event_queue_watch_id != 0) {
		g_source_remove (db->priv->event_queue_watch_id);
		db->priv->event_queue_watch_id = 0;
	}

	if (db->priv->save_timeout_id != 0) {
		g_source_remove (db->priv->save_timeout_id);
		db->priv->save_timeout_id = 0;
	}

	if (db->priv->emit_entry_signals_id != 0) {
		g_source_remove (db->priv->emit_entry_signals_id);
		db->priv->emit_entry_signals_id = 0;

		g_list_foreach (db->priv->added_entries_to_emit, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_foreach (db->priv->deleted_entries_to_emit, (GFunc)rhythmdb_entry_unref, NULL);
		if (db->priv->changed_entries_to_emit != NULL) {
			g_hash_table_destroy (db->priv->changed_entries_to_emit);
		}
	}

	if (db->priv->metadata != NULL) {
		g_object_unref (db->priv->metadata);
		db->priv->metadata = NULL;
	}

	G_OBJECT_CLASS (rhythmdb_parent_class)->dispose (object);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;
	int  i;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	rb_debug ("finalizing rhythmdb");
	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	rhythmdb_finalize_monitoring (db);

	g_thread_pool_free (db->priv->query_thread_pool, FALSE, TRUE);
	g_async_queue_unref (db->priv->action_queue);
	g_async_queue_unref (db->priv->event_queue);
	g_async_queue_unref (db->priv->restored_queue);
	g_async_queue_unref (db->priv->delayed_write_queue);

	g_mutex_free (db->priv->saving_mutex);
	g_cond_free (db->priv->saving_condition);

	g_list_free (db->priv->stat_list);
 	g_mutex_free (db->priv->stat_mutex);

	g_mutex_free (db->priv->change_mutex);

	g_hash_table_destroy (db->priv->propname_map);

	g_hash_table_destroy (db->priv->added_entries);
	g_hash_table_destroy (db->priv->deleted_entries);
	g_hash_table_destroy (db->priv->changed_entries);

	rb_refstring_unref (db->priv->empty_string);
	rb_refstring_unref (db->priv->octet_stream_str);

	g_hash_table_destroy (db->priv->entry_type_map);
	g_mutex_free (db->priv->entry_type_map_mutex);
	g_mutex_free (db->priv->entry_type_mutex);

	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		xmlFree (db->priv->column_xml_names[i]);
	}
	g_free (db->priv->column_xml_names);

	g_free (db->priv->name);

	G_OBJECT_CLASS (rhythmdb_parent_class)->finalize (object);
}

static void
rhythmdb_set_property (GObject *object,
		       guint prop_id,
		       const GValue *value,
		       GParamSpec *pspec)
{
	RhythmDB *db = RHYTHMDB (object);

	switch (prop_id) {
	case PROP_NAME:
		g_free (db->priv->name);
		db->priv->name = g_value_dup_string (value);
		break;
	case PROP_DRY_RUN:
		db->priv->dry_run = g_value_get_boolean (value);
		break;
	case PROP_NO_UPDATE:
		db->priv->no_update = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_get_property (GObject *object,
		       guint prop_id,
		       GValue *value,
		       GParamSpec *pspec)
{
	RhythmDB *source = RHYTHMDB (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, source->priv->name);
		break;
	case PROP_DRY_RUN:
		g_value_set_boolean (value, source->priv->dry_run);
		break;
	case PROP_NO_UPDATE:
		g_value_set_boolean (value, source->priv->no_update);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_thread_create (RhythmDB *db,
			GThreadPool *pool,
			GThreadFunc func,
			gpointer data)
{
	g_object_ref (db);
	g_atomic_int_inc (&db->priv->outstanding_threads);
	g_async_queue_ref (db->priv->action_queue);
	g_async_queue_ref (db->priv->event_queue);

	if (pool)
		g_thread_pool_push (pool, data, NULL);
	else
		g_thread_create ((GThreadFunc) func, data, FALSE, NULL);
}

static gboolean
rhythmdb_get_readonly (RhythmDB *db)
{
	return (g_atomic_int_get (&db->priv->read_counter) > 0);
}

static void
rhythmdb_read_enter (RhythmDB *db)
{
	gint count;
	g_return_if_fail (g_atomic_int_get (&db->priv->read_counter) >= 0);
	g_assert (rb_is_main_thread ());

	count = g_atomic_int_exchange_and_add (&db->priv->read_counter, 1);
	rb_debug ("counter: %d", count+1);
	if (count == 0)
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[READ_ONLY],
			       0, TRUE);
}

static void
rhythmdb_read_leave (RhythmDB *db)
{
	gint count;
	g_return_if_fail (rhythmdb_get_readonly (db));
	g_assert (rb_is_main_thread ());

	count = g_atomic_int_exchange_and_add (&db->priv->read_counter, -1);
	rb_debug ("counter: %d", count-1);
	if (count == 1) {

		g_signal_emit (G_OBJECT (db), rhythmdb_signals[READ_ONLY],
			       0, FALSE);

		/* move any delayed writes back to the main event queue */
		if (g_async_queue_length (db->priv->delayed_write_queue) > 0) {
			RhythmDBEvent *event;
			while ((event = g_async_queue_try_pop (db->priv->delayed_write_queue)) != NULL)
				g_async_queue_push (db->priv->event_queue, event);

			g_main_context_wakeup (g_main_context_default ());
		}

	}
}

static void
free_entry_changes (GSList *entry_changes)
{
	GSList *t;
	for (t = entry_changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;
		g_value_unset (&change->old);
		g_value_unset (&change->new);
		g_slice_free (RhythmDBEntryChange, change);
	}
	g_slist_free (entry_changes);
}

static gboolean
rhythmdb_emit_entry_signals_idle (RhythmDB *db)
{
	GList *added_entries;
	GList *deleted_entries;
	GHashTable *changed_entries;
	GList *l;
	GHashTableIter iter;
	RhythmDBEntry *entry;
	GSList *entry_changes;

	/* get lists of entries to emit, reset source id value */
	g_mutex_lock (db->priv->change_mutex);

	added_entries = db->priv->added_entries_to_emit;
	db->priv->added_entries_to_emit = NULL;

	deleted_entries = db->priv->deleted_entries_to_emit;
	db->priv->deleted_entries_to_emit = NULL;

	changed_entries = db->priv->changed_entries_to_emit;
	db->priv->changed_entries_to_emit = NULL;

	db->priv->emit_entry_signals_id = 0;

	g_mutex_unlock (db->priv->change_mutex);

	GDK_THREADS_ENTER ();

	/* emit changed entries */
	if (changed_entries != NULL) {
		g_hash_table_iter_init (&iter, changed_entries);
		while (g_hash_table_iter_next (&iter, (gpointer *)&entry, (gpointer *)&entry_changes)) {
			g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, entry, entry_changes);
			g_hash_table_iter_remove (&iter);
		}
	}

	/* emit added entries */
	for (l = added_entries; l; l = g_list_next (l)) {
		entry = (RhythmDBEntry *)l->data;
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, entry);
		rhythmdb_entry_unref (entry);
	}

	/* emit deleted entries */
	for (l = deleted_entries; l; l = g_list_next (l)) {
		entry = (RhythmDBEntry *)l->data;
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
		rhythmdb_entry_unref (entry);
	}

	GDK_THREADS_LEAVE ();

	if (changed_entries != NULL) {
		g_hash_table_destroy (changed_entries);
	}
	g_list_free (added_entries);
	g_list_free (deleted_entries);
	return FALSE;
}

static gboolean
process_added_entries_cb (RhythmDBEntry *entry,
			  GThread *thread,
			  RhythmDB *db)
{
	if (thread != g_thread_self ())
		return FALSE;

	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		const gchar *uri;

		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		if (uri == NULL)
			return TRUE;

		/* something to think about: only do the stat if the mountpoint is
		 * NULL.  other things are likely to be removable disks and network
		 * shares, where getting file info for a large number of files is going
		 * to be slow.  on the other hand, we'd want to mount those on startup
		 * rather than on first access, which may not be predictable.  hmm..
		 *
		 * we'd probably have to improve handling of the particular 'file not found'
		 * playback error, though.  or perhaps stat immediately before playback?
		 * what about crawling the filesystem to find new files?
		 *
		 * further: this should only be done for entries loaded from the database file,
		 * not for newly added entries.  cripes.
		 *
		 * hmm, do we really need to take the stat mutex to check if the action thread is running?
		 * maybe it should be atomicised?
		 */
		g_mutex_lock (db->priv->stat_mutex);
		if (db->priv->action_thread_running == FALSE) {
			rhythmdb_add_to_stat_list (db, uri, entry,
						   RHYTHMDB_ENTRY_TYPE_INVALID,
						   RHYTHMDB_ENTRY_TYPE_IGNORE,
						   RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
		}
		g_mutex_unlock (db->priv->stat_mutex);
	}

	g_assert ((entry->flags & RHYTHMDB_ENTRY_INSERTED) == 0);
	entry->flags |= RHYTHMDB_ENTRY_INSERTED;

	rhythmdb_entry_ref (entry);
	db->priv->added_entries_to_emit = g_list_prepend (db->priv->added_entries_to_emit, entry);

	return TRUE;
}

static gboolean
process_deleted_entries_cb (RhythmDBEntry *entry,
			    GThread *thread,
			    RhythmDB *db)
{
	if (thread != g_thread_self ())
		return FALSE;

	rhythmdb_entry_ref (entry);
	g_assert ((entry->flags & RHYTHMDB_ENTRY_INSERTED) != 0);
	entry->flags &= ~(RHYTHMDB_ENTRY_INSERTED);
	db->priv->deleted_entries_to_emit = g_list_prepend (db->priv->deleted_entries_to_emit, entry);

	return TRUE;
}

static gboolean
process_changed_entries_cb (RhythmDBEntry *entry,
			    GSList *changes,
			    RhythmDB *db)
{
	if (db->priv->changed_entries_to_emit == NULL) {
		db->priv->changed_entries_to_emit = g_hash_table_new_full (NULL,
									   NULL,
									   (GDestroyNotify) rhythmdb_entry_unref,
									   (GDestroyNotify) free_entry_changes);
	}

	g_hash_table_insert (db->priv->changed_entries_to_emit, rhythmdb_entry_ref (entry), changes);
	return TRUE;
}

static void
sync_entry_changed (RhythmDBEntry *entry,
		    GSList *changes,
		    RhythmDB *db)
{
	GSList *t;

	for (t = changes; t; t = t->next) {
		RBMetaDataField field;
		RhythmDBEntryChange *change = t->data;

		if (metadata_field_from_prop (change->prop, &field)) {
			RhythmDBAction *action;

			if (!rhythmdb_entry_is_editable (db, entry)) {
				g_warning ("trying to sync properties of non-editable file");
				break;
			}

			action = g_slice_new0 (RhythmDBAction);
			action->type = RHYTHMDB_ACTION_SYNC;
			action->uri = rb_refstring_ref (entry->location);
			g_async_queue_push (db->priv->action_queue, action);
			break;
		}
	}
}


static void
rhythmdb_commit_internal (RhythmDB *db,
			  gboolean sync_changes,
			  GThread *thread)
{
	g_mutex_lock (db->priv->change_mutex);
	
	if (sync_changes) {
		g_hash_table_foreach (db->priv->changed_entries, (GHFunc) sync_entry_changed, db);
	}

	/* update the sets of entry changed/added/deleted signals to emit */
	g_hash_table_foreach_remove (db->priv->changed_entries, (GHRFunc) process_changed_entries_cb, db);
	g_hash_table_foreach_remove (db->priv->added_entries, (GHRFunc) process_added_entries_cb, db);
	g_hash_table_foreach_remove (db->priv->deleted_entries, (GHRFunc) process_deleted_entries_cb, db);

	/* if there are some signals to emit, add a new idle callback if required */
	if (db->priv->added_entries_to_emit || db->priv->deleted_entries_to_emit || db->priv->changed_entries_to_emit) {
		if (db->priv->emit_entry_signals_id == 0)
			db->priv->emit_entry_signals_id = g_idle_add ((GSourceFunc) rhythmdb_emit_entry_signals_idle, db);
	}

	g_mutex_unlock (db->priv->change_mutex);
}

typedef struct {
	RhythmDB *db;
	gboolean sync;
	GThread *thread;
} RhythmDBTimeoutCommitData;

static gboolean
timeout_rhythmdb_commit (RhythmDBTimeoutCommitData *data)
{
	rhythmdb_commit_internal (data->db, data->sync, data->thread);
	g_object_unref (data->db);
	g_free (data);
	return FALSE;
}

static void
rhythmdb_add_timeout_commit (RhythmDB *db,
			     gboolean sync)
{
	RhythmDBTimeoutCommitData *data;

	g_assert (rb_is_main_thread ());

	data = g_new0 (RhythmDBTimeoutCommitData, 1);
	data->db = g_object_ref (db);
	data->sync = sync;
	data->thread = g_thread_self ();
	g_timeout_add (100, (GSourceFunc)timeout_rhythmdb_commit, data);
}

/**
 * rhythmdb_commit:
 * @db: a #RhythmDB.
 *
 * Apply all database changes, and send notification of changes and new entries.
 * This needs to be called after any changes have been made, such as a group of
 * rhythmdb_entry_set() calls, or a new entry has been added.
 **/
void
rhythmdb_commit (RhythmDB *db)
{
	rhythmdb_commit_internal (db, TRUE, g_thread_self ());
}

GQuark
rhythmdb_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("rhythmdb_error");

	return quark;
}

/* structure alignment magic, stolen from glib */
#define STRUCT_ALIGNMENT	(2 * sizeof (gsize))
#define ALIGN_STRUCT(offset) \
	((offset + (STRUCT_ALIGNMENT - 1)) & -STRUCT_ALIGNMENT)

/**
 * rhythmdb_entry_allocate:
 * @db: a #RhythmDB.
 * @type: type of entry to allocate
 *
 * Allocate and initialise memory for a new #RhythmDBEntry of the type @type.
 * The entry's initial properties needs to be set with rhythmdb_entry_set (),
 * the entry added to the database with rhythmdb_entry_insert(), and committed with
 * rhythmdb_commit().
 *
 * This should only be used by RhythmDB itself, or a backend (such as rhythmdb-tree).
 *
 * Returns: the newly allocated #RhythmDBEntry
 **/
RhythmDBEntry *
rhythmdb_entry_allocate (RhythmDB *db,
			 RhythmDBEntryType type)
{
	RhythmDBEntry *ret;
	gsize size = sizeof (RhythmDBEntry);

	if (type->entry_type_data_size) {
		size = ALIGN_STRUCT (sizeof (RhythmDBEntry)) + type->entry_type_data_size;
	}
	ret = g_malloc0 (size);
	ret->id = (guint) g_atomic_int_exchange_and_add (&db->priv->next_entry_id, 1);

	ret->type = type;
	ret->title = rb_refstring_ref (db->priv->empty_string);
	ret->genre = rb_refstring_ref (db->priv->empty_string);
	ret->artist = rb_refstring_ref (db->priv->empty_string);
	ret->album = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_trackid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_artistid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_albumid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_albumartistid = rb_refstring_ref (db->priv->empty_string);
	ret->artist_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->album_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->mimetype = rb_refstring_ref (db->priv->octet_stream_str);

	ret->flags |= RHYTHMDB_ENTRY_LAST_PLAYED_DIRTY |
		      RHYTHMDB_ENTRY_FIRST_SEEN_DIRTY |
		      RHYTHMDB_ENTRY_LAST_SEEN_DIRTY;

	/* The refcount is initially 0, we want to set it to 1 */
	ret->refcount = 1;

	if (type->post_entry_create)
		(type->post_entry_create)(ret, type->post_entry_create_data);

	return ret;
}

/**
 * rhythmdb_entry_get_type_data:
 * @entry: a #RhythmDBEntry
 * @expected_size: expected size of the type-specific data.
 *
 * Retrieves a pointer to the entry's type-specific data, checking that
 * the size of the data structure matches what is expected.
 * Callers should use the RHYTHMDB_ENTRY_GET_TYPE_DATA macro for
 * a slightly more friendly interface to this functionality.
 *
 * Return value: type-specific data pointer
 */
gpointer
rhythmdb_entry_get_type_data (RhythmDBEntry *entry,
			      guint expected_size)
{
	g_return_val_if_fail (entry != NULL, NULL);

	g_assert (expected_size == entry->type->entry_type_data_size);
	gsize offset = ALIGN_STRUCT (sizeof (RhythmDBEntry));

	return (gpointer) (((guint8 *)entry) + offset);
}

/**
 * rhythmdb_entry_insert:
 * @db: a #RhythmDB.
 * @entry: the entry to insert.
 *
 * Inserts a newly-created entry into the database.
 *
 * Note that you must call rhythmdb_commit() at some point after invoking
 * this function.
 **/
void
rhythmdb_entry_insert (RhythmDB *db,
		       RhythmDBEntry *entry)
{
	g_return_if_fail (RHYTHMDB_IS (db));
	g_return_if_fail (entry != NULL);

	g_assert ((entry->flags & RHYTHMDB_ENTRY_INSERTED) == 0);
	g_return_if_fail (entry->location != NULL);

	/* ref the entry before adding to hash, it is unreffed when removed */
	rhythmdb_entry_ref (entry);
	g_mutex_lock (db->priv->change_mutex);
	g_hash_table_insert (db->priv->added_entries, entry, g_thread_self ());
	g_mutex_unlock (db->priv->change_mutex);
}

/**
 * rhythmdb_entry_new:
 * @db: a #RhythmDB.
 * @type: type of entry to create
 * @uri: the location of the entry, this be unique amongst all entries.
 *
 * Creates a new entry of type @type and location @uri, and inserts
 * it into the database. You must call rhythmdb_commit() at some  point
 * after invoking this function.
 *
 * This may return NULL if entry creation fails. This can occur if there is
 * already an entry with the given uri.
 *
 * Returns: the newly created #RhythmDBEntry
 **/
RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db,
		    RhythmDBEntryType type,
		    const char *uri)
{
	RhythmDBEntry *ret;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	ret = rhythmdb_entry_lookup_by_location (db, uri);
	if (ret) {
		g_warning ("attempting to create entry that already exists: %s", uri);
		return NULL;
	}

	ret = rhythmdb_entry_allocate (db, type);
	ret->location = rb_refstring_new (uri);
	klass->impl_entry_new (db, ret);
	rb_debug ("emitting entry added");
	rhythmdb_entry_insert (db, ret);

	return ret;
}

/**
 * rhythmdb_entry_example_new:
 * @db: a #RhythmDB.
 * @type: type of entry to create
 * @uri: the location of the entry, this be unique amongst all entries.
 *
 * Creates a new sample entry of type @type and location @uri, it does not insert
 * it into the database.  This is indended for use as a example entry.
 *
 * This may return NULL if entry creation fails.
 *
 * Returns: the newly created #RhythmDBEntry
 **/
RhythmDBEntry *
rhythmdb_entry_example_new (RhythmDB *db,
			    RhythmDBEntryType type,
			    const char *uri)
{
	RhythmDBEntry *ret;

	ret = rhythmdb_entry_allocate (db, type);
	if (uri)
		ret->location = rb_refstring_new (uri);

	if (type == RHYTHMDB_ENTRY_TYPE_SONG) {
		rb_refstring_unref (ret->artist);
		/* Translators: this is an example artist name.  It should
		 * not be translated literally, but could be replaced with 
		 * a local artist name if desired.  Ensure the album name
		 * and song title are also replaced in this case.
		 */
		ret->artist = rb_refstring_new (_("The Beatles"));
		rb_refstring_unref (ret->album);
		/* Translators: this is an example album name.  If the
		 * example artist name is localised, this should be replaced
		 * with the name of an album by that artist.
		 */
		ret->album = rb_refstring_new (_("Help!"));
		rb_refstring_unref (ret->title);
		/* Translators: this is an example song title.  If the example
		 * artist and album names are localised, this should be replaced
		 * with the name of the seventh song from the localised album.
		 */
		ret->title = rb_refstring_new (_("Ticket To Ride"));
		ret->tracknum = 7;
	} else {
	}

	return ret;
}

/**
 * rhythmdb_entry_ref:
 * @entry: a #RhythmDBEntry.
 *
 * Increase the reference count of the entry.
 *
 * Returns: the entry
 **/
RhythmDBEntry *
rhythmdb_entry_ref (RhythmDBEntry *entry)
{
	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (entry->refcount > 0, NULL);

	g_atomic_int_inc (&entry->refcount);

	return entry;
}

static void
rhythmdb_entry_finalize (RhythmDBEntry *entry)
{
	RhythmDBEntryType type;

	type = rhythmdb_entry_get_entry_type (entry);

	if (type->pre_entry_destroy)
		(type->pre_entry_destroy)(entry, type->pre_entry_destroy_data);

	rb_refstring_unref (entry->location);
	rb_refstring_unref (entry->playback_error);
	rb_refstring_unref (entry->title);
	rb_refstring_unref (entry->genre);
	rb_refstring_unref (entry->artist);
	rb_refstring_unref (entry->album);
	rb_refstring_unref (entry->musicbrainz_trackid);
	rb_refstring_unref (entry->musicbrainz_artistid);
	rb_refstring_unref (entry->musicbrainz_albumid);
	rb_refstring_unref (entry->musicbrainz_albumartistid);
	rb_refstring_unref (entry->artist_sortname);
	rb_refstring_unref (entry->album_sortname);
	rb_refstring_unref (entry->mimetype);

	g_free (entry);
}

/**
 * rhythmdb_entry_unref:
 * @entry: a #RhythmDBEntry.
 *
 * Decrease the reference count of the entry, and destroy it if there are
 * no references left.
 **/
void
rhythmdb_entry_unref (RhythmDBEntry *entry)
{
	gboolean is_zero;

	g_return_if_fail (entry != NULL);
	g_return_if_fail (entry->refcount > 0);

	is_zero = g_atomic_int_dec_and_test (&entry->refcount);
	if (G_UNLIKELY (is_zero)) {
		rhythmdb_entry_finalize (entry);
	}
}

/**
 * rhythmdb_entry_is_editable:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 *
 * This determines whether any changes to the entries metadata can be saved.
 * Usually this is only true for entries backed by files, where tag-writing is
 * enabled, and the appropriate tag-writing facilities are available.
 *
 * Returns: whether the entries metadata can be changed.
 **/

gboolean
rhythmdb_entry_is_editable (RhythmDB *db,
			    RhythmDBEntry *entry)
{
	RhythmDBEntryType entry_type;

	g_return_val_if_fail (RHYTHMDB_IS (db), FALSE);
	g_return_val_if_fail (entry != NULL, FALSE);

	entry_type = rhythmdb_entry_get_entry_type (entry);
	return entry_type->can_sync_metadata (db, entry, entry_type->can_sync_metadata_data);
}

static void
set_metadata_string_default_unknown (RhythmDB *db,
				     RBMetaData *metadata,
				     RhythmDBEntry *entry,
				     RBMetaDataField field,
				     RhythmDBPropType prop)
{
	const char *unknown = _("Unknown");
	GValue val = {0, };

	if (!(rb_metadata_get (metadata,
			       field,
			       &val))) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_static_string (&val, unknown);
	} else {
                const gchar *str = g_value_get_string (&val);
                if (str == NULL || str[0] == '\0')
	        	g_value_set_static_string (&val, unknown);
        }
	rhythmdb_entry_set_internal (db, entry, TRUE, prop, &val);
	g_value_unset (&val);
}

static void
set_props_from_metadata (RhythmDB *db,
			 RhythmDBEntry *entry,
			 GFileInfo *fileinfo,
			 RBMetaData *metadata)
{
	const char *mime;
	GValue val = {0,};

	g_value_init (&val, G_TYPE_STRING);
	mime = rb_metadata_get_mime (metadata);
	if (mime) {
		g_value_set_string (&val, mime);
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MIMETYPE, &val);
	}
	g_value_unset (&val);

	/* track number */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_TRACK_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
	}
	rhythmdb_entry_set_internal (db, entry, TRUE,
				     RHYTHMDB_PROP_TRACK_NUMBER, &val);
	g_value_unset (&val);

	/* disc number */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_DISC_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
	}
	rhythmdb_entry_set_internal (db, entry, TRUE,
				     RHYTHMDB_PROP_DISC_NUMBER, &val);
	g_value_unset (&val);

	/* duration */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_DURATION,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	/* bitrate */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_BITRATE,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_BITRATE, &val);
		g_value_unset (&val);
	}

	/* date */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_DATE,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_DATE, &val);
		g_value_unset (&val);
	}

	/* musicbrainz trackid */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_MUSICBRAINZ_TRACKID,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, &val);
		g_value_unset (&val);
	}

	/* musicbrainz artistid */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, &val);
		g_value_unset (&val);
	}

	/* musicbrainz albumid */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, &val);
		g_value_unset (&val);
	}

	/* musicbrainz albumartistid */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, &val);
		g_value_unset (&val);
	}

	/* artist sortname */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_ARTIST_SORTNAME,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_ARTIST_SORTNAME, &val);
		g_value_unset (&val);
	}

	/* album sortname */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_ALBUM_SORTNAME,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_ALBUM_SORTNAME, &val);
		g_value_unset (&val);
	}

	/* filesize */
	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, g_file_info_get_attribute_uint64 (fileinfo, G_FILE_ATTRIBUTE_STANDARD_SIZE));
	rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	/* title */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_TITLE,
			      &val) || g_value_get_string (&val)[0] == '\0') {
		const char *fname;
		fname = g_file_info_get_display_name (fileinfo);
		if (G_VALUE_HOLDS_STRING (&val))
			g_value_reset (&val);
		else
			g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, fname);
	}
	rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	/* genre */
	set_metadata_string_default_unknown (db, metadata, entry,
					     RB_METADATA_FIELD_GENRE,
					     RHYTHMDB_PROP_GENRE);

	/* artist */
	set_metadata_string_default_unknown (db, metadata, entry,
					     RB_METADATA_FIELD_ARTIST,
					     RHYTHMDB_PROP_ARTIST);
	/* album */
	set_metadata_string_default_unknown (db, metadata, entry,
					     RB_METADATA_FIELD_ALBUM,
					     RHYTHMDB_PROP_ALBUM);

	/* replaygain track gain */
        if (rb_metadata_get (metadata,
                             RB_METADATA_FIELD_TRACK_GAIN,
                             &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_TRACK_GAIN, &val);
		g_value_unset (&val);
	}

	/* replaygain track peak */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_TRACK_PEAK,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_TRACK_PEAK, &val);
		g_value_unset (&val);
	}

	/* replaygain album gain */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_ALBUM_GAIN,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_ALBUM_GAIN, &val);
		g_value_unset (&val);
	}

	/* replaygain album peak */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_ALBUM_PEAK,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_ALBUM_PEAK, &val);
		g_value_unset (&val);
	}
}

static gboolean
is_ghost_entry (RhythmDBEntry *entry)
{
	GTimeVal time;
	gulong last_seen;
	gulong grace_period;
	GError *error;
	GConfClient *client;

	client = gconf_client_get_default ();
	if (client == NULL) {
		return FALSE;
	}
	error = NULL;
	grace_period = gconf_client_get_int (client, CONF_GRACE_PERIOD,
					     &error);
	g_object_unref (G_OBJECT (client));
	if (error != NULL) {
		g_error_free (error);
		return FALSE;
	}

	/* This is a bit silly, but I prefer to make sure we won't
	 * overflow in the following calculations
	 */
	if ((grace_period <= 0) || (grace_period > 20000)) {
		return FALSE;
	}

	/* Convert from days to seconds */
	grace_period = grace_period * 60 * 60 * 24;
	g_get_current_time (&time);
	last_seen = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_LAST_SEEN);

	return (last_seen + grace_period < time.tv_sec);
}

static void
rhythmdb_process_stat_event (RhythmDB *db,
			     RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	RhythmDBAction *action;
	GFileType file_type;
	
	if (event->entry != NULL) {
		entry = event->entry;
	} else {
		entry = rhythmdb_entry_lookup_by_location_refstring (db, event->real_uri);
	}

	/* handle errors:
	 * - if a non-ignore entry exists, process ghostliness (hide/delete)
	 * - otherwise, create an import error entry?  hmm.
	 */
	if (event->error) {
		if (entry != NULL) {
			if (!is_ghost_entry (entry)) {
				rhythmdb_entry_set_visibility (db, entry, FALSE);
			} else {
				rb_debug ("error accessing %s: %s", rb_refstring_get (event->real_uri),
					  event->error->message);
				rhythmdb_entry_delete (db, entry);
			}
			rhythmdb_commit (db);
		} else {
			/* erm.. */
		}

		return;
	}

	g_assert (event->file_info != NULL);

	/* figure out what to do based on the file type */
	file_type = g_file_info_get_attribute_uint32 (event->file_info,
						      G_FILE_ATTRIBUTE_STANDARD_TYPE);
	switch (file_type) {
	case G_FILE_TYPE_UNKNOWN:
	case G_FILE_TYPE_REGULAR:
		if (entry != NULL) {
			GValue val = {0, };
			GTimeVal time;
			guint64 new_mtime;
			guint64 new_size;

			/* update the existing entry, as long as the entry type matches */
			if ((event->entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) && (entry->type != event->entry_type))
				g_warning ("attempt to use same location in multiple entry types");

			if (entry->type == event->ignore_type)
				rb_debug ("ignoring %p", entry);

			rhythmdb_entry_set_visibility (db, entry, TRUE);

			/* Update last seen time. It will also be updated
			 * upon saving and when a volume is unmounted.
			 */
			g_get_current_time (&time);
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, time.tv_sec);
			rhythmdb_entry_set_internal (db, entry, TRUE,
						     RHYTHMDB_PROP_LAST_SEEN,
						     &val);
			g_value_unset (&val);

			/* compare modification time and size to the values in the database.
			 * if either has changed, we'll re-read the file.
			 */
			new_mtime = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
			new_size = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
			if (entry->mtime == new_mtime && (new_size == 0 || entry->file_size == new_size)) {
				rb_debug ("not modified: %s", rb_refstring_get (event->real_uri));
			} else {
				RhythmDBEvent *new_event;

				rb_debug ("changed: %s", rb_refstring_get (event->real_uri));
				new_event = g_slice_new0 (RhythmDBEvent);
				new_event->db = db;
				new_event->uri = rb_refstring_ref (event->real_uri);
				new_event->type = RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED;
				rhythmdb_push_event (db, new_event);
			}
		} else {
			/* push a LOAD action */
			action = g_slice_new0 (RhythmDBAction);
			action->type = RHYTHMDB_ACTION_LOAD;
			action->uri = rb_refstring_ref (event->real_uri);
			action->entry_type = event->entry_type;
			action->ignore_type = event->ignore_type;
			action->error_type = event->error_type;
			rb_debug ("queuing a RHYTHMDB_ACTION_LOAD: %s", rb_refstring_get (action->uri));
			g_async_queue_push (db->priv->action_queue, action);
		}
		break;

	case G_FILE_TYPE_DIRECTORY:
		rb_debug ("processing directory %s", rb_refstring_get (event->real_uri));
		/* push an ENUM_DIR action */
		action = g_slice_new0 (RhythmDBAction);
		action->type = RHYTHMDB_ACTION_ENUM_DIR;
		action->uri = rb_refstring_ref (event->real_uri);
		action->entry_type = event->entry_type;
		action->ignore_type = event->ignore_type;
		action->error_type = event->error_type;
		rb_debug ("queuing a RHYTHMDB_ACTION_ENUM_DIR: %s", rb_refstring_get (action->uri));
		g_async_queue_push (db->priv->action_queue, action);
		break;

	case G_FILE_TYPE_SYMBOLIC_LINK:
	case G_FILE_TYPE_SHORTCUT:
		/* this shouldn't happen, but maybe we should handle it anyway? */
		rb_debug ("ignoring stat results for %s: is link", rb_refstring_get (event->real_uri));
		break;

	case G_FILE_TYPE_SPECIAL:
	case G_FILE_TYPE_MOUNTABLE:		/* hmm. */
		rb_debug ("ignoring stat results for %s: is special", rb_refstring_get (event->real_uri));
		break;
	}

	rhythmdb_commit (db);
}

typedef struct
{
	RhythmDB *db;
	char *uri;
	char *msg;
} RhythmDBLoadErrorData;

static void
rhythmdb_add_import_error_entry (RhythmDB *db,
				 RhythmDBEvent *event,
				 RhythmDBEntryType error_entry_type)
{
	RhythmDBEntry *entry;
	GValue value = {0,};

	rb_debug ("adding import error for %s: %s", rb_refstring_get (event->real_uri), event->error ? event->error->message : "<no error>");
	if (error_entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		/* we don't have an error entry type, so we can't add an import error */
		return;
	}

	entry = rhythmdb_entry_lookup_by_location_refstring (db, event->real_uri);
	if (entry) {
		RhythmDBEntryType entry_type = rhythmdb_entry_get_entry_type (entry);
		if (entry_type != event->error_type &&
		    entry_type != event->ignore_type) {
			/* FIXME we've successfully read this file before.. so what should we do? */
			rb_debug ("%s already exists in the library.. ignoring import error?", rb_refstring_get (event->real_uri));
			return;
		}

		if (entry_type != error_entry_type) {
			/* delete the existing entry, then create a new one below */
			rhythmdb_entry_delete (db, entry);
			entry = NULL;
		} else if (error_entry_type == event->error_type) {
			/* we've already got an error for this file, so just update it */
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, event->error->message);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
			g_value_unset (&value);
		} else {
			/* no need to update the ignored file entry */
		}

		if (entry && event->file_info) {
			/* mtime */
			guint64 new_mtime = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, new_mtime);		/* hmm, cast */
			rhythmdb_entry_set(db, entry, RHYTHMDB_PROP_MTIME, &value);
			g_value_unset (&value);
		}

		rhythmdb_add_timeout_commit (db, FALSE);
	}

	if (entry == NULL) {
		/* create a new import error or ignore entry */
		entry = rhythmdb_entry_new (db, error_entry_type, rb_refstring_get (event->real_uri));
		if (entry == NULL)
			return;

		if (error_entry_type == event->error_type && event->error->message) {
			g_value_init (&value, G_TYPE_STRING);
			if (g_utf8_validate (event->error->message, -1, NULL))
				g_value_set_string (&value, event->error->message);
			else
				g_value_set_static_string (&value, _("invalid unicode in error message"));
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
			g_value_unset (&value);
		}

		/* mtime */
		if (event->file_info) {
			guint64 new_mtime = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, new_mtime);		/* hmm, cast */
			rhythmdb_entry_set(db, entry, RHYTHMDB_PROP_MTIME, &value);
			g_value_unset (&value);
		}

		/* record the mount point so we can delete entries for unmounted volumes */
		rhythmdb_entry_set_mount_point (db, entry, rb_refstring_get (event->real_uri));

		rhythmdb_entry_set_visibility (db, entry, TRUE);

		rhythmdb_add_timeout_commit (db, FALSE);
	}
}

static gboolean
rhythmdb_process_metadata_load_real (RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	GValue value = {0,};
	GTimeVal time;
	const char *media_type;

	if (event->entry_type == RHYTHMDB_ENTRY_TYPE_INVALID)
		event->entry_type = RHYTHMDB_ENTRY_TYPE_SONG;

	/*
	 * always ignore anything with video in it, or anything
	 * matching one of the media types we don't care about.
	 * if we can identify it that much, we know it's not interesting.
	 * otherwise, add an import error entry if there was an error,
	 * or just ignore it if it doesn't contain audio.
	 */

	media_type = rb_metadata_get_mime (event->metadata);
	if (rb_metadata_has_video (event->metadata) ||
	    (media_type != NULL && rhythmdb_ignore_media_type (media_type))) {
		rhythmdb_add_import_error_entry (event->db, event, event->ignore_type);
		return TRUE;
	}

	/* also ignore really small files we can't identify */
	if (event->error && event->error->code == RB_METADATA_ERROR_UNRECOGNIZED) {
		guint64 file_size;

		file_size = g_file_info_get_attribute_uint64 (event->file_info,
							      G_FILE_ATTRIBUTE_STANDARD_SIZE);
		if (file_size == 0) {
			/* except for empty files */
			g_clear_error (&event->error);
			g_set_error (&event->error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_EMPTY_FILE,
				     _("Empty file"));
		} else if (file_size < REALLY_SMALL_FILE_SIZE) {
			rhythmdb_add_import_error_entry (event->db, event, event->ignore_type);
			return TRUE;
		}
	}

	if (event->error) {
		rhythmdb_add_import_error_entry (event->db, event, event->error_type);
		return TRUE;
	}

	/* check if this is something we want in the library */
	if (rb_metadata_has_audio (event->metadata) == FALSE) {
		rhythmdb_add_import_error_entry (event->db, event, event->ignore_type);
		return TRUE;
	}

	g_get_current_time (&time);

	entry = rhythmdb_entry_lookup_by_location_refstring (event->db, event->real_uri);

	if (entry != NULL) {
		if (rhythmdb_entry_get_entry_type (entry) != event->entry_type) {
			/* switching from IGNORE to SONG or vice versa, recreate the entry */
			rhythmdb_entry_delete (event->db, entry);
			rhythmdb_add_timeout_commit (event->db, FALSE);
			entry = NULL;
		}
	}

	if (entry == NULL) {

		entry = rhythmdb_entry_new (event->db, event->entry_type, rb_refstring_get (event->real_uri));
		if (entry == NULL) {
			rb_debug ("entry already exists");
			return TRUE;
		}

		/* initialize the last played date to 0=never */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, 0);
		rhythmdb_entry_set (event->db, entry,
				    RHYTHMDB_PROP_LAST_PLAYED, &value);
		g_value_unset (&value);

		/* initialize the rating */
		g_value_init (&value, G_TYPE_DOUBLE);
		g_value_set_double (&value, 0);
		rhythmdb_entry_set (event->db, entry, RHYTHMDB_PROP_RATING, &value);
		g_value_unset (&value);

	        /* first seen */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, time.tv_sec);
		rhythmdb_entry_set (event->db, entry, RHYTHMDB_PROP_FIRST_SEEN, &value);
		g_value_unset (&value);
	}

	if ((event->entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) && (entry->type != event->entry_type))
		g_warning ("attempt to use same location in multiple entry types");

	/* mtime */
	if (event->file_info) {
		guint64 mtime;

		mtime = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, (gulong)mtime);
		rhythmdb_entry_set_internal (event->db, entry, TRUE, RHYTHMDB_PROP_MTIME, &value);
		g_value_unset (&value);
	}

	if (event->entry_type != event->ignore_type &&
	    event->entry_type != event->error_type) {
		set_props_from_metadata (event->db, entry, event->file_info, event->metadata);
	}

	/* we've seen this entry */
	rhythmdb_entry_set_visibility (event->db, entry, TRUE);

	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, time.tv_sec);
	rhythmdb_entry_set_internal (event->db, entry, TRUE, RHYTHMDB_PROP_LAST_SEEN, &value);
	g_value_unset (&value);

	/* Remember the mount point of the volume the song is on */
	rhythmdb_entry_set_mount_point (event->db, entry, rb_refstring_get (event->real_uri));

	/* monitor the file for changes */
	/* FIXME: watch for errors */
	if (eel_gconf_get_boolean (CONF_MONITOR_LIBRARY) && event->entry_type == RHYTHMDB_ENTRY_TYPE_SONG)
		rhythmdb_monitor_uri_path (event->db, rb_refstring_get (entry->location), NULL);

	rhythmdb_add_timeout_commit (event->db, FALSE);

	return TRUE;
}

static void
set_missing_plugin_error (RhythmDBEvent *event)
{
	char **missing_plugins;
	char **plugin_descriptions;
	char *list;

	g_clear_error (&event->error);

	rb_metadata_get_missing_plugins (event->metadata, &missing_plugins, &plugin_descriptions);
	list = g_strjoinv (", ", plugin_descriptions);
	/* Translators: the parameter here is a list of GStreamer plugins.
	 * The plugin names are already translated.
	 */
	g_set_error (&event->error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_MISSING_PLUGIN,
		     _("Additional GStreamer plugins are required to play this file: %s"),
		     list);
	g_free (list);
	g_strfreev (missing_plugins);
	g_strfreev (plugin_descriptions);
}

static void
rhythmdb_missing_plugins_cb (gpointer duh, gboolean should_retry, RhythmDBEvent *event)
{
	rb_debug ("missing-plugin retry closure called: event %p, retry %d", event, should_retry);

	if (should_retry) {
		RhythmDBAction *load_action;

		rb_debug ("retrying RHYTHMDB_ACTION_LOAD for %s", rb_refstring_get (event->real_uri));
		load_action = g_slice_new0 (RhythmDBAction);
		load_action->type = RHYTHMDB_ACTION_LOAD;
		load_action->uri = rb_refstring_ref (event->real_uri);
		load_action->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		load_action->ignore_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		load_action->error_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		g_async_queue_push (event->db->priv->action_queue, load_action);
	} else {
		/* plugin installation failed or was cancelled, so add an import error for the file */
		rb_debug ("not retrying RHYTHMDB_ACTION_LOAD for %s", rb_refstring_get (event->real_uri));
		set_missing_plugin_error (event);
		rhythmdb_process_metadata_load_real (event);
	}
}

static void
rhythmdb_missing_plugin_event_cleanup (RhythmDBEvent *event)
{
	rb_debug ("cleaning up missing plugin event %p", event);

	event->db->priv->metadata_blocked = FALSE;
	g_cond_signal (event->db->priv->metadata_cond);

	g_mutex_unlock (event->db->priv->metadata_lock);
	rhythmdb_event_free (event->db, event);
}

static gboolean
rhythmdb_process_metadata_load (RhythmDB *db,
				RhythmDBEvent *event)
{
	/* only process missing plugins for audio files */
	if (event->metadata != NULL &&
	    rb_metadata_has_audio (event->metadata) == TRUE &&
	    rb_metadata_has_video (event->metadata) == FALSE &&
	    rb_metadata_has_missing_plugins (event->metadata) == TRUE) {
		char **missing_plugins;
		char **plugin_descriptions;
		GClosure *closure;
		gboolean processing;

		rb_metadata_get_missing_plugins (event->metadata, &missing_plugins, &plugin_descriptions);
		
		rb_debug ("missing plugins during metadata load for %s", rb_refstring_get (event->real_uri));

		g_mutex_lock (event->db->priv->metadata_lock);

		closure = g_cclosure_new ((GCallback) rhythmdb_missing_plugins_cb,
					  event,
					  (GClosureNotify) rhythmdb_missing_plugin_event_cleanup);
		g_closure_set_marshal (closure, g_cclosure_marshal_VOID__BOOLEAN);
		g_signal_emit (db, rhythmdb_signals[MISSING_PLUGINS], 0, missing_plugins, plugin_descriptions, closure, &processing);
		if (processing) {
			rb_debug ("processing missing plugins");
		} else {
			/* not installing plugins because the requested plugins are blacklisted,
			 * so just add an import error for the file.
			 */
			set_missing_plugin_error (event);
			rhythmdb_process_metadata_load_real (event);
		}

		g_closure_sink (closure);
		return FALSE;
	} else if (rb_metadata_has_missing_plugins (event->metadata)) {
		rb_debug ("ignoring missing plugins for %s; not audio (%d %d %d)",
			  rb_refstring_get (event->real_uri),
			  rb_metadata_has_audio (event->metadata),
			  rb_metadata_has_video (event->metadata),
			  rb_metadata_has_other_data (event->metadata));

		g_mutex_lock (db->priv->metadata_lock);
		db->priv->metadata_blocked = FALSE;
		g_cond_signal (db->priv->metadata_cond);
		g_mutex_unlock (db->priv->metadata_lock);
	}

	return rhythmdb_process_metadata_load_real (event);
}


static void
rhythmdb_process_queued_entry_set_event (RhythmDB *db,
					 RhythmDBEvent *event)
{
	rhythmdb_entry_set_internal (db, event->entry,
				     event->signal_change,
				     event->change.prop,
				     &event->change.new);
	/* Don't run rhythmdb_commit right now in case there
	 * we can run a single commit for several queued
	 * entry_set
	 */
	rhythmdb_add_timeout_commit (db, TRUE);
}

static void
rhythmdb_process_file_created_or_modified (RhythmDB *db,
					   RhythmDBEvent *event)
{
	RhythmDBAction *action;

	action = g_slice_new0 (RhythmDBAction);
	action->type = RHYTHMDB_ACTION_LOAD;
	action->uri = rb_refstring_ref (event->uri);
	action->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;
	action->ignore_type = RHYTHMDB_ENTRY_TYPE_IGNORE;
	action->error_type = RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR;
	g_async_queue_push (db->priv->action_queue, action);
}

static void
rhythmdb_process_file_deleted (RhythmDB *db,
			       RhythmDBEvent *event)
{
	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location_refstring (db, event->uri);

	g_hash_table_remove (db->priv->changed_files, event->uri);

	if (entry) {
		rb_debug ("deleting entry for %s", rb_refstring_get (event->uri));
		rhythmdb_entry_set_visibility (db, entry, FALSE);
		rhythmdb_commit (db);
	}
}

static void
rhythmdb_process_one_event (RhythmDBEvent *event, RhythmDB *db)
{
	gboolean free = TRUE;

	/* if the database is read-only, we can't process those events
	 * since they call rhythmdb_entry_set. Doing it this way
	 * is safe if we assume all calls to read_enter/read_leave
	 * are done from the main thread (the thread this function
	 * runs in).
	 */
	if (rhythmdb_get_readonly (db) &&
	    ((event->type == RHYTHMDB_EVENT_STAT)
	     || (event->type == RHYTHMDB_EVENT_METADATA_LOAD)
	     || (event->type == RHYTHMDB_EVENT_ENTRY_SET))) {
		rb_debug ("Database is read-only, delaying event processing");
		g_async_queue_push (db->priv->delayed_write_queue, event);
		return;
	}

	switch (event->type) {
	case RHYTHMDB_EVENT_STAT:
		rb_debug ("processing RHYTHMDB_EVENT_STAT");
		rhythmdb_process_stat_event (db, event);
		break;
	case RHYTHMDB_EVENT_METADATA_LOAD:
		rb_debug ("processing RHYTHMDB_EVENT_METADATA_LOAD");
		free = rhythmdb_process_metadata_load (db, event);
		break;
	case RHYTHMDB_EVENT_ENTRY_SET:
		rb_debug ("processing RHYTHMDB_EVENT_ENTRY_SET");
		rhythmdb_process_queued_entry_set_event (db, event);
		break;
	case RHYTHMDB_EVENT_DB_LOAD:
		rb_debug ("processing RHYTHMDB_EVENT_DB_LOAD");
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[LOAD_COMPLETE], 0);

		/* save the db every five minutes */
		if (db->priv->save_timeout_id > 0) {
			g_source_remove (db->priv->save_timeout_id);
		}
		db->priv->save_timeout_id = g_timeout_add_seconds_full (G_PRIORITY_LOW,
									5 * 60,
									(GSourceFunc) rhythmdb_idle_save,
									db,
									NULL);
		break;
	case RHYTHMDB_EVENT_THREAD_EXITED:
		rb_debug ("processing RHYTHMDB_EVENT_THREAD_EXITED");
		break;
	case RHYTHMDB_EVENT_DB_SAVED:
		rb_debug ("processing RHYTHMDB_EVENT_DB_SAVED");
		rhythmdb_read_leave (db);
		break;
	case RHYTHMDB_EVENT_QUERY_COMPLETE:
		rb_debug ("processing RHYTHMDB_EVENT_QUERY_COMPLETE");
		rhythmdb_read_leave (db);
		break;
	case RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED:
		rb_debug ("processing RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED");
		rhythmdb_process_file_created_or_modified (db, event);
		break;
	case RHYTHMDB_EVENT_FILE_DELETED:
		rb_debug ("processing RHYTHMDB_EVENT_FILE_DELETED");
		rhythmdb_process_file_deleted (db, event);
		break;
	}
	if (free)
		rhythmdb_event_free (db, event);
}


static void
rhythmdb_file_info_query (RhythmDB *db, GFile *file, RhythmDBEvent *event)
{
	event->file_info = g_file_query_info (file,
					      RHYTHMDB_FILE_INFO_ATTRIBUTES,
					      G_FILE_QUERY_INFO_NONE,
					      db->priv->exiting,
					      &event->error);
}

static void
wrap_access_failed_error (RhythmDBEvent *event)
{
	GError *wrapped;

	wrapped = make_access_failed_error (rb_refstring_get (event->real_uri), event->error);
	g_error_free (event->error);
	event->error = wrapped;
}

static void
rhythmdb_execute_stat_mount_ready_cb (GObject *source, GAsyncResult *result, RhythmDBEvent *event)
{
	GError *error = NULL;
	
	g_file_mount_enclosing_volume_finish (G_FILE (source), result, &error);
	if (error != NULL) {
		event->error = make_access_failed_error (rb_refstring_get (event->real_uri), error);
		g_print ("not doing file info query; error %s\n", error->message);
		g_error_free (error);

		g_object_unref (event->file_info);
		event->file_info = NULL;
	} else {
		g_print ("retrying file info query after mount completed\n");
		rhythmdb_file_info_query (event->db, G_FILE (source), event);
	}

	g_mutex_lock (event->db->priv->stat_mutex);
	event->db->priv->outstanding_stats = g_list_remove (event->db->priv->outstanding_stats, event);
	g_mutex_unlock (event->db->priv->stat_mutex);

	g_object_unref (source);
	rhythmdb_push_event (event->db, event);
}


static void
rhythmdb_execute_stat (RhythmDB *db,
		       const char *uri,
		       RhythmDBEvent *event)
{
	GFile *file;

	event->real_uri = rb_refstring_new (uri);
	file = g_file_new_for_uri (uri);
	
	g_mutex_lock (db->priv->stat_mutex);
	db->priv->outstanding_stats = g_list_prepend (db->priv->outstanding_stats, event);
	g_mutex_unlock (db->priv->stat_mutex);

	rhythmdb_file_info_query (db, file, event);

	if (event->error != NULL) {
		/* if we can't get at it because the location isn't mounted, mount it and try again */
	       	if (g_error_matches (event->error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
			GMountOperation *mount_op = NULL;

			g_error_free (event->error);
			event->error = NULL;

			g_signal_emit (G_OBJECT (event->db), rhythmdb_signals[CREATE_MOUNT_OP], 0, &mount_op);
			if (mount_op != NULL) {
				g_print ("created mount op %p\n", mount_op);
				g_file_mount_enclosing_volume (file,
							       G_MOUNT_MOUNT_NONE,
							       mount_op,
							       event->db->priv->exiting,
							       (GAsyncReadyCallback)rhythmdb_execute_stat_mount_ready_cb,
							       event);
				return;
			}
		}

		/* if it's some other error, or we couldn't attempt to mount the location, report the error */
		wrap_access_failed_error (event);

		if (event->file_info != NULL) {
			g_object_unref (event->file_info);
			event->file_info = NULL;
		}
	}
       
	/* either way, we're done now */

	g_mutex_lock (event->db->priv->stat_mutex);
	event->db->priv->outstanding_stats = g_list_remove (event->db->priv->outstanding_stats, event);
	g_mutex_unlock (event->db->priv->stat_mutex);
	
	rhythmdb_push_event (event->db, event);
	g_object_unref (file);
}

static void
rhythmdb_execute_load (RhythmDB *db,
		       const char *uri,
		       RhythmDBEvent *event)
{
	GError *error = NULL;
	char *resolved;

	resolved = rb_uri_resolve_symlink (uri, &error);
	if (resolved != NULL) {
		GFile *file;
	
		file = g_file_new_for_uri (uri);
		event->file_info = g_file_query_info (file,
						      RHYTHMDB_FILE_INFO_ATTRIBUTES,
						      G_FILE_QUERY_INFO_NONE,
						      NULL,
						      &error);
		event->real_uri = rb_refstring_new (resolved);

		g_free (resolved);
		g_object_unref (file);
	} else {
		event->real_uri = rb_refstring_new (uri);
	}

	if (error != NULL) {
		event->error = make_access_failed_error (uri, error);
		if (event->file_info) {
			g_object_unref (event->file_info);
			event->file_info = NULL;
		}
	} else if (event->type == RHYTHMDB_EVENT_METADATA_LOAD) {
		g_mutex_lock (event->db->priv->metadata_lock);
		while (event->db->priv->metadata_blocked) {
			g_cond_wait (event->db->priv->metadata_cond, event->db->priv->metadata_lock);
		}

		event->metadata = rb_metadata_new ();
		rb_metadata_load (event->metadata,
				  rb_refstring_get (event->real_uri),
				  &event->error);

		/* if we're missing some plugins, block further attempts to
		 * read metadata until we've processed them.
		 */
		if (rb_metadata_has_missing_plugins (event->metadata)) {
			event->db->priv->metadata_blocked = TRUE;
		}

		g_mutex_unlock (event->db->priv->metadata_lock);
	}

	rhythmdb_push_event (db, event);
}

static void
rhythmdb_execute_enum_dir (RhythmDB *db,
			   RhythmDBAction *action)
{
	GFile *dir;
	GFileEnumerator *dir_enum;
	GError *error = NULL;

	dir = g_file_new_for_uri (rb_refstring_get (action->uri));
	dir_enum = g_file_enumerate_children (dir,
					      RHYTHMDB_FILE_CHILD_INFO_ATTRIBUTES,
					      G_FILE_QUERY_INFO_NONE,
					      db->priv->exiting,
					      &error);
	if (error != NULL) {
		/* don't need to worry about mounting here, as the mount should have
		 * occurred on the stat.
		 */

		/* um.. what now? */
		rb_debug ("unable to enumerate children of %s: %s",
			  rb_refstring_get (action->uri),
			  error->message);
		g_error_free (error);
		g_object_unref (dir);
		return;
	}

	while (1) {
		RhythmDBEvent *result;
		GFileInfo *file_info;
		GFile *child;
		char *child_uri;

		file_info = g_file_enumerator_next_file (dir_enum, db->priv->exiting, &error);
		if (file_info == NULL) {
			if (error == NULL) {
				/* done */
				break;
			}

			g_warning ("error getting next file: %s", error->message);
			g_clear_error (&error);
			continue;
		}

		child = g_file_get_child (dir, g_file_info_get_name (file_info));
		child_uri = g_file_get_uri (child);

		result = g_slice_new0 (RhythmDBEvent);
		result->db = db;
		result->type = RHYTHMDB_EVENT_STAT;
		result->entry_type = action->entry_type;
		result->error_type = action->error_type;
		result->ignore_type = action->ignore_type;
		result->real_uri = rb_refstring_new (child_uri);
		result->file_info = file_info;
		result->error = error;

		rhythmdb_push_event (db, result);
		g_free (child_uri);
	}

	g_file_enumerator_close (dir_enum, db->priv->exiting, &error);
	if (error != NULL) {
		/* hmm.. */
		rb_debug ("error closing file enumerator: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (dir);
	g_object_unref (dir_enum);
}

/**
 * rhythmdb_entry_get:
 * @db: the #RhythmDB
 * @entry: a #RhythmDBEntry.
 * @propid: the id of the property to get.
 * @val: return location for the property value.
 *
 * Gets a property of an entry, storing it in the given #GValue.
 **/
void
rhythmdb_entry_get (RhythmDB *db,
		    RhythmDBEntry *entry,
		    RhythmDBPropType propid,
		    GValue *val)
{
	g_return_if_fail (RHYTHMDB_IS (db));
	g_return_if_fail (entry != NULL);
	g_return_if_fail (entry->refcount > 0);

	rhythmdb_entry_sync_mirrored (entry, propid);

	g_assert (G_VALUE_TYPE (val) == rhythmdb_get_property_type (db, propid));
	switch (rhythmdb_property_type_map[propid]) {
	case G_TYPE_STRING:
		g_value_set_string (val, rhythmdb_entry_get_string (entry, propid));
		break;
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (val, rhythmdb_entry_get_boolean (entry, propid));
		break;
	case G_TYPE_ULONG:
		g_value_set_ulong (val, rhythmdb_entry_get_ulong (entry, propid));
		break;
	case G_TYPE_UINT64:
		g_value_set_uint64 (val, rhythmdb_entry_get_uint64 (entry, propid));
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (val, rhythmdb_entry_get_double (entry, propid));
		break;
	case G_TYPE_POINTER:
		g_value_set_pointer (val, rhythmdb_entry_get_pointer (entry, propid));
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
entry_to_rb_metadata (RhythmDB *db,
		      RhythmDBEntry *entry,
		      RBMetaData *metadata)
{
	GValue val = {0, };
	int i;

	for (i = RHYTHMDB_PROP_TYPE; i != RHYTHMDB_NUM_PROPERTIES; i++) {
		RBMetaDataField field;

		if (metadata_field_from_prop (i, &field) == FALSE) {
			continue;
		}

		g_value_init (&val, rhythmdb_property_type_map[i]);
		rhythmdb_entry_get (db, entry, i, &val);
		rb_metadata_set (metadata,
				 field,
				 &val);
		g_value_unset (&val);
	}
}

typedef struct
{
	RhythmDB *db;
	char *uri;
	GError *error;
} RhythmDBSaveErrorData;

static gboolean
emit_save_error_idle (RhythmDBSaveErrorData *data)
{
	g_signal_emit (G_OBJECT (data->db), rhythmdb_signals[SAVE_ERROR], 0, data->uri, data->error);
	g_object_unref (G_OBJECT (data->db));
	g_free (data->uri);
	g_error_free (data->error);
	g_free (data);
	return FALSE;
}

static gpointer
action_thread_main (RhythmDB *db)
{
	RhythmDBEvent *result;

	while (!g_cancellable_is_cancelled (db->priv->exiting)) {
		RhythmDBAction *action;

		action = g_async_queue_pop (db->priv->action_queue);

		/* hrm, do we need this check at all? */
		if (!g_cancellable_is_cancelled (db->priv->exiting)) {
			switch (action->type) {
			case RHYTHMDB_ACTION_STAT:
				result = g_slice_new0 (RhythmDBEvent);
				result->db = db;
				result->type = RHYTHMDB_EVENT_STAT;
				result->entry_type = action->entry_type;
				result->error_type = action->error_type;
				result->ignore_type = action->ignore_type;

				rb_debug ("executing RHYTHMDB_ACTION_STAT for \"%s\"", rb_refstring_get (action->uri));

				rhythmdb_execute_stat (db, rb_refstring_get (action->uri), result);
				break;

			case RHYTHMDB_ACTION_LOAD:
				result = g_slice_new0 (RhythmDBEvent);
				result->db = db;
				result->type = RHYTHMDB_EVENT_METADATA_LOAD;
				result->entry_type = action->entry_type;
				result->error_type = action->error_type;
				result->ignore_type = action->ignore_type;

				rb_debug ("executing RHYTHMDB_ACTION_LOAD for \"%s\"", rb_refstring_get (action->uri));

				rhythmdb_execute_load (db, rb_refstring_get (action->uri), result);
				break;

			case RHYTHMDB_ACTION_ENUM_DIR:
				rb_debug ("executing RHYTHMDB_ACTION_ENUM_DIR for \"%s\"", rb_refstring_get (action->uri));
				rhythmdb_execute_enum_dir (db, action);
				break;

			case RHYTHMDB_ACTION_SYNC:
			{
				GError *error = NULL;
				RhythmDBEntry *entry;
				RhythmDBEntryType entry_type;

				if (db->priv->dry_run) {
					rb_debug ("dry run is enabled, not syncing metadata");
					break;
				}

				entry = rhythmdb_entry_lookup_by_location_refstring (db, action->uri);
				if (!entry)
					break;

				entry_type = rhythmdb_entry_get_entry_type (entry);
				entry_type->sync_metadata (db, entry, &error, entry_type->sync_metadata_data);

				if (error != NULL) {
					RhythmDBSaveErrorData *data;

					data = g_new0 (RhythmDBSaveErrorData, 1);
					g_object_ref (db);
					data->db = db;
					data->uri = g_strdup (rb_refstring_get (action->uri));
					data->error = error;
					g_idle_add ((GSourceFunc)emit_save_error_idle, data);
					break;
				}
				break;
			}

			case RHYTHMDB_ACTION_QUIT:
				/* don't do any real work here, since we may not process it */
				rb_debug ("received QUIT action");
				break;

			default:
				g_assert_not_reached ();
				break;
			}
		}

		rhythmdb_action_free (db, action);
	}

	rb_debug ("exiting action thread");
	result = g_slice_new0 (RhythmDBEvent);
	result->db = db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (db, result);

	return NULL;
}

/**
 * rhythmdb_add_uri:
 * @db: a #RhythmDB.
 * @uri: the URI to add an entry/entries for
 *
 * Adds the file(s) pointed to by @uri to the database, as entries of type
 * RHYTHMDB_ENTRY_TYPE_SONG. If the URI is that of a file, it will be added.
 * If the URI is that of a directory, everything under it will be added recursively.
 **/
void
rhythmdb_add_uri (RhythmDB *db,
		  const char *uri)
{
	rhythmdb_add_uri_with_types (db,
				     uri,
				     RHYTHMDB_ENTRY_TYPE_INVALID,
				     RHYTHMDB_ENTRY_TYPE_IGNORE,
				     RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
}

static void
rhythmdb_add_to_stat_list (RhythmDB *db,
			   const char *uri,
			   RhythmDBEntry *entry,
			   RhythmDBEntryType type,
			   RhythmDBEntryType ignore_type,
			   RhythmDBEntryType error_type)
{
	RhythmDBEvent *result;

	result = g_slice_new0 (RhythmDBEvent);
	result->db = db;
	result->type = RHYTHMDB_EVENT_STAT;
	result->entry_type = type;
	result->ignore_type = ignore_type;
	result->error_type = error_type;
		
	if (entry != NULL) {
		result->entry = rhythmdb_entry_ref (entry);
	}

	/* do we really need to check for duplicate requests here?  .. nah. */
	result->uri = rb_refstring_new (uri);
	db->priv->stat_list = g_list_prepend (db->priv->stat_list, result);
}


/**
 * rhythmdb_add_uri_with_types:
 * @db: a #RhythmDB.
 * @uri: the URI to add
 * @type: the #RhythmDBEntryType to use for new entries
 * @ignore_type: the #RhythmDBEntryType to use for ignored files
 * @error_type: the #RhythmDBEntryType to use for import errors
 *
 * Adds the file(s) pointed to by @uri to the database, as entries
 * of the specified type. If the URI points to a file, it will be added.
 * The the URI identifies a directory, everything under it will be added
 * recursively.
 */
void
rhythmdb_add_uri_with_types (RhythmDB *db,
			     const char *uri,
			     RhythmDBEntryType type,
			     RhythmDBEntryType ignore_type,
			     RhythmDBEntryType error_type)
{
	rb_debug ("queueing stat for \"%s\"", uri);
	g_assert (uri && *uri);

	/*
	 * before the action thread is started, we queue up stat actions,
	 * as we're still creating and running queries, as well as loading
	 * the database.  when we start the action thread, we'll kick off
	 * a thread to process all the stat events too.
	 *
	 * when the action thread is already running, stat actions go through
	 * the normal action queue and are processed by the action thread.
	 */
	g_mutex_lock (db->priv->stat_mutex);
	if (db->priv->action_thread_running) {
		RhythmDBAction *action;
		g_mutex_unlock (db->priv->stat_mutex);

		action = g_slice_new0 (RhythmDBAction);
		action->type = RHYTHMDB_ACTION_STAT;
		action->uri = rb_refstring_new (uri);
		action->entry_type = type;
		action->ignore_type = ignore_type;
		action->error_type = error_type;

		g_async_queue_push (db->priv->action_queue, action);
	} else {
		RhythmDBEntry *entry;

		entry = rhythmdb_entry_lookup_by_location (db, uri);
		rhythmdb_add_to_stat_list (db, uri, entry, type, ignore_type, error_type);

		g_mutex_unlock (db->priv->stat_mutex);
	}
}


static gboolean
rhythmdb_sync_library_idle (RhythmDB *db)
{
	rhythmdb_sync_library_location (db);
	g_object_unref (db);
	return FALSE;
}

static gboolean
rhythmdb_load_error_cb (GError *error)
{
	GDK_THREADS_ENTER ();
	rb_error_dialog (NULL,
			 _("Could not load the music database:"),
			 "%s", error->message);
	g_error_free (error);

	GDK_THREADS_LEAVE ();
	return FALSE;
}

static gpointer
rhythmdb_load_thread_main (RhythmDB *db)
{
	RhythmDBEvent *result;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GError *error = NULL;

	rb_profile_start ("loading db");
	g_mutex_lock (db->priv->saving_mutex);
	if (klass->impl_load (db, db->priv->exiting, &error) == FALSE) {
		rb_debug ("db load failed: disabling saving");
		db->priv->can_save = FALSE;

		if (error) {
			g_idle_add ((GSourceFunc) rhythmdb_load_error_cb, error);
		}
	}
	g_mutex_unlock (db->priv->saving_mutex);

	g_object_ref (db);
	g_timeout_add_seconds (10, (GSourceFunc) rhythmdb_sync_library_idle, db);

	rb_debug ("queuing db load complete signal");
	result = g_slice_new0 (RhythmDBEvent);
	result->type = RHYTHMDB_EVENT_DB_LOAD;
	g_async_queue_push (db->priv->event_queue, result);

	rb_debug ("exiting");
	result = g_slice_new0 (RhythmDBEvent);
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (db, result);

	rb_profile_end ("loading db");
	return NULL;
}

/**
 * rhythmdb_load:
 * @db: a #RhythmDB.
 *
 * Load the database from disk.
 **/
void
rhythmdb_load (RhythmDB *db)
{
	rhythmdb_thread_create (db, NULL, (GThreadFunc) rhythmdb_load_thread_main, db);
}

static gpointer
rhythmdb_save_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass;
	RhythmDBEvent *result;

	rb_debug ("entering save thread");

	g_mutex_lock (db->priv->saving_mutex);

	db->priv->save_count++;
	g_cond_broadcast (db->priv->saving_condition);

	if (!(db->priv->dirty && db->priv->can_save)) {
		rb_debug ("no save needed, ignoring");
		g_mutex_unlock (db->priv->saving_mutex);
		goto out;
	}

	while (db->priv->saving)
		g_cond_wait (db->priv->saving_condition, db->priv->saving_mutex);

	db->priv->saving = TRUE;

	rb_debug ("saving rhythmdb");

	klass = RHYTHMDB_GET_CLASS (db);
	klass->impl_save (db);

	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	g_mutex_unlock (db->priv->saving_mutex);

	g_cond_broadcast (db->priv->saving_condition);

out:
	result = g_slice_new0 (RhythmDBEvent);
	result->db = db;
	result->type = RHYTHMDB_EVENT_DB_SAVED;
	g_async_queue_push (db->priv->event_queue, result);

	result = g_slice_new0 (RhythmDBEvent);
	result->db = db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (db, result);
	return NULL;
}

/**
 * rhythmdb_save_async:
 * @db: a #RhythmDB.
 *
 * Save the database to disk, asynchronously.
 **/
void
rhythmdb_save_async (RhythmDB *db)
{
	rb_debug ("saving the rhythmdb in the background");

	rhythmdb_read_enter (db);

	rhythmdb_thread_create (db, NULL, (GThreadFunc) rhythmdb_save_thread_main, db);
}

/**
 * rhythmdb_save:
 * @db: a #RhythmDB.
 *
 * Save the database to disk, not returning until it has been saved.
 **/
void
rhythmdb_save (RhythmDB *db)
{
	int new_save_count;
	
	rb_debug("saving the rhythmdb and blocking");

	g_mutex_lock (db->priv->saving_mutex);
	new_save_count = db->priv->save_count + 1;
	
	rhythmdb_save_async (db);
	
	/* wait until this save request is being processed */
	while (db->priv->save_count < new_save_count) {
		g_cond_wait (db->priv->saving_condition, db->priv->saving_mutex);
	}
	
	/* wait until it's done */
	while (db->priv->saving) {
		g_cond_wait (db->priv->saving_condition, db->priv->saving_mutex);
	}

	rb_debug ("done");

	g_mutex_unlock (db->priv->saving_mutex);
}

/**
 * rhythmdb_entry_set:
 * @db:# a RhythmDB.
 * @entry: a #RhythmDBEntry.
 * @propid: the id of the property to set.
 * @value: the property value.
 *
 * This function can be called by any code which wishes to change a
 * song property and send a notification.  It may be called when the
 * database is read-only; in this case the change will be queued for
 * an unspecified time in the future.  The implication of this is that
 * rhythmdb_entry_get() may not reflect the changes immediately.  However,
 * if this property is exposed in the user interface, you should still
 * make the change in the widget.  Then when the database returns to a
 * writable state, your change will take effect in the database too,
 * and a notification will be sent at that point.
 *
 * Note that you must call rhythmdb_commit() at some point after invoking
 * this function, and that even after the commit, your change may not
 * have taken effect.
 **/
void
rhythmdb_entry_set (RhythmDB *db,
		    RhythmDBEntry *entry,
		    guint propid,
		    const GValue *value)
{
	g_return_if_fail (RHYTHMDB_IS (db));
	g_return_if_fail (entry != NULL);

	if ((entry->flags & RHYTHMDB_ENTRY_INSERTED) != 0) {
		if (!rhythmdb_get_readonly (db) && rb_is_main_thread ()) {
			rhythmdb_entry_set_internal (db, entry, TRUE, propid, value);
		} else {
			RhythmDBEvent *result;

			result = g_slice_new0 (RhythmDBEvent);
			result->db = db;
			result->type = RHYTHMDB_EVENT_ENTRY_SET;

			rb_debug ("queuing RHYTHMDB_ACTION_ENTRY_SET");

			result->entry = rhythmdb_entry_ref (entry);
			result->change.prop = propid;
			result->signal_change = TRUE;
			g_value_init (&result->change.new, G_VALUE_TYPE (value));
			g_value_copy (value, &result->change.new);
			rhythmdb_push_event (db, result);
		}
	} else {
		rhythmdb_entry_set_internal (db, entry, FALSE, propid, value);
	}
}

static void
record_entry_change (RhythmDB *db,
		     RhythmDBEntry *entry,
		     guint propid,
		     const GValue *old_value,
		     const GValue *new_value)
{
	RhythmDBEntryChange *changedata;
	GSList *changelist;

	changedata = g_slice_new0 (RhythmDBEntryChange);
	changedata->prop = propid;

	g_value_init (&changedata->old, G_VALUE_TYPE (old_value));
	g_value_init (&changedata->new, G_VALUE_TYPE (new_value));
	g_value_copy (old_value, &changedata->old);
	g_value_copy (new_value, &changedata->new);

	g_mutex_lock (db->priv->change_mutex);
	/* ref the entry before adding to hash, it is unreffed when removed */
	rhythmdb_entry_ref (entry);
	changelist = g_hash_table_lookup (db->priv->changed_entries, entry);
	changelist = g_slist_append (changelist, changedata);
	g_hash_table_insert (db->priv->changed_entries, entry, changelist);
	g_mutex_unlock (db->priv->change_mutex);
}

void
rhythmdb_entry_set_internal (RhythmDB *db,
			     RhythmDBEntry *entry,
			     gboolean notify_if_inserted,
			     guint propid,
			     const GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	gboolean handled;
	RhythmDBPodcastFields *podcast = NULL;
	GValue old_value = {0,};
	gboolean nop;

	g_return_if_fail (entry != NULL);

	/* compare the value with what's already there */
	g_value_init (&old_value, G_VALUE_TYPE (value));
	rhythmdb_entry_get (db, entry, propid, &old_value);
	switch (G_VALUE_TYPE (value)) {
	case G_TYPE_STRING:
#ifndef G_DISABLE_ASSERT
		/* the playback error is allowed to be NULL */
		if (propid != RHYTHMDB_PROP_PLAYBACK_ERROR || g_value_get_string (value))
			g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
#endif
		if (g_value_get_string (value) && g_value_get_string (&old_value)) {
			nop = (strcmp (g_value_get_string (value), g_value_get_string (&old_value)) == 0);
		} else {
			nop = FALSE;
		}
		break;
	case G_TYPE_BOOLEAN:
		nop = (g_value_get_boolean (value) == g_value_get_boolean (&old_value));
		break;
	case G_TYPE_ULONG:
		nop = (g_value_get_ulong (value) == g_value_get_ulong (&old_value));
		break;
	case G_TYPE_UINT64:
		nop = (g_value_get_uint64 (value) == g_value_get_uint64 (&old_value));
		break;
	case G_TYPE_DOUBLE:
		nop = (g_value_get_double (value) == g_value_get_double (&old_value));
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	if (nop == FALSE && (entry->flags & RHYTHMDB_ENTRY_INSERTED) && notify_if_inserted) {
		record_entry_change (db, entry, propid, &old_value, value);
	}
	g_value_unset (&old_value);

	if (nop)
		return;

	handled = klass->impl_entry_set (db, entry, propid, value);

	if (!handled) {
		if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
		    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
			podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);

		switch (propid) {
		case RHYTHMDB_PROP_TYPE:
		case RHYTHMDB_PROP_ENTRY_ID:
			g_assert_not_reached ();
			break;
		case RHYTHMDB_PROP_TITLE:
			if (entry->title != NULL) {
				rb_refstring_unref (entry->title);
			}
			entry->title = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ALBUM:
			if (entry->album != NULL) {
				rb_refstring_unref (entry->album);
			}
			entry->album = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ARTIST:
			if (entry->artist != NULL) {
				rb_refstring_unref (entry->artist);
			}
			entry->artist = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_GENRE:
			if (entry->genre != NULL) {
				rb_refstring_unref (entry->genre);
			}
			entry->genre = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_TRACK_NUMBER:
			entry->tracknum = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DISC_NUMBER:
			entry->discnum = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DURATION:
			entry->duration = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_BITRATE:
			entry->bitrate = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DATE:
		{
			gulong julian;
			julian = g_value_get_ulong (value);
			if (julian > 0)
				g_date_set_julian (&entry->date, julian);
			else
				g_date_clear (&entry->date, 1);
			break;
		}
		case RHYTHMDB_PROP_TRACK_GAIN:
			entry->track_gain = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_TRACK_PEAK:
			entry->track_peak = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_ALBUM_GAIN:
			entry->album_gain = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_ALBUM_PEAK:
			entry->album_peak = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_LOCATION:
			rb_refstring_unref (entry->location);
			entry->location = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_PLAYBACK_ERROR:
			rb_refstring_unref (entry->playback_error);
			if (g_value_get_string (value))
				entry->playback_error = rb_refstring_new (g_value_get_string (value));
			else
				entry->playback_error = NULL;
			break;
		case RHYTHMDB_PROP_MOUNTPOINT:
			if (entry->mountpoint != NULL) {
				rb_refstring_unref (entry->mountpoint);
			}
			entry->mountpoint = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_FILE_SIZE:
			entry->file_size = g_value_get_uint64 (value);
			break;
		case RHYTHMDB_PROP_MIMETYPE:
			if (entry->mimetype != NULL) {
				rb_refstring_unref (entry->mimetype);
			}
			entry->mimetype = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_MTIME:
			entry->mtime = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_FIRST_SEEN:
			entry->first_seen = g_value_get_ulong (value);
			entry->flags |= RHYTHMDB_ENTRY_FIRST_SEEN_DIRTY;
			break;
		case RHYTHMDB_PROP_LAST_SEEN:
			entry->last_seen = g_value_get_ulong (value);
			entry->flags |= RHYTHMDB_ENTRY_LAST_SEEN_DIRTY;
			break;
		case RHYTHMDB_PROP_RATING:
			entry->rating = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_PLAY_COUNT:
			entry->play_count = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_LAST_PLAYED:
			entry->last_played = g_value_get_ulong (value);
			entry->flags |= RHYTHMDB_ENTRY_LAST_PLAYED_DIRTY;
			break;
		case RHYTHMDB_PROP_MUSICBRAINZ_TRACKID:
			rb_refstring_unref (entry->musicbrainz_trackid);
			entry->musicbrainz_trackid = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID:
			rb_refstring_unref (entry->musicbrainz_artistid);
			entry->musicbrainz_artistid = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID:
			rb_refstring_unref (entry->musicbrainz_albumid);
			entry->musicbrainz_albumid = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID:
			rb_refstring_unref (entry->musicbrainz_albumartistid);
			entry->musicbrainz_albumartistid = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ARTIST_SORTNAME:
			rb_refstring_unref (entry->artist_sortname);
			entry->artist_sortname = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ALBUM_SORTNAME:
			rb_refstring_unref (entry->album_sortname);
			entry->album_sortname = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_HIDDEN:
			if (g_value_get_boolean (value)) {
				entry->flags |= RHYTHMDB_ENTRY_HIDDEN;
			} else {
				entry->flags &= ~RHYTHMDB_ENTRY_HIDDEN;
			}
			entry->flags |= RHYTHMDB_ENTRY_LAST_SEEN_DIRTY;
			break;
		case RHYTHMDB_PROP_STATUS:
			g_assert (podcast);
			podcast->status = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DESCRIPTION:
			g_assert (podcast);
			rb_refstring_unref (podcast->description);
			podcast->description = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_SUBTITLE:
			g_assert (podcast);
			rb_refstring_unref (podcast->subtitle);
			podcast->subtitle = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_SUMMARY:
			g_assert (podcast);
			rb_refstring_unref (podcast->summary);
			podcast->summary = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_LANG:
			g_assert (podcast);
			if (podcast->lang != NULL) {
				rb_refstring_unref (podcast->lang);
			}
			podcast->lang = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_COPYRIGHT:
			g_assert (podcast);
			if (podcast->copyright != NULL) {
				rb_refstring_unref (podcast->copyright);
			}
			podcast->copyright = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_IMAGE:
			g_assert (podcast);
			if (podcast->image != NULL) {
				rb_refstring_unref (podcast->image);
			}
			podcast->image = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_POST_TIME:
			g_assert (podcast);
			podcast->post_time = g_value_get_ulong (value);
			break;
		case RHYTHMDB_NUM_PROPERTIES:
			g_assert_not_reached ();
			break;
		}
	}

	/* set the dirty state */
	db->priv->dirty = TRUE;
}

/**
 * rhythmdb_entry_sync_mirrored:
 * @db: a #RhythmDB.
 * @type: a #RhythmDBEntry.
 * @propid: the property to sync the mirrored version of.
 *
 * Synchronise "mirrored" properties, such as the string version of the last-played
 * time. This should be called when a property is directly modified, passing the
 * original property.
 *
 * This should only be used by RhythmDB itself, or a backend (such as rhythmdb-tree).
 **/

static void
rhythmdb_entry_sync_mirrored (RhythmDBEntry *entry,
			      guint propid)
{
	static const char *never;
	char *val;

	if (never == NULL)
		never = _("Never");

	switch (propid) {
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
	{
		RBRefString *old, *new;

		if (!(entry->flags & RHYTHMDB_ENTRY_LAST_PLAYED_DIRTY))
			break;

		old = g_atomic_pointer_get (&entry->last_played_str);
		if (entry->last_played == 0) {
			new = rb_refstring_new (never);
		} else {
			val = rb_utf_friendly_time (entry->last_played);
			new = rb_refstring_new (val);
			g_free (val);
		}

		if (g_atomic_pointer_compare_and_exchange (&entry->last_played_str, old, new)) {
			if (old != NULL) {
				rb_refstring_unref (old);
			}
		} else {
			rb_refstring_unref (new);
		}

		break;
	}
	case RHYTHMDB_PROP_FIRST_SEEN_STR:
	{
		RBRefString *old, *new;

		if (!(entry->flags & RHYTHMDB_ENTRY_FIRST_SEEN_DIRTY))
			break;

		old = g_atomic_pointer_get (&entry->first_seen_str);
 		if (entry->first_seen == 0) {
			new = rb_refstring_new (never);
 		} else {
 			val = rb_utf_friendly_time (entry->first_seen);
 			new = rb_refstring_new (val);
 			g_free (val);
 		}

		if (g_atomic_pointer_compare_and_exchange (&entry->first_seen_str, old, new)) {
			if (old != NULL) {
				rb_refstring_unref (old);
			}
		} else {
			rb_refstring_unref (new);
		}

		break;
	}
	case RHYTHMDB_PROP_LAST_SEEN_STR:
	{
		RBRefString *old, *new;

		if (!(entry->flags & RHYTHMDB_ENTRY_LAST_SEEN_DIRTY))
			break;

		old = g_atomic_pointer_get (&entry->last_seen_str);
		/* only store last seen time as a string for hidden entries */
		if (entry->flags & RHYTHMDB_ENTRY_HIDDEN) {
			val = rb_utf_friendly_time (entry->last_seen);
			new = rb_refstring_new (val);
			g_free (val);
		} else {
			new = NULL;
		}

		if (g_atomic_pointer_compare_and_exchange (&entry->last_seen_str, old, new)) {
			if (old != NULL) {
				rb_refstring_unref (old);
			}
		} else {
			rb_refstring_unref (new);
		}

		break;
	}
	default:
		break;
	}
}

/**
 * rhythmdb_entry_delete:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 *
 * Delete entry @entry from the database, sending notification of its deletion.
 * This is usually used by sources where entries can disappear randomly, such
 * as a network source.
 **/
void
rhythmdb_entry_delete (RhythmDB *db,
		       RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	g_return_if_fail (RHYTHMDB_IS (db));
	g_return_if_fail (entry != NULL);

	rb_debug ("deleting entry %p", entry);

	/* ref the entry before adding to hash, it is unreffed when removed */
	rhythmdb_entry_ref (entry);

	klass->impl_entry_delete (db, entry);

	g_mutex_lock (db->priv->change_mutex);
	g_hash_table_insert (db->priv->deleted_entries, entry, g_thread_self ());
	g_mutex_unlock (db->priv->change_mutex);

	/* deleting an entry makes the db dirty */
	db->priv->dirty = TRUE;
}

void
rhythmdb_entry_move_to_trash (RhythmDB *db,
			      RhythmDBEntry *entry)
{
	const char *uri;
	GFile *file;
	GError *error = NULL;

	uri = rb_refstring_get (entry->location);
	file = g_file_new_for_uri (uri);

	g_file_trash (file, NULL, &error);
	if (error != NULL) {
		GValue value = { 0, };

		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, error->message);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
		g_value_unset (&value);

		rb_debug ("trashing %s failed: %s",
			  uri,
			  error->message);
		g_error_free (error);
				
	} else {
		rhythmdb_entry_set_visibility (db, entry, FALSE);
	}
	g_object_unref (file);
}

/**
 * rhythmdb_entry_delete_by_type:
 * @db: a #RhythmDB.
 * @type: type of entried to delete.
 *
 * Delete all entries from the database of the given type.
 * This is usually used by non-permanent sources when they disappear, such as
 * removable media being removed, or a network share becoming unavailable.
 **/
void
rhythmdb_entry_delete_by_type (RhythmDB *db,
			       RhythmDBEntryType type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	if (klass->impl_entry_delete_by_type) {
		klass->impl_entry_delete_by_type (db, type);
	} else {
		g_warning ("delete_by_type not implemented");
	}
}

const xmlChar *
rhythmdb_nice_elt_name_from_propid (RhythmDB *db,
				    RhythmDBPropType propid)
{
	return db->priv->column_xml_names[propid];
}

int
rhythmdb_propid_from_nice_elt_name (RhythmDB *db,
				    const xmlChar *name)
{
	gpointer ret, orig;
	if (g_hash_table_lookup_extended (db->priv->propname_map, name,
					  &orig, &ret)) {
		return GPOINTER_TO_INT (ret);
	}
	return -1;
}

/**
 * rhythmdb_entry_lookup_by_location:
 * @db: a #RhythmDB.
 * @uri: the URI of the entry to lookup.
 *
 * Looks up the entry with location @uri.
 *
 * Returns: the entry with location @uri, or NULL if no such entry exists.
 **/
RhythmDBEntry *
rhythmdb_entry_lookup_by_location (RhythmDB *db,
				   const char *uri)
{
	RBRefString *rs;

	rs = rb_refstring_find (uri);
	if (rs != NULL) {
		return rhythmdb_entry_lookup_by_location_refstring (db, rs);
	} else {
		return NULL;
	}
}

RhythmDBEntry *
rhythmdb_entry_lookup_by_location_refstring (RhythmDB *db,
					     RBRefString *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lookup_by_location (db, uri);
}

/**
 *rhythmdb_entry_lookup_by_id:
 * @db: a #RhythmDB.
 * @id: entry ID
 *
 * Looks up the entry with id @id.
 *
 * Returns: the entry with id @id, or NULL if no such entry exists.
 */
RhythmDBEntry *
rhythmdb_entry_lookup_by_id (RhythmDB *db,
			     gint id)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lookup_by_id (db, id);
}

/**
 *rhythmdb_entry_lookup_from_string:
 * @db: a #RhythmDB.
 * @str: string
 * @is_id: whether the string is an entry ID or a location.
 *
 * Locates an entry using a string containing either an entry ID
 * or a location.
 *
 * Returns: the entry matching the string, or NULL if no such entry exists.
 */
RhythmDBEntry *
rhythmdb_entry_lookup_from_string (RhythmDB *db,
				   const char *str,
				   gboolean is_id)
{
	if (is_id) {
		gint id;

		id = strtoul (str, NULL, 10);
		if (id == 0)
			return NULL;

		return rhythmdb_entry_lookup_by_id (db, id);
	} else {
		return rhythmdb_entry_lookup_by_location (db, str);
	}
}

/**
 *rhythmdb_entry_foreach:
 * @db: a #RhythmDB.
 * @func: the function to call with each entry.
 * @data: user data to pass to the function.
 *
 * Calls the given function for each of the entries in the database.
 **/
void
rhythmdb_entry_foreach (RhythmDB *db,
			GFunc func,
			gpointer data)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_foreach (db, func, data);
}

/**
 *rhythmdb_entry_count:
 * @db: a #RhythmDB.
 *
 * Returns: the number of entries in the database.
 */
gint64
rhythmdb_entry_count (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_count (db);
}

/**
 *rhythmdb_entry_foreach_by_type:
 * @db: a #RhythmdB.
 * @entry_type: the type of entry to retrieve
 * @func: the function to call with each entry
 * @data: user data to pass to the function.
 *
 * Calls the given function for each of the entries in the database
 * of a given type.
 */
void
rhythmdb_entry_foreach_by_type (RhythmDB *db,
				RhythmDBEntryType entry_type,
				GFunc func,
				gpointer data)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_foreach_by_type (db, entry_type, func, data);
}

/**
 *rhythmdb_entry_count_by_type:
 * @db: a #RhythmDB.
 * @entry_type: a #RhythmDBEntryType.
 *
 * Returns: the number of entries in the database of a particular type.
 */
gint64
rhythmdb_entry_count_by_type (RhythmDB *db,
			      RhythmDBEntryType entry_type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_count_by_type (db, entry_type);
}


/**
 * rhythmdb_evaluate_query:
 * @db: a #RhythmDB.
 * @query: a query.
 * @entry: a @RhythmDBEntry.
 *
 * Evaluates the given entry against the given query.
 *
 * Returns: whether the given entry matches the criteria of the given query.
 **/
gboolean
rhythmdb_evaluate_query (RhythmDB *db,
			 GPtrArray *query,
			 RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_evaluate_query (db, query, entry);
}

static void
rhythmdb_query_internal (RhythmDBQueryThreadData *data)
{
	RhythmDBEvent *result;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (data->db);

	rhythmdb_query_preprocess (data->db, data->query);

	rb_debug ("doing query");

	klass->impl_do_full_query (data->db, data->query,
				   data->results,
				   &data->cancel);

	rb_debug ("completed");
	rhythmdb_query_results_query_complete (data->results);

	result = g_slice_new0 (RhythmDBEvent);
	result->db = data->db;
	result->type = RHYTHMDB_EVENT_QUERY_COMPLETE;
	result->results = data->results;
	rhythmdb_push_event (data->db, result);

	rhythmdb_query_free (data->query);
}

static gpointer
query_thread_main (RhythmDBQueryThreadData *data)
{
	RhythmDBEvent *result;

	rb_debug ("entering query thread");

	rhythmdb_query_internal (data);

	result = g_slice_new0 (RhythmDBEvent);
	result->db = data->db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (data->db, result);
	g_free (data);
	return NULL;
}

void
rhythmdb_do_full_query_async_parsed (RhythmDB *db,
				     RhythmDBQueryResults *results,
				     GPtrArray *query)
{
	RhythmDBQueryThreadData *data;

	data = g_new0 (RhythmDBQueryThreadData, 1);
	data->db = db;
	data->query = rhythmdb_query_copy (query);
	data->results = results;
	data->cancel = FALSE;

	rhythmdb_read_enter (db);

	rhythmdb_query_results_set_query (results, query);

	g_object_ref (results);
	g_object_ref (db);
	g_atomic_int_inc (&db->priv->outstanding_threads);
	g_async_queue_ref (db->priv->action_queue);
	g_async_queue_ref (db->priv->event_queue);
	g_thread_pool_push (db->priv->query_thread_pool, data, NULL);
}

void
rhythmdb_do_full_query_async (RhythmDB *db,
			      RhythmDBQueryResults *results,
			      ...)
{
	GPtrArray *query;
	va_list args;

	va_start (args, results);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_async_parsed (db, results, query);

	rhythmdb_query_free (query);

	va_end (args);
}

static void
rhythmdb_do_full_query_internal (RhythmDB *db,
				 RhythmDBQueryResults *results,
				 GPtrArray *query)
{
	RhythmDBQueryThreadData *data;

	data = g_new0 (RhythmDBQueryThreadData, 1);
	data->db = db;
	data->query = rhythmdb_query_copy (query);
	data->results = results;
	data->cancel = FALSE;

	rhythmdb_read_enter (db);

	rhythmdb_query_results_set_query (results, query);
	g_object_ref (results);

	rhythmdb_query_internal (data);
	g_free (data);
}

void
rhythmdb_do_full_query_parsed (RhythmDB *db,
			       RhythmDBQueryResults *results,
			       GPtrArray *query)
{
	rhythmdb_do_full_query_internal (db, results, query);
}

void
rhythmdb_do_full_query (RhythmDB *db,
			RhythmDBQueryResults *results,
			...)
{
	GPtrArray *query;
	va_list args;

	va_start (args, results);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_internal (db, results, query);

	rhythmdb_query_free (query);

	va_end (args);
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rhythmdb_query_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_QUERY_END, "Query end marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_DISJUNCTION, "Disjunctive marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_SUBQUERY, "Subquery"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_EQUALS, "Property equivalence"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LIKE, "Fuzzy property matching"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_NOT_LIKE, "Inverted fuzzy property matching"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_PREFIX, "Starts with"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_SUFFIX, "Ends with"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_GREATER, "True if property1 >= property2"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LESS, "True if property1 <= property2"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN, "True if property1 is within property2 of the current time"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN, "True if property1 is not within property2 of the current time"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_EQUALS, "Year equivalence: true if date within year"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_GREATER, "True if date greater than year"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_LESS, "True if date less than year"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBQueryType", values);
	}

	return etype;
}

GType
rhythmdb_entry_category_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (RHYTHMDB_ENTRY_NORMAL, "Anything that doesn't match the other categories"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_STREAM, "Endless streams (eg. shoutcast, last.fm)"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_CONTAINER, "Entries that point to other entries (eg. podcast feeds)"),
			ENUM_ENTRY (RHYTHMDB_ENTRY_VIRTUAL, "Import errors, ignored files"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBEntryCategory", values);
	}

	return etype;
}

GType
rhythmdb_prop_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			/* We reuse the description to store extra data about
			* a property.  The first part is just a generic
			* human-readable description.  Next, there is
			* a string describing the GType of the property, in
			* parenthesis.
			* Finally, there is the XML element name in brackets.
			*/
			ENUM_ENTRY (RHYTHMDB_PROP_TYPE, "Type of entry (gpointer) [type]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ENTRY_ID, "Numeric ID (gulong) [entry-id]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE, "Title (gchararray) [title]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (gchararray) [genre]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (gchararray) [artist]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (gchararray) [album]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (gulong) [track-number]"),
			ENUM_ENTRY (RHYTHMDB_PROP_DISC_NUMBER, "Disc Number (gulong) [disc-number]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, "Musicbrainz Track ID (gchararray) [mb-trackid]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, "Musicbrainz Artist ID (gchararray) [mb-artistid]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, "Musicbrainz Album ID (gchararray) [mb-albumid]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, "Musicbrainz Album Artist ID (gchararray) [mb-albumartistid]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORTNAME, "Artist Sortname (gchararray) [mb-artistsortname]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORTNAME, "Album Sortname (gchararray) [album-sortname]"),

			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration (gulong) [duration]"),
			ENUM_ENTRY (RHYTHMDB_PROP_FILE_SIZE, "File Size (guint64) [file-size]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LOCATION, "Location (gchararray) [location]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MOUNTPOINT, "Mount point it's located in (gchararray) [mountpoint]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MTIME, "Modification time (gulong) [mtime]"),
			ENUM_ENTRY (RHYTHMDB_PROP_FIRST_SEEN, "Time the song was added to the library (gulong) [first-seen]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_SEEN, "Last time the song was available (gulong) [last-seen]"),
			ENUM_ENTRY (RHYTHMDB_PROP_RATING, "Rating (gdouble) [rating]"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAY_COUNT, "Play Count (gulong) [play-count]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED, "Last Played (gulong) [last-played]"),
			ENUM_ENTRY (RHYTHMDB_PROP_BITRATE, "Bitrate (gulong) [bitrate]"),
			ENUM_ENTRY (RHYTHMDB_PROP_DATE, "Date of release (gulong) [date]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_GAIN, "Replaygain track gain (gdouble) [replaygain-track-gain]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_PEAK, "Replaygain track peak (gdouble) [replaygain-track-peak]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_GAIN, "Replaygain album pain (gdouble) [replaygain-album-gain]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_PEAK, "Replaygain album peak (gdouble) [replaygain-album-peak]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MIMETYPE, "Mime Type (gchararray) [mimetype]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_SORT_KEY, "Title sort key (gchararray) [title-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (gchararray) [genre-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (gchararray) [artist-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (gchararray) [album-sort-key]"),

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_FOLDED, "Title folded (gchararray) [title-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_FOLDED, "Genre folded (gchararray) [genre-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_FOLDED, "Artist folded (gchararray) [artist-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_FOLDED, "Album folded (gchararray) [album-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED_STR, "Last Played (gchararray) [last-played-str]"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAYBACK_ERROR, "Playback error string (gchararray) [playback-error]"),
			ENUM_ENTRY (RHYTHMDB_PROP_HIDDEN, "Hidden (gboolean) [hidden]"),
			ENUM_ENTRY (RHYTHMDB_PROP_FIRST_SEEN_STR, "Time Added to Library (gchararray) [first-seen-str]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_SEEN_STR, "Last time the song was available (gchararray) [last-seen-str]"),
			ENUM_ENTRY (RHYTHMDB_PROP_SEARCH_MATCH, "Search matching key (gchararray) [search-match]"),
			ENUM_ENTRY (RHYTHMDB_PROP_YEAR, "Year of date (gulong) [year]"),

			ENUM_ENTRY (RHYTHMDB_PROP_STATUS, "Status of file (gulong) [status]"),
			ENUM_ENTRY (RHYTHMDB_PROP_DESCRIPTION, "Podcast description(gchararray) [description]"),
			ENUM_ENTRY (RHYTHMDB_PROP_SUBTITLE, "Podcast subtitle (gchararray) [subtitle]"),
			ENUM_ENTRY (RHYTHMDB_PROP_SUMMARY, "Podcast summary (gchararray) [summary]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LANG, "Podcast language (gchararray) [lang]"),
			ENUM_ENTRY (RHYTHMDB_PROP_COPYRIGHT, "Podcast copyright (gchararray) [copyright]"),
			ENUM_ENTRY (RHYTHMDB_PROP_IMAGE, "Podcast image(gchararray) [image]"),
			ENUM_ENTRY (RHYTHMDB_PROP_POST_TIME, "Podcast time of post (gulong) [post-time]"),

			ENUM_ENTRY (RHYTHMDB_PROP_KEYWORD, "Keywords applied to track (gchararray) [keyword]"),
			{ 0, 0, 0 }
		};
		g_assert ((sizeof (values) / sizeof (values[0]) - 1) == RHYTHMDB_NUM_PROPERTIES);
		etype = g_enum_register_static ("RhythmDBPropType", values);
	}

	return etype;
}

void
rhythmdb_emit_entry_deleted (RhythmDB *db,
			     RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
}

static gboolean
rhythmdb_entry_extra_metadata_accumulator (GSignalInvocationHint *ihint,
					   GValue *return_accu,
					   const GValue *handler_return,
					   gpointer data)
{
	if (handler_return == NULL)
		return TRUE;

	g_value_copy (handler_return, return_accu);
	return (g_value_get_boxed (return_accu) == NULL);
}

/**
 * rhythmdb_entry_request_extra_metadata:
 * @db: a #RhythmDB
 * @entry: a #RhythmDBEntry
 * @property_name: the metadata predicate
 *
 * Emits a request for extra metadata for the @entry.
 * The @property_name argument is emitted as the ::detail part of the
 * "entry_extra_metadata_request" signal. It should be a namespaced RDF
 * predicate e.g. from Dublin Core, MusicBrainz, or internal to Rhythmbox
 * (namespace "rb:"). Suitable predicates would be those that are expensive to
 * acquire or only apply to a limited range of entries.
 * Handlers capable of providing a particular predicate may ensure they only
 * see appropriate requests by supplying an appropriate ::detail part when
 * connecting to the signal. Upon a handler returning a non-%NULL value,
 * emission will be stopped and the value returned to the caller; if no
 * handlers return a non-%NULL value, the caller will receive %NULL. Priority
 * is determined by signal connection order, with %G_CONNECT_AFTER providing a
 * second, lower rank of priority.
 * A handler returning a value should do so in a #GValue allocated on the heap;
 * the accumulator will take ownership. The caller should unset and free the
 * #GValue if non-%NULL when finished with it.
 *
 * Returns: an allocated, initialised, set #GValue, or NULL
 **/
GValue *
rhythmdb_entry_request_extra_metadata (RhythmDB *db,
				       RhythmDBEntry *entry,
				       const gchar *property_name)
{
	GValue *value = NULL;

	g_signal_emit (G_OBJECT (db),
		       rhythmdb_signals[ENTRY_EXTRA_METADATA_REQUEST],
		       g_quark_from_string (property_name),
		       entry,
		       &value);

	return value;
}

/**
 * rhythmdb_emit_entry_extra_metadata_notify:
 * @db: a #RhythmDB
 * @entry: a #RhythmDBEntry
 * @property_name: the metadata predicate
 * @metadata: a #GValue
 *
 * Emits a signal describing extra metadata for the @entry.  The @property_name
 * argument is emitted as the ::detail part of the
 * "entry_extra_metadata_notify" signal and as the 'field' parameter.  Handlers
 * can ensure they only get metadata they are interested in by supplying an
 * appropriate ::detail part when connecting to the signal.  If handlers are
 * interested in the metadata they should ref or copy the contents of @metadata
 * and unref or free it when they are finished with it.
 **/
void
rhythmdb_emit_entry_extra_metadata_notify (RhythmDB *db,
					   RhythmDBEntry *entry,
					   const gchar *property_name,
					   const GValue *metadata)
{
	g_signal_emit (G_OBJECT (db),
		       rhythmdb_signals[ENTRY_EXTRA_METADATA_NOTIFY],
		       g_quark_from_string (property_name),
		       entry,
		       property_name,
		       metadata);
}

/**
 * rhythmdb_entry_extra_gather:
 * @db: a #RhythmDB
 * @entry: a #RhythmDBEntry
 *
 * Gathers all metadata for the @entry. The returned GHashTable maps property
 * names and extra metadata names (described under
 * @rhythmdb_entry_request_extra_metadata) to GValues. Anything wanting to
 * provide extra metadata should connect to the "entry_extra_metadata_gather"
 * signal.
 *
 * Returns: a RBStringValueMap containing metadata for the entry.  This must be freed
 * using g_object_unref.
 */
RBStringValueMap *
rhythmdb_entry_gather_metadata (RhythmDB *db,
				RhythmDBEntry *entry)
{
	RBStringValueMap *metadata;
	GEnumClass *klass;
	guint i;

	metadata = rb_string_value_map_new ();

	/* add core properties */
	klass = g_type_class_ref (RHYTHMDB_TYPE_PROP_TYPE);
	for (i = 0; i < klass->n_values; i++) {
		GValue value = {0,};
		gint prop;
		GType value_type;
		const char *name;

		prop = klass->values[i].value;

		/* only include easily marshallable types in the hash table */
		value_type = rhythmdb_get_property_type (db, prop);
		switch (value_type) {
		case G_TYPE_STRING:
		case G_TYPE_BOOLEAN:
		case G_TYPE_ULONG:
		case G_TYPE_UINT64:
		case G_TYPE_DOUBLE:
			break;
		default:
			continue;
		}

		g_value_init (&value, value_type);
		rhythmdb_entry_get (db, entry, prop, &value);
		name = (char *)rhythmdb_nice_elt_name_from_propid (db, prop);
		rb_string_value_map_set (metadata, name, &value);
		g_value_unset (&value);
	}
	g_type_class_unref (klass);

	/* gather extra metadata */
	g_signal_emit (G_OBJECT (db),
		       rhythmdb_signals[ENTRY_EXTRA_METADATA_GATHER], 0,
		       entry,
		       metadata);

	return metadata;
}

static gboolean
queue_is_empty (GAsyncQueue *queue)
{
	return g_async_queue_length (queue) <= 0;
}

/**
 * rhythmdb_is_busy:
 * @db: a #RhythmDB.
 *
 * Returns: whether the #RhythmDB has events to process.
 **/
gboolean
rhythmdb_is_busy (RhythmDB *db)
{
	return (!db->priv->action_thread_running ||
		db->priv->stat_thread_running ||
		!queue_is_empty (db->priv->event_queue) ||
		!queue_is_empty (db->priv->action_queue) ||
		(db->priv->outstanding_stats != NULL));
}

/**
 * rhythmdb_compute_status_normal:
 * @n_songs: the number of tracks.
 * @duration: the total duration of the tracks.
 * @size: the total size of the tracks.
 * @singular: singular form of the format string to use for entries (eg "%d song")
 * @plural: plural form of the format string to use for entries (eg "%d songs")
 *
 * Creates a string containing the "status" information about a list of tracks.
 * The singular and plural strings must be used in a direct ngettext call
 * elsewhere in order for them to be marked for translation correctly.
 *
 * Returns: the string, which should be freed with g_free.
 **/
char *
rhythmdb_compute_status_normal (gint n_songs,
				glong duration,
				guint64 size,
				const char *singular,
				const char *plural)
{
	long days, hours, minutes, seconds;
	char *songcount = NULL;
	char *time = NULL;
	char *size_str = NULL;
	char *ret;
	const char *minutefmt;
	const char *hourfmt;
	const char *dayfmt;

	songcount = g_strdup_printf (ngettext (singular, plural, n_songs), n_songs);

	days    = duration / (60 * 60 * 24);
	hours   = (duration / (60 * 60)) - (days * 24);
	minutes = (duration / 60) - ((days * 24 * 60) + (hours * 60));
	seconds = duration % 60;

	minutefmt = ngettext ("%ld minute", "%ld minutes", minutes);
	hourfmt = ngettext ("%ld hour", "%ld hours", hours);
	dayfmt = ngettext ("%ld day", "%ld days", days);
	if (days > 0) {
		if (hours > 0)
			if (minutes > 0) {
				char *fmt;
				/* Translators: the format is "X days, X hours and X minutes" */
				fmt = g_strdup_printf (_("%s, %s and %s"), dayfmt, hourfmt, minutefmt);
				time = g_strdup_printf (fmt, days, hours, minutes);
				g_free (fmt);
			} else {
				char *fmt;
				/* Translators: the format is "X days and X hours" */
				fmt = g_strdup_printf (_("%s and %s"), dayfmt, hourfmt);
				time = g_strdup_printf (fmt, days, hours);
				g_free (fmt);
			}
		else
			if (minutes > 0) {
				char *fmt;
				/* Translators: the format is "X days and X minutes" */
				fmt = g_strdup_printf (_("%s and %s"), dayfmt, minutefmt);
				time = g_strdup_printf (fmt, days, minutes);
				g_free (fmt);
			} else {
				time = g_strdup_printf (dayfmt, days);
			}
	} else {
		if (hours > 0) {
			if (minutes > 0) {
				char *fmt;
				/* Translators: the format is "X hours and X minutes" */
				fmt = g_strdup_printf (_("%s and %s"), hourfmt, minutefmt);
				time = g_strdup_printf (fmt, hours, minutes);
				g_free (fmt);
			} else {
				time = g_strdup_printf (hourfmt, hours);
			}

		} else {
			time = g_strdup_printf (minutefmt, minutes);
		}
	}

	size_str = g_format_size_for_display (size);

	if (size > 0 && duration > 0) {
		ret = g_strdup_printf ("%s, %s, %s", songcount, time, size_str);
	} else if (duration > 0) {
		ret = g_strdup_printf ("%s, %s", songcount, time);
	} else if (size > 0) {
		ret = g_strdup_printf ("%s, %s", songcount, size_str);
	} else {
		ret = g_strdup (songcount);
	}

	g_free (songcount);
	g_free (time);
	g_free (size_str);

	return ret;
}

static void
default_sync_metadata (RhythmDB *db,
		       RhythmDBEntry *entry,
		       GError **error,
		       gpointer data)
{
	const char *uri;
	GError *local_error = NULL;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	rb_metadata_load (db->priv->metadata,
			  uri, &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return;
	}

	entry_to_rb_metadata (db, entry, db->priv->metadata);

	rb_metadata_save (db->priv->metadata, &local_error);
	if (local_error != NULL) {
		RhythmDBAction *load_action;

		/* reload the metadata, to revert the db changes */
		rb_debug ("error saving metadata for %s: %s; reloading metadata to revert",
			  rb_refstring_get (entry->location),
			  local_error->message);
		load_action = g_slice_new0 (RhythmDBAction);
		load_action->type = RHYTHMDB_ACTION_LOAD;
		load_action->uri = rb_refstring_ref (entry->location);
		load_action->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		load_action->error_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		load_action->ignore_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		g_async_queue_push (db->priv->action_queue, load_action);

		g_propagate_error (error, local_error);
	}
}

/**
 * rhythmdb_entry_register_type:
 * @db: a #RhythmDB
 * @name: optional name for the entry type
 *
 * Registers a new #RhythmDBEntryType. This should be called to create a new
 * entry type for non-permanent sources.
 *
 * Returns: the new #RhythmDBEntryType.
 **/
RhythmDBEntryType
rhythmdb_entry_register_type (RhythmDB *db,
			      const char *name)
{
	RhythmDBEntryType type;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	g_assert (name != NULL);

	type = g_new0 (RhythmDBEntryType_, 1);
	type->can_sync_metadata = (RhythmDBEntryCanSyncFunc)rb_false_function;
	type->sync_metadata = default_sync_metadata;
	type->name = g_strdup (name);

	g_mutex_lock (db->priv->entry_type_map_mutex);
	g_hash_table_insert (db->priv->entry_type_map, g_strdup (type->name), type);
	g_mutex_unlock (db->priv->entry_type_map_mutex);

	if (klass->impl_entry_type_registered)
		klass->impl_entry_type_registered (db, name, type);

	return type;
}

static void
rhythmdb_entry_register_type_alias (RhythmDB *db,
				    RhythmDBEntryType type,
				    const char *name)
{
	char *dn = g_strdup (name);

	g_mutex_lock (db->priv->entry_type_map_mutex);
	g_hash_table_insert (db->priv->entry_type_map, dn, type);
	g_mutex_unlock (db->priv->entry_type_map_mutex);
}

typedef struct {
	GHFunc func;
	gpointer data;
} RhythmDBEntryTypeForeachData;

static void
rhythmdb_entry_type_foreach_cb (const char *name,
				RhythmDBEntryType entry_type,
				RhythmDBEntryTypeForeachData *data)
{
	/* skip aliases */
	if (strcmp (entry_type->name, name))
		return;

	data->func ((gpointer) name, entry_type, data->data);
}

/**
 * rhythmdb_entry_type_foreach:
 * @db: a #RhythmDB
 * @func: callback function to call for each registered entry type
 * @data: data to pass to the callback
 *
 * Calls a function for each registered entry type.
 */
void
rhythmdb_entry_type_foreach (RhythmDB *db,
			     GHFunc func,
			     gpointer data)
{
	RhythmDBEntryTypeForeachData d;

	d.func = func;
	d.data = data;

	g_mutex_lock (db->priv->entry_type_mutex);
	g_hash_table_foreach (db->priv->entry_type_map,
			      (GHFunc) rhythmdb_entry_type_foreach_cb,
			      &d);
	g_mutex_unlock (db->priv->entry_type_mutex);
}

/**
 * rhythmdb_entry_type_get_by_name:
 * @db: a #RhythmDB
 * @name: name of the type to look for
 *
 * Locates a #RhythmDBEntryType by name. Returns
 * RHYTHMDB_ENTRY_TYPE_INVALID if no entry type
 * is registered with the specified name.
 *
 * Returns: the #RhythmDBEntryType
 */
RhythmDBEntryType
rhythmdb_entry_type_get_by_name (RhythmDB *db,
				 const char *name)
{
	gpointer t = NULL;

	g_mutex_lock (db->priv->entry_type_map_mutex);
	if (db->priv->entry_type_map) {
		t = g_hash_table_lookup (db->priv->entry_type_map, name);
	}
	g_mutex_unlock (db->priv->entry_type_map_mutex);

	if (t)
		return (RhythmDBEntryType) t;

	return RHYTHMDB_ENTRY_TYPE_INVALID;
}

static gboolean
song_can_sync_metadata (RhythmDB *db,
			RhythmDBEntry *entry,
			gpointer data)
{
	const char *mimetype;

	mimetype = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	return rb_metadata_can_save (db->priv->metadata, mimetype);
}

static char *
podcast_get_playback_uri (RhythmDBEntry *entry,
			  gpointer data)
{
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) != NULL) {
		return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
	}
	return NULL;
}

static void
podcast_post_create (RhythmDBEntry *entry,
		     gpointer something)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	RBRefString *empty = rb_refstring_new ("");
	podcast->description = rb_refstring_ref (empty);
	podcast->subtitle = rb_refstring_ref (empty);
	podcast->summary = rb_refstring_ref (empty);
	podcast->lang = rb_refstring_ref (empty);
	podcast->copyright = rb_refstring_ref (empty);
	podcast->image = rb_refstring_ref (empty);
	rb_refstring_unref (empty);
}

static void
podcast_data_destroy (RhythmDBEntry *entry,
		      gpointer something)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	rb_refstring_unref (podcast->description);
	rb_refstring_unref (podcast->subtitle);
	rb_refstring_unref (podcast->summary);
	rb_refstring_unref (podcast->lang);
	rb_refstring_unref (podcast->copyright);
	rb_refstring_unref (podcast->image);
}

static RhythmDBEntryType song_type = RHYTHMDB_ENTRY_TYPE_INVALID;
static RhythmDBEntryType ignore_type = RHYTHMDB_ENTRY_TYPE_INVALID;
static RhythmDBEntryType import_error_type = RHYTHMDB_ENTRY_TYPE_INVALID;

/* to be evicted */
static RhythmDBEntryType podcast_post_type = RHYTHMDB_ENTRY_TYPE_INVALID;
static RhythmDBEntryType podcast_feed_type = RHYTHMDB_ENTRY_TYPE_INVALID;

static void
rhythmdb_register_core_entry_types (RhythmDB *db)
{
	/* regular songs */
	song_type = rhythmdb_entry_register_type (db, "song");
	rhythmdb_entry_register_type_alias (db, song_type, "0");
	song_type->save_to_disk = TRUE;
	song_type->has_playlists = TRUE;
	song_type->category = RHYTHMDB_ENTRY_NORMAL;
	song_type->can_sync_metadata = song_can_sync_metadata;

	/* import errors */
	import_error_type = rhythmdb_entry_register_type (db, "import-error");
	import_error_type->get_playback_uri = (RhythmDBEntryStringFunc)rb_null_function;
	import_error_type->category = RHYTHMDB_ENTRY_VIRTUAL;

	/* ignored files */
	ignore_type = rhythmdb_entry_register_type (db, "ignore");
	ignore_type->save_to_disk = TRUE;
	ignore_type->category = RHYTHMDB_ENTRY_VIRTUAL;
	ignore_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc) rb_true_function;
	ignore_type->sync_metadata = (RhythmDBEntrySyncFunc) rb_null_function;

	/* podcast posts */
	podcast_post_type = rhythmdb_entry_register_type (db, "podcast-post");
	podcast_post_type->entry_type_data_size = sizeof (RhythmDBPodcastFields);
	podcast_post_type->save_to_disk = TRUE;
	podcast_post_type->category = RHYTHMDB_ENTRY_NORMAL;
	podcast_post_type->post_entry_create = (RhythmDBEntryActionFunc) podcast_post_create;
	podcast_post_type->pre_entry_destroy = (RhythmDBEntryActionFunc) podcast_data_destroy;
	podcast_post_type->get_playback_uri = podcast_get_playback_uri;
	podcast_post_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc) rb_true_function;
	podcast_post_type->sync_metadata = (RhythmDBEntrySyncFunc) rb_null_function;

	/* podcast feeds */
	podcast_feed_type = rhythmdb_entry_register_type (db, "podcast-feed");
	podcast_feed_type->entry_type_data_size = sizeof (RhythmDBPodcastFields);
	podcast_feed_type->save_to_disk = TRUE;
	podcast_feed_type->category = RHYTHMDB_ENTRY_VIRTUAL;
	podcast_post_type->post_entry_create = (RhythmDBEntryActionFunc) podcast_post_create;
	podcast_feed_type->pre_entry_destroy = (RhythmDBEntryActionFunc) podcast_data_destroy;
	podcast_feed_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc) rb_true_function;
	podcast_feed_type->sync_metadata = (RhythmDBEntrySyncFunc) rb_null_function;
}

RhythmDBEntryType
rhythmdb_entry_song_get_type (void)
{
	return song_type;
}

RhythmDBEntryType
rhythmdb_entry_ignore_get_type (void)
{
	return ignore_type;
}

RhythmDBEntryType
rhythmdb_entry_import_error_get_type (void)
{
	return import_error_type;
}

RhythmDBEntryType 
rhythmdb_entry_podcast_post_get_type (void) 
{
	return podcast_post_type;
}

RhythmDBEntryType
rhythmdb_entry_podcast_feed_get_type (void)
{
	return podcast_feed_type;
}

static void
rhythmdb_entry_set_mount_point (RhythmDB *db,
				RhythmDBEntry *entry,
				const gchar *realuri)
{
	gchar *mount_point;
	GValue value = {0, };

	mount_point = rb_uri_get_mount_point (realuri);
	if (mount_point != NULL) {
		g_value_init (&value, G_TYPE_STRING);
		g_value_take_string (&value, mount_point);
		rhythmdb_entry_set_internal (db, entry, FALSE,
					     RHYTHMDB_PROP_MOUNTPOINT,
					     &value);
		g_value_unset (&value);
	}
}

void
rhythmdb_entry_set_visibility (RhythmDB *db,
			       RhythmDBEntry *entry,
			       gboolean visible)
{
	GValue old_val = {0, };
	gboolean old_visible;

	g_return_if_fail (RHYTHMDB_IS (db));
	g_return_if_fail (entry != NULL);

	g_value_init (&old_val, G_TYPE_BOOLEAN);

	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_HIDDEN, &old_val);
	old_visible = !g_value_get_boolean (&old_val);

	if ((old_visible && !visible) || (!old_visible && visible)) {
		GValue new_val = {0, };

		g_value_init (&new_val, G_TYPE_BOOLEAN);
		g_value_set_boolean (&new_val, !visible);
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_HIDDEN, &new_val);
		g_value_unset (&new_val);
	}
	g_value_unset (&old_val);
}

static gboolean
rhythmdb_idle_save (RhythmDB *db)
{
	if (db->priv->dirty) {
		rb_debug ("database is dirty, doing regular save");
		rhythmdb_save_async (db);
	}

	return TRUE;
}

static void
rhythmdb_sync_library_location (RhythmDB *db)
{
	gboolean reload = (db->priv->library_locations != NULL);

	if (db->priv->library_location_notify_id == 0) {
		db->priv->library_location_notify_id =
			eel_gconf_notification_add (CONF_LIBRARY_LOCATION,
						    (GConfClientNotifyFunc) library_location_changed_cb,
						    db);
	}

	if (reload) {
		rb_debug ("ending monitor of old library directories");

		rhythmdb_stop_monitoring (db);

		g_slist_foreach (db->priv->library_locations, (GFunc) g_free, NULL);
		g_slist_free (db->priv->library_locations);
		db->priv->library_locations = NULL;
	}

	if (eel_gconf_get_boolean (CONF_MONITOR_LIBRARY)) {
		rb_debug ("starting library monitoring");
		db->priv->library_locations = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);

		rhythmdb_start_monitoring (db);
	}
}

static
void rhythmdb_monitor_library_changed_cb (GConfClient *client,
					  guint cnxn_id,
					  GConfEntry *entry,
					  RhythmDB *db)
{
	rb_debug ("'watch library' key changed");
	rhythmdb_sync_library_location (db);
}

static void
library_location_changed_cb (GConfClient *client,
			     guint cnxn_id,
			     GConfEntry *entry,
			     RhythmDB *db)
{
	rhythmdb_sync_library_location (db);
}

char *
rhythmdb_entry_dup_string (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	const char *s;

	g_return_val_if_fail (entry != NULL, NULL);

	s = rhythmdb_entry_get_string (entry, propid);
	if (s != NULL) {
		return g_strdup (s);
	} else {
		return NULL;
	}
}

const char *
rhythmdb_entry_get_string (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	RhythmDBPodcastFields *podcast = NULL;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (entry->refcount > 0, NULL);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);

	rhythmdb_entry_sync_mirrored (entry, propid);

	switch (propid) {
	case RHYTHMDB_PROP_TITLE:
		return rb_refstring_get (entry->title);
	case RHYTHMDB_PROP_ALBUM:
		return rb_refstring_get (entry->album);
	case RHYTHMDB_PROP_ARTIST:
		return rb_refstring_get (entry->artist);
	case RHYTHMDB_PROP_GENRE:
		return rb_refstring_get (entry->genre);
	case RHYTHMDB_PROP_MUSICBRAINZ_TRACKID:
		return rb_refstring_get (entry->musicbrainz_trackid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID:
		return rb_refstring_get (entry->musicbrainz_artistid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID:
		return rb_refstring_get (entry->musicbrainz_albumid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID:
		return rb_refstring_get (entry->musicbrainz_albumartistid);
	case RHYTHMDB_PROP_ARTIST_SORTNAME:
		return rb_refstring_get (entry->artist_sortname);
	case RHYTHMDB_PROP_ALBUM_SORTNAME:
		return rb_refstring_get (entry->album_sortname);
	case RHYTHMDB_PROP_MIMETYPE:
		return rb_refstring_get (entry->mimetype);
	case RHYTHMDB_PROP_TITLE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->title);
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album);
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		return rb_refstring_get_sort_key (entry->artist);
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->genre);
	case RHYTHMDB_PROP_TITLE_FOLDED:
		return rb_refstring_get_folded (entry->title);
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		return rb_refstring_get_folded (entry->album);
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		return rb_refstring_get_folded (entry->artist);
	case RHYTHMDB_PROP_GENRE_FOLDED:
		return rb_refstring_get_folded (entry->genre);
	case RHYTHMDB_PROP_LOCATION:
		return rb_refstring_get (entry->location);
	case RHYTHMDB_PROP_MOUNTPOINT:
		return rb_refstring_get (entry->mountpoint);
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		return rb_refstring_get (entry->last_played_str);
	case RHYTHMDB_PROP_PLAYBACK_ERROR:
		return rb_refstring_get (entry->playback_error);
	case RHYTHMDB_PROP_FIRST_SEEN_STR:
		return rb_refstring_get (entry->first_seen_str);
	case RHYTHMDB_PROP_LAST_SEEN_STR:
		return rb_refstring_get (entry->last_seen_str);

	/* synthetic properties */
	case RHYTHMDB_PROP_SEARCH_MATCH:
		return NULL;
	case RHYTHMDB_PROP_KEYWORD:
		return NULL;

	/* Podcast properties */
	case RHYTHMDB_PROP_DESCRIPTION:
		if (podcast)
			return rb_refstring_get (podcast->description);
		else
			return NULL;
	case RHYTHMDB_PROP_SUBTITLE:
		if (podcast)
			return rb_refstring_get (podcast->subtitle);
		else
			return NULL;
	case RHYTHMDB_PROP_SUMMARY:
		if (podcast)
			return rb_refstring_get (podcast->summary);
		else
			return NULL;
	case RHYTHMDB_PROP_LANG:
		if (podcast)
			return rb_refstring_get (podcast->lang);
		else
			return NULL;
	case RHYTHMDB_PROP_COPYRIGHT:
		if (podcast)
			return rb_refstring_get (podcast->copyright);
		else
			return NULL;
	case RHYTHMDB_PROP_IMAGE:
		if (podcast)
			return rb_refstring_get (podcast->image);
		else
			return NULL;

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

RBRefString *
rhythmdb_entry_get_refstring (RhythmDBEntry *entry,
			      RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (entry->refcount > 0, NULL);

	rhythmdb_entry_sync_mirrored (entry, propid);

	switch (propid) {
	case RHYTHMDB_PROP_TITLE:
		return rb_refstring_ref (entry->title);
	case RHYTHMDB_PROP_ALBUM:
		return rb_refstring_ref (entry->album);
	case RHYTHMDB_PROP_ARTIST:
		return rb_refstring_ref (entry->artist);
	case RHYTHMDB_PROP_GENRE:
		return rb_refstring_ref (entry->genre);
	case RHYTHMDB_PROP_MUSICBRAINZ_TRACKID:
		return rb_refstring_ref (entry->musicbrainz_trackid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID:
		return rb_refstring_ref (entry->musicbrainz_artistid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID:
		return rb_refstring_ref (entry->musicbrainz_albumid);
	case RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID:
		return rb_refstring_ref (entry->musicbrainz_albumartistid);
	case RHYTHMDB_PROP_ARTIST_SORTNAME:
		return rb_refstring_ref (entry->artist_sortname);
	case RHYTHMDB_PROP_ALBUM_SORTNAME:
		return rb_refstring_ref (entry->album_sortname);
	case RHYTHMDB_PROP_MIMETYPE:
		return rb_refstring_ref (entry->mimetype);
	case RHYTHMDB_PROP_MOUNTPOINT:
		return rb_refstring_ref (entry->mountpoint);
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		return rb_refstring_ref (entry->last_played_str);
	case RHYTHMDB_PROP_FIRST_SEEN_STR:
		return rb_refstring_ref (entry->first_seen_str);
	case RHYTHMDB_PROP_LAST_SEEN_STR:
		return rb_refstring_ref (entry->last_seen_str);
	case RHYTHMDB_PROP_LOCATION:
		return rb_refstring_ref (entry->location);
	case RHYTHMDB_PROP_PLAYBACK_ERROR:
		return rb_refstring_ref (entry->playback_error);
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

gboolean
rhythmdb_entry_get_boolean (RhythmDBEntry *entry,
			    RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, FALSE);

	switch (propid) {
	case RHYTHMDB_PROP_HIDDEN:
		return ((entry->flags & RHYTHMDB_ENTRY_HIDDEN) != 0);
	default:
		g_assert_not_reached ();
		return FALSE;
	}
}

guint64
rhythmdb_entry_get_uint64 (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, 0);

	switch (propid) {
	case RHYTHMDB_PROP_FILE_SIZE:
		return entry->file_size;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

RhythmDBEntryType
rhythmdb_entry_get_entry_type (RhythmDBEntry *entry)
{
	g_return_val_if_fail (entry != NULL, RHYTHMDB_ENTRY_TYPE_INVALID);

	return entry->type;
}

gpointer
rhythmdb_entry_get_pointer (RhythmDBEntry *entry,
			    RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, NULL);

	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return entry->type;
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

gulong
rhythmdb_entry_get_ulong (RhythmDBEntry *entry,
			  RhythmDBPropType propid)
{
	RhythmDBPodcastFields *podcast = NULL;

	g_return_val_if_fail (entry != NULL, 0);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);

	switch (propid) {
	case RHYTHMDB_PROP_ENTRY_ID:
		return entry->id;
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return entry->tracknum;
	case RHYTHMDB_PROP_DISC_NUMBER:
		return entry->discnum;
	case RHYTHMDB_PROP_DURATION:
		return entry->duration;
	case RHYTHMDB_PROP_MTIME:
		return entry->mtime;
	case RHYTHMDB_PROP_FIRST_SEEN:
		return entry->first_seen;
	case RHYTHMDB_PROP_LAST_SEEN:
		return entry->last_seen;
	case RHYTHMDB_PROP_LAST_PLAYED:
		return entry->last_played;
	case RHYTHMDB_PROP_PLAY_COUNT:
		return entry->play_count;
	case RHYTHMDB_PROP_BITRATE:
		return entry->bitrate;
	case RHYTHMDB_PROP_DATE:
		if (g_date_valid (&entry->date))
			return g_date_get_julian (&entry->date);
		else
			return 0;
	case RHYTHMDB_PROP_YEAR:
		if (g_date_valid (&entry->date))
			return g_date_get_year (&entry->date);
		else
			return 0;
	case RHYTHMDB_PROP_POST_TIME:
		if (podcast)
			return podcast->post_time;
		else
			return 0;
	case RHYTHMDB_PROP_STATUS:
		if (podcast)
			return podcast->status;
		else
			return 0;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

double
rhythmdb_entry_get_double (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, 0);

	switch (propid) {
	case RHYTHMDB_PROP_TRACK_GAIN:
		return entry->track_gain;
	case RHYTHMDB_PROP_TRACK_PEAK:
		return entry->track_peak;
	case RHYTHMDB_PROP_ALBUM_GAIN:
		return entry->album_gain;
	case RHYTHMDB_PROP_ALBUM_PEAK:
		return entry->album_peak;
	case RHYTHMDB_PROP_RATING:
		return entry->rating;
	default:
		g_assert_not_reached ();
		return 0.0;
	}
}

char *
rhythmdb_entry_get_playback_uri (RhythmDBEntry *entry)
{
	RhythmDBEntryType type;

	g_return_val_if_fail (entry != NULL, NULL);

	type = rhythmdb_entry_get_entry_type (entry);
	if (type->get_playback_uri)
		return (type->get_playback_uri) (entry, type->get_playback_uri_data);
	else
		return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
}


/**
 * rhythmdb_entry_keyword_add:
 * @db: the #RhythmDB
 * @entry: a #RhythmDBEntry.
 * @keyword: the keyword to add.
 *
 * Adds a keyword to an entry.
 *
 * Returns: whether the keyword was already on the entry
 **/
gboolean
rhythmdb_entry_keyword_add	(RhythmDB *db,
				 RhythmDBEntry *entry,
				 RBRefString *keyword)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	gboolean ret;

	ret = klass->impl_entry_keyword_add (db, entry, keyword);
	if (!ret) {
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_KEYWORD_ADDED], 0, entry, keyword);
	}
	return ret;
}

/**
 * rhythmdb_entry_keyword_remove:
 * @db: the #RhythmDB
 * @entry: a #RhythmDBEntry.
 * @keyword: the keyword to remove.
 *
 * Removed a keyword from an entry.
 *
 * Returns: whether the keyword had previously been added to the entry.
 **/
gboolean
rhythmdb_entry_keyword_remove	(RhythmDB *db,
				 RhythmDBEntry *entry,
				 RBRefString *keyword)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	gboolean ret;

	ret = klass->impl_entry_keyword_remove (db, entry, keyword);
	if (ret) {
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_KEYWORD_REMOVED], 0, entry, keyword);
	}
	return ret;
}

/**
 * rhythmdb_entry_keyword_has:
 * @db: the #RhythmDB
 * @entry: a #RhythmDBEntry.
 * @keyword: the keyword to check for.
 *
 * Checks whether a keyword is has been added to an entry.
 *
 * Returns: whether the keyword had been added to the entry.
 **/
gboolean
rhythmdb_entry_keyword_has	(RhythmDB *db,
				 RhythmDBEntry *entry,
				 RBRefString *keyword)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_keyword_has (db, entry, keyword);
}

/**
 * rhythmdb_entry_keywords_get:
 * @db: the #RhythmDB
 * @entry: a #RhythmDBEntry.
 *
 * Gets the list ofkeywords that have been added to an entry.
 *
 * Returns: the list of keywords that have been added to the entry.
 *          The caller is responsible for unref'ing the RBRefStrings and
 *          freeing the list with g_list_free.
 **/
GList* /*<RBRefString>*/
rhythmdb_entry_keywords_get	(RhythmDB *db,
				 RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_keywords_get (db, entry);
}


GType
rhythmdb_get_property_type (RhythmDB *db,
			    guint property_id)
{
	g_assert (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES);
	return rhythmdb_property_type_map[property_id];
}

GType
rhythmdb_entry_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RhythmDBEntry",
						     (GBoxedCopyFunc)rhythmdb_entry_ref,
						     (GBoxedFreeFunc)rhythmdb_entry_unref);
	}

	return type;
}

GType
rhythmdb_entry_type_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RhythmDBEntryType",
						     (GBoxedCopyFunc)rb_copy_function,
						     (GBoxedFreeFunc)rb_null_function);
	}

	return type;
}

gboolean
rhythmdb_entry_is_lossless (RhythmDBEntry *entry)
{
	const char *mime_type;

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
		return FALSE;
       
	/* possible performance improvement here, if it proves necessary:
	 * keep references to the refstrings for all lossless media types here,
	 * and use pointer comparisons rather than string comparisons to check entries.
	 */
	mime_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	return (g_str_equal (mime_type, "audio/x-flac"));
}

