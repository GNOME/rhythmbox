/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2009 Thomas H.P. Andersen <phomes@gmail.com>,
 *              2009 Javier Jardón <jjardon@gnome.org>
 *
 *  This runtime is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1, or (at your option)
 *  any later version.
 *
 *  This runtime is distributed in the hope runtime it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this runtime; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef GSEAL_GTK_COMPAT_H
#define GSEAL_GTK_COMPAT_H

G_BEGIN_DECLS


#if !GTK_CHECK_VERSION (2, 18, 0)
#define gtk_widget_has_focus(widget)                            (GTK_WIDGET_HAS_FOCUS (widget))
#define gtk_widget_is_drawable(widget)                          (GTK_WIDGET_DRAWABLE (widget))
#define gtk_widget_get_allocation(widget, alloc)                (*(alloc)=(widget)->allocation)
#define gtk_widget_get_app_paintable(widget)                    (GTK_WIDGET_APP_PAINTABLE (widget))
#define gtk_widget_get_has_window(widget)                       (!GTK_WIDGET_NO_WINDOW (widget))
#define gtk_widget_get_state(widget)                            ((widget)->state)
#define gtk_widget_get_visible(widget)                          (GTK_WIDGET_VISIBLE (widget))
#define gtk_widget_set_allocation(widget, alloc)                ((widget)->allocation=*(alloc))
#define gtk_widget_set_can_default(widget, TRUE)                GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_DEFAULT)
#define gtk_widget_set_can_focus(widget, TRUE)                  GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_FOCUS)
#define gtk_widget_set_double_buffered(widget, FALSE)           GTK_WIDGET_UNSET_FLAGS (widget, GTK_DOUBLE_BUFFERED)
#define gtk_widget_set_window(widget, _window)                  ((widget)->window=_window)

#define gtk_cell_renderer_get_padding(cell, xpad, ypad)		g_object_get (cell, "xpad", xpad, "ypad", ypad, NULL);
#define gtk_cell_renderer_get_alignment(cell, xalign, yalign)	g_object_get (cell, "xalign", xalign, "yalign", yalign, NULL);
#define gtk_cell_renderer_set_padding(cell, xpad, ypad)		g_object_set (cell, "xpad", xpad, "ypad", ypad, NULL);

#endif /* GTK+ < 2.18.0 */

G_END_DECLS

#endif /* GSEAL_GTK_COMPAT_H */
