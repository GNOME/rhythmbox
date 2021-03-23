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

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "rb-debug.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rhythmdb-private.h"
#include "rhythmdb-query-result-list.h"
#include "rb-file-helpers.h"

#define RHYTHMDB_FILE_MODIFY_PROCESS_TIME 2

static void rhythmdb_directory_change_cb (GFileMonitor *monitor,
					  GFile *file,
					  GFile *other_file,
					  GFileMonitorEvent event_type,
					  RhythmDB *db);
static void rhythmdb_mount_added_cb (GVolumeMonitor *monitor,
				     GMount *mount,
				     RhythmDB *db);
static void rhythmdb_mount_removed_cb (GVolumeMonitor *monitor,
				       GMount *mount,
				       RhythmDB *db);

void
rhythmdb_init_monitoring (RhythmDB *db)
{
	db->priv->monitored_directories = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal,
								 (GDestroyNotify) g_object_unref,
								 (GDestroyNotify)g_file_monitor_cancel);

	db->priv->changed_files = g_hash_table_new_full (rb_refstring_hash, rb_refstring_equal,
							 (GDestroyNotify) rb_refstring_unref,
							 NULL);

	db->priv->volume_monitor = g_volume_monitor_get ();
	g_signal_connect (G_OBJECT (db->priv->volume_monitor),
			  "mount-added",
			  G_CALLBACK (rhythmdb_mount_added_cb),
			  db);

	g_signal_connect (G_OBJECT (db->priv->volume_monitor),
			  "mount-removed",
			  G_CALLBACK (rhythmdb_mount_removed_cb),
			  db);
	g_signal_connect (G_OBJECT (db->priv->volume_monitor),
			  "mount-pre-unmount",
			  G_CALLBACK (rhythmdb_mount_removed_cb),
			  db);
}

void
rhythmdb_dispose_monitoring (RhythmDB *db)
{
	if (db->priv->changed_files_id != 0) {
		g_source_remove (db->priv->changed_files_id);
		db->priv->changed_files_id = 0;
	}

	if (db->priv->volume_monitor != NULL) {
		g_object_unref (db->priv->volume_monitor);
		db->priv->volume_monitor = NULL;
	}
}

void
rhythmdb_finalize_monitoring (RhythmDB *db)
{
	rhythmdb_stop_monitoring (db);

	g_hash_table_destroy (db->priv->monitored_directories);
	g_hash_table_destroy (db->priv->changed_files);
}

void
rhythmdb_stop_monitoring (RhythmDB *db)
{
	g_hash_table_foreach_remove (db->priv->monitored_directories,
				     (GHRFunc) rb_true_function,
				     db);
}

static void
actually_add_monitor (RhythmDB *db, GFile *directory, GError **error)
{
	GFileMonitor *monitor;

	if (directory == NULL) {
		return;
	}

	g_mutex_lock (&db->priv->monitor_mutex);

	if (g_hash_table_lookup (db->priv->monitored_directories, directory)) {
		g_mutex_unlock (&db->priv->monitor_mutex);
		return;
	}

	monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_SEND_MOVED, db->priv->exiting, error);
	if (monitor != NULL) {
		g_signal_connect_object (G_OBJECT (monitor),
					 "changed",
					 G_CALLBACK (rhythmdb_directory_change_cb),
					 db, 0);
		g_hash_table_insert (db->priv->monitored_directories,
				     g_object_ref (directory),
				     monitor);
	}

	g_mutex_unlock (&db->priv->monitor_mutex);
}

static gboolean
monitor_subdirectory (GFile *file, GFileInfo *info, RhythmDB *db)
{
	char *uri;

	uri = g_file_get_uri (file);
	if (g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_STANDARD_TYPE) == G_FILE_TYPE_DIRECTORY) {
		actually_add_monitor (db, file, NULL);
	} else {
		/* add the file to the database if it's not already there */
		RhythmDBEntry *entry;

		entry = rhythmdb_entry_lookup_by_location (db, uri);
		if (entry == NULL) {
			rhythmdb_add_uri (db, uri);
		}
	}
	g_free (uri);
	return TRUE;	
}

static void
monitor_library_directory (const char *uri, RhythmDB *db)
{
	if ((strcmp (uri, "file:///") == 0) ||
	    (strcmp (uri, "file://") == 0)) {
		/* display an error to the user? */
		return;
	}

	rb_debug ("beginning monitor of the library directory %s", uri);
	rhythmdb_monitor_uri_path (db, uri, NULL);
	rb_uri_handle_recursively_async (uri,
					 NULL,
					 (RBUriRecurseFunc) monitor_subdirectory,
					 g_object_ref (db),
					 (GDestroyNotify)g_object_unref);
}

