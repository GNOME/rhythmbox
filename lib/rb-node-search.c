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
 *
 *  !! FIXME !! should not depend on library/
 */

#include <glib.h>
#include <string.h>
#include <libgnomevfs/gnome-vfs-uri.h>

#include "rb-node.h"
#include "rb-node-search.h"
#include "rb-library.h" /* FIXME */

struct _RBNodeSearchPrivate
{
	GHashTable *table;
};

/* globals */
static GObjectClass *parent_class = NULL;

/* object funtion prototypes */
static void rb_node_search_class_init (RBNodeSearchClass *klass);
static void rb_node_search_init (RBNodeSearch *l);
static void rb_node_search_finalize (GObject *object);

static char *create_key_from_string (const char *string);
static GSList *create_combinations (const char *string);
static void free_node_list (gpointer data);

/**
 * rb_node_search_get_type: get the GObject type of the RBNodeSearch
 */
GType
rb_node_search_get_type (void)
{
	static GType rb_node_search_type = 0;

  	if (rb_node_search_type == 0)
    	{
      		static const GTypeInfo our_info =
      		{
        		sizeof (RBNodeSearchClass),
        		NULL, /* base_init */
        		NULL, /* base_finalize */
        		(GClassInitFunc) rb_node_search_class_init,
        		NULL, /* class_finalize */
        		NULL, /* class_data */
        		sizeof (RBNodeSearch),
        		0,    /* n_preallocs */
        		(GInstanceInitFunc) rb_node_search_init
      		};

      		rb_node_search_type = g_type_register_static (G_TYPE_OBJECT,
                				           "RBNodeSearch",
                                           	           &our_info, 0);
    	}

	return rb_node_search_type;
}

/**
 * rb_node_search_class_init: initialize the RBNodeSearch class
 */
static void
rb_node_search_class_init (RBNodeSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

  	parent_class = g_type_class_peek_parent (klass);

  	object_class->finalize = rb_node_search_finalize;
}

/**
 * rb_node_search_init: intialize the RBNodeSearch object
 */
static void
rb_node_search_init (RBNodeSearch *l)
{
	l->priv = g_new0 (RBNodeSearchPrivate, 1);

	l->priv->table = g_hash_table_new_full (g_str_hash, g_str_equal,
						(GDestroyNotify)g_free,
						free_node_list);
}

/**
 * rb_node_search_finalize: finalize the RBNodeSearch object
 */
static void
rb_node_search_finalize (GObject *object)
{
	RBNodeSearch *s;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_SEARCH (object));
	
   	s = RB_NODE_SEARCH (object);

	g_return_if_fail (s->priv != NULL);

	g_hash_table_destroy (s->priv->table);

	g_free (s->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * rb_node_search_new: create a new RBNodeSearch object
 */
RBNodeSearch *
rb_node_search_new (void)
{
 	RBNodeSearch *l;

	l = RB_NODE_SEARCH (g_object_new (RB_TYPE_NODE_SEARCH, NULL));

	g_return_val_if_fail (l->priv != NULL, NULL);

	return l;
}

void
rb_node_search_add_song (RBNodeSearch *search, RBNode *node)
{
	char *name_key, *artist_key, *album_key, *name;
	GSList *combinations = NULL;
	GSList **lookup = NULL;
	GSList *list = NULL;

	RBNode *artist_node, *album_node;

	artist_node = rb_node_get_grandparent (node);
	album_node  = (g_list_first (rb_node_get_parents (node)))->data;

	name         = g_strdup (rb_node_get_string_property (node, NODE_PROPERTY_NAME));
	if (name == NULL)
	{
		GnomeVFSURI *uri = gnome_vfs_uri_new (rb_node_get_string_property (node, SONG_PROPERTY_URI));
		name = gnome_vfs_uri_extract_short_name (uri);
		gnome_vfs_uri_unref (uri);
	}
	name_key     = create_key_from_string (name);
	combinations = create_combinations (name_key);
	g_free (name_key);
	g_free (name);

	if (artist_node) {
		artist_key = create_key_from_string (rb_node_get_string_property (artist_node, NODE_PROPERTY_NAME));
		combinations = g_slist_concat (create_combinations (artist_key), combinations);
		g_free (artist_key);
	}

	if (album_node) {
		album_key  = create_key_from_string (rb_node_get_string_property (album_node, NODE_PROPERTY_NAME));
		combinations = g_slist_concat (create_combinations (album_key) , combinations);
		g_free (album_key);
	}


	for (list = combinations; list != NULL; list = g_slist_next (list)) {
		lookup = g_hash_table_lookup (search->priv->table, list->data);
		if (lookup == NULL) {
			/* printf ("created entry for %s\n", list->data); */
			lookup = g_new0 (GSList *, 1);
			*lookup = NULL;
			g_hash_table_insert (search->priv->table, list->data, lookup);
		} else {
			/* printf ("used existing entry for %s\n", list->data); */
		}

		g_object_ref (G_OBJECT (node));
		*lookup = g_slist_prepend (*lookup, node);
	}
}

const GSList *
rb_node_search_run_search (RBNodeSearch *search, const char *search_string)
{
	GSList **lookup;
	char *key;

	key = create_key_from_string (search_string);
	lookup = g_hash_table_lookup (search->priv->table, key);
	g_free (key);

	if (lookup == NULL) return NULL;

	printf ("DEBUG: query for %s resulted in %d hits\n", search_string, g_slist_length (*lookup));

	return *lookup;
}

static GSList *
create_combinations (const char *string)
{
	GSList *list = NULL;
	int length;
	const char *i, *j, *next_char, *end_char, *last_char;
	char *substring;

	if (string == NULL) {
		return NULL;
	}

	last_char = string + strlen (string);

	for (i = string; i != last_char; i = g_utf8_find_next_char (i, NULL)) {
		for (j = i; j < last_char; j = next_char) {
			next_char = g_utf8_find_next_char (j, NULL);
			if (next_char == NULL) {
				end_char = last_char;
			} else {
				end_char = next_char;
			}
			length = end_char - i;
			substring = malloc ((end_char - i + 1) * sizeof (char));
			strncpy (substring, i, (end_char - i));
			substring[(end_char - i)] = '\0';

			list = g_slist_prepend (list, substring);
		}
	}

	return list;
}

static char *
create_key_from_string (const char *string)
{
	if (string == NULL) return NULL;
	return g_utf8_strdown (string, -1);
}

static void free_node_list (gpointer data)
{
	GSList **lookup = (GSList **)data;
	GSList *node;

	for (node = *lookup; node != NULL; node = g_slist_next (node)) {
		g_object_unref (G_OBJECT (node->data));
	}

	g_slist_free (*lookup);

	g_free (lookup);
}
