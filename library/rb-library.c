/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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
 *  $Id$
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libxml/tree.h>
#include <gtk/gtkmain.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <monkey-media.h>
#include <unistd.h>
#include <string.h>

#include "rb-node-db.h"
#include "rb-node-common.h"
#include "rb-library.h"
#include "rb-library-walker-thread.h"
#include "rb-library-main-thread.h"
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-library-action.h"
#include "rb-file-monitor.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-marshal.h"
#include "rb-file-helpers.h"

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_save (RBLibrary *library);
static void rb_library_create_skels (RBLibrary *library);
static void sync_node (RBNode *node,
		       RBLibrary *library,
		       gboolean check_reparent,
		       GError **error);
static void finalize_node (RBNode *node);
static void restore_node (RBNode *node);
static void sync_sort_keys (RBNode *node);
static void walker_thread_done_cb (RBLibraryWalkerThread *thread, RBLibrary *library);
static char *get_status_normal (RBLibrary *library, RBNode *root, RBNodeFilter *filter);
static gboolean poll_status_update (gpointer data);

enum RBLibraryState
{
	LIBRARY_STATE_NONE,
	LIBRARY_STATE_INITIAL_REFRESH,
};

struct RBLibraryPrivate
{
	GMutex *lock;

	enum RBLibraryState state;

	RBLibraryWalkerThread *walker_thread;
	RBLibraryMainThread *main_thread;

	GMutex *walker_mutex;
	GList *walker_threads;

	GAsyncQueue *main_queue;
	GAsyncQueue *add_queue;

	RBNodeDb *db;

	RBNode *all_genres;
	RBNode *all_artists;
	RBNode *all_albums;
	RBNode *all_songs;

	GHashTable *genre_hash;
	GHashTable *artist_hash;
	GHashTable *album_hash;
	GHashTable *song_hash;

	GStaticRWLock *genre_hash_lock;
	GStaticRWLock *artist_hash_lock;
	GStaticRWLock *album_hash_lock;
	GStaticRWLock *song_hash_lock;

	GTimeVal mod_time;

	GTimeVal cached_mod_time;
	char *cached_status;

	char *xml_file;

	guint refresh_count;

	guint idle_save_id;

	gboolean status_poll_queued;
	guint status_poll_id;

	gboolean in_shutdown;
};

enum
{
	PROP_0,
};

enum
{
	ERROR,
	STATUS_CHANGED,
	PROGRESS,
	LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;

static guint rb_library_signals[LAST_SIGNAL] = { 0 };

GType
rb_library_get_type (void)
{
	static GType rb_library_type = 0;

	if (rb_library_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_class_init,
			NULL,
			NULL,
			sizeof (RBLibrary),
			0,
			(GInstanceInitFunc) rb_library_init
		};

		rb_library_type = g_type_register_static (G_TYPE_OBJECT,
						          "RBLibrary",
						          &our_info, 0);
	}

	return rb_library_type;
}

static void
rb_library_class_init (RBLibraryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_finalize;

	rb_library_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryClass, error),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
	rb_library_signals[STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryClass, status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_library_signals[PROGRESS] =
		g_signal_new ("progress",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryClass, progress),
			      NULL, NULL,
			      rb_marshal_VOID__FLOAT,
			      G_TYPE_NONE,
			      1, G_TYPE_FLOAT);
}

static void
rb_library_init (RBLibrary *library)
{
	char *libname = g_strdup_printf ("library-%s.xml", RB_LIBRARY_XML_VERSION);
	
	library->priv = g_new0 (RBLibraryPrivate, 1);

	library->priv->db = rb_node_db_new (RB_NODE_DB_LIBRARY);

	library->priv->xml_file = g_build_filename (rb_dot_dir (),
						    libname,
						    NULL);

	g_free (libname); 

	library->priv->genre_hash = g_hash_table_new (g_str_hash,
						      g_str_equal);
	library->priv->artist_hash = g_hash_table_new (g_str_hash,
						       g_str_equal);
	library->priv->album_hash = g_hash_table_new (g_str_hash,
						      g_str_equal);
	library->priv->song_hash = g_hash_table_new (g_str_hash,
						     g_str_equal);

	library->priv->genre_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (library->priv->genre_hash_lock);

	library->priv->artist_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (library->priv->artist_hash_lock);

	library->priv->album_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (library->priv->album_hash_lock);

	library->priv->song_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (library->priv->song_hash_lock);

	rb_library_create_skels (library);

	library->priv->lock = g_mutex_new ();

	library->priv->walker_mutex = g_mutex_new ();

	library->priv->main_queue = g_async_queue_new ();
	library->priv->add_queue = g_async_queue_new ();
}

static void
rb_library_pass_on_error (RBLibraryMainThread *thread,
			  const char *uri, const char *error,
			  RBLibrary *library)
{
	rb_debug ("passing on signal");
	g_signal_emit (G_OBJECT (library), rb_library_signals[ERROR], 0,
		       uri, error);
}

