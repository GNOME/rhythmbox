/* 
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "rhythmdb.h"
#include "rb-debug.h"

static void rhythmdb_tree_class_init (RhythmDBTreeClass *klass);
static void rhythmdb_tree_init (RhythmDBTree *shell_player);
static void rhythmdb_tree_finalize (GObject *object);
static void rhythmdb_tree_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rhythmdb_tree_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static RhythmDBEntry * rhythmdb_tree_entry_new (RhythmDB *db);
static void rhythmdb_tree_entry_set (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);

static void rhythmdb_tree_entry_get (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);
static void rhythmdb_tree_entry_delete (RhythmDB *db, RhythmDBEntry *entry);
static GtkTreeModel * rhythmdb_tree_do_entry_query (RhythmDB *db, RhythmDBQuery *query);
static GtkTreeModel * rhythmdb_tree_do_property_query (RhythmDB *db, guint property_id, RhythmDBQuery *query);

static void finalize_tree_property (RhythmDB *db, RhythmDBTreeProperty *prop);
static void unref_tree_property (RhythmDB *db, RhythmDBTreeProperty *prop);

typdef struct RhythmDBTreeProperty
{
	struct RhythmDBTreeProperty *parent;
	char *name;
	char *sort_key;
	GHashTable *children;
} RhythmDBTreeProperty;

#define ENTRY_VALUE (ENTRY, PROPID) (&((ENTRY)->properties[PROPID]))

typedef struct RhythmDBTreeEntry
{
	struct RhythmDBTreeProperty *parent;
	GValue properties[RHYTHMDB_NUM_PROPERTIES];
} RhythmDBTreeEntry;

struct RhythmDBTreePrivate
{
	GMemChunk *entry_memchunk;
	GMemChunk *property_memchunk;

	GPtrArray *entries;
	GHashTable *genres;

	GHashTable *locations;
};

enum
{
	PROP_0,
};

static GObjectClass *parent_class = NULL;

GType
rhythmdb_tree_get_type (void)
{
	static GType rhythmdb_tree_type = 0;

	if (rhythmdb_tree_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBTreeClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_tree_class_init,
			NULL,
			NULL,
			sizeof (RhythmDBTree),
			0,
			(GInstanceInitFunc) rhythmdb_tree_init
		};

		rhythmdb_tree_type = g_type_register_static (RHYTHMDB_TYPE,
							     "RhythmDBTree",
							     &our_info, 0);
	}

	return rhythmdb_tree_type;
}

static void
rhythmdb_tree_class_init (RhythmDBTreeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RhythmDBClass *rhythmdb_class = RHYTHMDB_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rhythmdb_tree_finalize;
	object_class->set_property = rhythmdb_tree_set_property;
	object_class->get_property = rhythmdb_tree_get_property;

	object_class->impl_entry_new = rhythmdb_tree_entry_new;
	object_class->impl_entry_set = rhythmdb_tree_entry_set;
	object_class->impl_entry_get = rhythmdb_tree_entry_get;
	object_class->impl_entry_delete = rhythmdb_tree_entry_delete;
	object_class->impl_do_entry_query = rhythmdb_tree_do_entry_query;
	object_class->impl_do_property_query = rhythmdb_tree_do_property_query;
}

static void
rhythmdb_tree_init (RhythmDBTree *db)
{
	db->priv = g_new0 (RhythmDBTreePrivate, 1);

	db->priv->entry_memchunk = g_mem_chunk_new ("RhythmDBTree entry memchunk",
						    sizeof (struct RhythmDBTreeEntry),
						    1024, G_ALLOC_AND_FREE);
	db->priv->property_memchunk = g_mem_chunk_new ("RhythmDBTree property memchunk",
						       sizeof (struct RhythmDBTreeProperty),
						       1024, G_ALLOC_AND_FREE);

	db->priv->next_id = 1;

	db->priv->entries = g_ptr_array_sized_new (1024);
	db->priv->genres = g_hash_table_new (g_str_hash, g_str_equal);
	db->priv->artists = g_hash_table_new (g_str_hash, g_str_equal);
	db->priv->albums = g_hash_table_new (g_str_hash, g_str_equal);

	db->priv->locations = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
rhythmdb_tree_finalize (GObject *object)
{
	RhythmDBTree *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE_HEADER (object));

	db = RHYTHMDB_TREE (object);

	g_return_if_fail (db->priv != NULL);

	g_ptr_array_free (db->priv->entries);
	g_hash_table_destroy (db->priv->genres);

	g_hash_table_destroy (db->priv->locations);
	g_free (db->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rhythmdb_tree_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RhythmDBTree *db = RHYTHMDB_TREE (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rhythmdb_tree_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RhythmDBTree *db = RHYTHMDB_TREE (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RhythmDBTree *
rhythmdb_tree_new (const char *name)
{
	RhythmDBTree *db = g_object_new (RHYTHMDB_TYPE_TREE_DB, "name", name, NULL);

	g_return_val_if_fail (db->priv != NULL, NULL);

	return db;
}

static RhythmDBEntry *
rhythmdb_tree_entry_new (RhythmDB *db, enum RhythmDBEntryType type)
{
	RhythmDBTree *dbtree = RHYTHMDB_TREE (db);
	struct RhythmDBTreeEntry *ret;
	struct RhythmDBTreeEntry *artist;
	struct RhythmDBTreeEntry *genre;
	guint i;

	ret = g_mem_chunk_alloc (db->priv->entry_memchunk);

	/* Initialize all the properties. */
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		g_value_init (ENTRY_VALUE (ret, i),
			      rhythmdb_get_property_type (db, propid));

		/* Set up some special default values. */
		switch (i) {
		case RHYTHMDB_PROP_TYPE:
			g_value_set_int (ENTRY_VALUE (ret, i), type);
			break;
		case RHYTHMDB_PROP_LAST_PLAYED_STR:
			g_value_set_static_string (ENTRY_VALUE (ret, i), _("Never"));
			break;
		}
	}

	/* Initialize the tree structure. */
	genre = get_or_create_genre (db, _("Unknown"));
	artist = get_or_create_artist (db, genre, _("Unknown"));
	ret->album = get_or_create_album (db, artist, _("Unknown"));

	return ret;
}

