/*
 *  arch-tag: Header for small wrapper around GtkTreeViewColumn
 * 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_TREE_VIEW_COLUMN_H
#define __RB_TREE_VIEW_COLUMN_H

#include <gtk/gtktreeviewcolumn.h>

G_BEGIN_DECLS

#define RB_TYPE_TREE_VIEW_COLUMN         (rb_tree_view_column_get_type ())
#define RB_TREE_VIEW_COLUMN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TREE_VIEW_COLUMN, RBTreeViewColumn))
#define RB_TREE_VIEW_COLUMN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TREE_VIEW_COLUMN, RBTreeViewColumnClass))
#define RB_IS_TREE_VIEW_COLUMN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TREE_VIEW_COLUMN))
#define RB_IS_TREE_VIEW_COLUMN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TREE_VIEW_COLUMN))
#define RB_TREE_VIEW_COLUMN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TREE_VIEW_COLUMN, RBTreeViewColumnClass))

typedef struct RBTreeViewColumnPrivate RBTreeViewColumnPrivate;

typedef struct
{
	GtkTreeViewColumn parent;

	RBTreeViewColumnPrivate *priv;
} RBTreeViewColumn;

typedef struct
{
	GtkTreeViewColumnClass parent;
} RBTreeViewColumnClass;

GType             rb_tree_view_column_get_type       (void);

RBTreeViewColumn *rb_tree_view_column_new            (void);

gboolean          rb_tree_view_column_get_expand     (RBTreeViewColumn *column);
void              rb_tree_view_column_set_expand     (RBTreeViewColumn *column,
						      gboolean expand);

GList            *rb_tree_view_column_get_sort_order (RBTreeViewColumn *column);
void              rb_tree_view_column_set_sort_order (RBTreeViewColumn *column,
						      GList *sort_order);

G_END_DECLS

#endif /* __RB_TREE_VIEW_COLUMN_H */
