/* 
 *  arch-tag: Implementation of RhythmDB tree-structured database
 *
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
 */

#include <config.h>
#ifdef HAVE_GNU_FWRITE_UNLOCKED
#define _GNU_SOURCE
#endif
#include <stdio.h>
#ifdef HAVE_GNU_FWRITE_UNLOCKED
#undef _GNU_SOURCE
#endif
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gprintf.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkliststore.h>
#include <libxml/entities.h>
#include <libxml/SAX.h>

#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-string-helpers.h"

static void rhythmdb_tree_class_init (RhythmDBTreeClass *klass);
static void rhythmdb_tree_init (RhythmDBTree *shell_player);
static void rhythmdb_tree_finalize (GObject *object);

static void rhythmdb_tree_load (RhythmDB *rdb, GMutex *mutex, gboolean *die);
static void rhythmdb_tree_save (RhythmDB *rdb);
static RhythmDBEntry * rhythmdb_tree_entry_new (RhythmDB *db, RhythmDBEntryType type,
						const char *uri);
static void rhythmdb_tree_entry_set (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);

static void rhythmdb_tree_entry_get (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);
static void rhythmdb_tree_entry_delete (RhythmDB *db, RhythmDBEntry *entry);
/* static GtkTreeModel * rhythmdb_tree_do_entry_query (RhythmDB *db, RhythmDBQuery *query); */
/* static GtkTreeModel * rhythmdb_tree_do_property_query (RhythmDB *db, guint property_id, RhythmDBQuery *query); */
static RhythmDBEntry * rhythmdb_tree_entry_lookup_by_location (RhythmDB *db, const char *uri);
static void rhythmdb_tree_do_full_query (RhythmDB *db, GPtrArray *query,
					 GtkTreeModel **main_model,
					 GtkTreeModel **genre_model,
					 GtkTreeModel **artist_model,
					 GtkTreeModel **album_model);

#define RHYTHMDB_TREE_XML_VERSION "1.0"

typedef struct RhythmDBTreeProperty
{
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	struct RhythmDBTreeProperty *parent;
	char *name;
	char *sort_key;
	GHashTable *children;
} RhythmDBTreeProperty;

#define RHYTHMDB_TREE_ENTRY_VALUE(ENTRY, PROPID) (&((ENTRY)->properties[PROPID]))
#define RHYTHMDB_TREE_ENTRY_GET_TYPE(ENTRY) (g_value_get_int (RHYTHMDB_TREE_ENTRY_VALUE (ENTRY, RHYTHMDB_PROP_TYPE)))

/* Optimization possibility - note that we aren't using at least
 * three values in the array; the genre/artist/album names are
 * actually stored in the tree structure. */
typedef struct
{
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	RhythmDBTreeProperty *album;
	GValue properties[RHYTHMDB_NUM_PROPERTIES];
} RhythmDBTreeEntry;

#ifndef G_DISABLE_ASSERT
static inline RhythmDBTreeProperty *
assert_valid_tree_property (RhythmDBTreeProperty *prop)
{
	g_assert (((RhythmDBTreeProperty *) prop)->magic == 0xf00dbeef);
	return prop;
}
#define RHYTHMDB_TREE_PROPERTY(x) (assert_valid_tree_property (x))
#else
#define RHYTHMDB_TREE_PROPERTY(x) ((RhythmdbTreeProperty *) x)
#endif

#ifndef G_DISABLE_ASSERT
static inline RhythmDBTreeEntry *
assert_valid_tree_entry (RhythmDBTreeEntry *entry)
{
	g_assert (((RhythmDBTreeEntry *) entry)->magic == 0xdeadb33f);
	return entry;
}
#define RHYTHMDB_TREE_ENTRY(x) (assert_valid_tree_entry (x))
#else
#define RHYTHMDB_TREE_ENTRY(x) ((RhythmDBEntry *) x)
#endif

static RhythmDBEntry *rhythmdb_tree_entry_allocate (RhythmDBTree *db);
static void rhythmdb_tree_entry_insert (RhythmDBTree *db, RhythmDBTreeEntry *entry,
					RhythmDBEntryType type, const char *uri,
					const char *genrename, const char *artistname,
					const char *albumname);

static void destroy_tree_property (RhythmDBTreeProperty *prop);
static RhythmDBTreeProperty *get_or_create_album (RhythmDBTree *db, RhythmDBTreeProperty *artist,
						  const char *name);
static RhythmDBTreeProperty *get_or_create_artist (RhythmDBTree *db, RhythmDBTreeProperty *genre,
						   const char *name);
static inline RhythmDBTreeProperty *get_or_create_genre (RhythmDBTree *db, RhythmDBEntryType type,
							 const char *name);

static void rhythmdb_tree_entry_finalize (RhythmDBTreeEntry *entry);
static void remove_entry_from_album (RhythmDBTree *db, RhythmDBTreeEntry *entry);

static char *get_entry_genre_name (RhythmDBTreeEntry *entry);
static char *get_entry_artist_name (RhythmDBTreeEntry *entry);
static char *get_entry_album_name (RhythmDBTreeEntry *entry);
static char *get_entry_genre_sort_key (RhythmDBTreeEntry *entry);
static char *get_entry_artist_sort_key (RhythmDBTreeEntry *entry);
static char *get_entry_album_sort_key (RhythmDBTreeEntry *entry);

static void handle_genre_deletion (RhythmDBTree *db, const char *name);
static void handle_artist_deletion (RhythmDBTree *db, const char *name);
static void handle_album_deletion (RhythmDBTree *db, const char *name);

struct RhythmDBTreePrivate
{
	GMemChunk *entry_memchunk;
	GMemChunk *property_memchunk;

	GHashTable *entries;

	GHashTable *song_genres;
	GHashTable *iradio_genres;

	GHashTable *propname_map;

	gboolean finalizing;
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

	rhythmdb_class->impl_load = rhythmdb_tree_load;
	rhythmdb_class->impl_save = rhythmdb_tree_save;
	rhythmdb_class->impl_entry_new = rhythmdb_tree_entry_new;
	rhythmdb_class->impl_entry_set = rhythmdb_tree_entry_set;
	rhythmdb_class->impl_entry_get = rhythmdb_tree_entry_get;
	rhythmdb_class->impl_entry_delete = rhythmdb_tree_entry_delete;
	rhythmdb_class->impl_lookup_by_location = rhythmdb_tree_entry_lookup_by_location;
	rhythmdb_class->impl_do_full_query = rhythmdb_tree_do_full_query;
}