void
rb_library_release_brakes (RBLibrary *library)
{
	rb_debug ("releasing brakes");

	rb_debug ("library: kicking off main thread");
	/* create these after having loaded the xml to avoid extra loading time */
	library->priv->main_thread = rb_library_main_thread_new (library);

	g_signal_connect (G_OBJECT (library->priv->main_thread), "error",
			  G_CALLBACK (rb_library_pass_on_error), library);

}

static gboolean
queue_is_empty (GAsyncQueue *queue)
{
	gpointer data = g_async_queue_try_pop (queue);
	if (data == NULL)
		return TRUE;

	g_async_queue_push (queue, data);
	return FALSE;
}

/* We don't particularly care about race conditions here.  This function
 * is just supposed to give some feedback about whether the library
 * is busy or not, it doesn't have to be precise.
 */
gboolean
rb_library_is_idle (RBLibrary *library)
{
	return queue_is_empty (library->priv->main_queue)
		&& queue_is_empty (library->priv->add_queue);
}

void
rb_library_shutdown (RBLibrary *library)
{
	rb_debug ("Shuuut it dooooown!");

	library->priv->in_shutdown = TRUE;

	GDK_THREADS_LEAVE (); /* be sure the main thread is able to finish */
	g_object_unref (G_OBJECT (library->priv->main_thread));
	GDK_THREADS_ENTER ();

	rb_debug ("killing walker threads");
	g_mutex_lock (library->priv->walker_mutex);
	while (library->priv->walker_threads != NULL) {
		rb_library_walker_thread_kill (RB_LIBRARY_WALKER_THREAD (library->priv->walker_threads->data));
		library->priv->walker_threads = g_list_next (library->priv->walker_threads);
	}
	g_mutex_unlock (library->priv->walker_mutex);
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	if (library->priv->idle_save_id != 0) {
		g_source_remove (library->priv->idle_save_id);
	}

	rb_debug ("library: finalizing");

	g_mutex_free (library->priv->walker_mutex);

	g_async_queue_unref (library->priv->main_queue);
	g_async_queue_unref (library->priv->add_queue);

	rb_library_save (library);

	rb_debug ("unreffing ALL nodes");
	rb_node_unref (library->priv->all_songs);
	rb_node_unref (library->priv->all_albums);
	rb_node_unref (library->priv->all_artists);
	rb_node_unref (library->priv->all_genres);

	g_object_unref (library->priv->db);

	g_hash_table_destroy (library->priv->genre_hash);
	g_hash_table_destroy (library->priv->artist_hash);
	g_hash_table_destroy (library->priv->album_hash);
	g_hash_table_destroy (library->priv->song_hash);

	g_static_rw_lock_free (library->priv->genre_hash_lock);
	g_static_rw_lock_free (library->priv->artist_hash_lock);
	g_static_rw_lock_free (library->priv->album_hash_lock);
	g_static_rw_lock_free (library->priv->song_hash_lock);

	g_mutex_free (library->priv->lock);

	g_free (library->priv->cached_status);

	g_free (library->priv->xml_file);

	if (library->priv->status_poll_queued)
		g_source_remove (library->priv->status_poll_id);

	g_free (library->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
	rb_debug ("library finalization complete");
}

RBLibrary *
rb_library_new (void)
{
	RBLibrary *library;

	library = RB_LIBRARY (g_object_new (RB_TYPE_LIBRARY, NULL));

	g_return_val_if_fail (library->priv != NULL, NULL);

	return library;
}

static void
signal_status_changed (RBLibrary *library)
{
	rb_thread_helpers_lock_gdk ();

	if (!library->priv->in_shutdown) {
		g_get_current_time (&library->priv->mod_time);
		
		g_signal_emit (G_OBJECT (library), rb_library_signals[STATUS_CHANGED], 0);
	}
	
	rb_thread_helpers_unlock_gdk ();
}

static void
signal_progress_changed (RBLibrary *library)
{
	rb_thread_helpers_lock_gdk ();

	if (library->priv->in_shutdown)
		goto out_unlock;

	if (!queue_is_empty (library->priv->add_queue)) {
		g_signal_emit (G_OBJECT (library), rb_library_signals[PROGRESS], 0, -1.0);
	} else if (library->priv->state == LIBRARY_STATE_INITIAL_REFRESH) {
		float total;
		float refresh_count;

		g_mutex_lock (library->priv->lock);
		refresh_count = (float) library->priv->refresh_count;
		g_mutex_unlock (library->priv->lock);

		total = (float) rb_node_get_n_children (library->priv->all_songs);
		
		g_signal_emit (G_OBJECT (library), rb_library_signals[PROGRESS], 0,
			       refresh_count / total);
	} else {
		g_signal_emit (G_OBJECT (library), rb_library_signals[PROGRESS], 0, 1.0);
	}

out_unlock:
	rb_thread_helpers_unlock_gdk ();
}

static void
walker_thread_done_cb (RBLibraryWalkerThread *thread, RBLibrary *library)
{
	rb_debug ("caught walker done");

	g_mutex_lock (library->priv->walker_mutex);

	library->priv->walker_threads = g_list_remove (library->priv->walker_threads, thread);

	g_mutex_unlock (library->priv->walker_mutex);
}

/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
void
rb_library_add_uri (RBLibrary *library, const char *uri)
{
	if (rb_uri_is_directory (uri) == FALSE) {
		RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_ADD_FILE, uri);
		rb_debug ("queueing ADD_FILE for %s", uri);
		g_async_queue_push (library->priv->add_queue, action);
	} else {
		RBLibraryWalkerThread *thread = rb_library_walker_thread_new (library, uri);

		g_signal_connect (G_OBJECT (thread), "done", G_CALLBACK (walker_thread_done_cb), library);

		g_mutex_lock (library->priv->walker_mutex);
		library->priv->walker_threads = g_list_append (library->priv->walker_threads, thread);
		g_mutex_unlock (library->priv->walker_mutex);

		rb_debug ("starting walker thread for URI %s", uri);

		rb_library_walker_thread_start (thread);
	}
}

