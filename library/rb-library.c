/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
#include "rb-library-action-queue.h"
#include "rb-glist-wrapper.h"
#include "rb-string-helpers.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-marshal.h"
#include "rb-file-helpers.h"

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_save (RBLibrary *library);
static void rb_library_create_skels (RBLibrary *library);
static void rb_library_load (RBLibrary *library);
static void sync_node (RBNode *node,
		       RBLibrary *library,
		       gboolean check_reparent,
		       GError **error);

struct RBLibraryPrivate
{
	RBLibraryWalkerThread *walker_thread;
	RBLibraryMainThread *main_thread;

	RBLibraryActionQueue *walker_queue;
	RBLibraryActionQueue *main_queue;

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

	char *xml_file;
};

enum
{
	PROP_0,
};

enum
{
	ERROR,
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
}

static void
rb_library_init (RBLibrary *library)
{
	char *libname = g_strdup_printf ("library-%s.xml", RB_LIBRARY_XML_VERSION);

	/* ensure these types have been registered: */
	rb_glist_wrapper_get_type ();
	
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

	library->priv->main_queue = rb_library_action_queue_new ();
	library->priv->walker_queue = rb_library_action_queue_new ();
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
	rb_debug ("doing it");
	/* and off we go */
	rb_library_load (library);

	rb_debug ("library: kicking off main thread");
	/* create these after having loaded the xml to avoid extra loading time */
	library->priv->main_thread = rb_library_main_thread_new (library);

	g_signal_connect (G_OBJECT (library->priv->main_thread), "error",
			  G_CALLBACK (rb_library_pass_on_error), library);

	rb_debug ("library: creating walker thread");
	library->priv->walker_thread = rb_library_walker_thread_new (library);
}

gboolean
rb_library_is_idle (RBLibrary *library)
{
	return rb_library_action_queue_is_empty (library->priv->main_queue);
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	rb_debug ("library: finalizing");
	GDK_THREADS_LEAVE (); /* be sure the main thread is able to finish */
	g_object_unref (G_OBJECT (library->priv->main_thread));
	GDK_THREADS_ENTER ();
	g_object_unref (G_OBJECT (library->priv->walker_thread));
	g_object_unref (G_OBJECT (library->priv->main_queue));
	g_object_unref (G_OBJECT (library->priv->walker_queue));

	rb_library_save (library);

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

	g_free (library->priv->xml_file);

	g_free (library->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibrary *
rb_library_new (void)
{
	RBLibrary *library;

	library = RB_LIBRARY (g_object_new (RB_TYPE_LIBRARY, NULL));

	g_return_val_if_fail (library->priv != NULL, NULL);

	return library;
}

RBLibraryAction *
rb_library_add_uri (RBLibrary *library,
		    const char *uri)
{
	if (rb_uri_is_directory (uri) == FALSE)
	{
		return rb_library_action_queue_add (library->priv->main_queue,
					            TRUE,
					            RB_LIBRARY_ACTION_ADD_FILE,
					            uri);
	}
	else
	{
		return rb_library_action_queue_add (library->priv->walker_queue,
					            TRUE,
					            RB_LIBRARY_ACTION_ADD_DIRECTORY,
					            uri);
	}
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
			      RB_ALL_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_artists,
			      RB_ALL_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_albums,
			      RB_ALL_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (library->priv->all_songs,
			      RB_ALL_NODE_PROP_PRIORITY,
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
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != library->priv->all_artists)
			rb_node_save_to_xml (kid, root);
	}
	rb_node_thaw (library->priv->all_genres);

	children = rb_node_get_children (library->priv->all_artists);
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != library->priv->all_albums)
			rb_node_save_to_xml (kid, root);
	}
	rb_node_thaw (library->priv->all_artists);

	children = rb_node_get_children (library->priv->all_albums);
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);

		if (kid != library->priv->all_songs)
			rb_node_save_to_xml (kid, root);
	}
	rb_node_thaw (library->priv->all_albums);

	children = rb_node_get_children (library->priv->all_songs);
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);

		rb_node_save_to_xml (kid, root);
	}
	rb_node_thaw (library->priv->all_songs);

	xmlSaveFormatFile (tmpname->str, doc, 1);
	rename (tmpname->str, library->priv->xml_file);
	rb_debug ("library: done saving");
}

