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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gtk/gtkactiongroup.h>
#include <gtk/gtkuimanager.h>
#include "eggtrayicon.h"
#include "rhythmdb.h"
#include "rb-shell.h"

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

RBTrayIcon *		rb_tray_icon_new	(GtkUIManager *mgr, RBShell *shell);

void                    rb_tray_icon_get_geom   (RBTrayIcon *icon, int *x, int *y, int *width, int *height);

void                    rb_tray_icon_notify     (RBTrayIcon *icon,
						 guint timeout,
						 const char *primary_markup,
						 GtkWidget *msgicon,
						 const char *secondary_markup,
						 gboolean requested);

void                    rb_tray_icon_cancel_notify (RBTrayIcon *icon);

void rb_tray_icon_set_tooltip_primary_markup (RBTrayIcon *icon, const char *primary_markup);
void rb_tray_icon_set_tooltip_icon (RBTrayIcon *icon, GtkWidget *msgicon);
void rb_tray_icon_set_tooltip_secondary_markup (RBTrayIcon *icon, const char *secondary_markup);

G_END_DECLS

#endif /* __RB_TRAY_ICON_H */