static void
rhythmdb_tree_property_new (RhythmDB *db, const char *name)
{
	RhythmDBTreeProperty *ret = g_new0 (struct RhythmDBTreeProperty, 1);
	ret->name = g_strdup (name);
	ret->sort_key = rb_get_sort_key (ret->name);
	return ret;
}

static inline RhythmDBTreeProperty *
get_or_create_genre (RhythmDB *db, const char *name)
{
	RhythmDBTreeProperty *genre = g_hash_table_lookup (db->priv->genres, name);

	if (G_UNLIKELY (genre == NULL)) {
		genre = rhythmdb_tree_property_new (name);
		genre->children = g_hash_table_new (g_str_hash, g_str_equal);
		g_hash_table_insert (db->priv->genres, name, genre);
		genre->parent = NULL;
	}

	return genre;
}

static RhythmDBTreeProperty *
get_or_create_artist (RhythmDB *db, RhythmDBTreeProperty *genre, const char *name)
{
	RhythmDBTreeProperty *artist = g_hash_table_lookup (genre->children, name);

	if (G_UNLIKELY (artist == NULL)) {
		artist = rhythmdb_tree_property_new (name);
		artist->children = g_hash_table_new (g_str_hash, g_str_equal);
		g_hash_table_insert (genre->children, name, artist);
		artist->parent = genre;
	}

	return artist;
}

static RhythmDBTreeProperty *
get_or_create_album (RhythmDB *db, RhythmDBTreeProperty *artist, const char *name)
{
	RhythmDBTreeProperty *album = g_hash_table_lookup (artist->children, name);

	if (G_UNLIKELY (album == NULL)) {
		album = rhythmdb_tree_property_new (name);
		album->children = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_hash_table_insert (artist->children, name, album);
		album->parent = artist;
	}

	return album;
}

static gboolean
indexed_string_property_differs (RhythmDBEntry *entry, guint level, guint propid,
				 const char *str)
{
	RhythmDBTreeProperty *treeprop = entry->album;
	while (--level > 0) {
		treeprop = treeprop->parent;
	}

	return strcmp (treeprop->name, str);
}

static void
remove_child (RhythmDBTreeProperty *parent, RhythmDBTreeEntry *entry)
{
	g_hash_table_remove (parent->children, entry);
	if (g_hash_table_size (parent->children) <= 0)
		destroy_tree_property (parent);
}

