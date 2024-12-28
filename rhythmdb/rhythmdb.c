/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

/**
 * SECTION:rhythmdb
 * @short_description: Rhythmbox database functions
 *
 * RhythmDB is an in-memory database containing #RhythmDBEntry items.  It
 * runs queries represented as #GPtrArray<!-- -->s containing query criteria,
 * feeding the results into #RhythmDBQueryResults implementations such as
 * #RhythmDBQueryModel.  From there, entries are grouped by particular property
 * values to form #RhythmDBPropertyModel<!-- -->s.
 *
 * #RhythmDBEntry contains a fixed set of properties, defined by #RhythmDBPropType,
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


#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rhythmdb-private.h"
#include "rhythmdb-property-model.h"
#include "rb-dialog.h"
#include "rb-string-value-map.h"
#include "rb-async-queue-watch.h"
#include "rb-podcast-entry-types.h"
#include "rb-gst-media-types.h"

#define PROP_ENTRY(p,t,n) { RHYTHMDB_PROP_ ## p, "RHYTHMDB_PROP_" #p "", t, n }

typedef struct _RhythmDBPropertyDef {
	RhythmDBPropType prop_id;
	const char *prop_name;
	GType prop_type;
	const char *elt_name;
} RhythmDBPropertyDef;

static const RhythmDBPropertyDef rhythmdb_properties[] = {
	PROP_ENTRY(TYPE, G_TYPE_OBJECT, "type"),
	PROP_ENTRY(ENTRY_ID, G_TYPE_ULONG, "entry-id"),
	PROP_ENTRY(TITLE, G_TYPE_STRING, "title"),
	PROP_ENTRY(GENRE, G_TYPE_STRING, "genre"),
	PROP_ENTRY(ARTIST, G_TYPE_STRING, "artist"),
	PROP_ENTRY(ALBUM, G_TYPE_STRING, "album"),
	PROP_ENTRY(TRACK_NUMBER, G_TYPE_ULONG, "track-number"),
	PROP_ENTRY(TRACK_TOTAL, G_TYPE_ULONG, "track-total"),
	PROP_ENTRY(DISC_NUMBER, G_TYPE_ULONG, "disc-number"),
	PROP_ENTRY(DISC_TOTAL, G_TYPE_ULONG, "disc-total"),
	PROP_ENTRY(DURATION, G_TYPE_ULONG, "duration"),
	PROP_ENTRY(FILE_SIZE, G_TYPE_UINT64, "file-size"),
	PROP_ENTRY(LOCATION, G_TYPE_STRING, "location"),
	PROP_ENTRY(MOUNTPOINT, G_TYPE_STRING, "mountpoint"),
	PROP_ENTRY(MTIME, G_TYPE_ULONG, "mtime"),
	PROP_ENTRY(FIRST_SEEN, G_TYPE_ULONG, "first-seen"),
	PROP_ENTRY(LAST_SEEN, G_TYPE_ULONG, "last-seen"),
	PROP_ENTRY(RATING, G_TYPE_DOUBLE, "rating"),
	PROP_ENTRY(PLAY_COUNT, G_TYPE_ULONG, "play-count"),
	PROP_ENTRY(LAST_PLAYED, G_TYPE_ULONG, "last-played"),
	PROP_ENTRY(BITRATE, G_TYPE_ULONG, "bitrate"),
	PROP_ENTRY(DATE, G_TYPE_ULONG, "date"),
	PROP_ENTRY(TRACK_GAIN, G_TYPE_DOUBLE, "replaygain-track-gain"),
	PROP_ENTRY(TRACK_PEAK, G_TYPE_DOUBLE, "replaygain-track-peak"),
	PROP_ENTRY(ALBUM_GAIN, G_TYPE_DOUBLE, "replaygain-album-gain"),
	PROP_ENTRY(ALBUM_PEAK, G_TYPE_DOUBLE, "replaygain-album-peak"),
	PROP_ENTRY(MEDIA_TYPE, G_TYPE_STRING, "media-type"),
	PROP_ENTRY(TITLE_SORT_KEY, G_TYPE_STRING, "title-sort-key"),
	PROP_ENTRY(GENRE_SORT_KEY, G_TYPE_STRING, "genre-sort-key"),
	PROP_ENTRY(ARTIST_SORT_KEY, G_TYPE_STRING, "artist-sort-key"),
	PROP_ENTRY(ALBUM_SORT_KEY, G_TYPE_STRING, "album-sort-key"),
	PROP_ENTRY(TITLE_FOLDED, G_TYPE_STRING, "title-folded"),
	PROP_ENTRY(GENRE_FOLDED, G_TYPE_STRING, "genre-folded"),
	PROP_ENTRY(ARTIST_FOLDED, G_TYPE_STRING, "artist-folded"),
	PROP_ENTRY(ALBUM_FOLDED, G_TYPE_STRING, "album-folded"),
	PROP_ENTRY(LAST_PLAYED_STR, G_TYPE_STRING, "last-played-str"),
	PROP_ENTRY(HIDDEN, G_TYPE_BOOLEAN, "hidden"),
	PROP_ENTRY(PLAYBACK_ERROR, G_TYPE_STRING, "playback-error"),
	PROP_ENTRY(FIRST_SEEN_STR, G_TYPE_STRING, "first-seen-str"),
	PROP_ENTRY(LAST_SEEN_STR, G_TYPE_STRING, "last-seen-str"),

	PROP_ENTRY(SEARCH_MATCH, G_TYPE_STRING, "search-match"),
	PROP_ENTRY(YEAR, G_TYPE_ULONG, "year"),
	PROP_ENTRY(KEYWORD, G_TYPE_STRING, "keyword"),

	PROP_ENTRY(STATUS, G_TYPE_ULONG, "status"),
	PROP_ENTRY(DESCRIPTION, G_TYPE_STRING, "description"),
	PROP_ENTRY(SUBTITLE, G_TYPE_STRING, "subtitle"),
	PROP_ENTRY(SUMMARY, G_TYPE_STRING, "summary"),
	PROP_ENTRY(LANG, G_TYPE_STRING, "lang"),
	PROP_ENTRY(COPYRIGHT, G_TYPE_STRING, "copyright"),
	PROP_ENTRY(IMAGE, G_TYPE_STRING, "image"),
	PROP_ENTRY(POST_TIME, G_TYPE_ULONG, "post-time"),
	PROP_ENTRY(PODCAST_GUID, G_TYPE_STRING, "podcast-guid"),

	PROP_ENTRY(MUSICBRAINZ_TRACKID, G_TYPE_STRING, "mb-trackid"),
	PROP_ENTRY(MUSICBRAINZ_ARTISTID, G_TYPE_STRING, "mb-artistid"),
	PROP_ENTRY(MUSICBRAINZ_ALBUMID, G_TYPE_STRING, "mb-albumid"),
	PROP_ENTRY(MUSICBRAINZ_ALBUMARTISTID, G_TYPE_STRING, "mb-albumartistid"),
	PROP_ENTRY(ARTIST_SORTNAME, G_TYPE_STRING, "mb-artistsortname"),
	PROP_ENTRY(ALBUM_SORTNAME, G_TYPE_STRING, "album-sortname"),

	PROP_ENTRY(ARTIST_SORTNAME_SORT_KEY, G_TYPE_STRING, "artist-sortname-sort-key"),
	PROP_ENTRY(ARTIST_SORTNAME_FOLDED, G_TYPE_STRING, "artist-sortname-folded"),
	PROP_ENTRY(ALBUM_SORTNAME_SORT_KEY, G_TYPE_STRING, "album-sortname-sort-key"),
	PROP_ENTRY(ALBUM_SORTNAME_FOLDED, G_TYPE_STRING, "album-sortname-folded"),

	PROP_ENTRY(COMMENT, G_TYPE_STRING, "comment"),

	PROP_ENTRY(ALBUM_ARTIST, G_TYPE_STRING, "album-artist"),
	PROP_ENTRY(ALBUM_ARTIST_SORT_KEY, G_TYPE_STRING, "album-artist-sort-key"),
	PROP_ENTRY(ALBUM_ARTIST_FOLDED, G_TYPE_STRING, "album-artist-folded"),
	PROP_ENTRY(ALBUM_ARTIST_SORTNAME, G_TYPE_STRING, "album-artist-sortname"),
	PROP_ENTRY(ALBUM_ARTIST_SORTNAME_SORT_KEY, G_TYPE_STRING, "album-artist-sortname-sort-key"),
	PROP_ENTRY(ALBUM_ARTIST_SORTNAME_FOLDED, G_TYPE_STRING, "album-artist-sortname-folded"),
	
	PROP_ENTRY(BPM, G_TYPE_DOUBLE, "beats-per-minute"),

	PROP_ENTRY(COMPOSER, G_TYPE_STRING, "composer"),
	PROP_ENTRY(COMPOSER_SORT_KEY, G_TYPE_STRING, "composer-sort-key"),
	PROP_ENTRY(COMPOSER_FOLDED, G_TYPE_STRING, "composer-folded"),
	PROP_ENTRY(COMPOSER_SORTNAME, G_TYPE_STRING, "composer-sortname"),
	PROP_ENTRY(COMPOSER_SORTNAME_SORT_KEY, G_TYPE_STRING, "composer-sortname-sort-key"),
	PROP_ENTRY(COMPOSER_SORTNAME_FOLDED, G_TYPE_STRING, "composer-sortname-folded"),

	{ 0, 0, 0, 0 }
};

#define RB_PARSE_NICK_START (xmlChar *) "["
#define RB_PARSE_NICK_END (xmlChar *) "]"


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
	G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","		\
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
	{ "application/x-annodex", FALSE },
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
	RhythmDBEntryType *type;
	RhythmDBEntryType *ignore_type;
	RhythmDBEntryType *error_type;
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
	union {
		struct {
			RhythmDBEntryType *entry_type;
			RhythmDBEntryType *ignore_type;
			RhythmDBEntryType *error_type;
		} types;
		GSList *changes;
	} data;
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
static GThread * rhythmdb_thread_create (RhythmDB *db,
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
static void db_settings_changed_cb (GSettings *settings, const char *key, RhythmDB *db);
static void rhythmdb_sync_library_location (RhythmDB *db);
static void rhythmdb_entry_sync_mirrored (RhythmDBEntry *entry,
					  guint propid);
static gboolean rhythmdb_entry_extra_metadata_accumulator (GSignalInvocationHint *ihint,
							   GValue *return_accu,
							   const GValue *handler_return,
							   gpointer data);

static void rhythmdb_event_free (RhythmDB *db, RhythmDBEvent *event);
static void rhythmdb_add_to_stat_list (RhythmDB *db,
				       const char *uri,
				       RhythmDBEntry *entry,
				       RhythmDBEntryType *type,
				       RhythmDBEntryType *ignore_type,
				       RhythmDBEntryType *error_type);
static void free_entry_changes (GSList *entry_changes);
static RhythmDBEntry *rhythmdb_add_import_error_entry (RhythmDB *db, RhythmDBEvent *event, RhythmDBEntryType *error_entry_type);

static void perform_next_mount (RhythmDB *db);

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

	/**
	 * RhythmDB:name:
	 *
	 * Database name.  Not sure whta this is used for.
	 */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * RhythmDB:dry-run:
	 *
	 * If %TRUE, no metadata changes will be written back to media fies.
	 */
	g_object_class_install_property (object_class,
					 PROP_DRY_RUN,
					 g_param_spec_boolean ("dry-run",
							       "dry run",
							       "Whether or not changes should be saved",
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * RhythmDB:no-update:
	 *
	 * If %TRUE, the database will not be updated.
	 */
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
			      NULL,
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
			      NULL,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);

	/**
	 * RhythmDB::entry-changed:
	 * @db: the #RhythmDB
	 * @entry: the changed #RhythmDBEntry
	 * @changes: (element-type RB.RhythmDBEntryChange): a #GPtrArray of #RhythmDBEntryChange structures describing the changes
	 *
	 * Emitted when a database entry is modified.  The @changes list
	 * contains a structure for each entry property that has been modified.
	 */
	rhythmdb_signals[ENTRY_CHANGED] =
		g_signal_new ("entry-changed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE, 2,
			      RHYTHMDB_TYPE_ENTRY, G_TYPE_PTR_ARRAY);

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
			      NULL,
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
			      NULL,
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
			      NULL,
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
			      NULL,
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
			      NULL,
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
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RhythmDB::save-complete:
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
			      NULL,
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
			      NULL,
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
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	/**
	 * RhythmDB::create-mount-op:
	 * @db: the #RhythmDB
	 *
	 * Emitted to request creation of a #GMountOperation to use to mount a volume.
	 *
	 * Returns: (transfer full): a #GMountOperation (usually actually a #GtkMountOperation)
	 */
	rhythmdb_signals[CREATE_MOUNT_OP] =
		g_signal_new ("create-mount-op",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,		/* no need for an internal handler */
			      rb_signal_accumulator_object_handled, NULL,
			      NULL,
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
	case RHYTHMDB_PROP_COMMENT:
		*field = RB_METADATA_FIELD_COMMENT;
		return TRUE;
	case RHYTHMDB_PROP_TRACK_NUMBER:
		*field = RB_METADATA_FIELD_TRACK_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_TRACK_TOTAL:
		*field = RB_METADATA_FIELD_MAX_TRACK_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_DISC_NUMBER:
		*field = RB_METADATA_FIELD_DISC_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_DISC_TOTAL:
		*field = RB_METADATA_FIELD_MAX_DISC_NUMBER;
		return TRUE;
	case RHYTHMDB_PROP_DATE:
		*field = RB_METADATA_FIELD_DATE;
		return TRUE;
	case RHYTHMDB_PROP_BPM:
		*field = RB_METADATA_FIELD_BPM;
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
	case RHYTHMDB_PROP_ALBUM_ARTIST:
		*field = RB_METADATA_FIELD_ALBUM_ARTIST;
		return TRUE;
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME:
		*field = RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME;
		return TRUE;
	case RHYTHMDB_PROP_COMPOSER:
		*field = RB_METADATA_FIELD_COMPOSER;
		return TRUE;
	case RHYTHMDB_PROP_COMPOSER_SORTNAME:
		*field = RB_METADATA_FIELD_COMPOSER_SORTNAME;
		return TRUE;
	default:
		return FALSE;
	}
}

