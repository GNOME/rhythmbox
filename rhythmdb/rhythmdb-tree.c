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
#include <libxml/parserInternals.h>

#include "rhythmdb-tree.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-atomic.h"
#include "rb-string-helpers.h"

static void rhythmdb_tree_class_init (RhythmDBTreeClass *klass);
static void rhythmdb_tree_init (RhythmDBTree *shell_player);
static void rhythmdb_tree_finalize (GObject *object);

static void rhythmdb_tree_load (RhythmDB *rdb, gboolean *die);
static void rhythmdb_tree_save (RhythmDB *rdb);
static RhythmDBEntry * rhythmdb_tree_entry_new (RhythmDB *db, RhythmDBEntryType type,
						const char *uri);
static void rhythmdb_tree_entry_set (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);

static void rhythmdb_tree_entry_get (RhythmDB *db, RhythmDBEntry *entry,
				guint propid, GValue *value);
static void rhythmdb_tree_entry_delete (RhythmDB *db, RhythmDBEntry *entry);
static RhythmDBEntry * rhythmdb_tree_entry_lookup_by_location (RhythmDB *db, const char *uri);
static void rhythmdb_tree_do_full_query (RhythmDB *db, GPtrArray *query,
					 GtkTreeModel *main_model, gboolean *cancel);
gboolean rhythmdb_tree_evaluate_query (RhythmDB *adb, GPtrArray *query,
				       RhythmDBEntry *aentry);

#define RHYTHMDB_TREE_XML_VERSION "1.0"

#define RHYTHMDB_TREE_ENTRY_VALUE(ENTRY, PROPID) (&((ENTRY)->properties[PROPID]))
#define RHYTHMDB_TREE_ENTRY_GET_TYPE(ENTRY) (g_value_get_int (RHYTHMDB_TREE_ENTRY_VALUE (ENTRY, RHYTHMDB_PROP_TYPE)))

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

