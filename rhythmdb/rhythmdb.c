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
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"
#include "rb-metadata.h"
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
	char **column_xml_names;

	GThreadPool *query_thread_pool;

	/* Used for metadata reading */
	RBMetaData *metadata;
	GnomeVFSFileInfo *vfsinfo;

	gboolean query_in_progress;

	GAsyncQueue *main_thread_cancel;
	GAsyncQueue *update_queue;
	GAsyncQueue *add_queue;
	GAsyncQueue *action_queue;

	gboolean dry_run;
	gboolean no_update;

	gboolean writelocked;
	guint readlocks;

	GList *added_entries;
	GList *changed_entries;

	GHashTable *propname_map;

	guint thread_reaper_id;

	GAsyncQueue *status_queue;
	gint outstanding_threads;

	GMutex *exit_mutex;
	gboolean exiting;
	
	GCond *saving_condition;
	GMutex *saving_mutex;

	gboolean saving;
	gboolean dirty;
};

struct RhythmDBAction
{
	enum {
		RHYTHMDB_ACTION_SET,
		RHYTHMDB_ACTION_SYNC,
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

struct RhythmDBEntryChangeData
{
	RhythmDBEntry *entry;
	RhythmDBPropType prop;
	GValue old;
	GValue new;
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
	PROP_DRY_RUN,
	PROP_NO_UPDATE,
};

enum
{
	ENTRY_ADDED,
	ENTRY_RESTORED,
	ENTRY_CHANGED,
	ENTRY_DELETED,
	LOAD_COMPLETE,
	SAVE_COMPLETE,
	ERROR,
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
			      rb_marshal_VOID__POINTER_INT_POINTER_POINTER,
			      G_TYPE_NONE, 4, G_TYPE_POINTER,
			      G_TYPE_INT, G_TYPE_POINTER, G_TYPE_POINTER);

	rhythmdb_signals[LOAD_COMPLETE] =
		g_signal_new ("load_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	rhythmdb_signals[SAVE_COMPLETE] =
		g_signal_new ("save_complete",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, save_complete),
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
}

#if 0
static gboolean
prop_from_metadata_field (RBMetaDataField field, RhythmDBPropType *prop)
{
	switch (field) {
	case RB_METADATA_FIELD_TITLE:
		*prop = RHYTHMDB_PROP_TITLE;
		return TRUE;		
	case RB_METADATA_FIELD_ARTIST:
		*prop = RHYTHMDB_PROP_ARTIST; 
		return TRUE;		
	case RB_METADATA_FIELD_ALBUM:
		*prop = RHYTHMDB_PROP_ALBUM; 
		return TRUE;		
	case RB_METADATA_FIELD_GENRE:
		*prop = RHYTHMDB_PROP_GENRE; 
		return TRUE;		
	case RB_METADATA_FIELD_TRACK_NUMBER:
		*prop = RHYTHMDB_PROP_TRACK_NUMBER; 
		return TRUE;		
	case RB_METADATA_FIELD_DISC_NUMBER:
		*prop = RHYTHMDB_PROP_DISC_NUMBER; 
		return TRUE;		
	case RB_METADATA_FIELD_DURATION:
		*prop = RHYTHMDB_PROP_DURATION; 
		return TRUE;		
	case RB_METADATA_FIELD_BITRATE:
		*prop = RHYTHMDB_PROP_BITRATE; 
		return TRUE;		
	default:
		return FALSE;
	}
}
#endif

static gboolean
metadata_field_from_prop (RhythmDBPropType prop, RBMetaDataField *field)
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
	case RHYTHMDB_PROP_DURATION:
		*field = RB_METADATA_FIELD_DURATION; 
		return TRUE;
	case RHYTHMDB_PROP_BITRATE:
		*field = RB_METADATA_FIELD_BITRATE; 
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
	default:
		return FALSE;
	}
}