static inline const char *
elt_name_from_propid (RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return "type";
	case RHYTHMDB_PROP_NAME:
		return "name";
	case RHYTHMDB_PROP_GENRE:
		return "genre";
	case RHYTHMDB_PROP_ARTIST:
		return "artist";
	case RHYTHMDB_PROP_ALBUM:
		return "album";
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return "track-number";
	case RHYTHMDB_PROP_DURATION:
		return "duration";
	case RHYTHMDB_PROP_FILE_SIZE:
		return "file-size";
	case RHYTHMDB_PROP_LOCATION:
		return "location";
	case RHYTHMDB_PROP_MTIME:
		return "mtime";
	case RHYTHMDB_PROP_RATING:
		return "rating";
	case RHYTHMDB_PROP_PLAY_COUNT:
		return "play-count";
	case RHYTHMDB_PROP_LAST_PLAYED:
		return "last-played";
	case RHYTHMDB_PROP_QUALITY:
		return "quality";
	}
	g_assert_not_reached ();
	return NULL;
}

static void
rhythmdb_tree_init (RhythmDBTree *db)
{
	guint i;
	db->priv = g_new0 (RhythmDBTreePrivate, 1);

	db->priv->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
						   NULL,
						   (GDestroyNotify) rhythmdb_tree_entry_finalize);

	db->priv->entry_memchunk = g_mem_chunk_new ("RhythmDBTree entry memchunk",
						    sizeof (RhythmDBTreeEntry),
						    1024, G_ALLOC_AND_FREE);
	db->priv->property_memchunk = g_mem_chunk_new ("RhythmDBTree property memchunk",
						       sizeof (RhythmDBTreeProperty),
						       1024, G_ALLOC_AND_FREE);

	db->priv->song_genres = g_hash_table_new_full (g_str_hash, g_str_equal,
						       (GDestroyNotify) g_free,
						       NULL);
	db->priv->iradio_genres = g_hash_table_new_full (g_str_hash, g_str_equal,
							 (GDestroyNotify) g_free,
							 NULL);

	db->priv->propname_map = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < RHYTHMDB_NUM_SAVED_PROPERTIES; i++) {
		const char *name = elt_name_from_propid (i);
		g_hash_table_insert (db->priv->propname_map, (gpointer) name, GINT_TO_POINTER (i));
	}
}

static inline int
propid_from_elt_name (RhythmDBTree *db, const char *name)
{
	gpointer ret, orig;	
	if (g_hash_table_lookup_extended (db->priv->propname_map, name,
					  &orig, &ret)) {
		return GPOINTER_TO_INT (ret);
	}
	return -1;
}

static inline char *
get_entry_genre_name (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->parent->name;
}

static inline char *
get_entry_artist_name (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->name;
}

static inline char *
get_entry_album_name (RhythmDBTreeEntry *entry)
{
	return entry->album->name;
}

static inline char *
get_entry_genre_sort_key (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->parent->sort_key;
}

static inline char *
get_entry_artist_sort_key (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->sort_key;
}

static inline char *
get_entry_album_sort_key (RhythmDBTreeEntry *entry)
{
	return entry->album->sort_key;
}

#ifndef G_DISABLE_ASSERT
static inline void
sanity_check_entry_tree (RhythmDBTreeEntry *entry)
{
	RHYTHMDB_TREE_ENTRY (entry); 
	RHYTHMDB_TREE_PROPERTY (entry->album); 
	RHYTHMDB_TREE_PROPERTY (entry->album->parent); 
	RHYTHMDB_TREE_PROPERTY (entry->album->parent->parent); 
}
static void
sanity_check_entry_tree_from_hash (gpointer unused, RhythmDBTreeEntry *entry)
{
	sanity_check_entry_tree (entry);
}
static void
sanity_check_database (RhythmDBTree *db)
{
	g_hash_table_foreach (db->priv->entries, (GHFunc) sanity_check_entry_tree_from_hash, NULL);
}
#else
#define sanity_check_entry_tree(entry)
#define sanity_check_database(db)
#endif

static void
unparent_entries (const char *uri, RhythmDBTreeEntry *entry, RhythmDBTree *db)
{
	remove_entry_from_album (db, entry);
}

