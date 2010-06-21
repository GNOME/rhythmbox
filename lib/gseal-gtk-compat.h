/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2009 Thomas H.P. Andersen <phomes@gmail.com>,
 *              2009 Javier Jardón <jjardon@gnome.org>,
 *              2010 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#if !GTK_CHECK_VERSION (2, 21, 1)
#define gdk_drag_context_list_targets(context)			context->targets
#define gdk_drag_context_get_actions(context)			context->actions
#define gdk_drag_context_get_suggested_action(context)		context->suggested_action
#define gdk_drag_context_get_selected_action(context)		context->action
#endif

#if !GTK_CHECK_VERSION (2, 19, 5)
#define gtk_widget_get_realized(widget)				GTK_WIDGET_REALIZED(widget)
#define gtk_widget_set_realized(widget, TRUE)			GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED)
#endif

G_END_DECLS

#endif /* GSEAL_GTK_COMPAT_H */
