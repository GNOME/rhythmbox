/*
 *  arch-tag: Implementation of RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include "rhythmdb.h"
#include "rhythmdb-legacy.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"
#include "monkey-media.h"
#include "monkey-media-stream-info.h"
#include <string.h>
#include <gobject/gvaluecollector.h>
#include <gdk/gdk.h>
#include <libxml/tree.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnome/gnome-i18n.h>
#include "rb-string-helpers.h"
#include "rb-marshal.h"
#include "rb-file-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-debug.h"
#include "rb-cut-and-paste-code.h"

struct RhythmDBPrivate
{
	char *name;

	GStaticRWLock lock;

	GType *column_types;

	GThreadPool *query_thread_pool;

	GHashTable *legacy_id_map;

	gboolean query_in_progress;

	GAsyncQueue *main_thread_cancel;
	GAsyncQueue *update_queue;
	GAsyncQueue *add_queue;
	GAsyncQueue *action_queue;

	GHashTable *added_entries;
	GHashTable *changed_entries;

	GHashTable *propname_map;

	guint thread_reaper_id;

	GAsyncQueue *status_queue;
	gint outstanding_threads;

	GMutex *exit_mutex;
	gboolean exiting;
};

struct RhythmDBAction
{
	enum {
		RHYTHMDB_ACTION_SET,
	} type;
	RhythmDBEntry *entry;
	guint propid;
	GValue value;
};

struct RhythmDBQueryThreadData
{
	GPtrArray *query;
	guint propid;
	GtkTreeModel *main_model;
	gboolean lock;
	gboolean cancel;
};

static void rhythmdb_class_init (RhythmDBClass *klass);
static void rhythmdb_init (RhythmDB *source);
static void rhythmdb_finalize (GObject *object);
static void rhythmdb_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rhythmdb_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
static gpointer add_thread_main (RhythmDB *db);
static gpointer update_thread_main (RhythmDB *db);
static gpointer action_thread_main (RhythmDB *db);
static void update_song (RhythmDB *db, RhythmDBEntry *entry, GError **error);
gboolean reap_dead_threads (RhythmDB *db);
gpointer query_thread_main (struct RhythmDBQueryThreadData *data, RhythmDB *db);

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	ENTRY_ADDED,
	ENTRY_RESTORED,
	ENTRY_CHANGED,
	ENTRY_DELETED,
	LOAD_COMPLETE,
	ERROR,
	LEGACY_LOAD_COMPLETE,
	LAST_SIGNAL
};

static guint rhythmdb_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;


GType
rhythmdb_get_type (void)
{
	static GType rhythmdb_type = 0;

	if (rhythmdb_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_class_init,
			NULL,
			NULL,
			sizeof (RhythmDB),
			0,
			(GInstanceInitFunc) rhythmdb_init
		};

		rhythmdb_type = g_type_register_static (G_TYPE_OBJECT,
						       "RhythmDB",
						       &our_info, G_TYPE_FLAG_ABSTRACT);
	}

	return rhythmdb_type;
}

static void
rhythmdb_class_init (RhythmDBClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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

	rhythmdb_signals[ENTRY_ADDED] =
		g_signal_new ("entry_added",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_DELETED] =
		g_signal_new ("entry_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_RESTORED] =
		g_signal_new ("entry_restored",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_restored),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[ENTRY_CHANGED] =
		g_signal_new ("entry_changed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	rhythmdb_signals[LOAD_COMPLETE] =
		g_signal_new ("load_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rhythmdb_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, error),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
	rhythmdb_signals[LEGACY_LOAD_COMPLETE] =
		g_signal_new ("legacy-load-complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, legacy_load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static GType
extract_gtype_from_enum_entry (GEnumClass *klass, guint i)
{
	GType ret;
	GEnumValue *value;
	char *typename;
	char *typename_end;
	
	value = g_enum_get_value (klass, i);
	typename = strstr (value->value_nick, "(");
	g_return_val_if_fail (typename != NULL, G_TYPE_INVALID);

	typename_end = strstr (typename, ")");
	typename++;
	typename = g_strndup (typename, typename_end-typename);
	ret = g_type_from_name (typename);
	g_free (typename);
	return ret;
}

static void
rhythmdb_init (RhythmDB *db)
{
	guint i, j;
	GEnumClass *prop_class, *unsaved_prop_class;
	
	db->priv = g_new0 (RhythmDBPrivate, 1);

	g_static_rw_lock_init (&db->priv->lock);

	db->priv->main_thread_cancel = g_async_queue_new ();
	db->priv->update_queue = g_async_queue_new ();
	db->priv->add_queue = g_async_queue_new ();
	db->priv->action_queue = g_async_queue_new ();

	db->priv->status_queue = g_async_queue_new ();
	
	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

	g_assert (prop_class->n_values + unsaved_prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);
	g_assert (prop_class->n_values == RHYTHMDB_NUM_SAVED_PROPERTIES);
	db->priv->column_types = g_new (GType, RHYTHMDB_NUM_PROPERTIES);
	
	/* Now, extract the GType of each column from the enum descriptions,
	 * and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
	}
			
	
	for (j = 0; j < unsaved_prop_class->n_values; i++, j++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (unsaved_prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
	}

	g_type_class_unref (prop_class);
	g_type_class_unref (unsaved_prop_class);

	db->priv->propname_map = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		const char *name = rhythmdb_nice_elt_name_from_propid (db, i);
		g_hash_table_insert (db->priv->propname_map, (gpointer) name, GINT_TO_POINTER (i));
	}

	db->priv->legacy_id_map = g_hash_table_new (NULL, NULL);

	db->priv->query_thread_pool = g_thread_pool_new ((GFunc) query_thread_main,
							 db, 1, TRUE, NULL);
	db->priv->thread_reaper_id
		= g_idle_add ((GSourceFunc) reap_dead_threads, db);
	g_async_queue_ref (db->priv->main_thread_cancel);
	g_async_queue_ref (db->priv->add_queue);
	g_thread_create ((GThreadFunc) add_thread_main, db, FALSE, NULL);
	g_async_queue_ref (db->priv->main_thread_cancel);
	g_async_queue_ref (db->priv->update_queue);
	g_thread_create ((GThreadFunc) update_thread_main, db, FALSE, NULL);
	g_async_queue_ref (db->priv->main_thread_cancel);
	g_async_queue_ref (db->priv->action_queue);
	g_thread_create ((GThreadFunc) action_thread_main, db, FALSE, NULL);
}

gboolean
reap_dead_threads (RhythmDB *db)
{
	GObject *obj;

	while ((obj = g_async_queue_try_pop (db->priv->status_queue)) != NULL) {
		GDK_THREADS_ENTER ();
		g_object_unref (obj);
		db->priv->outstanding_threads--;
		GDK_THREADS_LEAVE ();
	}

	GDK_THREADS_ENTER ();
	db->priv->thread_reaper_id
		= g_timeout_add (5000, (GSourceFunc) reap_dead_threads, db);
	GDK_THREADS_LEAVE ();
	return FALSE;
}

void
rhythmdb_shutdown (RhythmDB *db)
{
	g_return_if_fail (RHYTHMDB_IS (db));

	db->priv->exiting = TRUE;
	
	g_source_remove (db->priv->thread_reaper_id);

	while (db->priv->outstanding_threads > 0) {
		GObject *obj = g_async_queue_pop (db->priv->status_queue);
		g_object_unref (obj);
		db->priv->outstanding_threads--;
	}

	/* Once for each thread */
	g_async_queue_pop (db->priv->main_thread_cancel);
	g_async_queue_pop (db->priv->main_thread_cancel);
	g_async_queue_pop (db->priv->main_thread_cancel);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	g_static_rw_lock_free (&db->priv->lock);

	g_thread_pool_free (db->priv->query_thread_pool, TRUE, FALSE);

	g_async_queue_unref (db->priv->status_queue);

	g_free (db->priv->column_types);

	g_hash_table_destroy (db->priv->propname_map);

	g_free (db->priv->name);

	g_free (db->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rhythmdb_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RhythmDB *source = RHYTHMDB (object);

	switch (prop_id)
	{
	case PROP_NAME:
		source->priv->name = g_strdup (g_value_get_string (value));
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

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, source->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rhythmdb_read_lock (RhythmDB *db)
{
	g_static_rw_lock_reader_lock (&db->priv->lock);
}

void
rhythmdb_read_unlock (RhythmDB *db)
{
	g_static_rw_lock_reader_unlock (&db->priv->lock);
}

void
rhythmdb_write_lock (RhythmDB *db)
{
	g_static_rw_lock_writer_lock (&db->priv->lock);
}

static void
emit_entry_changed (RhythmDBEntry *entry, gpointer unused,
		    RhythmDB *db)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, entry);
}