static GType
extract_gtype_from_enum_entry (RhythmDB *db, GEnumClass *klass, guint i)
{
	GType ret;
	GEnumValue *value;
	RBMetaDataField field;
	char *typename;
	char *typename_end;
	
	value = g_enum_get_value (klass, i);

	/* First check to see whether this is a property that maps to
	   a RBMetaData property. */
	if (metadata_field_from_prop (value->value, &field))
		return rb_metadata_get_field_type (db->priv->metadata, field); 

	/* This is a "synthetic" property. */

	typename = strstr (value->value_nick, "(");
	g_return_val_if_fail (typename != NULL, G_TYPE_INVALID);

	typename_end = strstr (typename, ")");
	typename++;
	typename = g_strndup (typename, typename_end-typename);
	ret = g_type_from_name (typename);
	g_free (typename);
	return ret;
}

static char *
extract_nice_name_from_enum_entry (RhythmDB *db, GEnumClass *klass, guint i)
{
	GEnumValue *value;
	char *name;
	char *name_end;
	
	value = g_enum_get_value (klass, i);

	name = strstr (value->value_nick, "[");
	g_return_val_if_fail (name != NULL, NULL);
	name_end = strstr (name, "]");
	name++;

	return g_strndup (name, name_end-name);
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

	db->priv->metadata = rb_metadata_new ();
	
	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

	g_assert (prop_class->n_values + unsaved_prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);
	g_assert (prop_class->n_values == RHYTHMDB_NUM_SAVED_PROPERTIES);
	db->priv->column_types = g_new (GType, RHYTHMDB_NUM_PROPERTIES);
	db->priv->column_xml_names = g_new (char *, RHYTHMDB_NUM_PROPERTIES);
	
	/* Now, extract the GType and XML tag of each column from the
	 * enum descriptions, and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (db, prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
		db->priv->column_xml_names[i] = extract_nice_name_from_enum_entry (db, prop_class, i);
		g_assert (db->priv->column_xml_names[i]);
	}
			
	
	for (j = 0; j < unsaved_prop_class->n_values; i++, j++) {
		db->priv->column_types[i] = extract_gtype_from_enum_entry (db, unsaved_prop_class, i);
		g_assert (db->priv->column_types[i] != G_TYPE_INVALID);
		db->priv->column_xml_names[i] = extract_nice_name_from_enum_entry (db, unsaved_prop_class, i);
		g_assert (db->priv->column_xml_names[i]);
	}

	g_type_class_unref (prop_class);
	g_type_class_unref (unsaved_prop_class);

	db->priv->propname_map = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		const char *name = rhythmdb_nice_elt_name_from_propid (db, i);
		g_hash_table_insert (db->priv->propname_map, (gpointer) name, GINT_TO_POINTER (i));
	}

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
	
	db->priv->saving_condition = g_cond_new ();
	db->priv->saving_mutex = g_mutex_new ();

	db->priv->exiting = FALSE;
	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;
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

	g_object_unref (db->priv->metadata);

	g_static_rw_lock_free (&db->priv->lock);

	g_thread_pool_free (db->priv->query_thread_pool, TRUE, FALSE);

	g_async_queue_unref (db->priv->status_queue);

	g_mutex_free (db->priv->saving_mutex);
	g_cond_free (db->priv->saving_condition);

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
	case PROP_DRY_RUN:
		source->priv->dry_run = g_value_get_boolean (value);
		break;
	case PROP_NO_UPDATE:
		source->priv->no_update = g_value_get_boolean (value);
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

	g_assert (db->priv->added_entries == NULL);
	g_assert (db->priv->changed_entries == NULL);
}

static void
emit_changed_signals (RhythmDB *db, gboolean commit)
{
	GList *tem;
	GHashTable *queued_entry_changes = g_hash_table_new (NULL, NULL);

	for (tem = db->priv->changed_entries; tem; tem = tem->next) {
		struct RhythmDBEntryChangeData *data = tem->data;

		if (commit) {
			struct RhythmDBAction *action;
			RBMetaDataField field;

			if (rhythmdb_entry_get_int (db, data->entry, RHYTHMDB_PROP_TYPE) == RHYTHMDB_ENTRY_TYPE_SONG
			    && !g_hash_table_lookup (queued_entry_changes, data->entry)
			    && metadata_field_from_prop (data->prop, &field)) {
				action = g_new0 (struct RhythmDBAction, 1);
				action->type = RHYTHMDB_ACTION_SYNC;
				action->entry = data->entry;
				g_hash_table_insert (queued_entry_changes, data->entry, action);
				g_async_queue_push (db->priv->action_queue, action);
			}
			g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, data->entry,
				       data->prop, &data->old, &data->new);
		}
		g_value_unset (&data->old);
		g_value_unset (&data->new);
		g_free (data);
	}
	g_list_free (db->priv->changed_entries);
	db->priv->changed_entries = NULL;
	g_hash_table_destroy (queued_entry_changes);
}

static void
rhythmdb_write_unlock_internal (RhythmDB *db, gboolean commit)
{
       GList *tem;

	emit_changed_signals (db, commit);

	for (tem = db->priv->added_entries; tem; tem = tem->next)
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, tem->data);
	g_list_free (db->priv->added_entries);
	db->priv->added_entries = NULL;
		 
	g_static_rw_lock_writer_unlock (&db->priv->lock);
}

void
rhythmdb_write_unlock (RhythmDB *db)
{
	rhythmdb_write_unlock_internal (db, TRUE);
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
#ifdef RHYTHMDB_ENABLE_SANITY_CHECK
	if (write) 
		g_assert (db->priv->lock.have_writer);
	else
		g_assert (db->priv->lock.read_counter > 0
			  || db->priv->lock.have_writer);
#endif
}

RhythmDBEntry *
rhythmdb_entry_new (RhythmDB *db, RhythmDBEntryType type, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	RhythmDBEntry *ret;

	db_enter (db, TRUE);
	
	ret = klass->impl_entry_new (db, type, uri);
	rb_debug ("emitting entry added");

	if (ret != NULL)
		db->priv->added_entries = g_list_append (db->priv->added_entries, ret);
	return ret;
}

gboolean
rhythmdb_entry_is_editable (RhythmDB *db, RhythmDBEntry *entry)
{
	return rb_metadata_can_save (db->priv->metadata,
				     rhythmdb_entry_get_string (db, entry,
								RHYTHMDB_PROP_MIMETYPE));
}

/* Threading: any thread
 */
static void
read_metadata_async (RhythmDB *db, const char *location, GError **real_error)
{
	GError *error = NULL;

	rb_metadata_load (db->priv->metadata, location, &error);
	if (error != NULL)
		g_propagate_error (real_error, error);

	if (db->priv->vfsinfo)
		gnome_vfs_file_info_unref (db->priv->vfsinfo);
	db->priv->vfsinfo = gnome_vfs_file_info_new ();
	gnome_vfs_get_file_info (location, db->priv->vfsinfo, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
}

static void
set_metadata_string_default_unknown (RhythmDB *db, RhythmDBEntry *entry,
				     RBMetaDataField field,
				     RhythmDBPropType prop)
{
	const char *unknown = _("Unknown");
	GValue val = {0, };
	
	if (!(rb_metadata_get (db->priv->metadata,
			       field,
			       &val))) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_static_string (&val, unknown);
	} else if (g_value_get_string (&val)[0] == '\0')
		g_value_set_static_string (&val, unknown);
	rhythmdb_entry_set (db, entry, prop, &val);
	g_value_unset (&val);
}

static void
set_props_from_metadata (RhythmDB *db, RhythmDBEntry *entry)
{
	const char *mime;
	GValue val = {0,};

	g_value_init (&val, G_TYPE_STRING);
	mime = rb_metadata_get_mime (db->priv->metadata);
	if (mime) {
		g_value_set_string (&val, mime);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MIMETYPE, &val);
	}
	g_value_unset (&val);

	/* track number */
	if (!rb_metadata_get (db->priv->metadata,
			      RB_METADATA_FIELD_TRACK_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, -1);
	}
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
	g_value_unset (&val);

	/* disc number */
	if (!rb_metadata_get (db->priv->metadata,
			      RB_METADATA_FIELD_DISC_NUMBER,
			      &val)) {
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, -1);
	}

	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DISC_NUMBER, &val);
	g_value_unset (&val);

	/* duration */
	if (rb_metadata_get (db->priv->metadata,
			     RB_METADATA_FIELD_DURATION,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	/* bitrate */
	if (rb_metadata_get (db->priv->metadata,
			     RB_METADATA_FIELD_BITRATE,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_BITRATE, &val);
		g_value_unset (&val);
	}

	/* filesize */
	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, db->priv->vfsinfo->size);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	/* title */
	if (!rb_metadata_get (db->priv->metadata,
			      RB_METADATA_FIELD_TITLE,
			      &val) || g_value_get_string (&val)[0] == '\0') {
		char *utf8name;
		utf8name = g_filename_to_utf8 (db->priv->vfsinfo->name, -1, NULL, NULL, NULL);
		if (!utf8name) {
			utf8name = g_strdup (_("<invalid filename>"));
		}
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, utf8name);
		g_free (utf8name);
	}
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	/* mtime */
	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, db->priv->vfsinfo->mtime);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MTIME, &val);
	g_value_unset (&val);

	gnome_vfs_file_info_unref (db->priv->vfsinfo);
	db->priv->vfsinfo = NULL;

	/* genre */
	set_metadata_string_default_unknown (db, entry, RB_METADATA_FIELD_GENRE,
					     RHYTHMDB_PROP_GENRE);

	/* artist */
	set_metadata_string_default_unknown (db, entry, RB_METADATA_FIELD_ARTIST,
					     RHYTHMDB_PROP_ARTIST);
	/* album */
	set_metadata_string_default_unknown (db, entry, RB_METADATA_FIELD_ALBUM,
					     RHYTHMDB_PROP_ALBUM);

	/* replaygain track gain */
        if (rb_metadata_get (db->priv->metadata,
                             RB_METADATA_FIELD_TRACK_GAIN,
                             &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_GAIN, &val);
		g_value_unset (&val);
	}

	/* replaygain track peak */
	if (rb_metadata_get (db->priv->metadata,
			     RB_METADATA_FIELD_TRACK_PEAK,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_PEAK, &val);
		g_value_unset (&val);
	}

	/* replaygain album gain */
	if (rb_metadata_get (db->priv->metadata,
			     RB_METADATA_FIELD_ALBUM_GAIN,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM_GAIN, &val);
		g_value_unset (&val);
	}

	/* replaygain album peak */
	if (rb_metadata_get (db->priv->metadata,
			     RB_METADATA_FIELD_ALBUM_PEAK,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM_PEAK, &val);
		g_value_unset (&val);
	}
}

