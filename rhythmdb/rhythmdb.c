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

#include <config.h>

#define	G_IMPLEMENT_INLINES 1
#define	__RHYTHMDB_C__
#include "rhythmdb.h"
#undef G_IMPLEMENT_INLINES

#include "rhythmdb-private.h"
#include "rhythmdb-property-model.h"
#include "rb-metadata.h"
#include <string.h>
#include <gobject/gvaluecollector.h>
#include <glib.h>
#include <glib-object.h>
#include <gconf/gconf-client.h>
#include <gdk/gdk.h>
#include <libxml/tree.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <glib/gi18n.h>
#include "rb-marshal.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-preferences.h"
#include "widgets/eel-gconf-extensions.h"

#define RB_PARSE_CONJ (xmlChar *) "conjunction"
#define RB_PARSE_SUBQUERY (xmlChar *) "subquery"
#define RB_PARSE_LIKE (xmlChar *) "like"
#define RB_PARSE_PROP (xmlChar *) "prop"
#define RB_PARSE_NOT_LIKE (xmlChar *) "not-like"
#define RB_PARSE_PREFIX (xmlChar *) "prefix"
#define RB_PARSE_SUFFIX (xmlChar *) "suffix"
#define RB_PARSE_EQUALS (xmlChar *) "equals"
#define RB_PARSE_DISJ (xmlChar *) "disjunction"
#define RB_PARSE_GREATER (xmlChar *) "greater"
#define RB_PARSE_LESS (xmlChar *) "less"
#define RB_PARSE_CURRENT_TIME_WITHIN (xmlChar *) "current-time-within"
#define RB_PARSE_CURRENT_TIME_NOT_WITHIN (xmlChar *) "current-time-not-within"
#define RB_PARSE_YEAR_EQUALS RB_PARSE_EQUALS
#define RB_PARSE_YEAR_GREATER RB_PARSE_GREATER
#define RB_PARSE_YEAR_LESS RB_PARSE_LESS

#define RB_PARSE_NICK_START (xmlChar *) "["
#define RB_PARSE_NICK_END (xmlChar *) "]"

#define RHYTHMDB_FILE_MODIFY_PROCESS_TIME 2

GType rhythmdb_property_type_map[RHYTHMDB_NUM_PROPERTIES];

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

#define RHYTHMDB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RHYTHMDB_TYPE, RhythmDBPrivate))
G_DEFINE_ABSTRACT_TYPE(RhythmDB, rhythmdb, G_TYPE_OBJECT)

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
	char *uri;
	RhythmDBEntryType type;
} RhythmDBAddThreadData;

typedef struct
{
	enum {
		RHYTHMDB_ACTION_STAT,
		RHYTHMDB_ACTION_LOAD,
		RHYTHMDB_ACTION_SYNC
	} type;
	char *uri;
 	RhythmDBEntryType entry_type;
} RhythmDBAction;

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

static void rhythmdb_finalize (GObject *object);
static void rhythmdb_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rhythmdb_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
static void rhythmdb_thread_create (RhythmDB *db, GThreadFunc func, gpointer data);
static void rhythmdb_read_enter (RhythmDB *db);
static void rhythmdb_read_leave (RhythmDB *db);
static gboolean rhythmdb_idle_poll_events (RhythmDB *db);
static gpointer add_thread_main (RhythmDBAddThreadData *data);
static gpointer action_thread_main (RhythmDB *db);
static gpointer query_thread_main (RhythmDBQueryThreadData *data);
static void queue_stat_uri (const char *uri, RhythmDB *db, RhythmDBEntryType type);
static void rhythmdb_entry_set_internal (RhythmDB *db, RhythmDBEntry *entry, gboolean notify_if_inserted, guint propid, const GValue *value);
static void rhythmdb_entry_set_mount_point (RhythmDB *db, 
 					    RhythmDBEntry *entry, 
 					    const gchar *realuri);
static void rhythmdb_entry_set_visibility (RhythmDB *db, RhythmDBEntry *entry, 
 					   gboolean visibility);
 
static void rhythmdb_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
 					GnomeVFSVolume *volume, 
 					gpointer data);
static void rhythmdb_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
 					  GnomeVFSVolume *volume, 
 					  gpointer data);
static gboolean free_entry_changes (RhythmDBEntry *entry, 
				    GSList *changes,
				    RhythmDB *db);
static gboolean rhythmdb_idle_save (RhythmDB *db);
static void library_location_changed_cb (GConfClient *client,
					  guint cnxn_id,
					  GConfEntry *entry,
					  RhythmDB *db);
static void rhythmdb_sync_library_location (RhythmDB *db);
static gboolean rhythmdb_process_changed_files (RhythmDB *db);
static void rhythmdb_monitor_uri_path (RhythmDB *db,
				       const char *uri,
				       GError **error);

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
	LOAD_COMPLETE,
	SAVE_COMPLETE,
	SAVE_ERROR,
	READ_ONLY,
	LAST_SIGNAL
};

static guint rhythmdb_signals[LAST_SIGNAL] = { 0 };

static void
rhythmdb_class_init (RhythmDBClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

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
			      1, RHYTHMDB_TYPE_ENTRY);

	rhythmdb_signals[ENTRY_DELETED] =
		g_signal_new ("entry_deleted",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);

	rhythmdb_signals[ENTRY_CHANGED] =
		g_signal_new ("entry_changed",
			      RHYTHMDB_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBClass, entry_changed),
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE, 2, 
			      RHYTHMDB_TYPE_ENTRY, G_TYPE_POINTER);

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

	g_type_class_add_private (klass, sizeof (RhythmDBPrivate));
}

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
extract_nice_name_from_enum_entry (RhythmDB *db, GEnumClass *klass, guint i)
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

	return xmlStrndup (name, name_end-name);
}

static void
rhythmdb_init (RhythmDB *db)
{
	guint i;
	GEnumClass *prop_class;

	db->priv = RHYTHMDB_GET_PRIVATE (db);

	db->priv->action_queue = g_async_queue_new ();
	db->priv->event_queue = g_async_queue_new ();
	db->priv->restored_queue = g_async_queue_new ();

	db->priv->metadata = rb_metadata_new ();
	
	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP_TYPE);

	g_assert (prop_class->n_values == RHYTHMDB_NUM_PROPERTIES);
	db->priv->column_xml_names = g_new (xmlChar *, RHYTHMDB_NUM_PROPERTIES);
	
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

	db->priv->monitored_directories = g_hash_table_new_full (g_str_hash, g_str_equal,
								 (GDestroyNotify) g_free,
								 NULL);
	db->priv->changed_files = g_hash_table_new_full (g_str_hash, g_str_equal,
							 (GDestroyNotify) g_free,
							 NULL);

 	db->priv->stat_events = g_hash_table_new_full (gnome_vfs_uri_hash, (GEqualFunc) gnome_vfs_uri_equal,
 						       (GDestroyNotify) gnome_vfs_uri_unref,
 						       NULL);
 	db->priv->stat_mutex = g_mutex_new ();

	db->priv->change_mutex = g_mutex_new ();
	db->priv->changed_entries = g_hash_table_new (NULL, NULL);
	db->priv->added_entries = g_hash_table_new (NULL, NULL);
	db->priv->deleted_entries = g_hash_table_new (NULL, NULL);
	
	db->priv->event_poll_id = g_idle_add ((GSourceFunc) rhythmdb_idle_poll_events, db);

	db->priv->saving_condition = g_cond_new ();
	db->priv->saving_mutex = g_mutex_new ();

	db->priv->exiting = FALSE;
	db->priv->saving = FALSE;
	db->priv->dirty = FALSE;

	db->priv->empty_string = rb_refstring_new ("");
	db->priv->octet_stream_str = rb_refstring_new ("application/octet-stream");      

	g_signal_connect (G_OBJECT (gnome_vfs_get_volume_monitor ()), 
			  "volume-mounted", 
			  G_CALLBACK (rhythmdb_volume_mounted_cb), 
			  db);

	g_signal_connect (G_OBJECT (gnome_vfs_get_volume_monitor ()), 
			  "volume-pre-unmount", 
			  G_CALLBACK (rhythmdb_volume_unmounted_cb), 
			  db);
	g_signal_connect (G_OBJECT (gnome_vfs_get_volume_monitor ()), 
			  "volume-unmounted", 
			  G_CALLBACK (rhythmdb_volume_unmounted_cb), 
			  db);
}

static void
rhythmdb_execute_multi_stat_info_cb (GnomeVFSAsyncHandle *handle,
				     GList *results, /* GnomeVFSGetFileInfoResult* items */
				     RhythmDB *db)
{
	g_mutex_lock (db->priv->stat_mutex);
	while (results != NULL) {
		GnomeVFSGetFileInfoResult *info_result = results->data;
		RhythmDBEvent *event;

		event = g_hash_table_lookup (db->priv->stat_events, info_result->uri);
		if (event == NULL) {
			char *uri_string;
			uri_string = gnome_vfs_uri_to_string (info_result->uri, GNOME_VFS_URI_HIDE_NONE);
			rb_debug ("ignoring unexpected uri in gnome_vfs_async_get_file_info response: %s",
				  uri_string);
			g_free (uri_string);
			results = results->next;
			continue;
		}
		g_hash_table_remove (db->priv->stat_events, info_result->uri);
		
		if (info_result->result == GNOME_VFS_OK) {
			event->vfsinfo = gnome_vfs_file_info_dup (info_result->file_info);
		} else {
			char *unescaped = gnome_vfs_unescape_string_for_display (event->real_uri);
			event->error = g_error_new (RHYTHMDB_ERROR,
						    RHYTHMDB_ERROR_ACCESS_FAILED,
						    _("Couldn't access %s: %s"),
						    unescaped,
						    gnome_vfs_result_to_string (info_result->result));
			rb_debug ("got error on %s: %s", unescaped, event->error->message);
			g_free (unescaped);
			event->vfsinfo = NULL;
		}
		g_async_queue_push (db->priv->event_queue, event);

		results = results->next;
	}
	g_mutex_unlock (db->priv->stat_mutex);
}

void
rhythmdb_start_action_thread (RhythmDB *db)
{
	g_mutex_lock (db->priv->stat_mutex);
	db->priv->action_thread_running = TRUE;
	rhythmdb_thread_create (db, (GThreadFunc) action_thread_main, db);

	if (db->priv->stat_list != NULL) {
		gnome_vfs_async_get_file_info (&db->priv->stat_handle, db->priv->stat_list,
					       GNOME_VFS_FILE_INFO_FOLLOW_LINKS,
					       GNOME_VFS_PRIORITY_MIN,
					       (GnomeVFSAsyncGetFileInfoCallback) rhythmdb_execute_multi_stat_info_cb,
					       db);
		g_list_free (db->priv->stat_list);
		db->priv->stat_list = NULL;
	}

	g_mutex_unlock (db->priv->stat_mutex);
}

static void
rhythmdb_action_free (RhythmDB *db, RhythmDBAction *action)
{
	switch (action->type) {
	case RHYTHMDB_ACTION_STAT:
	case RHYTHMDB_ACTION_LOAD:
	case RHYTHMDB_ACTION_SYNC:
		g_free (action->uri);
		break;
	}
	g_free (action);
}

static void
rhythmdb_event_free (RhythmDB *db, RhythmDBEvent *result)
{
	switch (result->type) {
	case RHYTHMDB_EVENT_STAT:
		g_free (result->uri);
		g_free (result->real_uri);
		if (result->vfsinfo)
			gnome_vfs_file_info_unref (result->vfsinfo);
		break;
	case RHYTHMDB_EVENT_METADATA_LOAD:
		g_free (result->uri);
		g_free (result->real_uri);
		if (result->vfsinfo)
			gnome_vfs_file_info_unref (result->vfsinfo);
		if (result->metadata)
			g_object_unref (result->metadata);
		g_clear_error (&result->error);
		break;
	case RHYTHMDB_EVENT_DB_LOAD:
		break;
	case RHYTHMDB_EVENT_THREAD_EXITED:
		g_object_unref (db);
		g_assert (g_atomic_int_dec_and_test (&db->priv->outstanding_threads) >= 0);
		g_async_queue_unref (db->priv->action_queue);
		g_async_queue_unref (db->priv->event_queue);
		break;
	case RHYTHMDB_EVENT_DB_SAVED:
		break;
	case RHYTHMDB_EVENT_QUERY_COMPLETE:
		g_object_unref (result->results);
		break;
	case RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED:
		break;
	case RHYTHMDB_EVENT_FILE_DELETED:	       
		break;
	case RHYTHMDB_EVENT_ENTRY_SET:
		g_value_unset (&result->change.new);
		break;
	}
	g_free (result);
}

static gboolean
rhythmdb_unmonitor_directories (char *dir, GnomeVFSMonitorHandle *handle, RhythmDB *db)
{
	gnome_vfs_monitor_cancel (handle);
	return TRUE;
}


