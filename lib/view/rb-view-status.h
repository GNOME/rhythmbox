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

#ifndef __RB_VIEW_STATUS_H
#define __RB_VIEW_STATUS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_VIEW_STATUS         (rb_view_status_get_type ())
#define RB_VIEW_STATUS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VIEW_STATUS, RBViewStatus))
#define RB_IS_VIEW_STATUS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VIEW_STATUS))
#define RB_VIEW_STATUS_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_VIEW_STATUS, RBViewStatusIface))

typedef struct RBViewStatus RBViewStatus; /* Dummy typedef */

typedef struct
{
	GTypeInterface g_iface;
	
	/* signals */
	void (*status_changed) (RBViewStatus *status);

	/* methods */
	const char *(*impl_get) (RBViewStatus *status);
} RBViewStatusIface;

GType       rb_view_status_get_type       (void);

const char *rb_view_status_get            (RBViewStatus *status);

void        rb_view_status_notify_changed (RBViewStatus *status);

G_END_DECLS

#endif /* __RB_VIEW_STATUS_H */
