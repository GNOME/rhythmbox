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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef RHYTHMDB_H
#define RHYTHMDB_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtktreemodel.h>
#include <stdarg.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libxml/tree.h>

#include "config.h"
#include "rb-refstring.h"

G_BEGIN_DECLS

#define RHYTHMDB_TYPE      (rhythmdb_get_type ())
#define RHYTHMDB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE, RhythmDB))
#define RHYTHMDB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE, RhythmDBClass))
#define RHYTHMDB_IS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE))
#define RHYTHMDB_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE))
#define RHYTHMDB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE, RhythmDBClass))


typedef gint32 RhythmDBEntryType;

#define RHYTHMDB_ENTRY_TYPE_SONG (rhythmdb_entry_song_get_type ())
#define RHYTHMDB_ENTRY_TYPE_IRADIO_STATION (rhythmdb_entry_iradio_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_POST (rhythmdb_entry_podcast_post_get_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_FEED (rhythmdb_entry_podcast_feed_get_type ())

typedef enum
{
	RHYTHMDB_QUERY_END,
	RHYTHMDB_QUERY_DISJUNCTION,
	RHYTHMDB_QUERY_SUBQUERY,
	RHYTHMDB_QUERY_PROP_EQUALS,
	RHYTHMDB_QUERY_PROP_LIKE,
	RHYTHMDB_QUERY_PROP_NOT_LIKE,
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
	RHYTHMDB_PROP_SEARCH_MATCH,

	/* Podcast properties */
	RHYTHMDB_PROP_STATUS,
	RHYTHMDB_PROP_DESCRIPTION,
	RHYTHMDB_PROP_SUBTITLE,
	RHYTHMDB_PROP_SUMMARY,
	RHYTHMDB_PROP_LANG,
	RHYTHMDB_PROP_COPYRIGHT,
	RHYTHMDB_PROP_IMAGE,
	RHYTHMDB_PROP_POST_TIME,
	
	RHYTHMDB_NUM_PROPERTIES
} RhythmDBPropType;

enum {
	RHYTHMDB_PODCAST_STATUS_COMPLETE = 100,
	RHYTHMDB_PODCAST_STATUS_ERROR = 101,
	RHYTHMDB_PODCAST_STATUS_WAITING = 102,
	RHYTHMDB_PODCAST_STATUS_PAUSED = 103,
};

GType rhythmdb_query_get_type (void);
GType rhythmdb_prop_get_type (void);

#define RHYTHMDB_TYPE_QUERY (rhythmdb_query_get_type ())
#define RHYTHMDB_TYPE_PROP (rhythmdb_prop_get_type ())

typedef struct {
	guint type;
	guint propid;
	GValue *val;
	GPtrArray *subquery;
} RhythmDBQueryData;

typedef struct {
	/* podcast */
	RBRefString *description;
	RBRefString *subtitle;
	RBRefString *summary;
	RBRefString *lang;
	RBRefString *copyright;
	RBRefString *image;
	gulong status;	/* 0-99: downloading
			   100: Complete
			   101: Error
			   102: wait
			   103: pause */
	gulong post_time;
} RhythmDBPodcastFields;


typedef struct {
	/* internal bits */
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	gboolean inserted;
	gint refcount;
	void *data;
	gulong type;
	
	/* metadata */
	RBRefString *title;
	RBRefString *artist;
	RBRefString *album;
	RBRefString *genre;
	gulong tracknum;
	gulong discnum;
	gulong duration;
	gulong bitrate;
	double track_gain;
	double track_peak;
	double album_gain;
	double album_peak;
	GDate *date;

	/* filesystem */
	char *location;
	RBRefString *mountpoint;
	guint64 file_size;
	RBRefString *mimetype;
	gulong mtime;
	gulong first_seen;
	gulong last_seen;

	/* user data */
	gdouble rating;
	glong play_count;
	gulong last_played;

	/* cached data */
	RBRefString *last_played_str;
	RBRefString *first_seen_str;

	/* playback error string */
	char *playback_error;

	/* visibility (to hide entries on unmounted volumes) */
	gboolean hidden;

	/*Podcast*/
	RhythmDBPodcastFields *podcast;
} RhythmDBEntry;

typedef struct {
	RhythmDBPropType prop;
	GValue old;
	GValue new;
} RhythmDBEntryChange;

void rhythmdb_entry_get (RhythmDBEntry *entry, RhythmDBPropType propid, GValue *val);
G_INLINE_FUNC const char *rhythmdb_entry_get_string	(RhythmDBEntry *entry, RhythmDBPropType propid);
G_INLINE_FUNC gboolean rhythmdb_entry_get_boolean	(RhythmDBEntry *entry, RhythmDBPropType propid);
G_INLINE_FUNC guint64 rhythmdb_entry_get_uint64		(RhythmDBEntry *entry, RhythmDBPropType propid);
G_INLINE_FUNC gulong rhythmdb_entry_get_ulong		(RhythmDBEntry *entry, RhythmDBPropType propid);
G_INLINE_FUNC double rhythmdb_entry_get_double		(RhythmDBEntry *entry, RhythmDBPropType propid);