static void
emit_entry_added (RhythmDBEntry *entry, gpointer unused,
		  RhythmDB *db)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, entry);
}

void
rhythmdb_write_unlock (RhythmDB *db)
{
	if (db->priv->changed_entries) {
		g_hash_table_foreach (db->priv->changed_entries, (GHFunc) emit_entry_changed, db);
		g_hash_table_destroy (db->priv->changed_entries);
		db->priv->changed_entries = NULL;
	}
	if (db->priv->added_entries) {
		g_hash_table_foreach (db->priv->added_entries, (GHFunc) emit_entry_added, db);
		g_hash_table_destroy (db->priv->added_entries);
		db->priv->added_entries = NULL;
	}
		 
	g_static_rw_lock_writer_unlock (&db->priv->lock);
}

GQuark
rhythmdb_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("rhythmdb_error");

	return quark;
}

static inline void
db_enter (RhythmDB *db, gboolean write)
{
	if (write) 
		g_assert (db->priv->lock.have_writer);
	else
		g_assert (db->priv->lock.read_counter > 0
			  || db->priv->lock.have_writer);
}

RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db, RhythmDBEntryType type, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	RhythmDBEntry *ret;

	db_enter (db, TRUE);
	
	ret = klass->impl_entry_new (db, type, uri);
	rb_debug ("emitting entry added");
	if (!db->priv->changed_entries)
		db->priv->added_entries = g_hash_table_new (NULL, NULL);

	if (ret != NULL)
		g_hash_table_insert (db->priv->added_entries, ret, NULL);
	return ret;
}

typedef struct 
{
	GValue track_number_val;
	GValue file_size_val;
	GValue title_val;
	GValue duration_val;
	GValue quality_val;
	GValue mtime_val;
	GValue genre_val;
	GValue artist_val;
	GValue album_val;
} RhythmDBEntryUpdateData;

/* Threading: any thread
 */