static void
set_entry_album (RhythmDB *db, RhythmDBTreeEntry *entry, RhythmDBTreeProperty *artist,
		 const char *name)
{
	entry->album = get_or_create_album (db, artist, name);
	g_hash_table_insert (entry->album->children, entry, NULL);
}

static void
rhythmdb_tree_entry_set (RhythmDB *db, RhythmDBEntry *entry,
			 guint propid, GValue *value)
{
	struct RhythmDBTreeEntry *tree_entry = entry;

	/* Handle special properties */
	switch (propid)
	{
	case RHYTHMDB_PROP_ALBUM:
	{
		const char *albumname = g_value_get_string (value);

		if (indexed_string_property_differs (entry, 1, RHYTHMDB_PROP_ALBUM, albumname)) {
			remove_child (entry->album, entry);

			set_entry_album (db, entry, entry->album->artist, albumname);
		} 
	}
	break;
	case RHYTHMDB_PROP_ARTIST:
	{
		const char *artistname = g_value_get_string (value);

		if (indexed_string_property_differs (entry, 2, RHYTHMDB_PROP_ARTIST, artistname)) {
			struct RhythmDBTreeProperty *new_artist;			
			struct RhythmDBTreeProperty *genre;			

			genre = entry->album->parent->parent;
			
			remove_child (entry->album, entry);
			
			new_artist = get_or_create_artist (db, genre, artistname); 

			set_entry_album (db, entry, new_artist, artistname);
		}
	}
	break;
	case RHYTHMDB_PROP_GENRE:
	{
		const char *genrename = g_value_get_string (value);

		if (indexed_string_property_differs (entry, 3, RHYTHMDB_PROP_GENRE, genrename)) {
			struct RhythmDBTreeProperty *new_genre;			
			struct RhythmDBTreeProperty *new_artist;
			char *artistname;
			char *albumname;

			artistname = g_strdup (entry->album->parent->name);
			albumname = g_strdup (entry->album->name);
			
			remove_child (entry->album, entry);

			new_genre = get_or_create_genre (db, genrename);
			new_artist = get_or_create_artist (db, new_genre, artistname);

			set_entry_album (db, entry, new_artist, albumname);
			g_free (artistname);
			g_free (albumname);
		}
	}
	break;
	default:
		g_value_reset (ENTRY_VALUE (tree_entry, propid));
		g_value_copy (value, ENTRY_VALUE (tree_entry, propid));
		break;
	}
}

static void
rhythmdb_tree_entry_get (RhythmDB *db, RhythmDBEntry *entry,
			 guint propid, GValue *value)
{
	struct RhythmDBTreeEntry *tree_entry = entry;
	g_value_copy (ENTRY_VALUE (tree_entry, propid), value);
}

static void
rhythmdb_tree_entry_delete (RhythmDB *db, RhythmDBEntry *entry)
{
	struct RhythmDBTreeEntry *tree_entry = entry;
	remove_child (entry->album, tree_entry);
	
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		g_value_unset (ENTRY_VALUE (tree_entry, propid));
	}
	g_mem_chunk_free (db->priv->entry_memchunk, tree_entry);
}

static void
destroy_tree_property (RhythmDB *db, RhythmDBTreeProperty *prop)
{
	if (prop->parent)
		destroy_tree_property (prop->parent);
	g_free (prop->name);
	g_free (prop->sort_key);
	g_ptr_array_free (prop->children);
}

typedef void (*RhythmDBTreeTraversalFunc) (RhythmDB *db, RhythmDBTreeEntry *entry, gpointer data);

struct RhythmDBTreeTraversalData
{
	RhythmDB *db;
	GPtrArray *query;
	RhythmDBTreeTraversalFunc func;
	gpointer data;
};

