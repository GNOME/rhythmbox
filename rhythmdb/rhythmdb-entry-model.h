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

#ifndef RHYTHMDB_ENTRY_MODEL_H
#define RHYTHMDB_ENTRY_MODEL_H

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_ENTRY_MODEL         (rhythmdb_entry_model_get_type ())
#define RHYTHMDB_ENTRY_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY_MODEL, RhythmDBEntryModel))
#define RHYTHMDB_ENTRY_MODEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_ENTRY_MODEL_TYPE, RhythmDBEntryModelClass))
#define RHYTHMDB_IS_ENTRY_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_ENTRY_MODEL))
#define RHYTHMDB_IS_ENTRY_MODEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_ENTRY_MODEL))
#define RHYTHMDB_ENTRY_MODEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_ENTRY_MODEL, RhythmDBEntryModelClass))

typedef struct
{
	GtkListStore parent;

	RhythmDBEntryModelPrivate *priv;
} RhythmDBEntryModel;

typedef struct
{
	GtkListStoreClass parent;

} RhythmDBEntryModelClass;

GType		rhythmdb_entry_model_get_type	(void);

RhythmDB *	rhythmdb_entry_model_new	(void);

RhythmDB *	rhythmdb_entry_model_new_from_hash	(GHashTable *table);

RhythmDB *	rhythmdb_entry_model_new_from_ptr_array	(GPtrArray *array);

G_END_DECLS

#endif /* __RHYTHMBDB_ENTRY_MODEL_H */
