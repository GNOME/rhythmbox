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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef __RB_SOURCELIST_MODEL_H
#define __RB_SOURCELIST_MODEL_H

#include <gtk/gtkliststore.h>

#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_SOURCELIST_MODEL		(rb_sourcelist_model_get_type ())
#define RB_SOURCELIST_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_SOURCELIST_MODEL, RBSourceListModel))
#define RB_SOURCELIST_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_SOURCELIST_MODEL, RBSourceListModelClass))
#define RB_IS_SOURCELIST_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_SOURCELIST_MODEL))
#define RB_IS_SOURCELIST_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_SOURCELIST_MODEL))
#define RB_SOURCELIST_MODEL_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_SOURCELIST_MODEL, RBSourceListModelClass))

typedef struct RBSourceListModelPriv RBSourceListModelPriv;

typedef struct RBSourceListModel
{
	GtkListStore parent;

	RBSourceListModelPriv *priv;
} RBSourceListModel;

typedef struct RBSourceListModelClass
{
	GtkListStoreClass parent_class;

	void (*drop_received) (RBSourceListModel *model, RBSource *target, GtkTreeViewDropPosition pos, GtkSelectionData *data);

} RBSourceListModelClass;

#define RB_SOURCELIST_MODEL_COLUMN_PIXBUF 0
#define RB_SOURCELIST_MODEL_COLUMN_NAME 1
#define RB_SOURCELIST_MODEL_COLUMN_SOURCE 2
#define RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES 3

GType		rb_sourcelist_model_get_type	(void);

GtkTreeModel *	rb_sourcelist_model_new		(void);

G_END_DECLS

#endif /* __RB_SOURCELIST_H */
