/*
 *  arch-tag: Header for spiced up, bug-injected, hacker-hated, gtktreeview hack
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
 *
 *  --------------------------------------------------------------
 *
 *  Welcome to RBTreeView, the spiced up, bug-injected, hacker-hated,
 *  gtktreeview hack.  Please stop reading RIGHT NOW if you are offended easily,
 *  or are a treeview developer.
 */

#ifndef __RB_TREE_VIEW_H
#define __RB_TREE_VIEW_H

#include <gtk/gtktreeview.h>

G_BEGIN_DECLS

#define RB_TYPE_TREE_VIEW         (rb_tree_view_get_type ())
#define RB_TREE_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TREE_VIEW, RBTreeView))
#define RB_TREE_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TREE_VIEW, RBTreeViewClass))
#define RB_IS_TREE_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TREE_VIEW))
#define RB_IS_TREE_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TREE_VIEW))
#define RB_TREE_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TREE_VIEW, RBTreeViewClass))

typedef struct
{
	GtkTreeView parent;
} RBTreeView;

typedef struct
{
	GtkTreeViewClass parent;
} RBTreeViewClass;

GType		rb_tree_view_get_type		(void);

RBTreeView *	rb_tree_view_new		(void);
/* may kick */
RBTreeView *	rb_tree_view_new_with_model	(GtkTreeModel *model);

G_END_DECLS

#endif /* __RB_TREE_VIEW_H */