RhythmDBEntry *
rhythmdb_add_song (RhythmDB *db, const char *uri, GError **real_error)
{
	RhythmDBEntry *entry = NULL;
	char *realuri;
	const char *mime;
	GError *error = NULL;
	GValue value = {0, };

	realuri = rb_uri_resolve_symlink (uri);

	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_lookup_by_location (db, realuri);

	if (entry) {
		rhythmdb_entry_ref_unlocked (db, entry);
		rhythmdb_write_unlock (db);
		update_song (db, entry, &error);
		if (error != NULL)
			g_propagate_error (real_error, error);
		rhythmdb_entry_unref (db, entry);
		return entry;
	}

	rhythmdb_write_unlock (db);

	/* Don't do file access with the db write lock held */
	read_metadata_async (db, uri, &error);
	if (error) {
		g_propagate_error (real_error, error);
		rb_debug ("failed to read data from \"%s\"", uri);
		goto out_freeuri;
	}

	mime = rb_metadata_get_mime (db->priv->metadata);

	if (!mime) {
		rb_debug ("unsupported file");
		goto out_freeuri;
	}

	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, realuri);
	if (entry == NULL)
		goto out_dupentry;
	set_props_from_metadata (db, entry);

	/* initialize the last played date to 0=never */
	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, 0);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &value);
	g_value_unset (&value);

	/* initialize the rating */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, 2.5);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);

	/* initialize auto rating */
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, TRUE);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_AUTO_RATE, &value);
	g_value_unset (&value);
	
 out_dupentry:
	rhythmdb_write_unlock_internal (db, FALSE);
 out_freeuri:
	g_free (realuri);

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
			g_value_unset (&action->value);
			break;
		case RHYTHMDB_ACTION_SYNC:
		{
			char *location;
			GError *error = NULL;

			if (db->priv->dry_run) {
				rb_debug ("dry run is enabled, not syncing metadata");
				break;
			}
			
			rhythmdb_read_lock (db);
			location = g_strdup (rhythmdb_entry_get_string (db, action->entry,
									RHYTHMDB_PROP_LOCATION));
			rhythmdb_read_unlock (db);
			
			rb_metadata_load (db->priv->metadata,
					  location, &error);
			if (error != NULL) {
				g_warning ("error loading metadata from %s: %s", location,
					   error->message);
				/* FIXME */
				g_free (location);
				break;
			}

			rb_metadata_save (db->priv->metadata, &error);
			if (error != NULL) {
				g_warning ("error saving metadata to %s: %s", location,
					   error->message);
				/* FIXME */
				g_free (location);
				break;
			}
				
			g_free (location);
			break;
		}
		}
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
	g_thread_create ((GThreadFunc) rhythmdb_load_thread_main, db, FALSE, NULL);
}