/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
void
rb_library_add_uri_sync (RBLibrary *library, const char *uri, GError **error)
{
	if (rb_library_get_song_by_location (library, uri) == NULL) {
		rb_debug ("uri \"%s\" does not exist, adding new node", uri);
		rb_library_new_node (library, uri, error);
		if (error && *error)
			goto signal;
	} else {
		rb_debug ("uri \"%s\" already exists", uri);
	}
	
	rb_file_monitor_add (rb_file_monitor_get (), uri);

signal:
	if (!library->priv->in_shutdown) {
		signal_status_changed (library);
		signal_progress_changed (library);
	}
}


/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
gboolean
rb_library_update_uri (RBLibrary *library, const char *uri, GError **error)
{
	RBNode *song;
	gboolean ret = FALSE;

	g_mutex_lock (library->priv->lock);
	library->priv->refresh_count++;
	g_mutex_unlock (library->priv->lock);
	
	song = rb_library_get_song_by_location (library, uri);
	if (song == NULL)
		goto out;
	
	if (rb_uri_exists (uri) == FALSE) {
		rb_debug ("song \"%s\" was deleted", uri);
		rb_node_unref (song);
		goto out;
	}
	
	rb_debug ("updating existing node \"%s\"", uri);
	ret = rb_library_update_node (library, song, error);
	if (error && *error) {
		goto out;
	}

	/* just to be sure */
	rb_file_monitor_add (rb_file_monitor_get (), uri);
	ret = TRUE;
out:
	signal_status_changed (library);
	signal_progress_changed (library);
	return ret;
}

void rb_library_remove_uri (RBLibrary *library, const char *uri)
{
	RBNode *song;
	
	song = rb_library_get_song_by_location (library, uri);
	if (song == NULL)
		return;

	rb_node_unref (song);

	rb_file_monitor_remove (rb_file_monitor_get (), uri);
}

void
rb_library_remove_node (RBLibrary *library,
			RBNode *node)
{
	rb_node_unref (node);
}

RBNodeDb *
rb_library_get_node_db (RBLibrary *library)
{
	return library->priv->db;
}

RBNode *
rb_library_get_all_genres (RBLibrary *library)
{
	return library->priv->all_genres;
}

RBNode *
rb_library_get_all_artists (RBLibrary *library)
{
	return library->priv->all_artists;
}

RBNode *
rb_library_get_all_albums (RBLibrary *library)
{
	return library->priv->all_albums;
}

RBNode *
rb_library_get_all_songs (RBLibrary *library)
{
	return library->priv->all_songs;
}

static void
genre_added_cb (RBNode *node,
		RBNode *child,
		RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->genre_hash_lock);

	g_hash_table_insert (library->priv->genre_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_NAME),
			     child);
	
	g_static_rw_lock_writer_unlock (library->priv->genre_hash_lock);
}

static void
artist_added_cb (RBNode *node,
	 	 RBNode *child,
		 RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->artist_hash_lock);
	
	g_hash_table_insert (library->priv->artist_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_NAME),
			     child);

	g_static_rw_lock_writer_unlock (library->priv->artist_hash_lock);
}

static void
album_added_cb (RBNode *node,
		RBNode *child,
		RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->album_hash_lock);
	
	g_hash_table_insert (library->priv->album_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_NAME),
			     child);
	
	g_static_rw_lock_writer_unlock (library->priv->album_hash_lock);
}

