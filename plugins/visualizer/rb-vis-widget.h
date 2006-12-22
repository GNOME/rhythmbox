/*
 * rb-vis-widget.h
 *
 * Copyright (C) 2006  Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifndef __RB_VIS_WIDGET_H__
#define __RB_VIS_WIDGET_H__

G_BEGIN_DECLS

#define RB_TYPE_VIS_WIDGET		(rb_vis_widget_get_type ())
#define RB_VIS_WIDGET(obj)		(GTK_CHECK_CAST ((obj), RB_TYPE_VIS_WIDGET, RBVisWidget))
#define RB_VIS_WIDGET_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), RB_TYPE_VIS_WIDGET, RBVisWidgetClass))
#define RB_IS_VIS_WIDGET(obj)		(GTK_CHECK_TYPE ((obj), RB_TYPE_VIS_WIDGET))
#define RB_IS_VIS_WIDGET_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), RB_TYPE_VIS_WIDGET))
#define RB_VIS_WIDGET_GET_CLASS(obj)	(GTK_CHECK_GET_CLASS ((obj), RB_TYPE_VIS_WIDGET, RBVisWidgetClass))

typedef struct _RBVisWidget
{
	GtkWidget parent;
	guint width;
	guint height;
	unsigned long window_xid;
} RBVisWidget;

typedef struct _RBVisWidgetClass
{
	GtkWidgetClass parent_class;
} RBVisWidgetClass;

GType	rb_vis_widget_get_type (void);
void	rb_vis_widget_resize (RBVisWidget *rbvw, int width, int height);

G_END_DECLS

#endif /* __RB_VIS_WIDGET_H__ */

