/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implemetation files for podcast download manager
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include "config.h"

#include <string.h>
#define __USE_XOPEN
#include <time.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-uri.h>

#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rb-podcast-manager.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-marshal.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-podcast-parse.h"
#include "rb-dialog.h"
#include "rb-metadata.h"
#include "rb-util.h"

#define CONF_STATE_PODCAST_PREFIX		CONF_PREFIX "/state/podcast"
#define CONF_STATE_PODCAST_DOWNLOAD_DIR		CONF_STATE_PODCAST_PREFIX "/download_prefix"
#define CONF_STATE_PODCAST_DOWNLOAD_INTERVAL	CONF_STATE_PODCAST_PREFIX "/download_interval"
#define CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME	CONF_STATE_PODCAST_PREFIX "/download_next_time"

enum
{
	PROP_0,
	PROP_DB
};

enum
{
	UPDATE_EVERY_HOUR,
	UPDATE_EVERY_DAY,
	UPDATE_EVERY_WEEK,
	UPDATE_MANUALLY
};

enum
{
	STATUS_CHANGED,
	START_DOWNLOAD,
	FINISH_DOWNLOAD,
	PROCESS_ERROR,
	FEED_UPDATES_AVAILABLE,
	LAST_SIGNAL
};

typedef enum
{
	RESULT_PARSE_OK,
	RESULT_PARSE_ERROR
} RBPodcastParseResultType;

/* passed from feed parsing threads back to main thread */
typedef struct
{
	RBPodcastParseResultType result;
	RBPodcastChannel 	*channel;
	RBPodcastManager	*pd;
} RBPodcastManagerParseResult;

typedef struct
{
	RBPodcastManager *pd;
	RhythmDBEntry *entry;
	GnomeVFSAsyncHandle *read_handle;
	GnomeVFSURI *write_uri;
	GnomeVFSURI *read_uri;
	char *query_string;

	guint total_size;
	guint progress;
	gboolean cancelled;
} RBPodcastManagerInfo;

typedef struct
{
	RBPodcastManager *pd;
	char *url;
} RBPodcastThreadInfo;

struct RBPodcastManagerPrivate
{
	RhythmDB *db;
	GList *download_list;
	RBPodcastManagerInfo *active_download;
	guint next_time;
	guint source_sync;
	guint update_interval_notify_id;
	guint next_file_id;
	gboolean shutdown;

	gboolean remove_files;
};

#define RB_PODCAST_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PODCAST_MANAGER, RBPodcastManagerPrivate))


static guint rb_podcast_manager_signals[LAST_SIGNAL] = { 0 };

/* functions */
static void rb_podcast_manager_class_init 		(RBPodcastManagerClass *klass);
static void rb_podcast_manager_init 			(RBPodcastManager *dp);
static GObject *rb_podcast_manager_constructor 		(GType type, guint n_construct_properties,
			   				 GObjectConstructParam *construct_properties);
static void rb_podcast_manager_dispose 			(GObject *object);
static void rb_podcast_manager_finalize 		(GObject *object);
static void rb_podcast_manager_set_property 		(GObject *object,
                                   			 guint prop_id,
		                                	 const GValue *value,
		                                	 GParamSpec *pspec);
static void rb_podcast_manager_get_property 		(GObject *object,
							 guint prop_id,
		                                	 GValue *value,
                		                	 GParamSpec *pspec);
static void rb_podcast_manager_download_file_info_cb	(GnomeVFSAsyncHandle *handle,
							 GList *results,
							 RBPodcastManagerInfo *data);
static void rb_podcast_manager_abort_download		(RBPodcastManagerInfo *data);
static gboolean rb_podcast_manager_sync_head_cb 	(gpointer data);
static gboolean rb_podcast_manager_head_query_cb 	(GtkTreeModel *query_model,
						   	 GtkTreePath *path,
							 GtkTreeIter *iter,
						   	 RBPodcastManager *data);
static gboolean rb_podcast_manager_save_metadata	(RhythmDB *db,
						  	 RhythmDBEntry *entry,
						  	 const char *uri);
static void rb_podcast_manager_db_entry_added_cb 	(RBPodcastManager *pd,
							 RhythmDBEntry *entry);
static void rb_podcast_manager_db_entry_deleted_cb 	(RBPodcastManager *pd,
							 RhythmDBEntry *entry);
static gboolean rb_podcast_manager_next_file 		(RBPodcastManager * pd);
static void rb_podcast_manager_insert_feed 		(RBPodcastManager *pd, RBPodcastChannel *data);

static gpointer rb_podcast_manager_thread_parse_feed	(RBPodcastThreadInfo *info);

/* async read file functions */
static guint download_progress_cb			(GnomeVFSXferProgressInfo *info,
							 gpointer data);
static guint download_progress_update_cb		(GnomeVFSAsyncHandle *handle,
							 GnomeVFSXferProgressInfo *info,
							 gpointer data);

/* internal functions */
static void download_info_free				(RBPodcastManagerInfo *data);
static void start_job					(RBPodcastManagerInfo *data);
static gboolean end_job					(RBPodcastManagerInfo *data);
static void cancel_job					(RBPodcastManagerInfo *pd);
static void rb_podcast_manager_update_synctime		(RBPodcastManager *pd);
static void rb_podcast_manager_config_changed		(GConfClient* client,
                                        		 guint cnxn_id,
				                         GConfEntry *entry,
							 gpointer user_data);

G_DEFINE_TYPE (RBPodcastManager, rb_podcast_manager, G_TYPE_OBJECT)

