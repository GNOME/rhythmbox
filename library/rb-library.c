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
#include <monkey-media.h>
#include <gtk/gtkmain.h>
#include <unistd.h>
#include <string.h>

#include "rb-library.h"
#include "rb-library-watcher.h"
#include "rb-node-song.h"
#include "rb-debug.h"

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_load (RBLibrary *library);
static void rb_library_save (RBLibrary *library);
static void rb_library_create_skels (RBLibrary *library);
static void rb_library_update_node (RBLibrary *library,
			            RBNode *node);
static void rb_library_file_created_cb (RBLibraryWatcher *watcher,
			                const char *file,
			                RBLibrary *library);
static void rb_library_file_changed_cb (RBLibraryWatcher *watcher,
			                const char *file,
			                RBLibrary *library);
static void rb_library_file_deleted_cb (RBLibraryWatcher *watcher,
			                const char *file,
			                RBLibrary *library);
static void rb_library_node_destroyed_cb (RBNode *node,
			                  RBLibrary *library);
static gboolean rb_library_timeout_cb (RBLibrary *library);
static gpointer rb_library_thread_main (RBLibraryPrivate *library);
static void rb_library_thread_check_died (RBLibraryPrivate *priv);
static void rb_library_thread_process_new_song (RBLibraryPrivate *priv,
				                char *file);
static void rb_library_thread_process_changed_node (RBLibraryPrivate *priv,
				                    RBNode *node);

struct RBLibraryPrivate
{
	RBLibraryWatcher *watcher;

	RBNode *all_genres;
	RBNode *all_artists;
	RBNode *all_albums;
	RBNode *all_songs;

	char *xml_file;

	GHashTable *genre_to_node;
	GHashTable *artist_to_node;
	GHashTable *album_to_node;
	GHashTable *file_to_node;

	GMutex *new_files_lock;
	GList *new_files;

	GMutex *changed_nodes_lock;
	GList *changed_nodes;

	GMutex *new_nodes_lock;
	GList *new_nodes;

	GMutex *thread_lock;
	GThread *thread;

	guint timeout;

	gboolean dead;
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
	GList *files, *l;

	rb_debug ("rb_library_init: starting");
	
	library->priv = g_new0 (RBLibraryPrivate, 1);
	
	library->priv->genre_to_node  = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       (GDestroyNotify) g_free,
							       NULL);
	library->priv->artist_to_node = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       (GDestroyNotify) g_free,
							       NULL);
	library->priv->album_to_node  = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       (GDestroyNotify) g_free,
							       NULL);
	library->priv->file_to_node   = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       (GDestroyNotify) g_free,
							       NULL);

	library->priv->thread_lock = g_mutex_new ();
	library->priv->new_files_lock = g_mutex_new ();
	library->priv->changed_nodes_lock = g_mutex_new ();
	library->priv->new_nodes_lock = g_mutex_new ();

	library->priv->xml_file = g_build_filename (g_get_home_dir (),
						    GNOME_DOT_GNOME,
						    "rhythmbox",
						    "library.xml",
						    NULL);

	rb_debug ("rb_library_init: stage 1 completed");
	rb_library_load (library);
	rb_debug ("rb_library_init: stage 2 completed");

	library->priv->watcher = rb_library_watcher_new ();

	rb_debug ("rb_library_init: .. just verifying .. ");

	g_signal_connect (G_OBJECT (library->priv->watcher),
			  "file_created",
			  G_CALLBACK (rb_library_file_created_cb),
			  library);
	g_signal_connect (G_OBJECT (library->priv->watcher),
			  "file_changed",
			  G_CALLBACK (rb_library_file_changed_cb),
			  library);
	g_signal_connect (G_OBJECT (library->priv->watcher),
			  "file_deleted",
			  G_CALLBACK (rb_library_file_deleted_cb),
			  library);

	rb_debug ("rb_library_init: stage 2.5 ;-)");

	files = rb_library_watcher_get_files (library->priv->watcher);
	for (l = files; l != NULL; l = g_list_next (l))
	{
		rb_library_add_file (library, (char *) l->data);
	}
	g_list_foreach (files, (GFunc) g_free, NULL);
	g_list_free (files);

	rb_debug ("rb_library_init: stage 3 completed");

	library->priv->timeout = g_timeout_add (10, (GSourceFunc) rb_library_timeout_cb, library);

	library->priv->thread = g_thread_create ((GThreadFunc) rb_library_thread_main,
						 library->priv, TRUE, NULL);

	rb_debug ("rb_library_init: finished");
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;
	GList *children, *l;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	rb_debug ("rb_library_finalize: waiting for locks");
	g_mutex_lock (library->priv->thread_lock);
	rb_debug ("rb_library_finalize: obtained locks, attempting to kill thread");

	g_source_remove (library->priv->timeout);

	rb_library_save (library);

	/* unref all songs. this will set a nice chain of recursive unrefs in motion */
	children = g_list_copy (rb_node_get_children (library->priv->all_songs));
	for (l = children; l != NULL; l = g_list_next (l))
	{
		rb_debug ("rb_library_finalize: going to unref a song");
		g_object_unref (G_OBJECT (l->data));
		rb_debug ("rb_library_finalize: unrefed a song");
	}
	g_list_free (children);
	rb_debug ("rb_library_finalize: done unreffing songs");

	g_object_unref (G_OBJECT (library->priv->watcher));

	g_free (library->priv->xml_file);

	g_hash_table_destroy (library->priv->genre_to_node);
	g_hash_table_destroy (library->priv->artist_to_node);
	g_hash_table_destroy (library->priv->album_to_node);
	g_hash_table_destroy (library->priv->file_to_node);

	library->priv->dead = TRUE;

	rb_debug ("rb_library_finalize: releasing locks");
	g_mutex_unlock (library->priv->thread_lock);
	rb_debug ("rb_library_finalize: released locks, continueing finalize");

	/* RBLibraryPrivate gets freed by the thread */

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

