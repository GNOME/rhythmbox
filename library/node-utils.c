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

#include <libgnomevfs/gnome-vfs.h>
#include <string.h>

#include "node-utils.h"
#include "rb-library.h"

Node *
song_get_artist (Node *node)
{
	return node_get_grandparent (node);
}

Node *
rb_nosong_get_album (Node *node)
{
	GList *parents, *l;
	parents = g_list_first (node_get_parents (node));
	for (l = parents; l != NULL; l = g_list_next(l))
	{
		if (node_get_string_property (NODE (l->data), NODE_PROPERTY_NAME) != NULL)
			return NODE (l->data);
	}
	return NULL;
}

GSList *
node_get_child_songs (Node *node)
{
	GSList *song_list = NULL;
	GList *list_node;

	if (node == NULL) {
		return NULL;
	} else if (node_get_int_property (node, NODE_PROPERTY_TYPE) == LIBRARY_NODE_SONG) {
		song_list = g_slist_append (song_list, node);
	} else {
		for (list_node = node_get_children (node); list_node != NULL; list_node = g_list_next (list_node)) {
			song_list = g_slist_concat (node_get_child_songs (list_node->data), song_list);
		}
	}

	return song_list;
}