RBLibraryActionQueue *
rb_library_get_main_queue (RBLibrary *library)
{
	return library->priv->main_queue;
}

RBLibraryActionQueue *
rb_library_get_walker_queue (RBLibrary *library)
{
	return library->priv->walker_queue;
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
	{
		(*func) (node, user_data);
	}
	else
	{
		GPtrArray *kids;
		int i;

		kids = rb_node_get_children (node);
		for (i = 0; i < kids->len; i++)
		{
			RBNode *n;

			n = g_ptr_array_index (kids, i);
			
			rb_library_handle_songs (library, n, func, user_data);
		}

		rb_node_thaw (node);
	}
}

static void
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

	if (doc == NULL)
	{
		unlink (library->priv->xml_file);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_LIBRARY_XML_VERSION) != 0)
	{
		g_free (tmp);
		unlink (library->priv->xml_file);
		xmlFreeDoc (doc);
		return;
	}
	g_free (tmp);

	p = rb_profiler_new ("XML loader");

	for (child = root->children; child != NULL; child = child->next)
	{
		RBNode *node;
		const char *location;

		if (xmlNodeIsText(child))
			continue;
		
		node = rb_node_new_from_xml (library->priv->db, child);

		if (!node) {
			tmp = g_strdup_printf("%s.busted", library->priv->xml_file);
			rb_error_dialog("failed to load \"%s\"", library->priv->xml_file);
			rename (library->priv->xml_file, tmp);
			g_free (tmp);
			return;
		}
			
		location = rb_node_get_property_string (node,
							RB_NODE_PROP_LOCATION);
		if (location != NULL) {
			rb_debug ("library: queueing %s for updating", location);
			rb_library_action_queue_add (library->priv->main_queue,
						     FALSE,
						     RB_LIBRARY_ACTION_UPDATE_FILE,
						     location);
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

	parent = rb_node_get_property_pointer (node,
					    RB_NODE_PROP_REAL_ALBUM);
	if (G_LIKELY (parent != NULL))
		rb_node_unref (parent);
	parent = rb_node_get_property_pointer (node,
					    RB_NODE_PROP_REAL_ARTIST);
	if (G_LIKELY (parent != NULL))
		rb_node_unref (parent);
	parent = rb_node_get_property_pointer (node,
					    RB_NODE_PROP_REAL_GENRE);
	if (G_LIKELY (parent != NULL))
		rb_node_unref (parent);
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

	rb_node_signal_connect_object (node, RB_NODE_DESTROY, (RBNodeCallback) finalize_node, NULL);

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
	if (error && *error)
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

	monkey_media_stream_info_get_value (info,
					    field,
					    0,
					    &val);

	rb_node_set_property (node,
			      property,
			      &val);

	g_value_unset (&val);
}

static void
set_node_title (RBNode *node, MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	char *collated, *folded;

	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    0,
					    &val);

	rb_node_set_property (node,
			      RB_NODE_PROP_NAME,
			      &val);

	folded = g_utf8_casefold (g_value_get_string (&val), -1);
	g_value_unset (&val);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);
	
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, collated);
	g_free (collated);
	rb_node_set_property (node,
			      RB_NODE_PROP_NAME_SORT_KEY,
			      &val);
	g_value_unset (&val);
}

