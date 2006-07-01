 /*
 *  arch-tag: Header for RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@rhythmbox.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef RHYTHMDB_H
#define RHYTHMDB_H

#include <glib.h>
#include <glib-object.h>
#include <stdarg.h>
#include <libxml/tree.h>

#include "config.h"
#include "rhythmdb-query-results.h"

G_BEGIN_DECLS

struct RhythmDB_;
typedef struct RhythmDB_ RhythmDB;

#define RHYTHMDB_TYPE      (rhythmdb_get_type ())
#define RHYTHMDB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE, RhythmDB))
#define RHYTHMDB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE, RhythmDBClass))
#define RHYTHMDB_IS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE))
#define RHYTHMDB_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE))
#define RHYTHMDB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE, RhythmDBClass))

struct RhythmDBEntry_;
typedef struct RhythmDBEntry_ RhythmDBEntry;
GType rhythmdb_entry_get_type (void);

#define RHYTHMDB_TYPE_ENTRY      (rhythmdb_entry_get_type ())
#define RHYTHMDB_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY, RhythmDBEntry))
#define RHYTHMDB_IS_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_ENTRY))


typedef void (*RhythmDBEntryActionFunc) (RhythmDBEntry *entry, gpointer data);
typedef char* (*RhythmDBEntryStringFunc) (RhythmDBEntry *entry, gpointer data);
typedef gboolean (*RhythmDBEntryCanSyncFunc) (RhythmDB *db, RhythmDBEntry *entry, gpointer data);
typedef void (*RhythmDBEntrySyncFunc) (RhythmDB *db, RhythmDBEntry *entry, GError **error, gpointer data);

typedef struct {
	char 				*name;

	guint				entry_type_data_size;

	/* virtual functions here */
	RhythmDBEntryActionFunc		post_entry_create;
	gpointer			post_entry_create_data;
	GDestroyNotify			post_entry_create_destroy;

	RhythmDBEntryActionFunc		pre_entry_destroy;
	gpointer			pre_entry_destroy_data;
	GDestroyNotify			pre_entry_destroy_destroy;

	RhythmDBEntryStringFunc		get_playback_uri;
	gpointer			get_playback_uri_data;
	GDestroyNotify			get_playback_uri_destroy;

	RhythmDBEntryCanSyncFunc	can_sync_metadata;
	gpointer			can_sync_metadata_data;
	GDestroyNotify			can_sync_metadata_destroy;

	RhythmDBEntrySyncFunc		sync_metadata;
	gpointer			sync_metadata_data;
	GDestroyNotify			sync_metadata_destroy;
} RhythmDBEntryType_;
typedef RhythmDBEntryType_ *RhythmDBEntryType;

GType rhythmdb_entry_type_get_type (void);
#define RHYTHMDB_TYPE_ENTRY_TYPE	(rhythmdb_entry_type_get_type ())
#define RHYTHMDB_ENTRY_TYPE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY_TYPE, RhythmDBEntryType_))
#define RHYTHMDB_IS_ENTRY_TYPE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_ENTRY_TYPE))



typedef GPtrArray RhythmDBQuery;
GType rhythmdb_query_get_type (void);
#define RHYTHMDB_TYPE_QUERY	(rhythmdb_query_get_type ())
#define RHYTHMDB_QUERY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_QUERY, RhythmDBQuery))
#define RHYTHMDB_IS_QUERY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_QUERY))


#define RHYTHMDB_ENTRY_TYPE_SONG (rhythmdb_entry_song_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IRADIO_STATION (rhythmdb_entry_iradio_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_POST (rhythmdb_entry_podcast_post_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_FEED (rhythmdb_entry_podcast_feed_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR (rhythmdb_entry_import_error_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IGNORE (rhythmdb_entry_ignore_get_type ())
#define RHYTHMDB_ENTRY_TYPE_INVALID (GINT_TO_POINTER (-1))

typedef enum
{
	RHYTHMDB_QUERY_END,
	RHYTHMDB_QUERY_DISJUNCTION,
	RHYTHMDB_QUERY_SUBQUERY,

	/* general */
	RHYTHMDB_QUERY_PROP_EQUALS,

	/* string */
	RHYTHMDB_QUERY_PROP_LIKE,
	RHYTHMDB_QUERY_PROP_NOT_LIKE,
	RHYTHMDB_QUERY_PROP_PREFIX,
	RHYTHMDB_QUERY_PROP_SUFFIX,

	/* numerical */
	RHYTHMDB_QUERY_PROP_GREATER,
	RHYTHMDB_QUERY_PROP_LESS,

	/* synthetic query types, translated into non-synthetic ones internally */
	RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN,
	RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN,
	RHYTHMDB_QUERY_PROP_YEAR_EQUALS,
	RHYTHMDB_QUERY_PROP_YEAR_GREATER,   
	RHYTHMDB_QUERY_PROP_YEAR_LESS,   
} RhythmDBQueryType;