static RhythmDBEntryUpdateData *
read_metadata (const char *location, GError **error)
{
	RhythmDBEntryUpdateData *data = g_new0 (RhythmDBEntryUpdateData, 1);
	MonkeyMediaStreamInfo *info;
	GnomeVFSFileInfo *vfsinfo;
	GValue tem = {0,};

	info = monkey_media_stream_info_new (location, error);
	if (G_UNLIKELY (info == NULL)) {
		g_free (data);
		return NULL;
	}

	/* track number */
	if (monkey_media_stream_info_get_value (info,
				                MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					        0, &data->track_number_val) == FALSE) {
		g_value_init (&data->track_number_val, G_TYPE_INT);
		g_value_set_int (&data->track_number_val, -1);
	}

	/* duration */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    0, &data->duration_val);

	/* quality */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY,
					    0, &tem);
	g_value_init (&data->quality_val, G_TYPE_INT);
	g_value_set_int (&data->quality_val, g_value_get_enum (&tem));

	/* filesize */
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    0, &data->file_size_val);

	/* title */
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    0, &data->title_val);
	if (*(g_value_get_string (&data->title_val)) == '\0') {
		GnomeVFSURI *vfsuri;
		char *fname;

		vfsuri = gnome_vfs_uri_new (location);
		fname = gnome_vfs_uri_extract_short_name (vfsuri);
		g_value_set_string_take_ownership (&data->title_val, fname);
		gnome_vfs_uri_unref (vfsuri);
	}

	vfsinfo = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (location, vfsinfo, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	/* mtime */
	g_value_init (&data->mtime_val, G_TYPE_LONG);
	g_value_set_long (&data->mtime_val, vfsinfo->mtime);

	gnome_vfs_file_info_unref (vfsinfo);

	/* genre */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    0,
				            &data->genre_val);

	/* artist */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    0,
				            &data->artist_val);

	/* album */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    0,
				            &data->album_val);
	g_object_unref (G_OBJECT (info));
	return data;
}

static void
synchronize_entry_with_data (RhythmDB *db, RhythmDBEntry *entry, RhythmDBEntryUpdateData *data)
{
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &data->track_number_val);
	g_value_unset (&data->track_number_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &data->duration_val);
	g_value_unset (&data->duration_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_QUALITY, &data->quality_val);
	g_value_unset (&data->quality_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FILE_SIZE, &data->file_size_val);
	g_value_unset (&data->file_size_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &data->title_val);
	g_value_unset (&data->title_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MTIME, &data->mtime_val);
	g_value_unset (&data->mtime_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &data->genre_val);
	g_value_unset (&data->genre_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &data->artist_val);
	g_value_unset (&data->artist_val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &data->album_val);
	g_value_unset (&data->album_val);
}

RhythmDBEntry *
rhythmdb_add_song (RhythmDB *db, const char *uri, GError **error)
{
	RhythmDBEntry *entry;
	char *realuri;
	RhythmDBEntryUpdateData *metadata;
	GValue last_time = {0, };

	realuri = rb_uri_resolve_symlink (uri);

	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_lookup_by_location (db, realuri);

	if (entry) {
		rhythmdb_entry_ref_unlocked (db, entry);
		rhythmdb_write_unlock (db);
		update_song (db, entry, error);
		rhythmdb_entry_unref (db, entry);
		return entry;
	}

	rhythmdb_write_unlock (db);

	/* Don't do file access with the db write lock held */
	metadata = read_metadata (uri, error);
	if (!metadata) {
		rb_debug ("failed to read data from \"%s\"", uri);
		g_free (realuri);
		return NULL;
	}

	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, realuri);
	if (entry == NULL)
		goto out_dupentry;
	synchronize_entry_with_data (db, entry, metadata);

	/* initialize the last played date to 0=never */
	g_value_init (&last_time, G_TYPE_LONG);
	g_value_set_long (&last_time, 0);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &last_time);
	g_value_unset (&last_time);

 out_dupentry:
	g_free (metadata);
	g_free (realuri);

	rhythmdb_write_unlock (db);
	return entry;
}

struct RhythmDBLoadErrorData {
	RhythmDB *db;
	char *uri;
	char *msg;
};

static gboolean
signal_err_idle (struct RhythmDBLoadErrorData *data)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (data->db), rhythmdb_signals[ERROR], 0,
		       data->uri, data->msg);
	g_free (data->uri);
	g_free (data->msg);
	g_free (data);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
push_err (RhythmDB *db, const char *uri, GError *error)
{
	struct RhythmDBLoadErrorData *loaderr = g_new0 (struct RhythmDBLoadErrorData, 1);

	loaderr->db = db;
	loaderr->uri = g_strdup (uri);
	loaderr->msg = g_strdup (error->message);

	g_error_free (error);

	rb_debug ("queueing error for \"%s\": %s", loaderr->uri, loaderr->msg);
	g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc) signal_err_idle, loaderr, NULL);
}

static gpointer
read_queue (GAsyncQueue *queue, gboolean *cancel)
{
	GTimeVal timeout;
	gpointer ret;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, G_USEC_PER_SEC);
	
	if (G_UNLIKELY (*cancel))
		return NULL;
	while ((ret = g_async_queue_timed_pop (queue, &timeout)) == NULL) {
		if (G_UNLIKELY (*cancel))
			return NULL;
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC);
	}

	return ret;
}

static void
add_file (const char *filename,
	  RhythmDB *db)
{
	g_async_queue_push (db->priv->add_queue, g_strdup (filename));
}

static gpointer
add_thread_main (RhythmDB *db)
{
	while (TRUE) {
		char *uri, *realuri;
		GError *error = NULL;

		uri = read_queue (db->priv->add_queue, &db->priv->exiting);
		if (!uri)
			break;

		realuri = rb_uri_resolve_symlink (uri);

		if (rb_uri_is_directory (uri) == FALSE)
			rhythmdb_add_song (db, realuri, &error);
		else
			rb_uri_handle_recursively (uri, (GFunc) add_file,
						   &db->priv->exiting, db);

		if (error != NULL)
			push_err (db, realuri, error);

		g_free (realuri);
		g_free (uri);
	}

	rb_debug ("exiting");
	g_async_queue_unref (db->priv->add_queue);
	g_async_queue_push (db->priv->main_thread_cancel, GINT_TO_POINTER (1));
	return NULL;
}