static void
rb_podcast_manager_class_init (RBPodcastManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_podcast_manager_constructor;
	object_class->dispose = rb_podcast_manager_dispose;
	object_class->finalize = rb_podcast_manager_finalize;

	object_class->set_property = rb_podcast_manager_set_property;
        object_class->get_property = rb_podcast_manager_get_property;

	g_object_class_install_property (object_class,
                                         PROP_DB,
                                         g_param_spec_object ("db",
							      "db",
							      "database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));

	rb_podcast_manager_signals[STATUS_CHANGED] =
	       g_signal_new ("status_changed",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, status_changed),
				NULL, NULL,
				rb_marshal_VOID__BOXED_ULONG,
				G_TYPE_NONE,
				2,
				RHYTHMDB_TYPE_ENTRY,
				G_TYPE_ULONG);

	rb_podcast_manager_signals[START_DOWNLOAD] =
	       g_signal_new ("start_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, start_download),
				NULL, NULL,
				g_cclosure_marshal_VOID__BOXED,
				G_TYPE_NONE,
				1,
				RHYTHMDB_TYPE_ENTRY);

	rb_podcast_manager_signals[FINISH_DOWNLOAD] =
	       g_signal_new ("finish_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, finish_download),
				NULL, NULL,
				g_cclosure_marshal_VOID__BOXED,
				G_TYPE_NONE,
				1,
				RHYTHMDB_TYPE_ENTRY);

	rb_podcast_manager_signals[FEED_UPDATES_AVAILABLE] =
	       g_signal_new ("feed_updates_available",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, feed_updates_available),
				NULL, NULL,
				g_cclosure_marshal_VOID__BOXED,
				G_TYPE_NONE,
				1,
				RHYTHMDB_TYPE_ENTRY);

	rb_podcast_manager_signals[PROCESS_ERROR] =
	       g_signal_new ("process_error",
		       		G_OBJECT_CLASS_TYPE (object_class),
				GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, process_error),
				NULL, NULL,
				g_cclosure_marshal_VOID__STRING,
				G_TYPE_NONE,
				1,
				G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBPodcastManagerPrivate));
}

static void
rb_podcast_manager_init (RBPodcastManager *pd)
{
	pd->priv = RB_PODCAST_MANAGER_GET_PRIVATE (pd);

	pd->priv->source_sync = 0;
	pd->priv->db = NULL;
	eel_gconf_monitor_add (CONF_STATE_PODCAST_PREFIX);
}

static GObject *
rb_podcast_manager_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBPodcastManager *pd;

	pd = RB_PODCAST_MANAGER (G_OBJECT_CLASS (rb_podcast_manager_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	pd->priv->update_interval_notify_id = eel_gconf_notification_add (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL,
	                    			       			  rb_podcast_manager_config_changed,
	                            		       			  pd);

	return G_OBJECT (pd);

}

static void
rb_podcast_manager_dispose (GObject *object)
{
	RBPodcastManager *pd;
	g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PODCAST_MANAGER (object));

	pd = RB_PODCAST_MANAGER (object);
	g_return_if_fail (pd->priv != NULL);

	eel_gconf_monitor_remove (CONF_STATE_PODCAST_PREFIX);

	if (pd->priv->next_file_id != 0) {
		g_source_remove (pd->priv->next_file_id);
		pd->priv->next_file_id = 0;
	}

	if (pd->priv->source_sync != 0) {
		g_source_remove (pd->priv->source_sync);
		pd->priv->source_sync = 0;
	}

	if (pd->priv->update_interval_notify_id != 0) {
		eel_gconf_notification_remove (pd->priv->update_interval_notify_id);
		pd->priv->update_interval_notify_id = 0;
	}

	if (pd->priv->db != NULL) {
		g_object_unref (pd->priv->db);
		pd->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_podcast_manager_parent_class)->dispose (object);
}

static void
rb_podcast_manager_finalize (GObject *object)
{
	RBPodcastManager *pd;
	g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PODCAST_MANAGER (object));

	pd = RB_PODCAST_MANAGER(object);

	g_return_if_fail (pd->priv != NULL);

	if (pd->priv->download_list) {
		g_list_foreach (pd->priv->download_list, (GFunc)g_free, NULL);
		g_list_free (pd->priv->download_list);
	}

	G_OBJECT_CLASS (rb_podcast_manager_parent_class)->finalize (object);
}

static void
rb_podcast_manager_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);

	switch (prop_id) {
	case PROP_DB:
		if (pd->priv->db) {
			g_signal_handlers_disconnect_by_func (pd->priv->db,
							      G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
							      pd);

			g_signal_handlers_disconnect_by_func (pd->priv->db,
							      G_CALLBACK (rb_podcast_manager_db_entry_deleted_cb),
							      pd);
			g_object_unref (pd->priv->db);
		}

		pd->priv->db = g_value_get_object (value);
		g_object_ref (pd->priv->db);

	        g_signal_connect_object (pd->priv->db,
	                                 "entry-added",
	                                 G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
	                                 pd, G_CONNECT_SWAPPED);

	        g_signal_connect_object (pd->priv->db,
	                                 "entry_deleted",
	                                 G_CALLBACK (rb_podcast_manager_db_entry_deleted_cb),
	                                 pd, G_CONNECT_SWAPPED);

		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
rb_podcast_manager_get_property (GObject *object,
					guint prop_id,
		                        GValue *value,
                		        GParamSpec *pspec)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, pd->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}

}

RBPodcastManager *
rb_podcast_manager_new (RhythmDB *db)
{
	RBPodcastManager *pd;

	pd = g_object_new (RB_TYPE_PODCAST_MANAGER, "db", db, NULL);
	return pd;
}