static RhythmDBEntry *rhythmdb_tree_entry_allocate (RhythmDBTree *db, RhythmDBEntryType type);
static gboolean rhythmdb_tree_entry_insert (RhythmDBTree *db, RhythmDBTreeEntry *entry,
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
static char *get_entry_genre_folded (RhythmDBTreeEntry *entry);
static char *get_entry_artist_folded (RhythmDBTreeEntry *entry);
static char *get_entry_album_folded (RhythmDBTreeEntry *entry);

static void handle_genre_deletion (RhythmDBTree *db, const char *name);
static void handle_artist_deletion (RhythmDBTree *db, const char *name);
static void handle_album_deletion (RhythmDBTree *db, const char *name);

static GList *split_query_by_disjunctions (RhythmDBTree *db, GPtrArray *query);
static gboolean evaluate_conjunctive_subquery (RhythmDBTree *db, GPtrArray *query,
					       guint base, guint max, RhythmDBTreeEntry *entry);

struct RhythmDBTreePrivate
{
	GMemChunk *entry_memchunk;
	GMemChunk *property_memchunk;

	GHashTable *entries;

	GHashTable *song_genres;
	GHashTable *iradio_genres;

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
	rhythmdb_class->impl_evaluate_query = rhythmdb_tree_evaluate_query;
	rhythmdb_class->impl_do_full_query = rhythmdb_tree_do_full_query;
}

static void
rhythmdb_tree_init (RhythmDBTree *db)
{
	db->priv = g_new0 (RhythmDBTreePrivate, 1);

	db->priv->entries = g_hash_table_new (g_str_hash, g_str_equal);

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

static inline char *
get_entry_genre_folded (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->parent->folded;
}

static inline char *
get_entry_artist_folded (RhythmDBTreeEntry *entry)
{
	return entry->album->parent->folded;
}

static inline char *
get_entry_album_folded (RhythmDBTreeEntry *entry)
{
	return entry->album->folded;
}

static inline void
sanity_check_entry_tree (RhythmDBTreeEntry *entry)
{
#ifdef RHYTHMDB_ENABLE_SANITY_CHECK
	RHYTHMDB_TREE_ENTRY (entry); 
	RHYTHMDB_TREE_PROPERTY (entry->album); 
	RHYTHMDB_TREE_PROPERTY (entry->album->parent); 
	RHYTHMDB_TREE_PROPERTY (entry->album->parent->parent); 
#endif
}
#ifdef RHYTHMDB_ENABLE_SANITY_CHECK
static void
sanity_check_entry_tree_from_hash (gpointer unused, RhythmDBTreeEntry *entry)
{
	sanity_check_entry_tree (entry);
}
#endif
static void
sanity_check_database (RhythmDBTree *db)
{
#ifdef RHYTHMDB_ENABLE_SANITY_CHECK
	g_hash_table_foreach (db->priv->entries, (GHFunc) sanity_check_entry_tree_from_hash, NULL);
#endif
}

static void
unparent_entries (const char *uri, RhythmDBTreeEntry *entry, RhythmDBTree *db)
{
	remove_entry_from_album (db, entry);
	rhythmdb_tree_entry_finalize (entry);
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
	xmlParserCtxtPtr xmlctx;
	gboolean *die;
	enum {
		RHYTHMDB_TREE_PARSER_STATE_START,
		RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY,
		RHYTHMDB_TREE_PARSER_STATE_END,
	} state;
	gboolean in_unknown_elt;
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
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

	if (ctx->in_unknown_elt)
		return;

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_START:
	{
		if (!strcmp (name, "rhythmdb"))
			ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
		else
			ctx->in_unknown_elt = TRUE;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
	{
		if (!strcmp (name, "entry")) {
			RhythmDBEntryType type = -1;
			gboolean type_set = FALSE;
			for (; *attrs; attrs +=2) {
				if (!strcmp (*attrs, "type")) {
					const char *typename = *(attrs+1);
					if (!strcmp (typename, "song"))
						type = RHYTHMDB_ENTRY_TYPE_SONG;
					else 
						type = RHYTHMDB_ENTRY_TYPE_IRADIO_STATION;
					type_set = TRUE;
					break;
				}
			}
			g_assert (type_set);
			ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY;
			ctx->entry = rhythmdb_tree_entry_allocate (ctx->db, type);
			ctx->genrename = NULL;
			ctx->albumname = NULL;
			ctx->artistname = NULL;
		} else
			ctx->in_unknown_elt = TRUE;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	{
		int val = rhythmdb_propid_from_nice_elt_name (RHYTHMDB (ctx->db), name);
		if (val < 0) {
			ctx->in_unknown_elt = TRUE;
			break;
		}
		
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
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

	if (ctx->in_unknown_elt) {
		ctx->in_unknown_elt = FALSE;		
		return;
	}
	
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
		g_free (ctx->genrename);
		g_free (ctx->artistname);
		g_free (ctx->albumname);

		rhythmdb_emit_entry_restored (RHYTHMDB (ctx->db), ctx->entry);

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
		rhythmdb_entry_sync_mirrored (RHYTHMDB (ctx->db),
					      ctx->entry, ctx->propid, value);

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
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

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
rhythmdb_tree_load (RhythmDB *rdb, gboolean *die)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	xmlParserCtxtPtr ctxt;
	xmlSAXHandlerPtr sax_handler = g_new0 (xmlSAXHandler, 1);
	struct RhythmDBTreeLoadContext *ctx = g_new0 (struct RhythmDBTreeLoadContext, 1);
	char *name;

	sax_handler->startElement = (startElementSAXFunc) rhythmdb_tree_parser_start_element;
	sax_handler->endElement = (endElementSAXFunc) rhythmdb_tree_parser_end_element;
	sax_handler->characters = (charactersSAXFunc) rhythmdb_tree_parser_characters;

	ctx->state = RHYTHMDB_TREE_PARSER_STATE_START;
	ctx->db = db;
	ctx->die = die;

	g_object_get (G_OBJECT (db), "name", &name, NULL);

	if (rb_uri_exists (name)) {
		ctxt = xmlCreateFileParserCtxt (name);
		ctx->xmlctx = ctxt;
		xmlFree (ctxt->sax);
		ctxt->userData = ctx;
		ctxt->sax = sax_handler;
		xmlParseDocument (ctxt);
		ctxt->sax = NULL;
		xmlFreeParserCtxt (ctxt);
			
	}
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
	RHYTHMDB_FWRITE_STATICSTR ("\">\n", ctx->handle);
		
	/* Skip over the first property - the type */
	for (i = 1; i < RHYTHMDB_NUM_SAVED_PROPERTIES; i++) {
		const char *elt_name;
		GValue *value;
		char small_buf[92];

		value = RHYTHMDB_TREE_ENTRY_VALUE (entry, i);

		/* Optimization - don't save default values */
		switch (G_VALUE_TYPE (value)) {
		case G_TYPE_INT:
			if (g_value_get_int (value) == 0)
				continue;
			break;
		case G_TYPE_LONG:
			if (g_value_get_long (value) == 0)
				continue;
			break;
		case G_TYPE_BOOLEAN:
			if (g_value_get_boolean (value) == FALSE)
				continue;
			break;
		}

		elt_name = rhythmdb_nice_elt_name_from_propid ((RhythmDB *) ctx->db, i);

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
rhythmdb_tree_entry_allocate (RhythmDBTree *db, RhythmDBEntryType type)
{
	RhythmDBTreeEntry *ret;
	guint i;
	
	ret = g_mem_chunk_alloc0 (db->priv->entry_memchunk);

#ifndef G_DISABLE_ASSERT
	ret->magic = 0xdeadb33f;
#endif	
	ret->refcount.value = 1;

	/* Initialize all the properties. */
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		GType val_type = rhythmdb_get_property_type (RHYTHMDB (db), i);
		g_value_init (RHYTHMDB_TREE_ENTRY_VALUE (ret, i), val_type);

		/* Hack to ensure all string values are initialized. */
		if (val_type == G_TYPE_STRING)
			g_value_set_static_string (RHYTHMDB_TREE_ENTRY_VALUE (ret, i), "");

		if (i == RHYTHMDB_PROP_TRACK_NUMBER)
			g_value_set_int (RHYTHMDB_TREE_ENTRY_VALUE (ret, i), -1);
	}
	g_value_set_int (RHYTHMDB_TREE_ENTRY_VALUE (ret, RHYTHMDB_PROP_TYPE), type);

	return ret;
}

static gboolean
rhythmdb_tree_entry_insert (RhythmDBTree *db, RhythmDBTreeEntry *entry,
			    RhythmDBEntryType type,
			    const char *uri,
			    const char *genrename, const char *artistname,
			    const char *albumname)
{
	RhythmDBTreeProperty *artist;
	RhythmDBTreeProperty *genre;
	char *new_uri;

	if (g_hash_table_lookup (db->priv->entries, uri))
		return FALSE;

	/* Initialize the tree structure. */
	genre = get_or_create_genre (db, type, genrename);
	artist = get_or_create_artist (db, genre, artistname);
	set_entry_album (db, entry, artist, albumname);

	new_uri = g_strdup (uri);

	g_hash_table_insert (db->priv->entries, new_uri, entry);
	g_value_set_string_take_ownership (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LOCATION),
					   new_uri);
	return TRUE;
}


static RhythmDBEntry *
rhythmdb_tree_entry_new (RhythmDB *rdb, RhythmDBEntryType type, const char *uri)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	RhythmDBTreeEntry *ret;

	sanity_check_database (db);

	ret = rhythmdb_tree_entry_allocate (db, type);

	if (!rhythmdb_tree_entry_insert (db, ret, type, uri, "", "", "")) {
		rhythmdb_tree_entry_destroy (db, ret);
		return NULL;
	}

	sanity_check_database (db);

	return ret;
}

void
rhythmdb_tree_entry_destroy (RhythmDBTree *db, RhythmDBEntry *aentry)
{
	RhythmDBTreeEntry *entry = RHYTHMDB_TREE_ENTRY (aentry);
	rhythmdb_tree_entry_finalize (entry);
	g_mem_chunk_free (db->priv->entry_memchunk, entry);
}

static RhythmDBTreeProperty *
rhythmdb_tree_property_new (RhythmDBTree *db, const char *name)
{
	RhythmDBTreeProperty *ret = g_mem_chunk_alloc0 (db->priv->property_memchunk);
	ret->name = g_strdup (name);
	ret->sort_key = rb_get_sort_key (ret->name);
	ret->folded = g_utf8_casefold (ret->name, -1);
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
	default:
		g_assert_not_reached ();
		table = NULL;
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
		/* rhythmdb_emit_genre_added (RHYTHMDB (db), name); */
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
		/* rhythmdb_emit_artist_added (RHYTHMDB (db), name); */
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
		/* rhythmdb_emit_album_added (RHYTHMDB (db), name); */
	}

	return RHYTHMDB_TREE_PROPERTY (album);
}

static inline void
handle_genre_deletion (RhythmDBTree *db, const char *name)
{
/* 	if (!db->priv->finalizing) */
/* 		rhythmdb_emit_genre_deleted (RHYTHMDB (db), name); */
}

static inline void
handle_artist_deletion (RhythmDBTree *db, const char *name)
{
/* 	if (!db->priv->finalizing) */
/* 		rhythmdb_emit_artist_deleted (RHYTHMDB (db), name); */
}

static inline void
handle_album_deletion (RhythmDBTree *db, const char *name)
{
/* 	if (!db->priv->finalizing) */
/* 		rhythmdb_emit_album_deleted (RHYTHMDB (db), name); */
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
	default:
		g_assert_not_reached ();
		table = NULL;
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

	if (entry->deleted)
		return;

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
rhythmdb_tree_entry_get (RhythmDB *adb, RhythmDBEntry *aentry,
			 guint propid, GValue *value)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBTreeEntry *entry = RHYTHMDB_TREE_ENTRY (aentry);

	sanity_check_database (db);

	if (G_UNLIKELY (entry->deleted)) {
		g_value_copy (RHYTHMDB_TREE_ENTRY_VALUE (entry, propid), value);
		return;
	}

	switch (propid)
	{
	/* Handle special properties */
	case RHYTHMDB_PROP_ALBUM:
		g_value_set_static_string (value, get_entry_album_name (entry));
		break;
	case RHYTHMDB_PROP_ARTIST:
		g_value_set_static_string (value, get_entry_artist_name (entry));
		break;
	case RHYTHMDB_PROP_GENRE:
		g_value_set_static_string (value, get_entry_genre_name (entry));
		break;
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		g_value_set_static_string (value, get_entry_album_sort_key (entry));
		break;
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		g_value_set_static_string (value, get_entry_artist_sort_key (entry));
		break;
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		g_value_set_static_string (value, get_entry_genre_sort_key (entry));
		break;
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		g_value_set_static_string (value, get_entry_album_folded (entry));
		break;
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		g_value_set_static_string (value, get_entry_artist_folded (entry));
		break;
	case RHYTHMDB_PROP_GENRE_FOLDED:
		g_value_set_static_string (value, get_entry_genre_folded (entry));
		break;
	/* Handle other string properties */
	case RHYTHMDB_PROP_TITLE:
		g_value_set_static_string (value, g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_TITLE)));
		break;
	case RHYTHMDB_PROP_TITLE_SORT_KEY:
		g_value_set_static_string (value, g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_TITLE_SORT_KEY)));
		break;
	case RHYTHMDB_PROP_LOCATION:
		g_value_set_static_string (value, g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LOCATION)));
		break;
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		g_value_set_static_string (value, g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LAST_PLAYED_STR)));
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
	const char *uri;

	sanity_check_database (db);

	if (entry->deleted)
		return;

	entry->deleted = TRUE;

	uri = g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_LOCATION));
	g_assert (g_hash_table_lookup (db->priv->entries, uri) != NULL);
	
	/* We store these properties back in the entry temporarily so that later
	   callbacks can retreive the value even though the entry is removed from
	   the indexed tree.
	*/
	g_value_set_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_GENRE),
			    get_entry_genre_name (entry));
	g_value_set_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_ARTIST),
			    get_entry_artist_name (entry));
	g_value_set_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, RHYTHMDB_PROP_ALBUM),
			    get_entry_album_name (entry));
	remove_entry_from_album (db, entry); 
	
	g_hash_table_remove (db->priv->entries, uri);

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
	g_free (prop->folded);
	g_hash_table_destroy (prop->children);
}

