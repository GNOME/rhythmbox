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

#ifndef __RB_SIDEBAR_BUTTON_H
#define __RB_SIDEBAR_BUTTON_H

#include <gtk/gtkselection.h>
#include <gtk/gtkradiobutton.h>

G_BEGIN_DECLS

#define RB_TYPE_SIDEBAR_BUTTON         (rb_sidebar_button_get_type ())
#define RB_SIDEBAR_BUTTON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SIDEBAR_BUTTON, RBSidebarButton))
#define RB_SIDEBAR_BUTTON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SIDEBAR_BUTTON, RBSidebarButtonClass))
#define RB_IS_SIDEBAR_BUTTON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SIDEBAR_BUTTON))
#define RB_IS_SIDEBAR_BUTTON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SIDEBAR_BUTTON))
#define RB_SIDEBAR_BUTTON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SIDEBAR_BUTTON, RBSidebarButtonClass))

typedef struct RBSidebarButtonPrivate RBSidebarButtonPrivate;

typedef struct
{
	GtkRadioButton parent;

	char *unique_id;

	GtkWidget *label;
	GtkWidget *image;

	RBSidebarButtonPrivate *priv;
} RBSidebarButton;

typedef struct
{
	GtkRadioButtonClass parent;

	void (*edited)  (RBSidebarButton *button);
	void (*deleted) (RBSidebarButton *button);
} RBSidebarButtonClass;

GType            rb_sidebar_button_get_type        (void);

RBSidebarButton *rb_sidebar_button_new             (const char *unique_id,
					            const char *button_name);

void             rb_sidebar_button_add_dnd_targets (RBSidebarButton *button,
						    const GtkTargetEntry *targets,
						    int n_targets);

void             rb_sidebar_button_set             (RBSidebarButton *button,
					            const char *stock_id,
					            const char *text,
					            gboolean is_static);

void             rb_sidebar_button_get             (RBSidebarButton *button,
				                    char **stock_id,
					            char **text,
					            gboolean *is_static);

void             rb_sidebar_button_rename          (RBSidebarButton *button);

G_END_DECLS

#endif /* __RB_SIDEBAR_BUTTON_H */