static void
rhythmdb_tree_finalize (GObject *object)
{
	RhythmDBTree *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_TREE (object));

	db = RHYTHMDB_TREE (object);

	g_return_if_fail (db->priv != NULL);

	db->priv->finalizing = TRUE;

	sanity_check_database (db);
	
	g_hash_table_foreach (db->priv->entries, (GHFunc) unparent_entries, db);

	g_hash_table_destroy (db->priv->entries);

	g_hash_table_destroy (db->priv->propname_map);

	g_mem_chunk_destroy (db->priv->entry_memchunk);
	g_mem_chunk_destroy (db->priv->property_memchunk);

	g_hash_table_destroy (db->priv->song_genres);
	g_hash_table_destroy (db->priv->iradio_genres);

	g_free (db->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

struct RhythmDBTreeLoadContext
{
	RhythmDBTree *db;
	GMutex *mutex;
	gboolean *die;
	enum {
		RHYTHMDB_TREE_PARSER_STATE_START,
		RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY,
		RHYTHMDB_TREE_PARSER_STATE_END,
	} state;
	RhythmDBTreeEntry *entry;
	char *genrename;
	char *albumname;
	char *artistname;
	GString *buf;
	RhythmDBPropType propid;
};

static void
rhythmdb_tree_parser_start_element (struct RhythmDBTreeLoadContext *ctx,
				    const char *name, const char **attrs)
{
	g_mutex_lock (ctx->mutex);
	if (*ctx->die == TRUE)
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_END;
	g_mutex_unlock (ctx->mutex);

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_START:
	{
		if (!strcmp (name, "rhythmdb"))
			ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
	{
		if (!strcmp (name, "entry")) {
			ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY;
			ctx->entry = rhythmdb_tree_entry_allocate (ctx->db);
			ctx->genrename = NULL;
			ctx->albumname = NULL;
			ctx->artistname = NULL;
		}
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	{
		int val = propid_from_elt_name (ctx->db, name);
		if (val < 0)
			break;
		
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY;
		ctx->propid = val;
		ctx->buf = g_string_new ("");
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static void
rhythmdb_tree_parser_end_element (struct RhythmDBTreeLoadContext *ctx, const char *name)
{
	g_mutex_lock (ctx->mutex);
	if (*ctx->die == TRUE)
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_END;
	g_mutex_unlock (ctx->mutex);
	
	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_END;
		break;
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	{
		RhythmDBEntryType type;
		const char *uri;

		type = RHYTHMDB_TREE_ENTRY_GET_TYPE (ctx->entry);

		uri = g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (ctx->entry,
								     RHYTHMDB_PROP_LOCATION));
		
		rhythmdb_write_lock (RHYTHMDB (ctx->db));

		rhythmdb_tree_entry_insert (ctx->db, ctx->entry, type,
					    uri, ctx->genrename, ctx->artistname,
					    ctx->albumname);

		rhythmdb_write_unlock (RHYTHMDB (ctx->db));

		ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
	{
		GValue *value = RHYTHMDB_TREE_ENTRY_VALUE (ctx->entry, ctx->propid);

		/* Handle indexed properties. */
		switch (ctx->propid)
		{
		case RHYTHMDB_PROP_GENRE:
			ctx->genrename = ctx->buf->str;
			g_string_free (ctx->buf, FALSE);
			goto nextstate;
		case RHYTHMDB_PROP_ARTIST:
			ctx->artistname = ctx->buf->str;
			g_string_free (ctx->buf, FALSE);
			goto nextstate;
		case RHYTHMDB_PROP_ALBUM:
			ctx->albumname = ctx->buf->str;
			g_string_free (ctx->buf, FALSE);
			goto nextstate;
		default:
			break;
		}
			
		/* Other properties */
		g_value_reset (value);

		/* Optimization possibility - don't use strtoull? We don't need
		 * 64 bits. */
		switch (G_VALUE_TYPE (value))
		{
		case G_TYPE_STRING:
			g_value_set_string_take_ownership (value, ctx->buf->str);
			g_string_free (ctx->buf, FALSE);
			break;
		case G_TYPE_BOOLEAN:
			g_value_set_boolean (value, g_ascii_strtoull (ctx->buf->str, NULL, 10));
			g_string_free (ctx->buf, TRUE);
			break;
		case G_TYPE_INT:
			g_value_set_int (value, g_ascii_strtoull (ctx->buf->str, NULL, 10));
			g_string_free (ctx->buf, TRUE);
			break;
		case G_TYPE_LONG:
			g_value_set_long (value, g_ascii_strtoull (ctx->buf->str, NULL, 10));
			g_string_free (ctx->buf, TRUE);
			break;
		case G_TYPE_FLOAT:
			g_value_set_float (value, g_ascii_strtod (ctx->buf->str, NULL));
			g_string_free (ctx->buf, TRUE);
			break;
		case G_TYPE_DOUBLE:
			g_value_set_float (value, g_ascii_strtod (ctx->buf->str, NULL));
			g_string_free (ctx->buf, TRUE);
			break;
		default:
			g_assert_not_reached ();
			break;
		}

	nextstate:
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_START:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static void
rhythmdb_tree_parser_characters (struct RhythmDBTreeLoadContext *ctx, const char *data,
				 guint len)
{
	g_mutex_lock (ctx->mutex);
	if (*ctx->die == TRUE)
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_END;
	g_mutex_unlock (ctx->mutex);

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
		g_string_append_len (ctx->buf, data, len);
		break;
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
	case RHYTHMDB_TREE_PARSER_STATE_START:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static void
rhythmdb_tree_load (RhythmDB *rdb, GMutex *mutex, gboolean *die)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	xmlSAXHandlerPtr sax_handler = g_new0 (xmlSAXHandler, 1);
	struct RhythmDBTreeLoadContext *ctx = g_new0 (struct RhythmDBTreeLoadContext, 1);
	char *name;

	sax_handler->startElement = (startElementSAXFunc) rhythmdb_tree_parser_start_element;
	sax_handler->endElement = (endElementSAXFunc) rhythmdb_tree_parser_end_element;
	sax_handler->characters = (charactersSAXFunc) rhythmdb_tree_parser_characters;

	ctx->state = RHYTHMDB_TREE_PARSER_STATE_START;
	ctx->db = db;
	ctx->mutex = mutex;
	ctx->die = die;

	g_object_get (G_OBJECT (db), "name", &name, NULL);

	xmlSAXUserParseFile (sax_handler, ctx, name);
	g_free (name);
	g_free (sax_handler);
	g_free (ctx);
}

struct RhythmDBTreeSaveContext
{
	RhythmDBTree *db;
	FILE *handle;
};

/* This code is intended to be highly optimized.  This came at a small
 * readability cost.  Sorry about that.
 */
static void
save_entry (const char *uri, RhythmDBTreeEntry *entry, struct RhythmDBTreeSaveContext *ctx)
{
#ifdef HAVE_GNU_FWRITE_UNLOCKED
#define RHYTHMDB_FWRITE fwrite_unlocked
#define RHYTHMDB_FPUTC fputc_unlocked
#else
#define RHYTHMDB_FWRITE fwrite
#define RHYTHMDB_FPUTC fputc
#endif
	
#define RHYTHMDB_FWRITE_STATICSTR(STR, F) RHYTHMDB_FWRITE (STR, 1, sizeof (STR)-1, F)
#define RHYTHMDB_FWRITE_SMALLTYPE(F, FMT, TYPE)				\
G_STMT_START {								\
	g_snprintf (small_buf, sizeof (small_buf),			\
		    FMT, g_value_get_ ## TYPE (value));			\
	RHYTHMDB_FWRITE (small_buf, 1, strlen (small_buf), F);		\
} G_STMT_END
#define RHYTHMDB_FWRITE_ENCODED_STR(STR, F)				\
G_STMT_START {								\
char *encoded;								\
encoded	= xmlEncodeEntitiesReentrant (NULL, STR);			\
RHYTHMDB_FWRITE (encoded, 1, strlen (encoded), F);			\
g_free (encoded);							\
} G_STMT_END
	
	RhythmDBPropType i;
	
	RHYTHMDB_FWRITE_STATICSTR ("  <entry type=\"", ctx->handle);
	switch (RHYTHMDB_TREE_ENTRY_GET_TYPE (entry))
	{
	case RHYTHMDB_ENTRY_TYPE_SONG:
		RHYTHMDB_FWRITE_STATICSTR ("song", ctx->handle);
		break;
	case RHYTHMDB_ENTRY_TYPE_IRADIO_STATION:
		RHYTHMDB_FWRITE_STATICSTR ("iradio", ctx->handle);
		break;
	}
	RHYTHMDB_FWRITE_STATICSTR ("\">", ctx->handle);
		
	/* Skip over the first property - the type */
	for (i = 1; i < RHYTHMDB_NUM_SAVED_PROPERTIES; i++) {
		const char *elt_name;
		GValue *value;
		char small_buf[92];

		value = RHYTHMDB_TREE_ENTRY_VALUE (entry, i);

		elt_name = elt_name_from_propid (i);

		RHYTHMDB_FWRITE_STATICSTR ("    <", ctx->handle);
		RHYTHMDB_FWRITE (elt_name, 1, strlen (elt_name), ctx->handle);
		RHYTHMDB_FPUTC ('>', ctx->handle);
		    
		/* Handle special properties. */
		switch (i)
		{
		case RHYTHMDB_PROP_ALBUM:
			RHYTHMDB_FWRITE_ENCODED_STR(get_entry_album_name (entry), ctx->handle);
			goto finish_elt;
		case RHYTHMDB_PROP_ARTIST:
			RHYTHMDB_FWRITE_ENCODED_STR(get_entry_artist_name (entry), ctx->handle);
			goto finish_elt;
		case RHYTHMDB_PROP_GENRE:
			RHYTHMDB_FWRITE_ENCODED_STR(get_entry_genre_name (entry), ctx->handle);
			goto finish_elt;
		default:
			break;
		}

		switch (G_VALUE_TYPE (value))
		{
		case G_TYPE_STRING:
			RHYTHMDB_FWRITE_ENCODED_STR(g_value_get_string (value), ctx->handle);
			break;
		case G_TYPE_BOOLEAN:
			RHYTHMDB_FWRITE_SMALLTYPE (ctx->handle, "%d", boolean);
			break;
		case G_TYPE_INT:
			RHYTHMDB_FWRITE_SMALLTYPE (ctx->handle, "%d", int);
			break;
		case G_TYPE_LONG:
			RHYTHMDB_FWRITE_SMALLTYPE (ctx->handle, "%ld", long);
			break;
		case G_TYPE_FLOAT:
			RHYTHMDB_FWRITE_SMALLTYPE (ctx->handle, "%f", float);
			break;
		case G_TYPE_DOUBLE:
			RHYTHMDB_FWRITE_SMALLTYPE (ctx->handle, "%f", double);
			break;
		default:
			g_assert_not_reached ();
			break;
		}

	finish_elt:
		RHYTHMDB_FWRITE_STATICSTR ("</", ctx->handle);
		RHYTHMDB_FWRITE (elt_name, 1, strlen (elt_name), ctx->handle);
		RHYTHMDB_FWRITE_STATICSTR (">\n", ctx->handle);
	}
	RHYTHMDB_FWRITE_STATICSTR ("  </entry>\n", ctx->handle);

#undef RHYTHMDB_FWRITE_ENCODED_STR
#undef RHYTHMDB_FWRITE_SMALLTYPE
#undef RHYTHMDB_FWRITE_STATICSTR
#undef RHYTHMDB_FPUTC
#undef RHYTHMDB_FWRITE
}

static void
rhythmdb_tree_save (RhythmDB *rdb)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	char *name;
	GString *savepath;
	FILE *f;
	struct RhythmDBTreeSaveContext *ctx;

	ctx = g_new0 (struct RhythmDBTreeSaveContext, 1);

	g_object_get (G_OBJECT (db), "name", &name, NULL);

	savepath = g_string_new (name);
	g_string_append (savepath, ".tmp");

	f = fopen (savepath->str, "w");

	if (!f) {
		g_warning ("Can't save XML");
		goto out;
	}

	fprintf (f, "%s\n%s\n", "<?xml version=\"1.0\"?>",
		 "<rhythmdb version=\"" RHYTHMDB_TREE_XML_VERSION "\">");

	ctx->db = db;
	ctx->handle = f;

	g_hash_table_foreach (db->priv->entries, (GHFunc) save_entry, ctx);

	fprintf (f, "%s\n", "</rhythmdb>");

	fsync (fileno (f));
	fclose (f);

	rename (savepath->str, name);
out:
	g_string_free (savepath, TRUE);
	g_free (name);

	g_free (ctx);
}

RhythmDB *
rhythmdb_tree_new (const char *name)
{
	RhythmDBTree *db = g_object_new (RHYTHMDB_TYPE_TREE, "name", name, NULL);

	g_return_val_if_fail (db->priv != NULL, NULL);

	return RHYTHMDB (db);
}

static void
set_entry_album (RhythmDBTree *db, RhythmDBTreeEntry *entry, RhythmDBTreeProperty *artist,
		 const char *name)
{
	entry->album = get_or_create_album (db, RHYTHMDB_TREE_PROPERTY (artist), name);
	g_hash_table_insert (entry->album->children, RHYTHMDB_TREE_ENTRY (entry), NULL);
}

static RhythmDBEntry *
rhythmdb_tree_entry_allocate (RhythmDBTree *db)
{
	RhythmDBTreeEntry *ret;
	guint i;
	
	ret = g_mem_chunk_alloc0 (db->priv->entry_memchunk);

#ifndef G_DISABLE_ASSERT
	ret->magic = 0xdeadb33f;
#endif	

	/* Initialize all the properties. */
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		GType val_type = rhythmdb_get_property_type (RHYTHMDB (db), i);
		g_value_init (RHYTHMDB_TREE_ENTRY_VALUE (ret, i), val_type);

		/* Hack to ensure all string values are initialized. */
		if (val_type == G_TYPE_STRING)
			g_value_set_static_string (RHYTHMDB_TREE_ENTRY_VALUE (ret, i), "");
	}
	return ret;
}

static void
rhythmdb_tree_entry_insert (RhythmDBTree *db, RhythmDBTreeEntry *entry,
			    RhythmDBEntryType type,
			    const char *uri,
			    const char *genrename, const char *artistname,
			    const char *albumname)
{
	RhythmDBTreeProperty *artist;
	RhythmDBTreeProperty *genre;
	char *new_uri;

	/* Initialize the tree structure. */
	genre = get_or_create_genre (db, type, genrename);
	artist = get_or_create_artist (db, genre, artistname);
	set_entry_album (db, entry, artist, albumname);

	new_uri = g_strdup (uri);

	g_assert (!g_hash_table_lookup (db->priv->entries, new_uri));
	g_hash_table_insert (db->priv->entries, new_uri, entry);
	g_value_set_string_take_ownership (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LOCATION),
					   new_uri);
}


static RhythmDBEntry *
rhythmdb_tree_entry_new (RhythmDB *rdb, RhythmDBEntryType type, const char *uri)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	RhythmDBTreeEntry *ret;

	sanity_check_database (db);

	ret = rhythmdb_tree_entry_allocate (db);

	rhythmdb_tree_entry_insert (db, ret, type, uri, "", "", "");

	g_value_set_int (RHYTHMDB_TREE_ENTRY_VALUE (ret, RHYTHMDB_PROP_TYPE), type);

	sanity_check_database (db);

	return ret;
}

static RhythmDBTreeProperty *
rhythmdb_tree_property_new (RhythmDBTree *db, const char *name)
{
	RhythmDBTreeProperty *ret = g_mem_chunk_alloc0 (db->priv->property_memchunk);
	ret->name = g_strdup (name);
	ret->sort_key = rb_get_sort_key (ret->name);
#ifndef G_DISABLE_ASSERT
	ret->magic = 0xf00dbeef;
#endif	
	return ret;
}

static inline RhythmDBTreeProperty *
get_or_create_genre (RhythmDBTree *db, RhythmDBEntryType type,
		     const char *name)
{
	RhythmDBTreeProperty *genre;
	GHashTable *table;

	switch (type)
	{
	case RHYTHMDB_ENTRY_TYPE_SONG:
		table = db->priv->song_genres;
		break;
	case RHYTHMDB_ENTRY_TYPE_IRADIO_STATION:
		table = db->priv->iradio_genres;
		break;
	}

	genre = g_hash_table_lookup (table, name);		

	if (G_UNLIKELY (genre == NULL)) {
		genre = rhythmdb_tree_property_new (db, name);
		genre->children = g_hash_table_new_full (g_str_hash, g_str_equal,
							 (GDestroyNotify) g_free,
							 NULL);
		g_hash_table_insert (table, g_strdup (name), genre);
		genre->parent = NULL;
		rhythmdb_emit_genre_added (RHYTHMDB (db), name);
	}

	return RHYTHMDB_TREE_PROPERTY (genre);
}

static RhythmDBTreeProperty *
get_or_create_artist (RhythmDBTree *db, RhythmDBTreeProperty *genre,
		      const char *name)
{
	RhythmDBTreeProperty *artist;

	artist = g_hash_table_lookup (RHYTHMDB_TREE_PROPERTY (genre)->children, name);

	if (G_UNLIKELY (artist == NULL)) {
		artist = rhythmdb_tree_property_new (db, name);
		artist->children = g_hash_table_new_full (g_str_hash, g_str_equal,
							  (GDestroyNotify) g_free,
							  NULL);
		g_hash_table_insert (genre->children, g_strdup (name), artist);
		artist->parent = genre;
		rhythmdb_emit_artist_added (RHYTHMDB (db), name);
	}

	return RHYTHMDB_TREE_PROPERTY (artist);
}

static RhythmDBTreeProperty *
get_or_create_album (RhythmDBTree *db, RhythmDBTreeProperty *artist,
		     const char *name)
{
	RhythmDBTreeProperty *album;

	album = g_hash_table_lookup (RHYTHMDB_TREE_PROPERTY (artist)->children, name);

	if (G_UNLIKELY (album == NULL)) {
		album = rhythmdb_tree_property_new (db, name);
		album->children = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_hash_table_insert (artist->children, g_strdup (name), album);
		album->parent = artist;
		rhythmdb_emit_album_added (RHYTHMDB (db), name);
	}

	return RHYTHMDB_TREE_PROPERTY (album);
}

static void
handle_genre_deletion (RhythmDBTree *db, const char *name)
{
	if (!db->priv->finalizing)
		rhythmdb_emit_genre_deleted (RHYTHMDB (db), name);
}

static void
handle_artist_deletion (RhythmDBTree *db, const char *name)
{
	if (!db->priv->finalizing)
		rhythmdb_emit_artist_deleted (RHYTHMDB (db), name);
}

static void
handle_album_deletion (RhythmDBTree *db, const char *name)
{
	if (!db->priv->finalizing)
		rhythmdb_emit_album_deleted (RHYTHMDB (db), name);
}

static gboolean
remove_child (RhythmDBTreeProperty *parent, gpointer entry)
{
	RHYTHMDB_TREE_PROPERTY (parent);
	g_assert (g_hash_table_remove (parent->children, entry));
	if (g_hash_table_size (parent->children) <= 0) {
		return TRUE;
	}
	return FALSE;
}

static void
remove_entry_from_album (RhythmDBTree *db, RhythmDBTreeEntry *entry)
{
	char *cur_albumname;
	char *cur_artistname;
	char *cur_genrename;
	GHashTable *table;
			
	cur_albumname = g_strdup (get_entry_album_name (entry));
	cur_artistname = g_strdup (get_entry_artist_name (entry));
	cur_genrename = g_strdup (get_entry_genre_name (entry));

	switch (RHYTHMDB_TREE_ENTRY_GET_TYPE (entry))
	{
	case RHYTHMDB_ENTRY_TYPE_SONG:
		table = db->priv->song_genres;
		break;
	case RHYTHMDB_ENTRY_TYPE_IRADIO_STATION:
		table = db->priv->iradio_genres;
		break;
	}

	if (remove_child (RHYTHMDB_TREE_PROPERTY (entry->album), entry)) {

		handle_album_deletion (db, cur_albumname);

		if (remove_child (RHYTHMDB_TREE_PROPERTY (entry->album->parent), cur_albumname)) {
			handle_artist_deletion (db, cur_artistname);

			if (remove_child (RHYTHMDB_TREE_PROPERTY (entry->album->parent->parent),
					  cur_artistname)) {
				handle_genre_deletion (db, cur_albumname);
				destroy_tree_property (entry->album->parent->parent);
				g_hash_table_remove (table, cur_genrename);
			}
			destroy_tree_property (entry->album->parent);
		}

		destroy_tree_property (entry->album);
	}

	g_free (cur_genrename);
	g_free (cur_artistname);
	g_free (cur_albumname);
}

static void
rhythmdb_tree_entry_set (RhythmDB *adb, RhythmDBEntry *aentry,
			 guint propid, GValue *value)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBTreeEntry *entry = RHYTHMDB_TREE_ENTRY (aentry);
	RhythmDBEntryType type;

	type = RHYTHMDB_TREE_ENTRY_GET_TYPE (entry);

	sanity_check_database (db);

	/* Handle special properties */
	switch (propid)
	{
	case RHYTHMDB_PROP_ALBUM:
	{
		const char *albumname = g_value_get_string (value);

		if (strcmp (get_entry_album_name (entry), albumname)) {
			char *cur_genrename, *cur_artistname;
			RhythmDBTreeProperty *artist;
			RhythmDBTreeProperty *genre;			

			cur_artistname = g_strdup (get_entry_artist_name (entry));
			cur_genrename = g_strdup (get_entry_genre_name (entry));

			remove_entry_from_album (db, entry); 

			genre = RHYTHMDB_TREE_PROPERTY (get_or_create_genre (db, type, cur_genrename)); 
			artist = RHYTHMDB_TREE_PROPERTY (get_or_create_artist (db, genre, cur_artistname)); 
			set_entry_album (db, entry, artist, albumname);

			sanity_check_entry_tree (entry);

			g_free (cur_artistname);
			g_free (cur_genrename);
		}
	}
	break;
	case RHYTHMDB_PROP_ARTIST:
	{
		const char *artistname = g_value_get_string (value);
		char *cur_artistname = get_entry_artist_name (entry);

		if (strcmp (cur_artistname, artistname)) {
			RhythmDBTreeProperty *new_artist;
			RhythmDBTreeProperty *genre;			
			char *cur_genrename, *cur_albumname;

			cur_artistname = g_strdup (cur_artistname);
			cur_albumname = g_strdup (get_entry_album_name (entry));
			cur_genrename = g_strdup (get_entry_genre_name (entry));

			remove_entry_from_album (db, entry); 

			genre = RHYTHMDB_TREE_PROPERTY (get_or_create_genre (db, type, cur_genrename)); 
			new_artist = RHYTHMDB_TREE_PROPERTY (get_or_create_artist (db, genre, artistname)); 

			set_entry_album (db, entry, new_artist, cur_albumname);

			sanity_check_entry_tree (entry);

			g_free (cur_genrename);
			g_free (cur_albumname);
			g_free (cur_artistname);
		}
	}
	break;
	case RHYTHMDB_PROP_GENRE:
	{
		const char *genrename = g_value_get_string (value);
		char *cur_genrename = get_entry_genre_name (entry);

		if (strcmp (cur_genrename, genrename)) {
			RhythmDBTreeProperty *new_genre;			
			RhythmDBTreeProperty *new_artist;
			char *artistname;
			char *albumname;

			cur_genrename = g_strdup (cur_genrename);

			artistname = g_strdup (get_entry_artist_name (entry));
			albumname = g_strdup (get_entry_album_name (entry));
			
			remove_entry_from_album (db, entry); 

			new_genre = RHYTHMDB_TREE_PROPERTY (get_or_create_genre (db, type, genrename)); 
			new_artist = RHYTHMDB_TREE_PROPERTY (get_or_create_artist (db, new_genre, artistname)); 

			set_entry_album (db, entry, new_artist, albumname);

			sanity_check_entry_tree (entry);

			g_free (cur_genrename);
			g_free (artistname);
			g_free (albumname);
		}
	}
	break;
	default:
		g_value_reset (RHYTHMDB_TREE_ENTRY_VALUE (entry, propid));
		g_value_copy (value, RHYTHMDB_TREE_ENTRY_VALUE (entry, propid));
		break;
	}

	sanity_check_database (db);
}

static void
rhythmdb_tree_entry_get (RhythmDB *db, RhythmDBEntry *aentry,
			 guint propid, GValue *value)
{
	RhythmDBTreeEntry *entry = aentry;

	sanity_check_database (RHYTHMDB_TREE (db));

	/* Handle special properties */
	switch (propid)
	{
	case RHYTHMDB_PROP_ALBUM:
		g_value_set_string (value, get_entry_album_name (entry));
		break;
	case RHYTHMDB_PROP_ARTIST:
		g_value_set_string (value, get_entry_artist_name (entry));
		break;
	case RHYTHMDB_PROP_GENRE:
		g_value_set_string (value, get_entry_genre_name (entry));
		break;
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		g_value_set_string (value, get_entry_album_sort_key (entry));
		break;
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		g_value_set_string (value, get_entry_artist_sort_key (entry));
		break;
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		g_value_set_string (value, get_entry_genre_sort_key (entry));
		break;
	default:
		g_value_copy (RHYTHMDB_TREE_ENTRY_VALUE (entry, propid), value);
		break;
	}

	sanity_check_database (RHYTHMDB_TREE (db));
}

static void
rhythmdb_tree_entry_finalize (RhythmDBTreeEntry *entry)
{
	guint i;
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		g_value_unset (RHYTHMDB_TREE_ENTRY_VALUE (entry, i));
	}
#ifndef G_DISABLE_ASSERT
	entry->magic = 0xf33df33d;
#endif
}

static void
rhythmdb_tree_entry_delete (RhythmDB *adb, RhythmDBEntry *aentry)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBTreeEntry *entry = RHYTHMDB_TREE_ENTRY (aentry);
#ifndef G_DISABLE_ASSERT
	const char *uri;
#endif

	sanity_check_database (db);

#ifndef G_DISABLE_ASSERT
	uri = g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LOCATION));
	g_assert (g_hash_table_lookup (db->priv->entries, uri) != NULL);
