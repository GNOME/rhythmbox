/*
 *  arch-tag: Header for status display widget
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtkhbox.h>
#include <monkey-media.h>

#include "rb-shell-player.h"
#include "rb-source.h"
#include "rhythmdb.h"

#ifndef __RB_STATUSBAR_H
#define __RB_STATUSBAR_H

G_BEGIN_DECLS

#define RB_TYPE_STATUSBAR         (rb_statusbar_get_type ())
#define RB_STATUSBAR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STATUSBAR, RBStatusbar))
#define RB_STATUSBAR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STATUSBAR, RBStatusbarClass))
#define RB_IS_STATUSBAR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STATUSBAR))
#define RB_IS_STATUSBAR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STATUSBAR))
#define RB_STATUSBAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STATUSBAR, RBStatusbarClass))

typedef struct RBStatusbarPrivate RBStatusbarPrivate;

typedef struct
{
	GtkHBox parent;

	RBStatusbarPrivate *priv;
} RBStatusbar;

typedef struct
{
	GtkHBoxClass parent_class;
} RBStatusbarClass;

GType			rb_statusbar_get_type	(void);

RBStatusbar *		rb_statusbar_new	(RhythmDB *db,
						 BonoboUIComponent *component,
						 RBShellPlayer *player);

void			rb_statusbar_set_source	(RBStatusbar *bar,
						 RBSource *player);

void			rb_statusbar_set_progress(RBStatusbar *bar, double progress);
void			rb_statusbar_sync_state (RBStatusbar *statusbar);

G_END_DECLS

#endif /* __RB_STATUSBAR_H */