void
rb_library_add_file (RBLibrary *library,
		     const char *file)
{
	if (file == NULL)
		return;

	if (g_hash_table_lookup (library->priv->file_to_node, file) != NULL)
		return;

	rb_debug ("rb_library_add_file: waiting lock");
	g_mutex_lock (library->priv->new_files_lock);
	rb_debug ("rb_library_add_file: obtained lock");

	library->priv->new_files = g_list_append (library->priv->new_files, g_strdup (file));

	rb_debug ("rb_library_add_file: releasing lock");
	g_mutex_unlock (library->priv->new_files_lock);
	rb_debug ("rb_library_add_file: released lock");
}

static void
rb_library_update_node (RBLibrary *library,
			RBNode *node)
{
	rb_debug ("rb_library_update_node: waiting lock");
	g_mutex_lock (library->priv->changed_nodes_lock);
	rb_debug ("rb_library_update_node: obtained lock");
	
	library->priv->changed_nodes = g_list_append (library->priv->changed_nodes, node);

	rb_debug ("rb_library_update_node: releasing lock");
	g_mutex_unlock (library->priv->changed_nodes_lock);
	rb_debug ("rb_library_update_node: released lock");
}

void
rb_library_remove_node (RBLibrary *library,
			RBNode *node)
{
	g_object_unref (G_OBJECT (node));
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
rb_library_create_skels (RBLibrary *library)
{
	/* create a boostrap setup, used if no xml stuff could be loaded */
	GValue value = { 0, };

	library->priv->all_genres  = rb_node_new (RB_NODE_TYPE_ALL_GENRES);
	library->priv->all_artists = rb_node_new (RB_NODE_TYPE_ALL_ARTISTS);
	library->priv->all_albums  = rb_node_new (RB_NODE_TYPE_ALL_ALBUMS);
	library->priv->all_songs   = rb_node_new (RB_NODE_TYPE_ALL_SONGS);
	
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, _("All"));
	rb_node_set_property (library->priv->all_genres,
			      RB_NODE_PROPERTY_NAME,
			      &value);
	rb_node_set_property (library->priv->all_artists,
			      RB_NODE_PROPERTY_NAME,
			      &value);
	rb_node_set_property (library->priv->all_albums,
			      RB_NODE_PROPERTY_NAME,
			      &value);
	rb_node_set_property (library->priv->all_songs,
			      RB_NODE_PROPERTY_NAME,
			      &value);
	g_value_unset (&value);

	rb_node_add_child (library->priv->all_genres,
			   library->priv->all_artists);
	rb_node_add_child (library->priv->all_artists,
			   library->priv->all_albums);
	rb_node_add_child (library->priv->all_albums,
			   library->priv->all_songs);
}