#endif

	remove_entry_from_album (db, entry); 

	g_hash_table_remove (db->priv->entries, uri);
	g_mem_chunk_free (db->priv->entry_memchunk, entry);

	sanity_check_database (db);
}

static void
destroy_tree_property (RhythmDBTreeProperty *prop)
{
#ifndef G_DISABLE_ASSERT
	prop->magic = 0xf33df33d;
#endif
	g_free (prop->name);
	g_free (prop->sort_key);
	g_hash_table_destroy (prop->children);
}

typedef void (*RhythmDBTreeTraversalFunc) (RhythmDBTree *db, RhythmDBTreeEntry *entry, gpointer data);

struct RhythmDBTreeTraversalData
{
	RhythmDBTree *db;
	GPtrArray *query;
	RhythmDBTreeTraversalFunc func;
	gpointer data;
};

static gboolean
evaluate_conjunctive_subquery (RhythmDBTree *db, GPtrArray *query, RhythmDBTreeEntry *entry)
{
	guint i;
/* Optimization possibility - we may get here without actually having
 * anything in the query.  It would be faster to instead just merge
 * the child hash table into the query result hash.
 */
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		switch (data->type) {
		case RHYTHMDB_QUERY_PROP_LIKE:
			if (G_VALUE_TYPE (data->val) == G_TYPE_STRING) {
				const char *stra, *strb;

				switch (data->propid)
				{
				case RHYTHMDB_PROP_ALBUM:
					stra = get_entry_album_name (entry);
					break;
				case RHYTHMDB_PROP_ARTIST:
					stra = get_entry_artist_name (entry);
					break;
				case RHYTHMDB_PROP_GENRE:
					stra = get_entry_genre_name (entry);
					break;
				default:
					stra = g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid));
				}
		
				strb = g_value_get_string (data->val);
				return strstr (strb, stra) != NULL;
			}
			/* Deliberately fall through here */
		case RHYTHMDB_QUERY_PROP_EQUALS:
			switch (data->propid)
			{
			case RHYTHMDB_PROP_ALBUM:
				if (strcmp (get_entry_album_name (entry),
					    g_value_get_string (data->val)))
					return FALSE;
				break;
			case RHYTHMDB_PROP_ARTIST:
				if (strcmp (get_entry_artist_name (entry),
					    g_value_get_string (data->val)))
					return FALSE;
				break;
			case RHYTHMDB_PROP_GENRE:
				if (strcmp (get_entry_genre_name (entry),
					    g_value_get_string (data->val)))
					return FALSE;
				break;				
			default:
				if (rb_gvalue_compare (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid),
						       data->val) != 0)
					return FALSE;
				
			}
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			if (rb_gvalue_compare (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid),
					       data->val) > 0)
				return FALSE;
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			if (rb_gvalue_compare (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid),
					       data->val) < 0)
				return FALSE;
			break;
		case RHYTHMDB_QUERY_END:
		case RHYTHMDB_QUERY_DISJUNCTION:
			g_assert_not_reached ();
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
	g_hash_table_foreach (album->children, (GHFunc) do_conjunction, data);
}

