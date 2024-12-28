/*
 *  Copyright (C) 2004 Colin Walters <walters@rhythmbox.org>
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

#ifndef RHYTHMDB_PRIVATE_H
#define RHYTHMDB_PRIVATE_H

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rb-refstring.h>
#include <metadata/rb-metadata.h>

G_BEGIN_DECLS

RhythmDBEntry * rhythmdb_entry_allocate		(RhythmDB *db, RhythmDBEntryType *type);
void		rhythmdb_entry_insert		(RhythmDB *db, RhythmDBEntry *entry);

typedef struct {
	/* podcast */
	RBRefString *description;
	RBRefString *subtitle;
	RBRefString *lang;
	RBRefString *copyright;
	RBRefString *image;
	RBRefString *guid;
	gulong status;
	gulong post_time;
} RhythmDBPodcastFields;

enum {
	RHYTHMDB_ENTRY_HIDDEN = 1,
	RHYTHMDB_ENTRY_INSERTED = 2,
	RHYTHMDB_ENTRY_LAST_PLAYED_DIRTY = 4,
	RHYTHMDB_ENTRY_FIRST_SEEN_DIRTY = 8,
	RHYTHMDB_ENTRY_LAST_SEEN_DIRTY = 16,

	/* the backend can use the top 16 bits for private flags */
	RHYTHMDB_ENTRY_PRIVATE_FLAG_BASE = 65536,
};

struct _RhythmDBEntry {
	/* internal bits */
	guint flags;
	volatile gint refcount;
	void *data;
	RhythmDBEntryType *type;
	guint id;

	/* metadata */
	RBRefString *title;
	RBRefString *artist;
	RBRefString *composer;
	RBRefString *album;
	RBRefString *album_artist;
	RBRefString *genre;
	RBRefString *comment;
	RBRefString *musicbrainz_trackid;
	RBRefString *musicbrainz_artistid;
	RBRefString *musicbrainz_albumid;
	RBRefString *musicbrainz_albumartistid;
	RBRefString *artist_sortname;
	RBRefString *composer_sortname;
	RBRefString *album_sortname;
	RBRefString *album_artist_sortname;
	gulong tracknum;
	gulong tracktotal;
	gulong discnum;
	gulong disctotal;
	gulong duration;
	gulong bitrate;
	double bpm;
	GDate date;

	/* filesystem */
	RBRefString *location;
	RBRefString *mountpoint;
	guint64 file_size;
	RBRefString *media_type;
	gulong mtime;
	gulong first_seen;
	gulong last_seen;

	/* user data */
	gdouble rating;
	glong play_count;
	gulong last_played;

	/* cached data */
	gpointer last_played_str;
	gpointer first_seen_str;
	gpointer last_seen_str;

	/* playback error string */
	RBRefString *playback_error;
};

struct _RhythmDBPrivate
{
	char *name;

	gint read_counter;

	RBMetaData *metadata;

	RBRefString *empty_string;
	RBRefString *octet_stream_str;

	gboolean action_thread_running;
	gint outstanding_threads;
	GAsyncQueue *action_queue;
	GAsyncQueue *event_queue;
	GAsyncQueue *restored_queue;
	GAsyncQueue *delayed_write_queue;
	GThreadPool *query_thread_pool;
	GThread *load_thread;

	GList *stat_list;
	GList *outstanding_stats;
	GList *active_mounts;
	GList *mount_list;
	GMutex stat_mutex;
	gboolean stat_thread_running;
	int stat_thread_count;
	int stat_thread_done;

	GVolumeMonitor *volume_monitor;
	GHashTable *monitored_directories;
	GHashTable *changed_files;
	guint changed_files_id;
	char **library_locations;
	GMutex monitor_mutex;

	gboolean dry_run;
	gboolean no_update;

	GMutex change_mutex;
	GHashTable *added_entries;
	GHashTable *changed_entries;
	GHashTable *deleted_entries;