static void
song_added_cb (RBNode *node,
	       RBNode *child,
	       RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->song_hash_lock);

	/* Don't bother to emit the status changed signal here;
	 * the source will come get the updated status because of the
	 * node view change anyways. */
	g_get_current_time (&library->priv->mod_time);
	
	g_hash_table_insert (library->priv->song_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_LOCATION),
			     child);
	
	g_static_rw_lock_writer_unlock (library->priv->song_hash_lock);
}

static void
genre_removed_cb (RBNode *node,
		  RBNode *child,
		  guint last_id,
		  RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->genre_hash_lock);
	
	g_hash_table_remove (library->priv->genre_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	
	g_static_rw_lock_writer_unlock (library->priv->genre_hash_lock);
}

static void
artist_removed_cb (RBNode *node,
		   RBNode *child,
		   guint last_id,
		   RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->artist_hash_lock);
	
	g_hash_table_remove (library->priv->artist_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	
	g_static_rw_lock_writer_unlock (library->priv->artist_hash_lock);
}

static void
album_removed_cb (RBNode *node,
		  RBNode *child,
		  guint last_id,
		  RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->album_hash_lock);
	
	g_hash_table_remove (library->priv->album_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	
	g_static_rw_lock_writer_unlock (library->priv->album_hash_lock);
}

static void
song_removed_cb (RBNode *node,
		 RBNode *child,
		 guint last_id,
		 RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->song_hash_lock);

	g_get_current_time (&library->priv->mod_time);

	g_hash_table_remove (library->priv->song_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_LOCATION));
	
	g_static_rw_lock_writer_unlock (library->priv->song_hash_lock);
}

static void
rb_library_create_skels (RBLibrary *library)
{
	/* create a boostrap setup */
	GValue value = { 0, };

	library->priv->all_genres  = rb_node_new_with_id (library->priv->db, LIBRARY_GENRES_NODE_ID);
	library->priv->all_artists = rb_node_new_with_id (library->priv->db, LIBRARY_ARTISTS_NODE_ID);
	library->priv->all_albums  = rb_node_new_with_id (library->priv->db, LIBRARY_ALBUMS_NODE_ID);
	library->priv->all_songs   = rb_node_new_with_id (library->priv->db, LIBRARY_SONGS_NODE_ID);

	rb_node_ref (library->priv->all_genres);
	rb_node_ref (library->priv->all_artists);
	rb_node_ref (library->priv->all_albums);
	rb_node_ref (library->priv->all_songs);

	rb_node_signal_connect_object (library->priv->all_genres,
				       RB_NODE_CHILD_ADDED,
				       (RBNodeCallback) genre_added_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_artists,
				       RB_NODE_CHILD_ADDED,
				       (RBNodeCallback) artist_added_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_albums,
				       RB_NODE_CHILD_ADDED,
				       (RBNodeCallback) album_added_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_songs,
				       RB_NODE_CHILD_ADDED,
				       (RBNodeCallback) song_added_cb,
				       G_OBJECT (library));
				 
	rb_node_signal_connect_object (library->priv->all_genres,
				       RB_NODE_CHILD_REMOVED,
				       (RBNodeCallback) genre_removed_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_artists,
				       RB_NODE_CHILD_REMOVED,
				       (RBNodeCallback) artist_removed_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_albums,
				       RB_NODE_CHILD_REMOVED,
				       (RBNodeCallback) album_removed_cb,
				       G_OBJECT (library));
	rb_node_signal_connect_object (library->priv->all_songs,
				       RB_NODE_CHILD_REMOVED,
				       (RBNodeCallback) song_removed_cb,
				       G_OBJECT (library));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, _("All"));
	rb_node_set_property (library->priv->all_genres,
			      RB_NODE_PROP_NAME,
			      &value);
	rb_node_set_property (library->priv->all_artists,
			      RB_NODE_PROP_NAME,
			      &value);
	rb_node_set_property (library->priv->all_albums,
			      RB_NODE_PROP_NAME,
			      &value);
	rb_node_set_property (library->priv->all_songs,
			      RB_NODE_PROP_NAME,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, TRUE);
	rb_node_set_property (library->priv->all_genres,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_artists,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_albums,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_songs,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	g_value_unset (&value);


	rb_node_add_child (library->priv->all_genres,
			   library->priv->all_artists);
	rb_node_add_child (library->priv->all_artists,
			   library->priv->all_albums);
	rb_node_add_child (library->priv->all_albums,
			   library->priv->all_songs);
	
	rb_debug ("Done creating skels");
}