static void
conjunctive_query_albums (const char *name, RhythmDBTreeProperty *artist,
			  struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int album_query_idx = -1;

	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_ALBUM) {
			if (album_query_idx > 0)
				return;
			album_query_idx = i;
			
		}
	}

	if (album_query_idx >= 0) {
		RhythmDBTreeProperty *album;
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, album_query_idx);
		
		g_ptr_array_remove_index_fast (data->query, album_query_idx);
		
		album = g_hash_table_lookup (artist->children, g_value_get_string (qdata->val));

		if (album != NULL) {
			conjunctive_query_songs (album->name, album, data);
		}
		return;
	} 

	g_hash_table_foreach (artist->children, (GHFunc) conjunctive_query_songs, data);
}

static void
conjunctive_query_artists (const char *name, RhythmDBTreeProperty *genre,
			   struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int artist_query_idx = -1;

	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_ARTIST) {
			if (artist_query_idx > 0)
				return;
			artist_query_idx = i;
			
		}
	}

	if (artist_query_idx >= 0) {
		RhythmDBTreeProperty *artist;
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, artist_query_idx);
		
		g_ptr_array_remove_index_fast (data->query, artist_query_idx);
		
		artist = g_hash_table_lookup (genre->children, g_value_get_string (qdata->val));
		if (artist != NULL) {
			conjunctive_query_albums (artist->name, artist, data);
		}
		return;
	} 

	g_hash_table_foreach (genre->children, (GHFunc) conjunctive_query_albums, data);
}

