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

#include <glib.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-ops.h>

#include "rb-library.h"
#include "rb-library-private.h"
#include "rb-node.h"

#if 0 
static void remove_node_cb (Node *node, gint level, Library *l);
#endif

/**
 * library_private_remove_song: remove a song node from the library
 */
void
library_private_remove_song (Library *l, RBNode *song)
{
	g_static_rw_lock_writer_lock (l->priv->mutex->library_data);
	{
		g_hash_table_remove (l->priv->uri_to_node,
				     rb_node_get_string_property (song, "uri"));
		
		g_hash_table_remove (l->priv->id_to_node,
				     GINT_TO_POINTER (rb_node_get_int_property (song, "id")));

		/* node_remove removes the parents as well if the last node was removed */
		/* FIXME */
		//if (0) node_remove (song, 3, 1, (ActionNotifier) remove_node_cb, l);
	} g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
}

void 
library_private_add_song (Library *l, RBNode *song, RBNode *artist, RBNode *album)
{
	guint id;
	const char *uri;

	uri = rb_node_get_string_property (song, "uri");
	id = rb_node_get_int_property (song, "id");

	/*printf ("  private_add_song on uri %s\n", uri);*/

	if (artist != NULL) rb_node_set_grandparent (song, artist);

	g_static_rw_lock_writer_lock (l->priv->mutex->library_data);
	{	
		g_hash_table_insert (l->priv->id_to_node, GINT_TO_POINTER (id), song);
		g_hash_table_insert (l->priv->uri_to_node, (char *) uri, song);
		if (album != NULL)
			rb_node_append (album, song);
		rb_node_append (l->priv->all_songs, song);
		rb_node_search_add_song (l->priv->search, song);
	} g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
}

/**
 * library_private_build_id: build a new unique node ID
 */
int 
library_private_build_id (Library *l)
{
	int val = l->priv->lastid++;
	l->priv->lastid++;
	return val;
}

void
library_private_append_node_signal (Library *l, RBNode *node, int signal_index)
{
	LibraryPrivateSignal *signal;

	signal = g_new0 (LibraryPrivateSignal, 1);
	signal->node = node;
	signal->signal_index = signal_index;

	g_mutex_lock (l->priv->mutex->nodes_to_signal);
	l->priv->nodes_to_signal = g_list_append (l->priv->nodes_to_signal, signal);
	g_mutex_unlock (l->priv->mutex->nodes_to_signal);
}

gboolean
is_node (gpointer *foo)
{
	return RB_IS_NODE (foo);
}

RBNode *
library_private_add_artist_if_needed (Library *l, const char *name)
{
	RBNode *artist;
	char *artist_name_created;

	if (name == NULL) name = "";

	g_static_rw_lock_writer_lock (l->priv->mutex->library_data);
	{	
		artist = g_hash_table_lookup (l->priv->artist_to_node, name);
		
		if (artist == NULL)
		{
			artist = rb_node_new (XML_NODE_ARTIST);
			artist_name_created = g_strdup (name);
			rb_node_set_int_property (artist, "type", LIBRARY_NODE_ARTIST);
			rb_node_set_string_property (artist, "name", artist_name_created);
			
			rb_node_append (l->priv->root, artist);
			if (artist_name_created != NULL)
				g_hash_table_insert (l->priv->artist_to_node, artist_name_created, artist);

			library_private_append_node_signal (l, artist, NODE_CREATED);
		}
	} g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);

	return artist;
}

RBNode *
library_private_add_album_if_needed (Library *l, const char *name, RBNode *artist)
{
	RBNode *album;
	char *album_name_created;

	if (name == NULL) name = "";

	g_static_rw_lock_writer_lock (l->priv->mutex->library_data);
	{	
		album = g_hash_table_lookup (l->priv->album_to_node, name);
		
		if (album == NULL)
		{
			album = rb_node_new (XML_NODE_ALBUM); 
			album_name_created = g_strdup (name);
			rb_node_set_int_property (album, "type", LIBRARY_NODE_ALBUM);
			rb_node_set_string_property (album, "name", album_name_created);
			if (album_name_created != NULL)
				g_hash_table_insert (l->priv->album_to_node, album_name_created, album);
			
			rb_node_append (artist, album);
			rb_node_append (l->priv->all_albums, album);
			library_private_append_node_signal (l, album, NODE_CREATED);
		}

		if (rb_node_has_child (artist, album) == FALSE)
			rb_node_append (artist, album);
	} g_static_rw_lock_writer_unlock (l->priv->mutex->library_data);
		
	return album;
}


#if 0 /* not used, -Werror makes this an error */
/**
 * remove_node_cb: some node was removed
 */
static void
remove_node_cb (Node *node, gint level, Library *l)
{
	const char *tmp;

	if (level > 0)
	{
		/* okay an album or artist was removed, let the objects sitting
		 * on us know */
		library_private_append_node_signal (l, node, NODE_DELETED);
	}

	/* remove the removed node from the hashes */
	switch (level)
	{
	case 2:
		tmp = rb_node_get_string_property (node, NODE_PROPERTY_NAME);
		if (tmp != NULL)
			g_hash_table_remove (l->priv->artist_to_node, tmp);
		break;
	case 1:
		tmp = rb_node_get_string_property (node, NODE_PROPERTY_NAME);
		if (tmp != NULL)
			g_hash_table_remove (l->priv->album_to_node, tmp);
		break;
	default:
		break;
	}
}
#endif