/**
 * rhythmdb_shutdown:
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

	db->priv->exiting = TRUE;

	eel_gconf_notification_remove (db->priv->library_location_notify_id);
	g_hash_table_foreach_remove (db->priv->monitored_directories, (GHRFunc) rhythmdb_unmonitor_directories,
				     db);
	g_slist_foreach (db->priv->library_locations, (GFunc) g_free, NULL);
	g_slist_free (db->priv->library_locations);
	db->priv->library_locations = NULL;

	while ((action = g_async_queue_try_pop (db->priv->action_queue)) != NULL) {
		rhythmdb_action_free (db, action);
	}

	
	rb_debug ("%d outstanding threads", g_atomic_int_get (&db->priv->outstanding_threads));
	while (g_atomic_int_get (&db->priv->outstanding_threads) > 0) {
		result = g_async_queue_pop (db->priv->event_queue);
		rhythmdb_event_free (db, result);
	}

	//FIXME
	while ((result = g_async_queue_try_pop (db->priv->event_queue)) != NULL)
		rhythmdb_event_free (db, result);
}

static void
rhythmdb_finalize (GObject *object)
{
	RhythmDB *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS (object));

	db = RHYTHMDB (object);

	g_return_if_fail (db->priv != NULL);

	g_source_remove (db->priv->event_poll_id);
	if (db->priv->save_timeout_id > 0) {
		g_source_remove (db->priv->save_timeout_id);
	}

	g_async_queue_unref (db->priv->action_queue);
	g_async_queue_unref (db->priv->event_queue);
	g_async_queue_unref (db->priv->restored_queue);

	g_mutex_free (db->priv->saving_mutex);
	g_cond_free (db->priv->saving_condition);

	g_mutex_free (db->priv->change_mutex);

	g_hash_table_destroy (db->priv->propname_map);

	g_hash_table_destroy (db->priv->monitored_directories);
	g_source_remove (db->priv->changed_files_id);
	g_hash_table_destroy (db->priv->changed_files);

	g_hash_table_destroy (db->priv->added_entries);
	g_hash_table_destroy (db->priv->deleted_entries);
	g_hash_table_destroy (db->priv->changed_entries);

	rb_refstring_unref (db->priv->empty_string);
	rb_refstring_unref (db->priv->octet_stream_str);

	g_free (db->priv->name);
	
	G_OBJECT_CLASS (rhythmdb_parent_class)->finalize (object);
}

static void
rhythmdb_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RhythmDB *source = RHYTHMDB (object);

	switch (prop_id) {
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
rhythmdb_thread_create (RhythmDB *db, GThreadFunc func, gpointer data)
{
	g_object_ref (G_OBJECT (db));
	g_atomic_int_inc (&db->priv->outstanding_threads);
	g_async_queue_ref (db->priv->action_queue);
	g_async_queue_ref (db->priv->event_queue);
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
	if (count == 1)
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[READ_ONLY],
			       0, FALSE);
}

static gboolean
free_entry_changes (RhythmDBEntry *entry, GSList *changes, RhythmDB *db)
{
	GSList *t;
	for (t = changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;
		g_value_unset (&change->old);
		g_value_unset (&change->new);
		g_free (change);
	}
	g_slist_free (changes);
	return TRUE;
}

static void
emit_entry_changed (RhythmDBEntry *entry, GSList *changes, RhythmDB *db)
{
	g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_CHANGED], 0, entry, changes);
}

static void
sync_entry_changed (RhythmDBEntry *entry, GSList *changes, RhythmDB *db)
{
	if (rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_SONG) {
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
				
				action = g_new0 (RhythmDBAction, 1);
				action->type = RHYTHMDB_ACTION_SYNC;
				action->uri = g_strdup (entry->location);
				g_async_queue_push (db->priv->action_queue, action);
				break;
			}
		}
	}
}



static gboolean
rhythmdb_emit_entry_signals_idle (RhythmDB *db)
{
	GList *added_entries;
	GList *deleted_entries;
	GList *l;

	/* get lists of entries to emit, reset source id value */
	g_mutex_lock (db->priv->change_mutex);

	added_entries = db->priv->added_entries_to_emit;
	db->priv->added_entries_to_emit = NULL;

	deleted_entries = db->priv->deleted_entries_to_emit;
	db->priv->deleted_entries_to_emit = NULL;

	db->priv->emit_entry_signals_id = 0;
	
	g_mutex_unlock (db->priv->change_mutex);


	/* emit added entries */
	for (l = added_entries; l; l = g_list_next (l)) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_ADDED], 0, entry);
		rhythmdb_entry_unref (db, entry);
	}

	/* emit deleted entries */
	for (l = deleted_entries; l; l = g_list_next (l)) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;
		g_signal_emit (G_OBJECT (db), rhythmdb_signals[ENTRY_DELETED], 0, entry);
		rhythmdb_entry_unref (db, entry);
	}

	g_list_free (added_entries);
	g_list_free (deleted_entries);
	return FALSE;
}

static gboolean
process_added_entries_cb (RhythmDBEntry *entry, GThread *thread, RhythmDB *db)
{
	if (thread != g_thread_self ())
		return FALSE;
	
	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		const gchar *uri;

		uri = rhythmdb_entry_get_string (entry, 
						 RHYTHMDB_PROP_LOCATION);

		/* always start remote files hidden*/
		if (!g_str_has_prefix (uri, "file://"))
			entry->hidden = TRUE;

		queue_stat_uri (uri, db, RHYTHMDB_ENTRY_TYPE_INVALID);
	}

	g_assert (entry->inserted == FALSE);
	entry->inserted = TRUE;
	
	rhythmdb_entry_ref (db, entry);
	db->priv->added_entries_to_emit = g_list_prepend (db->priv->added_entries_to_emit, entry);
	return TRUE;
}

static gboolean
process_deleted_entries_cb (RhythmDBEntry *entry, GThread *thread, RhythmDB *db)
{
	if (thread != g_thread_self ())
		return FALSE;
	
	rhythmdb_entry_ref (db, entry);
	db->priv->deleted_entries_to_emit = g_list_prepend (db->priv->deleted_entries_to_emit, entry);
	return TRUE;
}