static void
update_song (RhythmDB *db, RhythmDBEntry *entry, GError **error)
{
	char *location;
	time_t stored_mtime;
	GnomeVFSFileInfo *vfsinfo = NULL;
	GnomeVFSResult result;

	rhythmdb_read_lock (db);
	location = g_strdup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_LOCATION));
	stored_mtime = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_MTIME);
	rhythmdb_read_unlock (db);
	
	vfsinfo = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (location, vfsinfo, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	if (result != GNOME_VFS_OK) {
		if (result != GNOME_VFS_ERROR_NOT_FOUND)
			g_set_error (error,
				     RHYTHMDB_ERROR,
				     RHYTHMDB_ERROR_ACCESS_FAILED,
				     "%s", gnome_vfs_result_to_string (result));
		rb_debug ("song \"%s\" was deleted", location);
		rhythmdb_write_lock (db);
		rhythmdb_entry_delete (db, entry);
		rhythmdb_write_unlock (db);
		goto out;
	}

	if (stored_mtime == vfsinfo->mtime)
		goto out;

	rhythmdb_write_lock (db);
	
	rb_debug ("song \"%s\" changed, deleting and re-adding", location);
	rhythmdb_entry_delete (db, entry);

	rhythmdb_write_unlock (db);

	rhythmdb_add_uri_async (db, location); 

out:
	g_free (location);
	gnome_vfs_file_info_unref (vfsinfo);
}

static gpointer
update_thread_main (RhythmDB *db)
{
	while (TRUE) {
		GError *error = NULL;
		RhythmDBEntry *entry;

		entry = read_queue (db->priv->update_queue, &db->priv->exiting);

		if (entry == NULL)
			break;

		update_song (db, entry, &error);
		rhythmdb_entry_unref (db, entry);

		if (error != NULL)
			push_err (db, NULL, error);
	}

	rb_debug ("exiting");
	g_async_queue_unref (db->priv->update_queue);
	g_async_queue_push (db->priv->main_thread_cancel, GINT_TO_POINTER (1));
	g_async_queue_unref (db->priv->main_thread_cancel);
	g_thread_exit (NULL);
	return NULL;
}

static gpointer
action_thread_main (RhythmDB *db)
{
	while (TRUE) {
		struct RhythmDBAction *action;

		action = read_queue (db->priv->action_queue, &db->priv->exiting);

		if (action == NULL)
			break;
		
		switch (action->type)
		{
		case RHYTHMDB_ACTION_SET:
			rhythmdb_write_lock (db);
			rhythmdb_entry_set (db, action->entry, action->propid,
					    &action->value);
			rhythmdb_write_unlock (db);
			break;
		}
		g_value_unset (&action->value);
		g_free (action);
	}

	rb_debug ("exiting");
	g_async_queue_unref (db->priv->action_queue);
	g_async_queue_push (db->priv->main_thread_cancel, GINT_TO_POINTER (1));
	g_async_queue_unref (db->priv->main_thread_cancel);
	g_thread_exit (NULL);
	return NULL;
}

void
rhythmdb_add_uri_async (RhythmDB *db, const char *uri)
{
	g_async_queue_push (db->priv->add_queue, g_strdup (uri));
}

static gpointer
rhythmdb_load_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_load (db, &db->priv->exiting);

	g_signal_emit (G_OBJECT (db), rhythmdb_signals[LOAD_COMPLETE], 0);
	g_async_queue_push (db->priv->status_queue, db);
	return NULL;
}

void
rhythmdb_load (RhythmDB *db)
{
	g_object_ref (G_OBJECT (db));
	db->priv->outstanding_threads++;
	g_thread_create ((GThreadFunc) rhythmdb_load_thread_main, db, TRUE, NULL);
}

void
rhythmdb_save (RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);
	
	klass->impl_save (db);
}

static void
set_sort_key_value (RhythmDB *db, RhythmDBClass *klass,
		    RhythmDBEntry *entry, guint propid,
		    const char *str)
{
	GValue val = {0, };
	char *key = rb_get_sort_key (str);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, key);
	klass->impl_entry_set (db, entry, propid, &val);
	g_value_unset (&val);
}

static void
set_folded_value (RhythmDB *db, RhythmDBClass *klass,
		  RhythmDBEntry *entry, guint propid,
		  const char *str)
{
	GValue val = {0, };
	char *key = g_utf8_casefold (str, -1);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, key);
	klass->impl_entry_set (db, entry, propid, &val);
	g_value_unset (&val);
}

void
rhythmdb_entry_set (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

#ifndef G_DISABLE_ASSERT	
	switch (G_VALUE_TYPE (value))
	{
	case G_TYPE_STRING:
		g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
		break;
	case G_TYPE_BOOLEAN:
	case G_TYPE_INT:
	case G_TYPE_LONG:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		break;
	default:
		g_assert_not_reached ();
		break;
	}
#endif

	db_enter (db, TRUE);

	if (!db->priv->changed_entries)
		db->priv->changed_entries = g_hash_table_new (NULL, NULL);

	g_hash_table_insert (db->priv->changed_entries, entry, NULL);

	klass->impl_entry_set (db, entry, propid, value);

	rhythmdb_entry_sync_mirrored (db, entry, propid, value);
}

void
rhythmdb_entry_queue_set (RhythmDB *db, RhythmDBEntry *entry,
			  guint propid, GValue *value)
{
	struct RhythmDBAction *action = g_new0 (struct RhythmDBAction, 1);
	action->entry = entry;
	action->propid = propid;
	g_value_init (&action->value, G_VALUE_TYPE (value));
	g_value_copy (value, &action->value);
	g_async_queue_push (db->priv->action_queue, action);
}