static void
rb_library_save (RBLibrary *library)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *children;
	int i;
	GString *tmpname = g_string_new (library->priv->xml_file);

	rb_debug ("library: saving");

	g_string_append (tmpname, ".tmp");
	
	/* save nodes to xml */
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_library", NULL);
	xmlSetProp (root, "version", RB_LIBRARY_XML_VERSION);
	xmlDocSetRootElement (doc, root);

	children = rb_node_get_children (library->priv->all_genres);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != library->priv->all_artists)
			rb_node_save_to_xml (kid, root);
	}

	children = rb_node_get_children (library->priv->all_artists);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != library->priv->all_albums)
			rb_node_save_to_xml (kid, root);
	}

	children = rb_node_get_children (library->priv->all_albums);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);

		if (kid != library->priv->all_songs)
			rb_node_save_to_xml (kid, root);
	}

	children = rb_node_get_children (library->priv->all_songs);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);

		rb_node_save_to_xml (kid, root);
	}

	rb_node_thaw (library->priv->all_genres);
	rb_node_thaw (library->priv->all_artists);
	rb_node_thaw (library->priv->all_albums);
	rb_node_thaw (library->priv->all_songs);

	xmlSaveFormatFile (tmpname->str, doc, 1);
	rename (tmpname->str, library->priv->xml_file);
	g_string_free (tmpname, TRUE);
	xmlFreeDoc (doc);
	rb_debug ("library: done saving");
}

GAsyncQueue *
rb_library_get_main_queue (RBLibrary *library)
{
	return library->priv->main_queue;
}

GAsyncQueue *
rb_library_get_add_queue (RBLibrary *library)
{
	return library->priv->add_queue;
}

RBNode *
rb_library_get_genre_by_name (RBLibrary *library,
			      const char *genre)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (library->priv->genre_hash_lock);

	ret = g_hash_table_lookup (library->priv->genre_hash,
				   genre);
	
	g_static_rw_lock_reader_unlock (library->priv->genre_hash_lock);

	return ret;
}

RBNode *
rb_library_get_artist_by_name (RBLibrary *library,
			       const char *artist)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (library->priv->artist_hash_lock);
	
	ret = g_hash_table_lookup (library->priv->artist_hash,
				   artist);

	g_static_rw_lock_reader_unlock (library->priv->artist_hash_lock);

	return ret;
}

RBNode *
rb_library_get_album_by_name (RBLibrary *library,
			      const char *album)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (library->priv->album_hash_lock);
	
	ret = g_hash_table_lookup (library->priv->album_hash,
				   album);
	
	g_static_rw_lock_reader_unlock (library->priv->album_hash_lock);

	return ret;
}

RBNode *
rb_library_get_song_by_location (RBLibrary *library,
			         const char *location)
{
	RBNode *ret;

	g_static_rw_lock_reader_lock (library->priv->song_hash_lock);
	
	ret = g_hash_table_lookup (library->priv->song_hash,
				   location);
	
	g_static_rw_lock_reader_unlock (library->priv->song_hash_lock);

	return ret;
}

void
rb_library_handle_songs (RBLibrary *library,
			 RBNode *node,
			 GFunc func,
			 gpointer user_data)
{
	if (rb_node_get_property_string (node, RB_NODE_PROP_LOCATION))
		(*func) (node, user_data);
	else {
		GPtrArray *kids;
		int i;

		kids = rb_node_get_children (node);
		for (i = 0; i < kids->len; i++) {
			RBNode *n;

			n = g_ptr_array_index (kids, i);
			
			rb_library_handle_songs (library, n, func, user_data);
		}

		rb_node_thaw (node);
	}
}

void
rb_library_load (RBLibrary *library)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	char *tmp;
	RBProfiler *p;

	if (g_file_test (library->priv->xml_file, G_FILE_TEST_EXISTS) == FALSE)
		return;

	rb_debug ("library: loading");

	doc = xmlParseFile (library->priv->xml_file);

	if (doc == NULL) {
		rb_error_dialog (_("Failed to load library!"));
		unlink (library->priv->xml_file);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_LIBRARY_XML_VERSION) != 0) {
		g_free (tmp);
		unlink (library->priv->xml_file);
		xmlFreeDoc (doc);
		return;
	}
	g_free (tmp);

	p = rb_profiler_new ("XML loader");

	library->priv->state = LIBRARY_STATE_INITIAL_REFRESH;

	for (child = root->children; child != NULL; child = child->next) {
		RBNode *node;
		const char *location;

		if (xmlNodeIsText(child))
			continue;
		
		node = rb_node_new_from_xml (library->priv->db, child);

		if (G_UNLIKELY (!node)) {
			tmp = g_strdup_printf("%s.busted", library->priv->xml_file);
			rb_error_dialog("failed to load \"%s\"", library->priv->xml_file);
			rename (library->priv->xml_file, tmp);
			g_free (tmp);
			return;
		}

		restore_node (node);
			
		location = rb_node_get_property_string (node, RB_NODE_PROP_LOCATION);
		if (G_LIKELY (location != NULL)) {
			RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_UPDATE_FILE, location);
			g_async_queue_push (library->priv->main_queue, action);
		}
	}

	rb_profiler_dump (p);
	rb_profiler_free (p);

	xmlFreeDoc (doc);
	rb_debug ("library: done loading");
}