static void
conjunctive_query_genre (RhythmDBTree *db, GHashTable *genres,
			 GPtrArray *query, struct RhythmDBTreeTraversalData *data)
{
	int genre_query_idx = -1;
	guint i;
	
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_GENRE) {
			/* A song can't currently have two genres.  So
			 * if we get a conjunctive query for that, we
			 * know the result must be the empty set. */
			if (genre_query_idx > 0)
				return;
			genre_query_idx = i;
			
		}
	}

	if (genre_query_idx >= 0) {
		RhythmDBTreeProperty *genre;
		RhythmDBQueryData *qdata = g_ptr_array_index (query, genre_query_idx);
		
		g_ptr_array_remove_index_fast (query, genre_query_idx);
		
		genre = g_hash_table_lookup (genres, g_value_get_string (qdata->val));
		if (genre != NULL) {
			conjunctive_query_artists (genre->name, genre, data);
		} 
		return;
	} 

	g_hash_table_foreach (genres, (GHFunc) conjunctive_query_artists, data);
}

static void
conjunctive_query (RhythmDBTree *db, GPtrArray *query,
		   RhythmDBTreeTraversalFunc func, gpointer data)
{
	int type_query_idx = -1;
	guint i;
	struct RhythmDBTreeTraversalData *traversal_data;
	
	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_TYPE) {
			/* A song can't have two types. */
			if (type_query_idx > 0)
				return;
			type_query_idx = i;
		}
	}

	traversal_data = g_new (struct RhythmDBTreeTraversalData, 1);
	traversal_data->db = db;
	traversal_data->query = query;
	traversal_data->func = func;
	traversal_data->data = data;

	if (type_query_idx >= 0) {
		RhythmDBEntryType etype;
		RhythmDBQueryData *qdata = g_ptr_array_index (query, type_query_idx);
		
		g_ptr_array_remove_index_fast (query, type_query_idx);
		
		etype = g_value_get_int (qdata->val);
		switch (etype)
		{
		case RHYTHMDB_ENTRY_TYPE_SONG:
			conjunctive_query_genre (db, db->priv->song_genres, query, traversal_data);
			break;
		case RHYTHMDB_ENTRY_TYPE_IRADIO_STATION:
			conjunctive_query_genre (db, db->priv->iradio_genres, query, traversal_data);
			break;
		}
		goto out;
	} 

	/* No type was given; punt and query everything */
	conjunctive_query_genre (db, db->priv->song_genres, query, traversal_data);
	conjunctive_query_genre (db, db->priv->iradio_genres, query, traversal_data);