static void
rb_library_load (RBLibrary *library)
{
	xmlDocPtr doc;
	xmlNodePtr child;

	if (g_file_test (library->priv->xml_file, G_FILE_TEST_EXISTS) == FALSE)
	{
		rb_library_create_skels (library);
		return;
	}
	
	doc = xmlParseFile (library->priv->xml_file);

	if (doc == NULL)
	{
		rb_library_create_skels (library);
		return;
	}

	for (child = doc->children->children; child != NULL; child = child->next)
	{
		RBNode *node;
		RBNodeType type;
		GValue value = { 0, };
		
		node = rb_node_new_from_xml (child);
		if (node == NULL)
			continue;
		
		type = rb_node_get_node_type (node);
		switch (type)
		{
		case RB_NODE_TYPE_ALL_GENRES:
			library->priv->all_genres = node;
			break;
		case RB_NODE_TYPE_ALL_ARTISTS:
			library->priv->all_artists = node;
			break;
		case RB_NODE_TYPE_ALL_ALBUMS:
			library->priv->all_albums = node;
			break;
		case RB_NODE_TYPE_ALL_SONGS:
			library->priv->all_songs = node;
			break;
		case RB_NODE_TYPE_GENRE:
			rb_node_get_property (node, RB_NODE_PROPERTY_NAME,
					      &value);
			g_hash_table_insert (library->priv->genre_to_node,
					     g_strdup (g_value_get_string (&value)), node);
			g_value_unset (&value);
			break;
		case RB_NODE_TYPE_ARTIST:
			rb_node_get_property (node, RB_NODE_PROPERTY_NAME,
					      &value);
			g_hash_table_insert (library->priv->artist_to_node,
					     g_strdup (g_value_get_string (&value)), node);
			g_value_unset (&value);
			break;
		case RB_NODE_TYPE_ALBUM:
			rb_node_get_property (node, RB_NODE_PROPERTY_NAME,
					      &value);
			g_hash_table_insert (library->priv->album_to_node,
					     g_strdup (g_value_get_string (&value)), node);
			g_value_unset (&value);
			break;
		case RB_NODE_TYPE_SONG:
			rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION,
					      &value);
			g_hash_table_insert (library->priv->file_to_node,
					     g_strdup (g_value_get_string (&value)), node);
			g_value_unset (&value);

			rb_node_get_property (node, RB_NODE_PROPERTY_SONG_MTIME, &value);
			if (g_value_get_long (&value) != rb_node_song_get_real_mtime (node))
			{
				g_mutex_lock (library->priv->changed_nodes_lock);
				library->priv->changed_nodes = g_list_append (library->priv->changed_nodes,
									      node);
				g_mutex_unlock (library->priv->changed_nodes_lock);
			}
			g_value_unset (&value);
			
			break;
		default:
			break;
		}

		g_signal_connect_object (G_OBJECT (node),
					 "destroyed",
					 G_CALLBACK (rb_library_node_destroyed_cb),
					 G_OBJECT (library),
					 0);
	}

	xmlFreeDoc (doc);

	if (library->priv->all_genres == NULL)
		rb_library_create_skels (library);
}

static void
rb_library_save (RBLibrary *library)
{
	xmlDocPtr doc;
	GList *children, *l;
	
	/* save nodes to xml */
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");
	doc->children = xmlNewDocNode (doc, NULL, "RBLibrary", NULL);

	rb_node_save_to_xml (library->priv->all_genres, doc->children);
	children = rb_node_get_children (library->priv->all_genres);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_artists)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}

	rb_node_save_to_xml (library->priv->all_artists, doc->children);
	children = rb_node_get_children (library->priv->all_artists);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_albums)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}

	rb_node_save_to_xml (library->priv->all_albums, doc->children);
	children = rb_node_get_children (library->priv->all_albums);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_songs)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}

	rb_node_save_to_xml (library->priv->all_songs, doc->children);
	children = rb_node_get_children (library->priv->all_songs);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}

	xmlSaveFormatFile (library->priv->xml_file, doc, 1);
}

