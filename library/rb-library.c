/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <string.h>
#include <libxml/tree.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-ops.h>

#include "rb-library.h"
#include "rb-library-watcher.h"
#include "rb-node.h"
#include "eel-gconf-extensions.h"

#include "rb-library-private.h"
#include "rb-library-thread.h"

/* globals */
static GObjectClass *parent_class = NULL;

guint library_signals[LAST_SIGNAL] = { 0 };

const char *string_properties[] = {"name", "date", "genre", "comment", "codecinfo", "tracknum", NULL};
const char *int_properties[]    = {"bitrate", "filesize", "duration", "mtime", NULL};

/* object funtion prototypes */
static void library_class_init (LibraryClass *klass);
static void library_init (Library *p);
static void library_finalize (GObject *object);

/* local function prototypes */
static gboolean file_created_cb (FileWatcher *watcher, const gchar *uri, Library *l);
static void file_changed_cb (FileWatcher *watcher, const gchar *uri, Library *l);
static void file_deleted_cb (FileWatcher *watcher, const gchar *uri, Library *l);
static void library_save_artist_node (RBNode *artist, xmlNodePtr parent_node);
static void library_save_to_file (Library *l, const char *target_file);
static void library_load_song_from_xml (Library *l, xmlNodePtr node, RBNode *parent_album,
					RBNode *parent_artist);
static void library_load_album_from_xml (Library *l, xmlNodePtr node, RBNode *parent_artist);
static void library_load_artist_from_xml (Library *l, xmlNodePtr node, RBNode *root);
static void library_load_from_file (Library *l, const char *saved_library_file);
static time_t get_mtime (RBNode *node);
static void update_song (Library *l, RBNode *song);
static gboolean process_node_signals (gpointer data);

/**
 * library_get_type: get the GObject type of the playbar 
 */
GType
library_get_type (void)
{
	static GType library_type = 0;

  	if (library_type == 0)
    	{
      		static const GTypeInfo our_info =
      		{
        		sizeof (LibraryClass),
        		NULL, /* base_init */
        		NULL, /* base_finalize */
        		(GClassInitFunc) library_class_init,
        		NULL, /* class_finalize */
        		NULL, /* class_data */
        		sizeof (Library),
        		0,    /* n_preallocs */
        		(GInstanceInitFunc) library_init
      		};

      		library_type = g_type_register_static (G_TYPE_OBJECT,
                				       "Library",
                                           	       &our_info, 0);
    	}

	return library_type;
}

/**
 * library_class_init: initialize the Library class
 */
