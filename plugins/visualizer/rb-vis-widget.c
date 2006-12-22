/*
 * rb-vis-widget.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-vis-widget.h"
#include "rb-debug.h"

enum
{
	PROP_0,
	PROP_WINDOW_XID,
	PROP_WIDTH,
	PROP_HEIGHT
};


G_DEFINE_TYPE(RBVisWidget, rb_vis_widget, GTK_TYPE_WIDGET)

static void
rb_vis_widget_init (RBVisWidget *rbvw)
{
	GTK_WIDGET_SET_FLAGS (GTK_WIDGET (rbvw), GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS (GTK_WIDGET (rbvw), GTK_DOUBLE_BUFFERED);
}

static void
rb_vis_widget_realize (GtkWidget *widget)
{
	GdkWindowAttr attributes;
	gint attributes_mask;

	rb_debug ("realizing container window");

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = gtk_widget_get_events (widget);
	attributes.event_mask |= GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK;
	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
					 &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, widget);
	gdk_window_show (widget->window);

	widget->style = gtk_style_attach (widget->style, widget->window);
	gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);

	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
}

static void
rb_vis_widget_size_allocate (GtkWidget *widget,
			     GtkAllocation *allocation)
{
	RBVisWidget *rbvw = RB_VIS_WIDGET (widget);
	widget->allocation = *allocation;

	if (!GTK_WIDGET_REALIZED (widget))
		return;

	rb_debug ("handling size allocate event");
	gdk_window_move_resize (widget->window,
				allocation->x, allocation->y,
				allocation->width, allocation->height);

	if (rbvw->width != allocation->width) {
		rbvw->width = allocation->width;
		g_object_notify (G_OBJECT (rbvw), "width");
	}
	if (rbvw->height != allocation->height) {
		rbvw->height = allocation->height;
		g_object_notify (G_OBJECT (rbvw), "height");
	}
}

static gboolean
rb_vis_widget_expose_event (GtkWidget *widget,
			    GdkEventExpose *event)
{
	RBVisWidget *rbvw = RB_VIS_WIDGET (widget);

	if (rbvw->window_xid != GDK_WINDOW_XWINDOW (widget->window)) {
		rbvw->window_xid = GDK_WINDOW_XWINDOW (widget->window);

		gdk_display_sync (gdk_drawable_get_display (GDK_DRAWABLE (widget->window)));

		rb_debug ("got new window ID %lu", rbvw->window_xid);
		g_object_notify (G_OBJECT (rbvw), "window-xid");
	}

	return TRUE;
}

static void
rb_vis_widget_hide (GtkWidget *widget)
{
	if (widget->window != NULL) {
		rb_debug ("hiding output window");
		gdk_window_hide (widget->window);
	}

	GTK_WIDGET_CLASS (rb_vis_widget_parent_class)->hide (widget);
}

static void
rb_vis_widget_show (GtkWidget *widget)
{
	if (widget->window != NULL) {
		rb_debug ("showing output window");
		gdk_window_show (widget->window);
	} else {
		rb_debug ("got show event before realized..");
	}

	GTK_WIDGET_CLASS (rb_vis_widget_parent_class)->show (widget);
}

void
rb_vis_widget_resize (RBVisWidget *rbvw, int width, int height)
{
	rbvw->width = width;
	rbvw->height = height;

	if (GTK_WIDGET_REALIZED (rbvw))
		gtk_widget_set_size_request (GTK_WIDGET (rbvw), width, height);
}

static void
rb_vis_widget_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBVisWidget *rbvw = RB_VIS_WIDGET (object);
	switch (prop_id) {
	case PROP_WIDTH:
		rbvw->width = g_value_get_uint (value);
		break;
	case PROP_HEIGHT:
		rbvw->height = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_vis_widget_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBVisWidget *rbvw = RB_VIS_WIDGET (object);
	switch (prop_id) {
	case PROP_WIDTH:
		g_value_set_uint (value, rbvw->width);
		break;
	case PROP_HEIGHT:
		g_value_set_uint (value, rbvw->height);
		break;
	case PROP_WINDOW_XID:
		g_value_set_ulong (value, rbvw->window_xid);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_vis_widget_dispose (GObject *object)
{
	RBVisWidget *rbvw;

	rbvw = RB_VIS_WIDGET (object);
	rb_debug ("vis widget %p disposed", rbvw);

	G_OBJECT_CLASS (rb_vis_widget_parent_class)->dispose (object);
}

static void
rb_vis_widget_class_init (RBVisWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->size_allocate = rb_vis_widget_size_allocate;
	widget_class->realize = rb_vis_widget_realize;
	widget_class->expose_event = rb_vis_widget_expose_event;
	widget_class->hide = rb_vis_widget_hide;
	widget_class->show = rb_vis_widget_show;

	object_class->dispose = rb_vis_widget_dispose;
	object_class->set_property = rb_vis_widget_set_property;
	object_class->get_property = rb_vis_widget_get_property;

	g_object_class_install_property (object_class,
					 PROP_WINDOW_XID,
					 g_param_spec_ulong ("window-xid",
						 	     "window XID",
							     "XID for the video window",
							     0, G_MAXULONG, 0,
							     G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_WIDTH,
					 g_param_spec_uint ("width",
						 	    "width",
							    "width of the video window",
							    0, G_MAXUINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_HEIGHT,
					 g_param_spec_uint ("height",
						 	    "height",
							    "height of the video window",
							    0, G_MAXUINT, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