static gboolean
evaluate_conjunctive_subquery (RhythmDB *db, RhythmDBQuery *query, RhythmDBTreeEntry *entry)
{
	enum RhythmDBQueryType i;
	for (i = 0; i < query->len; i++) {
		struct RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (i) {
		case RHYTHMDB_QUERY_PROP_LIKE:
			if (G_VALUE_TYPE (data->val) == G_TYPE_STRING) {
				const char *stra, *strb;

				stra = g_value_get_string (ENTRY_VALUE (entry, data->propid));
				strb = g_value_get_string (data->val);
				return strstr (strb, stra);
			}
			/* Deliberately fall through here */
		case RHYTHMDB_QUERY_PROP_EQUALS:
			if (rb_gvalue_compare (ENTRY_VALUE (entry, data->propid), data->val) != 0)
				return FALSE;
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			if (rb_gvalue_compare (ENTRY_VALUE (entry, data->propid), data->val) > 0)
				return FALSE;
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			if (rb_gvalue_compare (ENTRY_VALUE (entry, data->propid), data->val) < 0)
				return FALSE;
			break;
		}
	}
	return TRUE;
}

static void
do_conjunction (RhythmDBTreeEntry *entry, gpointer unused,
		struct RhythmDBTreeTraversalData *data)
{
	/* Finally, we actually evaluate the query! */
	if (evaluate_conjunctive_subquery (data->db, data->query, entry))
		data->func (data->db, entry, data->data);
}

static void
conjunctive_query_songs (const char *name, RhythmDBTreeProperty *album,
			 struct RhythmDBTreeTraversalData *data)
{
	g_hash_table_foreach (album->children, do_conjunction, traversal_data);
}

static void
conjunctive_query_albums (const char *name, RhythmDBTreeProperty *artist,
			  struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int album_query_idx = -1;

	for (i = 0; i < query->len; i++) {
		struct RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && data->propid == RHYTHMDB_PROP_ALBUM) {
			if (album_query_idx > 0)
				return;
			album_query_idx = i;
			
		}
	}

	if (album_query_idx >= 0) {
		RhythmDBTreeProperty *album;
		struct RhythmDBQueryData *data = g_ptr_array_index (query, album_query_idx);
		
		g_ptr_array_remove_index_fast (query, album_query_idx);
		
		album = g_hash_table_lookup (artist->children, g_value_get_string (data->val));
		if (artist != NULL)
			conjunctive_query_songs (album->name, album, traversal_data);
		else
			g_hash_table_foreach (album->children, conjunctive_query_songs,
					      traversal_data);
	} else {
		g_hash_table_foreach (album->children, conjunctive_query_songs,
				      traversal_data);
	}

}

static void
conjunctive_query_artists (const char *name, RhythmDBTreeProperty *genre,
			   struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int artist_query_idx = -1;

	for (i = 0; i < query->len; i++) {
		struct RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && data->propid == RHYTHMDB_PROP_ARTIST) {
			if (artist_query_idx > 0)
				return;
			artist_query_idx = i;
			
		}
	}

	if (artist_query_idx >= 0) {
		RhythmDBTreeProperty *artist;
		struct RhythmDBQueryData *data = g_ptr_array_index (query, artist_query_idx);
		
		g_ptr_array_remove_index_fast (query, artist_query_idx);
		
		artist = g_hash_table_lookup (genre->children, g_value_get_string (data->val));
		if (artist != NULL)
			conjunctive_query_albums (artist->name, artist, traversal_data);
		else
			g_hash_table_foreach (genres->children, conjunctive_query_albums,
					      traversal_data);
	} else {
		g_hash_table_foreach (genres->children, conjunctive_query_albums,
				      traversal_data);
	}
}

static void
conjunctive_query (RhythmDB *db, GPtrArray *query, RhythmDBTreeTraversalFunc func, gpointer data)
{
	GList *subqueries;
	int genre_query_idx = -1;
	guint i;
	struct RhythmDBTreeTraversalData *traversal_data;
	
	for (i = 0; i < query->len; i++) {
		struct RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && data->propid == RHYTHMDB_PROP_GENRE) {
			/* A song can't currently have two genres.  So
			 * if we get a conjunctive query for that, we
			 * know the result must be the empty set. */
			if (genre_query_idx > 0)
				return;
			genre_query_idx = i;
			
		}
	}

	traversal_data = g_new (struct RhythmDBTreeTraversalData, 1);
	traversal_data->db = db;
	traversal_data->query = query;
	traversal_data->func = func;
	traversal_data->data = data;
	
	if (genre_query_idx >= 0) {
		RhythmDBTreeProperty *genre;
		struct RhythmDBQueryData *data = g_ptr_array_index (query, genre_query_idx);
		
		g_ptr_array_remove_index_fast (query, genre_query_idx);
		
		genre = g_hash_table_lookup (db->priv->genres, g_value_get_string (data->val));
		if (genre != NULL)
			conjunctive_query_artists (genre->name, genre, traversal_data);
		else
			g_hash_table_foreach (db->priv->genres, conjunctive_query_artists,
					      traversal_data);
	} else {
		g_hash_table_foreach (db->priv->genres, conjunctive_query_artists,
				      traversal_data);
	}

	g_free (traversal_data);
}