void
rhythmdb_entry_sync_mirrored (RhythmDB *db, RhythmDBEntry *entry, guint propid,
			      GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	switch (propid)
	{
	case RHYTHMDB_PROP_TITLE:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_TITLE_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_TITLE_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ARTIST:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ARTIST_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_ARTIST_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_ALBUM:
		set_sort_key_value (db, klass, entry,
				    RHYTHMDB_PROP_ALBUM_SORT_KEY,
				    g_value_get_string (value));
		set_folded_value (db, klass, entry,
				  RHYTHMDB_PROP_ALBUM_FOLDED,
				  g_value_get_string (value));
		break;
	case RHYTHMDB_PROP_LAST_PLAYED:
	{
		GValue tem = {0, };
		time_t last_time;

		g_value_init (&tem, G_TYPE_STRING);

		last_time = g_value_get_long (value);

		if (last_time == 0)
			g_value_set_static_string (&tem, _("Never"));
		else
			g_value_set_string_take_ownership (&tem, eel_strdup_strftime (_("%Y-%m-%d %H:%M"), localtime (&last_time)));

		klass->impl_entry_set (db, entry,
				       RHYTHMDB_PROP_LAST_PLAYED_STR,
				       &tem);
		g_value_unset (&tem);
		break;
	}
	default:
		break;
	}
}

void
rhythmdb_entry_get (RhythmDB *db, RhythmDBEntry *entry,
		    guint propid, GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	klass->impl_entry_get (db, entry, propid, value);
}

void
rhythmdb_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	db_enter (db, TRUE);

	rhythmdb_emit_entry_deleted (db, entry);
	
	klass->impl_entry_delete (db, entry);
}

GPtrArray *
rhythmdb_query_copy (GPtrArray *array)
{
	guint i;
	GPtrArray *ret;

	if (!array)
		return NULL;
	
	ret = g_ptr_array_sized_new (array->len);	
	for (i = 0; i < array->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (array, i);
		RhythmDBQueryData *new_data = g_new0 (RhythmDBQueryData, 1);
		new_data->type = data->type;
		new_data->propid = data->propid;
		if (data->val) {
			new_data->val = g_new0 (GValue, 1);
			g_value_init (new_data->val, G_VALUE_TYPE (data->val));
			g_value_copy (data->val, new_data->val);
		}
		if (data->subquery)
			new_data->subquery = rhythmdb_query_copy (data->subquery);
		g_ptr_array_add (ret, new_data);
	}

	return ret;
}

GPtrArray *
rhythmdb_query_parse_valist (RhythmDB *db, va_list args)
{
	RhythmDBQueryType query;
	GPtrArray *ret = g_ptr_array_new ();
	char *error;
	
	while ((query = va_arg (args, RhythmDBQueryType)) != RHYTHMDB_QUERY_END) {
		RhythmDBQueryData *data = g_new0 (RhythmDBQueryData, 1);
		data->type = query;
		switch (query)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			data->subquery = va_arg (args, GPtrArray *);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			data->propid = va_arg (args, guint);
			data->val = g_new0 (GValue, 1);
			g_value_init (data->val, rhythmdb_get_property_type (db, data->propid));
			G_VALUE_COLLECT (data->val, args, 0, &error);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_ptr_array_add (ret, data);
	}
	return ret;
}

GPtrArray *
rhythmdb_query_parse (RhythmDB *db, ...)
{
	GPtrArray *ret;
	va_list args;

	va_start (args, db);

	ret = rhythmdb_query_parse_valist (db, args);

	va_end (args);

	return ret;
}

void
rhythmdb_query_append (RhythmDB *db, GPtrArray *query, ...)
{
	va_list args;
	guint i;
	GPtrArray *new = g_ptr_array_new ();

	va_start (args, query);

	new = rhythmdb_query_parse_valist (db, args);

	for (i = 0; i < new->len; i++)
		g_ptr_array_add (query, g_ptr_array_index (new, i));

	g_ptr_array_free (new, FALSE);

	va_end (args);
}

void
rhythmdb_query_free (GPtrArray *query)
{
	guint i;

	if (query == NULL)
		return;
	
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (data->type)
		{
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			rhythmdb_query_free (data->subquery);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			g_value_unset (data->val);
			g_free (data->val);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_free (data);
	}

	g_ptr_array_free (query, TRUE);
}

inline const char *
rhythmdb_nice_elt_name_from_propid (RhythmDB *db, gint propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return "type";
	case RHYTHMDB_PROP_TITLE:
		return "title";
	case RHYTHMDB_PROP_TITLE_FOLDED:
		return "title-folded";
	case RHYTHMDB_PROP_TITLE_SORT_KEY:
		return "title-sort-key";
	case RHYTHMDB_PROP_GENRE:
		return "genre";
	case RHYTHMDB_PROP_GENRE_FOLDED:
		return "genre-folded";
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		return "genre-sort-key";
	case RHYTHMDB_PROP_ARTIST:
		return "artist";
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		return "artist-folded";
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		return "artist-sort-key";
	case RHYTHMDB_PROP_ALBUM:
		return "album";
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		return "album-folded";
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		return "album-sort-key";
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return "track-number";
	case RHYTHMDB_PROP_DURATION:
		return "duration";
	case RHYTHMDB_PROP_FILE_SIZE:
		return "file-size";
	case RHYTHMDB_PROP_LOCATION:
		return "location";
	case RHYTHMDB_PROP_MTIME:
		return "mtime";
	case RHYTHMDB_PROP_RATING:
		return "rating";
	case RHYTHMDB_PROP_PLAY_COUNT:
		return "play-count";
	case RHYTHMDB_PROP_LAST_PLAYED:
		return "last-played";
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		return "last-played-str";
	case RHYTHMDB_PROP_QUALITY:
		return "quality";
	default:
		g_assert_not_reached ();
	}
	return NULL;
}