static void
rb_library_file_created_cb (RBLibraryWatcher *watcher,
			    const char *file,
			    RBLibrary *library)
{
	rb_library_add_file (library, file);
}

static void
rb_library_file_changed_cb (RBLibraryWatcher *watcher,
			    const char *file,
			    RBLibrary *library)
{
	RBNode *node = g_hash_table_lookup (library->priv->file_to_node,
					    file);

	if (node != NULL)
		rb_library_update_node (library, node);
}

static void
rb_library_file_deleted_cb (RBLibraryWatcher *watcher,
			    const char *file,
			    RBLibrary *library)
{
	RBNode *node = g_hash_table_lookup (library->priv->file_to_node,
					    file);

	if (node != NULL)
		rb_library_remove_node (library, node);
}

static void
rb_library_node_destroyed_cb (RBNode *node,
			      RBLibrary *library)
{
	switch (rb_node_get_node_type (node))
	{
	case RB_NODE_TYPE_GENRE:
		g_hash_table_remove (library->priv->genre_to_node, node);
		break;
	case RB_NODE_TYPE_ARTIST:
		g_hash_table_remove (library->priv->artist_to_node, node);
		break;
	case RB_NODE_TYPE_ALBUM:
		g_hash_table_remove (library->priv->album_to_node, node);
		break;
	case RB_NODE_TYPE_SONG:
		g_hash_table_remove (library->priv->file_to_node, node);
		break;
	default:
		break;
	}
}

static gboolean
rb_library_timeout_cb (RBLibrary *library)
{
	GList *list;
	RBNode *node;
	char *artist, *album, *genre, *file;
	RBNode *genre_node, *artist_node, *album_node;
	GValue value = { 0, };

	if (library->priv->new_nodes == NULL)
		return TRUE;

	rb_debug ("rb_library_timeout_cb: waiting lock");
	g_mutex_lock (library->priv->new_nodes_lock);
	rb_debug ("rb_library_timeout_cb: obtained lock");
		
	list = g_list_first (library->priv->new_nodes);
	node = RB_NODE (list->data);
	library->priv->new_nodes = g_list_remove (library->priv->new_nodes, node);

	rb_debug ("rb_library_timeout_cb: releasing lock");
	g_mutex_unlock (library->priv->new_nodes_lock);
	rb_debug ("rb_library_timeout_cb: released lock");

	rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &value);
	file = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	rb_debug ("rb_library_timeout_cb: going to insert %s into the library", file);

	genre  = g_object_get_data (G_OBJECT (node), "genre");
	if (genre == NULL)
		genre = g_strdup ("");
	artist = g_object_get_data (G_OBJECT (node), "artist");
	if (artist == NULL)
		artist = g_strdup ("");
	album  = g_object_get_data (G_OBJECT (node), "album");
	if (album == NULL)
		album = g_strdup ("");

	if (g_hash_table_lookup (library->priv->file_to_node, file) != NULL)
	{
		g_object_unref (G_OBJECT (node));
		g_free (genre);
		g_free (artist);
		g_free (album);
		g_free (file);
		return TRUE;
	}
	
	g_hash_table_insert (library->priv->file_to_node, file, node);

	genre_node  = g_hash_table_lookup (library->priv->genre_to_node,
					   genre);
	artist_node = g_hash_table_lookup (library->priv->artist_to_node,
					   artist);
	album_node  = g_hash_table_lookup (library->priv->album_to_node,
					   album);

	if (genre_node == NULL)
	{
		genre_node = rb_node_new (RB_NODE_TYPE_GENRE);
		
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, genre);
		rb_node_set_property (genre_node, RB_NODE_PROPERTY_NAME, &value);
		g_value_unset (&value);

		rb_node_add_child (genre_node, library->priv->all_artists);
		rb_node_add_child (library->priv->all_genres, genre_node);
		
		g_hash_table_insert (library->priv->genre_to_node, g_strdup (genre), genre_node);
	}

	if (artist_node == NULL)
	{
		artist_node = rb_node_new (RB_NODE_TYPE_ARTIST);
		
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, artist);
		rb_node_set_property (artist_node, RB_NODE_PROPERTY_NAME, &value);
		g_value_unset (&value);

		rb_node_add_child (genre_node, artist_node);
		rb_node_add_child (artist_node, library->priv->all_songs);
		rb_node_add_child (library->priv->all_artists, artist_node);
		
		g_hash_table_insert (library->priv->artist_to_node, g_strdup (artist), artist_node);
	}

	if (rb_node_has_child (genre_node, artist_node) == FALSE)
		rb_node_add_child (genre_node, artist_node);

	if (album_node == NULL)
	{
		album_node = rb_node_new (RB_NODE_TYPE_ALBUM);
		
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, album);
		rb_node_set_property (album_node, RB_NODE_PROPERTY_NAME, &value);
		g_value_unset (&value);

		rb_node_add_child (artist_node, album_node);
		rb_node_add_child (library->priv->all_albums, album_node);
		
		g_hash_table_insert (library->priv->album_to_node, g_strdup (album), album_node);
	}

	if (rb_node_has_child (artist_node, album_node) == FALSE)
		rb_node_add_child (artist_node, album_node);

	rb_node_add_grandparent (node, artist_node);
	rb_node_add_grandparent (node, library->priv->all_albums);
	rb_node_add_child (album_node, node);
	rb_node_add_child (library->priv->all_songs, node);

	g_signal_connect_object (G_OBJECT (node),
				 "destroyed",
				 G_CALLBACK (rb_library_node_destroyed_cb),
				 G_OBJECT (library),
				 0);

	g_free (genre);
	g_free (artist);
	g_free (album);
	
	return TRUE;
}

