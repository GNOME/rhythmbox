/*
 *  arch-tag: Header for RhythmDB libgda/SQLite database
 *
 *  Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

#ifndef RHYTHMDB_GDA_H
#define RHYTHMDB_GDA_H

#include "rhythmdb.h"
#include <libgda/libgda.h>

G_BEGIN_DECLS


#define RHYTHMDB_TYPE_GDA         (rhythmdb_gda_get_type ())
#define RHYTHMDB_GDA(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_GDA, RhythmDBGda))
#define RHYTHMDB_GDA_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_GDA_TYPE, RhythmDBGdaClass))
#define RHYTHMDB_IS_GDA(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_GDA))
#define RHYTHMDB_IS_GDA_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_GDA))
#define RHYTHMDB_GDA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_GDA, RhythmDBGdaClass))

typedef struct RhythmDBGdaPrivate RhythmDBGdaPrivate;

typedef struct
{
	RhythmDB rhythmdb;

	GdaClient *client;
	GdaConnection *conn;

} RhythmDBGda;

typedef struct
{
	RhythmDBClass rhythmdb_class;

} RhythmDBGdaClass;

GType		rhythmdb_gda_get_type	(void);

RhythmDB *	rhythmdb_gda_new	(const char *name);

void		rhythmdb_gda_ref	(RhythmDBGda *db, gint id, gint count);
#define rhythmdb_entry_ref(DB, ENTRY) rhythmdb_gda_ref (RHYTHMDB_GDA (DB), GPOINTER_TO_INT (ENTRY), 1)
#define rhythmdb_entry_unref(DB, ENTRY) rhythmdb_gda_ref (RHYTHMDB_GDA (DB), GPOINTER_TO_INT (ENTRY), -1)

G_END_DECLS

#endif /* __RHYTHMBDB_GDA_H */
