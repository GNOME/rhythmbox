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

#include <gtk/gtkhbox.h>

#include "rb-view-status.h"

#ifndef __RB_SHELL_STATUS_H
#define __RB_SHELL_STATUS_H

G_BEGIN_DECLS

#define RB_TYPE_SHELL_STATUS         (rb_shell_status_get_type ())
#define RB_SHELL_STATUS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL_STATUS, RBShellStatus))
#define RB_SHELL_STATUS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL_STATUS, RBShellStatusClass))
#define RB_IS_SHELL_STATUS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL_STATUS))
#define RB_IS_SHELL_STATUS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL_STATUS))
#define RB_SHELL_STATUS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL_STATUS, RBShellStatusClass))

typedef struct RBShellStatusPrivate RBShellStatusPrivate;

typedef struct
{
	GtkHBox parent;

	RBShellStatusPrivate *priv;
} RBShellStatus;

typedef struct
{
	GtkHBoxClass parent_class;
} RBShellStatusClass;

GType          rb_shell_status_get_type   (void);

RBShellStatus *rb_shell_status_new        (void);

void           rb_shell_status_set_status (RBShellStatus *shell_status,
					   RBViewStatus *status);

RBViewStatus  *rb_shell_status_get_status (RBShellStatus *shell_status);

G_END_DECLS

#endif /* __RB_SHELL_STATUS_H */
