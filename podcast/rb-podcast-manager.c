/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <libgnomevfs/gnome-vfs-uri.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>
#define __USE_XOPEN
#include <time.h>

#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rb-podcast-manager.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-podcast-marshal.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-podcast-parse.h"
#include "rb-dialog.h"
#include "rb-metadata.h"

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
	FEED_UPDATES_AVALIABLE,
	LAST_SIGNAL
};

typedef enum 
{
	EVENT_INSERT_FEED,
	EVENT_ERROR_FEED,
	EVENT_INSERT_POST
}RBPodcastEventType;



struct RBPodcastManagerPrivate
{
	RhythmDB *db;
	GList *download_list;
	guint next_time;
	guint source_sync;
	guint update_interval_notify_id;
	GMutex *mutex_job;

	gboolean remove_files;

	//manager loop
	guint event_count;
	GList *event_list;
	GMutex *event_mutex;
	GMutex *event_loop_mutex;
};

//used on event loop
typedef struct
{
	RBPodcastEventType 	type;
	RBPodcastChannel 	*channel;
}RBPodcastManagerEvent;


//used on donwload thread
typedef struct 
{
	RBPodcastManager *pd;
	RhythmDBEntry *entry;
	GnomeVFSAsyncHandle *read_handle;
	GnomeVFSURI *write_uri;
	GnomeVFSURI *read_uri;
	GMutex *mutex_working;
	
	guint total_size;
	guint progress;
	gboolean canceled;

}RBPodcastManagerInfo;

//used on subscribe thread
typedef struct
{
	RBPodcastManager *pd;
	char* url;
}RBPodcastThreadInfo;


static guint rb_podcast_manager_signals[LAST_SIGNAL] = { 0 };
static GStaticMutex entry_add_mutex = G_STATIC_MUTEX_INIT;

/* functions */
static void rb_podcast_manager_class_init 		(RBPodcastManagerClass *klass);
static void rb_podcast_manager_init 			(RBPodcastManager *dp);
static GObject *rb_podcast_manager_constructor 		(GType type, guint n_construct_properties,
			   				 GObjectConstructParam *construct_properties);
static void rb_podcast_manager_finalize 		(GObject *object);
static void rb_podcast_manager_set_property 		(GObject *object,
                                   			 guint prop_id,
		                                	 const GValue *value,
		                                	 GParamSpec *pspec);
static void rb_podcast_manager_get_property 		(GObject *object,
							 guint prop_id,
		                                	 GValue *value,
                		                	 GParamSpec *pspec);
static void rb_podcast_manager_copy_post 		(RBPodcastManager *pd);
static int rb_podcast_manager_mkdir_with_parents 	(const gchar *pathname,
					  		 int mode);
static gboolean rb_podcast_manager_sync_head_cb 	(gpointer data);
static gboolean rb_podcast_manager_head_query_cb 	(GtkTreeModel *query_model,
						   	 GtkTreePath *path, 
							 GtkTreeIter *iter,
						   	 RBPodcastManager *data);
static gboolean rb_podcast_manager_entry_remove_cb	(GtkTreeModel *query_model,
						   	 GtkTreePath *path, 
							 GtkTreeIter *iter,
						   	 gpointer data);
static gboolean rb_podcast_manager_save_metadata	(RhythmDB *db, 
						  	 RhythmDBEntry *entry, 
						  	 const char* uri);
static void rb_podcast_manager_db_entry_added_cb 	(RBPodcastManager *pd, 
							 RhythmDBEntry *entry);
static void rb_podcast_manager_db_entry_deleted_cb 	(RBPodcastManager *pd, 
							 RhythmDBEntry *entry);
static void rb_podcast_manager_next_file 		(RBPodcastManager * pd);
static gboolean rb_podcast_manager_next_file_idle 	(RBPodcastManager *pd);
static void rb_podcast_manager_insert_feed 		(RBPodcastManager *pd, RBPodcastChannel *data);
static void rb_podcast_manager_abort_subscribe 		(RBPodcastManager *pd);

//event loop
static gboolean rb_podcast_manager_event_loop 		(gpointer data);
static gpointer rb_podcast_manager_thread_parse_feed 	(gpointer data);
	

/* async read file functions */
static guint download_progress_cb			(GnomeVFSXferProgressInfo *info,
							 gpointer data);
static guint download_progress_update_cb		(GnomeVFSAsyncHandle *handle,
							 GnomeVFSXferProgressInfo *info,
							 gpointer data);

/* internal functions */
static void download_info_free				(RBPodcastManagerInfo *data);
static RBPodcastManagerInfo *download_info_new		(void);
static void start_job					(RBPodcastManagerInfo *data);
static void end_job					(RBPodcastManagerInfo *data);
static void cancel_job					(RBPodcastManagerInfo *pd);
static void write_job_data				(RBPodcastManagerInfo *data);
static void  rb_podcast_manager_update_synctime		(RBPodcastManager *pd);
static void  rb_podcast_manager_config_changed		(GConfClient* client,
                                        		 guint cnxn_id,
				                         GConfEntry *entry,
							 gpointer user_data);

