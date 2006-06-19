 /*
 *  arch-tag: Header for RhythmDB private bits
 *
 *  Copyright (C) 2004 Colin Walters <walters@rhythmbox.org>
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

#ifndef RHYTHMDB_PRIVATE_H
#define RHYTHMDB_PRIVATE_H

#include "config.h"

#include "rhythmdb.h"
#include "rb-refstring.h"
#include "rb-metadata.h"

#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>

G_BEGIN_DECLS

RhythmDBEntry * rhythmdb_entry_allocate		(RhythmDB *db, RhythmDBEntryType type);
void		rhythmdb_entry_insert		(RhythmDB *db, RhythmDBEntry *entry);

typedef struct {
	/* podcast */
	RBRefString *description;
	RBRefString *subtitle;
	RBRefString *summary;
	RBRefString *lang;
	RBRefString *copyright;
	RBRefString *image;
	gulong status;	/* 0-99: downloading
			   100: Complete
			   101: Error
			   102: wait
			   103: pause */
	gulong post_time;
} RhythmDBPodcastFields;

enum {
	RHYTHMDB_ENTRY_HIDDEN = 1,
	RHYTHMDB_ENTRY_INSERTED = 2,

	/* the backend can use the top 16 bits for private flags */
	RHYTHMDB_ENTRY_PRIVATE_FLAG_BASE = 65536,
};

struct RhythmDBEntry_ {
	/* internal bits */
	guint flags;
	gint refcount;
	void *data;
	RhythmDBEntryType type;
	
	/* metadata */
	RBRefString *title;
	RBRefString *artist;
	RBRefString *album;
	RBRefString *genre;
	gulong tracknum;
	gulong discnum;
	gulong duration;
	gulong bitrate;
	double track_gain;
	double track_peak;
	double album_gain;
	double album_peak;
	GDate date;

	/* filesystem */
	char *location;
	RBRefString *mountpoint;
	guint64 file_size;
	RBRefString *mimetype;
	gulong mtime;
	gulong first_seen;
	gulong last_seen;

	/* user data */
	gdouble rating;
	glong play_count;
	gulong last_played;

	/* cached data */
	RBRefString *last_played_str;
	RBRefString *first_seen_str;
	RBRefString *last_seen_str;

	/* playback error string */
	char *playback_error;
};

struct RhythmDBPrivate
{
	char *name;

	gint read_counter;

	RBMetaData *metadata;

	xmlChar **column_xml_names;

	RBRefString *empty_string;
	RBRefString *octet_stream_str;

	gboolean action_thread_running;
	gint outstanding_threads;
	GAsyncQueue *action_queue;
	GAsyncQueue *event_queue;
	GAsyncQueue *restored_queue;
	GThreadPool *query_thread_pool;

	GList *stat_list;
	GHashTable *stat_events;
	GnomeVFSAsyncHandle *stat_handle;
	GMutex *stat_mutex;

	GHashTable *monitored_directories;
	GHashTable *changed_files;
	guint library_location_notify_id;
	guint changed_files_id;
	GSList *library_locations;

	gboolean dry_run;
	gboolean no_update;

	GMutex *change_mutex;
	GHashTable *added_entries;
	GHashTable *changed_entries;
	GHashTable *deleted_entries;

	GHashTable *propname_map;

	GMutex *exit_mutex;
	gboolean exiting;
	
	GCond *saving_condition;
	GMutex *saving_mutex;

	guint event_poll_id;
	guint commit_timeout_id;
	guint save_timeout_id;

	guint emit_entry_signals_id;
	GList *added_entries_to_emit;
	GList *deleted_entries_to_emit;

	gboolean saving;
	gboolean dirty;
};

typedef struct
{
	enum {
		RHYTHMDB_EVENT_STAT,
		RHYTHMDB_EVENT_METADATA_LOAD,
		RHYTHMDB_EVENT_DB_LOAD,
		RHYTHMDB_EVENT_THREAD_EXITED,
		RHYTHMDB_EVENT_DB_SAVED,
		RHYTHMDB_EVENT_QUERY_COMPLETE,
		RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED,
		RHYTHMDB_EVENT_FILE_DELETED,
		RHYTHMDB_EVENT_ENTRY_SET
	} type;
	char *uri;
	char *real_uri; /* Target of a symlink, if any */
	RhythmDBEntryType entry_type;

	GError *error;
	RhythmDB *db;

	/* STAT */
	GnomeVFSFileInfo *vfsinfo;
	GnomeVFSAsyncHandle *handle;
	/* LOAD */
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
void queue_stat_uri (const char *uri, RhythmDB *db, RhythmDBEntryType type);
void rhythmdb_entry_set_visibility (RhythmDB *db, RhythmDBEntry *entry, 
				    gboolean visibility);
void rhythmdb_entry_set_internal (RhythmDB *db, RhythmDBEntry *entry, 
				  gboolean notify_if_inserted, guint propid, 
				  const GValue *value);

/* from rhythmdb-monitor.c */
void rhythmdb_init_monitoring (RhythmDB *db);
void rhythmdb_finalize_monitoring (RhythmDB *db);
void rhythmdb_stop_monitoring (RhythmDB *db);
void rhythmdb_start_monitoring (RhythmDB *db);
void rhythmdb_monitor_uri_path (RhythmDB *db, const char *uri, GError **error);

/* from rhythmdb-query.c */
GPtrArray *rhythmdb_query_parse_valist (RhythmDB *db, va_list args);
void       rhythmdb_read_encoded_property (RhythmDB *db, const char *data, RhythmDBPropType propid, GValue *val);

G_END_DECLS

#endif /* __RHYTHMDB_PRIVATE_H */
