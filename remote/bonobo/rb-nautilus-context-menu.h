/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * arch-tag: Nautilus context menu main definition
 * Copyright (C) 2002 James Willcox
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
 * Author:  James Willcox  <jwillcox@gnome.org>
 */

#ifndef RB_NAUTILUS_CONTEXT_MENU_H
#define RB_NAUTILUS_CONTEXT_MENU_H

#include <bonobo/bonobo-object.h>

#define TYPE_RB_NAUTILUS_CONTEXT_MENU	     (rb_nautilus_context_menu_get_type ())
#define RB_NAUTILUS_CONTEXT_MENU(obj)	     (GTK_CHECK_CAST ((obj), TYPE_RB_NAUTILUS_CONTEXT_MENU, RbNautilusContextMenu))
#define RB_NAUTILUS_CONTEXT_MENU_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), TYPE_RB_NAUTILUS_CONTEXT_MENU, RbNautilusContextMenuClass))
#define IS_RB_NAUTILUS_CONTEXT_MENU(obj)	     (GTK_CHECK_TYPE ((obj), TYPE_RB_NAUTILUS_CONTEXT_MENU))
#define IS_RB_NAUTILUS_CONTEXT_MENU_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), TYPE_XMMS_COMPONENT))

typedef struct {
	BonoboObject parent;
} RbNautilusContextMenu;

typedef struct {
	BonoboObjectClass parent;

	POA_Bonobo_Listener__epv epv;
} RbNautilusContextMenuClass;

GType rb_nautilus_context_menu_get_type (void);

#endif /* RB_NAUTILUS_CONTEXT_MENU_H */