void
rb_podcast_manager_download_entry (RBPodcastManager *pd,
				   RhythmDBEntry *entry)
{
	gulong status;
	g_assert (rb_is_main_thread ());

	g_return_if_fail (RB_IS_PODCAST_MANAGER (pd));

	if (entry == NULL)
		return;

	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	if ((status < RHYTHMDB_PODCAST_STATUS_COMPLETE) ||
	    (status == RHYTHMDB_PODCAST_STATUS_WAITING)) {
		RBPodcastManagerInfo *data;
		if (status < RHYTHMDB_PODCAST_STATUS_COMPLETE) {
			GValue status_val = { 0, };
			g_value_init (&status_val, G_TYPE_ULONG);
			g_value_set_ulong (&status_val, RHYTHMDB_PODCAST_STATUS_WAITING);
			rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_STATUS, &status_val);
			g_value_unset (&status_val);

			rhythmdb_commit (pd->priv->db);
		}
		rb_debug ("Adding podcast episode %s to download list",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

		data = g_new0 (RBPodcastManagerInfo, 1);
		data->pd = g_object_ref (pd);
		data->entry = rhythmdb_entry_ref (entry);

		pd->priv->download_list = g_list_append (pd->priv->download_list, data);
		if (pd->priv->next_file_id == 0) {
			pd->priv->next_file_id =
				g_idle_add ((GSourceFunc) rb_podcast_manager_next_file, pd);
		}
	}
}

gboolean
rb_podcast_manager_entry_downloaded (RhythmDBEntry *entry)
{
	gulong status;
	const gchar *file_name;
	RhythmDBEntryType type = rhythmdb_entry_get_entry_type (entry);

	g_assert (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST);

	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	file_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);

	return (status != RHYTHMDB_PODCAST_STATUS_ERROR && file_name != NULL);
}

void
rb_podcast_manager_start_sync (RBPodcastManager *pd)
{
	gint next_time;

	g_return_if_fail (RB_IS_PODCAST_MANAGER (pd));

	if (pd->priv->next_time > 0) {
		next_time = pd->priv->next_time;
	} else {
		next_time = eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME);
	}

	if (next_time > 0) {
		if (pd->priv->source_sync != 0) {
			g_source_remove (pd->priv->source_sync);
			pd->priv->source_sync = 0;
		}
		next_time = next_time - ((int)time (NULL));
		if (next_time <= 0) {
			rb_podcast_manager_update_feeds (pd);
			pd->priv->next_time = 0;
			rb_podcast_manager_update_synctime (pd);
			return;
		}
		pd->priv->source_sync = g_timeout_add (next_time * 1000, (GSourceFunc) rb_podcast_manager_sync_head_cb, pd);
	}

}

static gboolean
rb_podcast_manager_sync_head_cb (gpointer data)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (data);

	g_assert (rb_is_main_thread ());

	GDK_THREADS_ENTER ();

	rb_podcast_manager_update_feeds (pd);
	pd->priv->source_sync = 0;
	pd->priv->next_time = 0;
	rb_podcast_manager_update_synctime (RB_PODCAST_MANAGER (data));
	GDK_THREADS_LEAVE ();
	return FALSE;
}

void
rb_podcast_manager_update_feeds (RBPodcastManager *pd)
{
	GtkTreeModel *query_model;

	g_return_if_fail (RB_IS_PODCAST_MANAGER (pd));

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (pd->priv->db));

	rhythmdb_do_full_query (pd->priv->db,
				RHYTHMDB_QUERY_RESULTS (query_model),
                                RHYTHMDB_QUERY_PROP_EQUALS,
                                RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
                                RHYTHMDB_QUERY_END);

 	gtk_tree_model_foreach (query_model,
		                (GtkTreeModelForeachFunc) rb_podcast_manager_head_query_cb,
                                pd);

	g_object_unref (query_model);
}

static gboolean
rb_podcast_manager_head_query_cb (GtkTreeModel *query_model,
 	   			  GtkTreePath *path,
				  GtkTreeIter *iter,
				  RBPodcastManager *manager)
{
        const char *uri;
        RhythmDBEntry *entry;
	guint status;

        gtk_tree_model_get (query_model, iter, 0, &entry, -1);
        uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);

	if (status == 1)
		rb_podcast_manager_subscribe_feed (manager, uri);

	rhythmdb_entry_unref (entry);

        return FALSE;
}

static gboolean
rb_podcast_manager_next_file (RBPodcastManager * pd)
{
	const char *location;
	RBPodcastManagerInfo *data;
	char *query_string;
	GList *d;

	g_assert (rb_is_main_thread ());

	rb_debug ("looking for something to download");

	GDK_THREADS_ENTER ();

	pd->priv->next_file_id = 0;

	if (pd->priv->active_download != NULL) {
		rb_debug ("already downloading something");
		GDK_THREADS_LEAVE ();
		return FALSE;
	}

	d = g_list_first (pd->priv->download_list);
	if (d == NULL) {
		rb_debug ("download queue is empty");
		GDK_THREADS_LEAVE ();
		return FALSE;
	}

	data = (RBPodcastManagerInfo *) d->data;
	g_assert (data != NULL);
	g_assert (data->entry != NULL);

	pd->priv->active_download = data;

	location = rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION);
	rb_debug ("processing %s", location);

	/* gnome-vfs currently doesn't handle HTTP query strings correctly.
	 * so we do it ourselves.
	 */
	query_string = strrchr (location, '?');
	if (query_string != NULL) {
		char *base_uri;

		base_uri = g_strdup (location);
		query_string = strrchr (base_uri, '?');
		*query_string++ = '\0';
		rb_debug ("hiding query string %s from gnome-vfs", query_string);

		data->read_uri = gnome_vfs_uri_new (base_uri);
		if (data->read_uri != NULL) {
			char *full_uri;

			full_uri = g_strdup_printf ("%s?%s",
						    data->read_uri->text,
						    query_string);
			g_free (data->read_uri->text);
			data->read_uri->text = full_uri;

			/* include the question mark in data->query_string to make
			 * the later check easier.
			 */
			query_string--;
			*query_string = '?';
			data->query_string = g_strdup (query_string);
		}
		g_free (base_uri);
	} else {
		data->read_uri = gnome_vfs_uri_new (location);
	}

	if (data->read_uri == NULL) {
		rb_debug ("Error downloading podcast: could not create remote uri");
		rb_podcast_manager_abort_download (data);
	} else {
		GList *l;

		l = g_list_prepend (NULL, data->read_uri);
		gnome_vfs_async_get_file_info (&data->read_handle,
					       l,
					       GNOME_VFS_FILE_INFO_FOLLOW_LINKS,
					       GNOME_VFS_PRIORITY_DEFAULT,
					       (GnomeVFSAsyncGetFileInfoCallback) rb_podcast_manager_download_file_info_cb,
					       data);
		g_list_free (l);
	}

	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
