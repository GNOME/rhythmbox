/*
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
 *  $Id$
 */

#ifndef __RB_NODE_VIEW_H
#define __RB_NODE_VIEW_H

#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkdnd.h>

#include "rb-tree-model-node.h"
#include "rb-enums.h"

G_BEGIN_DECLS

#define RB_TYPE_NODE_VIEW         (rb_node_view_get_type ())
#define RB_NODE_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_VIEW, RBNodeView))
#define RB_NODE_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_VIEW, RBNodeViewClass))
#define RB_IS_NODE_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_VIEW))
#define RB_IS_NODE_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_VIEW))
#define RB_NODE_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_VIEW, RBNodeViewClass))

typedef struct RBNodeViewPrivate RBNodeViewPrivate;

typedef struct
{
	GtkScrolledWindow parent;

	RBNodeViewPrivate *priv;
} RBNodeView;

typedef struct
{
	GtkScrolledWindowClass parent;

	void (*node_selected)  (RBNodeView *view, RBNode *node);
	void (*node_activated) (RBNodeView *view, RBNode *node);

	void (*changed)        (RBNodeView *view);
} RBNodeViewClass;

GType       rb_node_view_get_type          (void);

RBNodeView *rb_node_view_new                (RBNode *root,
					     const char *view_desc_file);

void        rb_node_view_set_filter         (RBNodeView *view,
				             RBNode *filter_parent,
					     RBNode *filter_artist);

void        rb_node_view_set_playing_node   (RBNodeView *view,
					     RBNode *node);
RBNode     *rb_node_view_get_playing_node   (RBNodeView *view);

RBNode     *rb_node_view_get_next_node      (RBNodeView *view);
RBNode     *rb_node_view_get_previous_node  (RBNodeView *view);
RBNode     *rb_node_view_get_first_node     (RBNodeView *view);
RBNode     *rb_node_view_get_random_node    (RBNodeView *view);

gboolean    rb_node_view_have_selection     (RBNodeView *view);
GList      *rb_node_view_get_selection      (RBNodeView *view);
void        rb_node_view_select_all         (RBNodeView *view);
void        rb_node_view_select_none        (RBNodeView *view);
void        rb_node_view_select_node        (RBNodeView *view,
					     RBNode *node);

char       *rb_node_view_get_status         (RBNodeView *view);

gboolean    rb_node_view_get_node_visible   (RBNodeView *view,
					     RBNode *node);

void        rb_node_view_scroll_to_node     (RBNodeView *view,
					     RBNode *node);

RBNode 	   *rb_node_view_get_node           (RBNodeView *view,
				             RBNode *start,
				             RBDirection direction);

void	    rb_node_view_enable_drag_source (RBNodeView *view,
					     const GtkTargetEntry *targets,
					     int n_targets);

G_END_DECLS

#endif /* __RB_NODE_VIEW_H */