out:
	g_free (traversal_data);
}


static GList *
split_query_by_disjunctions (RhythmDBTree *db, GPtrArray *query)
{
	GList *conjunctions = NULL;
	guint i, j;
	guint last_disjunction = 0;
	GPtrArray *subquery = g_ptr_array_new ();

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {

			/* Copy the subquery */
			for (j = last_disjunction; j < i; j++) {
				g_ptr_array_add (subquery, g_ptr_array_index (query, j));
			}

			conjunctions = g_list_prepend (conjunctions, subquery);
			last_disjunction = i+1;
			g_assert (subquery->len > 0);
			subquery = g_ptr_array_new ();
		}
	}

	/* Copy the last subquery, except for the QUERY_END */
	for (i = last_disjunction; i < query->len; i++) {
		g_ptr_array_add (subquery, g_ptr_array_index (query, i));
	}
	g_assert (subquery->len > 0);
	
	conjunctions = g_list_prepend (conjunctions, subquery);
	return conjunctions;
}

static void
do_query_recurse (RhythmDBTree *db, GPtrArray *query, RhythmDBTreeTraversalFunc func, gpointer data)
{
	GList *conjunctions, *tem;

	conjunctions = split_query_by_disjunctions (db, query);

	for (tem = conjunctions; tem; tem = tem->next) {
		conjunctive_query (db, tem->data, func, data);
		g_ptr_array_free (tem->data, TRUE);
	}

	g_list_free (conjunctions);
}

