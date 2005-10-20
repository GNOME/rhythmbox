/*
 *  arch-tag: Header for widget to display Podcast Feed 
 *
 *  Copyright (C) 2005 Renato Filho <renato.filho@indt.org.br>
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

#ifndef __RB_SIMPLE_VIEW_H
#define __RB_SIMPLE_VIEW_H

#include <gtk/gtkdnd.h>

#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-property-view.h"

G_BEGIN_DECLS

#define RB_TYPE_SIMPLE_VIEW         (rb_simple_view_get_type ())
#define RB_SIMPLE_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SIMPLE_VIEW, RBSimpleView))
#define RB_SIMPLE_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SIMPLE_VIEW, RBSimpleViewClass))
#define RB_IS_SIMPLE_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SIMPLE_VIEW))
#define RB_IS_SIMPLE_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SIMPLE_VIEW))
#define RB_SIMPLE_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SIMPLE_VIEW, RBSimpleViewClass))

typedef struct RBSimpleViewPrivate RBSimpleViewPrivate;

typedef struct
{
	RBPropertyView parent;

	RBSimpleViewPrivate *priv;
} RBSimpleView;

typedef struct
{
	RBPropertyViewClass parent;

	void 	(*show_popup)             		(RBSimpleView *view);
} RBSimpleViewClass;

GType		rb_simple_view_get_type			(void);

RBSimpleView *	rb_simple_view_new			(RhythmDB *db,
	       						 guint propid,
							 const char *title);

void		rb_simple_view_append_column_custom 	(RBSimpleView *view,
				    			 GtkTreeViewColumn *column,
				    			 const char *title, 
				    			 gpointer user_data);

void 		rb_simple_view_enable_drag_source 	(RBSimpleView *view,
				   			 const GtkTargetEntry *targets,
				   			 int n_targets);
G_END_DECLS

#endif /* __RB_SIMPLE_VIEW_H */
