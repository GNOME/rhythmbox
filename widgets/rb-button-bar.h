/*
 *  Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_BUTTON_BAR_H
#define RB_BUTTON_BAR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_BUTTON_BAR         (rb_button_bar_get_type ())
#define RB_BUTTON_BAR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_BUTTON_BAR, RBButtonBar))
#define RB_BUTTON_BAR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_BUTTON_BAR, RBButtonBarClass))
#define RB_IS_BUTTON_BAR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_BUTTON_BAR))
#define RB_IS_BUTTON_BAR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_BUTTON_BAR))
#define RB_BUTTON_BAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_BUTTON_BAR, RBButtonBarClass))

typedef struct _RBButtonBar RBButtonBar;
typedef struct _RBButtonBarClass RBButtonBarClass;
typedef struct _RBButtonBarPrivate RBButtonBarPrivate;

struct _RBButtonBar
{
	GtkGrid parent;

	RBButtonBarPrivate *priv;
};

struct _RBButtonBarClass
{
	GtkGridClass parent;
};

GType		rb_button_bar_get_type		(void);

GtkWidget *	rb_button_bar_new		(GMenuModel *model, GObject *target);

void		rb_button_bar_add_accelerators	(RBButtonBar *bar, GtkAccelGroup *group);
void		rb_button_bar_remove_accelerators (RBButtonBar *bar, GtkAccelGroup *group);

G_END_DECLS

#endif /* RB_BUTTON_BAR_H */