static gboolean
rhythmdb_check_changed_file (RBRefString *uri, gpointer data, RhythmDB *db)
{
	GTimeVal time;
	glong time_sec = GPOINTER_TO_INT (data);

	g_get_current_time (&time);
	if (time.tv_sec >= time_sec + RHYTHMDB_FILE_MODIFY_PROCESS_TIME) {
		rb_debug ("adding newly located file %s", rb_refstring_get (uri));
		rhythmdb_add_uri (db, rb_refstring_get (uri));
		return TRUE;
	}

	rb_debug ("waiting to add newly located file %s", rb_refstring_get (uri));

	return FALSE;
}

static gboolean
rhythmdb_process_changed_files (RhythmDB *db)
{
	/*
	 * no need for a mutex around the changed files map as it's only accessed
	 * from the main thread.  GFileMonitor's 'changed' signal is emitted from an
	 * idle handler, and we only process the map in a timeout callback.
	 */
	if (g_hash_table_size (db->priv->changed_files) == 0) {
		db->priv->changed_files_id = 0;
		return FALSE;
	}

	g_hash_table_foreach_remove (db->priv->changed_files,
				     (GHRFunc)rhythmdb_check_changed_file, db);
	return TRUE;
}

void
rhythmdb_start_monitoring (RhythmDB *db)
{
	/* monitor all library locations */
	if (db->priv->library_locations) {
		int i;
		for (i = 0; db->priv->library_locations[i] != NULL; i++) {
			monitor_library_directory (db->priv->library_locations[i], db);
		}
	}
}

static void
add_changed_file (RhythmDB *db, const char *uri)
{
	GTimeVal time;

	g_get_current_time (&time);
	g_hash_table_replace (db->priv->changed_files,
			      rb_refstring_new (uri),
			      GINT_TO_POINTER (time.tv_sec));
	if (db->priv->changed_files_id == 0) {
		db->priv->changed_files_id =
			g_timeout_add_seconds (RHYTHMDB_FILE_MODIFY_PROCESS_TIME,
					       (GSourceFunc) rhythmdb_process_changed_files,
					       db);
	}
}

static void
rhythmdb_directory_change_cb (GFileMonitor *monitor,
			      GFile *file,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      RhythmDB *db)
{
	char *canon_uri;
	char *other_canon_uri = NULL;
	RhythmDBEntry *entry;

	canon_uri = g_file_get_uri (file);
	if (other_file != NULL) {
		other_canon_uri = g_file_get_uri (other_file);
	}

	rb_debug ("directory event %d for %s", event_type, canon_uri);

	switch (event_type) {
        case G_FILE_MONITOR_EVENT_CREATED:
		{
			gboolean in_library = FALSE;
			int i;

			if (!g_settings_get_boolean (db->priv->settings, "monitor-library"))
				break;

			if (rb_uri_is_hidden (canon_uri))
				break;

			/* ignore new files outside of the library locations */
			for (i = 0; db->priv->library_locations[i] != NULL; i++) {
				if (rb_uri_is_descendant (canon_uri, db->priv->library_locations[i])) {
					in_library = TRUE;
					break;
				}
			}

			if (!in_library)
				break;
		}

		/* process directories immediately */
		if (rb_uri_is_directory (canon_uri)) {
			actually_add_monitor (db, file, NULL);
			rhythmdb_add_uri (db, canon_uri);
		} else {
			add_changed_file (db, canon_uri);
		}
		break;
	case G_FILE_MONITOR_EVENT_CHANGED:
        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		if (rhythmdb_entry_lookup_by_location (db, canon_uri)) {
			add_changed_file (db, canon_uri);
		}
		break;
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		/* hmm.. */
		break;
	case G_FILE_MONITOR_EVENT_DELETED:
		entry = rhythmdb_entry_lookup_by_location (db, canon_uri);
		if (entry != NULL) {
			g_hash_table_remove (db->priv->changed_files, entry->location);
			rhythmdb_entry_set_visibility (db, entry, FALSE);
			rhythmdb_commit (db);
		}
		break;
	case G_FILE_MONITOR_EVENT_MOVED:
		if (other_canon_uri == NULL) {
			break;
		}

		entry = rhythmdb_entry_lookup_by_location (db, other_canon_uri);
		if (entry != NULL) {
			rb_debug ("file move target %s already exists in database", other_canon_uri);
			entry = rhythmdb_entry_lookup_by_location (db, canon_uri);
			if (entry != NULL) {
				g_hash_table_remove (db->priv->changed_files, entry->location);
				rhythmdb_entry_set_visibility (db, entry, FALSE);
				rhythmdb_commit (db);
			}
		} else {
			entry = rhythmdb_entry_lookup_by_location (db, canon_uri);
			if (entry != NULL) {
				GValue v = {0,};
				g_value_init (&v, G_TYPE_STRING);
				g_value_set_string (&v, other_canon_uri);
				rhythmdb_entry_set_internal (db, entry, TRUE, RHYTHMDB_PROP_LOCATION, &v);
				g_value_unset (&v);
			}
		}
		break;
	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
	default:
		break;
	}

	g_free (canon_uri);
	g_free (other_canon_uri);
}

