/*
 *  arch-tag: Header for RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

G_BEGIN_DECLS

#define RHYTHMDB_TYPE      (rhythmdb_get_type ())
#define RHYTHMDB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE, RhythmDB))
#define RHYTHMDB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE, RhythmDBClass))
#define RHYTHMDB_IS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE))
#define RHYTHMDB_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE))
#define RHYTHMDB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE, RhythmDBClass))

typedef enum
{
	RHYTHMDB_ENTRY_TYPE_SONG,
	RHYTHMDB_ENTRY_TYPE_IRADIO_STATION
} RhythmDBEntryType;

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
} RhythmDBQueryType;

#define RHYTHMDB_NUM_PROPERTIES 23
#define RHYTHMDB_NUM_SAVED_PROPERTIES 14

typedef enum
{
	RHYTHMDB_PROP_TYPE,
	RHYTHMDB_PROP_TITLE,
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_TRACK_NUMBER,
	RHYTHMDB_PROP_DURATION,
	RHYTHMDB_PROP_FILE_SIZE,
	RHYTHMDB_PROP_LOCATION,
	RHYTHMDB_PROP_MTIME,
	RHYTHMDB_PROP_RATING,
	RHYTHMDB_PROP_PLAY_COUNT,
	RHYTHMDB_PROP_LAST_PLAYED,
	RHYTHMDB_PROP_QUALITY,
} RhythmDBPropType;

typedef enum
{
	RHYTHMDB_PROP_TITLE_SORT_KEY = 14,
	RHYTHMDB_PROP_GENRE_SORT_KEY,
	RHYTHMDB_PROP_ARTIST_SORT_KEY,
	RHYTHMDB_PROP_ALBUM_SORT_KEY,
	RHYTHMDB_PROP_TITLE_FOLDED,
	RHYTHMDB_PROP_GENRE_FOLDED,
	RHYTHMDB_PROP_ARTIST_FOLDED,
	RHYTHMDB_PROP_ALBUM_FOLDED,
	RHYTHMDB_PROP_LAST_PLAYED_STR,
} RhythmDBUnsavedPropType;

GType rhythmdb_query_get_type (void);
GType rhythmdb_prop_get_type (void);
GType rhythmdb_unsaved_prop_get_type (void);

#define RHYTHMDB_TYPE_QUERY (rhythmdb_query_get_type ())
#define RHYTHMDB_TYPE_PROP (rhythmdb_prop_get_type ())
#define RHYTHMDB_TYPE_UNSAVED_PROP (rhythmdb_unsaved_prop_get_type ())

typedef struct {
	guint type;
	guint propid;
	GValue *val;
	GPtrArray *subquery;
} RhythmDBQueryData;

typedef void RhythmDBEntry;

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
	void	(*entry_restored)	(RhythmDBEntry *entry);
	void	(*entry_changed)	(RhythmDBEntry *entry);
	void	(*entry_deleted)	(RhythmDBEntry *entry);
	void	(*load_complete)	(RhythmDBEntry *entry);
	void	(*legacy_load_complete)	(RhythmDBEntry *entry);
	void	(*error)		(const char *uri, const char *msg);

	/* virtual methods */

	void		(*impl_load)		(RhythmDB *db, gboolean *dead);
	void		(*impl_save)		(RhythmDB *db);
	
	RhythmDBEntry *	(*impl_entry_new)	(RhythmDB *db, RhythmDBEntryType type,
						 const char *uri);

	void		(*impl_entry_set)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_get)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_delete)	(RhythmDB *db, RhythmDBEntry *entry);
	RhythmDBEntry *	(*impl_lookup_by_location)(RhythmDB *db, const char *uri);

	gboolean 	(*impl_evaluate_query)	(RhythmDB *db, GPtrArray *query, RhythmDBEntry *entry);

	void		(*impl_do_full_query)	(RhythmDB *db, GPtrArray *query,
						 GtkTreeModel *main_model,
						 gboolean *cancel);
} RhythmDBClass;

GType		rhythmdb_get_type	(void);

RhythmDB *	rhythmdb_new		(const char *name);