	GHashTable *propname_map;

	GMutex exit_mutex;
	GCancellable *exiting;		/* hrm, name? */

	GCond saving_condition;
	GMutex saving_mutex;
	guint save_count;

	guint event_queue_watch_id;
	guint commit_timeout_id;
	guint save_timeout_id;
	guint sync_library_id;

	guint emit_entry_signals_id;
	GList *added_entries_to_emit;
	GList *deleted_entries_to_emit;
	GHashTable *changed_entries_to_emit;

	GList *barriers_done;
	GMutex barrier_mutex;
	GCond barrier_condition;

	gboolean can_save;
	gboolean saving;
	gboolean dirty;

	GHashTable *entry_type_map;
	GMutex entry_type_map_mutex;
	GMutex entry_type_mutex;

	gint next_entry_id;

	GSettings *settings;

	guint dbus_object_id;
};

typedef struct
{
	enum {
		RHYTHMDB_EVENT_STAT,
		RHYTHMDB_EVENT_METADATA_LOAD,
		RHYTHMDB_EVENT_METADATA_CACHE,
		RHYTHMDB_EVENT_DB_LOAD,
		RHYTHMDB_EVENT_THREAD_EXITED,
		RHYTHMDB_EVENT_DB_SAVED,
		RHYTHMDB_EVENT_QUERY_COMPLETE,
		RHYTHMDB_EVENT_ENTRY_SET,
		RHYTHMDB_EVENT_BARRIER
	} type;
	RBRefString *uri;
	RBRefString *real_uri; /* Target of a symlink, if any */
	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *ignore_type;
	RhythmDBEntryType *error_type;

	GError *error;
	RhythmDB *db;

	/* STAT */
	GFileInfo *file_info;
	/* LOAD */
	GArray cached_metadata;
	RBMetaData *metadata;
	/* QUERY_COMPLETE */
	RhythmDBQueryResults *results;
	/* ENTRY_SET */
	RhythmDBEntry *entry;
	/* ENTRY_SET */
	gboolean signal_change;
	RhythmDBEntryChange change;
} RhythmDBEvent;

/* from rhythmdb.c */
void rhythmdb_entry_set_visibility (RhythmDB *db, RhythmDBEntry *entry,
				    gboolean visibility);
void rhythmdb_entry_set_internal (RhythmDB *db, RhythmDBEntry *entry,
				  gboolean notify_if_inserted, guint propid,
				  const GValue *value);
void rhythmdb_entry_type_foreach (RhythmDB *db, GHFunc func, gpointer data);
RhythmDBEntry *	rhythmdb_entry_lookup_by_location_refstring (RhythmDB *db, RBRefString *uri);

/* from rhythmdb-monitor.c */
void rhythmdb_init_monitoring (RhythmDB *db);
void rhythmdb_dispose_monitoring (RhythmDB *db);
void rhythmdb_finalize_monitoring (RhythmDB *db);
void rhythmdb_stop_monitoring (RhythmDB *db);
void rhythmdb_start_monitoring (RhythmDB *db);
void rhythmdb_monitor_uri_path (RhythmDB *db, const char *uri, GError **error);
GList *rhythmdb_get_active_mounts (RhythmDB *db);

/* from rhythmdb-query.c */
GPtrArray *rhythmdb_query_parse_valist (RhythmDB *db, va_list args);
void       rhythmdb_read_encoded_property (RhythmDB *db, const char *data, RhythmDBPropType propid, GValue *val);

/* from rhythmdb-song-entry-types.c */
void       rhythmdb_register_song_entry_types (RhythmDB *db);

/* from rhythmdb-dbus.c */
void rhythmdb_dbus_register (RhythmDB *db);
void rhythmdb_dbus_unregister (RhythmDB *db);

G_END_DECLS

#endif /* __RHYTHMDB_PRIVATE_H */