struct RhythmDBTreeQueryGatheringData
{
	RhythmDBTree *db;
	GHashTable *genres;
	GHashTable *artists;
	GHashTable *albums;

	GtkListStore *main_model;
};

static void
handle_entry_match (RhythmDB *db, RhythmDBTreeEntry *entry,
		    struct RhythmDBTreeQueryGatheringData *data)
{
	GtkTreeIter iter;
	gtk_list_store_prepend (data->main_model, &iter);
	gtk_list_store_set (data->main_model, &iter, 0, RHYTHMDB_TREE_ENTRY (entry), -1);

	g_hash_table_insert (data->genres, get_entry_genre_name (entry), NULL);
	g_hash_table_insert (data->artists, get_entry_artist_name (entry), NULL);
	g_hash_table_insert (data->albums, get_entry_album_name (entry), NULL);
}

static void
fill_list_store_from_hash (char *key, gpointer unused, GtkListStore *store)
{
	GtkTreeIter iter;
	gtk_list_store_prepend (store, &iter);
	gtk_list_store_set (store, &iter, 0, key, -1);
}

static void
rhythmdb_tree_do_full_query (RhythmDB *adb,
			     GPtrArray *query,
			     GtkTreeModel **main_model,
			     GtkTreeModel **genre_model,
			     GtkTreeModel **artist_model,
			     GtkTreeModel **album_model)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	struct RhythmDBTreeQueryGatheringData *data = g_new (struct RhythmDBTreeQueryGatheringData, 1);

	*main_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_POINTER));
	data->main_model = GTK_LIST_STORE (*main_model);
	data->genres = g_hash_table_new (g_direct_hash, g_direct_equal);
	data->artists = g_hash_table_new (g_direct_hash, g_direct_equal);
	data->albums = g_hash_table_new (g_direct_hash, g_direct_equal);
	
	do_query_recurse (db, query, (RhythmDBTreeTraversalFunc) handle_entry_match, data);

	*genre_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_STRING));
	*artist_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_STRING));
	*album_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_STRING));
	g_hash_table_foreach (data->genres, (GHFunc) fill_list_store_from_hash,
			      *genre_model);
	g_hash_table_foreach (data->artists, (GHFunc) fill_list_store_from_hash,
			      *artist_model);
	g_hash_table_foreach (data->albums, (GHFunc) fill_list_store_from_hash,
			      *album_model);
	g_hash_table_destroy (data->genres);
	g_hash_table_destroy (data->artists);
	g_hash_table_destroy (data->albums);
	g_free (data);
}

static RhythmDBEntry *
rhythmdb_tree_entry_lookup_by_location (RhythmDB *adb, const char *uri)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	return g_hash_table_lookup (db->priv->entries, uri);
}


/* static GtkListStore * */
/* create_main_model (RhythmDB *db) */
/* { */
/* 	GtkListStore *store; */
/* 	GType *types; */
/* 	int i; */
	
/* 	source->priv->db = g_value_get_object (value); */
	
/* 	types = g_new (GType, RHYTHMDB_NUM_PROPERTIES); */
	
/* 	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) */
/* 		types[i] = rhythmdb_get_property_type (db, i); */

/* 	store = g_object_new (GTK_TYPE_LIST_STORE, NULL); */
	
/* 	gtk_list_store_set_column_types (store, RHYTHMDB_NUM_PROPERTIES, types); */

/* 	g_free (types); */
/* 	return store; */
/* } */
/* static GtkTreeModel * */
/* rhythmdb_tree_do_entry_query (RhythmDB *adb, GPtrArray *query) */
/* { */
/* 	RhythmDBTree *db = RHYTHMDB_TREE (adb); */
/* 	GHashTable *result_set; */

/* 	result_set = build_entry_query_set (db, query); */

/* 	return GTK_TREE_MODEL (rhythmdb_entry_model_new_from_hash (result_set)); */
/* } */

/* struct RhythmDBTreePropertyGatheringData */
/* { */
/* 	guint prop_id; */
/* 	GHashTable *set; */
/* }; */

/* static void */
/* gather_property (RhythmDBTreeEntry *entry, gpointer unused, */
/* 		 struct RhythmDBTreePropertyGatheringData *data) */
/* { */
/* 	g_hash_table_insert (data->set, RHYTHMDB_TREE_ENTRY_VALUE (entry, data->prop_id), NULL); */
/* } */

/* static GHashTable * */
/* gather_property_set (RhythmDB *db, guint prop_id, GHashTable *table) */
/* { */
/* 	GHashTable *ret; */
/* 	struct RhythmDBTreePropertyGatheringData *data; */

/* 	ret = g_hash_table_new (g_direct_hash, g_direct_equal); */
	
/* 	data = g_new (struct RhythmDBTreePropertyGatheringData, 1); */
/* 	data->prop_id = prop_id; */
/* 	data->set = ret; */

/* 	g_hash_table_foreach (table, gather_property, data); */

/* 	g_free (data); */
/* 	return ret; */
/* } */

/* static void */
/* handle_entry_match (RhythmDB *db, RhythmDBTreeEntry *entry, GHashTable *set) */
/* { */
/* 	g_hash_table_insert (set, entry, NULL); */
/* } */


/* static GtkTreeModel * */
/* rhythmdb_tree_do_property_query (RhythmDB *db, guint property_id, RhythmDBQuery *query) */
/* { */
/* 	GHashTable *result_set; */
	
/* 	switch (property_id) */
/* 	{ */
/* 	case RHYTHMDB_PROP_GENRE: */
/* 	case RHYTHMDB_PROP_ARTIST: */
/* 	case RHYTHMDB_PROP_ALBUM: */
/* 		break; */
		
/* 	default: */
/* 		g_assert_not_reached (); */
/* 		break; */
/* 	} */
	
/* } */