static void
rhythmdb_save_worker (RhythmDB *db)
{
	RhythmDBClass *klass;
	
	g_mutex_lock (db->priv->saving_mutex);

	if (db->priv->saving) {
		rb_debug ("already saving, ignoring");
		g_mutex_unlock (db->priv->saving_mutex);
		return;
	}

	if (!db->priv->dirty) {
		rb_debug ("no save needed, ignoring");
		g_mutex_unlock (db->priv->saving_mutex);
		return;
	}

	db->priv->saving = TRUE;

	g_mutex_unlock (db->priv->saving_mutex);

	rb_debug ("saving rhythmdb");
			
	klass = RHYTHMDB_GET_CLASS (db);
	db_enter (db, FALSE);
	klass->impl_save (db);

	g_mutex_lock (db->priv->saving_mutex);

	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	g_mutex_unlock (db->priv->saving_mutex);

	g_cond_broadcast (db->priv->saving_condition);
}

static gpointer
rhythmdb_save_thread_main (RhythmDB *db)
{
	rb_debug ("entering save thread");
	
	rhythmdb_save_worker (db);

	g_signal_emit (G_OBJECT (db), rhythmdb_signals[SAVE_COMPLETE], 0);
	g_async_queue_push (db->priv->status_queue, db);
	
	return NULL;
}