static void
rhythmdb_init (RhythmDB *db)
{
	guint i;
	GEnumClass *prop_class;

	db->priv = RHYTHMDB_GET_PRIVATE (db);

	db->priv->settings = g_settings_new ("org.gnome.rhythmbox.rhythmdb");
	g_signal_connect_object (db->priv->settings, "changed", G_CALLBACK (db_settings_changed_cb), db, 0);

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

	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP_TYPE);

	g_assert (prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);

	g_type_class_unref (prop_class);

	db->priv->propname_map = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		const xmlChar *name = rhythmdb_nice_elt_name_from_propid (db, i);
		g_hash_table_insert (db->priv->propname_map, (gpointer) name, GINT_TO_POINTER (i));
	}

	db->priv->entry_type_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	rhythmdb_register_song_entry_types (db);
	rb_podcast_register_entry_types (db);

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

	db->priv->can_save = TRUE;
	db->priv->exiting = g_cancellable_new ();
	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	db->priv->empty_string = rb_refstring_new ("");
	db->priv->octet_stream_str = rb_refstring_new ("application/octet-stream");

	db->priv->next_entry_id = 1;

	rhythmdb_init_monitoring (db);

	rhythmdb_dbus_register (db);
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
	RhythmDB *db;
	GList *stat_list;
} RhythmDBStatThreadData;