typedef void (*RhythmDBTreeTraversalFunc) (RhythmDBTree *db, RhythmDBTreeEntry *entry, gpointer data);
typedef void (*RhythmDBTreeAlbumTraversalFunc) (RhythmDBTree *db, RhythmDBTreeProperty *album, gpointer data);

struct RhythmDBTreeTraversalData
{
	RhythmDBTree *db;
	GPtrArray *query;
	RhythmDBTreeTraversalFunc func;
	gpointer data;
	gboolean *cancel;
};

gboolean
rhythmdb_tree_evaluate_query (RhythmDB *adb, GPtrArray *query,
			      RhythmDBEntry *aentry)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBTreeEntry *entry = RHYTHMDB_TREE_ENTRY (aentry);
	guint i;
	guint last_disjunction;

	for (i = 0, last_disjunction = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {
			if (evaluate_conjunctive_subquery (db, query, last_disjunction, i, entry))
				return TRUE;

			last_disjunction = i;
		}
	}
	if (evaluate_conjunctive_subquery (db, query, last_disjunction, query->len, entry))
		return TRUE;
	return FALSE;
}

static gboolean
evaluate_conjunctive_subquery (RhythmDBTree *db, GPtrArray *query,
			       guint base, guint max, RhythmDBTreeEntry *entry)