rb_podcast_manager_download_file_info_cb (GnomeVFSAsyncHandle *handle,
					  GList *results,
					  RBPodcastManagerInfo *data)
{
	GnomeVFSGetFileInfoResult *result = results->data;
	char *local_file_name;
	char *local_file_path;
	char *dir_name;
	char *conf_dir_name;

	g_assert (rb_is_main_thread ());

	rb_debug ("got file info results for %s",
		  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION));

	if (result->result != GNOME_VFS_OK) {

		GValue val = {0,};

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_ERROR);
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, gnome_vfs_result_to_string (result->result));
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
		g_value_unset (&val);

		rhythmdb_commit (data->pd->priv->db);

		rb_debug ("get_file_info request failed");
		rb_podcast_manager_abort_download (data);
		return;
	}

	/* construct download directory */
	conf_dir_name = rb_podcast_manager_get_podcast_dir (data->pd);
	dir_name = g_build_filename (conf_dir_name,
				     rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_ALBUM),
				     NULL);
	g_free (conf_dir_name);

	if (g_mkdir_with_parents (dir_name, 0750) == -1) {
		rb_debug ("Could not create podcast download directory %s", dir_name);
		/* FIXME: display error to user */
		g_free (dir_name);
		rb_podcast_manager_abort_download (data);
		return;
	}

	/* if the filename ends with the query string from the original URI,
	 * remove it.
	 */
	if (data->query_string &&
	    g_str_has_suffix (result->file_info->name, data->query_string)) {
		local_file_name = g_strdup (result->file_info->name);
		local_file_name[strlen (local_file_name) - strlen (data->query_string)] = '\0';
		rb_debug ("removing query string \"%s\" -> local file name \"%s\"", data->query_string, local_file_name);
	} else {
		local_file_name = result->file_info->name;
	}

	/* construct local filename */
	local_file_path = g_build_filename (dir_name,
					    local_file_name,
					    NULL);

	if (local_file_name != result->file_info->name)
		g_free (local_file_name);

	g_free (dir_name);
	rb_debug ("creating file %s", local_file_path);

	local_file_name = g_filename_to_uri (local_file_path, NULL, NULL);
	g_free (local_file_path);
	local_file_path = local_file_name;

	data->write_uri = gnome_vfs_uri_new (local_file_path);
	if (data->write_uri == NULL) {
		g_warning ("Could not create local podcast URI for %s", local_file_path);
		rb_podcast_manager_abort_download (data);
		return;
	}

	if (g_file_test (local_file_path, G_FILE_TEST_EXISTS)) {
		guint64 local_size;
		GnomeVFSFileInfo *local_info;
		GnomeVFSResult local_result;

		local_info = gnome_vfs_file_info_new ();
		local_result = gnome_vfs_get_file_info (local_file_path,
							local_info,
							GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
		local_size = local_info->size;
		gnome_vfs_file_info_unref (local_info);

		if (local_result != GNOME_VFS_OK) {
			g_warning ("Could not get info on downloaded podcast file %s",
				   local_file_path);
			rb_podcast_manager_abort_download (data);
			return;
		} else if (result->file_info->size == local_size) {
			GValue val = {0,};
			char *uri;
			char *canon_uri;

			uri = gnome_vfs_uri_to_string (data->write_uri, GNOME_VFS_URI_HIDE_NONE);
			canon_uri = rb_canonicalise_uri (uri);
			g_free (uri);

			rb_debug ("podcast %s already downloaded",
				  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION));

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_COMPLETE);
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);

			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, canon_uri);
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
			g_value_unset (&val);

			rb_podcast_manager_save_metadata (data->pd->priv->db, data->entry, canon_uri);

			g_free (canon_uri);

			rb_podcast_manager_abort_download (data);
			return;
		} else if (result->file_info->size > local_size) {
			/* TODO: support resume file */
			rb_debug ("podcast episode already partially downloaded, but we can't resume downloads");
		} else {
			/* the local file is larger. replace it */
		}
	}

	g_free (local_file_path);
	start_job (data);

}

static void
rb_podcast_manager_abort_download (RBPodcastManagerInfo *data)
{
	RBPodcastManager *mgr = data->pd;

	g_assert (rb_is_main_thread ());

	mgr->priv->download_list = g_list_remove (mgr->priv->download_list, data);
	download_info_free (data);

	if (mgr->priv->active_download == data)
		mgr->priv->active_download = NULL;

	if (mgr->priv->next_file_id == 0) {
		mgr->priv->next_file_id =
			g_idle_add ((GSourceFunc) rb_podcast_manager_next_file, mgr);
	}
}

gboolean
rb_podcast_manager_subscribe_feed (RBPodcastManager *pd, const char *url)
{
	RBPodcastThreadInfo *info;
	gchar *valid_url = gnome_vfs_make_uri_from_input (url);

	if (valid_url == NULL) {
		rb_error_dialog (NULL, _("Invalid URL"),
				 _("The URL \"%s\" is not valid, please check it."), url);
		return FALSE;
	}

	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (pd->priv->db, valid_url);
	if (entry) {
		if (rhythmdb_entry_get_entry_type (entry) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			/* added as something else, probably iradio */
			rb_error_dialog (NULL, _("URL already added"),
					 _("The URL \"%s\" has already been added as a radio station. "
					 "If this is a podcast feed, please remove the radio station."), url);
			return FALSE;
		}
	}

	info = g_new0 (RBPodcastThreadInfo, 1);
	info->pd = g_object_ref (pd);
	info->url = valid_url;

	g_thread_create ((GThreadFunc) rb_podcast_manager_thread_parse_feed,
			 info, FALSE, NULL);

	return TRUE;
}