#if defined (G_CAN_INLINE) || defined (__RHYTHMDB_C__)

G_INLINE_FUNC const char *
rhythmdb_entry_get_string (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TITLE:
		return rb_refstring_get (entry->title);
	case RHYTHMDB_PROP_ALBUM:
		return rb_refstring_get (entry->album);
	case RHYTHMDB_PROP_ARTIST:
		return rb_refstring_get (entry->artist);
	case RHYTHMDB_PROP_GENRE:
		return rb_refstring_get (entry->genre);
	case RHYTHMDB_PROP_MIMETYPE:
		return rb_refstring_get (entry->mimetype);
	case RHYTHMDB_PROP_TITLE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->title);
	case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		return rb_refstring_get_sort_key (entry->album);
	case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		return rb_refstring_get_sort_key (entry->artist);
	case RHYTHMDB_PROP_GENRE_SORT_KEY:
		return rb_refstring_get_sort_key (entry->genre);
	case RHYTHMDB_PROP_TITLE_FOLDED:
		return rb_refstring_get_folded (entry->title);
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		return rb_refstring_get_folded (entry->album);
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		return rb_refstring_get_folded (entry->artist);
	case RHYTHMDB_PROP_GENRE_FOLDED:
		return rb_refstring_get_folded (entry->genre);
	case RHYTHMDB_PROP_LOCATION:
		return entry->location;
	case RHYTHMDB_PROP_MOUNTPOINT:
		return rb_refstring_get (entry->mountpoint);
	case RHYTHMDB_PROP_LAST_PLAYED_STR:
		return rb_refstring_get (entry->last_played_str);
	case RHYTHMDB_PROP_PLAYBACK_ERROR:
		return entry->playback_error;
	case RHYTHMDB_PROP_FIRST_SEEN_STR:
		return rb_refstring_get (entry->first_seen_str);
	case RHYTHMDB_PROP_SEARCH_MATCH:
		return NULL;	/* synthetic property */
	/* Podcast properties */
	case RHYTHMDB_PROP_DESCRIPTION:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->description);
		else
			return NULL;
	case RHYTHMDB_PROP_SUBTITLE:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->subtitle);
		else
			return NULL;
	case RHYTHMDB_PROP_SUMMARY: 
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->summary);
		else
			return NULL;
	case RHYTHMDB_PROP_LANG:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->lang);
		else
			return NULL;
	case RHYTHMDB_PROP_COPYRIGHT:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->copyright);
		else
			return NULL;
	case RHYTHMDB_PROP_IMAGE:
		if (entry->podcast)
			return rb_refstring_get (entry->podcast->image);
		else
			return NULL;

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

G_INLINE_FUNC gboolean
rhythmdb_entry_get_boolean (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_HIDDEN:
		return entry->hidden;
	default:
		g_assert_not_reached ();
		return FALSE;
	}
}

G_INLINE_FUNC guint64
rhythmdb_entry_get_uint64 (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_FILE_SIZE:
		return entry->file_size;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

G_INLINE_FUNC gulong
rhythmdb_entry_get_ulong (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return entry->type;
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return entry->tracknum;
	case RHYTHMDB_PROP_DISC_NUMBER:
		return entry->discnum;
	case RHYTHMDB_PROP_DURATION:
		return entry->duration;
	case RHYTHMDB_PROP_MTIME:
		return entry->mtime;
	case RHYTHMDB_PROP_FIRST_SEEN:
		return entry->first_seen;
	case RHYTHMDB_PROP_LAST_SEEN:
		return entry->last_seen;
	case RHYTHMDB_PROP_LAST_PLAYED:
		return entry->last_played;
	case RHYTHMDB_PROP_PLAY_COUNT:
		return entry->play_count;
	case RHYTHMDB_PROP_BITRATE:
		return entry->bitrate;		
	case RHYTHMDB_PROP_DATE:
		if (entry->date)
			return g_date_get_julian (entry->date);
		else
			return 0;
	case RHYTHMDB_PROP_POST_TIME:
		if (entry->podcast)
			return entry->podcast->post_time;
		else
			return 0;
	case RHYTHMDB_PROP_STATUS:
		if (entry->podcast)
			return entry->podcast->status;		
		else
			return 0;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

G_INLINE_FUNC double
rhythmdb_entry_get_double (RhythmDBEntry *entry, RhythmDBPropType propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TRACK_GAIN:
		return entry->track_gain;
	case RHYTHMDB_PROP_TRACK_PEAK:
		return entry->track_peak;
	case RHYTHMDB_PROP_ALBUM_GAIN:
		return entry->album_gain;
	case RHYTHMDB_PROP_ALBUM_PEAK:
		return entry->album_peak;
	case RHYTHMDB_PROP_RATING:
		return entry->rating;
	default:
		g_assert_not_reached ();
		return 0.0;
	}
}
#endif

typedef enum
{
	RHYTHMDB_ERROR_ACCESS_FAILED,
} RhythmDBError;

#define RHYTHMDB_ERROR (rhythmdb_error_quark ())

GQuark rhythmdb_error_quark (void);

typedef struct RhythmDBPrivate RhythmDBPrivate;

typedef struct
{
	GObject parent;

	RhythmDBPrivate *priv;
} RhythmDB;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*entry_added)		(RhythmDBEntry *entry);
	void	(*entry_changed)	(RhythmDBEntry *entry, GSList *changes); /* list of RhythmDBEntryChanges */
	void	(*entry_deleted)	(RhythmDBEntry *entry);
	void	(*load_complete)	(void);
	void	(*save_complete)	(void);
	void	(*load_error)		(const char *uri, const char *msg);
	void	(*save_error)		(const char *uri, const GError *error);
	void	(*read_only)		(gboolean readonly);

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
						 GtkTreeModel *main_model,
						 gboolean *cancel);
} RhythmDBClass;