static GList *
split_query_by_disjunctions (RhythmDB *db, RhythmDBQuery *query)
{
	GList *conjunctions = NULL;
	guint i;
	guint last_disjunction = 0;

	for (i = 0; i < query->len; i++) {
		struct RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {
			GPtrArray *subquery = g_ptr_array_sized_new (i+1);
			guint j;

			/* Copy the subquery */
			for (j = last_disjunction; j < i; j++) {
				g_ptr_array_index (subquery, j-last_disjunction) =
					g_ptr_array_index (query, i+(j-last_disjunction));
			}

			conjunctions = g_list_prepend (conjuctions, subquery);
			last_disjunction = i+1;
		}
	}

	{
		GPtrArray *subquery = g_ptr_array_sized_new ((i+1)-last_disjunction);
		guint j;
	
		/* Copy the subquery */
		for (j = last_disjunction; j < i; j++) {
			g_ptr_array_index (subquery, j-last_disjunction) =
				g_ptr_array_index (query, i+(j-last_disjunction));
		}
		
		conjunctions = g_list_prepend (conjuctions, subquery);
	}
	return conjuctions;
}

static void
handle_entry_match (RhythmDB *db, RhythmDBTreeEntry *entry, GHashTable *set)
{
	g_hash_table_insert (set, entry, NULL);
}

static GHashTable *
build_entry_query_set (RhythmDB *db, RhythmDBQuery *query)
{
	GHashTable *result_set;
	GList *conjuctions, *tem;

	result_set = g_hash_table_new (g_direct_hash, g_direct_equal);
	conjuctions = split_query_by_disjunctions (db, query);

	for (tem = conjuctions; tem; tem = tem->next)
		conjunctive_query (db, tem->data, handle_entry_match, result_set);

	g_list_free (conjunctions);
	return result_set;
}

static GtkTreeModel *
rhythmdb_tree_do_entry_query (RhythmDB *db, RhythmDBQuery *query)
{
	GHashTable *result_set;

	result_set = build_entry_query_set (db, query);

	return GTK_TREE_MODEL (rhythmdb_entry_model_new_from_hash (result_set));
}

struct RhythmDBTreePropertyGatheringData
{
	guint prop_id;
	GHashTable *set;
};

static void
gather_property (RhythmDBTreeEntry *entry, gpointer unused,
		 struct RhythmDBTreePropertyGatheringData *data)
{
	g_hash_table_insert (data->set, ENTRY_VALUE (entry, data->prop_id), NULL);
}

static GHashTable *
gather_property_set (RhythmDB *db, guint prop_id, GHashTable *table)
{
	GHashTable *ret;
	struct RhythmDBTreePropertyGatheringData *data;

	ret = g_hash_table_new (g_direct_hash, g_direct_equal);
	
	data = g_new (struct RhythmDBTreePropertyGatheringData, 1);
	data->prop_id = prop_id;
	data->set = ret;

	g_hash_table_foreach (table, gather_property, data);

	g_free (data);
	return ret;
}

static void
handle_entry_match (RhythmDB *db, RhythmDBTreeEntry *entry, GHashTable *set)
{
	g_hash_table_insert (set, entry, NULL);
}


static GtkTreeModel *
rhythmdb_tree_do_property_query (RhythmDB *db, guint property_id, RhythmDBQuery *query)
{
	GHashTable *result_set;
	
	switch (property_id)
	{
	case RHYTHMDB_PROP_GENRE:
	case RHYTHMDB_PROP_ARTIST:
	case RHYTHMDB_PROP_ALBUM:
		break;
		
	default:
		g_assert_not_reached ();
		break;
	}
	
}

void
rhythmdb_tree_do_full_query (RhythmDB *db,
			     GPtrArray *query,
			     GtkTreeModel **main_model,
			     GtkTreeModel **genre_model,
			     GtkTreeModel **artist_model,
			     GtkTreeModle **album_model)
{
	

}