static gpointer
stat_thread_main (RhythmDBStatThreadData *data)
{
	GList *i;
	GError *error = NULL;
	RhythmDBEvent *result;

	data->db->priv->stat_thread_count = g_list_length (data->stat_list);
	data->db->priv->stat_thread_done = 0;

	rb_debug ("entering stat thread: %d to process", data->db->priv->stat_thread_count);
	for (i = data->stat_list; i != NULL; i = i->next) {
		RhythmDBEvent *event = (RhythmDBEvent *)i->data;
		GFile *file;

		/* if we've been cancelled, just free the event.  this will
		 * clean up the list and then we'll exit the thread.
		 */
		if (g_cancellable_is_cancelled (data->db->priv->exiting)) {
			rhythmdb_event_free (data->db, event);
			continue;
		}

		if (data->db->priv->stat_thread_done > 0 &&
		    data->db->priv->stat_thread_done % 1000 == 0) {
			rb_debug ("%d file info queries done",
				  data->db->priv->stat_thread_done);
		}

		file = g_file_new_for_uri (rb_refstring_get (event->uri));
		event->real_uri = rb_refstring_ref (event->uri);		/* what? */
		event->file_info = g_file_query_info (file,
						      G_FILE_ATTRIBUTE_TIME_MODIFIED,	/* anything else? */
						      G_FILE_QUERY_INFO_NONE,
						      data->db->priv->exiting,
						      &error);
		if (error != NULL) {
			event->error = make_access_failed_error (rb_refstring_get (event->uri), error);
			g_clear_error (&error);

			if (event->file_info != NULL) {
				g_object_unref (event->file_info);
				event->file_info = NULL;
			}
		}

		g_async_queue_push (data->db->priv->event_queue, event);
		g_object_unref (file);
		g_atomic_int_inc (&data->db->priv->stat_thread_done);
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

static void
perform_next_mount_cb (GObject *file, GAsyncResult *res, RhythmDB *db)
{
	GError *error = NULL;

	g_file_mount_enclosing_volume_finish (G_FILE (file), res, &error);
	if (error != NULL) {
		char *uri;

		uri = g_file_get_uri (G_FILE (file));
		rb_debug ("Unable to mount %s: %s", uri, error->message);
		g_free (uri);
		g_clear_error (&error);
	}
	g_object_unref (file);

	perform_next_mount (db);
}

static void
perform_next_mount (RhythmDB *db)
{
	GList *l;
	char *mountpoint;
	GMountOperation *mount_op = NULL;

	if (db->priv->mount_list == NULL) {
		rb_debug ("finished mounting");
		return;
	}

	l = db->priv->mount_list;
	db->priv->mount_list = db->priv->mount_list->next;
	mountpoint = l->data;
	g_list_free1 (l);

	rb_debug ("mounting %s", (char *)mountpoint);
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[CREATE_MOUNT_OP], 0, &mount_op);
	g_file_mount_enclosing_volume (g_file_new_for_uri (mountpoint),
				       G_MOUNT_MOUNT_NONE,
				       mount_op,
				       db->priv->exiting,
				       (GAsyncReadyCallback) perform_next_mount_cb,
				       db);
}

/**
 * rhythmdb_start_action_thread:
 * @db: the #RhythmDB
 *
 * Starts the #RhythmDB processing thread. Needs to be called during startup.
 */
void
rhythmdb_start_action_thread (RhythmDB *db)
{
	g_mutex_lock (&db->priv->stat_mutex);
	db->priv->action_thread_running = TRUE;
	rhythmdb_thread_create (db, (GThreadFunc) action_thread_main, db);

	if (db->priv->stat_list != NULL) {
		RhythmDBStatThreadData *data;
		data = g_new0 (RhythmDBStatThreadData, 1);
		data->db = g_object_ref (db);
		data->stat_list = db->priv->stat_list;
		db->priv->stat_list = NULL;

		db->priv->stat_thread_running = TRUE;
		rhythmdb_thread_create (db, (GThreadFunc) stat_thread_main, data);
	}

	perform_next_mount (db);

	g_mutex_unlock (&db->priv->stat_mutex);
}

static void
rhythmdb_action_free (RhythmDB *db,
		      RhythmDBAction *action)
{
	rb_refstring_unref (action->uri);
	if (action->type == RHYTHMDB_ACTION_SYNC) {
		free_entry_changes (action->data.changes);
	}
	g_slice_free (RhythmDBAction, action);
}

static void
free_cached_metadata (GArray *metadata)
{
	RhythmDBEntryChange *fields = (RhythmDBEntryChange *)metadata->data;
	int i;

	for (i = 0; i < metadata->len; i++) {
		g_value_unset (&fields[i].new);
	}
	g_free (fields);
	metadata->data = NULL;
	metadata->len = 0;
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
		break;
	case RHYTHMDB_EVENT_ENTRY_SET:
		g_value_unset (&result->change.new);
		break;
	case RHYTHMDB_EVENT_METADATA_CACHE:
		free_cached_metadata(&result->cached_metadata);
		break;
	case RHYTHMDB_EVENT_BARRIER:
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
 */
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

	/* abort all async io operations */
	g_mutex_lock (&db->priv->stat_mutex);
	g_list_foreach (db->priv->outstanding_stats, (GFunc)_shutdown_foreach_swapped, db);
	g_list_free (db->priv->outstanding_stats);
	db->priv->outstanding_stats = NULL;
	g_mutex_unlock (&db->priv->stat_mutex);

	g_clear_handle_id (&db->priv->sync_library_id, g_source_remove);

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
	rhythmdb_dbus_unregister (db);

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

	if (db->priv->exiting != NULL) {
		g_object_unref (db->priv->exiting);
		db->priv->exiting = NULL;
	}

	if (db->priv->settings != NULL) {
		g_object_unref (db->priv->settings);
		db->priv->settings = NULL;
	}

	G_OBJECT_CLASS (rhythmdb_parent_class)->dispose (object);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	rb_debug ("finalizing rhythmdb");
	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	rhythmdb_finalize_monitoring (db);
	g_strfreev (db->priv->library_locations);
	db->priv->library_locations = NULL;

	g_thread_pool_free (db->priv->query_thread_pool, FALSE, TRUE);
	g_async_queue_unref (db->priv->action_queue);
	g_async_queue_unref (db->priv->event_queue);
	g_async_queue_unref (db->priv->restored_queue);
	g_async_queue_unref (db->priv->delayed_write_queue);

	g_list_free (db->priv->stat_list);

	g_hash_table_destroy (db->priv->propname_map);

	g_hash_table_destroy (db->priv->added_entries);
	g_hash_table_destroy (db->priv->deleted_entries);
	g_hash_table_destroy (db->priv->changed_entries);

	rb_refstring_unref (db->priv->empty_string);
	rb_refstring_unref (db->priv->octet_stream_str);

	g_hash_table_destroy (db->priv->entry_type_map);

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

static GThread *
rhythmdb_thread_create (RhythmDB *db,
			GThreadFunc func,
			gpointer data)
{
	g_object_ref (db);
	g_atomic_int_inc (&db->priv->outstanding_threads);
	g_async_queue_ref (db->priv->action_queue);
	g_async_queue_ref (db->priv->event_queue);

	return g_thread_new ("rhythmdb-thread", (GThreadFunc) func, data);
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

	count = g_atomic_int_add (&db->priv->read_counter, 1);
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

	count = g_atomic_int_add (&db->priv->read_counter, -1);
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
rhythmdb_entry_change_free (RhythmDBEntryChange *change)
{
	g_value_unset (&change->old);
	g_value_unset (&change->new);
	g_slice_free (RhythmDBEntryChange, change);
}

static RhythmDBEntryChange *
rhythmdb_entry_change_copy (RhythmDBEntryChange *change)
{
	RhythmDBEntryChange *c = g_slice_new0 (RhythmDBEntryChange);

	c->prop = change->prop;
	g_value_init (&c->old, G_VALUE_TYPE (&change->old));
	g_value_init (&c->new, G_VALUE_TYPE (&change->new));
	g_value_copy (&change->old, &c->old);
	g_value_copy (&change->new, &c->new);
	return c;
}

static void
free_entry_changes (GSList *entry_changes)
{
	GSList *t;
	for (t = entry_changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;
		rhythmdb_entry_change_free (change);
	}
	g_slist_free (entry_changes);
}

static GSList *
copy_entry_changes (GSList *entry_changes)
{
	GSList *r = NULL;
	GSList *t;
	for (t = entry_changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;
		r = g_slist_prepend (r, rhythmdb_entry_change_copy (change));
	}

	return g_slist_reverse (r);
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
	g_mutex_lock (&db->priv->change_mutex);

	added_entries = db->priv->added_entries_to_emit;
	db->priv->added_entries_to_emit = NULL;

	deleted_entries = db->priv->deleted_entries_to_emit;
	db->priv->deleted_entries_to_emit = NULL;

	changed_entries = db->priv->changed_entries_to_emit;
	db->priv->changed_entries_to_emit = NULL;

	db->priv->emit_entry_signals_id = 0;

	g_mutex_unlock (&db->priv->change_mutex);

	/* emit changed entries */
	if (changed_entries != NULL) {
		g_hash_table_iter_init (&iter, changed_entries);
		while (g_hash_table_iter_next (&iter, (gpointer *)&entry, (gpointer *)&entry_changes)) {
			GPtrArray *emit_changes;
			GSList *c;

			emit_changes = g_ptr_array_new_full (g_slist_length (entry_changes), NULL);
			for (c = entry_changes; c != NULL; c = c->next) {
				g_ptr_array_add (emit_changes, c->data); 
			}
			g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, entry, emit_changes);
			g_ptr_array_unref (emit_changes);
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

		/*
		 * current plan:
		 * - only stat things with mountpoint == NULL here
		 * - collect other mountpoints
		 * just before starting action/stat threads:
		 * - find remote mountpoints that aren't mounted, try to mount them
		 * - for local mountpoints that are mounted, add to stat list
		 * - for everything else, hide entries on those mountpoints
		 */
		if (thread == db->priv->load_thread) {
			const char *mountpoint;

			g_mutex_lock (&db->priv->stat_mutex);
			mountpoint = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
			if (mountpoint == NULL) {
				/* entry is on a core filesystem, always check it */
				rhythmdb_add_to_stat_list (db, uri, entry,
							   RHYTHMDB_ENTRY_TYPE_SONG,
							   RHYTHMDB_ENTRY_TYPE_IGNORE,
							   RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
			} else if (rb_string_list_contains (db->priv->active_mounts, mountpoint)) {
				/* mountpoint is mounted - check the file if it's local */
				if (rb_uri_is_local (mountpoint)) {
					rhythmdb_add_to_stat_list (db,
								   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
								   entry,
								   RHYTHMDB_ENTRY_TYPE_SONG,
								   RHYTHMDB_ENTRY_TYPE_IGNORE,
								   RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
				} else {
					rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_MOUNTED);
				}
			} else {
				/* mountpoint is not mounted */
				rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_UNMOUNTED);

				if (rb_string_list_contains (db->priv->mount_list, mountpoint) == FALSE) {
					db->priv->mount_list = g_list_prepend (db->priv->mount_list, g_strdup (mountpoint));
				}
			}
			g_mutex_unlock (&db->priv->stat_mutex);
		}
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
	GSList *existing;
	if (db->priv->changed_entries_to_emit == NULL) {
		/* the value destroy function is just g_slist_free because we
		 * steal the actual change structures to build the value array.
		 */
		db->priv->changed_entries_to_emit = g_hash_table_new_full (NULL,
									   NULL,
									   (GDestroyNotify) rhythmdb_entry_unref,
									   (GDestroyNotify) g_slist_free);
	}

	/* if the entry is already in the change map from a previous commit, add the
	 * new changes to the end of the existing list.
	 */
	existing = g_hash_table_lookup (db->priv->changed_entries_to_emit, entry);
	if (existing != NULL) {
		changes = g_slist_concat (existing, changes);

		/* steal the hash entry so it doesn't free the changes; also means we
		 * don't need to add a reference on the entry.
		 */
		g_hash_table_steal (db->priv->changed_entries_to_emit, entry);
	} else {
		rhythmdb_entry_ref (entry);
	}

	g_hash_table_insert (db->priv->changed_entries_to_emit, entry, changes);
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

			if (!rhythmdb_entry_can_sync_metadata (entry)) {
				g_warning ("trying to sync properties of non-editable file");
				break;
			}

			action = g_slice_new0 (RhythmDBAction);
			action->type = RHYTHMDB_ACTION_SYNC;
			action->uri = rb_refstring_ref (entry->location);
			action->data.changes = copy_entry_changes (changes);
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
	/*
	 * during normal operation, if committing from a worker thread,
	 * wait for changes made on the thread to be processed by the main thread.
	 * this avoids races and ensures the signals emitted are correct.
	 */
	if (db->priv->action_thread_running && !rb_is_main_thread ()) {
		RhythmDBEvent *event;

		event = g_slice_new0 (RhythmDBEvent);
		event->db = db;
		event->type = RHYTHMDB_EVENT_BARRIER;

		g_mutex_lock (&db->priv->barrier_mutex);
		rhythmdb_push_event (db, event);
		while (g_list_find (db->priv->barriers_done, event) == NULL)
			g_cond_wait (&db->priv->barrier_condition, &db->priv->barrier_mutex);
		db->priv->barriers_done = g_list_remove (db->priv->barriers_done, event);
		g_mutex_unlock (&db->priv->barrier_mutex);

		rhythmdb_event_free (db, event);
	}

	g_mutex_lock (&db->priv->change_mutex);

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

	g_mutex_unlock (&db->priv->change_mutex);
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
 */
void
rhythmdb_commit (RhythmDB *db)
{
	rhythmdb_commit_internal (db, TRUE, g_thread_self ());
}

/**
 * rhythmdb_error_quark:
 *
 * Returns the #GQuark used for #RhythmDBError information
 *
 * Return value: error quark
 */
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
 */
RhythmDBEntry *
rhythmdb_entry_allocate (RhythmDB *db,
			 RhythmDBEntryType *type)
{
	RhythmDBEntry *ret;
	guint type_data_size = 0;
	gsize size = sizeof (RhythmDBEntry);

	g_object_get (type, "type-data-size", &type_data_size, NULL);
	if (type_data_size > 0) {
		size = ALIGN_STRUCT (sizeof (RhythmDBEntry)) + type_data_size;
	}
	ret = g_malloc0 (size);
	ret->id = (guint) g_atomic_int_add (&db->priv->next_entry_id, 1);

	ret->type = type;
	ret->title = rb_refstring_ref (db->priv->empty_string);
	ret->genre = rb_refstring_ref (db->priv->empty_string);
	ret->artist = rb_refstring_ref (db->priv->empty_string);
	ret->composer = rb_refstring_ref (db->priv->empty_string);
	ret->album = rb_refstring_ref (db->priv->empty_string);
	ret->comment = rb_refstring_ref (db->priv->empty_string);
	ret->album_artist = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_trackid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_artistid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_albumid = rb_refstring_ref (db->priv->empty_string);
	ret->musicbrainz_albumartistid = rb_refstring_ref (db->priv->empty_string);
	ret->artist_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->composer_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->album_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->album_artist_sortname = rb_refstring_ref (db->priv->empty_string);
	ret->media_type = rb_refstring_ref (db->priv->octet_stream_str);

	ret->flags |= RHYTHMDB_ENTRY_LAST_PLAYED_DIRTY |
		      RHYTHMDB_ENTRY_FIRST_SEEN_DIRTY |
		      RHYTHMDB_ENTRY_LAST_SEEN_DIRTY;

	/* The refcount is initially 0, we want to set it to 1 */
	ret->refcount = 1;

	rhythmdb_entry_created (ret);

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
 * Return value: (transfer none): type-specific data pointer
 */
gpointer
rhythmdb_entry_get_type_data (RhythmDBEntry *entry,
			      guint expected_size)
{
	g_return_val_if_fail (entry != NULL, NULL);
	int type_data_size = 0;
	gsize offset;

	g_object_get (entry->type, "type-data-size", &type_data_size, NULL);

	g_assert (expected_size == type_data_size);
	offset = ALIGN_STRUCT (sizeof (RhythmDBEntry));

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
 */
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
	g_mutex_lock (&db->priv->change_mutex);
	g_hash_table_insert (db->priv->added_entries, entry, g_thread_self ());
	g_mutex_unlock (&db->priv->change_mutex);
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
 * Returns: (transfer none): the newly created #RhythmDBEntry
 */
RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db,
		    RhythmDBEntryType *type,
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
 */
RhythmDBEntry *
rhythmdb_entry_example_new (RhythmDB *db,
			    RhythmDBEntryType *type,
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
 */
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
	rhythmdb_entry_pre_destroy (entry);

	rb_refstring_unref (entry->location);
	rb_refstring_unref (entry->playback_error);
	rb_refstring_unref (entry->title);
	rb_refstring_unref (entry->genre);
	rb_refstring_unref (entry->artist);
	rb_refstring_unref (entry->composer);
	rb_refstring_unref (entry->album);
	rb_refstring_unref (entry->comment);
	rb_refstring_unref (entry->musicbrainz_trackid);
	rb_refstring_unref (entry->musicbrainz_artistid);
	rb_refstring_unref (entry->musicbrainz_albumid);
	rb_refstring_unref (entry->musicbrainz_albumartistid);
	rb_refstring_unref (entry->artist_sortname);
	rb_refstring_unref (entry->composer_sortname);
	rb_refstring_unref (entry->album_sortname);
	rb_refstring_unref (entry->media_type);

	g_free (entry);
}

/**
 * rhythmdb_entry_unref:
 * @entry: a #RhythmDBEntry.
 *
 * Decrease the reference count of the entry, and destroys it if there are
 * no references left.
 */
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

static void
set_metadata_string_with_default (RhythmDB *db,
				  RBMetaData *metadata,
				  RhythmDBEntry *entry,
				  RBMetaDataField field,
				  RhythmDBPropType prop,
				  const char *default_value)
{
	GValue val = {0, };

	if (!(rb_metadata_get (metadata,
			       field,
			       &val))) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_static_string (&val, default_value);
	} else {
                const gchar *str = g_value_get_string (&val);
                if (str == NULL || str[0] == '\0')
			g_value_set_static_string (&val, default_value);
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
	const char *media_type;
	GValue val = {0,};

	g_value_init (&val, G_TYPE_STRING);
	media_type = rb_metadata_get_media_type (metadata);
	if (media_type) {
		g_value_set_string (&val, media_type);
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_MEDIA_TYPE, &val);
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

	/* track total */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_MAX_TRACK_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
	}
	rhythmdb_entry_set_internal (db, entry, TRUE,
				     RHYTHMDB_PROP_TRACK_TOTAL, &val);
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

	/* disc total */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_MAX_DISC_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
	}
	rhythmdb_entry_set_internal (db, entry, TRUE,
				     RHYTHMDB_PROP_DISC_TOTAL, &val);
	g_value_unset (&val);

	/* duration */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_DURATION,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	/* bitrate (only set for non-lossless media types) */
	if (!rb_gst_media_type_is_lossless (media_type)) {
		if (rb_metadata_get (metadata,
				     RB_METADATA_FIELD_BITRATE,
				     &val)) {
			rhythmdb_entry_set_internal (db, entry, TRUE,
						     RHYTHMDB_PROP_BITRATE, &val);
			g_value_unset (&val);
		}
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
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_MUSICBRAINZ_TRACKID,
					  RHYTHMDB_PROP_MUSICBRAINZ_TRACKID,
					  "");

	/* musicbrainz artistid */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID,
					  RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID,
					  "");

	/* musicbrainz albumid */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID,
					  RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID,
					  "");

	/* musicbrainz albumartistid */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID,
					  RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID,
					  "");

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
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_GENRE,
					  RHYTHMDB_PROP_GENRE,
					  _("Unknown"));

	/* artist */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ARTIST,
					  RHYTHMDB_PROP_ARTIST,
					  _("Unknown"));

	/* beats per minute */
	if (rb_metadata_get (metadata,
			     RB_METADATA_FIELD_BPM,
			     &val)) {
		rhythmdb_entry_set_internal (db, entry, TRUE,
					     RHYTHMDB_PROP_BPM, &val);
		g_value_unset (&val);
	}

	/* album */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ALBUM,
					  RHYTHMDB_PROP_ALBUM,
					  _("Unknown"));
	/* artist sortname */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ARTIST_SORTNAME,
					  RHYTHMDB_PROP_ARTIST_SORTNAME,
					  "");

	/* album sortname */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ALBUM_SORTNAME,
					  RHYTHMDB_PROP_ALBUM_SORTNAME,
					  "");

	/* comment */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_COMMENT,
					  RHYTHMDB_PROP_COMMENT,
					  "");
	/* album artist */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ALBUM_ARTIST,
					  RHYTHMDB_PROP_ALBUM_ARTIST,
					  "");

	/* album artist sortname */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME,
					  RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME,
					  "");

	/* composer */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_COMPOSER,
					  RHYTHMDB_PROP_COMPOSER,
					  _("Unknown"));

	/* composer sortname */
	set_metadata_string_with_default (db, metadata, entry,
					  RB_METADATA_FIELD_COMPOSER_SORTNAME,
					  RHYTHMDB_PROP_COMPOSER_SORTNAME,
					  "");
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
			rb_debug ("error accessing %s: %s",
				  rb_refstring_get (event->real_uri),
				  event->error->message);
			rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_NOT_FOUND);
			rhythmdb_commit (db);
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
			guint64 new_mtime;
			guint64 new_size;

			/* update the existing entry, as long as the entry type matches */
			if ((event->entry_type != NULL) &&
			    (entry->type != event->entry_type) &&
			    (entry->type != event->ignore_type) &&
			    (entry->type != event->error_type)) {
				if (event->entry_type == RHYTHMDB_ENTRY_TYPE_SONG &&
				    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
					rb_debug ("Ignoring stat event for '%s', it's already loaded as a podcast",
						  rb_refstring_get (event->real_uri));
					break;
				}
				g_warning ("attempt to use location %s in multiple entry types (%s and %s)",
					   rb_refstring_get (event->real_uri),
					   rhythmdb_entry_type_get_name (event->entry_type),
					   rhythmdb_entry_type_get_name (entry->type));
			}

			if (entry->type == event->ignore_type)
				rb_debug ("ignoring %p", entry);

			rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_CHECKED);

			/* compare modification time and size to the values in the database.
			 * if either has changed, we'll re-read the file.
			 */
			new_mtime = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
			new_size = g_file_info_get_attribute_uint64 (event->file_info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
			if (entry->mtime == new_mtime && (new_size == 0 || entry->file_size == new_size)) {
				rb_debug ("not modified: %s", rb_refstring_get (event->real_uri));
			} else {
				rb_debug ("changed: %s", rb_refstring_get (event->real_uri));
				action = g_slice_new0 (RhythmDBAction);
				action->type = RHYTHMDB_ACTION_LOAD;
				action->uri = rb_refstring_ref (event->real_uri);
				action->data.types.entry_type = event->entry_type;
				action->data.types.ignore_type = event->ignore_type;
				action->data.types.error_type = event->error_type;
				g_async_queue_push (db->priv->action_queue, action);
			}
		} else {
			/* push a LOAD action */
			action = g_slice_new0 (RhythmDBAction);
			action->type = RHYTHMDB_ACTION_LOAD;
			action->uri = rb_refstring_ref (event->real_uri);
			action->data.types.entry_type = event->entry_type;
			action->data.types.ignore_type = event->ignore_type;
			action->data.types.error_type = event->error_type;
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
		action->data.types.entry_type = event->entry_type;
		action->data.types.ignore_type = event->ignore_type;
		action->data.types.error_type = event->error_type;
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
		rhythmdb_add_import_error_entry (db, event, event->ignore_type);
		break;
	}

	rhythmdb_commit (db);
}