void
rhythmdb_save (RhythmDB *db)
{
	rb_debug ("saving the rhythmdb in the background");

	g_object_ref (G_OBJECT (db));
	db->priv->outstanding_threads++;
	
	g_thread_create ((GThreadFunc) rhythmdb_save_thread_main, db, FALSE, NULL);
}

void
rhythmdb_save_blocking (RhythmDB *db)
{
	rb_debug("saving the rhythmdb and blocking");
	
	rhythmdb_save_worker (db);
	
	g_mutex_lock (db->priv->saving_mutex);

	while (db->priv->saving)
		g_cond_wait (db->priv->saving_condition, db->priv->saving_mutex);

	g_mutex_unlock (db->priv->saving_mutex);
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
	struct RhythmDBEntryChangeData *changedata;

#ifndef G_DISABLE_ASSERT	
	switch (G_VALUE_TYPE (value))
	{
	case G_TYPE_STRING:
		g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
		break;
	case G_TYPE_BOOLEAN:
	case G_TYPE_INT:
	case G_TYPE_LONG:
	case G_TYPE_UINT64:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		break;
	default:
		g_assert_not_reached ();
		break;
	}
#endif

	db_enter (db, TRUE);

	changedata = g_new0 (struct RhythmDBEntryChangeData, 1);
	changedata->entry = entry;
	changedata->prop = propid;

	/* Copy a temporary gvalue, since _entry_get uses
	 * _set_static_string to avoid memory allocations. */
	{
		GValue tem = {0,};
		g_value_init (&tem, G_VALUE_TYPE (value));
		klass->impl_entry_get (db, entry, propid, &tem);
		g_value_init (&changedata->old, G_VALUE_TYPE (value));
		g_value_copy (&tem, &changedata->old);
		g_value_unset (&tem);
	}
	g_value_init (&changedata->new, G_VALUE_TYPE (value));
	g_value_copy (value, &changedata->new);
	db->priv->changed_entries = g_list_append (db->priv->changed_entries, changedata);

	klass->impl_entry_set (db, entry, propid, value);

	rhythmdb_entry_sync_mirrored (db, entry, propid, value);
	
	/* set the dirty state */
	db->priv->dirty = TRUE;
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
	
	/* deleting an entry makes the db dirty */
	db->priv->dirty = TRUE;
}