static void
rhythmdb_commit_internal (RhythmDB *db, gboolean sync_changes, GThread *thread)
{
	g_mutex_lock (db->priv->change_mutex);

	g_hash_table_foreach (db->priv->changed_entries, (GHFunc) emit_entry_changed, db);
	if (sync_changes)
		g_hash_table_foreach (db->priv->changed_entries, (GHFunc) sync_entry_changed, db);
	g_hash_table_foreach_remove (db->priv->changed_entries, (GHRFunc) free_entry_changes, db);

	/* update the lists of entry added/deleted signals to emit */
	g_hash_table_foreach_remove (db->priv->added_entries, (GHRFunc) process_added_entries_cb, db);
	g_hash_table_foreach_remove (db->priv->deleted_entries, (GHRFunc) process_deleted_entries_cb, db);

	/* if there are some signals to emit, add a new idle callback if required */
	if (db->priv->added_entries_to_emit || db->priv->deleted_entries_to_emit) {
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
	g_free (data);
	return FALSE;
}

static void
rhythmdb_add_timeout_commit (RhythmDB *db, gboolean sync)
{
	RhythmDBTimeoutCommitData *data;

	g_assert (rb_is_main_thread ());

	data = g_new0 (RhythmDBTimeoutCommitData, 1);
	data->db = db;
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

/**
 * rhythmdb_entry_allocate:
 * @db: a #RhythmDB.
 * @type: type of entry to allocate
 *
 * Allocate and initialise memory for a new #RhythmDBEntry of the type @type.
 * The entry's initial properties needs to be set with rhythmdb_entry_set_uninserted(),
 * the entry added to the database with rhythmdb_entry_insert(), and committed with
 * rhythmdb_commit().
 *
 * This should only be used by RhythmDB itself, or a backend (such as rhythmdb-tree).
 *
 * Returns: the newly allocated #RhythmDBEntry
 **/
RhythmDBEntry *
rhythmdb_entry_allocate (RhythmDB *db, RhythmDBEntryType type)
{
	RhythmDBEntry *ret;
	ret = g_new0 (RhythmDBEntry, 1);

	ret->type = type;
	ret->title = rb_refstring_ref (db->priv->empty_string);
	ret->genre = rb_refstring_ref (db->priv->empty_string);
	ret->artist = rb_refstring_ref (db->priv->empty_string);
	ret->album = rb_refstring_ref (db->priv->empty_string);
	ret->mimetype = rb_refstring_ref (db->priv->octet_stream_str);

	if ((type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) ||
	    (type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED))
	       ret->podcast = g_new0 (RhythmDBPodcastFields, 1);
	else
		ret->podcast = NULL;

	rhythmdb_entry_sync_mirrored (db, ret, RHYTHMDB_PROP_LAST_PLAYED);
	rhythmdb_entry_sync_mirrored (db, ret, RHYTHMDB_PROP_FIRST_SEEN);
	
	/* The refcount is initially 0, we want to set it to 1 */
	g_atomic_int_inc (&ret->refcount);

	if (type->post_entry_create)
		(type->post_entry_create)(ret, type->post_entry_create_data);

	return ret;
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
rhythmdb_entry_insert (RhythmDB *db, RhythmDBEntry *entry)
{
	g_assert (entry->inserted == FALSE);
	g_return_if_fail (entry->location != NULL);
	
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
rhythmdb_entry_new (RhythmDB *db, RhythmDBEntryType type, const char *uri)
{
	RhythmDBEntry *ret;
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	ret = rhythmdb_entry_lookup_by_location (db, uri);
	if (ret) {
		g_warning ("attempting to create entry that already exists: %s", uri);
		return NULL;
	}

	ret = rhythmdb_entry_allocate (db, type);
	ret->location = g_strdup (uri);
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
rhythmdb_entry_example_new (RhythmDB *db, RhythmDBEntryType type, const char *uri)
{
	RhythmDBEntry *ret;
	
	ret = rhythmdb_entry_allocate (db, type);
	ret->location = g_strdup (uri);

	if (type == RHYTHMDB_ENTRY_TYPE_SONG) {
		rb_refstring_unref (ret->artist);
		ret->artist = rb_refstring_new ("The Beatles");
		rb_refstring_unref (ret->album);
		ret->album = rb_refstring_new ("Help!");
		rb_refstring_unref (ret->title);
		ret->title = rb_refstring_new ("Ticket To Ride");
		ret->tracknum = 7;
	} else {
	}

	return ret;
}

/**
 * rhythmdb_entry_ref:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 *
 * Increase the reference count of the entry.
 **/
void
rhythmdb_entry_ref (RhythmDB *db, RhythmDBEntry *entry)
{
	g_atomic_int_inc (&entry->refcount);
}

static void
rhythmdb_entry_finalize (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBEntryType type;

	type = rhythmdb_entry_get_entry_type (entry);
	if (type->pre_entry_destroy)
		(type->pre_entry_destroy)(entry, type->pre_entry_destroy_data);

	g_free (entry->location);
	g_free (entry->playback_error);
	if (entry->date)
		g_date_free (entry->date);
	rb_refstring_unref (entry->title);
	rb_refstring_unref (entry->genre);
	rb_refstring_unref (entry->artist);
	rb_refstring_unref (entry->album);
	rb_refstring_unref (entry->mimetype);

	if (entry->podcast) {
		rb_refstring_unref (entry->podcast->description);
		rb_refstring_unref (entry->podcast->subtitle);
		rb_refstring_unref (entry->podcast->summary);
		rb_refstring_unref (entry->podcast->lang);
		rb_refstring_unref (entry->podcast->copyright);
		rb_refstring_unref (entry->podcast->image);
		g_free (entry->podcast);
		entry->podcast = NULL;
	}

	g_free (entry);
}

/**
 * rhythmdb_entry_unref:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 *
 * Decrease the reference count of the entry, and destroy it if there are
 * no references left.
 **/
void
rhythmdb_entry_unref (RhythmDB *db, RhythmDBEntry *entry)
{
	if (g_atomic_int_dec_and_test (&entry->refcount))
		rhythmdb_entry_finalize (db, entry);
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
rhythmdb_entry_is_editable (RhythmDB *db, RhythmDBEntry *entry)
{
	return rb_metadata_can_save (db->priv->metadata,
				     rb_refstring_get (entry->mimetype));
}

static void
rhythmdb_directory_change_cb (GnomeVFSMonitorHandle *handle,
			      const char *monitor_uri,
			      const char *info_uri,
			      GnomeVFSMonitorEventType vfsevent,
			      RhythmDB *db)
{
	rb_debug ("directory event %d for %s: %s", (int) vfsevent,
		  monitor_uri, info_uri);

	switch (vfsevent) {
        case GNOME_VFS_MONITOR_EVENT_CREATED:
		{
			GSList *cur;
			gboolean in_library = FALSE;
			
			if (!eel_gconf_get_boolean (CONF_MONITOR_LIBRARY))
				return;

			/* don't notices new files outside of the library locations */
			for (cur = db->priv->library_locations; cur != NULL; cur = g_slist_next (cur)) {
				if (g_str_has_prefix (info_uri, cur->data)) {
					in_library = TRUE;
					break;
				}
			}
		
			if (!in_library)
				return;	
		}
		
		/* process directories immediately */
		if (rb_uri_is_directory (info_uri)) {
			rhythmdb_monitor_uri_path (db, info_uri, NULL);
			rhythmdb_add_uri (db, info_uri);
			return;
		}
		/* fall through*/
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
        case GNOME_VFS_MONITOR_EVENT_METADATA_CHANGED:
		{
			GTimeVal time;

			g_get_current_time (&time);
			g_hash_table_replace (db->priv->changed_files,
					      g_strdup (info_uri),
					      GINT_TO_POINTER (time.tv_sec));
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		{
			RhythmDBEvent *event = g_new0 (RhythmDBEvent, 1);
			event->db = db;
			event->type = RHYTHMDB_EVENT_FILE_DELETED;
			event->uri = g_strdup (info_uri);
			g_async_queue_push (db->priv->event_queue, event);
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_STARTEXECUTING:
	case GNOME_VFS_MONITOR_EVENT_STOPEXECUTING:
		break;
	}
}

static void
rhythmdb_monitor_uri_path (RhythmDB *db, const char *uri, GError **error)
{
	char *directory;

	if (rb_uri_is_directory (uri))
		if (g_str_has_suffix(uri, G_DIR_SEPARATOR_S))
			directory = g_strdup (uri);
		else
			directory = g_strconcat (uri, G_DIR_SEPARATOR_S, NULL);
	else {
		GnomeVFSURI *vfsuri, *parent;
		
		vfsuri = gnome_vfs_uri_new (uri);
		parent = gnome_vfs_uri_get_parent (vfsuri);
		directory = gnome_vfs_uri_to_string (parent, GNOME_VFS_URI_HIDE_NONE);
		gnome_vfs_uri_unref (vfsuri);
		gnome_vfs_uri_unref (parent);
	}

	if (directory == NULL)
		return;

	if (!g_hash_table_lookup (db->priv->monitored_directories, directory)) {
		GnomeVFSResult vfsresult;
		GnomeVFSMonitorHandle **handle = g_new0 (GnomeVFSMonitorHandle *, 1);
		vfsresult = gnome_vfs_monitor_add (handle, directory,
						   GNOME_VFS_MONITOR_DIRECTORY,
						   (GnomeVFSMonitorCallback) rhythmdb_directory_change_cb,
						   db);
		if (vfsresult == GNOME_VFS_OK) {
			rb_debug ("monitoring: %s", directory);
			g_hash_table_insert (db->priv->monitored_directories,
					     directory, *handle);
		} else {
			g_set_error (error,
				     RHYTHMDB_ERROR,
				     RHYTHMDB_ERROR_ACCESS_FAILED,
				     _("Couldn't monitor %s: %s"),
				     directory,
				     gnome_vfs_result_to_string (vfsresult));
			rb_debug ("failed to monitor %s", directory);
			g_free (directory);
			g_free (handle);
		}
	}
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
set_props_from_metadata (RhythmDB *db, RhythmDBEntry *entry,
			 GnomeVFSFileInfo *vfsinfo, RBMetaData *metadata)
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

	/* filesize */
	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, vfsinfo->size);
	rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	/* title */
	if (!rb_metadata_get (metadata,
			      RB_METADATA_FIELD_TITLE,
			      &val) || g_value_get_string (&val)[0] == '\0') {
		char *utf8name;
		utf8name = g_filename_to_utf8 (vfsinfo->name, -1, NULL, NULL, NULL);
		if (!utf8name) {
			utf8name = g_strdup (_("<invalid filename>"));
		}
		if (G_VALUE_HOLDS_STRING (&val))
			g_value_reset (&val);
		else
			g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, utf8name);
		g_free (utf8name);
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
	if ((grace_period < 0) || (grace_period > 20000)) {
		return FALSE;
	}

	/* Convert from days to seconds */
	grace_period = grace_period * 60 * 60 * 24;
	g_get_current_time (&time);
	last_seen = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_LAST_SEEN);

	return (last_seen + grace_period < time.tv_sec);
}


static void
rhythmdb_process_stat_event (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	RhythmDBAction *action;

	entry = rhythmdb_entry_lookup_by_location (db, event->real_uri);
	if (entry) {
		time_t mtime = (time_t) entry->mtime;

		if ((event->entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) && (entry->type != event->entry_type))
			g_warning ("attempt to use same location in multiple entry types");

		if (entry->type == RHYTHMDB_ENTRY_TYPE_IGNORE)
			rb_debug ("ignoring %p", entry);

		if (event->error) {
			if (!is_ghost_entry (entry)) {
				rhythmdb_entry_set_visibility (db, entry, FALSE);
			} else {
				rb_debug ("error accessing %s: %s", event->real_uri,
					  event->error->message);
				rhythmdb_entry_delete (db, entry);
			}
		} else {
			GValue val = {0, };
			GTimeVal time;
			const char *mount_point;

			rhythmdb_entry_set_visibility (db, entry, TRUE);
				
			/* Update mount point if necessary (main reason is 
			 * that we want to set the mount point in legacy
			 * rhythmdb that doesn't have it already
			 */
			mount_point = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
			if (mount_point == NULL) {
				rhythmdb_entry_set_mount_point (db, entry,
								event->real_uri);
			}

			/* Update last seen time. It will also be updated
			 * upon saving and when a volume is unmounted 
			 */
			g_get_current_time (&time);
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, time.tv_sec);
			rhythmdb_entry_set_internal (db, entry, TRUE,
						     RHYTHMDB_PROP_LAST_SEEN,
						     &val);
			/* Old rhythmdb.xml files won't have a value for
			 * FIRST_SEEN, so set it here
			 */
			if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_FIRST_SEEN) == 0) {
				rhythmdb_entry_set_internal (db, entry, TRUE,
							     RHYTHMDB_PROP_FIRST_SEEN,
							     &val);
			}
			g_value_unset (&val);
					    
			if (mtime == event->vfsinfo->mtime) {
				rb_debug ("not modified: %s", event->real_uri);
				/* monitor the file for changes */
				if (eel_gconf_get_boolean (CONF_MONITOR_LIBRARY))
					rhythmdb_monitor_uri_path (db, entry->location, NULL /* FIXME */);
			} else {
				RhythmDBEvent *new_event;

				rb_debug ("changed: %s", event->real_uri);
				new_event = g_new0 (RhythmDBEvent, 1);
				new_event->db = db;
				new_event->uri = g_strdup (event->real_uri);
				new_event->type = RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED;
				g_async_queue_push (db->priv->event_queue, 
						    new_event);
			}
		}

		rhythmdb_commit (db);
	} else {
		action = g_new0 (RhythmDBAction, 1);
		action->type = RHYTHMDB_ACTION_LOAD;
		action->uri = g_strdup (event->real_uri);
		action->entry_type = event->entry_type;
		rb_debug ("queuing a RHYTHMDB_ACTION_LOAD: %s", action->uri);
		g_async_queue_push (db->priv->action_queue, action);
	}
}

typedef struct
{
	RhythmDB *db;
	char *uri;
	char *msg;
} RhythmDBLoadErrorData;

static void
rhythmdb_add_import_error_entry (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	GValue value = {0,};
	RhythmDBEntryType error_entry_type = RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR;

	if (g_error_matches (event->error, RB_METADATA_ERROR, RB_METADATA_ERROR_NOT_AUDIO_IGNORE)) {
		/* only add an ignore entry for the main library */
		if (event->entry_type != RHYTHMDB_ENTRY_TYPE_SONG)
			return;

		error_entry_type = RHYTHMDB_ENTRY_TYPE_IGNORE;
	}
	
	entry = rhythmdb_entry_lookup_by_location (db, event->real_uri);
	if (entry) {
		RhythmDBEntryType entry_type = rhythmdb_entry_get_entry_type (entry);
		if (entry_type != RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR &&
		    entry_type != RHYTHMDB_ENTRY_TYPE_IGNORE) {
			/* FIXME we've successfully read this file before.. so what should we do? */
			rb_debug ("%s already exists in the library.. ignoring import error?", event->real_uri);
			return;
		}

		if (entry_type != error_entry_type) {
			/* delete the existing entry, then create a new one below */
			rhythmdb_entry_delete (db, entry);
			entry = NULL;
		} else if (error_entry_type == RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
			/* we've already got an error for this file, so just update it */
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, event->error->message);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
			g_value_unset (&value);
		} else {
			/* no need to update the ignored file entry */
		}

		if (event->vfsinfo) {
			/* mtime */
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, event->vfsinfo->mtime);
			rhythmdb_entry_set(db, entry, RHYTHMDB_PROP_MTIME, &value);
			g_value_unset (&value);
		}

		rhythmdb_add_timeout_commit (db, FALSE);
	} 

	if (entry == NULL) {
		/* create a new import error or ignore entry */
		entry = rhythmdb_entry_new (db, error_entry_type, event->real_uri);
		
		if (error_entry_type == RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, event->error->message);
			rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
			g_value_unset (&value);
		}
	
		/* mtime */
		if (event->vfsinfo) {
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, event->vfsinfo->mtime);
			rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_MTIME, &value);
			g_value_unset (&value);
		}

		/* record the mount point so we can delete entries for unmounted volumes */
		rhythmdb_entry_set_mount_point (db, entry, event->real_uri);

		rhythmdb_entry_set_visibility (db, entry, TRUE);

		rhythmdb_add_timeout_commit (db, FALSE);
	}
}

static gboolean
rhythmdb_process_metadata_load (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry;
	GValue value = {0,};
	const char *mime;
	GTimeVal time;

	if (rhythmdb_get_readonly (db)) {
		rb_debug ("database is read-only right now, re-queuing event");
		g_async_queue_push (db->priv->event_queue, event);
		return FALSE;
	}

	if (event->error) {
		rhythmdb_add_import_error_entry (db, event);
		return TRUE;
	}

	/* do we really need to do this? */
	mime = rb_metadata_get_mime (event->metadata);
	if (!mime) {
		rb_debug ("unsupported file");
		return TRUE;
	}

	g_get_current_time (&time);

	entry = rhythmdb_entry_lookup_by_location (db, event->real_uri);

	if (entry) {
		if ((event->entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) &&
		    (rhythmdb_entry_get_entry_type (entry) != event->entry_type)) {
			/* switching from IGNORE to SONG or vice versa, recreate the entry */
			rhythmdb_entry_delete (db, entry);
			rhythmdb_add_timeout_commit (db, FALSE);
			entry = NULL;
		}
	}
	
	if (!entry) {
		if (event->entry_type == RHYTHMDB_ENTRY_TYPE_INVALID)
			event->entry_type = RHYTHMDB_ENTRY_TYPE_SONG;
		entry = rhythmdb_entry_new (db, event->entry_type, event->real_uri);
		if (entry == NULL) {
			rb_debug ("entry already exists");
			return TRUE;
		}

		/* initialize the last played date to 0=never */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, 0);
		rhythmdb_entry_set_uninserted (db, entry,
					       RHYTHMDB_PROP_LAST_PLAYED, &value);
		g_value_unset (&value);

		/* initialize the rating */
		g_value_init (&value, G_TYPE_DOUBLE);
		g_value_set_double (&value, 0);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_RATING, &value);
		g_value_unset (&value);

	        /* first seen */
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, time.tv_sec);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_FIRST_SEEN, &value);
		g_value_unset (&value);
	}

	if ((event->entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) && (entry->type != event->entry_type))
		g_warning ("attempt to use same location in multiple entry types");

	/* mtime */
	if (event->vfsinfo) {
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, event->vfsinfo->mtime);
		rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_MTIME, &value);
		g_value_unset (&value);
	}

	if (event->entry_type != RHYTHMDB_ENTRY_TYPE_IGNORE &&
	    event->entry_type != RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
		set_props_from_metadata (db, entry, event->vfsinfo, event->metadata);
	}

	/* we've seen this entry */
	rhythmdb_entry_set_visibility (db, entry, TRUE);
	
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, time.tv_sec);
	rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_LAST_SEEN, &value);
	g_value_unset (&value);

	/* Remember the mount point of the volume the song is on */
	rhythmdb_entry_set_mount_point (db, entry, event->real_uri);

	/* monitor the file for changes */
	/* FIXME: watch for errors */
	if (eel_gconf_get_boolean (CONF_MONITOR_LIBRARY) && event->entry_type == RHYTHMDB_ENTRY_TYPE_SONG)
		rhythmdb_monitor_uri_path (db, entry->location, NULL);

	rhythmdb_add_timeout_commit (db, FALSE);
	
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
rhythmdb_process_file_created_or_modified (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBAction *action;

	action = g_new0 (RhythmDBAction, 1);
	action->type = RHYTHMDB_ACTION_LOAD;
	action->uri = g_strdup (event->uri);
	action->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;
	g_async_queue_push (db->priv->action_queue, action);
}

