/*
 *  arch-tag: Header for RhythmDB model interface
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

#ifndef __RHYTHMDB_MODEL_H
#define __RHYTHMDB_MODEL_H

#include "rhythmdb.h"

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_MODEL         (rhythmdb_model_get_type ())
#define RHYTHMDB_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_MODEL, RhythmDBModel))
#define RHYTHMDB_IS_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_MODEL))
#define RHYTHMDB_MODEL_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RHYTHMDB_TYPE_MODEL, RhythmDBModelIface))

typedef struct RhythmDBModel RhythmDBModel;

typedef struct
{
	GTypeInterface g_iface;

	/* methods */
	gboolean	(*entry_to_iter)	(RhythmDBModel *model, RhythmDBEntry *entry,
						 GtkTreeIter *iter);
	gboolean	(*poll)			(RhythmDBModel *model, GTimeVal *timeout);
	void		(*cancel)		(RhythmDBModel *model);
} RhythmDBModelIface;

GType		rhythmdb_model_get_type		(void);

gboolean	rhythmdb_model_entry_to_iter	(RhythmDBModel *model, RhythmDBEntry *entry,
						 GtkTreeIter *iter);

gboolean	rhythmdb_model_poll		(RhythmDBModel *model, GTimeVal *timeout);

void		rhythmdb_model_cancel		(RhythmDBModel *model);

G_END_DECLS

#endif /* __RHYTHMDB_MODEL_H */
