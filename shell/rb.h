/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_H
#define __RB_H

#include "Rhythmbox.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-object.h>

G_BEGIN_DECLS

#define RB_OAFIID "OAFIID:GNOME_Rhythmbox"

#define RB_TYPE               (rb_get_type ())
#define RB(o)                 (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE, RB))
#define RB_CLASS(k)           (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE, RBClass))
#define RB_IS(o)              (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE))
#define RB_IS_CLASS(k)        (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE))
#define RB_GET_CLASS(o)       (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE, RBClass))

typedef struct RBPrivate RBPrivate;

typedef struct
{
        BonoboObject parent;

	RBPrivate *priv;
} RB;

typedef struct
{
        BonoboObjectClass parent_class;

        POA_GNOME_Rhythmbox__epv epv;
} RBClass;

#include "rb-commander.h"
#include "rb-player.h"
#include "rb-library.h"

GType        rb_get_type      (void);

RB          *rb_new           (void);

void         rb_construct     (RB *rb);

void         rb_set_title     (RB *rb,
		               const char *title);

RBPlayer    *rb_get_player    (RB *rb);

RBCommander *rb_get_commander (RB *rb);

RBLibrary   *rb_get_library   (RB *rb);

G_END_DECLS

#endif /* __RB_H */