static void
rb_podcast_manager_free_parse_result (RBPodcastManagerParseResult *result)
{
	rb_podcast_parse_channel_free (result->channel);
	g_object_unref (result->pd);
	g_free (result);
}

static gboolean
rb_podcast_manager_parse_complete_cb (RBPodcastManagerParseResult *result)
{
	GDK_THREADS_ENTER ();
	if (result->pd->priv->shutdown) {
		GDK_THREADS_LEAVE ();
		return FALSE;
	}

	switch (result->result)
	{
		case RESULT_PARSE_OK:
			rb_podcast_manager_insert_feed (result->pd, result->channel);
			break;
		case RESULT_PARSE_ERROR:
		{
			gchar *error_msg;
			error_msg = g_strdup_printf (_("There was a problem adding this podcast. Please verify the URL: %s"),
						     (gchar *) result->channel->url);
			g_signal_emit (result->pd,
				       rb_podcast_manager_signals[PROCESS_ERROR],
				       0, error_msg);
			g_free (error_msg);
			break;
		}
	}

	GDK_THREADS_LEAVE ();
	return FALSE;
}


static gpointer
rb_podcast_manager_thread_parse_feed (RBPodcastThreadInfo *info)
{
	RBPodcastChannel *feed = g_new0 (RBPodcastChannel, 1);

	if (rb_podcast_parse_load_feed (feed, info->url)) {
		RBPodcastManagerParseResult *result;

		result = g_new0 (RBPodcastManagerParseResult, 1);
		result->channel = feed;
		result->result = (feed->title == NULL) ? RESULT_PARSE_ERROR : RESULT_PARSE_OK;
		result->pd = g_object_ref (info->pd);

		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				 (GSourceFunc) rb_podcast_manager_parse_complete_cb,
				 result,
				 (GDestroyNotify) rb_podcast_manager_free_parse_result);
	}

	g_object_unref (info->pd);
	g_free (info->url);
	g_free (info);
	return NULL;
}

RhythmDBEntry *
rb_podcast_manager_add_post (RhythmDB *db,
			      const char *name,
			      const char *title,
			      const char *subtitle,
			      const char *generator,
			      const char *uri,
			      const char *description,
			      gulong date,
			      gulong duration,
			      guint64 filesize)
{
	RhythmDBEntry *entry;
	GValue val = {0,};
	GTimeVal time;

	if (!uri || !name || !title || !g_utf8_validate(uri, -1, NULL)) {
		return NULL;
	}
	entry = rhythmdb_entry_lookup_by_location (db, uri);
	if (entry)
		return NULL;

	entry = rhythmdb_entry_new (db,
				    RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				    uri);
	if (entry == NULL)
		return NULL;

	g_get_current_time (&time);
	if (date == 0)
		date = time.tv_sec;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, name);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);

	g_value_reset (&val);
	g_value_set_static_string (&val, _("Podcast"));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);

	g_value_reset (&val);
	g_value_set_string (&val, title);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);

	g_value_reset (&val);
	if (subtitle)
		g_value_set_string (&val, subtitle);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_SUBTITLE, &val);

	g_value_reset (&val);
	if (description)
		g_value_set_string (&val, description);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DESCRIPTION, &val);

	g_value_reset (&val);
	if (generator)
		g_value_set_string (&val, generator);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_PAUSED);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &val);

	g_value_reset (&val);
	g_value_set_ulong (&val, date);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_POST_TIME, &val);

	g_value_reset (&val);
	g_value_set_ulong (&val, duration);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &val);

	g_value_reset (&val);
	g_value_set_ulong (&val, 0);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);

	/* first seen */
	g_value_reset (&val);
	g_value_set_ulong (&val, time.tv_sec);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FIRST_SEEN, &val);
	g_value_unset (&val);

	/* initialize the rating */
	g_value_init (&val, G_TYPE_DOUBLE);
	g_value_set_double (&val, 2.5);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, filesize);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	return entry;
}

static gboolean
rb_podcast_manager_save_metadata (RhythmDB *db, RhythmDBEntry *entry, const char *uri)
{
	RBMetaData *md = rb_metadata_new ();
	GError *error = NULL;
	GValue val = { 0, };
	const char *mime;

	rb_debug ("Loading podcast metadata from %s", uri);
        rb_metadata_load (md, uri, &error);

	if (error != NULL) {
		/* this probably isn't an audio enclosure. or some other error */
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_ERROR);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, error->message);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
		g_value_unset (&val);

		rhythmdb_commit (db);

		g_object_unref (md);
		g_error_free (error);

		return FALSE;
	}

	mime = rb_metadata_get_mime (md);
	if (mime) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, mime);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MIMETYPE, &val);
		g_value_unset (&val);
	}

	if (rb_metadata_get (md,
			     RB_METADATA_FIELD_DURATION,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	if (rb_metadata_get (md,
			     RB_METADATA_FIELD_BITRATE,
			     &val)) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_BITRATE, &val);
		g_value_unset (&val);
	}

	rhythmdb_commit (db);

	g_object_unref (md);
	return TRUE;
}