static RhythmDBEntry *
create_blank_entry (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	GTimeVal time;
	GValue value = {0,};

	entry = rhythmdb_entry_new (db, event->entry_type, rb_refstring_get (event->real_uri));
	if (entry == NULL) {
		rb_debug ("entry already exists");
		return NULL;
	}

	/* initialize the last played date to 0=never */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, 0);
	rhythmdb_entry_set (db, entry,
			    RHYTHMDB_PROP_LAST_PLAYED, &value);
	g_value_unset (&value);

	/* initialize the rating */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, 0);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);

	/* first seen */
	g_get_current_time (&time);
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, time.tv_sec);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FIRST_SEEN, &value);
	g_value_unset (&value);

	return entry;
}

static void
apply_mtime (RhythmDB *db, RhythmDBEntry *entry, GFileInfo *file_info)
{
	guint64 mtime;
	GValue value = {0,};

	if (file_info == NULL) {
		return;
	}

	mtime = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, (gulong)mtime);
	rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_MTIME, &value);
	g_value_unset (&value);
}


static RhythmDBEntry *
rhythmdb_add_import_error_entry (RhythmDB *db,
				 RhythmDBEvent *event,
				 RhythmDBEntryType *error_entry_type)
{
	RhythmDBEntry *entry;
	GValue value = {0,};

	if (error_entry_type == NULL) {
		/* we don't have an error entry type, so we can't add an import error */
		return NULL;
	}
	rb_debug ("adding import error type %s for %s: %s",
		  rhythmdb_entry_type_get_name (error_entry_type),
		  rb_refstring_get (event->real_uri),
		  event->error ? event->error->message : "<no error>");

	entry = rhythmdb_entry_lookup_by_location_refstring (db, event->real_uri);
	if (entry) {
		RhythmDBEntryType *entry_type = rhythmdb_entry_get_entry_type (entry);
		if (entry_type != event->error_type &&
		    entry_type != event->ignore_type) {
			/* FIXME we've successfully read this file before.. so what should we do? */
			rb_debug ("%s already exists in the library.. ignoring import error?", rb_refstring_get (event->real_uri));
			return NULL;
		}

		if (entry_type != error_entry_type) {
			/* delete the existing entry, then create a new one below */
			rhythmdb_entry_delete (db, entry);
			entry = NULL;
		} else if (error_entry_type == event->error_type && event->error) {
			/* we've already got an error for this file, so just update it */
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, event->error->message);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
			g_value_unset (&value);
		} else {
			/* no need to update the ignored file entry */
		}

		if (entry) {
			apply_mtime (db, entry, event->file_info);
		}

		rhythmdb_add_timeout_commit (db, FALSE);
	}

	if (entry == NULL) {
		/* create a new import error or ignore entry */
		entry = rhythmdb_entry_new (db, error_entry_type, rb_refstring_get (event->real_uri));
		if (entry == NULL)
			return NULL;

		/* if we have missing plugin details, store them in the
		 * comment field so we can collect them later, and set a
		 * suitable error message
		 */
		if (event->metadata != NULL && rb_metadata_has_missing_plugins (event->metadata)) {
			char **missing_plugins;
			char **plugin_descriptions;
			char *comment;
			char *list;
			const char *msg;

			/* Translators: the parameter here is a list of GStreamer plugins.
			 * The plugin names are already translated.
			 */
			msg = _("Additional GStreamer plugins are required to play this file: %s");

			if (rb_metadata_has_audio (event->metadata) == TRUE &&
				   rb_metadata_has_video (event->metadata) == FALSE &&
				   rb_metadata_has_missing_plugins (event->metadata) == TRUE) {
				rb_metadata_get_missing_plugins (event->metadata, &missing_plugins, &plugin_descriptions);
				comment = g_strjoinv ("\n", missing_plugins);
				rb_debug ("storing missing plugin details: %s", comment);

				g_value_init (&value, G_TYPE_STRING);
				g_value_take_string (&value, comment);
				rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_COMMENT, &value);
				g_value_unset (&value);

				g_value_init (&value, G_TYPE_STRING);
				list = g_strjoinv (", ", plugin_descriptions);
				g_value_take_string (&value, g_strdup_printf (msg, list));
				g_free (list);
				rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
				g_value_unset (&value);

				g_strfreev (missing_plugins);
				g_strfreev (plugin_descriptions);

			} else if (rb_metadata_has_missing_plugins (event->metadata)) {
				rb_debug ("ignoring missing plugins for non-audio file");
			}
		} else if (error_entry_type == event->error_type && event->error && event->error->message) {
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

	return entry;
}

static gboolean
rhythmdb_process_metadata_cache (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	RhythmDBEntryChange *fields;
	gboolean monitor;
	int i;

	fields = (RhythmDBEntryChange *)event->cached_metadata.data;
	for (i = 0; i < event->cached_metadata.len; i++) {
		if (fields[i].prop == RHYTHMDB_PROP_MEDIA_TYPE) {
			const char *media_type;
			media_type = g_value_get_string (&fields[i].new);
			/* if no media type is set, it's an ignore entry */
			if (g_strcmp0 (media_type, "application/octet-stream") == 0) {
				rhythmdb_add_import_error_entry (db, event, event->ignore_type);
				return TRUE;
			}
			break;
		}
	}

	entry = rhythmdb_entry_lookup_by_location_refstring (db, event->real_uri);
	if (entry == NULL) {
		entry = create_blank_entry (db, event);
		if (entry == NULL) {
			return TRUE;
		}
	}

	apply_mtime (db, entry, event->file_info);

	rhythmdb_entry_apply_cached_metadata (entry, &event->cached_metadata);

	rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_CHECKED);

	/* Remember the mount point of the volume the song is on */
	rhythmdb_entry_set_mount_point (db, entry, rb_refstring_get (event->real_uri));

	/* monitor the file for changes */
	/* FIXME: watch for errors */
	monitor = g_settings_get_boolean (db->priv->settings, "monitor-library");
	if (monitor && event->entry_type == RHYTHMDB_ENTRY_TYPE_SONG)
		rhythmdb_monitor_uri_path (db, rb_refstring_get (entry->location), NULL);

	rhythmdb_commit_internal (db, FALSE, g_thread_self ());

	return TRUE;
}