static void
rb_library_thread_check_died (RBLibraryPrivate *priv)
{
	if (priv->dead == TRUE)
	{
		rb_debug ("rb_library_thread_check_died: freeing new_files_lock");
		g_mutex_free (priv->new_files_lock);
		rb_debug ("rb_library_thread_check_died: freeing changed_nodes_lock");
		g_mutex_free (priv->changed_nodes_lock);
		rb_debug ("rb_library_thread_check_died: freeing new_nodes_lock");
		g_mutex_free (priv->new_nodes_lock);

		g_list_free (priv->new_files);
		g_list_free (priv->changed_nodes);
		g_list_free (priv->new_nodes);

		g_mutex_unlock (priv->thread_lock);
		g_mutex_free (priv->thread_lock);

		rb_debug ("rb_library_thread_check_died: freeing RBLibraryPrivate");
		g_free (priv);
		g_thread_exit (NULL);
	}
}

static void
rb_library_thread_process_new_song (RBLibraryPrivate *priv,
				    char *file)
{
	RBNode *node;
	GValue value = { 0, };
	MonkeyMediaStreamInfo *info;
	GError *err = NULL;

	rb_debug ("rb_library_thread_process_new_song: waiting lock #1");
	g_mutex_lock (priv->new_files_lock);
	rb_debug ("rb_library_thread_process_new_song: obtained lock #1");
	priv->new_files = g_list_remove (priv->new_files, file);
	rb_debug ("rb_library_thread_process_new_song: releasing lock #1");
	g_mutex_unlock (priv->new_files_lock);
	rb_debug ("rb_library_thread_process_new_song: released lock #1");

	info = monkey_media_stream_info_new (file, &err);
	if (err != NULL)
	{
		g_error_free (err);
		g_free (file);
		return;
	}

	node = rb_node_new (RB_NODE_TYPE_SONG);
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, file);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &value);
	rb_debug ("rb_library_thread_process_new_song: set uri of song to %s", file);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_TRACK_NUMBER, &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_DURATION, &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_FILE_SIZE, &value);
	g_value_unset (&value);
	
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_NAME, &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, rb_node_song_get_real_mtime (node));
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_MTIME, &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE, &value);
	g_object_set_data (G_OBJECT (node), "genre", g_strdup (g_value_get_string (&value)));
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST, &value);
	g_object_set_data (G_OBJECT (node), "artist", g_strdup (g_value_get_string (&value)));
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM, &value);
	g_object_set_data (G_OBJECT (node), "album", g_strdup (g_value_get_string (&value)));
	g_value_unset (&value);

	rb_debug ("rb_library_thread_process_new_song: waiting lock #2");
	g_mutex_lock (priv->new_nodes_lock);
	rb_debug ("rb_library_thread_process_new_song: obtained lock #2");
	priv->new_nodes = g_list_append (priv->new_nodes, node);
	rb_debug ("rb_library_thread_process_new_song: releasing lock #2");
	g_mutex_unlock (priv->new_nodes_lock);
	rb_debug ("rb_library_thread_process_new_song: released lock #2");

	g_object_unref (G_OBJECT (info));
	g_free (file);
}

