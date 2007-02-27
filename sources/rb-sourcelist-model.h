/*
 *  arch-tag: Header for GtkTreeModel impl. containing RBSource objects
 *
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_SOURCELIST_MODEL_H
#define __RB_SOURCELIST_MODEL_H

#include <gtk/gtktreemodelfilter.h>

#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_SOURCELIST_MODEL		(rb_sourcelist_model_get_type ())
#define RB_SOURCELIST_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_SOURCELIST_MODEL, RBSourceListModel))
#define RB_SOURCELIST_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_SOURCELIST_MODEL, RBSourceListModelClass))
#define RB_IS_SOURCELIST_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_SOURCELIST_MODEL))
#define RB_IS_SOURCELIST_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_SOURCELIST_MODEL))
#define RB_SOURCELIST_MODEL_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_SOURCELIST_MODEL, RBSourceListModelClass))

typedef enum {
	RB_SOURCELIST_MODEL_COLUMN_PLAYING = 0,
	RB_SOURCELIST_MODEL_COLUMN_PIXBUF,
	RB_SOURCELIST_MODEL_COLUMN_NAME,
	RB_SOURCELIST_MODEL_COLUMN_SOURCE,
	RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES,
	RB_SOURCELIST_MODEL_COLUMN_VISIBILITY,
	RB_SOURCELIST_MODEL_N_COLUMNS
} RBSourceListModelColumn;

GType rb_sourcelist_model_column_get_type (void);
#define RB_TYPE_SOURCELIST_MODEL_COLUMN (rb_sourcelist_model_column_get_type ())


typedef struct RBSourceListModelPrivate RBSourceListModelPrivate;

typedef struct RBSourceListModel
{
	GtkTreeModelFilter parent;

	RBSourceListModelPrivate *priv;
} RBSourceListModel;

typedef struct RBSourceListModelClass
{
	GtkTreeModelFilterClass parent_class;

	void (*drop_received) (RBSourceListModel *model, RBSource *target, GtkTreeViewDropPosition pos, GtkSelectionData *data);

} RBSourceListModelClass;


GType		rb_sourcelist_model_get_type	(void);

GtkTreeModel *	rb_sourcelist_model_new		(void);

void		rb_sourcelist_model_set_dnd_targets (RBSourceListModel *sourcelist,
						     GtkTreeView *treeview);
GtkTreePath *	rb_sourcelist_model_get_group_path (RBSourceListModel *sourcelist,
						    RBSourceListGroup group);
gboolean	rb_sourcelist_model_row_is_separator (GtkTreeModel *model,
						      GtkTreeIter *iter,
						      RBSourceListModel *sourcelist);

G_END_DECLS

#endif /* __RB_SOURCELIST_H */