static gboolean
rhythmdb_process_metadata_load (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	GTimeVal time;
	gboolean monitor;

	entry = NULL;

	if (event->entry_type == NULL)
		event->entry_type = RHYTHMDB_ENTRY_TYPE_SONG;

	if (event->metadata != NULL) {
		/* always ignore anything with video in it */
		if (rb_metadata_has_video (event->metadata)) {
			entry = rhythmdb_add_import_error_entry (db, event, event->ignore_type);
		}

		/* if we identified the media type, we can ignore anything
		 * that matches one of the media types we don't care about,
		 * as well as anything that doesn't contain audio.
		 */
		const char *media_type = rb_metadata_get_media_type (event->metadata);
		if (entry == NULL && media_type != NULL && media_type[0] != '\0') {
			if (rhythmdb_ignore_media_type (media_type) ||
			    rb_metadata_has_audio (event->metadata) == FALSE) {
				entry = rhythmdb_add_import_error_entry (db, event, event->ignore_type);
			}
		}

		if (entry != NULL) {
			rhythmdb_entry_cache_metadata (entry);
			return TRUE;
		}
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
			entry = rhythmdb_add_import_error_entry (db, event, event->ignore_type);
			if (entry != NULL) {
				rhythmdb_entry_cache_metadata (entry);
			}
			return TRUE;
		}
	}

	if (event->error) {
		rhythmdb_add_import_error_entry (db, event, event->error_type);
		return TRUE;
	}

	g_get_current_time (&time);

	entry = rhythmdb_entry_lookup_by_location_refstring (db, event->real_uri);

	if (entry != NULL) {
		RhythmDBEntryType *etype;
		etype = rhythmdb_entry_get_entry_type (entry);
		if (etype == event->error_type || etype == event->ignore_type) {
			/* switching from IGNORE/ERROR to SONG, recreate the entry */
			rhythmdb_entry_delete (db, entry);
			rhythmdb_add_timeout_commit (db, FALSE);
			entry = NULL;
		}
	}

	if (entry == NULL) {
		entry = create_blank_entry (db, event);
		if (entry == NULL) {
			rb_debug ("entry already exists");
			return TRUE;
		}
	}

	if ((event->entry_type != NULL) && (entry->type != event->entry_type)) {
		g_warning ("attempt to use same location in multiple entry types");
		return TRUE;
	}

	apply_mtime (db, entry, event->file_info);

	if (event->entry_type != event->ignore_type &&
	    event->entry_type != event->error_type) {
		set_props_from_metadata (db, entry, event->file_info, event->metadata);
	}

	rhythmdb_entry_cache_metadata (entry);

	rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_CHECKED);

	/* Remember the mount point of the volume the song is on */
	rhythmdb_entry_set_mount_point (db, entry, rb_refstring_get (event->real_uri));

	/* monitor the file for changes */
	/* FIXME: watch for errors */
	monitor = g_settings_get_boolean (db->priv->settings, "monitor-library");
	if (monitor && event->entry_type == RHYTHMDB_ENTRY_TYPE_SONG)
		rhythmdb_monitor_uri_path (db, rb_refstring_get (entry->location), NULL);

	rhythmdb_commit_internal (db, FALSE, g_thread_self ());

	return TRUE;
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
	     || (event->type == RHYTHMDB_EVENT_METADATA_CACHE)
	     || (event->type == RHYTHMDB_EVENT_ENTRY_SET)
	     || (event->type == RHYTHMDB_EVENT_BARRIER))) {
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
	case RHYTHMDB_EVENT_METADATA_CACHE:
		rb_debug ("processing RHTHMDB_EVENT_METADATA_CACHE");
		free = rhythmdb_process_metadata_cache (db, event);
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
	case RHYTHMDB_EVENT_BARRIER:
		rb_debug ("processing RHYTHMDB_EVENT_BARRIER");
		g_mutex_lock (&db->priv->barrier_mutex);
		db->priv->barriers_done = g_list_prepend (db->priv->barriers_done, event);
		g_cond_broadcast (&db->priv->barrier_condition);
		g_mutex_unlock (&db->priv->barrier_mutex);

		/* freed by the thread waiting on the barrier */
		free = FALSE;
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
		g_error_free (error);

		g_object_unref (event->file_info);
		event->file_info = NULL;
	} else {
		rhythmdb_file_info_query (event->db, G_FILE (source), event);
	}

	g_mutex_lock (&event->db->priv->stat_mutex);
	event->db->priv->outstanding_stats = g_list_remove (event->db->priv->outstanding_stats, event);
	g_mutex_unlock (&event->db->priv->stat_mutex);

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

	g_mutex_lock (&db->priv->stat_mutex);
	db->priv->outstanding_stats = g_list_prepend (db->priv->outstanding_stats, event);
	g_mutex_unlock (&db->priv->stat_mutex);

	rhythmdb_file_info_query (db, file, event);

	if (event->error != NULL) {
		/* if we can't get at it because the location isn't mounted, mount it and try again */
	       	if (g_error_matches (event->error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
			GMountOperation *mount_op = NULL;

			g_error_free (event->error);
			event->error = NULL;

			g_signal_emit (G_OBJECT (event->db), rhythmdb_signals[CREATE_MOUNT_OP], 0, &mount_op);
			if (mount_op != NULL) {
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

	g_mutex_lock (&event->db->priv->stat_mutex);
	event->db->priv->outstanding_stats = g_list_remove (event->db->priv->outstanding_stats, event);
	g_mutex_unlock (&event->db->priv->stat_mutex);

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
	} else {
		gboolean valid;

		valid = FALSE;
		if (rhythmdb_entry_type_fetch_metadata (event->entry_type, uri, &event->cached_metadata)) {
			RhythmDBEntryChange *fields = (RhythmDBEntryChange *)event->cached_metadata.data;
			guint64 new_filesize;
			guint64 new_mtime;
			int i;

			valid = TRUE;
			new_filesize = g_file_info_get_attribute_uint64 (event->file_info,
									 G_FILE_ATTRIBUTE_STANDARD_SIZE);
			new_mtime = g_file_info_get_attribute_uint64 (event->file_info,
								     G_FILE_ATTRIBUTE_TIME_MODIFIED);
			for (i = 0; i < event->cached_metadata.len; i++) {
				switch (fields[i].prop) {
				case RHYTHMDB_PROP_MTIME:
					if (new_mtime != g_value_get_ulong (&fields[i].new)) {
						rb_debug ("mtime mismatch, ignoring cached metadata");
						valid = FALSE;
					}
					break;
				case RHYTHMDB_PROP_FILE_SIZE:
					if (new_filesize != g_value_get_uint64 (&fields[i].new)) {
						rb_debug ("size mismatch, ignoring cached metadata");
						valid = FALSE;
					}
					break;
				default:
					break;
				}
			}

			if (valid) {
				event->type = RHYTHMDB_EVENT_METADATA_CACHE;
				rb_debug ("got valid cached metadata");
			} else {
				free_cached_metadata(&event->cached_metadata);
			}
		}

		if (valid == FALSE) {
			event->metadata = rb_metadata_new ();
			rb_metadata_load (event->metadata,
					  rb_refstring_get (event->real_uri),
					  &event->error);
		}
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

		if (g_file_info_get_attribute_boolean (file_info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN)) {
			rb_debug ("ignoring hidden file %s", g_file_info_get_name (file_info));
			g_object_unref (file_info);
			continue;
		}

		child = g_file_get_child (dir, g_file_info_get_name (file_info));
		child_uri = g_file_get_uri (child);

		result = g_slice_new0 (RhythmDBEvent);
		result->db = db;
		result->type = RHYTHMDB_EVENT_STAT;
		result->entry_type = action->data.types.entry_type;
		result->error_type = action->data.types.error_type;
		result->ignore_type = action->data.types.ignore_type;
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
 */
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
	switch (rhythmdb_properties[propid].prop_type) {
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
	case G_TYPE_OBJECT:
		g_value_set_object (val, rhythmdb_entry_get_object (entry, propid));
		break;
	default:
		g_assert_not_reached ();
		break;
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
				result->entry_type = action->data.types.entry_type;
				result->error_type = action->data.types.error_type;
				result->ignore_type = action->data.types.ignore_type;

				rb_debug ("executing RHYTHMDB_ACTION_STAT for \"%s\"", rb_refstring_get (action->uri));

				rhythmdb_execute_stat (db, rb_refstring_get (action->uri), result);
				break;

			case RHYTHMDB_ACTION_LOAD:
				result = g_slice_new0 (RhythmDBEvent);
				result->db = db;
				result->type = RHYTHMDB_EVENT_METADATA_LOAD;
				result->entry_type = action->data.types.entry_type;
				result->error_type = action->data.types.error_type;
				result->ignore_type = action->data.types.ignore_type;

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

				if (db->priv->dry_run) {
					rb_debug ("dry run is enabled, not syncing metadata");
					break;
				}

				entry = rhythmdb_entry_lookup_by_location_refstring (db, action->uri);
				if (!entry)
					break;

				rhythmdb_entry_sync_metadata (entry, action->data.changes, &error);

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
 */
void
rhythmdb_add_uri (RhythmDB *db,
		  const char *uri)
{
	rhythmdb_add_uri_with_types (db,
				     uri,
				     RHYTHMDB_ENTRY_TYPE_SONG,
				     RHYTHMDB_ENTRY_TYPE_IGNORE,
				     RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
}

static void
rhythmdb_add_to_stat_list (RhythmDB *db,
			   const char *uri,
			   RhythmDBEntry *entry,
			   RhythmDBEntryType *type,
			   RhythmDBEntryType *ignore_type,
			   RhythmDBEntryType *error_type)
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
			     RhythmDBEntryType *type,
			     RhythmDBEntryType *ignore_type,
			     RhythmDBEntryType *error_type)
{
	RhythmDBEntry *entry;

	rb_debug ("queueing stat for \"%s\"", uri);
	g_assert (uri && *uri);

	/* keep this outside the stat mutex, as there are other code
	 * paths that take the stat mutex while already holding the
	 * entry mutex.
	 */
	entry = rhythmdb_entry_lookup_by_location (db, uri);

	/*
	 * before the action thread is started, we queue up stat actions,
	 * as we're still creating and running queries, as well as loading
	 * the database.  when we start the action thread, we'll kick off
	 * a thread to process all the stat events too.
	 *
	 * when the action thread is already running, stat actions go through
	 * the normal action queue and are processed by the action thread.
	 */
	g_mutex_lock (&db->priv->stat_mutex);
	if (db->priv->action_thread_running) {
		RhythmDBAction *action;
		g_mutex_unlock (&db->priv->stat_mutex);

		action = g_slice_new0 (RhythmDBAction);
		action->type = RHYTHMDB_ACTION_STAT;
		action->uri = rb_refstring_new (uri);
		action->data.types.entry_type = type;
		action->data.types.ignore_type = ignore_type;
		action->data.types.error_type = error_type;

		g_async_queue_push (db->priv->action_queue, action);
	} else {
		rhythmdb_add_to_stat_list (db, uri, entry, type, ignore_type, error_type);
		g_mutex_unlock (&db->priv->stat_mutex);
	}
}


static gboolean
rhythmdb_sync_library_idle (RhythmDB *db)
{
	rhythmdb_sync_library_location (db);
	db->priv->sync_library_id = 0;
	return FALSE;
}

static gboolean
rhythmdb_load_error_cb (GError *error)
{
	rb_error_dialog (NULL,
			 _("Could not load the music database:"),
			 "%s", error->message);
	g_error_free (error);
	return FALSE;
}

static gpointer
rhythmdb_load_thread_main (RhythmDB *db)
{
	RhythmDBEvent *result;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GError *error = NULL;

	db->priv->active_mounts = rhythmdb_get_active_mounts (db);

	rb_profile_start ("loading db");
	g_mutex_lock (&db->priv->saving_mutex);
	if (klass->impl_load (db, db->priv->exiting, &error) == FALSE) {
		rb_debug ("db load failed: disabling saving");
		db->priv->can_save = FALSE;

		if (error) {
			g_idle_add ((GSourceFunc) rhythmdb_load_error_cb, error);
		}
	}
	g_mutex_unlock (&db->priv->saving_mutex);

	rb_list_deep_free (db->priv->active_mounts);
	db->priv->active_mounts = NULL;

	db->priv->load_thread = NULL;

	db->priv->sync_library_id = g_timeout_add_seconds (10, (GSourceFunc) rhythmdb_sync_library_idle, db);

	rb_debug ("queuing db load complete signal");
	result = g_slice_new0 (RhythmDBEvent);
	result->type = RHYTHMDB_EVENT_DB_LOAD;
	g_async_queue_push (db->priv->event_queue, result);

	rb_debug ("exiting");
	result = g_slice_new0 (RhythmDBEvent);
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	rhythmdb_push_event (db, result);

	return NULL;
}

/**
 * rhythmdb_load:
 * @db: a #RhythmDB.
 *
 * Load the database from disk.
 */
void
rhythmdb_load (RhythmDB *db)
{
	db->priv->load_thread = rhythmdb_thread_create (db, (GThreadFunc) rhythmdb_load_thread_main, db);
}

static gpointer
rhythmdb_save_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass;
	RhythmDBEvent *result;

	rb_debug ("entering save thread");

	g_mutex_lock (&db->priv->saving_mutex);

	db->priv->save_count++;
	g_cond_broadcast (&db->priv->saving_condition);

	if (!(db->priv->dirty && db->priv->can_save)) {
		rb_debug ("no save needed, ignoring");
		g_mutex_unlock (&db->priv->saving_mutex);
		goto out;
	}

	while (db->priv->saving)
		g_cond_wait (&db->priv->saving_condition, &db->priv->saving_mutex);

	db->priv->saving = TRUE;

	rb_debug ("saving rhythmdb");

	klass = RHYTHMDB_GET_CLASS (db);
	klass->impl_save (db);

	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	g_mutex_unlock (&db->priv->saving_mutex);

	g_cond_broadcast (&db->priv->saving_condition);

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
 */
void
rhythmdb_save_async (RhythmDB *db)
{
	rb_debug ("saving the rhythmdb in the background");

	rhythmdb_read_enter (db);

	rhythmdb_thread_create (db, (GThreadFunc) rhythmdb_save_thread_main, db);
}

/**
 * rhythmdb_save:
 * @db: a #RhythmDB.
 *
 * Save the database to disk, not returning until it has been saved.
 */
void
rhythmdb_save (RhythmDB *db)
{
	int new_save_count;

	rb_debug("saving the rhythmdb and blocking");

	g_mutex_lock (&db->priv->saving_mutex);
	new_save_count = db->priv->save_count + 1;

	rhythmdb_save_async (db);

	/* wait until this save request is being processed */
	while (db->priv->save_count < new_save_count) {
		g_cond_wait (&db->priv->saving_condition, &db->priv->saving_mutex);
	}

	/* wait until it's done */
	while (db->priv->saving) {
		g_cond_wait (&db->priv->saving_condition, &db->priv->saving_mutex);
	}

	rb_debug ("done");

	g_mutex_unlock (&db->priv->saving_mutex);
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
 */
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

	g_mutex_lock (&db->priv->change_mutex);
	/* ref the entry before adding to hash, it is unreffed when removed */
	rhythmdb_entry_ref (entry);
	changelist = g_hash_table_lookup (db->priv->changed_entries, entry);
	changelist = g_slist_append (changelist, changedata);
	g_hash_table_insert (db->priv->changed_entries, entry, changelist);
	g_mutex_unlock (&db->priv->change_mutex);
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
	GValue conv_value = {0,};
	GValue old_value = {0,};
	gboolean nop;

	g_return_if_fail (entry != NULL);

	/* convert the value if necessary */
	if (G_VALUE_TYPE (value) != rhythmdb_get_property_type (db, propid)) {
		g_value_init (&conv_value, rhythmdb_get_property_type (db, propid));
		if (g_value_transform (value, &conv_value) == FALSE) {
			g_warning ("Unable to convert new value for property %s from %s to %s",
				   rhythmdb_nice_elt_name_from_propid (db, propid),
				   g_type_name (G_VALUE_TYPE (value)),
				   g_type_name (rhythmdb_get_property_type (db, propid)));
			g_assert_not_reached ();
		}
		value = &conv_value;
	}

	/* compare the value with what's already there */
	g_value_init (&old_value, G_VALUE_TYPE (value));
	rhythmdb_entry_get (db, entry, propid, &old_value);
	switch (G_VALUE_TYPE (value)) {
	case G_TYPE_STRING:
		/* some properties are allowed to be NULL */
		switch (propid) {
		case RHYTHMDB_PROP_PLAYBACK_ERROR:
		case RHYTHMDB_PROP_MOUNTPOINT:
			break;
		default:
			g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
			break;
		}
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
	case G_TYPE_OBJECT:
		nop = (g_value_get_object (value) == g_value_get_object (&old_value));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	if (nop == FALSE && (entry->flags & RHYTHMDB_ENTRY_INSERTED) && notify_if_inserted) {
		record_entry_change (db, entry, propid, &old_value, value);
	}
	g_value_unset (&old_value);

	if (nop) {
		if (value == &conv_value) {
			g_value_unset (&conv_value);
		}
		return;
	}

	handled = klass->impl_entry_set (db, entry, propid, value);

	if (!handled) {
		if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
		    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST ||
		    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH)
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
		case RHYTHMDB_PROP_COMMENT:
			if (entry->comment != NULL) {
				rb_refstring_unref (entry->comment);
			}
			entry->comment = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_TRACK_NUMBER:
			entry->tracknum = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_TRACK_TOTAL:
			entry->tracktotal = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DISC_NUMBER:
			entry->discnum = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DISC_TOTAL:
			entry->disctotal = g_value_get_ulong (value);
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
			g_warning ("RHYTHMDB_PROP_TRACK_GAIN no longer supported");
			break;
		case RHYTHMDB_PROP_TRACK_PEAK:
			g_warning ("RHYTHMDB_PROP_TRACK_PEAK no longer supported");
			break;
		case RHYTHMDB_PROP_ALBUM_GAIN:
			g_warning ("RHYTHMDB_PROP_ALBUM_GAIN no longer supported");
			break;
		case RHYTHMDB_PROP_ALBUM_PEAK:
			g_warning ("RHYTHMDB_PROP_ALBUM_PEAK no longer supported");
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
				entry->mountpoint = NULL;
			}
			if (g_value_get_string (value) != NULL) {
				entry->mountpoint = rb_refstring_new (g_value_get_string (value));
			}
			break;
		case RHYTHMDB_PROP_FILE_SIZE:
			entry->file_size = g_value_get_uint64 (value);
			break;
		case RHYTHMDB_PROP_MEDIA_TYPE:
			if (entry->media_type != NULL) {
				rb_refstring_unref (entry->media_type);
			}
			entry->media_type = rb_refstring_new (g_value_get_string (value));
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
		case RHYTHMDB_PROP_BPM:
			entry->bpm = g_value_get_double (value);
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
		case RHYTHMDB_PROP_ALBUM_ARTIST:
			rb_refstring_unref (entry->album_artist);
			entry->album_artist = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME:
			rb_refstring_unref (entry->album_artist_sortname);
			entry->album_artist_sortname = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_COMPOSER:
			rb_refstring_unref (entry->composer);
			entry->composer = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_COMPOSER_SORTNAME:
			rb_refstring_unref (entry->composer_sortname);
			entry->composer_sortname = rb_refstring_new (g_value_get_string (value));
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
			g_assert_not_reached ();
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
		case RHYTHMDB_PROP_PODCAST_GUID:
			g_assert (podcast);
			if (podcast->guid != NULL)
				rb_refstring_unref (podcast->guid);
			podcast->guid = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_NUM_PROPERTIES:
			g_assert_not_reached ();
			break;
		}
	}

	if (value == &conv_value) {
		g_value_unset (&conv_value);
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
 */
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
 */
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

	g_mutex_lock (&db->priv->change_mutex);
	g_hash_table_insert (db->priv->deleted_entries, entry, g_thread_self ());
	g_mutex_unlock (&db->priv->change_mutex);

	/* deleting an entry makes the db dirty */
	db->priv->dirty = TRUE;
}

/**
 * rhythmdb_entry_move_to_trash:
 * @db: the #RhythmDB
 * @entry: #RhythmDBEntry to trash
 *
 * Trashes the file represented by #entry.  If possible, the file is
 * moved to the user's trash directory and the entry is set to hidden,
 * otherwise the error will be stored as the playback error for the entry.
 */
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
 */
void
rhythmdb_entry_delete_by_type (RhythmDB *db,
			       RhythmDBEntryType *type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	if (klass->impl_entry_delete_by_type) {
		klass->impl_entry_delete_by_type (db, type);
	} else {
		g_warning ("delete_by_type not implemented");
	}
}

/**
 * rhythmdb_nice_elt_name_from_propid:
 * @db: the #RhythmDB
 * @propid: property ID
 *
 * Returns a short non-translated name for the property #propid.
 * This name is suitable for use as an XML tag name, for example.
 *
 * Return value: property ID name, must not be freed
 */
const xmlChar *
rhythmdb_nice_elt_name_from_propid (RhythmDB *db,
				    RhythmDBPropType propid)
{
	return (xmlChar *)rhythmdb_properties[propid].elt_name;
}

/**
 * rhythmdb_propid_from_nice_elt_name:
 * @db: the #RhythmDB
 * @name: a property ID name
 *
 * Converts a property name returned by @rhythmdb_propid_from_nice_elt_name
 * back to a #RhythmDBPropType.  If the name does not match a property ID,
 * -1 will be returned instead.
 *
 * Return value: a #RhythmDBPropType, or -1
 */
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
 * Returns: (transfer none): the entry with location @uri, or NULL if no such entry exists.
 */
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

/**
 * rhythmdb_entry_lookup_by_location_refstring:
 * @db: the #RhythmDB
 * @uri: #RBRefString for the entry location
 *
 * Looks up the entry with location @uri.
 *
 * Returns: (transfer none): the entry with location @uri, or NULL if no such entry exists.
 */
RhythmDBEntry *
rhythmdb_entry_lookup_by_location_refstring (RhythmDB *db,
					     RBRefString *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lookup_by_location (db, uri);
}

/**
 * rhythmdb_entry_lookup_by_id:
 * @db: a #RhythmDB.
 * @id: entry ID
 *
 * Looks up the entry with id @id.
 *
 * Returns: (transfer none): the entry with id @id, or NULL if no such entry exists.
 */
RhythmDBEntry *
rhythmdb_entry_lookup_by_id (RhythmDB *db,
			     gint id)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lookup_by_id (db, id);
}

/**
 * rhythmdb_entry_lookup_from_string:
 * @db: a #RhythmDB.
 * @str: string
 * @is_id: whether the string is an entry ID or a location.
 *
 * Locates an entry using a string containing either an entry ID
 * or a location.
 *
 * Returns: (transfer none): the entry matching the string, or NULL if no such entry exists.
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
 * rhythmdb_entry_foreach:
 * @db: a #RhythmDB.
 * @func: (scope call): the function to call with each entry.
 * @data: user data to pass to the function.
 *
 * Calls the given function for each of the entries in the database.
 */
void
rhythmdb_entry_foreach (RhythmDB *db,
			RhythmDBEntryForeachFunc func,
			gpointer data)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_foreach (db, func, data);
}

/**
 * rhythmdb_entry_count:
 * @db: a #RhythmDB.
 *
 * Returns the number of entries in the database.
 *
 * Return value: number of entries
 */
gint64
rhythmdb_entry_count (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_count (db);
}

/**
 * rhythmdb_entry_foreach_by_type:
 * @db: a #RhythmDB.
 * @entry_type: the type of entry to retrieve
 * @func: (scope call): the function to call with each entry
 * @data: user data to pass to the function.
 *
 * Calls the given function for each of the entries in the database
 * of a given type.
 */
void
rhythmdb_entry_foreach_by_type (RhythmDB *db,
				RhythmDBEntryType *entry_type,
				RhythmDBEntryForeachFunc func,
				gpointer data)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_foreach_by_type (db, entry_type, func, data);
}

/**
 * rhythmdb_entry_count_by_type:
 * @db: a #RhythmDB.
 * @entry_type: a #RhythmDBEntryType.
 *
 * Returns the number of entries in the database of a particular type.
 *
 * Return value: entry count
 */
gint64
rhythmdb_entry_count_by_type (RhythmDB *db,
			      RhythmDBEntryType *entry_type)
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
 */
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

/**
 * rhythmdb_do_full_query_async_parsed:
 * @db: the #RhythmDB
 * @results: a #RhythmDBQueryResults instance to feed results to
 * @query: the query to run
 *
 * Asynchronously runs a parsed query across the database, feeding matching
 * entries to @results in chunks.  This can only be called from the
 * main thread.
 *
 * Since @results is always a @RhythmDBQueryModel,
 * use the RhythmDBQueryModel::complete signal to identify when the
 * query is complete.
 */
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

/**
 * rhythmdb_do_full_query_async:
 * @db: the #RhythmDB
 * @results: a #RhythmDBQueryResults to feed results to
 * @...: query parameters
 *
 * Asynchronously runs a query specified in the function arguments
 * across the database, feeding matching entries to @results in chunks.
 * This can only be called from the main thread.
 *
 * Since @results is always a @RhythmDBQueryModel,
 * use the RhythmDBQueryModel::complete signal to identify when the
 * query is complete.
 *
 * FIXME: example
 */
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

/**
 * rhythmdb_do_full_query_parsed:
 * @db: the #RhythmDB
 * @results: a #RhythmDBQueryResults instance to feed results to
 * @query: a parsed query
 *
 * Synchronously evaluates the parsed query @query, feeding results
 * to @results in chunks.  Does not return until the query is complete.
 */
void
rhythmdb_do_full_query_parsed (RhythmDB *db,
			       RhythmDBQueryResults *results,
			       GPtrArray *query)
{
	rhythmdb_do_full_query_internal (db, results, query);
}

/**
 * rhythmdb_do_full_query:
 * @db: the #RhythmDB
 * @results: a #RhythmDBQueryResults instance to feed results to
 * @...: query parameters
 *
 * Synchronously evaluates @query, feeding results to @results in
 * chunks.  Does not return until the query is complete.
 * This can only be called from the main thread.
 *
 * FIXME: example
 */
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

			ENUM_ENTRY (RHYTHMDB_QUERY_END, "query-end"),
			ENUM_ENTRY (RHYTHMDB_QUERY_DISJUNCTION, "disjunctive-marker"),
			ENUM_ENTRY (RHYTHMDB_QUERY_SUBQUERY, "subquery"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_EQUALS, "equals"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_NOT_EQUAL, "not-equal"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LIKE, "fuzzy-match"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_NOT_LIKE, "inverted-fuzzy-match"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_PREFIX, "starts-with"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_SUFFIX, "ends-with"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_GREATER, "greater-than"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LESS, "less-than"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN, "within-current-time"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN, "not-within-current-time"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_EQUALS, "year-equals"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL, "year-not-equals"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_GREATER, "year-greater-than"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_YEAR_LESS, "year-less-than"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBQueryType", values);
	}

	return etype;
}

