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

#include "rb.h"

#ifndef __RB_COMMANDER_H
#define __RB_COMMANDER_H

G_BEGIN_DECLS

#define RB_TYPE_COMMANDER         (rb_commander_get_type ())
#define RB_COMMANDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_COMMANDER, RBCommander))
#define RB_COMMANDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_COMMANDER, RBCommanderClass))
#define RB_IS_COMMANDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_COMMANDER))
#define RB_IS_COMMANDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_COMMANDER))
#define RB_COMMANDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_COMMANDER, RBCommanderClass))

typedef struct RBCommanderPrivate RBCommanderPrivate;

typedef struct
{
	GObject parent;

	RBCommanderPrivate *priv;
} RBCommander;

typedef struct
{
	GObjectClass parent_class;
} RBCommanderClass;

GType        rb_commander_get_type (void);

RBCommander *rb_commander_new      (RB *rb);

G_END_DECLS

#endif /* __RB_COMMANDER_H */
