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

#include "rhythmdb-private.h"
#include <glib-object.h>
#include <glib/gatomic.h>

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

G_END_DECLS

#endif /* __RHYTHMBDB_TREE_H */