GType
rhythmdb_prop_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		int i;
		static GEnumValue values[G_N_ELEMENTS(rhythmdb_properties)];
		g_assert(G_N_ELEMENTS(rhythmdb_properties)-1 == RHYTHMDB_NUM_PROPERTIES);
		for (i = 0; i < G_N_ELEMENTS(rhythmdb_properties)-1; i++) {
			g_assert (i == rhythmdb_properties[i].prop_id);
			values[i].value = rhythmdb_properties[i].prop_id;
			values[i].value_name = rhythmdb_properties[i].prop_name;
			values[i].value_nick = rhythmdb_properties[i].elt_name;
		}
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
 */
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
 */
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
 * rhythmdb_entry_gather_metadata:
 * @db: a #RhythmDB
 * @entry: a #RhythmDBEntry
 *
 * Gathers all metadata for the @entry. The returned GHashTable maps property
 * names and extra metadata names (described under
 * @rhythmdb_entry_request_extra_metadata) to GValues. Anything wanting to
 * provide extra metadata should connect to the "entry_extra_metadata_gather"
 * signal.
 *
 * Returns: (transfer full): a RBStringValueMap containing metadata for the entry.
 * This must be freed using g_object_unref.
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

		/* skip deprecated properties */
		switch (prop) {
		case RHYTHMDB_PROP_TRACK_GAIN:
		case RHYTHMDB_PROP_TRACK_PEAK:
		case RHYTHMDB_PROP_ALBUM_GAIN:
		case RHYTHMDB_PROP_ALBUM_PEAK:
			continue;
		default:
			break;
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
 */
char *
rhythmdb_compute_status_normal (gint n_songs,
				glong duration,
				guint64 size,
				const char *singular,
				const char *plural)
{
	long days, hours, minutes;
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

	size_str = g_format_size (size);

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

/**
 * rhythmdb_register_entry_type:
 * @db: the #RhythmDB
 * @entry_type: the new entry type to register
 *
 * Registers a new entry type.  An entry type must be registered before
 * any entries can be created for it.
 */
void
rhythmdb_register_entry_type (RhythmDB *db, RhythmDBEntryType *entry_type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	char *name = NULL;

	g_object_get (entry_type, "name", &name, NULL);
	g_assert (name != NULL);
	g_mutex_lock (&db->priv->entry_type_map_mutex);
	g_hash_table_insert (db->priv->entry_type_map, name, g_object_ref (entry_type));
	g_mutex_unlock (&db->priv->entry_type_map_mutex);

	if (klass->impl_entry_type_registered)
		klass->impl_entry_type_registered (db, entry_type);
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
	g_mutex_lock (&db->priv->entry_type_mutex);
	g_hash_table_foreach (db->priv->entry_type_map, func, data);
	g_mutex_unlock (&db->priv->entry_type_mutex);
}

/**
 * rhythmdb_entry_type_get_by_name:
 * @db: a #RhythmDB
 * @name: name of the type to look for
 *
 * Locates a #RhythmDBEntryType by name. Returns NULL if no entry
 * type is registered with the specified name.
 *
 * Returns: (transfer none): the #RhythmDBEntryType
 */
RhythmDBEntryType *
rhythmdb_entry_type_get_by_name (RhythmDB *db,
				 const char *name)
{
	gpointer t = NULL;

	g_mutex_lock (&db->priv->entry_type_map_mutex);
	if (db->priv->entry_type_map) {
		t = g_hash_table_lookup (db->priv->entry_type_map, name);
	}
	g_mutex_unlock (&db->priv->entry_type_map_mutex);

	return (RhythmDBEntryType *) t;
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
	if (db->priv->library_locations != NULL &&
	    g_strv_length (db->priv->library_locations) > 0) {
		rb_debug ("ending monitor of old library directories");

		rhythmdb_stop_monitoring (db);

		g_strfreev (db->priv->library_locations);
		db->priv->library_locations = NULL;
	}

	if (g_settings_get_boolean (db->priv->settings, "monitor-library")) {
		rb_debug ("starting library monitoring");
		db->priv->library_locations = g_settings_get_strv (db->priv->settings, "locations");

		rhythmdb_start_monitoring (db);
	}
}

static void
db_settings_changed_cb (GSettings *settings, const char *key, RhythmDB *db)
{
	if (g_strcmp0 (key, "locations") == 0 || g_strcmp0 (key, "monitor-library") == 0) {
		rhythmdb_sync_library_location (db);
	}
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

/**
 * rhythmdb_entry_get_string:
 * @entry: a #RhythmDBEntry
 * @propid: the #RhythmDBPropType to return
 *
 * Returns the value of a string property of #entry.
 *
 * Return value: property value, must not be freed
 */
const char *
rhythmdb_entry_get_string (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	RhythmDBPodcastFields *podcast = NULL;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (entry->refcount > 0, NULL);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH)
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
	case RHYTHMDB_PROP_COMMENT:
		return rb_refstring_get (entry->comment);
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
	case RHYTHMDB_PROP_ALBUM_ARTIST:
		return rb_refstring_get (entry->album_artist);
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME:
		return rb_refstring_get (entry->album_artist_sortname);
	case RHYTHMDB_PROP_COMPOSER:
		return rb_refstring_get (entry->composer);
	case RHYTHMDB_PROP_COMPOSER_SORTNAME:
		return rb_refstring_get (entry->composer_sortname);
	case RHYTHMDB_PROP_MEDIA_TYPE:
		return rb_refstring_get (entry->media_type);
	case RHYTHMDB_PROP_TITLE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->title);
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album);
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		return rb_refstring_get_sort_key (entry->artist);
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->genre);
	case RHYTHMDB_PROP_ARTIST_SORTNAME_SORT_KEY:
		return rb_refstring_get_sort_key (entry->artist_sortname);
	case RHYTHMDB_PROP_ALBUM_SORTNAME_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album_sortname);
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album_artist);
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album_artist_sortname);
	case RHYTHMDB_PROP_COMPOSER_SORT_KEY:
		return rb_refstring_get_sort_key (entry->composer);
	case RHYTHMDB_PROP_COMPOSER_SORTNAME_SORT_KEY:
		return rb_refstring_get_sort_key (entry->composer_sortname);
	case RHYTHMDB_PROP_TITLE_FOLDED:
		return rb_refstring_get_folded (entry->title);
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		return rb_refstring_get_folded (entry->album);
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		return rb_refstring_get_folded (entry->artist);
	case RHYTHMDB_PROP_GENRE_FOLDED:
		return rb_refstring_get_folded (entry->genre);
	case RHYTHMDB_PROP_ARTIST_SORTNAME_FOLDED:
		return rb_refstring_get_folded (entry->artist_sortname);
	case RHYTHMDB_PROP_ALBUM_SORTNAME_FOLDED:
		return rb_refstring_get_folded (entry->album_sortname);
	case RHYTHMDB_PROP_ALBUM_ARTIST_FOLDED:
		return rb_refstring_get_folded (entry->album_artist);
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME_FOLDED:
		return rb_refstring_get_folded (entry->album_artist_sortname);
	case RHYTHMDB_PROP_COMPOSER_FOLDED:
		return rb_refstring_get_folded (entry->composer);
	case RHYTHMDB_PROP_COMPOSER_SORTNAME_FOLDED:
		return rb_refstring_get_folded (entry->composer_sortname);
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
	case RHYTHMDB_PROP_PODCAST_GUID:
		if (podcast)
			return rb_refstring_get (podcast->guid);
		else
			return NULL;

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

