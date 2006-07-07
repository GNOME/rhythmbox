/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-private.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"


#define RHYTHMDB_FILE_MODIFY_PROCESS_TIME 2

static void rhythmdb_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
 					GnomeVFSVolume *volume, 
 					gpointer data);
static void rhythmdb_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
 					  GnomeVFSVolume *volume, 
 					  gpointer data);

void
rhythmdb_init_monitoring (RhythmDB *db)
{
	db->priv->monitored_directories = g_hash_table_new_full (g_str_hash, g_str_equal,
								 (GDestroyNotify) g_free,
								 NULL);

	db->priv->changed_files = g_hash_table_new_full (g_str_hash, g_str_equal,
							 (GDestroyNotify) g_free,
							 NULL);

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

void
rhythmdb_finalize_monitoring (RhythmDB *db)
{
	rhythmdb_stop_monitoring (db);
	
	g_hash_table_destroy (db->priv->monitored_directories);
	if (db->priv->changed_files_id)
		g_source_remove (db->priv->changed_files_id);
	g_hash_table_destroy (db->priv->changed_files);
}

static gboolean
rhythmdb_unmonitor_directories (char *dir, GnomeVFSMonitorHandle *handle, RhythmDB *db)
{
	gnome_vfs_monitor_cancel (handle);
	return TRUE;
}

void
rhythmdb_stop_monitoring (RhythmDB *db)
{
	g_hash_table_foreach_remove (db->priv->monitored_directories,
				     (GHRFunc) rhythmdb_unmonitor_directories,
				     db);
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


void
rhythmdb_start_monitoring (RhythmDB *db)
{
	db->priv->changed_files_id = g_timeout_add (RHYTHMDB_FILE_MODIFY_PROCESS_TIME * 1000,
						    (GSourceFunc) rhythmdb_process_changed_files, db);

	if (db->priv->library_locations) {
		g_slist_foreach (db->priv->library_locations, (GFunc) monitor_library_directory, db);
	}
		
	/* monitor every directory that contains a (TYPE_SONG) track */
	rhythmdb_entry_foreach (db, (GFunc) monitor_entry_file, db);
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

			/* ignore new files outside of the library locations */
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

void
rhythmdb_monitor_uri_path (RhythmDB *db, const char *uri, GError **error)
{
	char *directory;
	GnomeVFSResult vfsresult;
	GnomeVFSMonitorHandle **handle;

	if (rb_uri_is_directory (uri)) {
		if (g_str_has_suffix(uri, G_DIR_SEPARATOR_S)) {
			directory = g_strdup (uri);
		} else {
			directory = g_strconcat (uri, G_DIR_SEPARATOR_S, NULL);
		}
	} else {
		GnomeVFSURI *vfsuri, *parent;
		
		vfsuri = gnome_vfs_uri_new (uri);
		parent = gnome_vfs_uri_get_parent (vfsuri);
		directory = gnome_vfs_uri_to_string (parent, GNOME_VFS_URI_HIDE_NONE);
		gnome_vfs_uri_unref (vfsuri);
		gnome_vfs_uri_unref (parent);
	}

	if (directory == NULL || g_hash_table_lookup (db->priv->monitored_directories, directory))
		return;

	handle = g_new0 (GnomeVFSMonitorHandle *, 1);
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

			/* make files visible immediately, 
			 * then hide any that turn out to be missing.
			 */
			rhythmdb_entry_set_visibility (ctxt->db, entry, TRUE);
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