G_DEFINE_TYPE (RBPodcastManager, rb_podcast_manager, G_TYPE_OBJECT)


static void
rb_podcast_manager_class_init (RBPodcastManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->constructor = rb_podcast_manager_constructor;	
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
				rb_podcast_marshal_VOID__POINTER_ULONG,
				G_TYPE_NONE,
				2,
				G_TYPE_POINTER,
				G_TYPE_ULONG);

	rb_podcast_manager_signals[START_DOWNLOAD] = 
	       g_signal_new ("start_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, start_download),
				NULL, NULL,
				g_cclosure_marshal_VOID__POINTER,
				G_TYPE_NONE,
				1,
				G_TYPE_POINTER);

	rb_podcast_manager_signals[FINISH_DOWNLOAD] = 
	       g_signal_new ("finish_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, finish_download),
				NULL, NULL,
				g_cclosure_marshal_VOID__POINTER,
				G_TYPE_NONE,
				1,
				G_TYPE_POINTER);

	rb_podcast_manager_signals[FEED_UPDATES_AVALIABLE] = 
	       g_signal_new ("feed_updates_avaliable",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, feed_updates_avaliable),
				NULL, NULL,
				g_cclosure_marshal_VOID__POINTER,
				G_TYPE_NONE,
				1,
				G_TYPE_POINTER);

	rb_podcast_manager_signals[PROCESS_ERROR] = 
	       g_signal_new ("process_error",
		       		G_OBJECT_CLASS_TYPE (object_class),
				GTK_RUN_LAST,
				G_STRUCT_OFFSET (RBPodcastManagerClass, process_error),
				NULL, NULL,
				g_cclosure_marshal_VOID__STRING,
				G_TYPE_NONE,
				1,
				G_TYPE_POINTER);
					
}	

static void
rb_podcast_manager_init (RBPodcastManager *pd)
{
	pd->priv = g_new0 (RBPodcastManagerPrivate, 1);
	pd->priv->source_sync = 0;
	pd->priv->mutex_job = g_mutex_new();
	pd->priv->db = NULL;
	pd->priv->event_list = NULL;
	pd->priv->event_mutex  = g_mutex_new ();
	pd->priv->event_loop_mutex  = g_mutex_new ();
	pd->priv->event_count = 0;
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
rb_podcast_manager_finalize (GObject *object)
{
	RBPodcastManager *pd;
	g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PODCAST_MANAGER (object));

	pd = RB_PODCAST_MANAGER(object);
	
	g_return_if_fail (pd->priv != NULL);


	eel_gconf_monitor_remove (CONF_STATE_PODCAST_PREFIX);
	
	if (pd->priv->source_sync) {
		g_source_remove (pd->priv->source_sync);
		pd->priv->source_sync = 0;
	}


	eel_gconf_notification_remove (pd->priv->update_interval_notify_id);	
	

	if (pd->priv->download_list)
		g_list_free (pd->priv->download_list);


	g_mutex_free (pd->priv->mutex_job);	


	g_mutex_free (pd->priv->event_loop_mutex);

	g_list_free (pd->priv->event_list);

	g_mutex_free (pd->priv->event_mutex);
	
	//g_static_mutex_free (&entry_add_mutex);
	
	g_free (pd->priv);
	
	G_OBJECT_CLASS (rb_podcast_manager_parent_class)->finalize (object);
	rb_debug ("Podcast Manager END");
}

