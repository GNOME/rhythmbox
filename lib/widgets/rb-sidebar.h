/*
 * Copyright (C) 2002 CodeFactory AB
 * Copyright (C) 2002 Richard Hult <rhult@codefactory.se>
 * Copyright (C) 2002 Mikael Hallendal <micke@codefactory.se>
 * Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 * $Id$
 */

#ifndef __RB_SIDEBAR_H
#define __RB_SIDEBAR_H

#include <gtk/gtkframe.h>

#include "rb-sidebar-button.h"

G_BEGIN_DECLS

#define RB_TYPE_SIDEBAR		   (rb_sidebar_get_type ())
#define RB_SIDEBAR(obj)		   (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_SIDEBAR, RBSidebar))
#define RB_SIDEBAR_CLASS(klass)	   (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_SIDEBAR, RBSidebarClass))
#define RB_IS_SIDEBAR(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_SIDEBAR))
#define RB_IS_SIDEBAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_SIDEBAR))
#define RB_SIDEBAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_SIDEBAR, RBSidebarClass))

typedef struct _RBSidebar           RBSidebar;
typedef struct _RBSidebarClass      RBSidebarClass;
typedef struct _RBSidebarPriv       RBSidebarPriv;

struct _RBSidebar
{
	GtkFrame parent;

	RBSidebarPriv *priv;
};

struct _RBSidebarClass
{
	GtkFrameClass parent_class;
};

typedef enum
{
	RB_SIDEBAR_DND_TYPE_NEW_BUTTON,
	RB_SIDEBAR_DND_TYPE_BUTTON
} RBSidebarDNDType;

GType      rb_sidebar_get_type        (void);

GtkWidget *rb_sidebar_new             (void);

void       rb_sidebar_append          (RBSidebar *sidebar,
				       RBSidebarButton *button);

void       rb_sidebar_remove          (RBSidebar *sidebar,
				       RBSidebarButton *button);

void       rb_sidebar_save_layout     (RBSidebar *sidebar,
				       const char *filename);

void       rb_sidebar_load_layout     (RBSidebar *sidebar,
				       const char *filename);

G_END_DECLS

#endif /* __RB_SIDEBAR_H */