/* If you modify this enum, don't forget to modify rhythmdb_prop_get_type */
typedef enum
{
	RHYTHMDB_PROP_TYPE = 0,
	RHYTHMDB_PROP_TITLE,
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_TRACK_NUMBER,
	RHYTHMDB_PROP_DISC_NUMBER,
	RHYTHMDB_PROP_DURATION,
	RHYTHMDB_PROP_FILE_SIZE,
	RHYTHMDB_PROP_LOCATION,
	RHYTHMDB_PROP_MOUNTPOINT,
	RHYTHMDB_PROP_MTIME,
	RHYTHMDB_PROP_FIRST_SEEN,
	RHYTHMDB_PROP_LAST_SEEN,
	RHYTHMDB_PROP_RATING,
	RHYTHMDB_PROP_PLAY_COUNT,
	RHYTHMDB_PROP_LAST_PLAYED,
	RHYTHMDB_PROP_BITRATE,
	RHYTHMDB_PROP_DATE,
	RHYTHMDB_PROP_TRACK_GAIN,
	RHYTHMDB_PROP_TRACK_PEAK,
	RHYTHMDB_PROP_ALBUM_GAIN,
	RHYTHMDB_PROP_ALBUM_PEAK,
	RHYTHMDB_PROP_MIMETYPE,
	RHYTHMDB_PROP_TITLE_SORT_KEY,
	RHYTHMDB_PROP_GENRE_SORT_KEY,
	RHYTHMDB_PROP_ARTIST_SORT_KEY,
	RHYTHMDB_PROP_ALBUM_SORT_KEY,
	RHYTHMDB_PROP_TITLE_FOLDED,
	RHYTHMDB_PROP_GENRE_FOLDED,
	RHYTHMDB_PROP_ARTIST_FOLDED,
	RHYTHMDB_PROP_ALBUM_FOLDED,
	RHYTHMDB_PROP_LAST_PLAYED_STR,
	RHYTHMDB_PROP_HIDDEN,
	RHYTHMDB_PROP_PLAYBACK_ERROR,
	RHYTHMDB_PROP_FIRST_SEEN_STR,
	RHYTHMDB_PROP_LAST_SEEN_STR,

	/* synthetic properties */
	RHYTHMDB_PROP_SEARCH_MATCH,
	RHYTHMDB_PROP_YEAR,

	/* Podcast properties */
	RHYTHMDB_PROP_STATUS,
	RHYTHMDB_PROP_DESCRIPTION,
	RHYTHMDB_PROP_SUBTITLE,
	RHYTHMDB_PROP_SUMMARY,
	RHYTHMDB_PROP_LANG,
	RHYTHMDB_PROP_COPYRIGHT,
	RHYTHMDB_PROP_IMAGE,
	RHYTHMDB_PROP_POST_TIME,

	RHYTHMDB_PROP_MUSICBRAINZ_TRACKID,
	
	RHYTHMDB_NUM_PROPERTIES
} RhythmDBPropType;

enum {
	RHYTHMDB_PODCAST_STATUS_COMPLETE = 100,
	RHYTHMDB_PODCAST_STATUS_ERROR = 101,
	RHYTHMDB_PODCAST_STATUS_WAITING = 102,
	RHYTHMDB_PODCAST_STATUS_PAUSED = 103,
};

GType rhythmdb_query_type_get_type (void);
GType rhythmdb_prop_type_get_type (void);

#define RHYTHMDB_TYPE_QUERY_TYPE (rhythmdb_query_type_get_type ())
#define RHYTHMDB_TYPE_PROP_TYPE (rhythmdb_prop_type_get_type ())

typedef struct {
	guint type;
	guint propid;
	GValue *val;
	GPtrArray *subquery;
} RhythmDBQueryData;

typedef struct {
	RhythmDBPropType prop;
	GValue old;
	GValue new;
} RhythmDBEntryChange;

