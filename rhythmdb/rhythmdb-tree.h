/*
 *  arch-tag: Header for RhythmDB tree-structured database implementation
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

#ifndef RHYTHMDB_TREE_H
#define RHYTHMDB_TREE_H

#include "rhythmdb.h"
#include "rb-atomic.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_TREE         (rhythmdb_tree_get_type ())
#define RHYTHMDB_TREE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_TREE, RhythmDBTree))
#define RHYTHMDB_TREE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TREE_TYPE, RhythmDBTreeClass))
#define RHYTHMDB_IS_TREE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_TREE))
#define RHYTHMDB_IS_TREE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_TREE))
#define RHYTHMDB_TREE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_TREE, RhythmDBTreeClass))

typedef struct RhythmDBTreePrivate RhythmDBTreePrivate;

typedef struct
{
	RhythmDB parent;

	RhythmDBTreePrivate *priv;
} RhythmDBTree;

typedef struct
{
	RhythmDBClass parent;

} RhythmDBTreeClass;

GType		rhythmdb_tree_get_type	(void);

RhythmDB *	rhythmdb_tree_new	(const char *name);

void		rhythmdb_tree_entry_destroy	(RhythmDBTree *db, RhythmDBEntry *entry);

/* PRIVATE */

typedef struct RhythmDBTreeProperty
{
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	struct RhythmDBTreeProperty *parent;
	char *name;
	char *folded;
	char *sort_key;
	GHashTable *children;
} RhythmDBTreeProperty;

/* Optimization possibility - note that we aren't using at least
 * three values in the array; the genre/artist/album names are
 * actually stored in the tree structure. */
typedef struct
{
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	gboolean deleted;
	RBAtomic refcount;
	RhythmDBTreeProperty *album;
	GValue properties[RHYTHMDB_NUM_PROPERTIES];
} RhythmDBTreeEntry;

#define rhythmdb_entry_ref_unlocked(DB,ENTRY) rhythmdb_entry_ref (DB, ENTRY)

static inline void
rhythmdb_entry_ref (RhythmDB *adb, RhythmDBEntry *aentry)
{
	RhythmDBTreeEntry *entry = (RhythmDBTreeEntry *) aentry;

	rb_atomic_inc (&entry->refcount);
}


static inline void
rhythmdb_entry_unref (RhythmDB *adb, RhythmDBEntry *aentry)
{
	RhythmDBTree *db = (RhythmDBTree *) adb;
	RhythmDBTreeEntry *entry = (RhythmDBTreeEntry *) aentry;

	if (rb_atomic_dec (&entry->refcount) <= 1) {
		rhythmdb_write_lock (adb);
		rhythmdb_tree_entry_destroy (db, entry);
		rhythmdb_write_unlock (adb);
	}
}

static inline void
rhythmdb_entry_unref_unlocked (RhythmDB *adb, RhythmDBEntry *aentry)
{
	RhythmDBTree *db = (RhythmDBTree *) adb;
	RhythmDBTreeEntry *entry = (RhythmDBTreeEntry *) aentry;

	if (rb_atomic_dec (&entry->refcount) <= 1) {
		rhythmdb_tree_entry_destroy (db, entry);
	}
}

G_END_DECLS

#endif /* __RHYTHMBDB_TREE_H */
