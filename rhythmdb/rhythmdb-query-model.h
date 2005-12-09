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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"
#include <glib-object.h>
#include <glib.h>

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
	void	(*complete)		(void);
	void	(*entry_prop_changed)	(RhythmDBEntry *entry, RhythmDBPropType prop,
					 const GValue *old, const GValue *new);
	void    (*non_entry_dropped)    (const char *uri);
	void    (*entry_removed)        (RhythmDBEntry *entry);

} RhythmDBQueryModelClass;

GType			rhythmdb_query_model_get_type	(void);

RhythmDBQueryModel *	rhythmdb_query_model_new	(RhythmDB *db, GPtrArray *query,
							 GCompareDataFunc sort_func,
							 gpointer user_data);

RhythmDBQueryModel *	rhythmdb_query_model_new_empty	(RhythmDB *db);

void			rhythmdb_query_model_add_entry	(RhythmDBQueryModel *model, RhythmDBEntry *entry);
void			rhythmdb_query_model_add_entries(RhythmDBQueryModel *model, GPtrArray *entries);

gboolean		rhythmdb_query_model_remove_entry	(RhythmDBQueryModel *model, RhythmDBEntry *entry);

void			rhythmdb_query_model_set_connected	(RhythmDBQueryModel *model, gboolean connected);

void			rhythmdb_query_model_signal_complete	(RhythmDBQueryModel *model);

GnomeVFSFileSize	rhythmdb_query_model_get_size	(RhythmDBQueryModel *model);

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
char *			rhythmdb_query_model_compute_status_normal (RhythmDBQueryModel *model);

void			rhythmdb_query_model_set_sort_order (RhythmDBQueryModel *model,
							     GCompareDataFunc sort_func,
							     gpointer user_data,
							     GDestroyNotify sort_destroy_notify);

G_END_DECLS

#endif /* __RHYTHMBDB_QUERY_MODEL_H */