static void
rhythmdb_process_file_deleted (RhythmDB *db, RhythmDBEvent *event)
{
	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (db, event->uri);

	g_hash_table_remove (db->priv->changed_files, event->uri);

	if (entry) {
		rb_debug ("deleting entry for %s", event->uri);
		rhythmdb_entry_set_visibility (db, entry, FALSE);
		rhythmdb_commit (db);
	}
}

static gboolean
rhythmdb_process_events (RhythmDB *db, GTimeVal *timeout)
{
	RhythmDBEvent *event;
	guint count = 0;
	
	while ((event = g_async_queue_try_pop (db->priv->event_queue)) != NULL) {
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
			if (count >= g_async_queue_length (db->priv->event_queue)) {
				rb_debug ("Database is read-only, and we can't process any more events");
				/* give the running query some time to complete */
				return FALSE;
			}
			rb_debug ("Database is read-only, delaying event processing\n");
			g_async_queue_push (db->priv->event_queue, event);
			goto next_event;
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
			db->priv->save_timeout_id = g_timeout_add_full (G_PRIORITY_LOW,
									5 * 60 * 1000,
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
		
		count++;
	next_event:
		if (timeout && (count % 8 == 0)) {
			GTimeVal now;
			g_get_current_time (&now);
			if (rb_compare_gtimeval (timeout,&now) < 0) {
				/* probably more work to do, so try to come back as soon as possible */
				return TRUE;
			}
		}
	}

	/* queue is empty, so we can wait a while before checking it again */
	return FALSE;
}

static gboolean
rhythmdb_idle_poll_events (RhythmDB *db)
{
	gboolean poll_soon;
	GTimeVal timeout;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, G_USEC_PER_SEC*0.75);

	GDK_THREADS_ENTER ();

	poll_soon = rhythmdb_process_events (db, &timeout);

	if (poll_soon)
		db->priv->event_poll_id =
			g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc) rhythmdb_idle_poll_events,
					 db, NULL);
	else
		db->priv->event_poll_id =
			g_timeout_add (1000, (GSourceFunc) rhythmdb_idle_poll_events, db);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

#define READ_QUEUE_TIMEOUT G_USEC_PER_SEC / 10

static gpointer
read_queue (GAsyncQueue *queue, gboolean *cancel)
{
	GTimeVal timeout;
	gpointer ret;

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, READ_QUEUE_TIMEOUT);

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
rhythmdb_execute_stat_info_cb (GnomeVFSAsyncHandle *handle,
			       GList *results, /* GnomeVFSGetFileInfoResult* items */
			       RhythmDBEvent *event)
{
	/* this is in the main thread, so we can't do any long operation here */
	GnomeVFSGetFileInfoResult *info_result = results->data;
	
	if (info_result->result == GNOME_VFS_OK) {
		event->vfsinfo = gnome_vfs_file_info_dup (info_result->file_info);
	} else {
		char *unescaped = gnome_vfs_unescape_string_for_display (event->real_uri);
		event->error = g_error_new (RHYTHMDB_ERROR,
					    RHYTHMDB_ERROR_ACCESS_FAILED,
					    _("Couldn't access %s: %s"),
					    unescaped,
					    gnome_vfs_result_to_string (info_result->result));
		rb_debug ("got error on %s: %s", unescaped, event->error->message);
		g_free (unescaped);
		event->vfsinfo = NULL;
	}
	g_async_queue_push (event->db->priv->event_queue, event);
}

static void
rhythmdb_execute_stat (RhythmDB *db, const char *uri, RhythmDBEvent *event)
{
	GnomeVFSURI *vfs_uri = gnome_vfs_uri_new (uri);

	GList *uri_list = g_list_append (NULL, vfs_uri);
	event->real_uri = g_strdup (uri);

	gnome_vfs_async_get_file_info (&event->handle, uri_list,
			       GNOME_VFS_FILE_INFO_FOLLOW_LINKS,
			       GNOME_VFS_PRIORITY_MIN,
			       (GnomeVFSAsyncGetFileInfoCallback) rhythmdb_execute_stat_info_cb,
			       event);
	g_list_free (uri_list);
}


static void
queue_stat_uri (const char *uri, RhythmDB *db, RhythmDBEntryType type)
{
	RhythmDBEvent *result;

	rb_debug ("queueing stat for \"%s\"", uri);
	g_assert (uri && *uri);

	result = g_new0 (RhythmDBEvent, 1);
	result->db = db;
	result->type = RHYTHMDB_EVENT_STAT;
	result->entry_type = type;

	/*
	 * before the action thread is started, we queue up stat events,
	 * as we're still creating and running queries, as well as loading
	 * the database.  when we start the action thread, we'll kick off
	 * a gnome-vfs job to run all the stat events too.
	 *
	 * when the action thread is already running, we can start the
	 * async_get_file_info job directly.
	 */
	g_mutex_lock (db->priv->stat_mutex);

	if (db->priv->action_thread_running) {
		rhythmdb_execute_stat (db, uri, result);
	} else {
		GnomeVFSURI *vfs_uri;

		vfs_uri = gnome_vfs_uri_new (uri);
		
		/* construct a list of URIs and a hash table containing
		 * stat events to fill in and post on the event queue.
		 */
		if (g_hash_table_lookup (db->priv->stat_events, vfs_uri)) {
			g_free (result);
			gnome_vfs_uri_unref (vfs_uri);
		} else {
			result->real_uri = g_strdup (uri);
			g_hash_table_insert (db->priv->stat_events, vfs_uri, result);
			db->priv->stat_list = g_list_prepend (db->priv->stat_list, vfs_uri);
		}	
	}
	
	g_mutex_unlock (db->priv->stat_mutex);
}

static void
queue_stat_uri_tad (const char *uri, RhythmDBAddThreadData *data)
{
	queue_stat_uri (uri, data->db, data->type);
}


static gpointer
add_thread_main (RhythmDBAddThreadData *data)
{
	RhythmDBEvent *result;

	rb_uri_handle_recursively (data->uri, (GFunc) queue_stat_uri_tad,
				   &data->db->priv->exiting, data);

	rb_debug ("exiting");
	result = g_new0 (RhythmDBEvent, 1);
	result->db = data->db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	g_async_queue_push (data->db->priv->event_queue, result);
	g_free (data->uri);
	g_free (data);
	return NULL;
}

static void
rhythmdb_execute_load (RhythmDB *db, const char *uri, RhythmDBEvent *event)
{
	GnomeVFSURI *vfs_uri = gnome_vfs_uri_new (uri);
	GnomeVFSResult vfsresult;

	event->real_uri = rb_uri_resolve_symlink (uri);
	event->vfsinfo = gnome_vfs_file_info_new ();

	vfsresult = gnome_vfs_get_file_info (uri,
					     event->vfsinfo,
					     GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	if (vfsresult != GNOME_VFS_OK) {
		char *unescaped = gnome_vfs_unescape_string_for_display (uri);
		event->error = g_error_new (RHYTHMDB_ERROR,
					    RHYTHMDB_ERROR_ACCESS_FAILED,
					    _("Couldn't access %s: %s"),
					    unescaped,
					    gnome_vfs_result_to_string (vfsresult));
		rb_debug ("got error on %s: %s", unescaped, event->error->message);
		g_free (unescaped);
		if (event->vfsinfo)
			gnome_vfs_file_info_unref (event->vfsinfo);
		event->vfsinfo = NULL;
	} else {
		if (event->type == RHYTHMDB_EVENT_METADATA_LOAD) {
			event->metadata = rb_metadata_new ();
			rb_metadata_load (event->metadata, event->real_uri,
					  &event->error);
		}
	}

	gnome_vfs_uri_unref (vfs_uri);
	g_async_queue_push (db->priv->event_queue, event);
}


/**
 * rhythmdb_entry_get:
 * @entry: a #RhythmDBEntry.
 * @propid: the id of the property to get.
 * @val: return location for the property value.
 *
 * Gets a property of an entry, storing it in the given #GValue.
 **/
void
rhythmdb_entry_get (RhythmDB *db, RhythmDBEntry *entry, 
		    RhythmDBPropType propid, GValue *val)
{
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
entry_to_rb_metadata (RhythmDB *db, RhythmDBEntry *entry, RBMetaData *metadata)
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

	while (TRUE) {
		RhythmDBAction *action;

		action = read_queue (db->priv->action_queue, &db->priv->exiting);

		if (action == NULL)
			break;

		switch (action->type) {
		case RHYTHMDB_ACTION_STAT:
		{
			result = g_new0 (RhythmDBEvent, 1);
			result->db = db;
			result->type = RHYTHMDB_EVENT_STAT;
			result->entry_type = action->entry_type;

			rb_debug ("executing RHYTHMDB_ACTION_STAT for \"%s\"", action->uri);

			rhythmdb_execute_stat (db, action->uri, result);
		}
		break;
		case RHYTHMDB_ACTION_LOAD:
		{
			result = g_new0 (RhythmDBEvent, 1);
			result->db = db;
			result->type = RHYTHMDB_EVENT_METADATA_LOAD;
			result->entry_type = action->entry_type;

			rb_debug ("executing RHYTHMDB_ACTION_LOAD for \"%s\"", action->uri);

			rhythmdb_execute_load (db, action->uri, result);
		}
		break;
		case RHYTHMDB_ACTION_SYNC:
		{
			GError *error = NULL;
			RhythmDBSaveErrorData *data;
			RhythmDBEntry *entry;

			if (db->priv->dry_run) {
				rb_debug ("dry run is enabled, not syncing metadata");
				break;
			}

			rb_metadata_load (db->priv->metadata,
					  action->uri, &error);
			if (error != NULL) {
				data = g_new0 (RhythmDBSaveErrorData, 1);
				g_object_ref (G_OBJECT (db));
				data->db = db;
				data->uri = g_strdup (action->uri);
				data->error = error;
		
				g_idle_add ((GSourceFunc)emit_save_error_idle, data);
				break;
			}

			entry = rhythmdb_entry_lookup_by_location (db, action->uri);
			if (!entry)
				break;

			entry_to_rb_metadata (db, entry, db->priv->metadata);

			rb_metadata_save (db->priv->metadata, &error);
			if (error != NULL) {
				RhythmDBAction *load_action;

				/* reload the metadata, to revert the db changes */
				load_action = g_new0 (RhythmDBAction, 1);
				load_action->type = RHYTHMDB_ACTION_LOAD;
				load_action->uri = g_strdup (action->uri);
				load_action->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;
				g_async_queue_push (db->priv->action_queue, load_action);
				
				data = g_new0 (RhythmDBSaveErrorData, 1);
				g_object_ref (G_OBJECT (db));
				data->db = db;
				data->uri = g_strdup (action->uri);
				data->error = error;
				g_idle_add ((GSourceFunc)emit_save_error_idle, data);

				break;
			}
			break;
		}
		break;
		default:
			g_assert_not_reached ();
			break;
		}
		rhythmdb_action_free (db, action);

	}

	rb_debug ("exiting main thread");
	result = g_new0 (RhythmDBEvent, 1);
	result->db = db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	g_async_queue_push (db->priv->event_queue, result);

	return NULL;
}

/**
 * rhythmdb_add_uri:
 * @db: a #RhythmDB.
 * @uri: the URI to add an entry/entries for
 *
 * Adds the file(s) pointed to by @uri to the database, as entries of type
 * RHYTHMDB_ENTRY_TYPE_SONG. If the URI is that of a file, they will be added.
 * If the URI is that of a directory, everything under it will be added recursively.
 **/
void
rhythmdb_add_uri (RhythmDB *db, const char *uri)
{
	rhythmdb_add_uri_with_type (db, uri, RHYTHMDB_ENTRY_TYPE_INVALID);
}

void
rhythmdb_add_uri_with_type (RhythmDB *db, const char *uri, RhythmDBEntryType type)
{
	char *realuri;
	char *canon_uri;
	       
	canon_uri = rb_canonicalise_uri (uri);
	realuri = rb_uri_resolve_symlink (canon_uri);
	g_free (canon_uri);

	if (rb_uri_is_directory (realuri)) {
		RhythmDBAddThreadData *data = g_new0 (RhythmDBAddThreadData, 1);
		data->db = db;
		data->uri = realuri;
		data->type = type;

		rhythmdb_thread_create (db, (GThreadFunc) add_thread_main, data);
	} else {
		queue_stat_uri (realuri, db, type);
		g_free (realuri);
	}
}

static gboolean
rhythmdb_sync_library_idle (RhythmDB *db)
{
	rhythmdb_sync_library_location (db);
	return FALSE;
}

static gpointer 	 
rhythmdb_load_thread_main (RhythmDB *db) 	 
{ 	 
	RhythmDBEvent *result; 	 
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db); 	 

	g_mutex_lock (db->priv->saving_mutex);
	klass->impl_load (db, &db->priv->exiting); 	 
	g_mutex_unlock (db->priv->saving_mutex);

	g_timeout_add (10000, (GSourceFunc) rhythmdb_sync_library_idle, db);
	db->priv->changed_files_id = g_timeout_add (RHYTHMDB_FILE_MODIFY_PROCESS_TIME * 1000,
						    (GSourceFunc) rhythmdb_process_changed_files, db);

	rb_debug ("queuing db load complete signal"); 	 
	result = g_new0 (RhythmDBEvent, 1); 	 
	result->type = RHYTHMDB_EVENT_DB_LOAD; 	 
	g_async_queue_push (db->priv->event_queue, result); 	 
  	 
	rb_debug ("exiting"); 	 
	result = g_new0 (RhythmDBEvent, 1); 	 
	result->type = RHYTHMDB_EVENT_THREAD_EXITED; 	 
	g_async_queue_push (db->priv->event_queue, result); 	 

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
	rb_profile_start ("loading db");
	rhythmdb_thread_create (db, (GThreadFunc) rhythmdb_load_thread_main, db);

#if 0
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	RhythmDBEvent *result;
	
	/* grab the saving mutex, so that saves will block until the load is complete */
	g_mutex_lock (db->priv->saving_mutex);
	klass->impl_load (db, &db->priv->exiting);
	g_mutex_unlock (db->priv->saving_mutex);

	g_idle_add ((GSourceFunc) rhythmdb_sync_library_idle, db);
	db->priv->changed_files_id = g_timeout_add (RHYTHMDB_FILE_MODIFY_PROCESS_TIME * 1000,
						    (GSourceFunc) rhythmdb_process_changed_files, db);

	rb_debug ("queuing db load complete signal");
	result = g_new0 (RhythmDBEvent, 1);
	result->db = db;
	result->type = RHYTHMDB_EVENT_DB_LOAD;
	g_async_queue_push (db->priv->event_queue, result);
#endif
	rb_profile_end ("loading db");
}