/**
 * rhythmdb_entry_get_refstring:
 * @entry: a #RhythmDBEntry
 * @propid: the property to return
 *
 * Returns an #RBRefString containing a string property of @entry.
 *
 * Return value: a #RBRefString, must be unreffed by caller.
 */
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
	case RHYTHMDB_PROP_ALBUM_ARTIST:
		return rb_refstring_ref (entry->album_artist);
	case RHYTHMDB_PROP_COMPOSER:
		return rb_refstring_ref (entry->composer);
	case RHYTHMDB_PROP_GENRE:
		return rb_refstring_ref (entry->genre);
	case RHYTHMDB_PROP_COMMENT:
		return rb_refstring_ref (entry->comment);
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
	case RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME:
		return rb_refstring_ref (entry->album_artist_sortname);
	case RHYTHMDB_PROP_COMPOSER_SORTNAME:
		return rb_refstring_ref (entry->composer_sortname);
	case RHYTHMDB_PROP_MEDIA_TYPE:
		return rb_refstring_ref (entry->media_type);
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

/**
 * rhythmdb_entry_get_boolean:
 * @entry: a #RhythmDBEntry
 * @propid: property to return
 *
 * Returns the value of a boolean property of @entry.
 *
 * Return value: property value
 */
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

/**
 * rhythmdb_entry_get_uint64:
 * @entry: a #RhythmDBEntry
 * @propid: property to return
 *
 * Returns the value of a 64bit unsigned integer property.
 *
 * Return value: property value
 */
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

