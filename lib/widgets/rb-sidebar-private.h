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

#ifndef __RB_SIDEBAR_PRIVATE_H
#define __RB_SIDEBAR_PRIVATE_H

#include "rb-sidebar.h"

G_BEGIN_DECLS

typedef enum
{
	RB_SIDEBAR_DND_POSITION_TOP,
	RB_SIDEBAR_DND_POSITION_MID,
	RB_SIDEBAR_DND_POSITION_BOTTOM
} RBSidebarDNDPosition;

void                 rb_sidebar_show_dnd_hint  (RBSidebar *sidebar,
			                        GtkWidget *button,
			                        RBSidebarDNDPosition pos);
void                 rb_sidebar_hide_dnd_hint  (RBSidebar *sidebar);

void                 rb_sidebar_button_dropped (RBSidebar *sidebar,
				                RBSidebarButton *over,
				                RBSidebarButton *button,
				                RBSidebarDNDPosition pos);

RBSidebarDNDPosition rb_sidebar_get_button_pos (GtkWidget *widget,
						int y);

RBSidebarButton     *rb_sidebar_button_from_id (RBSidebar *sidebar,
					        const char *unique_id);

G_END_DECLS

#endif /* __RB_SIDEBAR_PRIVATE_H */