const char *rhythmdb_entry_get_string	(RhythmDBEntry *entry, RhythmDBPropType propid);
char *rhythmdb_entry_dup_string	(RhythmDBEntry *entry, RhythmDBPropType propid);
gboolean rhythmdb_entry_get_boolean	(RhythmDBEntry *entry, RhythmDBPropType propid);
guint64 rhythmdb_entry_get_uint64	(RhythmDBEntry *entry, RhythmDBPropType propid);
gulong rhythmdb_entry_get_ulong		(RhythmDBEntry *entry, RhythmDBPropType propid);
double rhythmdb_entry_get_double	(RhythmDBEntry *entry, RhythmDBPropType propid);
gpointer rhythmdb_entry_get_pointer     (RhythmDBEntry *entry, RhythmDBPropType propid);

RhythmDBEntryType rhythmdb_entry_get_entry_type (RhythmDBEntry *entry);



typedef enum
{
	RHYTHMDB_ERROR_ACCESS_FAILED,
} RhythmDBError;

#define RHYTHMDB_ERROR (rhythmdb_error_quark ())

GQuark rhythmdb_error_quark (void);

typedef struct RhythmDBPrivate RhythmDBPrivate;

struct RhythmDB_
{
	GObject parent;

	RhythmDBPrivate *priv;
};

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*entry_added)		(RhythmDB *db, RhythmDBEntry *entry);
	void	(*entry_changed)	(RhythmDB *db, RhythmDBEntry *entry, GSList *changes); /* list of RhythmDBEntryChanges */
	void	(*entry_deleted)	(RhythmDB *db, RhythmDBEntry *entry);
	void	(*load_complete)	(RhythmDB *db);
	void	(*save_complete)	(RhythmDB *db);
	void	(*load_error)		(RhythmDB *db, const char *uri, const char *msg);
	void	(*save_error)		(RhythmDB *db, const char *uri, const GError *error);
	void	(*read_only)		(RhythmDB *db, gboolean readonly);

	/* virtual methods */

	void		(*impl_load)		(RhythmDB *db, gboolean *dead);
	void		(*impl_save)		(RhythmDB *db);
	
	void		(*impl_entry_new)	(RhythmDB *db, RhythmDBEntry *entry);

	gboolean	(*impl_entry_set)	(RhythmDB *db, RhythmDBEntry *entry,
	                                 guint propid, const GValue *value);

	void		(*impl_entry_get)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_delete)	(RhythmDB *db, RhythmDBEntry *entry);

	void            (*impl_entry_delete_by_type) (RhythmDB *db, RhythmDBEntryType type);

	RhythmDBEntry *	(*impl_lookup_by_location)(RhythmDB *db, const char *uri);
	
	gboolean 	(*impl_evaluate_query)	(RhythmDB *db, GPtrArray *query, RhythmDBEntry *entry);

	void		(*impl_entry_foreach)	(RhythmDB *db, GFunc func, gpointer data);

	void		(*impl_do_full_query)	(RhythmDB *db, GPtrArray *query,
						 RhythmDBQueryResults *results,
						 gboolean *cancel);
} RhythmDBClass;


GType		rhythmdb_get_type	(void);

RhythmDB *	rhythmdb_new		(const char *name);

void		rhythmdb_shutdown	(RhythmDB *db);

void		rhythmdb_load		(RhythmDB *db);

void		rhythmdb_save		(RhythmDB *db);
void		rhythmdb_save_async	(RhythmDB *db);

void		rhythmdb_start_action_thread	(RhythmDB *db);

void		rhythmdb_commit		(RhythmDB *db);

gboolean	rhythmdb_entry_is_editable (RhythmDB *db, RhythmDBEntry *entry);

RhythmDBEntry *	rhythmdb_entry_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);
RhythmDBEntry *	rhythmdb_entry_example_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);

void		rhythmdb_add_uri	(RhythmDB *db, const char *uri);
void		rhythmdb_add_uri_with_type	(RhythmDB *db, const char *uri, RhythmDBEntryType type);

void		rhythmdb_entry_get	(RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType propid, GValue *val);
void		rhythmdb_entry_set	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, const GValue *value);
void		rhythmdb_entry_set_nonotify	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, const GValue *value);
void		rhythmdb_entry_set_uninserted   (RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, const GValue *value);

char *		rhythmdb_entry_get_playback_uri	(RhythmDBEntry *entry);

gpointer	rhythmdb_entry_get_type_data (RhythmDBEntry *entry, guint expected_size);
#define		RHYTHMDB_ENTRY_GET_TYPE_DATA(e,t)	((t*)rhythmdb_entry_get_type_data((e),sizeof(t)))