static void
library_class_init (LibraryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

  	parent_class = g_type_class_peek_parent (klass);

  	object_class->finalize = library_finalize;

	/* init signals */
	library_signals[NODE_CREATED] =
   		g_signal_new ("node_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (LibraryClass, node_created),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      rb_node_get_type ());
	library_signals[NODE_CHANGED] =
   		g_signal_new ("node_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (LibraryClass, node_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      rb_node_get_type ());
	library_signals[NODE_DELETED] =
   		g_signal_new ("node_deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (LibraryClass, node_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      rb_node_get_type ());
}

/**
 * library_init: intialize the library object
 */
static void
library_init (Library *p)
{
	p->priv = g_new0 (LibraryPrivate, 1);

	/* set library xml filename */
	p->priv->file = g_build_filename (g_get_home_dir (), GNOME_DOT_GNOME,
					  "rhythmbox", "library.rhythmbox", NULL);

	/* create root node */
	p->priv->root = rb_node_new ("Library");
	rb_node_set_int_property (p->priv->root, "type", LIBRARY_NODE_ROOT);

	/* init hashtables */
	p->priv->artist_to_node = g_hash_table_new (g_str_hash, g_str_equal);
	p->priv->album_to_node  = g_hash_table_new (g_str_hash, g_str_equal);
	p->priv->uri_to_node    = g_hash_table_new (g_str_hash, g_str_equal);
	p->priv->id_to_node     = g_hash_table_new (NULL, NULL);

	p->priv->search = rb_node_search_new ();

	p->priv->all_albums = rb_node_new (NULL);
	rb_node_set_int_property (p->priv->all_albums, "priority", TRUE);
	p->priv->all_songs = rb_node_new (NULL);
	rb_node_set_int_property (p->priv->all_songs, "priority", TRUE);

	/* make nodes to update an empty list */
	p->priv->songs_to_update = NULL;

	p->priv->lastid = 0;

	/* create mutices */
	p->priv->mutex = g_new0 (LibraryPrivateMutices, 1);
	p->priv->mutex->run_thread      = g_mutex_new ();
	p->priv->mutex->songs_to_update = g_mutex_new ();
	p->priv->mutex->nodes_to_signal = g_mutex_new ();
	p->priv->mutex->library_data    = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (p->priv->mutex->library_data);

	/* initialize the updater thread */
	p->priv->thread = g_thread_create (library_thread_main, p, TRUE, NULL);
}

/**
 * library_release_brakes: to be called when all objects are hooked up,
 * it will start emitting signals now
 */
void
library_release_brakes (Library *p)
{
	FileWatcher *watcher;

	/* load xml */
	library_load_from_file (p, p->priv->file);

	/* load watcher */
	watcher = file_watcher_new ();
	g_signal_connect (G_OBJECT (watcher), "file_created",
			  G_CALLBACK (file_created_cb), p);
	g_signal_connect (G_OBJECT (watcher), "file_changed",
			  G_CALLBACK (file_changed_cb), p);
	g_signal_connect (G_OBJECT (watcher), "file_deleted",
			  G_CALLBACK (file_deleted_cb), p);

	/* we are ready to start receiving file creation/deletion signals */
	file_watcher_release_brakes (watcher);

	g_timeout_add (100, process_node_signals, p);
}

/**
 * library_finalize: finalize the playbar object
 */
static void
library_finalize (GObject *object)
{
	Library *p;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_LIBRARY (object));
	
   	p = LIBRARY (object);

	g_return_if_fail (p->priv != NULL);

	/* save the library */
	library_save_to_file (p, p->priv->file);
	g_free (p->priv->file);

	/* cleanup */
	g_hash_table_destroy (p->priv->artist_to_node);
	g_hash_table_destroy (p->priv->album_to_node);
	g_hash_table_destroy (p->priv->uri_to_node);
	g_hash_table_destroy (p->priv->id_to_node);

	g_object_unref (G_OBJECT (p->priv->search));

	g_object_unref (G_OBJECT (p->priv->all_songs));
	g_object_unref (G_OBJECT (p->priv->all_albums));

	/* free the nodes to update list */
	g_list_free (p->priv->songs_to_update);

	g_free (p->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * library_new: create a new playbar object
 */
Library *
library_new (void)
{
 	Library *p;

	p = LIBRARY (g_object_new (TYPE_LIBRARY, NULL));

	g_return_val_if_fail (p->priv != NULL, NULL);

	return p;
}

/**
 * library_add_uri: add an uri to the library
 */
RBNode *
library_add_uri (Library *l, const gchar *uri)
{
	RBNode *song;
	gchar *s;

	g_static_rw_lock_writer_lock (l->priv->mutex->library_data);
	{
		song = g_hash_table_lookup (l->priv->uri_to_node, uri);
		if (song != NULL)
		{
			g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
			return song;
		}
		
		/* create the song node */
		s = g_strdup (uri);
		
		song = rb_node_new (XML_NODE_SONG);
		
		rb_node_set_int_property (song, NODE_PROPERTY_TYPE, LIBRARY_NODE_SONG);
		rb_node_set_string_property (song, SONG_PROPERTY_URI, s);
	} g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
	
	library_private_add_song (l, song, NULL, NULL);

	update_song (l, song);

	return song;
}

/**
 * library_search: return a node with searched for nodes as children
 */
RBNode *
library_search (Library *l, const char *search_text)
{
	RBNode *search_node;

	search_node = rb_node_new (NULL);

	rb_node_set_int_property (search_node, "is_search", TRUE);

	if (strlen (search_text) > 0)
	{
		const GSList *nodes;
		const GSList *node;

		nodes = rb_node_search_run_search (l->priv->search, search_text);
		
		for (node = nodes; node != NULL; node = g_slist_next (node))
		{
			rb_node_append (search_node, RB_NODE (node->data));
		}
	}
	else
	{
		GList *nodes;
		GList *node;
		
		nodes = rb_node_get_children (l->priv->all_songs);

		for (node = nodes; node != NULL; node = g_list_next (node))
		{
			rb_node_append (search_node, RB_NODE (node->data));
		}
	}

	return search_node;
}


/**
 * library_remove_uri: remove an uri from the library
 */
void
library_remove_uri (Library *l, const gchar *uri)
{
	RBNode *node;

	g_static_rw_lock_reader_lock (l->priv->mutex->library_data);
	{
		node = g_hash_table_lookup (l->priv->uri_to_node, uri);

		if (node == NULL) {
			g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
			return;
		}
	} g_static_rw_lock_reader_unlock (l->priv->mutex->library_data);		

	library_private_remove_song (l, node);
	g_signal_emit (G_OBJECT (l), library_signals[NODE_DELETED], 0, node);
}

/**
 * file_created_cb: a file was created, add to the library
 */
static gboolean 
file_created_cb (FileWatcher *watcher, const gchar *uri, Library *l)
{
	library_add_uri (l, uri);
	return FALSE;
}

/**
 * file_changed_cb: a file was changed, update the node and emit signal
 */
static void
file_changed_cb (FileWatcher *watcher, const gchar *uri, Library *l)
{
	RBNode *song;

	song = g_hash_table_lookup (l->priv->uri_to_node, uri);

	if (song == NULL) return;

	update_song (l, song);
}

/**
 * update_song: node props changed, update and reparent node if
 * necessary
 */
static void
update_song (Library *l, RBNode *song)
{
	g_mutex_lock (l->priv->mutex->songs_to_update);
	{
		l->priv->songs_to_update = g_list_append (l->priv->songs_to_update, song);
	} g_mutex_unlock (l->priv->mutex->songs_to_update);

	g_mutex_unlock (l->priv->mutex->run_thread);
}

/**
 * file_deleted_cb: a file deleted, remove from library
 */
static void
file_deleted_cb (FileWatcher *watcher, const gchar *uri, Library *l)
{
	library_remove_uri (l, uri);
}

/**
 * library_get_root: get the root node
 */
RBNode *
library_get_root (Library *l)
{
	return l->priv->root;
}

/**
 * library_node_from_id: lookup a node in the id hash
 */
RBNode *
library_node_from_id (Library *l, int id)
{
	return g_hash_table_lookup (l->priv->id_to_node, GINT_TO_POINTER (id));
}

static void
library_save_artist_node (RBNode *artist,
			  xmlNodePtr parent_node)
{
	rb_node_save_to_xml (artist, artist, parent_node, TRUE);
}

/**
 * Save this library to target_file
 */
static void
library_save_to_file (Library *l, const char *target_file)
{
	xmlDocPtr doc;
	GList *list;

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");
	doc->children = xmlNewDocNode (doc, NULL, "RhythmBoxLibrary", NULL);

	list = rb_node_get_children (l->priv->root);
	g_list_foreach (list, (GFunc) library_save_artist_node, doc->children);

	xmlSaveFormatFile (target_file, doc, 1);
}

/**
 * Load a song out of the current XML stream and add it to the library
 */
static void
library_load_song_from_xml (Library *l, xmlNodePtr node, RBNode *parent_album,
			    RBNode *parent_artist)
{
	RBNode *song;
	char *string_property;
	char *uri;
	char *id;
	int int_property;
	int i;
	GnomeVFSURI *vuri;
  
	uri = xmlGetProp (node, "uri");

	if (uri == NULL) return;

	/* check whether this uri exists */
	vuri = gnome_vfs_uri_new (uri);
	if (vuri == NULL || !gnome_vfs_uri_exists (vuri))
	{
		gnome_vfs_uri_unref (vuri);
		g_free (uri);
		return;
	}
	gnome_vfs_uri_unref (vuri);

	/* create a new song */
	song = rb_node_new (XML_NODE_SONG);
	rb_node_set_string_property (song, "uri", uri);

	id = xmlGetProp (node, "id");

	/* if this id is equal or greather than the ID counter, we need to
	 * update the counter */
	if (atoi (id) >= l->priv->lastid) l->priv->lastid = atoi (id) + 1;
	rb_node_set_int_property (song, "id", atoi (id));
  
	rb_node_set_int_property (song, "type", LIBRARY_NODE_SONG);

	/* load properties */
	for (i = 0; string_properties[i] != NULL; i++) 
	{
		string_property = xmlGetProp (node, string_properties[i]);
		if (string_property)
			rb_node_set_string_property (song, string_properties[i], string_property);
	}
  
	for (i = 0; int_properties[i] != NULL; i++) 
	{
		string_property = xmlGetProp (node, int_properties[i]);
		if (string_property) 
		{
			int_property = strtol (string_property, NULL, 10);
			xmlFree (string_property);
			rb_node_set_int_property (song, int_properties[i], int_property);
		}
	}

	library_private_add_song (l, song, parent_artist, parent_album);

	g_signal_emit (G_OBJECT (l), library_signals[NODE_CREATED], 0, song);

	/* check mtime */
	if (get_mtime (song) != rb_node_get_int_property (song, "mtime"))
	{
		/* file was modified */
		update_song (l, song);
	}
}

/**
 * Load an album (and all its child songs) from the current XML stream and
 * add them to the library
 */
static void
library_load_album_from_xml (Library *l, xmlNodePtr node, RBNode *parent_artist)
{
	RBNode *album = NULL;
	xmlNodePtr child;
	char *s;

	s = xmlGetProp (node, "name");

	if (s == NULL) return;

	album = library_private_add_album_if_needed (l, s, parent_artist);
	
	/* add songs */
	for (child = node->children; child != NULL; child = child->next)
		library_load_song_from_xml (l, child, album, parent_artist);

	/* if no songs were added, destroy the album FIXME */
	/*if (g_list_length (rb_node_get_children (album)) == 0)
		rb_node_remove (album, 2, 2, NULL, NULL);*/
}

/**
 * Load an artist (and all its child albums) from the current XML stream and
 * add them to the library
 */
static void
library_load_artist_from_xml (Library *l, xmlNodePtr node, RBNode *root)
{
	RBNode *artist = NULL;
	xmlNodePtr child;
	char *s;

	s = xmlGetProp (node, "name");

	if (s == NULL) return;

	artist = library_private_add_artist_if_needed (l, s);

	/* load child albums */
	for (child = node->children; child != NULL; child = child->next) 
		library_load_album_from_xml (l, child, artist);
	
	/* if none were found, remove ourself again FIXME */
	/*if (g_list_length (rb_node_get_children (artist)) == 0)
		rb_node_remove (artist, 3, 3, NULL, NULL);*/
}

/**
 * Load songs/albums/artists from saved_library_file into the library
 */
static void
library_load_from_file (Library *l, const char *saved_library_file)
{
	xmlDocPtr doc;
	xmlNodePtr child;

	doc = xmlParseFile (saved_library_file);

	if (doc == NULL) return;

	for (child = doc->children->children; child != NULL; child = child->next)
		library_load_artist_from_xml (l, child, l->priv->root);

	xmlFreeDoc (doc);
}

/**
 * get_mtime: return mtime for @node
 */
static time_t
get_mtime (RBNode *node)
{
	GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
	time_t ret;
	gnome_vfs_get_file_info (rb_node_get_string_property (node, "uri"),
				 info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	ret = info->mtime;
	gnome_vfs_file_info_unref (info);
	return ret;
}

static gboolean
process_node_signals (gpointer data)
{
	Library *l = LIBRARY (data);
	GList *node;
	LibraryPrivateSignal *signal;

	if (l->priv->nodes_to_signal == NULL) {
		return TRUE;
	}

	g_mutex_lock (l->priv->mutex->nodes_to_signal);
	{
		for (node = l->priv->nodes_to_signal; node != NULL; node = node->next) {
			signal = node->data;
			g_signal_emit (G_OBJECT (l), library_signals[signal->signal_index], 0, signal->node, NULL);
			g_free (signal);
		}
		g_list_free (l->priv->nodes_to_signal);
		l->priv->nodes_to_signal = NULL;
	} g_mutex_unlock (l->priv->mutex->nodes_to_signal);

	return TRUE;
}

/**
 * library_get_all_songs: return all song nodes
 */
RBNode *
library_get_all_songs (Library *l)
{
	return l->priv->all_songs;
}

/**
 * library_get_all_albums return all album nodes
 */
RBNode *
library_get_all_albums (Library *l)
{
	return l->priv->all_albums;
}