static void 
rb_podcast_manager_set_property (GObject *object,
                           		guint prop_id,
	                                const GValue *value,
	                                GParamSpec *pspec)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);

	switch (prop_id)
	{
	case PROP_DB:
		if (pd->priv->db) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (pd->priv->db),
							      G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
							      pd);

			g_signal_handlers_disconnect_by_func (G_OBJECT (pd->priv->db),
							      G_CALLBACK (rb_podcast_manager_db_entry_deleted_cb),
							      pd);
			
		}
		
		pd->priv->db = g_value_get_object (value);

	        g_signal_connect_object (G_OBJECT (pd->priv->db),
	                                 "entry-added",
	                                 G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
	                                 pd, G_CONNECT_SWAPPED);
		
	        g_signal_connect_object (G_OBJECT (pd->priv->db),
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

	switch (prop_id)
	{
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
rb_podcast_manager_download_entry (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	gulong status;
	if (entry == NULL)
		return;

	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	if ((status < 100) || (status == 102)) {
		RBPodcastManagerInfo *data;
		if (status < 100) {
			GValue status_val = { 0, };
			g_value_init (&status_val, G_TYPE_ULONG);
			g_value_set_ulong (&status_val, 102);
			rhythmdb_entry_set_nonotify (pd->priv->db, entry, RHYTHMDB_PROP_STATUS, &status_val);
			g_value_unset (&status_val);
		}
		rb_debug ("Try insert entry for download.");
		data  = download_info_new();
		data->pd = pd;	
		data->entry = entry;
        	g_static_mutex_lock (&entry_add_mutex);
		pd->priv->download_list =  g_list_append (pd->priv->download_list, data);
		g_static_mutex_unlock (&entry_add_mutex);
		gtk_idle_add ((GtkFunction) rb_podcast_manager_next_file_idle , pd);
	}
}


void
rb_podcast_manager_start_sync(RBPodcastManager *pd)
{
	gint next_time;
	if (pd->priv->next_time > 0) {
		next_time = pd->priv->next_time;
	} else {
		next_time = eel_gconf_get_integer(CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME);
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
		pd->priv->source_sync = g_timeout_add (next_time * 1000, (GSourceFunc ) rb_podcast_manager_sync_head_cb, pd);
	}

}

static gboolean
rb_podcast_manager_sync_head_cb (gpointer data)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (data);
	rb_podcast_manager_update_feeds (pd);
	pd->priv->source_sync = 0;
	pd->priv->next_time = 0;
	rb_podcast_manager_update_synctime (RB_PODCAST_MANAGER (data));
	return FALSE;
}

void
rb_podcast_manager_update_feeds (RBPodcastManager *pd)
{
	GtkTreeModel* query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(pd->priv->db));	

	rhythmdb_do_full_query (pd->priv->db, query_model,
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
 	   			  GtkTreePath *path, GtkTreeIter *iter,
				  RBPodcastManager *data)
{
        const char* uri;
        RhythmDBEntry* entry;
	guint status;

        gtk_tree_model_get (query_model, iter, 0, &entry, -1);
        uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	if (status == 1) {
		if (rb_podcast_manager_subscribe_feed (data, uri) == TRUE) {
			GDK_THREADS_ENTER ();
			g_signal_emit (data, rb_podcast_manager_signals[FEED_UPDATES_AVALIABLE],
				       0, entry);
			GDK_THREADS_LEAVE ();

		}
	}
	
        return FALSE;
}

static void
rb_podcast_manager_next_file (RBPodcastManager * pd)
{

	rb_debug ("try lock file_process mutex");
	if (g_mutex_trylock (pd->priv->mutex_job) == TRUE) {
		gint size;
		
		g_static_mutex_lock (&entry_add_mutex);
		size = g_list_length (pd->priv->download_list);
		g_static_mutex_unlock (&entry_add_mutex);
			
		if (size > 0)
			rb_podcast_manager_copy_post (pd);
		else
			g_mutex_unlock (pd->priv->mutex_job);
	}
	else
		rb_debug ("not start");
}

static void 
rb_podcast_manager_copy_post (RBPodcastManager *pd)
{
	GnomeVFSURI *remote_uri = NULL;
	GnomeVFSURI *local_uri = NULL;
	
	GValue location_val = { 0, };
	
	const char *short_name;
	const char *local_file_name;
	const char *dir_name;
	const char *location; 
	const char *album_name; 
	const char *conf_dir_name;
	
	RBPodcastManagerInfo *data = NULL; 
	
	RhythmDBEntry *entry;

	rb_debug ("Stating copy file");
	g_value_init (&location_val, G_TYPE_STRING);
	conf_dir_name = eel_gconf_get_string (CONF_STATE_PODCAST_DOWNLOAD_DIR);

	if (conf_dir_name == NULL) {
		conf_dir_name = g_build_filename (g_get_home_dir (),
						  "Podcasts",
						  NULL);
		eel_gconf_set_string (CONF_STATE_PODCAST_DOWNLOAD_DIR, conf_dir_name);
	}

		
	//get first element of list
	g_static_mutex_lock (&entry_add_mutex);

	data = (RBPodcastManagerInfo *) g_list_first(pd->priv->download_list)->data;
	if (data == NULL)
		return;
			
	g_static_mutex_unlock (&entry_add_mutex);

	
	entry = data->entry;
			
	g_assert (entry != NULL);
		
	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	album_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);	
	
	rb_debug ("processing %s", location);
	
	remote_uri = gnome_vfs_uri_new (location);
	if (!remote_uri) {
		rb_debug ("Error downloading podcast: could not create remote uri");
		goto next_step;
	}
		
	if (gnome_vfs_uri_is_local (remote_uri)) {	
		rb_debug ("Error downloading podcast: uri is local");
		goto next_step;
	}


	short_name = gnome_vfs_uri_extract_short_name (remote_uri);
		
	
	

	dir_name = g_build_filename (conf_dir_name,
				     album_name,
				     NULL);


	 if (!g_file_test (dir_name, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		if (rb_podcast_manager_mkdir_with_parents (dir_name, 0750) != 0) {
			rb_debug ("Error downloading podcast: could not create local dirs");
			goto next_step;
		}
	 }
		 		 
	local_file_name = g_build_filename (dir_name,
	 				    short_name,
					   NULL);	

	rb_debug ("creating file %s\n", local_file_name);

	local_uri = gnome_vfs_uri_new (local_file_name);
	if (!local_uri) {
		rb_debug ("Error downloading podcast: could not create local uri");
		goto next_step;
	}

	if (g_file_test (local_file_name, G_FILE_TEST_EXISTS)) {
		guint64 remote_size;
		GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
		GnomeVFSResult result;

		result = gnome_vfs_get_file_info_uri (remote_uri, info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
		if (result != GNOME_VFS_OK) {
			rb_debug ("unable to retrieve info on remote of podcast");
			goto next_step;
		} else { 
			remote_size = info->size;
		}

		result = gnome_vfs_get_file_info (local_file_name, info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
		if (result != GNOME_VFS_OK) {
			rb_debug ("unable to retrieve info on local copy of podcast");
			goto next_step;
		} else if (remote_size == info->size) {
			GValue val = {0,};
			char *uri = gnome_vfs_uri_to_string (local_uri, GNOME_VFS_URI_HIDE_NONE);
		
			rb_debug ("podcast %s already downloaded", location);

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, 100);
			rhythmdb_entry_set (pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);

			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, uri);
			rhythmdb_entry_set (pd->priv->db, data->entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
			g_value_unset (&val);

			rb_podcast_manager_save_metadata (pd->priv->db, data->entry, uri);
			rhythmdb_commit (pd->priv->db);
				
			goto next_step;
		} else if (remote_size > info->size) {
			/* TODO: suport resume file */
		} else {
			/* the local file is larger. replace it */
		}
		
		gnome_vfs_file_info_unref (info);
	}

	
	
	data->read_uri = remote_uri;
	data->write_uri = local_uri;
	
	start_job (data);
	return;
	
next_step:
	
	if (remote_uri)
		gnome_vfs_uri_unref (remote_uri);

	if (local_uri)
		gnome_vfs_uri_unref (local_uri);

	g_static_mutex_lock (&entry_add_mutex);
	pd->priv->download_list = g_list_remove (pd->priv->download_list, (gconstpointer ) data);
	g_static_mutex_unlock (&entry_add_mutex);

	download_info_free (data);
	data = NULL;

	g_mutex_unlock (pd->priv->mutex_job);
	gtk_idle_add ((GtkFunction) rb_podcast_manager_next_file_idle , pd);	
}


static int
rb_podcast_manager_mkdir_with_parents (const gchar *pathname,
		   			int          mode)
{
  gchar *fn, *p;

  if (pathname == NULL || *pathname == '\0')
    {
      return -1;
    }

  fn = g_strdup (pathname);

  if (g_path_is_absolute (fn))
    p = (gchar *) g_path_skip_root (fn);
  else
    p = fn;

  do
    {
      while (*p && !G_IS_DIR_SEPARATOR (*p))
	p++;
      
      if (!*p)
	p = NULL;
      else
	*p = '\0';
      
      if (!g_file_test (fn, G_FILE_TEST_EXISTS))
	{
	  if (g_mkdir (fn, mode) == -1)
	    {
	      g_free (fn);
	      return -1;
	    }
	}
      else if (!g_file_test (fn, G_FILE_TEST_IS_DIR))
	{
	  g_free (fn);
	  return -1;
	}
      if (p)
	{
	  *p++ = G_DIR_SEPARATOR;
	  while (*p && G_IS_DIR_SEPARATOR (*p))
	    p++;
	}
    }
  while (p);

  g_free (fn);

  return 0;
}

gboolean
rb_podcast_manager_subscribe_feed (RBPodcastManager *pd, const char* url)
{
	RBPodcastThreadInfo *info;
	gchar *valid_url = gnome_vfs_make_uri_from_input (url);

	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (pd->priv->db, url);
	if (entry) {
		if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			/* added as something else, probably iradio */
			rb_error_dialog (NULL, _("URL already added"),
					 _("The URL \"%s\" has already been added as a radio station. "
					 "If this is a podcast feed, please remove the radio station."), url);
			return FALSE;
		}
	}

	info = g_new0 (RBPodcastThreadInfo, 1);
	info->pd = pd;
	info->url = valid_url;


	g_mutex_lock (pd->priv->event_mutex);
	pd->priv->event_count++;
	if (pd->priv->event_count == 1) {
		g_mutex_lock (pd->priv->event_loop_mutex);
		g_idle_add ((GSourceFunc ) rb_podcast_manager_event_loop, pd);
	}
	g_mutex_unlock (pd->priv->event_mutex);

	
	g_thread_create (rb_podcast_manager_thread_parse_feed,
			 info, TRUE, NULL);

	return TRUE;
}

static gpointer 
rb_podcast_manager_thread_parse_feed (gpointer data)
{
	RBPodcastManagerEvent *event;
	RBPodcastThreadInfo *info = (RBPodcastThreadInfo *) data;
	RBPodcastChannel *feed = g_new0 (RBPodcastChannel, 1);

	rb_podcast_parse_load_feed (feed, info->url);	
	
	event = g_new0 (RBPodcastManagerEvent, 1);
	if (feed->title	== NULL) {
	        event->type = EVENT_ERROR_FEED;	
		event->channel = feed;
	} else {
		event->type = EVENT_INSERT_FEED;
		event->channel = feed;
	}


	info->pd->priv->event_list = g_list_append (info->pd->priv->event_list, event);
	
	g_free (info->url);
	g_free (info);
	return NULL;
}

gboolean
rb_podcast_manager_add_post (RhythmDB *db,
			      const char *name,
			      const char *title,
			      const char *subtitle,
			      const char *generator,
			      const char *uri,
			      const char *description,
			      gulong status,
			      gulong date,
			      gulong duration,
			      guint64 filesize)
{

	if (uri && name && title && date && g_utf8_validate(uri, -1, NULL)) {
		RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (db, uri);
		GValue val = {0,};
		GTimeVal time;

		if (entry)
			return FALSE;

		entry = rhythmdb_entry_new (db,
					    RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				    	    uri);

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, name);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_ALBUM, &val);
		
		g_value_reset (&val);
		g_value_set_string (&val, uri);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
	
		g_value_reset (&val);
		g_value_set_static_string (&val, _("Podcast"));
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_GENRE, &val);

		g_value_reset (&val);
		g_value_set_string (&val, title);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_TITLE, &val);

		g_value_reset (&val);
		if (subtitle)
			g_value_set_string (&val, subtitle);
		else
			g_value_set_static_string (&val, "");
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_SUBTITLE, &val);

		g_value_reset (&val);
		if (description)
			g_value_set_string (&val, description);
		else
			g_value_set_static_string (&val, "");
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_DESCRIPTION, &val);

		g_value_reset (&val);
		if (generator) 
			g_value_set_string (&val, generator);
		else
			g_value_set_static_string (&val, "");
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_ARTIST, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, status);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_STATUS, &val);

		g_value_reset (&val);
		g_value_set_ulong (&val, date);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_POST_TIME, &val);

		g_value_reset (&val);
		g_value_set_ulong (&val, duration);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_DURATION, &val);
	
		g_value_reset (&val);
		g_value_set_ulong (&val, 0);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);

		/* first seen */
		g_get_current_time (&time);
		g_value_reset (&val);
		g_value_set_ulong (&val, time.tv_sec);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_FIRST_SEEN, &val);
		g_value_unset (&val);
		
		/* initialize the rating */
		g_value_init (&val, G_TYPE_DOUBLE);
		g_value_set_double (&val, 2.5);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_RATING, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_UINT64);
		g_value_set_uint64 (&val, filesize);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_FILE_SIZE, &val);
		g_value_unset (&val);

		return TRUE;
	} else {
		return FALSE;
	}
}