static void
rb_podcast_manager_db_entry_added_cb (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	RhythmDBEntryType type = rhythmdb_entry_get_entry_type (entry);

	if (type != RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		return;

        rb_podcast_manager_download_entry (pd, entry);
}

static void
download_info_free (RBPodcastManagerInfo *data)
{
	if (data->write_uri) {
		gnome_vfs_uri_unref (data->write_uri);
		data->write_uri = NULL;
	}

	if (data->read_uri) {
		gnome_vfs_uri_unref (data->read_uri);
		data->read_uri = NULL;
	}
	if (data->query_string) {
		g_free (data->query_string);
		data->query_string = NULL;
	}

	if (data->entry) {
		rhythmdb_entry_unref (data->entry);
	}

	g_free (data);
}

static void
start_job (RBPodcastManagerInfo *data)
{
	GList *source_uri_list;
	GList *target_uri_list;

	GDK_THREADS_ENTER ();
	g_signal_emit (data->pd, rb_podcast_manager_signals[START_DOWNLOAD],
		       0, data->entry);
	GDK_THREADS_LEAVE ();

	source_uri_list = g_list_prepend (NULL, data->read_uri);
	target_uri_list = g_list_prepend (NULL, data->write_uri);

	gnome_vfs_async_xfer (&data->read_handle,
			      source_uri_list,
			      target_uri_list,
			      GNOME_VFS_XFER_DEFAULT ,
			      GNOME_VFS_XFER_ERROR_MODE_ABORT,
			      GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE,
			      GNOME_VFS_PRIORITY_DEFAULT,
			      (GnomeVFSAsyncXferProgressCallback) download_progress_update_cb,
			      data,
			      (GnomeVFSXferProgressCallback) download_progress_cb,
			      data);

	g_list_free (source_uri_list);
	g_list_free (target_uri_list);
}

static gboolean
end_job	(RBPodcastManagerInfo *data)
{
	RBPodcastManager *pd = data->pd;

	g_assert (rb_is_main_thread ());

	rb_debug ("cleaning up download of %s",
		  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION));

	data->pd->priv->download_list = g_list_remove (data->pd->priv->download_list, data);

	GDK_THREADS_ENTER ();
	g_signal_emit (data->pd, rb_podcast_manager_signals[FINISH_DOWNLOAD],
		       0, data->entry);
	GDK_THREADS_LEAVE ();

	g_assert (pd->priv->active_download == data);
	pd->priv->active_download = NULL;

	download_info_free (data);

	if (pd->priv->next_file_id == 0) {
		pd->priv->next_file_id =
			g_idle_add ((GSourceFunc) rb_podcast_manager_next_file, pd);
	}
	return FALSE;
}

static void
cancel_job (RBPodcastManagerInfo *data)
{
	g_assert (rb_is_main_thread ());
	rb_debug ("cancelling download of %s",
		  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION));

	/* is this the active download? */
	if (data == data->pd->priv->active_download) {
		data->cancelled = TRUE;
		if (data->read_handle != NULL) {
			gnome_vfs_async_cancel (data->read_handle);
			data->read_handle = NULL;
		}

		/* download data will be cleaned up after next progress callback */
	} else {
		/* destroy download data */
		data->pd->priv->download_list = g_list_remove (data->pd->priv->download_list, data);
		download_info_free (data);
	}
}

static guint
download_progress_cb (GnomeVFSXferProgressInfo *info, gpointer cb_data)
{
	GValue val = {0, };
	RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) cb_data;

	if (data == NULL) {
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	}

	if (info->status != GNOME_VFS_XFER_PROGRESS_STATUS_OK ||
	    ((info->phase == GNOME_VFS_XFER_PHASE_COMPLETED) && (info->file_size == 0))) {

		rb_debug ("error downloading %s",
			  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION));

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_ERROR);
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		if (info->vfs_status != GNOME_VFS_OK) {
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, gnome_vfs_result_to_string (info->vfs_status));
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
			g_value_unset (&val);
		}

		rhythmdb_commit (data->pd->priv->db);
		g_idle_add ((GSourceFunc)end_job, data);
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	}

	if (rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_MOUNTPOINT) == NULL) {
		char *uri = gnome_vfs_uri_to_string (data->write_uri, GNOME_VFS_URI_HIDE_NONE);
		char *canon_uri = rb_canonicalise_uri (uri);
		g_free (uri);

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, canon_uri);
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
		g_value_unset (&val);

		rhythmdb_commit (data->pd->priv->db);
		g_free (canon_uri);
	}

	if (info->phase == GNOME_VFS_XFER_PHASE_COMPLETED) {
		if (data->cancelled == FALSE) {
			char *uri;
			char *canon_uri;

			uri = gnome_vfs_uri_to_string (data->write_uri,
						       GNOME_VFS_URI_HIDE_NONE);
			canon_uri = rb_canonicalise_uri (uri);
			g_free (uri);
			rb_debug ("download of %s completed", canon_uri);

			g_value_init (&val, G_TYPE_UINT64);
			g_value_set_uint64 (&val, info->file_size);
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_FILE_SIZE, &val);
			g_value_unset (&val);

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_COMPLETE);
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);

			rb_podcast_manager_save_metadata (data->pd->priv->db,
							  data->entry,
							  canon_uri);
			g_free (canon_uri);
		}
		g_idle_add ((GSourceFunc)end_job, data);
		return GNOME_VFS_XFER_ERROR_ACTION_SKIP;
	}

	return 1;
}

static guint
download_progress_update_cb (GnomeVFSAsyncHandle *handle, GnomeVFSXferProgressInfo *info, gpointer cb_data)
{
	RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) cb_data;

	if (data == NULL) {
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	}

	if ((info->phase == GNOME_VFS_XFER_PHASE_COPYING) &&
	    (data->entry != NULL)) {
		guint local_progress = 0;

		if (info->file_size > 0)
			local_progress = (gint) 100 * info->total_bytes_copied / info->file_size;

		if (local_progress != data->progress) {
			GValue val = {0,};

			GDK_THREADS_ENTER ();

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, local_progress);
			rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);

			g_signal_emit (data->pd, rb_podcast_manager_signals[STATUS_CHANGED],
				       0, data->entry, local_progress);

			GDK_THREADS_LEAVE ();

			data->progress = local_progress;
		}
	}

	return GNOME_VFS_XFER_ERROR_ACTION_SKIP;
}

void
rb_podcast_manager_unsubscribe_feed (RhythmDB *db, const char *url)
{
	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (db, url);
	if (entry) {
		GValue val = {0, };
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);
	}

}

