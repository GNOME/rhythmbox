/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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
#include <gconf/gconf-client.h>
#include <gio/gio.h>

#include "rb-debug.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rhythmdb-private.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

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
	char *uri;

	if (directory == NULL) {
		return;
	}

	if (g_hash_table_lookup (db->priv->monitored_directories, directory)) {
		return;
	}

	uri = g_file_get_uri (directory);
	monitor = g_file_monitor_directory (directory, 0, db->priv->exiting, error);
	if (monitor != NULL) {
		g_signal_connect_object (G_OBJECT (monitor),
					 "changed",
					 G_CALLBACK (rhythmdb_directory_change_cb),
					 db, 0);
		g_hash_table_insert (db->priv->monitored_directories,
				     g_object_ref (directory),
				     monitor);
	}
}

static void
monitor_entry_file (RhythmDBEntry *entry, RhythmDB *db)
{
	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		const char *loc;
		GSList *l;

		loc = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

		/* don't add add monitor if it's in the library path */
		for (l = db->priv->library_locations; l != NULL; l = g_slist_next (l)) {
			if (g_str_has_prefix (loc, (const char*)l->data))
				return;
				
		}
		rhythmdb_monitor_uri_path (db, loc, NULL);
	}
}

static gboolean
monitor_subdirectory (GFile *file, gboolean dir, RhythmDB *db)
{
	char *uri;

	uri = g_file_get_uri (file);
	if (dir) {
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
		/* process and remove from table */
		RhythmDBEvent *event = g_slice_new0 (RhythmDBEvent);
		event->db = db;
		event->type = RHYTHMDB_EVENT_FILE_CREATED_OR_MODIFIED;
		event->uri = rb_refstring_ref (uri);

		g_async_queue_push (db->priv->event_queue, event);
		rb_debug ("adding newly located file %s", rb_refstring_get (uri));
		return TRUE;
	}

	rb_debug ("waiting to add newly located file %s", rb_refstring_get (uri));

	return FALSE;
}

static gboolean
rhythmdb_process_changed_files (RhythmDB *db)
{
	if (g_hash_table_size (db->priv->changed_files) == 0) {
		db->priv->changed_files_id = 0;
		return FALSE;
	}

	g_hash_table_foreach_remove (db->priv->changed_files,
				     (GHRFunc)rhythmdb_check_changed_file, db);
	return TRUE;
}

static gpointer
_monitor_entry_thread (RhythmDB *db)
{
	rhythmdb_entry_foreach (db, (GFunc) monitor_entry_file, db);
	g_object_unref (G_OBJECT (db));
	return NULL;
}

void
rhythmdb_start_monitoring (RhythmDB *db)
{
	g_thread_create ((GThreadFunc)_monitor_entry_thread, g_object_ref (db), FALSE, NULL);

	/* monitor all library locations */
	if (db->priv->library_locations)
		g_slist_foreach (db->priv->library_locations, (GFunc) monitor_library_directory, db);
}

static void
add_changed_file (RhythmDB *db, const char *uri)
{
	GTimeVal time;

	g_get_current_time (&time);
	g_hash_table_replace (db->priv->changed_files,
			      rb_refstring_new (uri),
			      GINT_TO_POINTER (time.tv_sec));
	if (db->priv->changed_files_id == 0)
		db->priv->changed_files_id = g_timeout_add (RHYTHMDB_FILE_MODIFY_PROCESS_TIME * 1000,
							    (GSourceFunc) rhythmdb_process_changed_files, db);
}

static void
rhythmdb_directory_change_cb (GFileMonitor *monitor,
			      GFile *file,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      RhythmDB *db)
{
	char *canon_uri;
	canon_uri = g_file_get_uri (file);
	rb_debug ("directory event %d for %s", event_type, canon_uri);

	switch (event_type) {
        case G_FILE_MONITOR_EVENT_CREATED:
		{
			GSList *cur;
			gboolean in_library = FALSE;

			if (!eel_gconf_get_boolean (CONF_MONITOR_LIBRARY))
				break;

			if (rb_uri_is_hidden (canon_uri))
				break;

			/* ignore new files outside of the library locations */
			for (cur = db->priv->library_locations; cur != NULL; cur = g_slist_next (cur)) {
				if (g_str_has_prefix (canon_uri, cur->data)) {
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
		if (rhythmdb_entry_lookup_by_location (db, canon_uri)) {
			RhythmDBEvent *event = g_slice_new0 (RhythmDBEvent);
			event->db = db;
			event->type = RHYTHMDB_EVENT_FILE_DELETED;
			event->uri = rb_refstring_new (canon_uri);
			g_async_queue_push (db->priv->event_queue, event);
		}
		break;
	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
		break;
	}

	g_free (canon_uri);
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
}

typedef struct
{
	RhythmDB *db;
	RBRefString *mount_point;
	gboolean mounted;
} MountCtxt;

static void
entry_volume_mounted_or_unmounted (RhythmDBEntry *entry,
				   MountCtxt *ctxt)
{
	RBRefString *mount_point;
	const char *location;

	if (entry->type != RHYTHMDB_ENTRY_TYPE_SONG &&
	    entry->type != RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR) {
		return;
	}

	mount_point = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (mount_point == NULL || !rb_refstring_equal (mount_point, ctxt->mount_point)) {
		return;
	}
	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

	if (entry->type == RHYTHMDB_ENTRY_TYPE_SONG) {
		if (ctxt->mounted) {
			rb_debug ("queueing stat for entry %s (mounted)", location);

			/* make files visible immediately,
			 * then hide any that turn out to be missing.
			 */
			rhythmdb_entry_set_visibility (ctxt->db, entry, TRUE);
			rhythmdb_add_uri_with_types (ctxt->db,
						     location,
						     RHYTHMDB_ENTRY_TYPE_SONG,
						     RHYTHMDB_ENTRY_TYPE_IGNORE,
						     RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);
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
rhythmdb_mount_added_cb (GVolumeMonitor *monitor,
			 GMount *mount,
			 RhythmDB *db)
{
	MountCtxt ctxt;
	char *mp;
	GFile *root;

	root = g_mount_get_root (mount);
	mp = g_file_get_uri (root);
	g_object_unref (root);

	ctxt.mount_point = rb_refstring_new (mp);
	g_free (mp);

	ctxt.db = db;
	ctxt.mounted = TRUE;
	rb_debug ("volume %s mounted", rb_refstring_get (ctxt.mount_point));
	rhythmdb_entry_foreach (db,
				(GFunc)entry_volume_mounted_or_unmounted,
				&ctxt);
	rhythmdb_commit (db);
	rb_refstring_unref (ctxt.mount_point);
}

static void
rhythmdb_mount_removed_cb (GVolumeMonitor *monitor,
			   GMount *mount,
			   RhythmDB *db)
{
	MountCtxt ctxt;
	char *mp;
	GFile *root;

	root = g_mount_get_root (mount);
	mp = g_file_get_uri (root);
	g_object_unref (root);

	ctxt.mount_point = rb_refstring_new (mp);
	g_free (mp);

	ctxt.db = db;
	ctxt.mounted = FALSE;
	rb_debug ("volume %s unmounted", rb_refstring_get (ctxt.mount_point));
	rhythmdb_entry_foreach (db,
				(GFunc)entry_volume_mounted_or_unmounted,
				&ctxt);
	rhythmdb_commit (db);
	rb_refstring_unref (ctxt.mount_point);
}