static gboolean
rb_podcast_manager_save_metadata(RhythmDB *db, RhythmDBEntry *entry, const char* uri)
{
	RBMetaData *md = rb_metadata_new();
	GError *error = NULL;
	GValue val = { 0, };
	const char *mime;
	
	rb_debug("Loading podcast metadata");
        rb_metadata_load (md, uri, &error);	

	mime = rb_metadata_get_mime (md);
	if (mime) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, mime);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MIMETYPE, &val);
		g_value_unset (&val);
	}


	if (error != NULL) {
		g_object_unref (md);
		return FALSE;
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

	g_object_unref (md);
	return TRUE;
}

static void
rb_podcast_manager_db_entry_added_cb (RBPodcastManager *pd, RhythmDBEntry *entry)
{

	if (entry->type != RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		return;
	
        rb_podcast_manager_download_entry (pd, entry);
}

static void 
write_job_data (RBPodcastManagerInfo *data)
{

	GValue val = {0, };
	RhythmDB *db = data->pd->priv->db;

	rb_debug ("in the write_job");
	
	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, data->total_size);
	rhythmdb_entry_set (db, data->entry, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);
	
	
	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, 100);
	rhythmdb_entry_set (db, data->entry, RHYTHMDB_PROP_STATUS, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val,  
			    gnome_vfs_uri_to_string (data->write_uri, GNOME_VFS_URI_HIDE_NONE));
	rhythmdb_entry_set (db, data->entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
	g_value_unset (&val);
	
	rb_podcast_manager_save_metadata (db, data->entry, 
		       			   gnome_vfs_uri_to_string (data->write_uri, GNOME_VFS_URI_HIDE_NONE));

	rhythmdb_commit (db);
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

	
	g_mutex_free (data->mutex_working);

	g_free (data);
}

