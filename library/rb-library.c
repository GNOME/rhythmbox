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
#include <unistd.h>
#include <string.h>

#include "rb-library.h"
#include "rb-library-walker-thread.h"
#include "rb-library-main-thread.h"
#include "rb-library-action-queue.h"
#include "rb-node-song.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_save (RBLibrary *library);
static void rb_library_create_skels (RBLibrary *library);
static void rb_library_load (RBLibrary *library);

struct RBLibraryPrivate
{
	RBLibraryWalkerThread *walker_thread;
	RBLibraryMainThread *main_thread;

	RBLibraryActionQueue *walker_queue;
	RBLibraryActionQueue *main_queue;

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

static GObjectClass *parent_class = NULL;

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
}

static void
rb_library_init (RBLibrary *library)
{
	rb_node_system_init ();

	/* ensure these types have been registered: */
	rb_node_get_type ();
	rb_node_song_get_type ();
	
	library->priv = g_new0 (RBLibraryPrivate, 1);

	library->priv->xml_file = g_build_filename (rb_dot_dir (),
						    "library.xml",
						    NULL);

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

void
rb_library_release_brakes (RBLibrary *library)
{
	/* and off we go */
	rb_library_load (library);

	/* create these after having loaded the xml to avoid extra loading time */
	library->priv->main_thread = rb_library_main_thread_new (library);

	library->priv->walker_thread = rb_library_walker_thread_new (library);
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;
	GPtrArray *children;
	int i;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	GDK_THREADS_LEAVE (); /* be sure the main thread is able to finish */
	g_object_unref (G_OBJECT (library->priv->main_thread));
	GDK_THREADS_ENTER ();
	g_object_unref (G_OBJECT (library->priv->walker_thread));
	g_object_unref (G_OBJECT (library->priv->main_queue));
	g_object_unref (G_OBJECT (library->priv->walker_queue));
	
	rb_library_save (library);

	/* unref all songs. this will set a nice chain of recursive unrefs in motion */
	children = rb_node_get_children (library->priv->all_songs);
	rb_node_thaw (library->priv->all_songs);
	for (i = children->len - 1; i >= 0; i--) {
		rb_node_unref (g_ptr_array_index (children, i));
	}
	
	rb_node_unref (library->priv->all_songs);
	rb_node_unref (library->priv->all_albums);
	rb_node_unref (library->priv->all_artists);
	rb_node_unref (library->priv->all_genres);

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

	rb_node_system_shutdown ();

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
	rb_node_unref (RB_NODE (node));
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
			     (char *) rb_node_get_property_string (child, RB_NODE_SONG_PROP_LOCATION),
			     child);
	
	g_static_rw_lock_writer_unlock (library->priv->song_hash_lock);
}

static void
genre_removed_cb (RBNode *node,
		  RBNode *child,
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
		 RBLibrary *library)
{
	g_static_rw_lock_writer_lock (library->priv->song_hash_lock);

	g_hash_table_remove (library->priv->song_hash,
			     rb_node_get_property_string (child, RB_NODE_SONG_PROP_LOCATION));
	
	g_static_rw_lock_writer_unlock (library->priv->song_hash_lock);
}

static void
rb_library_create_skels (RBLibrary *library)
{
	/* create a boostrap setup */
	GValue value = { 0, };

	library->priv->all_genres  = rb_node_new ();
	library->priv->all_artists = rb_node_new ();
	library->priv->all_albums  = rb_node_new ();
	library->priv->all_songs   = rb_node_new ();

	g_signal_connect_object (G_OBJECT (library->priv->all_genres),
				 "child_added",
				 G_CALLBACK (genre_added_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_artists),
				 "child_added",
				 G_CALLBACK (artist_added_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_albums),
				 "child_added",
				 G_CALLBACK (album_added_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_songs),
				 "child_added",
				 G_CALLBACK (song_added_cb),
				 G_OBJECT (library),
				 0);

	g_signal_connect_object (G_OBJECT (library->priv->all_genres),
				 "child_removed",
				 G_CALLBACK (genre_removed_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_artists),
				 "child_removed",
				 G_CALLBACK (artist_removed_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_albums),
				 "child_removed",
				 G_CALLBACK (album_removed_cb),
				 G_OBJECT (library),
				 0);
	g_signal_connect_object (G_OBJECT (library->priv->all_songs),
				 "child_removed",
				 G_CALLBACK (song_removed_cb),
				 G_OBJECT (library),
				 0);

	rb_node_ref (library->priv->all_genres);
	rb_node_ref (library->priv->all_artists);
	rb_node_ref (library->priv->all_albums);
	rb_node_ref (library->priv->all_songs);

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

	xmlSaveFile (library->priv->xml_file, doc);
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
	if (G_OBJECT_TYPE (node) == RB_TYPE_NODE_SONG)
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
		
		node = rb_node_new_from_xml (child);

		if (RB_IS_NODE_SONG (node))
		{
			const char *location;

			location = rb_node_get_property_string (node,
						                RB_NODE_SONG_PROP_LOCATION);
					
			rb_library_action_queue_add (library->priv->main_queue,
						     FALSE,
						     RB_LIBRARY_ACTION_UPDATE_FILE,
						     location);
		}
	}

	rb_profiler_dump (p);
	rb_profiler_free (p);

	xmlFreeDoc (doc);
}