/**
 * rhythmdb_entry_get_entry_type:
 * @entry: a #RhythmDBEntry
 *
 * Returns the #RhythmDBEntryType for @entry.  This is used to access
 * entry type properties, to check that entries are of the same type,
 * and to call entry type methods.
 *
 * Return value: (transfer none): the #RhythmDBEntryType for @entry
 */
RhythmDBEntryType *
rhythmdb_entry_get_entry_type (RhythmDBEntry *entry)
{
	g_return_val_if_fail (entry != NULL, NULL);

	return entry->type;
}

/**
 * rhythmdb_entry_get_object:
 * @entry: a #RhythmDBEntry
 * @propid: the property to return
 *
 * Returns the value of an object property of @entry.
 *
 * Return value: (transfer none): property value
 */
GObject *
rhythmdb_entry_get_object (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, NULL);

	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return G_OBJECT (entry->type);
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

/**
 * rhythmdb_entry_get_ulong:
 * @entry: a #RhythmDBEntry
 * @propid: property to return
 *
 * Returns the value of an unsigned long integer property of @entry.
 *
 * Return value: property value
 */
gulong
rhythmdb_entry_get_ulong (RhythmDBEntry *entry,
			  RhythmDBPropType propid)
{
	RhythmDBPodcastFields *podcast = NULL;

	g_return_val_if_fail (entry != NULL, 0);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH)
		podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);

	switch (propid) {
	case RHYTHMDB_PROP_ENTRY_ID:
		return entry->id;
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return entry->tracknum;
	case RHYTHMDB_PROP_TRACK_TOTAL:
		return entry->tracktotal;
	case RHYTHMDB_PROP_DISC_NUMBER:
		return entry->discnum;
	case RHYTHMDB_PROP_DISC_TOTAL:
		return entry->disctotal;
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

/**
 * rhythmdb_entry_get_double:
 * @entry: a #RhythmDBEntry
 * @propid: the property to return
 *
 * Returns the value of a double-precision floating point property of @value.
 *
 * Return value: property value
 */
double
rhythmdb_entry_get_double (RhythmDBEntry *entry,
			   RhythmDBPropType propid)
{
	g_return_val_if_fail (entry != NULL, 0);

	switch (propid) {
	case RHYTHMDB_PROP_TRACK_GAIN:
		g_warning ("RHYTHMDB_PROP_TRACK_GAIN no longer supported");
		return 0.0;
	case RHYTHMDB_PROP_TRACK_PEAK:
		g_warning ("RHYTHMDB_PROP_TRACK_PEAK no longer supported");
		return 1.0;
	case RHYTHMDB_PROP_ALBUM_GAIN:
		g_warning ("RHYTHMDB_PROP_ALBUM_GAIN no longer supported");
		return 0.0;
	case RHYTHMDB_PROP_ALBUM_PEAK:
		g_warning ("RHYTHMDB_PROP_ALBUM_PEAK no longer supported");
		return 1.0;
	case RHYTHMDB_PROP_RATING:
		return entry->rating;
	case RHYTHMDB_PROP_BPM:
		return entry->bpm;
	default:
		g_assert_not_reached ();
		return 0.0;
	}
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
 */
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
 */
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
 */
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
 * Returns: (element-type RBRefString) (transfer full): the list of keywords
 *          that have been added to the entry.
 */
GList*
rhythmdb_entry_keywords_get	(RhythmDB *db,
				 RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_entry_keywords_get (db, entry);
}

/**
 * rhythmdb_entry_write_metadata_changes:
 * @db: the #RhythmDB
 * @entry: the #RhythmDBEntry to update
 * @changes: (element-type RB.RhythmDBEntryChange): a list of changes to write
 * @error: returns error information
 *
 * This can be called from a #RhythmDBEntryType sync_metadata function
 * when the appropriate action is to write the metadata changes
 * to the file at the entry's location.
 */
void
rhythmdb_entry_write_metadata_changes (RhythmDB *db,
				       RhythmDBEntry *entry,
				       GSList *changes,
				       GError **error)
{
	const char *uri;
	GError *local_error = NULL;
	GSList *t;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	rb_metadata_reset (db->priv->metadata);

	for (t = changes; t; t = t->next) {
		RBMetaDataField field;
		GValue val = {0,};
		RhythmDBEntryChange *change = (RhythmDBEntryChange *)t->data;

		if (metadata_field_from_prop (change->prop, &field) == FALSE) {
			continue;
		}

		g_value_init (&val, rhythmdb_get_property_type (db, change->prop));
		rhythmdb_entry_get (db, entry, change->prop, &val);
		rb_metadata_set (db->priv->metadata, field, &val);
		g_value_unset (&val);
	}

	rb_metadata_save (db->priv->metadata, uri, &local_error);
	if (local_error != NULL) {
		RhythmDBAction *load_action;

		/* reload the metadata, to revert the db changes */
		rb_debug ("error saving metadata for %s: %s; reloading metadata to revert",
			  rb_refstring_get (entry->location),
			  local_error->message);
		load_action = g_slice_new0 (RhythmDBAction);
		load_action->type = RHYTHMDB_ACTION_LOAD;
		load_action->uri = rb_refstring_ref (entry->location);
		load_action->data.types.entry_type = rhythmdb_entry_get_entry_type (entry);
		g_async_queue_push (db->priv->action_queue, load_action);

		g_propagate_error (error, local_error);
	}
}

/**
 * rhythmdb_get_property_type:
 * @db: the #RhythmDB
 * @property_id: a property ID (#RhythmDBPropType)
 *
 * Returns the #GType for the value of the property.
 *
 * Return value: property value type
 */
GType
rhythmdb_get_property_type (RhythmDB *db,
			    guint property_id)
{
	g_assert (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES);
	return rhythmdb_properties[property_id].prop_type;
}

/**
 * rhythmdb_entry_get_type:
 *
 * Returns the #GType for #RhythmDBEntry.  The #GType for #RhythmDBEntry is a
 * boxed type, where copying the value references the entry and freeing it
 * unrefs it.
 *
 * Return value: value type
 */
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

/**
 * rhythmdb_entry_change_get_type:
 *
 * Returns the #GType for #RhythmDBEntryChange.  #RhythmDBEntryChange is stored as a
 * boxed value.  Copying the value copies the full change, including old and new values.
 *
 * Return value: entry change value type
 */
GType
rhythmdb_entry_change_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RhythmDBEntryChange",
						     (GBoxedCopyFunc)rhythmdb_entry_change_copy,
						     (GBoxedFreeFunc)rhythmdb_entry_change_free);
	}
	return type;
}

/**
 * rhythmdb_entry_is_lossless:
 * @entry: a #RhythmDBEntry
 *
 * Checks if @entry represents a file that is losslessly encoded.
 * An entry is considered lossless if it has no bitrate value and
 * its media type is "audio/x-flac".  Other lossless encoding types
 * may be added in the future.
 *
 * Return value: %TRUE if @entry is lossless
 */
gboolean
rhythmdb_entry_is_lossless (RhythmDBEntry *entry)
{
	const char *media_type;

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
		return FALSE;
       
	media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
	return rb_gst_media_type_is_lossless (media_type);
}

/**
 * rhythmdb_entry_matches_ext_db_key:
 * @db: #RhythmDB instance
 * @entry: a #RhythmDBEntry
 * @key: a #RBExtDBKey
 *
 * Checks whether @key matches @entry.
 *
 * Return value: %TRUE if the key matches the entry
 */
gboolean
rhythmdb_entry_matches_ext_db_key (RhythmDB *db, RhythmDBEntry *entry, RBExtDBKey *key)
{
	char **fields;
	int i;

	fields = rb_ext_db_key_get_field_names (key);
	for (i = 0; fields[i] != NULL; i++) {
		RhythmDBPropType prop;
		RhythmDBPropType extra_prop;
		const char *v;

		prop = rhythmdb_propid_from_nice_elt_name (db, (const xmlChar *)fields[i]);
		if (prop == -1) {
			if (rb_ext_db_key_field_matches (key, fields[i], NULL) == FALSE) {
				g_strfreev (fields);
				return FALSE;
			}
			continue;
		}

		/* check additional values for some fields */
		switch (prop) {
		case RHYTHMDB_PROP_ARTIST:
			extra_prop = RHYTHMDB_PROP_ALBUM_ARTIST;
			break;
		default:
			extra_prop = -1;
			break;
		}

		if (extra_prop != -1) {
			v = rhythmdb_entry_get_string (entry, extra_prop);
			if (rb_ext_db_key_field_matches (key, fields[i], v))
				continue;
		}

		v = rhythmdb_entry_get_string (entry, prop);
		if (rb_ext_db_key_field_matches (key, fields[i], v) == FALSE) {
			g_strfreev (fields);
			return FALSE;
		}
	}

	g_strfreev (fields);
	return TRUE;
}