{
	guint i;
	entry = RHYTHMDB_TREE_ENTRY (entry);
/* Optimization possibility - we may get here without actually having
 * anything in the query.  It would be faster to instead just merge
 * the child hash table into the query result hash.
 */
	for (i = base; i < max; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
		{
			gboolean matched = FALSE;
			GList *conjunctions = split_query_by_disjunctions (db, data->subquery);
			GList *tem;

			for (tem = conjunctions; tem; tem = tem->next) {
				GPtrArray *subquery = tem->data;
				if (!matched && evaluate_conjunctive_subquery (db, subquery,
									       0, subquery->len,
									       entry)) {
					matched = TRUE;
				}
				g_ptr_array_free (tem->data, TRUE);
			}
			g_list_free (conjunctions);
			if (!matched)
				return FALSE;
		}
		break;
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
			if (G_VALUE_TYPE (data->val) == G_TYPE_STRING) {
				gboolean islike;
				const char *stra, *strb;

				switch (data->propid)
				{
				case RHYTHMDB_PROP_ALBUM_FOLDED:
					stra = get_entry_album_folded (entry);
					break;
				case RHYTHMDB_PROP_ARTIST_FOLDED:
					stra = get_entry_artist_folded (entry);
					break;
				case RHYTHMDB_PROP_GENRE_FOLDED:
					stra = get_entry_genre_folded (entry);
					break;
				default:
					stra = g_value_get_string (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid));
				}

				strb = g_value_get_string (data->val);
				islike = (strstr (stra, strb) != NULL);
				if (data->type == RHYTHMDB_QUERY_PROP_LIKE
				    && !islike)
					return FALSE;
				else if (data->type == RHYTHMDB_QUERY_PROP_NOT_LIKE
				    && islike)
					return FALSE;
			} else {
				if (rb_gvalue_compare (RHYTHMDB_TREE_ENTRY_VALUE (entry, data->propid),
						       data->val) != 0)
					return FALSE;
			}
			break;
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
	if (G_UNLIKELY (*data->cancel))
		return;
	/* Finally, we actually evaluate the query! */
	if (evaluate_conjunctive_subquery (data->db, data->query, 0, data->query->len,
					   entry)) {
		data->func (data->db, entry, data->data);
	}
}

