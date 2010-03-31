/*
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-query-model.h>

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
	RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER,
	RHYTHMDB_PROPERTY_MODEL_COLUMN_LAST,
} RhythmDBPropertyModelColumn;

GType rhythmdb_property_model_column_get_type (void);
#define RHYTHMDB_TYPE_PROPERTY_MODEL_COLUMN (rhythmdb_property_model_column_get_type ())

typedef struct _RhythmDBPropertyModel RhythmDBPropertyModel;
typedef struct _RhythmDBPropertyModelClass RhythmDBPropertyModelClass;
typedef struct RhythmDBPropertyModelPrivate RhythmDBPropertyModelPrivate;

struct _RhythmDBPropertyModel
{
	GObject parent;

	RhythmDBPropertyModelPrivate *priv;
};

struct _RhythmDBPropertyModelClass
{
	GObjectClass parent;

	void (*pre_row_deletion) (RhythmDBPropertyModel *model);
};

GType			rhythmdb_property_model_get_type	(void);

RhythmDBPropertyModel *	rhythmdb_property_model_new		(RhythmDB *db, RhythmDBPropType propid);

gboolean		rhythmdb_property_model_iter_from_string(RhythmDBPropertyModel *model, const char *name, GtkTreeIter *iter);

void			rhythmdb_property_model_enable_drag	(RhythmDBPropertyModel *model, GtkTreeView *view);

G_END_DECLS

#endif /* __RHYTHMBDB_PROPERTY_MODEL_H */