static RBPodcastManagerInfo*
download_info_new (void)
{
	RBPodcastManagerInfo *data = g_new0 (RBPodcastManagerInfo, 1);
	data->pd = NULL;
	data->entry = NULL;
	data->write_uri = NULL;
	data->read_uri = NULL;
	data->mutex_working = g_mutex_new ();
	data->total_size = 0;
	data->progress = 0;	
	data->canceled = FALSE;

	return data;
}

static void 
start_job (RBPodcastManagerInfo *data)
{
	
	GList *source_uri_list = NULL;
	GList *target_uri_list = NULL;

	rb_debug ("start job");

	GDK_THREADS_ENTER ();
	g_signal_emit (data->pd, rb_podcast_manager_signals[START_DOWNLOAD],
		       0, data->entry);
	GDK_THREADS_LEAVE ();

	source_uri_list = g_list_append (source_uri_list, data->read_uri);
	target_uri_list = g_list_append (target_uri_list, data->write_uri);

	g_mutex_lock (data->mutex_working);

	rb_debug ("start async copy");
	gnome_vfs_async_xfer ( &data->read_handle,
				source_uri_list,
				target_uri_list,
				GNOME_VFS_XFER_DEFAULT ,
				GNOME_VFS_XFER_ERROR_MODE_ABORT,
				GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE,
				GNOME_VFS_PRIORITY_DEFAULT,
				(GnomeVFSAsyncXferProgressCallback ) download_progress_update_cb,
				data,
				(GnomeVFSXferProgressCallback ) download_progress_cb,
				data);

}