static void
conjunctive_query_songs (const char *name, RhythmDBTreeProperty *album,
			 struct RhythmDBTreeTraversalData *data)
{
	if (G_UNLIKELY (*data->cancel))
		return;
	g_hash_table_foreach (album->children, (GHFunc) do_conjunction, data);
}

static GPtrArray *
clone_remove_ptr_array_index (GPtrArray *arr, guint index)
{
	GPtrArray *ret = g_ptr_array_new ();
	guint i;
	for (i = 0; i < arr->len; i++)
		if (i != index)
			g_ptr_array_add (ret, g_ptr_array_index (arr, i));
	
	return ret;
}

static void
conjunctive_query_albums (const char *name, RhythmDBTreeProperty *artist,
			  struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int album_query_idx = -1;

	if (G_UNLIKELY (*data->cancel))
		return;

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
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, album_query_idx);
		
		album = g_hash_table_lookup (artist->children, g_value_get_string (qdata->val));

		if (album != NULL) {
				conjunctive_query_songs (album->name, album, data);
		}
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
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

	if (G_UNLIKELY (*data->cancel))
		return;

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
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, artist_query_idx);
		
		artist = g_hash_table_lookup (genre->children, g_value_get_string (qdata->val));
		if (artist != NULL) {
			conjunctive_query_albums (artist->name, artist, data);
		}
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
		return;
	} 

	g_hash_table_foreach (genre->children, (GHFunc) conjunctive_query_albums, data);
}