static void
set_node_mtime (RBNode *node, const char *location)
{
	GnomeVFSFileInfo *info;
	GValue val = { 0, };

	info = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, info->mtime);

	rb_node_set_property (node,
			      RB_NODE_PROP_MTIME,
			      &val);

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
		GValue value = { 0, };
		char *folded, *key;

		genre = rb_node_new (library->priv->db);

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME,
				      &val);

		folded = g_utf8_casefold (g_value_get_string (&val), -1);
		key = g_utf8_collate_key (folded, -1);
		g_free (folded);
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, key);
		g_free (key);

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME_SORT_KEY,
				      &value);

		g_value_unset (&value);

		rb_node_add_child (rb_library_get_all_genres (library), genre);
	}
	
	if (check_reparent == FALSE)
		rb_node_ref (genre);

	rb_node_set_property (node,
			      RB_NODE_PROP_GENRE,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property (node,
			      RB_NODE_PROP_REAL_GENRE,
			      &val);
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
	char *swapped, *collated, *folded;
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
	
	artist = rb_library_get_artist_by_name (library,
						swapped);
	
	if (artist == NULL) {
		GValue swapped_val = { 0, };
		GValue value = { 0, };
		char *folded, *key;
		
		artist = rb_node_new (library->priv->db);

		g_value_init (&swapped_val, G_TYPE_STRING);
		g_value_set_string (&swapped_val, swapped);
		rb_node_set_property (artist,
				      RB_NODE_PROP_NAME,
				      &swapped_val);
		g_value_unset (&swapped_val);

		folded = g_utf8_casefold (swapped, -1);
		key = g_utf8_collate_key (folded, -1);
		g_free (folded);
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, key);
		g_free (key);

		rb_node_set_property (artist,
				      RB_NODE_PROP_NAME_SORT_KEY,
				      &value);

		g_value_unset (&value);

		new = TRUE;
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE),
				   artist);

		rb_node_ref (artist);
	}

	rb_node_set_property (node,
			      RB_NODE_PROP_ARTIST,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	folded = g_utf8_casefold (swapped, -1);
	g_free (swapped);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);
	g_value_set_string (&val, collated);
	g_free (collated);
	rb_node_set_property (node,
			      RB_NODE_PROP_ARTIST_SORT_KEY,
			      &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, artist);
	rb_node_set_property (node,
			      RB_NODE_PROP_REAL_ARTIST,
			      &val);
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
	char *collated, *folded;
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
		GValue value = { 0, };
		char *folded, *key;

		album = rb_node_new (library->priv->db);

		rb_node_set_property (album,
				      RB_NODE_PROP_NAME,
				      &val);

		folded = g_utf8_casefold (g_value_get_string (&val), -1);
		key = g_utf8_collate_key (folded, -1);
		g_free (folded);
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, key);
		g_free (key);

		rb_node_set_property (album,
				      RB_NODE_PROP_NAME_SORT_KEY,
				      &value);

		g_value_unset (&value);

		new = TRUE;
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ARTIST),
				   album);
	
		rb_node_ref (album);
	}
	
	rb_node_set_property (node,
			      RB_NODE_PROP_ALBUM,
			      &val);
		
	folded = g_utf8_casefold (g_value_get_string (&val), -1);
	g_value_unset (&val);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, collated);
	g_free (collated);
	rb_node_set_property (node,
			      RB_NODE_PROP_ALBUM_SORT_KEY,
			      &val);
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
sync_node (RBNode *node,
	   RBLibrary *library,
	   gboolean check_reparent,
	   GError **error)
{
	MonkeyMediaStreamInfo *info;
	const char *location;

	location = rb_node_get_property_string (node,
				                RB_NODE_PROP_LOCATION);
	
	info = monkey_media_stream_info_new (location, error);
	if (G_UNLIKELY (info == NULL)) {
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
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_library_get_all_songs (library), node);
		rb_node_add_child (rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_ALBUM),
				   node);
	}

	g_object_unref (G_OBJECT (info));
}

void
rb_library_update_node (RBLibrary *library,
			RBNode *node,
			GError **error)
{
	GnomeVFSFileInfo *info;
	const char *location;
	long mtime;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_LIBRARY (library));

	info = gnome_vfs_file_info_new ();
	
	location = rb_node_get_property_string (node,
						RB_NODE_PROP_LOCATION);
	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	mtime = rb_node_get_property_long (node,
				           RB_NODE_PROP_MTIME);

	if (info->mtime != mtime)
		sync_node (node, library, TRUE, error);
	
	gnome_vfs_file_info_unref (info);
}