void 
rb_podcast_manager_cancel_all	(RBPodcastManager *pd)
{
	guint i;
	guint lst_len;
	GList *lst = g_list_reverse (pd->priv->download_list);
	rb_debug ("cancel all job %d", g_list_length (lst));
	lst_len = g_list_length (lst);
	
	for (i=0; i < lst_len; i++) {
		RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) lst->data;
		lst = lst->next;
		cancel_job (data);
		rb_debug ("cancel next job");
	}
	
	if (lst_len > 0) {
		g_mutex_lock (pd->priv->mutex_job);
		g_mutex_unlock (pd->priv->mutex_job);
	}
}

static void 
end_job	(RBPodcastManagerInfo *data)
{
	RBPodcastManager *pd = data->pd;
	rb_debug ("end_job");
	
	g_static_mutex_lock (&entry_add_mutex);
	data->pd->priv->download_list = g_list_remove (data->pd->priv->download_list, (gconstpointer ) data);
	g_static_mutex_unlock (&entry_add_mutex);
	
	g_mutex_unlock (data->mutex_working);

	
	if (data->canceled != TRUE) {
		GDK_THREADS_ENTER ();

		g_signal_emit (data->pd, rb_podcast_manager_signals[FINISH_DOWNLOAD], 
			       0, data->entry);

		GDK_THREADS_LEAVE ();	
	}

	download_info_free (data);
	
	g_mutex_unlock (pd->priv->mutex_job);

	gtk_idle_add ((GtkFunction) rb_podcast_manager_next_file_idle , pd);
	
}

static gboolean
rb_podcast_manager_next_file_idle (RBPodcastManager *pd)
{
	rb_podcast_manager_next_file  (pd);

	return FALSE;
}

static void
cancel_job (RBPodcastManagerInfo *data)
{
	if (g_mutex_trylock (data->mutex_working) == FALSE) {
		rb_debug ("async cancel");
		data->canceled = TRUE;
	}
	else {
		rb_debug ("job cancel");
		
		g_static_mutex_lock (&entry_add_mutex);
		data->pd->priv->download_list = g_list_remove (data->pd->priv->download_list, (gconstpointer ) data);
		g_static_mutex_unlock (&entry_add_mutex);

		g_mutex_unlock (data->mutex_working);
		
		download_info_free (data);
		data = NULL;
	}
}

static guint 
download_progress_cb (GnomeVFSXferProgressInfo *info, gpointer cb_data)
{
	RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) cb_data;

	if (data == NULL) {
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	}

	if (info->status != GNOME_VFS_XFER_PROGRESS_STATUS_OK) {
		GValue val = {0, };
		rb_debug ("error on download");
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 101);
		GDK_THREADS_ENTER ();
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS,  &val);
		rhythmdb_commit (data->pd->priv->db);
		GDK_THREADS_LEAVE ();
		g_value_unset (&val);
		end_job (data);
		data = NULL;
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
	}
	
	if (info->phase  == GNOME_VFS_XFER_PHASE_COMPLETED) {
		if (data->canceled != TRUE)  {
			rb_debug ("download completed");
			data->total_size = info->file_size;
			write_job_data (data);
		}
		end_job (data);
		data = NULL;
		return GNOME_VFS_XFER_ERROR_ACTION_SKIP;
	}

	if (data->canceled == TRUE) {
		rb_debug ("job canceled");
		gnome_vfs_async_cancel (data->read_handle);
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
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
			data->entry->podcast->status = local_progress;
			GDK_THREADS_ENTER ();
			
			g_signal_emit (data->pd, rb_podcast_manager_signals[STATUS_CHANGED],
				       0, data->entry, local_progress);
			
			GDK_THREADS_LEAVE ();
			data->progress = local_progress;
		}
		
	}
	
	return GNOME_VFS_XFER_ERROR_ACTION_SKIP;
}

void
rb_podcast_manager_unsubscribe_feed (RhythmDB *db, const char* url)
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
rb_podcast_manager_remove_feed (RBPodcastManager *pd, const char* url, gboolean remove_files)
{
	RhythmDBEntry *entry = rhythmdb_entry_lookup_by_location (pd->priv->db, url);
	if (entry) {
		rb_podcast_manager_set_remove_files (pd, remove_files);
		rhythmdb_entry_delete (pd->priv->db, entry);
		rhythmdb_commit (pd->priv->db);
		return TRUE;
	}
	
	return FALSE;
}