static void
conjunctive_query_genre (RhythmDBTree *db, GHashTable *genres,
			 struct RhythmDBTreeTraversalData *data)
{
	int genre_query_idx = -1;
	guint i;

	if (G_UNLIKELY (*data->cancel))
		return;
	
	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
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
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, genre_query_idx);
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, genre_query_idx);
		
		genre = g_hash_table_lookup (genres, g_value_get_string (qdata->val));
		if (genre != NULL) {
			conjunctive_query_artists (genre->name, genre, data);
		} 
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
		return;
	} 

	g_hash_table_foreach (genres, (GHFunc) conjunctive_query_artists, data);
}

static void
conjunctive_query (RhythmDBTree *db, GPtrArray *query,
		   RhythmDBTreeTraversalFunc func, gpointer data,
		   gboolean *cancel)
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
	traversal_data->cancel = cancel;

	if (type_query_idx >= 0) {
		RhythmDBEntryType etype;
		RhythmDBQueryData *qdata = g_ptr_array_index (query, type_query_idx);
		
		g_ptr_array_remove_index_fast (query, type_query_idx);
		
		etype = g_value_get_int (qdata->val);
		switch (etype)
		{
		case RHYTHMDB_ENTRY_TYPE_SONG:
			conjunctive_query_genre (db, db->priv->song_genres, traversal_data);
			break;
		case RHYTHMDB_ENTRY_TYPE_IRADIO_STATION:
			conjunctive_query_genre (db, db->priv->iradio_genres, traversal_data);
			break;
		}
		goto out;
	} 

	/* No type was given; punt and query everything */
	conjunctive_query_genre (db, db->priv->song_genres, traversal_data);
	conjunctive_query_genre (db, db->priv->iradio_genres, traversal_data);
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

struct RhythmDBTreeQueryGatheringData
{
	RhythmDBTree *db;
	GHashTable *entries;
	RhythmDBQueryModel *main_model;
};

static void
do_query_recurse (RhythmDBTree *db, GPtrArray *query, RhythmDBTreeTraversalFunc func,
		  struct RhythmDBTreeQueryGatheringData *data, gboolean *cancel)
{
	GList *conjunctions, *tem;

	conjunctions = split_query_by_disjunctions (db, query);

	rb_debug ("doing recursive query, %d conjunctions", g_list_length (conjunctions));

	/* If there is a disjunction involved, we must uniquify the entry hits. */
	if (conjunctions->next != NULL)
		data->entries = g_hash_table_new (g_direct_hash, g_direct_equal);
	else
		data->entries = NULL;

	for (tem = conjunctions; tem; tem = tem->next) {
		if (G_UNLIKELY (*cancel))
			break;
		conjunctive_query (db, tem->data, func, data, cancel);
		g_ptr_array_free (tem->data, TRUE);
	}

	if (data->entries != NULL)
		g_hash_table_destroy (data->entries);

	g_list_free (conjunctions);
}

static void
handle_entry_match (RhythmDB *db, RhythmDBTreeEntry *entry,
		    struct RhythmDBTreeQueryGatheringData *data)
{

	if (data->entries
	    && g_hash_table_lookup (data->entries, entry))
		return;
		
	rhythmdb_query_model_add_entry (data->main_model, entry);
}

static void
rhythmdb_tree_do_full_query (RhythmDB *adb,
			     GPtrArray *query,
			     GtkTreeModel *main_model,
			     gboolean *cancel)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	struct RhythmDBTreeQueryGatheringData *data = g_new (struct RhythmDBTreeQueryGatheringData, 1);

	data->main_model = RHYTHMDB_QUERY_MODEL (main_model);

	do_query_recurse (db, query, (RhythmDBTreeTraversalFunc) handle_entry_match, data, cancel);

	g_free (data);
}

static RhythmDBEntry *
rhythmdb_tree_entry_lookup_by_location (RhythmDB *adb, const char *uri)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	return g_hash_table_lookup (db->priv->entries, uri);
}