inline int
rhythmdb_propid_from_nice_elt_name (RhythmDB *db, const char *name)
{
	gpointer ret, orig;	
	if (g_hash_table_lookup_extended (db->priv->propname_map, name,
					  &orig, &ret)) {
		return GPOINTER_TO_INT (ret);
	}
	return -1;
}

static void
write_encoded_gvalue (xmlNodePtr node,
		      GValue *val)
{
	char *strval;
	char *quoted;

	switch (G_VALUE_TYPE (val))
	{
	case G_TYPE_STRING:
		strval = g_value_dup_string (val);
		break;
	case G_TYPE_BOOLEAN:
		strval = g_strdup_printf ("%d", g_value_get_boolean (val));
		break;
	case G_TYPE_INT:
		strval = g_strdup_printf ("%d", g_value_get_int (val));
		break;
	case G_TYPE_LONG:
		strval = g_strdup_printf ("%ld", g_value_get_long (val));
		break;
	case G_TYPE_FLOAT:
		strval = g_strdup_printf ("%f", g_value_get_float (val));
		break;
	case G_TYPE_DOUBLE:
		strval = g_strdup_printf ("%f", g_value_get_double (val));
		break;
	default:
		g_assert_not_reached ();
		strval = NULL;
		break;
	}

	quoted = xmlEncodeEntitiesReentrant (NULL, strval);
	g_free (strval);
	xmlNodeSetContent (node, quoted);
	g_free (quoted);
}