static void
rb_library_thread_process_changed_node (RBLibraryPrivate *priv,
				        RBNode *node)
{
	MonkeyMediaStreamInfo *info;
	GError *error = NULL;
	GValue value = { 0, };
	char *node_prop;
	gboolean need_reparent;

	rb_debug ("rb_library_thread_process_changed_node: waiting lock #1");
	g_mutex_lock (priv->changed_nodes_lock);
	rb_debug ("rb_library_thread_process_changed_node: obtained lock #1");
	priv->changed_nodes = g_list_remove (priv->changed_nodes, node);
	rb_debug ("rb_library_thread_process_changed_node: releasing lock #1");
	g_mutex_unlock (priv->changed_nodes_lock);
	rb_debug ("rb_library_thread_process_changed_node: released lock #1");

	rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &value);
	info = monkey_media_stream_info_new (g_value_get_string (&value), &error);
	if (error != NULL)
	{
		g_value_unset (&value);
		g_error_free (error);
		return;
	}
	g_value_unset (&value);

	/* check whether album/artist/genre differ, if any of them do we delete the node
	 * and put it into the add queue */

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE, &value);
	node_prop = rb_node_song_get_genre (node);
	need_reparent = (strcmp (node_prop, g_value_get_string (&value)) != 0);
	g_value_unset (&value);
	g_free (node_prop);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST, &value);
	node_prop = rb_node_song_get_artist (node);
	need_reparent = (strcmp (node_prop, g_value_get_string (&value)) != 0);
	g_value_unset (&value);
	g_free (node_prop);
	
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM, &value);
	node_prop = rb_node_song_get_album (node);
	need_reparent = (strcmp (node_prop, g_value_get_string (&value)) != 0);
	g_value_unset (&value);
	g_free (node_prop);

	if (need_reparent == TRUE)
	{
		char *file;

		rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &value);
		file = g_strdup (g_value_get_string (&value));
		g_value_unset (&value);

		g_object_unref (G_OBJECT (node));
		
		g_mutex_lock (priv->new_files_lock);
		priv->new_files = g_list_append (priv->new_files, file);
		g_mutex_unlock (priv->new_files_lock);

		return;
	}

	/* sync "innocent" tags */
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_TRACK_NUMBER, &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_DURATION, &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_FILE_SIZE, &value);
	g_value_unset (&value);
	
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE, &value);
	rb_node_set_property (node, RB_NODE_PROPERTY_NAME, &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, rb_node_song_get_real_mtime (node));
	rb_node_set_property (node, RB_NODE_PROPERTY_SONG_MTIME, &value);
	g_value_unset (&value);
}

static gpointer
rb_library_thread_main (RBLibraryPrivate *priv)
{

	while (TRUE)
	{
		g_mutex_lock (priv->thread_lock);
		rb_library_thread_check_died (priv);

		if (priv->new_files != NULL)
		{
			GList *list;
			char *file;
			
			list = g_list_first (priv->new_files);
			file = (char *) list->data;
			rb_library_thread_process_new_song (priv, file);
		}

		if (priv->changed_nodes != NULL)
		{
			GList *list;
			RBNode *node;

			list = g_list_first (priv->changed_nodes);
			node = RB_NODE (list->data);
			rb_library_thread_process_changed_node (priv, node);
		}

		g_mutex_unlock (priv->thread_lock);

		g_usleep (10);
	}

	return NULL;
}