static void
finalize_node (RBNode *node)
{
	RBNode *parent;
	
	rb_debug ("finalizing %p", node);

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ALBUM);
	if (G_LIKELY (parent != NULL))
		rb_node_unref_with_locked_child (parent, node);

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ARTIST);
	if (G_LIKELY (parent != NULL))
		rb_node_unref_with_locked_child (parent, node);

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE);
	if (G_LIKELY (parent != NULL))
		rb_node_unref_with_locked_child (parent, node);
}

static void
restore_node (RBNode *node)
{
	RBNode *parent;

	if (rb_node_get_property_string (node, RB_NODE_PROP_LOCATION))
		rb_node_signal_connect_object (node, RB_NODE_DESTROY,
					       (RBNodeCallback) finalize_node, NULL);

	sync_sort_keys (node);
	
	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ALBUM);
	if (G_LIKELY (parent != NULL))
		rb_node_ref (parent);

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ARTIST);
	if (G_LIKELY (parent != NULL))
		rb_node_ref (parent);

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE);
	if (G_LIKELY (parent != NULL))
		rb_node_ref (parent);
}

RBNode *
rb_library_new_node (RBLibrary *library,
		     const char *location,
		     GError **error)
{
	RBNode *node;
	GValue value = { 0, };

	g_return_val_if_fail (location != NULL, NULL);
	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	node = rb_node_new (library->priv->db); 

	if (G_UNLIKELY (node == NULL))
		return NULL;

	rb_node_signal_connect_object (node, RB_NODE_DESTROY,
				       (RBNodeCallback) finalize_node, NULL);

	/* Location */
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, location);
	rb_node_set_property (node,
			      RB_NODE_PROP_LOCATION,
			      &value);
	g_value_unset (&value);

	/* Number of plays */
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 0);
	rb_node_set_property (node,
			      RB_NODE_PROP_PLAY_COUNT,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, 0);
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "Never");
	/* Last played time */
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED_STR,
			      &value);

	g_value_unset (&value);

	sync_node (node, library, FALSE, error);
	if (G_UNLIKELY (error && *error))
		return NULL;

	return node;
}

static gboolean
is_different (RBNode *node, int property, GValue *value)
{
	gboolean equal;
	const char *string;

	string = rb_node_get_property_string (node, property);

	equal = (strcmp (string, g_value_get_string (value)) == 0);

	return !equal;
}

static void
set_node_value (RBNode *node, int property,
		MonkeyMediaStreamInfo *info,
		MonkeyMediaStreamInfoField field)
{
	GValue val = { 0, };

	monkey_media_stream_info_get_value (info, field, 0, &val);

	rb_node_set_property (node, property, &val);

	g_value_unset (&val);
}

static void
set_node_title (RBNode *node, MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };

	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    0,
					    &val);

	if (*(g_value_get_string (&val)) == '\0') {
		GnomeVFSURI *vfsuri = gnome_vfs_uri_new (rb_node_get_property_string (node, RB_NODE_PROP_LOCATION));
		char *fname = gnome_vfs_uri_extract_short_name (vfsuri);
		g_value_set_string_take_ownership (&val, fname);
		gnome_vfs_uri_unref (vfsuri);
	}

	rb_node_set_property (node, RB_NODE_PROP_NAME, &val);
}

static void
set_sort_key_prop (RBNode *node, guint source, guint dest)
{
	GValue val = { 0, };

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, rb_get_sort_key (rb_node_get_property_string (node, source)));
	rb_node_set_property (node, dest, &val);
	g_value_unset (&val);
}

static void
sync_sort_keys (RBNode *node)
{
		
	set_sort_key_prop (node, RB_NODE_PROP_NAME, RB_NODE_PROP_NAME_SORT_KEY);

	if (rb_node_get_property_string (node, RB_NODE_PROP_ARTIST) != NULL)
		set_sort_key_prop (node, RB_NODE_PROP_ARTIST, RB_NODE_PROP_ARTIST_SORT_KEY);
	if (rb_node_get_property_string (node, RB_NODE_PROP_ALBUM) != NULL)
		set_sort_key_prop (node, RB_NODE_PROP_ALBUM, RB_NODE_PROP_ALBUM_SORT_KEY);
}
	
static void
set_node_mtime (RBNode *node, const char *location)
{
	GnomeVFSFileInfo *info;
	GValue val = { 0, };

	info = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (location, info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, info->mtime);
	rb_node_set_property (node, RB_NODE_PROP_MTIME, &val);
	g_value_unset (&val);

	gnome_vfs_file_info_unref (info);
}