static void
read_encoded_property (RhythmDB *db,
		       xmlNodePtr node,
		       guint propid,
		       GValue *val)
{
	char *content;
	
	g_value_init (val, db->priv->column_types[propid]);

	content = xmlNodeGetContent (node);
	
	switch (G_VALUE_TYPE (val))
	{
	case G_TYPE_STRING:
		g_value_set_string (val, content);
		break;
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_INT:
		g_value_set_int (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_LONG:
		g_value_set_long (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_FLOAT:
		g_value_set_float (val, g_ascii_strtod (content, NULL));
		break;
	case G_TYPE_DOUBLE:
		g_value_set_float (val, g_ascii_strtod (content, NULL));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_free (content);
}

void
rhythmdb_query_serialize (RhythmDB *db, GPtrArray *query,
			  xmlNodePtr parent)
{
	guint i;
	xmlNodePtr node = xmlNewChild (parent, NULL, "conjunction", NULL);
	xmlNodePtr subnode;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		
		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
			subnode = xmlNewChild (node, NULL, "subquery", NULL);
			rhythmdb_query_serialize (db, data->subquery, subnode);
			break;
		case RHYTHMDB_QUERY_PROP_LIKE:
			subnode = xmlNewChild (node, NULL, "like", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
			subnode = xmlNewChild (node, NULL, "not-like", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
			subnode = xmlNewChild (node, NULL, "equals", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_DISJUNCTION:
			subnode = xmlNewChild (node, NULL, "disjunction", NULL);
			break;
		case RHYTHMDB_QUERY_END:
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			g_assert_not_reached ();
			break;
		}		
	}
}

GPtrArray *
rhythmdb_query_deserialize (RhythmDB *db, xmlNodePtr parent)
{
	GPtrArray *query = g_ptr_array_new ();
	xmlNodePtr child;

	g_assert (!strcmp (parent->name, "conjunction"));
	
	for (child = parent->children; child; child = child->next) {
		RhythmDBQueryData *data;

		if (xmlNodeIsText (child))
			continue;

		data = g_new0 (RhythmDBQueryData, 1);

		if (!strcmp (child->name, "subquery")) {
			xmlNodePtr subquery;
			data->type = RHYTHMDB_QUERY_SUBQUERY;
			subquery = child->children;
			while (xmlNodeIsText (subquery))
				subquery = subquery->next;
			
			data->subquery = rhythmdb_query_deserialize (db, subquery);
		} else if (!strcmp (child->name, "disjunction")) {
			data->type = RHYTHMDB_QUERY_DISJUNCTION;
		} else if (!strcmp (child->name, "like")) {
			data->type = RHYTHMDB_QUERY_PROP_LIKE;
		} else if (!strcmp (child->name, "not-like")) {
			data->type = RHYTHMDB_QUERY_PROP_NOT_LIKE;
		} else if (!strcmp (child->name, "equals")) {
			data->type = RHYTHMDB_QUERY_PROP_EQUALS;
		} else
 			g_assert_not_reached ();

		if (!strcmp (child->name, "like")
		    || !strcmp (child->name, "not-like")
		    || !strcmp (child->name, "equals")) {
			char *propstr = xmlGetProp (child, "prop");
			gint propid = rhythmdb_propid_from_nice_elt_name (db, propstr);
			g_free (propstr);

			g_assert (propid >= 0 && propid < RHYTHMDB_NUM_PROPERTIES);

			data->propid = propid;
			data->val = g_new0 (GValue, 1);

			read_encoded_property (db, child, data->propid, data->val);
		} 

		g_ptr_array_add (query, data);
	}

	return query;
}

RhythmDBEntry *
rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	return klass->impl_lookup_by_location (db, uri);
}

gboolean
rhythmdb_evaluate_query (RhythmDB *db, GPtrArray *query, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	db_enter (db, FALSE);

	return klass->impl_evaluate_query (db, query, entry);
}

gpointer
query_thread_main (struct RhythmDBQueryThreadData *data,
		   RhythmDB *db)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	rb_debug ("doing query");

	/* Async queries now expect the db to be locked */
/* 	if (data->lock) */
/* 		rhythmdb_read_lock (db); */
	
	klass->impl_do_full_query (db, data->query,
				   data->main_model,
				   &data->cancel);


	rb_debug ("completed");
	rhythmdb_query_model_signal_complete (RHYTHMDB_QUERY_MODEL (data->main_model));
	if (data->lock)
		rhythmdb_read_unlock (db);
	rhythmdb_query_model_finish_complete (RHYTHMDB_QUERY_MODEL (data->main_model));

	rhythmdb_query_free (data->query);
	g_async_queue_push (db->priv->status_queue, data->main_model);
	g_free (data);
	return NULL;
}

void
rhythmdb_do_full_query_async_parsed (RhythmDB *db, GtkTreeModel *main_model,
				     GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	db_enter (db, FALSE);

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->query = rhythmdb_query_copy (query);
	data->main_model = main_model;
	data->cancel = FALSE;
	data->lock = TRUE;

	g_object_set (G_OBJECT (RHYTHMDB_QUERY_MODEL (main_model)),
		      "query", query, NULL);

	g_object_ref (G_OBJECT (main_model));
	db->priv->outstanding_threads++;
	g_thread_pool_push (db->priv->query_thread_pool, data, NULL);
}

void
rhythmdb_do_full_query_async (RhythmDB *db, GtkTreeModel *main_model, ...)
{
	GPtrArray *query;
	va_list args;

	va_start (args, main_model);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_async_parsed (db, main_model, query);

	rhythmdb_query_free (query);

	va_end (args);
}

static void
rhythmdb_do_full_query_internal (RhythmDB *db, GtkTreeModel *main_model,
				 GPtrArray *query)
{
	struct RhythmDBQueryThreadData *data;

	data = g_new0 (struct RhythmDBQueryThreadData, 1);
	data->query = rhythmdb_query_copy (query);
	data->main_model = main_model;
	data->cancel = FALSE;
	data->lock = FALSE;

	g_object_set (G_OBJECT (RHYTHMDB_QUERY_MODEL (main_model)),
		      "query", query, NULL);

	g_object_ref (G_OBJECT (main_model));
	db->priv->outstanding_threads++;
	query_thread_main (data, db);
	while (rhythmdb_query_model_poll (RHYTHMDB_QUERY_MODEL (main_model), NULL))
		;
}

void
rhythmdb_do_full_query_parsed (RhythmDB *db, GtkTreeModel *main_model,
			       GPtrArray *query)
{
	db_enter (db, FALSE);

	rhythmdb_do_full_query_internal (db, main_model, query);
}

void
rhythmdb_do_full_query (RhythmDB *db, GtkTreeModel *main_model, ...)
{
	GPtrArray *query;
	va_list args;

	db_enter (db, FALSE);

	va_start (args, main_model);

	query = rhythmdb_query_parse_valist (db, args);

	rhythmdb_do_full_query_internal (db, main_model, query);

	rhythmdb_query_free (query);
	
	va_end (args);
}

GType rhythmdb_get_property_type (RhythmDB *db, guint property_id)
{
	g_return_val_if_fail (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES,
			      G_TYPE_INVALID);

	return db->priv->column_types[property_id];
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rhythmdb_query_get_type (void)
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
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_GREATER, "True iff property1 > property2"),
			ENUM_ENTRY (RHYTHMDB_QUERY_PROP_LESS, "True iff property1 < property2"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBQueryType", values);
	}

	return etype;
}

GType
rhythmdb_prop_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_PROP_TYPE, "Type of entry (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE, "Title (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_FILE_SIZE, "File Size (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LOCATION, "Location (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_MTIME, "Modification time (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_RATING, "Rating (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAY_COUNT, "Play Count (gint)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED, "Last Played (glong)"),
			ENUM_ENTRY (RHYTHMDB_PROP_QUALITY, "Quality (gint)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBPropType", values);
	}

	return etype;
}


GType
rhythmdb_unsaved_prop_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_SORT_KEY, "Title sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (gchararray)"),

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_FOLDED, "Title folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_FOLDED, "Genre folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_FOLDED, "Artist folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_FOLDED, "Album folded (gchararray)"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED_STR, "Last Played (gchararray)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBUnsavedPropType", values);
	}

	return etype;
}

const char *
rhythmdb_entry_get_string (RhythmDB *db, RhythmDBEntry *entry, guint propid)
{ 
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	GValue gval = {0, };
	const char *ret;
	db_enter (db, FALSE);
	g_value_init (&gval, G_TYPE_STRING);
	klass->impl_entry_get (db, entry, propid, &gval);
	ret = g_value_get_string (&gval);
	return ret;
}

#define DEFINE_GETTER(NAME, TYPE, GTYPE, DEFAULT)	\
TYPE \
rhythmdb_entry_get_ ## NAME (RhythmDB *db, RhythmDBEntry *entry, guint propid) \
{ \
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); \
	GValue gval = {0, }; \
	TYPE retval; \
	db_enter (db, FALSE); \
	g_value_init (&gval, G_TYPE_ ## GTYPE); \
	klass->impl_entry_get (db, entry, propid, &gval); \
	retval = g_value_get_ ## NAME (&gval); \
	return retval; \
}

DEFINE_GETTER(boolean, gboolean, BOOLEAN, FALSE)
DEFINE_GETTER(pointer, gpointer, POINTER, NULL)
DEFINE_GETTER(long, long, LONG, 0)
DEFINE_GETTER(int, int, INT, 0)
DEFINE_GETTER(double, double, DOUBLE, 0)
DEFINE_GETTER(float, float, FLOAT, 0)
#undef DEFINE_GETTER

void
rhythmdb_emit_entry_added (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, entry);
}