void		rhythmdb_entry_delete	(RhythmDB *db, RhythmDBEntry *entry);
void            rhythmdb_entry_delete_by_type (RhythmDB *db, 
					       RhythmDBEntryType type);
void		rhythmdb_entry_move_to_trash (RhythmDB *db,
					      RhythmDBEntry *entry);


RhythmDBEntry *	rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri);

gboolean	rhythmdb_evaluate_query		(RhythmDB *db, GPtrArray *query,
						 RhythmDBEntry *entry);

void		rhythmdb_entry_foreach		(RhythmDB *db,
						 GFunc func,
						 gpointer data);

/**
 * Returns a freshly allocated GtkTreeModel which represents the query.
 * The extended arguments alternate between RhythmDBQueryType args
 * and their values. Items are prioritized like algebraic expressions, and
 * implicitly ANDed. Here's an example:
 *
rhythmdb_do_full_query (db,
			RHYTHMDB_QUERY_PROP_EQUALS,
 				RHYTHMDB_PROP_ARTIST, "Pink Floyd",
		RHYTHMDB_QUERY_DISJUNCTION,
			RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Classical",
			RHYTHMDB_QUERY_PROP_GREATER,
				RHYTHMDB_PROP_RATING, 5,
	RHYTHMDB_QUERY_END);
 * Which means: artist = Pink Floyd OR (genre = Classical AND rating >= 5)
 */
void		rhythmdb_do_full_query			(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 ...);
void		rhythmdb_do_full_query_parsed		(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 GPtrArray *query);

void		rhythmdb_do_full_query_async		(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 ...);

void		rhythmdb_do_full_query_async_parsed	(RhythmDB *db,
							 RhythmDBQueryResults *results,
							 GPtrArray *query);

void		rhythmdb_entry_sync_mirrored		(RhythmDB *db, RhythmDBEntry *entry, guint propid);

GPtrArray *	rhythmdb_query_parse			(RhythmDB *db, ...);
void		rhythmdb_query_append			(RhythmDB *db, GPtrArray *query, ...);
void		rhythmdb_query_append_prop_multiple	(RhythmDB *db, GPtrArray *query, RhythmDBPropType propid, GList *items);
void		rhythmdb_query_concatenate		(GPtrArray *query1, GPtrArray *query2);
void		rhythmdb_query_free			(GPtrArray *query);
GPtrArray *	rhythmdb_query_copy			(GPtrArray *array);
void		rhythmdb_query_preprocess		(RhythmDB *db, GPtrArray *query);

void		rhythmdb_query_serialize		(RhythmDB *db, GPtrArray *query,
							 xmlNodePtr node);

GPtrArray *	rhythmdb_query_deserialize		(RhythmDB *db, xmlNodePtr node);

gboolean	rhythmdb_query_is_time_relative		(RhythmDB *db, GPtrArray *query);

const xmlChar *	rhythmdb_nice_elt_name_from_propid	(RhythmDB *db, RhythmDBPropType propid);
int		rhythmdb_propid_from_nice_elt_name	(RhythmDB *db, const xmlChar *name);

void		rhythmdb_emit_entry_added		(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_deleted		(RhythmDB *db, RhythmDBEntry *entry);

gboolean	rhythmdb_is_busy			(RhythmDB *db);
char *		rhythmdb_compute_status_normal		(gint n_songs, glong duration,
							 guint64 size,
							 const char *singular,
							 const char *plural);


RhythmDBEntryType rhythmdb_entry_register_type          (const char *name);
RhythmDBEntryType rhythmdb_entry_type_get_by_name       (const char *name);

RhythmDBEntryType rhythmdb_entry_song_get_type          (void);
RhythmDBEntryType rhythmdb_entry_iradio_get_type        (void);
RhythmDBEntryType rhythmdb_entry_podcast_post_get_type  (void);
RhythmDBEntryType rhythmdb_entry_podcast_feed_get_type  (void);
RhythmDBEntryType rhythmdb_entry_import_error_get_type	(void);
RhythmDBEntryType rhythmdb_entry_ignore_get_type        (void);

GType rhythmdb_get_property_type (RhythmDB *db, guint property_id);

RhythmDBEntry* rhythmdb_entry_ref (RhythmDBEntry *entry);
void rhythmdb_entry_unref (RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RHYTHMBDB_H */
