/*  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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
 *  $Id$
 */

#ifndef __RB_TREE_MODEL_SORT_H
#define __RB_TREE_MODEL_SORT_H

#include <glib-object.h>

#include <gtk/gtktreemodelsort.h>

G_BEGIN_DECLS

#define RB_TYPE_TREE_MODEL_SORT         (rb_tree_model_sort_get_type ())
#define RB_TREE_MODEL_SORT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TREE_MODEL_SORT, RBTreeModelSort))
#define RB_TREE_MODEL_SORT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TREE_MODEL_SORT, RBTreeModelSortClass))
#define RB_IS_TREE_MODEL_SORT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TREE_MODEL_SORT))
#define RB_IS_TREE_MODEL_SORT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TREE_MODEL_SORT))
#define RB_TREE_MODEL_SORT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TREE_MODEL_SORT, RBTreeModelSortClass))

typedef struct RBTreeModelSortPrivate RBTreeModelSortPrivate;

typedef struct
{
	GtkTreeModelSort parent;

	RBTreeModelSortPrivate *priv;
} RBTreeModelSort;

typedef struct
{
	GtkTreeModelSortClass parent_class;

	void (*node_from_iter) (RBTreeModelSort *model, GtkTreeIter *iter, void **node);
} RBTreeModelSortClass;

GType		rb_tree_model_sort_get_type	(void);

GtkTreeModel   *rb_tree_model_sort_new 		(GtkTreeModel *child_model);


G_END_DECLS

#endif /* __RB_TREE_MODEL_SORT_H */
