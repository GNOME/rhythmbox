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

#ifndef __RB_VIEW_H
#define __RB_VIEW_H

#include <bonobo/bonobo-ui-component.h>
#include <gtk/gtkframe.h>

#include "rb-sidebar-button.h"

G_BEGIN_DECLS

#define RB_TYPE_VIEW         (rb_view_get_type ())
#define RB_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VIEW, RBView))
#define RB_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VIEW, RBViewClass))
#define RB_IS_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VIEW))
#define RB_IS_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VIEW))
#define RB_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VIEW, RBViewClass))

typedef struct RBViewPrivate RBViewPrivate;

typedef struct
{
	GtkFrame parent;

	RBViewPrivate *priv;
} RBView;

typedef struct
{
	GtkFrameClass parent;
} RBViewClass;

GType            rb_view_get_type           (void);

void             rb_view_merge_menus        (RBView *view);

RBSidebarButton *rb_view_get_sidebar_button (RBView *view);

G_END_DECLS

#endif /* __RB_VIEW_H */