void
rhythmdb_entry_delete_by_type (RhythmDB *db, RhythmDBEntryType type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	db_enter (db, TRUE);

	if (klass->impl_entry_delete_by_type) {
		klass->impl_entry_delete_by_type (db, type);
	} else {
		g_warning ("delete_by_type not implemented");
	}
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

static GPtrArray *
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

	g_ptr_array_free (new, TRUE);

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
	return db->priv->column_xml_names[propid];
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
	case G_TYPE_UINT64:
		strval = g_strdup_printf ("%llu", g_value_get_uint64 (val));
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
	case G_TYPE_UINT64:
		g_value_set_uint64 (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_FLOAT:
		g_value_set_float (val, g_ascii_strtod (content, NULL));
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (val, g_ascii_strtod (content, NULL));
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
			subnode = xmlNewChild (node, NULL, "greater", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			subnode = xmlNewChild (node, NULL, "less", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
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
		} else if (!strcmp (child->name, "greater")) {
			data->type = RHYTHMDB_QUERY_PROP_GREATER;
		} else if (!strcmp (child->name, "less")) {
			data->type = RHYTHMDB_QUERY_PROP_LESS;
		} else
 			g_assert_not_reached ();

		if (!strcmp (child->name, "like")
		    || !strcmp (child->name, "not-like")
		    || !strcmp (child->name, "equals")
		    || !strcmp (child->name, "greater")
		    || !strcmp (child->name, "less")) {
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
			/* We reuse the description to store extra data about
			* a property.  The first part is just a generic
			* human-readable description.  Next, there is optionally
			* a string describing the GType of the property, in
			* parenthesis.  If this doesn't exist, we assume
			* the property is mirrored from RBMetaData, and
			* get the type of the property from there.
			* Finally, there is the XML element name in brackets.
			*/
			ENUM_ENTRY (RHYTHMDB_PROP_TYPE, "Type of entry (gint) [type]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE, "Title [title]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre [genre]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist [artist]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album [album]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number [track-number]"),
			ENUM_ENTRY (RHYTHMDB_PROP_DISC_NUMBER, "Disc Number [disc-number]"),

			ENUM_ENTRY (RHYTHMDB_PROP_DURATION, "Duration [duration]"),
			ENUM_ENTRY (RHYTHMDB_PROP_FILE_SIZE, "File Size (guint64) [file-size]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LOCATION, "Location (gchararray) [location]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MTIME, "Modification time (glong) [mtime]"),
			ENUM_ENTRY (RHYTHMDB_PROP_RATING, "Rating (gdouble) [rating]"),
			ENUM_ENTRY (RHYTHMDB_PROP_AUTO_RATE, "Whether to auto-rate song (gboolean) [auto-rate]"),
			ENUM_ENTRY (RHYTHMDB_PROP_PLAY_COUNT, "Play Count (gint) [play-count]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED, "Last Played (glong) [last-played]"),
			ENUM_ENTRY (RHYTHMDB_PROP_BITRATE, "Bitrate [bitrate]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_GAIN, "Replaygain track gain [replaygain-track-gain]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_PEAK, "Replaygain track peak [replaygain-track-peak]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_GAIN, "Replaygain album pain [replaygain-album-gain]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_PEAK, "Replaygain album peak [replaygain-album-peak]"),
			ENUM_ENTRY (RHYTHMDB_PROP_MIMETYPE, "Mime Type (gchararray) [mimetype]"),
			{ 0, 0, 0 }
		};
		g_assert ((sizeof (values) / sizeof (values[0]) - 1) == RHYTHMDB_NUM_SAVED_PROPERTIES);
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

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_SORT_KEY, "Title sort key (gchararray) [title-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_SORT_KEY, "Genre sort key (gchararray) [genre-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_SORT_KEY, "Artist sort key (gchararray) [artist-sort-key]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_SORT_KEY, "Album sort key (gchararray) [album-sort-key]"),

			ENUM_ENTRY (RHYTHMDB_PROP_TITLE_FOLDED, "Title folded (gchararray) [title-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE_FOLDED, "Genre folded (gchararray) [genre-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST_FOLDED, "Artist folded (gchararray) [artist-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM_FOLDED, "Album folded (gchararray) [album-folded]"),
			ENUM_ENTRY (RHYTHMDB_PROP_LAST_PLAYED_STR, "Last Played (gchararray) [last-played-str]"),
			{ 0, 0, 0 }
		};
		g_assert ((sizeof (values) / sizeof (values[0]) - 1) == RHYTHMDB_NUM_PROPERTIES - RHYTHMDB_NUM_SAVED_PROPERTIES);
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
DEFINE_GETTER(uint64, guint64, UINT64, 0)
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
	if (!db->priv->no_update
	    && rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_TYPE) == RHYTHMDB_ENTRY_TYPE_SONG) {
		rhythmdb_entry_ref_unlocked (db, entry);
		g_async_queue_push (db->priv->update_queue, entry);
	}
}

void
rhythmdb_emit_entry_deleted (RhythmDB *db, RhythmDBEntry *entry)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
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
	long days, hours, minutes, seconds;
	char *songcount;
	char *time;
	char *size_str;
	char *ret;
	const char *minutefmt;
	const char *hourfmt;	
	const char *dayfmt;

	songcount = g_strdup_printf (ngettext ("%d song", "%d songs", n_songs), n_songs);

	days    = duration / (60 * 60 * 24); 
	hours   = (duration / (60 * 60)) - (days * 24);
	minutes = (duration / 60) - ((days * 24 * 60) + (hours * 60));
	seconds = duration % 60;

	minutefmt = ngettext ("%ld minute", "%ld minutes", minutes);
	hourfmt = ngettext ("%ld hour", "%ld hours", hours);
	dayfmt = ngettext ("%ld day", "%ld days", days);
	if (days >= 1) {
		char *fmt;
		/* Translators: the format is "X days, X hours and X minutes" */
		fmt = g_strdup_printf (_("%s, %s and %s"), dayfmt, hourfmt, minutefmt);
		time = g_strdup_printf (fmt, days, hours, minutes);
		g_free (fmt);
	} else {
		if (hours >= 1) {		
			const char *hourfmt = ngettext ("%ld hour", "%ld hours", hours);
			char *fmt;
			/* Translators: the format is "X hours and X minutes" */
			fmt = g_strdup_printf (_("%s and %s"), hourfmt, minutefmt);
			time = g_strdup_printf (fmt, hours, minutes);
			g_free (fmt);
		} else {
			time = g_strdup_printf (minutefmt, minutes);
		}
	}

	size_str = gnome_vfs_format_file_size_for_display (size);
	ret = g_strdup_printf ("%s, %s, %s", songcount, time, size_str);
	g_free (songcount);
	g_free (time);
	g_free (size_str);

	return ret;
}

static RBAtomic last_entry_type = {0};

RhythmDBEntryType
rhythmdb_entry_register_type (void)
{
	/* FIXME: does it need locking ? */
	return rb_atomic_inc (&last_entry_type);		
}

RhythmDBEntryType rhythmdb_entry_song_get_type (void) 
{
	static RhythmDBEntryType song_type = -1;
       
	if (song_type == -1) {
		song_type = rhythmdb_entry_register_type ();
	}

	return song_type;
}

RhythmDBEntryType rhythmdb_entry_iradio_get_type (void) 
{
	static RhythmDBEntryType iradio_type = -1;
       
	if (iradio_type == -1) {
		iradio_type = rhythmdb_entry_register_type ();
	}

	return iradio_type;
}