void
rhythmdb_emit_entry_restored (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_RESTORED], 0, entry);
	if (rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_TYPE) == RHYTHMDB_ENTRY_TYPE_SONG) {
		rhythmdb_entry_ref_unlocked (db, entry);
		g_async_queue_push (db->priv->update_queue, entry);
	}
}

void
rhythmdb_emit_entry_deleted (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
}

typedef struct
{
	RhythmDB *db;
	char *libname;
} RhythmDBLegacyLoadData;

RhythmDBEntry *
rhythmdb_legacy_id_to_entry (RhythmDB *db, guint id)
{
	return g_hash_table_lookup (db->priv->legacy_id_map, GINT_TO_POINTER (id));
}

static gboolean
emit_legacy_load_complete (RhythmDB *db)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (db), rhythmdb_signals[LEGACY_LOAD_COMPLETE], 0);
	g_hash_table_destroy (db->priv->legacy_id_map);
	g_object_unref (G_OBJECT (db));

	GDK_THREADS_LEAVE ();

	return FALSE;
}

gpointer
legacy_load_thread_main (RhythmDBLegacyLoadData *data)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	guint id;

	doc = xmlParseFile (data->libname);

	if (doc == NULL) {
		g_object_unref (G_OBJECT (data->db));
		goto free_exit;
	}

	rb_debug ("parsing entries");
	root = xmlDocGetRootElement (doc);
	for (child = root->children; child != NULL; child = child->next) {
		RhythmDBEntry *entry = 
			rhythmdb_legacy_parse_rbnode (data->db, RHYTHMDB_ENTRY_TYPE_SONG, child,
						      &id);

		if (id > 0)
			g_hash_table_insert (data->db->priv->legacy_id_map, GINT_TO_POINTER (id),
					     entry);
		if (entry) {
			rhythmdb_entry_ref (data->db, entry);
			g_async_queue_push (data->db->priv->update_queue, entry);
		}
	}
	xmlFreeDoc (doc);

	/* steals the library ref */
	g_idle_add ((GSourceFunc) emit_legacy_load_complete, data->db);
	rb_debug ("legacy load thread exiting");
free_exit:
	g_free (data->libname);
	g_free (data);
	g_thread_exit (NULL);
	return NULL;
}

void
rhythmdb_load_legacy (RhythmDB *db)
{
	RhythmDBLegacyLoadData *data;
	char *libname = g_build_filename (rb_dot_dir (), "library-2.1.xml", NULL);
	xmlDocPtr doc;
	xmlNodePtr root, child;

	if (!g_file_test (libname, G_FILE_TEST_EXISTS)) {
		g_free (libname);
		goto load_iradio;
	}

	data = g_new0 (RhythmDBLegacyLoadData, 1);
	data->db = db;
	g_object_ref (G_OBJECT (data->db));
	data->libname = libname;
	
	rb_debug ("kicking off library legacy loading thread");
	g_thread_create ((GThreadFunc) legacy_load_thread_main, data, FALSE, NULL);

load_iradio:
	libname = g_build_filename (rb_dot_dir (), "iradio-2.2.xml", NULL);
	if (!g_file_test (libname, G_FILE_TEST_EXISTS)) {
		g_free (libname);
		return;
	}

	doc = xmlParseFile (libname);
	g_free (libname);

	if (doc == NULL)
		return;

	root = xmlDocGetRootElement (doc);
	for (child = root->children; child != NULL; child = child->next) {
		rhythmdb_legacy_parse_rbnode (db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION,
					      child, NULL);
	}
	xmlFreeDoc (doc);
}

static gboolean
queue_is_empty (GAsyncQueue *queue)
{
	return g_async_queue_length (queue) <= 0;
}

char *
rhythmdb_get_status (RhythmDB *db)
{
	char *ret = NULL;

	if (!queue_is_empty (db->priv->add_queue))
		ret = g_strdup_printf ("<b>%s</b>", _("Loading songs..."));
	else if (!queue_is_empty (db->priv->update_queue))
		ret = g_strdup_printf ("<b>%s</b>", _("Refreshing songs..."));

	return ret;
}

char *
rhythmdb_compute_status_normal (gint n_songs, glong duration, GnomeVFSFileSize size)
{
	float days;
	long hours, minutes, seconds;
	char *songcount;
	char *time;
	char *size_str;
	char *ret;

	songcount = g_strdup_printf (ngettext ("%d song", "%d songs", n_songs), n_songs);

	days    = (float) duration / (float) (60 * 60 * 24); 
	hours   = duration / (60 * 60);
	minutes = duration / 60 - hours * 60;
	seconds = duration % 60;

	if (days >= 1.0) {
		time = ngettext ("%.1f day", "%.1f days", days);
		time = g_strdup_printf (time, days);			
	} else {
		const char *minutefmt = ngettext ("%ld minute", "%ld minutes", minutes);
		if (hours >= 1) {		
			const char *hourfmt = ngettext ("%ld hour", "%ld hours", hours);
			char *fmt;
			if (minutes > 0) {
				/* Translators: the format is "X hours and X minutes" */
				fmt = g_strdup_printf (_("%s and %s"), hourfmt, minutefmt);
			} else {
				/* Translators: the format is "X hours" */
				fmt = g_strdup_printf ("%s", hourfmt);
			}
			time = g_strdup_printf (fmt, hours, minutes);
			g_free (fmt);
		} else 
			time = g_strdup_printf (minutefmt, minutes);
	}
	size_str = gnome_vfs_format_file_size_for_display (size);
	ret = g_strdup_printf ("%s, %s, %s", songcount, time, size_str);
	g_free (songcount);
	g_free (time);
	g_free (size_str);

	return ret;
}