void
rhythmdb_monitor_uri_path (RhythmDB *db, const char *uri, GError **error)
{
	GFile *directory;

	if (rb_uri_is_directory (uri)) {
		char *dir;
		if (g_str_has_suffix(uri, G_DIR_SEPARATOR_S)) {
			dir = g_strdup (uri);
		} else {
			dir = g_strconcat (uri, G_DIR_SEPARATOR_S, NULL);
		}

		directory = g_file_new_for_uri (dir);
		g_free (dir);
	} else {
		GFile *file;

		file = g_file_new_for_uri (uri);
		directory = g_file_get_parent (file);
		g_object_unref (file);
	}

	actually_add_monitor (db, directory, error);
	g_object_unref (directory);
}

static void
rhythmdb_mount_added_cb (GVolumeMonitor *monitor,
			 GMount *mount,
			 RhythmDB *db)
{
	GList *l;
	RhythmDBQueryResultList *list;
	char *mountpoint;
	GFile *root;

	root = g_mount_get_root (mount);
	mountpoint = g_file_get_uri (root);
	rb_debug ("volume %s mounted", mountpoint);
	g_object_unref (root);

	list = rhythmdb_query_result_list_new ();
	rhythmdb_do_full_query (db,
				RHYTHMDB_QUERY_RESULTS (list),
				RHYTHMDB_QUERY_PROP_EQUALS,
				  RHYTHMDB_PROP_TYPE,
				  RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_PROP_EQUALS,
				  RHYTHMDB_PROP_MOUNTPOINT,
				  mountpoint,
				RHYTHMDB_QUERY_END);
	l = rhythmdb_query_result_list_get_results (list);
	rb_debug ("%d mounted entries to process", g_list_length (l));
	for (; l != NULL; l = l->next) {
		RhythmDBEntry *entry = l->data;
		const char *location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

		rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_MOUNTED);
		if (rb_uri_is_local (location)) {
			rhythmdb_add_uri_with_types (db,
						     location,
						     RHYTHMDB_ENTRY_TYPE_SONG,
						     RHYTHMDB_ENTRY_TYPE_IGNORE,
						     RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
		}
	}
	g_object_unref (list);
	g_free (mountpoint);
	rhythmdb_commit (db);
}

static void
process_unmounted_entries (RhythmDB *db, RhythmDBEntryType *entry_type, const char *mountpoint)
{
	RhythmDBQueryResultList *list;
	GList *l;

	list = rhythmdb_query_result_list_new ();
	rhythmdb_do_full_query (db,
				RHYTHMDB_QUERY_RESULTS (list),
				RHYTHMDB_QUERY_PROP_EQUALS,
				  RHYTHMDB_PROP_TYPE,
				  entry_type,
				RHYTHMDB_QUERY_PROP_EQUALS,
				  RHYTHMDB_PROP_MOUNTPOINT,
				  mountpoint,
				RHYTHMDB_QUERY_END);
	l = rhythmdb_query_result_list_get_results (list);
	rb_debug ("%d unmounted entries to process", g_list_length (l));
	for (; l != NULL; l = l->next) {
		RhythmDBEntry *entry = l->data;
		rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_UNMOUNTED);
	}
	g_object_unref (list);
	rhythmdb_commit (db);
}

static void
rhythmdb_mount_removed_cb (GVolumeMonitor *monitor,
			   GMount *mount,
			   RhythmDB *db)
{
	char *mountpoint;
	GFile *root;

	root = g_mount_get_root (mount);
	mountpoint = g_file_get_uri (root);
	rb_debug ("volume %s unmounted", mountpoint);
	g_object_unref (root);

	process_unmounted_entries (db, RHYTHMDB_ENTRY_TYPE_SONG, mountpoint);
	process_unmounted_entries (db, RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR, mountpoint);
	g_free (mountpoint);
}

GList *
rhythmdb_get_active_mounts (RhythmDB *db)
{
	GList *mounts;
	GList *mountpoints = NULL;
	GList *i;

	mounts = g_volume_monitor_get_mounts (db->priv->volume_monitor);
	for (i = mounts; i != NULL; i = i->next) {
		GFile *root;
		char *mountpoint;
		GMount *mount = i->data;

		root = g_mount_get_root (mount);
		mountpoint = g_file_get_uri (root);
		mountpoints = g_list_prepend (mountpoints, mountpoint);
		g_object_unref (root);
	}

	rb_list_destroy_free (mounts, (GDestroyNotify) g_object_unref);
	return mountpoints;
}
