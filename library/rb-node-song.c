/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include "rb-node-song.h"

static RBNode *
rb_node_song_get_artist_node (RBNode *node)
{
	GList *artists, *l;

	artists = rb_node_get_grandparents (node);

	for (l = artists; l != NULL; l = g_list_next (l))
	{
		RBNode *artist = RB_NODE (l->data);

		if (rb_node_get_node_type (artist) == RB_NODE_TYPE_ARTIST)
			return artist;
	}

	return NULL;
}

char *
rb_node_song_get_genre (RBNode *node)
{
	RBNode *artist;
	GList *genres, *l;

	artist = rb_node_song_get_artist_node (node);

	genres = rb_node_get_parents (artist);

	for (l = genres; l != NULL; l = g_list_next (l))
	{
		RBNode *genre = RB_NODE (l->data);

		if (rb_node_get_node_type (genre) == RB_NODE_TYPE_GENRE)
		{
			GValue val = { 0, };
			char *ret;

			rb_node_get_property (genre,
					      RB_NODE_PROPERTY_NAME,
					      &val);

			ret = g_strdup (g_value_get_string (&val));

			g_value_unset (&val);

			return ret;
		}
	}

	return NULL;
}

char *
rb_node_song_get_artist (RBNode *node)
{
	RBNode *artist;
	GValue val = { 0, };
	char *ret;

	artist = rb_node_song_get_artist_node (node);
	if (artist == NULL)
		return NULL;

	rb_node_get_property (artist,
			      RB_NODE_PROPERTY_NAME,
			      &val);

	ret = g_strdup (g_value_get_string (&val));

	g_value_unset (&val);

	return ret;
}

char *
rb_node_song_get_album (RBNode *node)
{
	GList *albums, *l;

	albums = rb_node_get_parents (node);

	for (l = albums; l != NULL; l = g_list_next (l))
	{
		RBNode *album = RB_NODE (l->data);

		if (rb_node_get_node_type (album) == RB_NODE_TYPE_ALBUM)
		{
			GValue val = { 0, };
			char *ret;

			rb_node_get_property (album,
					      RB_NODE_PROPERTY_NAME,
					      &val);

			ret = g_strdup (g_value_get_string (&val));

			g_value_unset (&val);

			return ret;
		}
	}

	return NULL;
}