static gpointer
rhythmdb_save_thread_main (RhythmDB *db)
{
	RhythmDBClass *klass;
	RhythmDBEvent *result;

	rb_debug ("entering save thread");
	
	g_mutex_lock (db->priv->saving_mutex);

	if (!db->priv->dirty) {
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
	result = g_new0 (RhythmDBEvent, 1);
	result->db = db;
	result->type = RHYTHMDB_EVENT_DB_SAVED;
	g_async_queue_push (db->priv->event_queue, result);

	result = g_new0 (RhythmDBEvent, 1);
	result->db = db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	g_async_queue_push (db->priv->event_queue, result);
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

	rhythmdb_thread_create (db, (GThreadFunc) rhythmdb_save_thread_main, db);
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
	rb_debug("saving the rhythmdb and blocking");
	
	rhythmdb_save_async (db);
	
	g_mutex_lock (db->priv->saving_mutex);

	while (db->priv->saving)
		g_cond_wait (db->priv->saving_condition, db->priv->saving_mutex);

	g_mutex_unlock (db->priv->saving_mutex);
}

static void
threadsafe_entry_set (RhythmDB *db, RhythmDBEntry *entry,
		      gboolean notify, guint propid, const GValue *value)
{
	if (!rhythmdb_get_readonly (db) && rb_is_main_thread ()) {
		rhythmdb_entry_set_internal (db, entry, notify, propid, value);
	} else {
		RhythmDBEvent *result;

		result = g_new0 (RhythmDBEvent, 1);
		result->db = db;
		result->type = RHYTHMDB_EVENT_ENTRY_SET;

		rb_debug ("queuing RHYTHMDB_ACTION_ENTRY_SET");

		result->entry = entry;
		result->change.prop = propid;
		result->signal_change = notify;
		g_value_init (&result->change.new, G_VALUE_TYPE (value));
		g_value_copy (value, &result->change.new);
		g_async_queue_push (db->priv->event_queue, result);
	}
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
rhythmdb_entry_set (RhythmDB *db, RhythmDBEntry *entry, 
		    guint propid, const GValue *value)
{
	g_return_if_fail (entry->inserted == TRUE);
	threadsafe_entry_set (db, entry, TRUE, propid, value);
}

/**
 * rhythmdb_entry_set_nonotify:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 * @propid: the id of the property to set.
 * @value: the property value.
 *
 * This function is like rhythmdb_entry_set(), except no notification
 * of the change will be sent.  This is useful if you know no
 * one could possibly be listening for the change.
 * 
 * Note that you do not need to call rhythmdb_commit() after this.
 **/
void 
rhythmdb_entry_set_nonotify (RhythmDB *db, RhythmDBEntry *entry, 
			     guint propid, const GValue *value)
{
	g_return_if_fail (!entry->inserted);
	threadsafe_entry_set (db, entry, FALSE, propid, value);
}

static void
record_entry_change (RhythmDB *db, RhythmDBEntry *entry,
		     guint propid, const GValue *value)
{
	RhythmDBEntryChange *changedata;
	GSList *changelist;

	changedata = g_new0 (RhythmDBEntryChange, 1);
	changedata->prop = propid;

	/* Copy a temporary gvalue, since _entry_get uses
	 * _set_static_string to avoid memory allocations. */
	{
		GValue tem = {0,};
		g_value_init (&tem, G_VALUE_TYPE (value));
		rhythmdb_entry_get (db, entry, propid, &tem);
		g_value_init (&changedata->old, G_VALUE_TYPE (value));
		g_value_copy (&tem, &changedata->old);
		g_value_unset (&tem);
	}
	g_value_init (&changedata->new, G_VALUE_TYPE (value));
	g_value_copy (value, &changedata->new);

	g_mutex_lock (db->priv->change_mutex);
	changelist = g_hash_table_lookup (db->priv->changed_entries, entry);
	changelist = g_slist_append (changelist, changedata);
	g_hash_table_insert (db->priv->changed_entries, entry, changelist);
	g_mutex_unlock (db->priv->change_mutex);
}

/**
 * rhythmdb_entry_set_uninserted:
 * @db: a #RhythmDB.
 * @entry: a #RhythmDBEntry.
 * @propid: the id of the property to set.
 * @value: the property value.
 *
 * This function is like rhythmdb_entry_set(), except that it should only
 * be called for entries that have been created with rhythmdb_entry_new()
 * but not yet committed to the database (i.e. before rhythmdb_commit()).
 *
 * Note that you need to call rhythmdb_commit() after all properties are set.
 **/
void
rhythmdb_entry_set_uninserted (RhythmDB *db, RhythmDBEntry *entry,
			       guint propid, const GValue *value)
{
	g_return_if_fail (entry->inserted == FALSE);

	rhythmdb_entry_set_internal (db, entry, FALSE, propid, value);
}

static void
rhythmdb_entry_set_internal (RhythmDB *db, RhythmDBEntry *entry,
			     gboolean notify_if_inserted,
			     guint propid, const GValue *value)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	gboolean handled;

#ifndef G_DISABLE_ASSERT	
	switch (G_VALUE_TYPE (value)) {
	case G_TYPE_STRING:
		/* the playback error is allowed to be NULL */
		if (propid != RHYTHMDB_PROP_PLAYBACK_ERROR || g_value_get_string (value))
			g_assert (g_utf8_validate (g_value_get_string (value), -1, NULL));
		break;
	case G_TYPE_BOOLEAN:
	case G_TYPE_ULONG:
	case G_TYPE_UINT64:
	case G_TYPE_DOUBLE:
		break;
	default:
		g_assert_not_reached ();
		break;
	}
#endif

	if (entry->inserted && notify_if_inserted) {
		record_entry_change (db, entry, propid, value);
	}

	handled = klass->impl_entry_set (db, entry, propid, value);

	if (!handled) {
		switch (propid) {
		case RHYTHMDB_PROP_TYPE:
			g_assert_not_reached ();
			break;
		case RHYTHMDB_PROP_TITLE:
			rb_refstring_unref (entry->title);
			entry->title = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ALBUM:
			rb_refstring_unref (entry->album);
			entry->album = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_ARTIST:
			rb_refstring_unref (entry->artist);
			entry->artist = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_GENRE:
			rb_refstring_unref (entry->genre);
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
			
			if (entry->date)
				g_date_free (entry->date);
			
			julian = g_value_get_ulong (value);
			entry->date = (julian > 0) ? g_date_new_julian (julian) : NULL;
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
			g_free (entry->location);
			entry->location = g_value_dup_string (value);
			break;
		case RHYTHMDB_PROP_PLAYBACK_ERROR:
			g_free (entry->playback_error);
			if (g_value_get_string (value))
				entry->playback_error = g_value_dup_string (value);
			else
				entry->playback_error = NULL;
			break;
		case RHYTHMDB_PROP_MOUNTPOINT:
			rb_refstring_unref (entry->mountpoint);
			entry->mountpoint = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_FILE_SIZE:
			entry->file_size = g_value_get_uint64 (value);
			break;
		case RHYTHMDB_PROP_MIMETYPE:
			if (entry->mimetype)
				rb_refstring_unref (entry->mimetype);
			entry->mimetype = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_MTIME:
			entry->mtime = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_FIRST_SEEN:
			entry->first_seen = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_LAST_SEEN:
			entry->last_seen = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_RATING:
			entry->rating = g_value_get_double (value);
			break;
		case RHYTHMDB_PROP_PLAY_COUNT:
			entry->play_count = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_LAST_PLAYED:
			entry->last_played = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_HIDDEN:
			entry->hidden = g_value_get_boolean (value);
			break;
		case RHYTHMDB_PROP_STATUS:
			entry->podcast->status = g_value_get_ulong (value);
			break;
		case RHYTHMDB_PROP_DESCRIPTION:
			rb_refstring_unref (entry->podcast->description);
			entry->podcast->description = rb_refstring_new (g_value_get_string (value));
			break;	
		case RHYTHMDB_PROP_SUBTITLE:
			rb_refstring_unref (entry->podcast->subtitle);
			entry->podcast->subtitle = rb_refstring_new (g_value_get_string (value));
			break;	
		case RHYTHMDB_PROP_SUMMARY:
			rb_refstring_unref (entry->podcast->summary);
			entry->podcast->summary = rb_refstring_new (g_value_get_string (value));
			break;	
		case RHYTHMDB_PROP_LANG:
			rb_refstring_unref (entry->podcast->lang);
			entry->podcast->lang = rb_refstring_new (g_value_get_string (value));
			break;	
		case RHYTHMDB_PROP_COPYRIGHT:
			rb_refstring_unref (entry->podcast->copyright);
			entry->podcast->copyright = rb_refstring_new (g_value_get_string (value));
			break;	
		case RHYTHMDB_PROP_IMAGE:
			rb_refstring_unref (entry->podcast->image);
			entry->podcast->image = rb_refstring_new (g_value_get_string (value));
			break;
		case RHYTHMDB_PROP_POST_TIME:
			entry->podcast->post_time = g_value_get_ulong (value);
			break;	
		case RHYTHMDB_NUM_PROPERTIES:
			g_assert_not_reached ();
			break;
		}
	}
	rhythmdb_entry_sync_mirrored (db, entry, propid);
	
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


void
rhythmdb_entry_sync_mirrored (RhythmDB *db, RhythmDBEntry *entry, guint propid)
{
	static const char *format;
	static const char *never;
	char *val;

	/* PERFORMANCE: doing these lookups every call creates a noticable slowdown */
	if (format == NULL)
		format = _("%Y-%m-%d %H:%M");
	if (never == NULL)
		never = _("Never");
	
	switch (propid) {
	case RHYTHMDB_PROP_LAST_PLAYED:
	{
		rb_refstring_unref (entry->last_played_str);
		if (entry->last_played == 0) {
			entry->last_played_str = rb_refstring_new (never);
		} else {
			val = eel_strdup_strftime (format, localtime ((glong*)&entry->last_played));
			entry->last_played_str = rb_refstring_new (val);
			g_free (val);
		}
		break;
	}
	case RHYTHMDB_PROP_FIRST_SEEN:
	{
		rb_refstring_unref (entry->first_seen_str);
		val = eel_strdup_strftime (format, localtime ((glong*)&entry->first_seen));
		entry->first_seen_str = rb_refstring_new (val);
		g_free (val);
		break;
	}
	case RHYTHMDB_PROP_LAST_SEEN:
	case RHYTHMDB_PROP_HIDDEN:
	{
		/* only store last seen time as a string for hidden entries */
		rb_refstring_unref (entry->last_seen_str);
		if (entry->hidden) {
			val = eel_strdup_strftime (format, localtime ((glong*)&entry->last_seen));
			entry->last_seen_str = rb_refstring_new (val);
			g_free (val);
		} else {
			entry->last_seen_str = NULL;
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
 * Delete entry @entry from the database, sending notification of it's deletion.
 * This is usually used by sources where entries can disappear randomly, such
 * as a network source.
 **/
void
rhythmdb_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	klass->impl_entry_delete (db, entry);

	rhythmdb_entry_ref (db, entry);
	g_mutex_lock (db->priv->change_mutex);
	g_hash_table_insert (db->priv->deleted_entries, entry, g_thread_self ());
	g_mutex_unlock (db->priv->change_mutex);

	/* deleting an entry makes the db dirty */
	db->priv->dirty = TRUE;
}

static gint
rhythmdb_entry_move_to_trash_cb (GnomeVFSXferProgressInfo *info, gpointer data)
{
	/* Abort immediately if anything happens */
	if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR)
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	/* Don't overwrite files */
	if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE)
		return 0;
	return TRUE;
}

static void
rhythmdb_entry_move_to_trash_set_error (RhythmDB *db, RhythmDBEntry *entry,
				        GnomeVFSResult res)
{
	GValue value = { 0, };

	if (res == -1)
		res = GNOME_VFS_ERROR_INTERNAL;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, gnome_vfs_result_to_string (res));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &value);
	g_value_unset (&value);

	rb_debug ("Deleting %s failed: %s", entry->location,
			gnome_vfs_result_to_string (res));
}

void
rhythmdb_entry_move_to_trash (RhythmDB *db, RhythmDBEntry *entry)
{
	GnomeVFSResult res;
	GnomeVFSURI *uri, *trash, *dest;
	char *shortname;

	uri = gnome_vfs_uri_new (entry->location);
	if (uri == NULL) {
		rhythmdb_entry_move_to_trash_set_error (db, entry, -1);
		return;
	}

	res = gnome_vfs_find_directory (uri,
			GNOME_VFS_DIRECTORY_KIND_TRASH,
			&trash,
			TRUE, TRUE,
			0);
	if (res != GNOME_VFS_OK || trash == NULL) {
		/* If the file doesn't exist, or trash isn't support,
		 * remove it from the db */
		if (res == GNOME_VFS_ERROR_NOT_FOUND ||
		    res == GNOME_VFS_ERROR_NOT_SUPPORTED) {
			rhythmdb_entry_set_visibility (db, entry, FALSE);
		} else {
			rhythmdb_entry_move_to_trash_set_error (db, entry, -1);
		}

		gnome_vfs_uri_unref (uri);
		return;
	}

	/* Is the file already in the Trash? If so it should be hidden */
	if (gnome_vfs_uri_is_parent (trash, uri, TRUE)) {
		GValue value = { 0, };
		g_value_init (&value, G_TYPE_BOOLEAN);
		g_value_set_boolean (&value, TRUE);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_HIDDEN, &value);
		rhythmdb_commit (db);

		gnome_vfs_uri_unref (trash);
		gnome_vfs_uri_unref (uri);
		return;
	}

	shortname = gnome_vfs_uri_extract_short_name (uri);
	if (shortname == NULL) {
		rhythmdb_entry_move_to_trash_set_error (db, entry, -1);
		rhythmdb_commit (db);
		gnome_vfs_uri_unref (uri);
		gnome_vfs_uri_unref (trash);
		return;
	}

	/* Compute the destination URI */
	dest = gnome_vfs_uri_append_path (trash, shortname);
	gnome_vfs_uri_unref (trash);
	g_free (shortname);
	if (dest == NULL) {
		rhythmdb_entry_move_to_trash_set_error (db, entry, -1);
		rhythmdb_commit (db);
		gnome_vfs_uri_unref (uri);
		return;
	}

	/* RB can't tell that a file's moved, so no unique names */
	res = gnome_vfs_xfer_uri (uri, dest,
			GNOME_VFS_XFER_REMOVESOURCE,
			GNOME_VFS_XFER_ERROR_MODE_ABORT,
			GNOME_VFS_XFER_OVERWRITE_MODE_SKIP,
			rhythmdb_entry_move_to_trash_cb,
			entry);

	if (res == GNOME_VFS_OK) {
		rhythmdb_entry_set_visibility (db, entry, FALSE);
	} else {
		rhythmdb_entry_move_to_trash_set_error (db, entry, res);
	}
	rhythmdb_commit (db);

	gnome_vfs_uri_unref (dest);
	gnome_vfs_uri_unref (uri);
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
rhythmdb_entry_delete_by_type (RhythmDB *db, RhythmDBEntryType type)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);
	
	if (klass->impl_entry_delete_by_type) {
		klass->impl_entry_delete_by_type (db, type);
	} else {
		g_warning ("delete_by_type not implemented");
	}
}