GType		rhythmdb_get_type	(void);

RhythmDB *	rhythmdb_new		(const char *name);

void		rhythmdb_shutdown	(RhythmDB *db);

void		rhythmdb_load		(RhythmDB *db);

void		rhythmdb_save		(RhythmDB *db);
void		rhythmdb_save_async	(RhythmDB *db);

void		rhythmdb_commit		(RhythmDB *db);

gboolean	rhythmdb_entry_is_editable (RhythmDB *db, RhythmDBEntry *entry);

RhythmDBEntry *	rhythmdb_entry_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);

void		rhythmdb_add_uri	(RhythmDB *db, const char *uri);
void		rhythmdb_add_uri_with_type	(RhythmDB *db, const char *uri, RhythmDBEntryType type);

void		rhythmdb_entry_set	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, const GValue *value);
void		rhythmdb_entry_set_nonotify	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, const GValue *value);
void		rhythmdb_entry_set_uninserted   (RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, const GValue *value);

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
							 GtkTreeModel *main_model,
							 ...);
void		rhythmdb_do_full_query_parsed		(RhythmDB *db,
							 GtkTreeModel *main_model,
							 GPtrArray *query);

void		rhythmdb_do_full_query_async		(RhythmDB *db, GtkTreeModel *main_model, ...);

void		rhythmdb_do_full_query_async_parsed	(RhythmDB *db, GtkTreeModel *main_model,
							 GPtrArray *query);

void		rhythmdb_query_cancel			(RhythmDB *db, GtkTreeModel *query_model);

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

inline const xmlChar *	rhythmdb_nice_elt_name_from_propid	(RhythmDB *db, RhythmDBPropType propid);
inline int		rhythmdb_propid_from_nice_elt_name	(RhythmDB *db, const xmlChar *name);

void		rhythmdb_emit_entry_added		(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_deleted		(RhythmDB *db, RhythmDBEntry *entry);

gboolean	rhythmdb_is_busy			(RhythmDB *db);
char *		rhythmdb_compute_status_normal		(gint n_songs, glong duration,
							 GnomeVFSFileSize size);
RhythmDBEntryType rhythmdb_entry_register_type          (void);

RhythmDBEntryType rhythmdb_entry_song_get_type          (void);
RhythmDBEntryType rhythmdb_entry_iradio_get_type        (void);
RhythmDBEntryType rhythmdb_entry_podcast_post_get_type  (void);
RhythmDBEntryType rhythmdb_entry_podcast_feed_get_type  (void);
RhythmDBEntryType rhythmdb_entry_icecast_get_type        (void);

extern GType rhythmdb_property_type_map[RHYTHMDB_NUM_PROPERTIES];
G_INLINE_FUNC GType rhythmdb_get_property_type		(RhythmDB *db, guint property_id);

#if defined (G_CAN_INLINE) || defined (__RHYTHMDB_C__)

G_INLINE_FUNC GType
rhythmdb_get_property_type (RhythmDB *db, guint property_id)
{
	g_assert (property_id >= 0 && property_id < RHYTHMDB_NUM_PROPERTIES);
	return rhythmdb_property_type_map[property_id];
}

#endif

void rhythmdb_entry_ref (RhythmDB *db, RhythmDBEntry *entry);

void rhythmdb_entry_unref (RhythmDB *db, RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RHYTHMBDB_H */