static void 
rb_podcast_manager_db_entry_deleted_cb (RBPodcastManager *pd, RhythmDBEntry *entry)
{

	if ( (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) && (pd->priv->remove_files == TRUE) )
	{
		const gchar *file_name;
		const gchar *dir_name;
		const gchar *conf_dir_name;
		GnomeVFSURI *uri;

		file_name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
		
		uri = gnome_vfs_uri_new (file_name);
		
		if ((uri != NULL) && (gnome_vfs_uri_is_local (uri) == TRUE)) {
			gnome_vfs_unlink (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT));
		
			//remove dir
			rb_debug ("removing dir");
			conf_dir_name = eel_gconf_get_string (CONF_STATE_PODCAST_DOWNLOAD_DIR);
			
			dir_name = g_build_filename (conf_dir_name,
						     rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
						     NULL);
			gnome_vfs_remove_directory (dir_name);
		}
	}
	else if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
	{
		GList *list = g_list_alloc();
		GtkTreeModel* query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty(pd->priv->db));	

		rhythmdb_do_full_query (pd->priv->db, query_model,
                	                RHYTHMDB_QUERY_PROP_EQUALS,
                        	        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
                	                RHYTHMDB_QUERY_PROP_LIKE,
					RHYTHMDB_PROP_SUBTITLE, rhythmdb_entry_get_string (entry,  RHYTHMDB_PROP_LOCATION),
                                	RHYTHMDB_QUERY_END);
	
	 	gtk_tree_model_foreach (query_model,
			                (GtkTreeModelForeachFunc) rb_podcast_manager_entry_remove_cb,
                	                (gpointer) list);

		list = list->next;
		
		while (list != NULL) {
			RhythmDBEntry *entry = (RhythmDBEntry *) list->data;
			rhythmdb_entry_delete (pd->priv->db, entry);
			list = list->next;
		}
		
		g_list_free (list);
		rhythmdb_commit (pd->priv->db);
	
	}
}

static gboolean
rb_podcast_manager_entry_remove_cb (GtkTreeModel *query_model,
				    GtkTreePath *path, GtkTreeIter *iter,
				    gpointer data)
{
	RhythmDBEntry* entry;
	GList *list = (GList *) data;
	gtk_tree_model_get (query_model, iter, 0, &entry, -1);
	list = g_list_append (list, entry);
	return FALSE;
}


void
rb_podcast_manager_cancel_download (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	GList *lst = pd->priv->download_list;
	while (lst) {
		RBPodcastManagerInfo *data = (RBPodcastManagerInfo *) lst->data;
		if (data->entry == entry) {
			rb_debug ("Found job");
			break;
		}
		lst = lst->next;
	}
	if (lst) 
		cancel_job (lst->data);
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
		value = 0;
	};

	eel_gconf_set_integer (CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME, value);
	eel_gconf_suggest_sync ();
	pd->priv->next_time = value;
	rb_podcast_manager_start_sync (pd);
}

static void  rb_podcast_manager_config_changed (GConfClient* client,
                                        	guint cnxn_id,
				                GConfEntry *entry,
						gpointer user_data)
{
	rb_podcast_manager_update_synctime (RB_PODCAST_MANAGER (user_data));
}


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
	gchar *last_post;
	gulong status;
	gboolean updates;
	gboolean new_feed;
	gboolean insert_post;
	RhythmDB *db = pd->priv->db;

	RhythmDBEntry *entry;

	GList *lst_songs;

	if (data->title	== NULL) {
		g_list_free (data->lst_itens);
		g_free (data);
		return;
	}

	last_post = NULL;
	insert_post = TRUE;
	new_feed = TRUE;

	//processing podcast head
	entry = rhythmdb_entry_lookup_by_location (db, (gchar* )data->url);
	if (entry) {
		if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
			return;
			
		rb_debug ("Head found");
		g_value_init (&status_val, G_TYPE_ULONG);
		g_value_set_ulong (&status_val, 1);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &status_val);
		g_value_unset (&status_val);
		last_post = g_strdup (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LAST_POST));
		if (last_post) {
			insert_post = FALSE;
		}
		new_feed = FALSE;
		goto load_posts;
	}
	
	rb_debug ("Insert new entry");
	entry = rhythmdb_entry_new (db,
				    RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
			    	    (gchar*) data->url);


	rb_debug("New entry create\n");
	
	g_value_init (&title_val, G_TYPE_STRING);
	g_value_set_string (&title_val, (gchar* ) data->title);
	rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_TITLE, &title_val);
	g_value_unset (&title_val);
	
	g_value_init (&author_val, G_TYPE_STRING);
	if (data->author)
		g_value_set_string (&author_val, (gchar* ) data->author);
	else
		g_value_set_static_string (&author_val, _("Unknown"));
	rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_ARTIST, &author_val);
	g_value_unset (&author_val);
	
	
	if (data->subtitle) {
		g_value_init (&subtitle_val, G_TYPE_STRING);
		g_value_set_string (&subtitle_val, (gchar* ) data->subtitle);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_SUBTITLE, &subtitle_val);
		g_value_unset (&subtitle_val);
	}
	
	if (data->description) {
		g_value_init (&description_val, G_TYPE_STRING);
		g_value_set_string (&description_val, (gchar* ) data->description);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_DESCRIPTION, &description_val);
		g_value_unset (&description_val);
	}
	
	if (data->summary) {
		g_value_init (&summary_val, G_TYPE_STRING);
		g_value_set_string (&summary_val, (gchar* ) data->summary);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_SUMMARY, &summary_val);
		g_value_unset (&summary_val);
	}

	if (data->lang) {
		g_value_init (&lang_val, G_TYPE_STRING);
		g_value_set_string (&lang_val, (gchar* ) data->lang);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_LANG, &lang_val);
		g_value_unset (&lang_val);
	}

	if (data->copyright) { 
		g_value_init (&copyright_val, G_TYPE_STRING);
		g_value_set_string (&copyright_val, (gchar* ) data->copyright);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_COPYRIGHT, &copyright_val);
		g_value_unset (&copyright_val);
	}

	if (data->img) {
		g_value_init (&image_val, G_TYPE_STRING);
		g_value_set_string (&image_val, (gchar* ) data->img);
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_IMAGE, &image_val);
		g_value_unset (&image_val);
	}

	g_value_init (&status_val, G_TYPE_ULONG);
	g_value_set_ulong (&status_val, 1);
	rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_STATUS, &status_val);
	g_value_unset (&status_val);

	rb_debug("Podcast head Inserted");
	