gboolean
rb_podcast_manager_remove_feed (RBPodcastManager *pd, const char *url, gboolean remove_files)
{
	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (pd->priv->db, url);

	if (entry) {
		rb_debug ("Removing podcast feed: %s remove_files: %d", url, remove_files);

		rb_podcast_manager_set_remove_files (pd, remove_files);
		rhythmdb_entry_delete (pd->priv->db, entry);
		rhythmdb_commit (pd->priv->db);
		return TRUE;
	}

	return FALSE;
}

static void
rb_podcast_manager_db_entry_deleted_cb (RBPodcastManager *pd,
					RhythmDBEntry *entry)
{
	RhythmDBEntryType type = rhythmdb_entry_get_entry_type (entry);

	if ((type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) && (pd->priv->remove_files == TRUE)) {
		const char *file_name;
		const char *dir_name;
		const char *conf_dir_name;
		GnomeVFSResult result;

		rb_debug ("Handling entry deleted");

		/* make sure we're not downloading it */
		rb_podcast_manager_cancel_download (pd, entry);

		file_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
		if (file_name == NULL) {
			/* episode has not been downloaded */
			rb_debug ("Episode not downloaded, skipping.");
			return;
		}

		result = gnome_vfs_unlink (file_name);
		if (result != GNOME_VFS_OK) {
			rb_debug ("Removing episode failed: %s", gnome_vfs_result_to_string (result));
			return;
		}

		/* remove dir */
		rb_debug ("removing dir");
		conf_dir_name = eel_gconf_get_string (CONF_STATE_PODCAST_DOWNLOAD_DIR);

		dir_name = g_build_filename (conf_dir_name,
					     rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
					     NULL);
		gnome_vfs_remove_directory (dir_name);

	} else if (type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
		GtkTreeModel *query_model;
		GtkTreeIter iter;

		query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (pd->priv->db));
		rhythmdb_do_full_query (pd->priv->db,
					RHYTHMDB_QUERY_RESULTS (query_model),
                	                RHYTHMDB_QUERY_PROP_EQUALS,
                        	        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
                	                RHYTHMDB_QUERY_PROP_LIKE,
					RHYTHMDB_PROP_SUBTITLE, rhythmdb_entry_get_string (entry,  RHYTHMDB_PROP_LOCATION),
                                	RHYTHMDB_QUERY_END);

		if (gtk_tree_model_get_iter_first (query_model, &iter)) {
			gboolean has_next;
			do {
				RhythmDBEntry *entry;

				gtk_tree_model_get (query_model, &iter, 0, &entry, -1);
				has_next = gtk_tree_model_iter_next (query_model, &iter);

				/* make sure we're not downloading it */
				rb_podcast_manager_cancel_download (pd, entry);

				rhythmdb_entry_delete (pd->priv->db, entry);
				rhythmdb_entry_unref (entry);

			} while (has_next);

			rhythmdb_commit (pd->priv->db);
		}

		g_object_unref (query_model);
	}
}

void
rb_podcast_manager_cancel_download (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	GList *lst;
	g_assert (rb_is_main_thread ());

	for (lst = pd->priv->download_list; lst != NULL; lst = lst->next) {
		RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) lst->data;
		if (data->entry == entry) {
			cancel_job (data);
			return;
		}
	}
}

static void
rb_podcast_manager_update_synctime (RBPodcastManager *pd)
{
	gint value;
	gint index = eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL);

	switch (index)
	{
	case UPDATE_EVERY_HOUR:
		value = time (NULL) + 3600;
		break;
	case UPDATE_EVERY_DAY:
		value = time (NULL) + (3600 * 24);
		break;
	case UPDATE_EVERY_WEEK:
		value = time (NULL) + (3600 * 24 * 7);
		break;
	case UPDATE_MANUALLY:
		value = 0;
		break;
	default:
		g_warning ("unknown download-inteval");
		value = 0;
	};

	eel_gconf_set_integer (CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME, value);
	eel_gconf_suggest_sync ();
	pd->priv->next_time = value;
	rb_podcast_manager_start_sync (pd);
}

static void
rb_podcast_manager_config_changed (GConfClient* client,
				   guint cnxn_id,
				   GConfEntry *entry,
				   gpointer user_data)
{
	rb_podcast_manager_update_synctime (RB_PODCAST_MANAGER (user_data));
}

/* this bit really wants to die */

void
rb_podcast_manager_set_remove_files (RBPodcastManager *pd, gboolean flag)
{
	pd->priv->remove_files = flag;

}

gboolean
rb_podcast_manager_get_remove_files (RBPodcastManager *pd)
{
	return pd->priv->remove_files;
}