void		rhythmdb_shutdown	(RhythmDB *db);

/**
 * This function must be called WITHOUT the RhythmDB lock held!
 */
void		rhythmdb_load		(RhythmDB *db);

void		rhythmdb_save		(RhythmDB *db);

void		rhythmdb_read_lock	(RhythmDB *db);
void		rhythmdb_write_lock	(RhythmDB *db);
void		rhythmdb_read_unlock	(RhythmDB *db);
void		rhythmdb_write_unlock	(RhythmDB *db);

RhythmDBEntry *	rhythmdb_entry_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);

void		rhythmdb_add_uri_async	(RhythmDB *db, const char *uri);
RhythmDBEntry *	rhythmdb_add_song	(RhythmDB *db, const char *uri, GError **error);

void		rhythmdb_entry_set	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, GValue *value);

void		rhythmdb_entry_queue_set(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, GValue *value);

void		rhythmdb_entry_get	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, GValue *value);

#ifndef WITH_RHYTHMDB_TREE
#define rhythmdb_entry_ref(DB, ENTRY) 
#define rhythmdb_entry_ref_unlocked(DB, ENTRY) 
#define rhythmdb_entry_unref(DB, ENTRY) 
#define rhythmdb_entry_unref_unlocked(DB, ENTRY) 
#else
#include "rhythmdb-tree.h"
#endif

void		rhythmdb_entry_delete	(RhythmDB *db, RhythmDBEntry *entry);

const char *	rhythmdb_entry_get_string	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
gboolean	rhythmdb_entry_get_boolean	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
long		rhythmdb_entry_get_long		(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
int		rhythmdb_entry_get_int		(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
double		rhythmdb_entry_get_double	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
float		rhythmdb_entry_get_float	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
gpointer	rhythmdb_entry_get_pointer	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);

RhythmDBEntry *	rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri);

gboolean	rhythmdb_evaluate_query		(RhythmDB *db, GPtrArray *query,
						 RhythmDBEntry *entry);

/**
 * Returns a freshly allocated GtkTreeModel which represents the query.
 * The extended arguments alternate between RhythmDBQueryType args
 * and their values.  Here's an example:
 *
 * rhythmdb_do_full_query (db, RHYTHMDB_QUERY_PROP_EQUALS, "genre", "Classical",
 *                          RHYTHMDB_QUERY_PROP_GREATER, "rating", 5,
 *                          RHYTHMDB_QUERY_END);
 *
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

GType		rhythmdb_get_property_type		(RhythmDB *db, guint property_id);

void		rhythmdb_entry_sync_mirrored		(RhythmDB *db, RhythmDBEntry *entry,
							 guint propid, GValue *value);

GPtrArray *	rhythmdb_query_parse			(RhythmDB *db, ...);
void		rhythmdb_query_append			(RhythmDB *db, GPtrArray *query, ...);
void		rhythmdb_query_free			(GPtrArray *query);
GPtrArray *	rhythmdb_query_copy			(GPtrArray *array);

void		rhythmdb_query_serialize		(RhythmDB *db, GPtrArray *query,
							 xmlNodePtr node);

GPtrArray *	rhythmdb_query_deserialize		(RhythmDB *db, xmlNodePtr node);

inline const char *	rhythmdb_nice_elt_name_from_propid	(RhythmDB *db, gint propid);
inline int		rhythmdb_propid_from_nice_elt_name	(RhythmDB *db, const char *name);

void		rhythmdb_emit_entry_added		(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_restored		(RhythmDB *db, RhythmDBEntry *entry);
void		rhythmdb_emit_entry_deleted		(RhythmDB *db, RhythmDBEntry *entry);

void		rhythmdb_load_legacy			(RhythmDB *db);
RhythmDBEntry * rhythmdb_legacy_id_to_entry		(RhythmDB *db, guint id);

char *		rhythmdb_get_status			(RhythmDB *db);
char *		rhythmdb_compute_status_normal		(gint n_songs, glong duration,
							 GnomeVFSFileSize size);

G_END_DECLS

#endif /* __RHYTHMBDB_H */
