/*
 *  arch-tag: Header for Rhythmbox tray icon object
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

#include "eggtrayicon.h"
#include <bonobo/bonobo-ui-component.h>

#include "rhythmdb.h"

#ifndef __RB_TRAY_ICON_H
#define __RB_TRAY_ICON_H

G_BEGIN_DECLS

#define RB_TYPE_TRAY_ICON         (rb_tray_icon_get_type ())
#define RB_TRAY_ICON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TRAY_ICON, RBTrayIcon))
#define RB_TRAY_ICON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TRAY_ICON, RBTrayIconClass))
#define RB_IS_TRAY_ICON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TRAY_ICON))
#define RB_IS_TRAY_ICON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TRAY_ICON))
#define RB_TRAY_ICON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TRAY_ICON, RBTrayIconClass))

typedef struct RBTrayIconPrivate RBTrayIconPrivate;

typedef struct
{
	EggTrayIcon parent;

	RBTrayIconPrivate *priv;
} RBTrayIcon;

typedef struct
{
	EggTrayIconClass parent_class;
} RBTrayIconClass;

GType			rb_tray_icon_get_type	(void);

RBTrayIcon *		rb_tray_icon_new	(BonoboUIContainer *container,
						 BonoboUIComponent *component,
						 RhythmDB *db,
						 GtkWindow *window);

void			rb_tray_icon_set_tooltip(RBTrayIcon *icon, const char *tooltip);

G_END_DECLS

#endif /* __RB_TRAY_ICON_H */