static void
rb_podcast_manager_insert_feed (RBPodcastManager *pd, RBPodcastChannel *data)
{
	GValue description_val = { 0, };
	GValue title_val = { 0, };
	GValue subtitle_val = { 0, };
	GValue summary_val = { 0, };
	GValue lang_val = { 0, };
	GValue copyright_val = { 0, };
	GValue image_val = { 0, };
	GValue author_val = { 0, };
	GValue status_val = { 0, };
	GValue last_post_val = { 0, };
	GValue last_update_val = { 0, };
	gulong last_post = 0;
	gulong new_last_post;
	GList *download_entries = NULL;
	gboolean new_feed, updated, download_last;
	RhythmDB *db = pd->priv->db;

	RhythmDBEntry *entry;

	GList *lst_songs;

	if (data->title	== NULL) {
		g_list_free (data->posts);
		g_free (data);
		return;
	}

	new_feed = TRUE;

	/* processing podcast head */
	entry = rhythmdb_entry_lookup_by_location (db, (gchar *)data->url);
	if (entry) {
		if (rhythmdb_entry_get_entry_type (entry) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
			return;

		rb_debug ("Podcast feed entry for %s found", data->url);
		g_value_init (&status_val, G_TYPE_ULONG);
		g_value_set_ulong (&status_val, 1);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &status_val);
		g_value_unset (&status_val);
		last_post = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
		new_feed = FALSE;
	} else {
		rb_debug ("Adding podcast feed: %s", data->url);
		entry = rhythmdb_entry_new (db,
					    RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				    	    (gchar *) data->url);
		if (entry == NULL)
			return;

		g_value_init (&status_val, G_TYPE_ULONG);
		g_value_set_ulong (&status_val, 1);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &status_val);
		g_value_unset (&status_val);
	}

	/* if the feed does not contain a title, use the URL instead */
	g_value_init (&title_val, G_TYPE_STRING);
	if (data->title == NULL || strlen ((gchar *)data->title) == 0) {
		g_value_set_string (&title_val, (gchar *) data->url);
	} else {
		g_value_set_string (&title_val, (gchar *) data->title);
	}
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &title_val);
	g_value_unset (&title_val);

	g_value_init (&author_val, G_TYPE_STRING);
	if (data->author)
		g_value_set_string (&author_val, (gchar *) data->author);
	else
		g_value_set_static_string (&author_val, _("Unknown"));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &author_val);
	g_value_unset (&author_val);

	if (data->subtitle) {
		g_value_init (&subtitle_val, G_TYPE_STRING);
		g_value_set_string (&subtitle_val, (gchar *) data->subtitle);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_SUBTITLE, &subtitle_val);
		g_value_unset (&subtitle_val);
	}

	if (data->description) {
		g_value_init (&description_val, G_TYPE_STRING);
		g_value_set_string (&description_val, (gchar *) data->description);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DESCRIPTION, &description_val);
		g_value_unset (&description_val);
	}

	if (data->summary) {
		g_value_init (&summary_val, G_TYPE_STRING);
		g_value_set_string (&summary_val, (gchar *) data->summary);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_SUMMARY, &summary_val);
		g_value_unset (&summary_val);
	}

	if (data->lang) {
		g_value_init (&lang_val, G_TYPE_STRING);
		g_value_set_string (&lang_val, (gchar *) data->lang);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LANG, &lang_val);
		g_value_unset (&lang_val);
	}

	if (data->copyright) {
		g_value_init (&copyright_val, G_TYPE_STRING);
		g_value_set_string (&copyright_val, (gchar *) data->copyright);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_COPYRIGHT, &copyright_val);
		g_value_unset (&copyright_val);
	}

	if (data->img) {
		g_value_init (&image_val, G_TYPE_STRING);
		g_value_set_string (&image_val, (gchar *) data->img);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_IMAGE, &image_val);
		g_value_unset (&image_val);
	}

	/* insert episodes */
	new_last_post = last_post;

	updated = FALSE;
	download_last = (eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL) != UPDATE_MANUALLY);
	for (lst_songs = data->posts; lst_songs != NULL; lst_songs = g_list_next (lst_songs)) {
		RBPodcastItem *item = (RBPodcastItem *) lst_songs->data;
		RhythmDBEntry *post_entry;

		if (item->pub_date > last_post || item->pub_date == 0) {
			updated = TRUE;

			post_entry =
				rb_podcast_manager_add_post (db,
							     (gchar *) data->title,
							     (gchar *) item->title,
							     (gchar *) data->url,
							     (gchar *) (item->author ? item->author : data->author),
							     (gchar *) item->url,
							     (gchar *) item->description,
							     (gulong) (item->pub_date > 0 ? item->pub_date : data->pub_date),
							     (gulong) item->duration,
							     item->filesize);
			if (post_entry && item->pub_date >= new_last_post) {
				if (item->pub_date > new_last_post) {
					g_list_free (download_entries);
					download_entries = NULL;
				}
				download_entries = g_list_prepend (download_entries, post_entry);
				new_last_post = item->pub_date;
			}
		}
	}

	if (download_last) {
		GValue status = {0,};
		GList *t;

		g_value_init (&status, G_TYPE_ULONG);
		g_value_set_ulong (&status, RHYTHMDB_PODCAST_STATUS_WAITING);
		for (t = download_entries; t != NULL; t = g_list_next (t)) {
			rhythmdb_entry_set (db,
					    (RhythmDBEntry*) t->data,
					    RHYTHMDB_PROP_STATUS,
					    &status);
		}
		g_value_unset (&status);
	}
	g_list_free (download_entries);

	if (updated)
		g_signal_emit (pd, rb_podcast_manager_signals[FEED_UPDATES_AVAILABLE],
			       0, entry);

	if (data->pub_date > new_last_post)
		new_last_post = data->pub_date;

	g_value_init (&last_post_val, G_TYPE_ULONG);
	g_value_set_ulong (&last_post_val, new_last_post);

	if (new_feed)
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_POST_TIME, &last_post_val);
	else
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_POST_TIME, &last_post_val);
	g_value_unset (&last_post_val);

	g_value_init (&last_update_val, G_TYPE_ULONG);
	g_value_set_ulong (&last_update_val, time(NULL));

	if (new_feed)
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_SEEN, &last_update_val);
	else
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_SEEN, &last_update_val);
	g_value_unset (&last_update_val);

	rhythmdb_commit (db);
}

void
rb_podcast_manager_shutdown (RBPodcastManager *pd)
{
	GList *lst, *l;

	g_assert (rb_is_main_thread ());

	lst = g_list_reverse (pd->priv->download_list);
	for (l = lst; l != NULL; l = l->next) {
		RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) l->data;
		cancel_job (data);
	}
	g_list_free (lst);

	pd->priv->shutdown = TRUE;
}

gchar *
rb_podcast_manager_get_podcast_dir (RBPodcastManager *pd)
{
	gchar *conf_dir_name = eel_gconf_get_string (CONF_STATE_PODCAST_DOWNLOAD_DIR);

	if (conf_dir_name == NULL || (strcmp (conf_dir_name, "") == 0)) {
		conf_dir_name = g_build_filename (g_get_home_dir (),
						  "Podcasts",
						  NULL);
		eel_gconf_set_string (CONF_STATE_PODCAST_DOWNLOAD_DIR, conf_dir_name);
	}

	return conf_dir_name;
}

