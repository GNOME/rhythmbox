/*
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <gtk/gtk.h>
#include "rb-status-icon-plugin.h"
#include "eggtrayicon.h"
#include "rb-shell.h"

#if defined(HAVE_NOTIFY)
#include <libnotify/notify.h>
#endif

#ifndef __RB_TRAY_ICON_H
#define __RB_TRAY_ICON_H

G_BEGIN_DECLS

#define RB_TYPE_TRAY_ICON         (rb_tray_icon_get_type ())
#define RB_TRAY_ICON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TRAY_ICON, RBTrayIcon))
#define RB_TRAY_ICON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TRAY_ICON, RBTrayIconClass))
#define RB_IS_TRAY_ICON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TRAY_ICON))
#define RB_IS_TRAY_ICON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TRAY_ICON))
#define RB_TRAY_ICON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TRAY_ICON, RBTrayIconClass))

typedef struct _RBTrayIcon RBTrayIcon;
typedef struct _RBTrayIconClass RBTrayIconClass;

typedef struct _RBTrayIconPrivate RBTrayIconPrivate;

struct _RBTrayIcon
{
	EggTrayIcon parent;

	RBTrayIconPrivate *priv;
};

struct _RBTrayIconClass
{
	EggTrayIconClass parent_class;
};

GType			rb_tray_icon_get_type	(void);

RBTrayIcon *		rb_tray_icon_new	(RBStatusIconPlugin *plugin, RBShell *shell);

void                    rb_tray_icon_get_geom   (RBTrayIcon *icon, int *x, int *y, int *width, int *height);

void 			rb_tray_icon_trigger_tooltip_query (RBTrayIcon *icon);

gboolean		rb_tray_icon_is_embedded (RBTrayIcon *icon);

void			rb_tray_icon_menu_popup (RBTrayIcon *icon, GtkWidget *popup, gint button);

#if defined(HAVE_NOTIFY)
void			rb_tray_icon_attach_notification (RBTrayIcon *icon, NotifyNotification *notification);
#endif

void			rb_tray_icon_set_visible (RBTrayIcon *icon, gboolean visible);

G_END_DECLS

#endif /* __RB_TRAY_ICON_H */