static void
set_node_duration (RBNode *node,
		   MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	GValue string_val = { 0, };
	long minutes = 0, seconds = 0;
	char *tmp;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    0,
				            &val);
	rb_node_set_property (node,
			      RB_NODE_PROP_DURATION,
			      &val);

	g_value_init (&string_val, G_TYPE_STRING);

	if (g_value_get_long (&val) > 0) {
		minutes = g_value_get_long (&val) / 60;
		seconds = g_value_get_long (&val) % 60;
	}

	tmp = g_strdup_printf (_("%ld:%02ld"), minutes, seconds);
	g_value_set_string (&string_val, tmp);
	g_free (tmp);

	rb_node_set_property (node,
			      RB_NODE_PROP_DURATION_STR,
			      &string_val);

	g_value_unset (&string_val);

	g_value_unset (&val);
}

static void
set_node_track_number (RBNode *node,
		       MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	int cur;

	if (monkey_media_stream_info_get_value (info,
				                MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					        0,
				                &val) == FALSE)
	{
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, -1);
	}

	rb_node_set_property (node,
			      RB_NODE_PROP_TRACK_NUMBER,
			      &val);
	cur = g_value_get_int (&val);
	g_value_unset (&val);
}

static gboolean
set_node_genre (RBNode *node,
		MonkeyMediaStreamInfo *info,
		RBLibrary *library,
		gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *genre;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_NODE_PROP_GENRE, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	genre = rb_library_get_genre_by_name (library,
					      g_value_get_string (&val));
	
	if (genre == NULL) {
		genre = rb_node_new (library->priv->db);

		rb_node_set_property (genre, RB_NODE_PROP_NAME, &val);

		sync_sort_keys (genre);

		rb_node_add_child (rb_library_get_all_genres (library), genre);
	}
	
	if (check_reparent == FALSE)
		rb_node_ref (genre);

	rb_node_set_property (node, RB_NODE_PROP_GENRE, &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property (node, RB_NODE_PROP_REAL_GENRE, &val);
	g_value_unset (&val);

	return FALSE;
}

static gboolean
set_node_artist (RBNode *node,
		 MonkeyMediaStreamInfo *info,
		 RBLibrary *library,
		 gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *artist;
	char *swapped;
	gboolean new = FALSE;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_NODE_PROP_ARTIST, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	swapped = rb_prefix_to_suffix (g_value_get_string (&val));
	if (swapped == NULL)
		swapped = g_strdup (g_value_get_string (&val));
	
	artist = rb_library_get_artist_by_name (library, swapped);
	
	if (artist == NULL) {
		GValue swapped_val = { 0, };
		
		artist = rb_node_new (library->priv->db);

		g_value_init (&swapped_val, G_TYPE_STRING);
		g_value_set_string (&swapped_val, swapped);
		rb_node_set_property (artist, RB_NODE_PROP_NAME, &swapped_val);
		g_value_unset (&swapped_val);

		sync_sort_keys (artist);

		new = TRUE;
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE),
				   artist);

		rb_node_ref (artist);
	}

	rb_node_set_property (node, RB_NODE_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, artist);
	rb_node_set_property (node, RB_NODE_PROP_REAL_ARTIST, &val);
	g_value_unset (&val);

	if (new == TRUE)
		rb_node_add_child (rb_library_get_all_artists (library), artist);

	return FALSE;
}

static gboolean
set_node_album (RBNode *node,
		MonkeyMediaStreamInfo *info,
		RBLibrary *library,
		gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *album;
	gboolean new = FALSE;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_NODE_PROP_ALBUM, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	album = rb_library_get_album_by_name (library,
					      g_value_get_string (&val));
	
	if (album == NULL) {
		album = rb_node_new (library->priv->db);

		rb_node_set_property (album, RB_NODE_PROP_NAME, &val);

		sync_sort_keys (album);

		new = TRUE;
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ARTIST),
				   album);
	
		rb_node_ref (album);
	}
	
	rb_node_set_property (node, RB_NODE_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, album);
	rb_node_set_property (node,
			      RB_NODE_PROP_REAL_ALBUM,
			      &val);
	g_value_unset (&val);

	if (new == TRUE)
		rb_node_add_child (rb_library_get_all_albums (library), album);

	return FALSE;
}

static void
sync_node (RBNode *node, RBLibrary *library, gboolean check_reparent, GError **error)
{
	MonkeyMediaStreamInfo *info;
	char *location;

	location = g_strdup (rb_node_get_property_string (node, RB_NODE_PROP_LOCATION));
	
	info = monkey_media_stream_info_new (location, error);
	if (G_UNLIKELY (info == NULL)) {
		rb_debug ("failed to get info for song \"%s\"", location);
		rb_node_unref (node);
		return;
	}

	/* track number */
	set_node_track_number (node, info);

	/* duration */
	set_node_duration (node, info);

	/* filesize */
	set_node_value (node, RB_NODE_PROP_FILE_SIZE,
			info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE);

	/* title */
	set_node_title (node, info);

	/* mtime */
	set_node_mtime (node, location);

	/* genre, artist & album */
	if (set_node_genre (node, info, library, check_reparent) == TRUE ||
	    set_node_artist (node, info, library, check_reparent) == TRUE ||
	    set_node_album (node, info, library, check_reparent) == TRUE) {
		/* reparent */
		rb_node_unref (node);

		rb_library_add_uri (library, location);
		goto out;
	}

	sync_sort_keys (node);

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_library_get_all_songs (library), node);
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ALBUM),
				   node);
	}

