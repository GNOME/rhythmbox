/* 
 *  arch-tag: Header for RhythmDB property GtkTreeModel impl.
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
#include "rhythmdb-query-model.h"

#ifndef RHYTHMDB_PROPERTY_MODEL_H
#define RHYTHMDB_PROPERTY_MODEL_H

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_PROPERTY_MODEL         (rhythmdb_property_model_get_type ())
#define RHYTHMDB_PROPERTY_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_PROPERTY_MODEL, RhythmDBPropertyModel))
#define RHYTHMDB_PROPERTY_MODEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_PROPERTY_MODEL_TYPE, RhythmDBPropertyModelClass))
#define RHYTHMDB_IS_PROPERTY_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_PROPERTY_MODEL))
#define RHYTHMDB_IS_PROPERTY_MODEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_PROPERTY_MODEL))
#define RHYTHMDB_PROPERTY_MODEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_PROPERTY_MODEL, RhythmDBPropertyModelClass))

typedef enum
{
	RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE,
	RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY,
	RHYTHMDB_PROPERTY_MODEL_COLUMN_LAST,
} RhythmDBPropertyModelColumn;

typedef struct RhythmDBPropertyModelPrivate RhythmDBPropertyModelPrivate;

typedef struct
{
	GObject parent;

	RhythmDBPropertyModelPrivate *priv;
} RhythmDBPropertyModel;

typedef struct
{
	GObjectClass parent;

	void (*pre_row_deletion) (void);
} RhythmDBPropertyModelClass;

GType			rhythmdb_property_model_get_type	(void);

RhythmDBPropertyModel *	rhythmdb_property_model_new		(RhythmDB *db, RhythmDBPropType propid);

G_END_DECLS

#endif /* __RHYTHMBDB_PROPERTY_MODEL_H */