/**
 * rhythmdb_query_copy:
 * @array: the query to copy.
 *
 * Creates a copy of a query.
 *
 * Return value: a copy of the passed query. It must be freed with rhythmdb_query_free()
 **/
GPtrArray *
rhythmdb_query_copy (GPtrArray *array)
{
	GPtrArray *ret;

	if (!array)
		return NULL;
	
	ret = g_ptr_array_sized_new (array->len);
	rhythmdb_query_concatenate (ret, array);

	return ret;
}

void
rhythmdb_query_concatenate (GPtrArray *query1, GPtrArray *query2)
{
	guint i;

	g_assert (query2);
	if (!query2)
		return;
	
	for (i = 0; i < query2->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query2, i);
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
		g_ptr_array_add (query1, new_data);
	}
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
		switch (query) {
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			data->subquery = va_arg (args, GPtrArray *);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_PREFIX:
		case RHYTHMDB_QUERY_PROP_SUFFIX:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:  
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

/**
 * rhythmdb_query_parse:
 * @db: a #RhythmDB instance
 *
 * Creates a query from a list of criteria.
 *
 * Most criteria consists of an operator (#RhythmDBQueryType),
 * a property (#RhythmDBPropType) and the data to compare with. An entry
 * matches a criteria if the operator returns true with the value of the
 * entries property as the first argument, and the given data as the second
 * argument.
 *
 * Three types criteria are special. Passing RHYTHMDB_QUERY_END indicates the
 * end of the list of criteria, and must be the last passes parameter.
 *
 * The second special criteria is a subquery which is defined by passing
 * RHYTHMDB_QUERY_SUBQUERY, followed by a query (#GPtrArray). An entry will
 * match a subquery criteria if it matches all criteria in the subquery.
 *
 * The third special criteria is a disjunction which is defined by passing
 * RHYTHMDB_QUERY_DISJUNCTION, which will make an entry match the query if
 * it matches the criteria before the disjunction, the criteria after the
 * disjunction, or both.
 *
 * Example:
 * 	rhythmdb_query_parse (db,
 * 		RHYTHMDB_QUERY_SUBQUERY, subquery,
 * 		RHYTHMDB_QUERY_DISJUNCTION
 * 		RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "cat",
 *		RHYTHMDB_QUERY_DISJUNCTION
 *		RHYTHMDB_QUERY_PROP_GREATER, RHYTHMDB_PROP_RATING, 2.5,
 *		RHYTHMDB_QUERY_PROP_LESS, RHYTHMDB_PROP_PLAY_COUNT, 10,
 * 		RHYTHMDB_QUERY_END);
 *
 * 	will create a query that matches entries:
 * 	a) that match the query "subquery", or
 * 	b) that have "cat" in their title, or
 * 	c) have a rating of at least 2.5, and a play count of at most 10
 * 
 * Returns: a the newly created query. It must be freed with rhythmdb_query_free()
 **/
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


/**
 * rhythmdb_query_append:
 * @db: a #RhythmDB instance
 * @query: a query.
 *
 * Appends new criteria to the query @query.
 *
 * The list of criteria must be in the same format as for rhythmdb_query_parse,
 * and ended by RHYTHMDB_QUERY_END.
 **/
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

/**
 * rhythmdb_query_free:
 * @query: a query.
 *
 * Frees the query @query
 **/
void
rhythmdb_query_free (GPtrArray *query)
{
	guint i;

	if (query == NULL)
		return;
	
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (data->type) {
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			rhythmdb_query_free (data->subquery);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_PREFIX:
		case RHYTHMDB_QUERY_PROP_SUFFIX:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:  
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

const xmlChar *
rhythmdb_nice_elt_name_from_propid (RhythmDB *db, RhythmDBPropType propid)
{
	return db->priv->column_xml_names[propid];
}

int
rhythmdb_propid_from_nice_elt_name (RhythmDB *db, const xmlChar *name)
{
	gpointer ret, orig;	
	if (g_hash_table_lookup_extended (db->priv->propname_map, name,
					  &orig, &ret)) {
		return GPOINTER_TO_INT (ret);
	}
	return -1;
}

static char *
entry_type_to_string (RhythmDBEntryType type)
{
	if (type == RHYTHMDB_ENTRY_TYPE_SONG) {
		return g_strdup ("0");
	} else if (type == RHYTHMDB_ENTRY_TYPE_IRADIO_STATION) {
		return g_strdup ("1");
	} else if (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		return g_strdup ("2");
	} else if (type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
		return g_strdup ("3");
	}
	g_assert_not_reached ();
}

static RhythmDBEntryType
entry_type_from_uint (unsigned int type) 
{
	switch (type) {
	case 0:
		return RHYTHMDB_ENTRY_TYPE_SONG;
	case 1:
		return RHYTHMDB_ENTRY_TYPE_IRADIO_STATION;
	case 2:
		return RHYTHMDB_ENTRY_TYPE_PODCAST_POST;
	case 3:
		return RHYTHMDB_ENTRY_TYPE_PODCAST_FEED;
	default:
		g_assert_not_reached ();
	}

	return RHYTHMDB_ENTRY_TYPE_INVALID;
}

static void
write_encoded_gvalue (xmlNodePtr node,
		      GValue *val)
{
	char *strval;
	xmlChar *quoted;

	switch (G_VALUE_TYPE (val)) {
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
	case G_TYPE_ULONG:
		strval = g_strdup_printf ("%lu", g_value_get_ulong (val));
		break;
	case G_TYPE_UINT64:
		strval = g_strdup_printf ("%" G_GUINT64_FORMAT, g_value_get_uint64 (val));
		break;
	case G_TYPE_FLOAT:
		strval = g_strdup_printf ("%f", g_value_get_float (val));
		break;
	case G_TYPE_DOUBLE:
		strval = g_strdup_printf ("%f", g_value_get_double (val));
		break;
	case G_TYPE_POINTER:
		strval = entry_type_to_string (g_value_get_pointer (val));
		break;
	default:
		g_assert_not_reached ();
		strval = NULL;
		break;
	}

	quoted = xmlEncodeEntitiesReentrant (NULL, BAD_CAST strval);
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

	g_value_init (val, rhythmdb_get_property_type (db, propid));

	content = (char *)xmlNodeGetContent (node);
	
	switch (G_VALUE_TYPE (val)) {
	case G_TYPE_STRING:
		g_value_set_string (val, content);
		break;
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_ULONG:
		g_value_set_ulong (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_UINT64:
		g_value_set_uint64 (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (val, g_ascii_strtod (content, NULL));
		break;
	case G_TYPE_POINTER:
		if (propid == RHYTHMDB_PROP_TYPE) {
			RhythmDBEntryType entry_type;
			entry_type = entry_type_from_uint (g_ascii_strtoull (content, NULL, 10));
			if (entry_type != RHYTHMDB_ENTRY_TYPE_INVALID) {
				g_value_set_pointer (val, entry_type);
				break;
			} else {
				g_warning ("Unexpected entry type");
				/* Fall through */
			}
		}
		/* Falling through on purpose to get an assert for unexpected 
		 * cases 
		 */
	default:
		g_warning ("Attempt to read '%s' of unhandled type %s", 
			   rhythmdb_nice_elt_name_from_propid (db, propid),
			   g_type_name (G_VALUE_TYPE (val)));
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
	xmlNodePtr node = xmlNewChild (parent, NULL, RB_PARSE_CONJ, NULL);
	xmlNodePtr subnode;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		
		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
			subnode = xmlNewChild (node, NULL, RB_PARSE_SUBQUERY, NULL);
			rhythmdb_query_serialize (db, data->subquery, subnode);
			break;
		case RHYTHMDB_QUERY_PROP_LIKE:
			subnode = xmlNewChild (node, NULL, RB_PARSE_LIKE, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
			subnode = xmlNewChild (node, NULL, RB_PARSE_NOT_LIKE, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_PREFIX:
			subnode = xmlNewChild (node, NULL, RB_PARSE_PREFIX, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_SUFFIX:
			subnode = xmlNewChild (node, NULL, RB_PARSE_SUFFIX, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_EQUALS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_EQUALS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_DISJUNCTION:
			subnode = xmlNewChild (node, NULL, RB_PARSE_DISJ, NULL);
			break;
		case RHYTHMDB_QUERY_END:
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			subnode = xmlNewChild (node, NULL, RB_PARSE_GREATER, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_GREATER, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_LESS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:  
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_LESS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
			subnode = xmlNewChild (node, NULL, RB_PARSE_CURRENT_TIME_WITHIN, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
			subnode = xmlNewChild (node, NULL, RB_PARSE_CURRENT_TIME_NOT_WITHIN, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
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

	g_assert (!xmlStrcmp (parent->name, RB_PARSE_CONJ));
	
	for (child = parent->children; child; child = child->next) {
		RhythmDBQueryData *data;

		if (xmlNodeIsText (child))
			continue;

		data = g_new0 (RhythmDBQueryData, 1);

		if (!xmlStrcmp (child->name, RB_PARSE_SUBQUERY)) {
			xmlNodePtr subquery;
			data->type = RHYTHMDB_QUERY_SUBQUERY;
			subquery = child->children;
			while (xmlNodeIsText (subquery))
				subquery = subquery->next;
			
			data->subquery = rhythmdb_query_deserialize (db, subquery);
		} else if (!xmlStrcmp (child->name, RB_PARSE_DISJ)) {
			data->type = RHYTHMDB_QUERY_DISJUNCTION;
		} else if (!xmlStrcmp (child->name, RB_PARSE_LIKE)) {
			data->type = RHYTHMDB_QUERY_PROP_LIKE;
		} else if (!xmlStrcmp (child->name, RB_PARSE_NOT_LIKE)) {
			data->type = RHYTHMDB_QUERY_PROP_NOT_LIKE;
		} else if (!xmlStrcmp (child->name, RB_PARSE_PREFIX)) {
			data->type = RHYTHMDB_QUERY_PROP_PREFIX;
		} else if (!xmlStrcmp (child->name, RB_PARSE_SUFFIX)) {
			data->type = RHYTHMDB_QUERY_PROP_SUFFIX;
		} else if (!xmlStrcmp (child->name, RB_PARSE_EQUALS)) {
			if (!xmlStrcmp(xmlGetProp(child, RB_PARSE_PROP), 
				       (xmlChar *)"date")) 
				data->type = RHYTHMDB_QUERY_PROP_YEAR_EQUALS;
			else 
				data->type = RHYTHMDB_QUERY_PROP_EQUALS;
		} else if (!xmlStrcmp (child->name, RB_PARSE_GREATER)) {
			if (!xmlStrcmp(xmlGetProp(child, RB_PARSE_PROP), 
				       (xmlChar *)"date")) 
				data->type = RHYTHMDB_QUERY_PROP_YEAR_GREATER;
			else 
				data->type = RHYTHMDB_QUERY_PROP_GREATER;
		} else if (!xmlStrcmp (child->name, RB_PARSE_LESS)) {
			if (!xmlStrcmp(xmlGetProp(child, RB_PARSE_PROP), 
				       (xmlChar *)"date")) 
				data->type = RHYTHMDB_QUERY_PROP_YEAR_LESS;
			else 
				data->type = RHYTHMDB_QUERY_PROP_LESS;
		} else if (!xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_WITHIN)) {
			data->type = RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN;
		} else if (!xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_NOT_WITHIN)) {
			data->type = RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN;
		} else
 			g_assert_not_reached ();

		if (!xmlStrcmp (child->name, RB_PARSE_LIKE)
		    || !xmlStrcmp (child->name, RB_PARSE_NOT_LIKE)
		    || !xmlStrcmp (child->name, RB_PARSE_PREFIX)
		    || !xmlStrcmp (child->name, RB_PARSE_SUFFIX)
		    || !xmlStrcmp (child->name, RB_PARSE_EQUALS)
		    || !xmlStrcmp (child->name, RB_PARSE_GREATER)
		    || !xmlStrcmp (child->name, RB_PARSE_LESS)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_EQUALS)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_GREATER)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_LESS)
		    || !xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_WITHIN)
		    || !xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_NOT_WITHIN)) {
			xmlChar *propstr = xmlGetProp (child, RB_PARSE_PROP);
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
rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	return klass->impl_lookup_by_location (db, uri);
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
rhythmdb_entry_foreach (RhythmDB *db, GFunc func, gpointer data)
{
	RhythmDBClass *klass = RHYTHMDB_GET_CLASS (db);

	klass->impl_entry_foreach (db, func, data);
}

/**
 * rhythmdb_evaluate_query:
 * @db: a #RhythmDB.
 * @query: a query.
 * @entry a @RhythmDBEntry.
 *
 * Evaluates the given entry against the given query.
 *
 * Returns: whether the given entry matches the criteria of the given query.
 **/
gboolean
rhythmdb_evaluate_query (RhythmDB *db, GPtrArray *query, RhythmDBEntry *entry)
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

	result = g_new0 (RhythmDBEvent, 1);
	result->db = data->db;
	result->type = RHYTHMDB_EVENT_QUERY_COMPLETE;
	result->results = data->results;
	g_async_queue_push (data->db->priv->event_queue, result);

	rhythmdb_query_free (data->query);
}

static gpointer
query_thread_main (RhythmDBQueryThreadData *data)
{
	RhythmDBEvent *result;

	rb_debug ("entering query thread");

	rhythmdb_query_internal (data);

	result = g_new0 (RhythmDBEvent, 1);
	result->db = data->db;
	result->type = RHYTHMDB_EVENT_THREAD_EXITED;
	g_async_queue_push (data->db->priv->event_queue, result);
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

	g_object_ref (G_OBJECT (results));
	rhythmdb_thread_create (db, (GThreadFunc) query_thread_main, data);
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
	g_object_ref (G_OBJECT (results));

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
			ENUM_ENTRY (RHYTHMDB_PROP_TITLE, "Title (gchararray) [title]"),
			ENUM_ENTRY (RHYTHMDB_PROP_GENRE, "Genre (gchararray) [genre]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ARTIST, "Artist (gchararray) [artist]"),
			ENUM_ENTRY (RHYTHMDB_PROP_ALBUM, "Album (gchararray) [album]"),
			ENUM_ENTRY (RHYTHMDB_PROP_TRACK_NUMBER, "Track Number (gulong) [track-number]"),
			ENUM_ENTRY (RHYTHMDB_PROP_DISC_NUMBER, "Disc Number (gulong) [disc-number]"),

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
			{ 0, 0, 0 }
		};
		g_assert ((sizeof (values) / sizeof (values[0]) - 1) == RHYTHMDB_NUM_PROPERTIES);
		etype = g_enum_register_static ("RhythmDBPropType", values);
	}

	return etype;
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

/**
 * rhythmdb_is_busy:
 * @db: a #RhythmDB.
 *
 * Returns: whether the #RhythmDB has events to process.
 **/
gboolean
rhythmdb_is_busy (RhythmDB *db)
{
	return (!queue_is_empty (db->priv->event_queue));
}

/**
 * rhythmdb_compute_status_normal:
 * @n_songs: the number of tracks.
 * @duration: the total duration of the tracks.
 * @size: the total size of the tracks.
 *
 * Creates a string containing the "status" information about a list of tracks.
 * 
 * Returns: the string, which should be freed with g_free.
 **/
char *
rhythmdb_compute_status_normal (gint n_songs, glong duration, guint64 size)
{
	long days, hours, minutes, seconds;
	char *songcount = NULL;
	char *time = NULL;
	char *size_str = NULL;
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

	size_str = gnome_vfs_format_file_size_for_display (size);

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
 * rhythmdb_entry_register_type:
 *
 * Registers a new #RhythmDBEntryType. This should be called to create a new
 * entry type for non-permanent sources.
 *
 * Returns: the new #RhythmDBEntryType.
 **/
RhythmDBEntryType
rhythmdb_entry_register_type (void)
{
	return g_new0(RhythmDBEntryType_, 1);
}

static GStaticMutex entry_type_mutex = G_STATIC_MUTEX_INIT;

RhythmDBEntryType 
rhythmdb_entry_song_get_type (void) 
{
	static RhythmDBEntryType song_type = RHYTHMDB_ENTRY_TYPE_INVALID;

	g_static_mutex_lock (&entry_type_mutex);
	if (song_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		song_type = rhythmdb_entry_register_type ();
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return song_type;
}

RhythmDBEntryType 
rhythmdb_entry_ignore_get_type (void) 
{
	static RhythmDBEntryType ignore_type = RHYTHMDB_ENTRY_TYPE_INVALID;

	g_static_mutex_lock (&entry_type_mutex);
	if (ignore_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		ignore_type = rhythmdb_entry_register_type ();
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return ignore_type;
}

RhythmDBEntryType rhythmdb_entry_iradio_get_type (void) 
{
	static RhythmDBEntryType iradio_type = RHYTHMDB_ENTRY_TYPE_INVALID;
       
	g_static_mutex_lock (&entry_type_mutex);
	if (iradio_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		iradio_type = rhythmdb_entry_register_type ();
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return iradio_type;
}

static char *
_get_podcast_playback_uri (RhythmDBEntry *entry, gpointer data)
{
	return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
}

RhythmDBEntryType rhythmdb_entry_podcast_post_get_type (void) 
{
	static RhythmDBEntryType podcast_post_type = RHYTHMDB_ENTRY_TYPE_INVALID;
       
	g_static_mutex_lock (&entry_type_mutex);
	if (podcast_post_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		podcast_post_type = rhythmdb_entry_register_type ();

		podcast_post_type->get_playback_uri = _get_podcast_playback_uri;
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return podcast_post_type;
}

RhythmDBEntryType rhythmdb_entry_podcast_feed_get_type (void) 
{
	static RhythmDBEntryType podcast_feed_type = RHYTHMDB_ENTRY_TYPE_INVALID;
       
	g_static_mutex_lock (&entry_type_mutex);
	if (podcast_feed_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		podcast_feed_type = rhythmdb_entry_register_type ();
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return podcast_feed_type;
}

static char *
_get_import_error_playback_uri (RhythmDBEntry *entry, gpointer data)
{
	return NULL;
}

RhythmDBEntryType rhythmdb_entry_import_error_get_type (void) 
{
	static RhythmDBEntryType import_error_type = RHYTHMDB_ENTRY_TYPE_INVALID;
       
	g_static_mutex_lock (&entry_type_mutex);
	if (import_error_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		import_error_type = rhythmdb_entry_register_type ();
		
		import_error_type->get_playback_uri = _get_import_error_playback_uri;
	}
	g_static_mutex_unlock (&entry_type_mutex);

	return import_error_type;
}


typedef struct
{
	RhythmDB *db;
	char *mount_point;
	gboolean mounted;
} MountCtxt;

static void 
entry_volume_mounted_or_unmounted (RhythmDBEntry *entry, 
				   MountCtxt *ctxt)
{
	const char *mount_point;
	const char *location;
	
	if (entry->type != RHYTHMDB_ENTRY_TYPE_SONG &&
	    entry->type != RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
		return;
	}
	
	mount_point = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (mount_point == NULL || strcmp (mount_point, ctxt->mount_point) != 0) {
		return;
	}
	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		if (ctxt->mounted) {
			rb_debug ("queueing stat for entry %s (mounted)", location);
			queue_stat_uri (location, 
					ctxt->db,
					RHYTHMDB_ENTRY_TYPE_SONG);
		} else {
			GTimeVal time;
			GValue val = {0, };

			rb_debug ("hiding entry %s (unmounted)", location);
			
			g_get_current_time (&time);
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, time.tv_sec);
			rhythmdb_entry_set_internal (ctxt->db, entry, FALSE,
						     RHYTHMDB_PROP_LAST_SEEN, &val);
			g_value_unset (&val);

			rhythmdb_entry_set_visibility (ctxt->db, entry, FALSE);
		}
	} else if (entry->type == RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
		/* delete import errors for files on unmounted volumes */
		if (ctxt->mounted == FALSE) {
			rb_debug ("removing import error for %s (unmounted)", location);
			rhythmdb_entry_delete (ctxt->db, entry);
		}
	}
}


static void 
rhythmdb_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
			    GnomeVFSVolume *volume, 
			    gpointer data)
{
	MountCtxt ctxt;

	ctxt.db = RHYTHMDB (data);
	ctxt.mount_point = gnome_vfs_volume_get_activation_uri (volume);
	ctxt.mounted = TRUE;
	rhythmdb_entry_foreach (RHYTHMDB (data), 
				(GFunc)entry_volume_mounted_or_unmounted, 
				&ctxt);
	rhythmdb_commit (RHYTHMDB (data));
	g_free (ctxt.mount_point);
}


static void 
rhythmdb_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
			      GnomeVFSVolume *volume, 
			      gpointer data)
{
	MountCtxt ctxt;

	ctxt.db = RHYTHMDB (data);
	ctxt.mount_point = gnome_vfs_volume_get_activation_uri (volume);
	ctxt.mounted = FALSE;
	rb_debug ("volume %s unmounted", ctxt.mount_point);
	rhythmdb_entry_foreach (RHYTHMDB (data), 
				(GFunc)entry_volume_mounted_or_unmounted, 
				&ctxt);
	rhythmdb_commit (RHYTHMDB (data));
	g_free (ctxt.mount_point);
}


static void
rhythmdb_entry_set_mount_point (RhythmDB *db, RhythmDBEntry *entry, 
				const gchar *realuri)
{
	gchar *mount_point;
	GValue value = {0, };

	mount_point = rb_uri_get_mount_point (realuri);
	if (mount_point != NULL) {
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string_take_ownership (&value, mount_point);
		rhythmdb_entry_set_internal (db, entry, FALSE,
					     RHYTHMDB_PROP_MOUNTPOINT, 
					     &value);
		g_value_unset (&value);
	}
}

static void
rhythmdb_entry_set_visibility (RhythmDB *db, RhythmDBEntry *entry, 
			       gboolean visible)
{
	GValue old_val = {0, };
	gboolean old_visible;

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

		rhythmdb_entry_sync_mirrored (db, entry, RHYTHMDB_PROP_LAST_SEEN);
	}
	g_value_unset (&old_val);
}

/**
 * This is used to "process" queries, before using them. It is mainly used to two things:
 *
 * 1) performing expensive data transformations once per query, rather than
 *    once per entry we try to match against. e.g. RHYTHMDB_PROP_SEARCH_MATCH
 *
 * 2) defining criteria in terms of other lower-level ones that the db backend
 *    actually implements. e.g. RHYTHMDB_QUERY_YEAR_*
 **/

void
rhythmdb_query_preprocess (RhythmDB *db, GPtrArray *query)
{
	int i;	

	if (query == NULL)
		return;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		gboolean restart_criteria = FALSE;
		
		if (data->subquery) {
			rhythmdb_query_preprocess (db, data->subquery);
		} else switch (data->propid) {
			case RHYTHMDB_PROP_TITLE_FOLDED:
			case RHYTHMDB_PROP_GENRE_FOLDED:
			case RHYTHMDB_PROP_ARTIST_FOLDED:
			case RHYTHMDB_PROP_ALBUM_FOLDED:
			{
				/* as we are matching against a folded property, the string needs to also be folded */
				const char *orig = g_value_get_string (data->val);
				char *folded = rb_search_fold (orig);

				g_value_reset (data->val);
				g_value_take_string (data->val, folded);
				break;
			}

			case RHYTHMDB_PROP_SEARCH_MATCH:
			{
				const char *orig = g_value_get_string (data->val);
				char *folded = rb_search_fold (orig);
				char **words = rb_string_split_words (folded);

				g_free (folded);
				g_value_unset (data->val);
				g_value_init (data->val, G_TYPE_STRV);
				g_value_take_boxed (data->val, words);
				break;
			}

			case RHYTHMDB_PROP_DATE:
			{
				GDate *date;
				gulong search_date;		
				gulong begin;			
				gulong end;
				gulong year;

				search_date = g_value_get_ulong (data->val);
				date = g_date_new_julian (search_date);		
				year = g_date_get_year (date);	
				g_date_free (date);				

				/* get Julian dates for beginning and end of year */ 
				date = g_date_new_dmy (1, G_DATE_JANUARY, year); 
				begin = g_date_get_julian (date);	
				g_date_free (date);				

				/* end the dat before the beginning of the next year */
				date = g_date_new_dmy (1, G_DATE_JANUARY, year + 1); 
				end =  g_date_get_julian (date) - 1;
				g_date_free (date);				
				
				switch (data->type)
				{
				case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_SUBQUERY;
					data->subquery = rhythmdb_query_parse (db,
									       RHYTHMDB_QUERY_PROP_GREATER, data->propid, begin,
									       RHYTHMDB_QUERY_PROP_LESS, data->propid, end,
									       RHYTHMDB_QUERY_END);
					break;

				case RHYTHMDB_QUERY_PROP_YEAR_LESS:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_PROP_LESS;
					g_value_set_ulong (data->val, end);
					break;

				case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_PROP_GREATER;
					g_value_set_ulong (data->val, begin);
					break;

				default:
					break;
				}
				
				break;
			}
			
			default:
				break;
		}

		/* re-do this criteria, in case it needs furthur transformation */
		if (restart_criteria)
			i--;
	}
}

void
rhythmdb_query_append_prop_multiple (RhythmDB *db, GPtrArray *query, RhythmDBPropType propid, GList *items)
{
	GPtrArray *subquery;

	if (items == NULL)
		return;

	if (items->next == NULL) {
		rhythmdb_query_append (db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       propid,
				       items->data,
				       RHYTHMDB_QUERY_END);
		return;
	}

	subquery = g_ptr_array_new ();

	rhythmdb_query_append (db,
			       subquery,
			       RHYTHMDB_QUERY_PROP_EQUALS,
			       propid,
			       items->data,
			       RHYTHMDB_QUERY_END);
	items = items->next;
	while (items) {
		rhythmdb_query_append (db,
				       subquery,
				       RHYTHMDB_QUERY_DISJUNCTION,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       propid,
				       items->data,
				       RHYTHMDB_QUERY_END);
		items = items->next;
	}
	rhythmdb_query_append (db, query, RHYTHMDB_QUERY_SUBQUERY, subquery,
			       RHYTHMDB_QUERY_END);
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
monitor_subdirectory (const char *uri, RhythmDB *db)
{
	GError *error = NULL;

	if (!rb_uri_is_directory (uri))
		return;

	rhythmdb_monitor_uri_path (db, uri, &error);

	if (error) {
		/* FIXME: should we complain to the user? */
		rb_debug ("error while attempting to monitor the library directory: %s", error->message);
	}
}

static void
monitor_library_directory (const char *uri, RhythmDB *db)
{
	GError *error = NULL;

	if ((strcmp (uri, "file:///") == 0) ||
	    (strcmp (uri, "file://") == 0)) {
		/* display an error to the user? */
		return;
	}
	
	rb_debug ("beginning monitor of the library directory %s", uri);
	rhythmdb_monitor_uri_path (db, uri, &error);
	rb_uri_handle_recursively (uri, (GFunc) monitor_subdirectory, NULL, db);

	if (error) {
		/* FIXME: should we complain to the user? */
		rb_debug ("error while attempting to monitor the library directory: %s", error->message);
	}

	rb_debug ("loading new tracks from library directory %s", uri);
	rhythmdb_add_uri (db, uri);
}

static void
monitor_entry_file (RhythmDBEntry *entry, RhythmDB *db)
{
	GError *error = NULL;

	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		rhythmdb_monitor_uri_path (db, entry->location, &error);
	}

	if (error) {
		/* FIXME: should we complain to the user? */
		rb_debug ("error while attempting to monitor library track: %s", error->message);
	} 
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

		g_hash_table_foreach_remove (db->priv->monitored_directories,
					     (GHRFunc) rhythmdb_unmonitor_directories,
					     db);
		g_slist_foreach (db->priv->library_locations, (GFunc) g_free, NULL);
		g_slist_free (db->priv->library_locations);
		db->priv->library_locations = NULL;
	}

	if (eel_gconf_get_boolean (CONF_MONITOR_LIBRARY)) {
		db->priv->library_locations = eel_gconf_get_string_list (CONF_LIBRARY_LOCATION);

		if (db->priv->library_locations) {
			g_slist_foreach (db->priv->library_locations, (GFunc) monitor_library_directory, db);
		}

		/* monitor every directory that contains a (TYPE_SONG) track */
		rhythmdb_entry_foreach (db, (GFunc) monitor_entry_file, db);
	}
}

static void
library_location_changed_cb (GConfClient *client,
			     guint cnxn_id,
			     GConfEntry *entry,
			     RhythmDB *db)
{
	rhythmdb_sync_library_location (db);
}

static gboolean
rhythmdb_check_changed_file (const char *uri, gpointer data, RhythmDB *db)
{
	GTimeVal time;
	glong time_sec = GPOINTER_TO_INT (data);

	g_get_current_time (&time);
	if (time.tv_sec >= time_sec + RHYTHMDB_FILE_MODIFY_PROCESS_TIME) {
		/* process and remove from table */
		RhythmDBEvent *event = g_new0 (RhythmDBEvent, 1);
		event->db = db;
		event->type = RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED;
		event->uri = g_strdup (uri);
		
		g_async_queue_push (db->priv->event_queue, event);
		rb_debug ("adding newly located file %s", uri);
		return TRUE;
	}
	
	rb_debug ("waiting to add newly located file %s", uri);
	
	return FALSE;
}

static gboolean
rhythmdb_process_changed_files (RhythmDB *db)
{
	g_hash_table_foreach_remove (db->priv->changed_files,
				     (GHRFunc)rhythmdb_check_changed_file, db);
	return TRUE;
}

char *
rhythmdb_entry_dup_string (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	const char *s;

	s = rhythmdb_entry_get_string (entry, propid);
	if (s)
		return g_strdup (s);
	else
		return NULL;
}

const char *
rhythmdb_entry_get_string (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TITLE:
		return rb_refstring_get (entry->title);
	case RHYTHMDB_PROP_ALBUM:
		return rb_refstring_get (entry->album);
	case RHYTHMDB_PROP_ARTIST:
		return rb_refstring_get (entry->artist);
	case RHYTHMDB_PROP_GENRE:
		return rb_refstring_get (entry->genre);
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
		return entry->location;
	case RHYTHMDB_PROP_MOUNTPOINT:
		return rb_refstring_get (entry->mountpoint);
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		return rb_refstring_get (entry->last_played_str);
	case RHYTHMDB_PROP_PLAYBACK_ERROR:
		return entry->playback_error;
	case RHYTHMDB_PROP_FIRST_SEEN_STR:
		return rb_refstring_get (entry->first_seen_str);
	case RHYTHMDB_PROP_LAST_SEEN_STR:
		return rb_refstring_get (entry->last_seen_str);
	case RHYTHMDB_PROP_SEARCH_MATCH:
		return NULL;	/* synthetic property */
	/* Podcast properties */
	case RHYTHMDB_PROP_DESCRIPTION:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->description);
		else
			return NULL;
	case RHYTHMDB_PROP_SUBTITLE:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->subtitle);
		else
			return NULL;
	case RHYTHMDB_PROP_SUMMARY: 
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->summary);
		else
			return NULL;
	case RHYTHMDB_PROP_LANG:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->lang);
		else
			return NULL;
	case RHYTHMDB_PROP_COPYRIGHT:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->copyright);
		else
			return NULL;
	case RHYTHMDB_PROP_IMAGE:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->image);
		else
			return NULL;

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

gboolean
rhythmdb_entry_get_boolean (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_HIDDEN:
		return entry->hidden;
	default:
		g_assert_not_reached ();
		return FALSE;
	}
}

guint64
rhythmdb_entry_get_uint64 (RhythmDBEntry *entry, RhythmDBPropType propid)
{
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
	return entry->type;
}

gpointer
rhythmdb_entry_get_pointer (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return entry->type;
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

gulong
rhythmdb_entry_get_ulong (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
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
		if (entry->date)
			return g_date_get_julian (entry->date);
		else
			return 0;
	case RHYTHMDB_PROP_YEAR:
		if (entry->date)
			return g_date_get_year (entry->date);
		else
			return 0;
	case RHYTHMDB_PROP_POST_TIME:
		if (entry->podcast)
			return entry->podcast->post_time;
		else
			return 0;
	case RHYTHMDB_PROP_STATUS:
		if (entry->podcast)
			return entry->podcast->status;		
		else
			return 0;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

double
rhythmdb_entry_get_double (RhythmDBEntry *entry, RhythmDBPropType propid)
{
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

	type = rhythmdb_entry_get_entry_type (entry);
	if (type->get_playback_uri)
		return (type->get_playback_uri) (entry, type->get_playback_uri_data);
	else
		return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
}

GType
rhythmdb_get_property_type (RhythmDB *db, guint property_id)
{
	g_assert (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES);
	return rhythmdb_property_type_map[property_id];
}

GType
rhythmdb_entry_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_pointer_type_register_static ("RhythmDBEntry");
	}

	return type;
}

GType
rhythmdb_entry_type_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_pointer_type_register_static ("RhythmDBEntryType");
	}

	return type;
}

GType
rhythmdb_query_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RhythmDBQuery",
						     (GBoxedCopyFunc)rhythmdb_query_copy,
						     (GBoxedFreeFunc)rhythmdb_query_free);
	}

	return type;
}