out:
	g_free (location);
	g_object_unref (G_OBJECT (info));
}

gboolean
rb_library_update_node (RBLibrary *library, RBNode *node, GError **error)
{
	GnomeVFSFileInfo *info;
	const char *location;
	long mtime;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_LIBRARY (library), FALSE);

	info = gnome_vfs_file_info_new ();
	
	location = rb_node_get_property_string (node,
						RB_NODE_PROP_LOCATION);
	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	mtime = rb_node_get_property_long (node,
				           RB_NODE_PROP_MTIME);

	if (info->mtime != mtime) {
		gnome_vfs_file_info_unref (info);
		rb_debug ("mtime for file \"%s\" has changed", location);
		sync_node (node, library, TRUE, error);
		return TRUE;
	}
	
	gnome_vfs_file_info_unref (info);
	return FALSE;
}

char *
rb_library_compute_status (RBLibrary *library, RBNode *root, RBNodeFilter *filter)
{
	char *ret = NULL;
	gboolean adding_songs = !queue_is_empty (library->priv->add_queue);

	if (adding_songs)
		ret = g_strdup_printf ("<b>%s</b>", _("Loading songs..."));
	else if (library->priv->state == LIBRARY_STATE_INITIAL_REFRESH) {
		guint refresh_count;
		
		g_mutex_lock (library->priv->lock);
		refresh_count = library->priv->refresh_count;
		g_mutex_unlock (library->priv->lock);

		if (!library->priv->status_poll_queued) {
			g_idle_add ((GSourceFunc) poll_status_update, library);
			library->priv->status_poll_queued = TRUE;
		}
	
		ret = g_strdup_printf ("<b>%s</b>", _("Refreshing songs..."));
	}

	if (adding_songs || library->priv->state == LIBRARY_STATE_INITIAL_REFRESH) {
		if (!library->priv->status_poll_queued) {
			g_idle_add ((GSourceFunc) poll_status_update, library);
			library->priv->status_poll_queued = TRUE;
		}
		return ret;
	}
		
	return get_status_normal (library, root, filter);
}

static gboolean
poll_status_update (gpointer data)
{
	RBLibrary *library = RB_LIBRARY (data);

	GDK_THREADS_ENTER ();

	/* This marks things as requiring an update.  That way we'll
	 * get the full status back when the library is idle.
	 */
	if (rb_library_is_idle (library)) {

		if (library->priv->state == LIBRARY_STATE_INITIAL_REFRESH)
			library->priv->state = LIBRARY_STATE_NONE;

		library->priv->status_poll_queued = FALSE;

		signal_status_changed (library);

	} else {
		library->priv->status_poll_id =
			g_timeout_add (500, (GSourceFunc) poll_status_update, library);
	}
	
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static char *
get_status_normal (RBLibrary *library, RBNode *root, RBNodeFilter *filter)
{
	char *ret;
	float days;
	long len, hours, minutes, seconds;
	long n_seconds = 0;
	GPtrArray *kids;
	int i;

	kids = rb_node_get_children (root);

	len = 0;

	for (i = 0; i < kids->len; i++) {
		long secs;
		RBNode *node;

		node = g_ptr_array_index (kids, i);

		if (filter != NULL && rb_node_filter_evaluate (filter, node) == FALSE)
			continue;

		secs = rb_node_get_property_long (node, RB_NODE_PROP_DURATION);
		if (secs < 0)
			g_warning ("Invalid duration value for node %p", node);
		else
			n_seconds += secs;

		len++;
	}

	rb_node_thaw (root);

	days    = (float) n_seconds / (float) (60 * 60 * 24); 
	hours   = n_seconds / (60 * 60);
	minutes = n_seconds / 60 - hours * 60;
	seconds = n_seconds % 60;

	/* FIXME impl remaining time */
	if (days >= 1.0) {
		ret = g_strdup_printf (_("<b>%.1f</b> days (%ld songs)"),
				       days, len);
	} else if (hours >= 1) {
		ret = g_strdup_printf (_("<b>%ld</b> hours and <b>%ld</b> minutes (%ld songs)"),
				       hours, minutes, len);
	} else {
		ret = g_strdup_printf (_("<b>%ld</b> minutes (%ld songs)"),
				       minutes, len);
	}

	return ret;
}