load_posts:
	
	lst_songs = g_list_reverse (data->lst_itens);
	status = 103;
	updates = FALSE;
	while (lst_songs) {
		RBPodcastItem *item = (RBPodcastItem *) lst_songs->data;
		if (item && (new_feed || insert_post)) {
			
			if (last_post) {
				g_free (last_post);	
			}
			
			last_post = g_strdup ((gchar* ) item->url);
			if (g_list_last (lst_songs) == lst_songs) {
				status = 102;
			}
				    
			updates = rb_podcast_manager_add_post (db, 
							       (gchar* ) data->title,
						 	       (gchar* ) item->title, 
							       (gchar* ) data->url,
							       (gchar* ) (item->author ? item->author : data->author),
							       (gchar* ) item->url,
							       (gchar* ) item->description,
							       status,
							       (gulong ) (item->pub_date > 0 ? item->pub_date : data->pub_date),
							       (gulong) item->duration,
							       item->filesize);
			
			status = 103;
		} else {
			if (!g_strcasecmp(last_post, (gchar* )item->url)) {
				insert_post = TRUE;
			}
		}
		
		lst_songs = lst_songs->next;
	}
	
	g_value_init (&last_post_val, G_TYPE_STRING);
	if (last_post) {
		g_value_set_string (&last_post_val, last_post);
	} else {
		g_value_set_string (&last_post_val, "");
	}
	if (new_feed) 
		rhythmdb_entry_set_uninserted (db, entry, RHYTHMDB_PROP_LAST_POST, &last_post_val);
	else
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_POST, &last_post_val);
	
	g_value_unset (&last_post_val);
	
	rhythmdb_commit (db);

	g_free (last_post);
	return;
}


static gboolean 
rb_podcast_manager_event_loop (gpointer data) 
{
	gboolean ret;
	RBPodcastManager *pd = (RBPodcastManager *) data;

	if (pd->priv->event_list && (g_list_length (pd->priv->event_list) > 0)) {
		while (g_list_length (pd->priv->event_list) > 0) {
			RBPodcastManagerEvent *event =  (RBPodcastManagerEvent *) g_list_first (pd->priv->event_list)->data;
			switch (event->type)
			{
				case EVENT_INSERT_FEED:
					rb_podcast_manager_insert_feed (pd, event->channel);
					break;
				case EVENT_ERROR_FEED:
				{
					gchar *error_msg;
					error_msg = g_strdup_printf (_("There was a problem adding this podcast. Please verify the URL: %s"), (gchar *) event->channel->url);
					g_signal_emit (G_OBJECT (data), 
						       rb_podcast_manager_signals[PROCESS_ERROR], 
						       0, error_msg);
					g_free (error_msg);
					break;
				}
				default:					
					break;
			}
			
			g_mutex_lock (pd->priv->event_mutex);
			pd->priv->event_list = g_list_remove (pd->priv->event_list, (gconstpointer ) event);
			pd->priv->event_count--;
			g_mutex_unlock (pd->priv->event_mutex);
			rb_podcast_parse_channel_free (event->channel);
			event->channel = NULL;
			g_free (event);
		}
	}
	
	g_mutex_lock (pd->priv->event_mutex);
	if (pd->priv->event_count > 0) {
		ret = TRUE;
		g_usleep (1000);
	} else {
		ret = FALSE;
		g_mutex_unlock (pd->priv->event_loop_mutex);
	}
	g_mutex_unlock (pd->priv->event_mutex);
	
	return ret;
}

static void
rb_podcast_manager_abort_subscribe (RBPodcastManager *pd)
{
	GList *lst;
	pd->priv->event_count = 0;
	g_mutex_lock (pd->priv->event_loop_mutex);

	lst = pd->priv->event_list;
	
	while (lst) {
		RBPodcastManagerEvent *event = (RBPodcastManagerEvent *) (g_list_first (lst))->data;
		rb_podcast_parse_channel_free (event->channel);
		event->channel = NULL;
		lst = g_list_remove (lst, (gconstpointer ) event);
		g_free (event);
	}

	g_mutex_unlock (pd->priv->event_loop_mutex);
}

void
rb_podcast_manager_shutdown (RBPodcastManager *pd)
{
	rb_podcast_manager_cancel_all (pd);
	rb_podcast_manager_abort_subscribe (pd);
}
