/*
 *  arch-tag: Header for RhythmDB query result GtkTreeModel impl.
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"

#ifndef RHYTHMDB_QUERY_MODEL_H
#define RHYTHMDB_QUERY_MODEL_H

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_QUERY_MODEL         (rhythmdb_query_model_get_type ())
#define RHYTHMDB_QUERY_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_QUERY_MODEL, RhythmDBQueryModel))
#define RHYTHMDB_QUERY_MODEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE_QUERY_MODEL, RhythmDBQueryModelClass))
#define RHYTHMDB_IS_QUERY_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_QUERY_MODEL))
#define RHYTHMDB_IS_QUERY_MODEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_QUERY_MODEL))
#define RHYTHMDB_QUERY_MODEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_QUERY_MODEL, RhythmDBQueryModelClass))

GType rhythmdb_query_model_limit_type_get_type (void);
#define RHYTHMDB_TYPE_QUERY_MODEL_LIMIT_TYPE (rhythmdb_query_model_limit_type_get_type ())

typedef enum {
	RHYTHMDB_QUERY_MODEL_LIMIT_NONE,
	RHYTHMDB_QUERY_MODEL_LIMIT_COUNT,
	RHYTHMDB_QUERY_MODEL_LIMIT_SIZE,
	RHYTHMDB_QUERY_MODEL_LIMIT_TIME,
} RhythmDBQueryModelLimitType;

typedef struct RhythmDBQueryModelPrivate RhythmDBQueryModelPrivate;

#define RHYTHMDB_QUERY_MODEL_SUGGESTED_UPDATE_CHUNK 32

typedef struct
{
	GObject parent;

	RhythmDBQueryModelPrivate *priv;
} RhythmDBQueryModel;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*complete)		(RhythmDBQueryModel *model);
	void	(*entry_prop_changed)	(RhythmDBQueryModel *model,
					 RhythmDBEntry *entry,
					 RhythmDBPropType prop,
					 const GValue *old,
					 const GValue *new_value);
	void    (*non_entry_dropped)    (RhythmDBQueryModel *model,
					 const char *uri,
					 int position);
	void    (*entry_removed)        (RhythmDBQueryModel *model,
					 RhythmDBEntry *entry);
	void	(*post_entry_delete)	(RhythmDBQueryModel *model,
					 RhythmDBEntry *entry);

} RhythmDBQueryModelClass;

GType			rhythmdb_query_model_get_type	(void);

RhythmDBQueryModel *	rhythmdb_query_model_new	(RhythmDB *db, GPtrArray *query,
							 GCompareDataFunc sort_func,
							 gpointer sort_data,
							 GDestroyNotify sort_data_destroy,
							 gboolean sort_reverse);

RhythmDBQueryModel *	rhythmdb_query_model_new_empty	(RhythmDB *db);

void			rhythmdb_query_model_chain	(RhythmDBQueryModel *child,
							 RhythmDBQueryModel *base,
							 gboolean import_entries);

void			rhythmdb_query_model_add_entry	(RhythmDBQueryModel *model, RhythmDBEntry *entry, gint index);

gboolean		rhythmdb_query_model_remove_entry	(RhythmDBQueryModel *model, RhythmDBEntry *entry);

void			rhythmdb_query_model_move_entry (RhythmDBQueryModel *model, RhythmDBEntry *entry, gint index);

guint64	rhythmdb_query_model_get_size	(RhythmDBQueryModel *model);

long			rhythmdb_query_model_get_duration(RhythmDBQueryModel *model);

gboolean		rhythmdb_query_model_entry_to_iter(RhythmDBQueryModel *model,
							   RhythmDBEntry *entry,
							   GtkTreeIter *iter);

gboolean		rhythmdb_query_model_has_pending_changes (RhythmDBQueryModel *model);

RhythmDBEntry *		rhythmdb_query_model_tree_path_to_entry(RhythmDBQueryModel *model,
								GtkTreePath *path);
RhythmDBEntry *		rhythmdb_query_model_iter_to_entry(RhythmDBQueryModel *model,
							   GtkTreeIter *entry_iter);
RhythmDBEntry *		rhythmdb_query_model_get_next_from_entry(RhythmDBQueryModel *model,
								 RhythmDBEntry *entry);
RhythmDBEntry *		rhythmdb_query_model_get_previous_from_entry(RhythmDBQueryModel *model,
								     RhythmDBEntry *entry);
char *			rhythmdb_query_model_compute_status_normal (RhythmDBQueryModel *model,
								    const char *singular,
								    const char *plural);

void			rhythmdb_query_model_set_sort_order (RhythmDBQueryModel *model,
							     GCompareDataFunc sort_func,
							     gpointer sort_data,
							     GDestroyNotify sort_data_destroy,
							     gboolean sort_reverse);

void			rhythmdb_query_model_reapply_query (RhythmDBQueryModel *model, gboolean filter);

gint 			rhythmdb_query_model_location_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
								 gpointer data);

gint 			rhythmdb_query_model_string_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							       gpointer data);

gint 			rhythmdb_query_model_title_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							      gpointer data);

gint 			rhythmdb_query_model_album_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							      gpointer data);

gint 			rhythmdb_query_model_artist_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							       gpointer data);

gint 			rhythmdb_query_model_genre_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							      gpointer data);

gint 			rhythmdb_query_model_track_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							      gpointer data);

gint 			rhythmdb_query_model_double_ceiling_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
								       gpointer data);

gint 			rhythmdb_query_model_ulong_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							      gpointer data);

gint 			rhythmdb_query_model_date_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
							     gpointer data);
G_END_DECLS

#endif /* __RHYTHMDB_QUERY_MODEL_H */
