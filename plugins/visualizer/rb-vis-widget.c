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
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
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

#include "gseal-gtk-compat.h"

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
	gtk_widget_set_can_focus (GTK_WIDGET (rbvw), TRUE);
	gtk_widget_set_double_buffered (GTK_WIDGET (rbvw), FALSE);
}

static void
rb_vis_widget_realize (GtkWidget *widget)
{
	GtkAllocation  allocation;
	GtkStyle      *style;
	GdkWindowAttr  attributes;
	GdkWindow     *window;
	gint           attributes_mask;

	rb_debug ("realizing container window");
	gtk_widget_get_allocation (widget, &allocation);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = gtk_widget_get_events (widget);
	attributes.event_mask |= GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK;
	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	window = gdk_window_new (gtk_widget_get_parent_window (widget),
				 &attributes, attributes_mask);
	gtk_widget_set_window (widget, window);
	gdk_window_set_user_data (window, widget);
	gdk_window_show (window);

	style = gtk_style_attach (gtk_widget_get_style (widget), window);
	gtk_widget_set_style (widget, style);
	gtk_style_set_background (style, window, GTK_STATE_NORMAL);

	gtk_widget_set_realized (widget, TRUE);
}

static void
rb_vis_widget_size_allocate (GtkWidget *widget,
			     GtkAllocation *allocation)
{
	RBVisWidget *rbvw = RB_VIS_WIDGET (widget);
	gtk_widget_set_allocation (widget, allocation);

	if (!gtk_widget_get_realized (widget))
		return;

	rb_debug ("handling size allocate event ([%d,%d] - [%d,%d])",
		  allocation->x, allocation->y,
		  allocation->width, allocation->height);
	gdk_window_move_resize (gtk_widget_get_window (widget),
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
	GdkWindow   *window;
	RBVisWidget *rbvw = RB_VIS_WIDGET (widget);

	window = gtk_widget_get_window (widget);

	if (rbvw->window_xid != GDK_WINDOW_XWINDOW (window)) {
		rbvw->window_xid = GDK_WINDOW_XWINDOW (window);

		gdk_display_sync (gdk_drawable_get_display (GDK_DRAWABLE (window)));

		rb_debug ("got new window ID %lu", rbvw->window_xid);
		g_object_notify (G_OBJECT (rbvw), "window-xid");
	}

	return TRUE;
}

static void
rb_vis_widget_hide (GtkWidget *widget)
{
	GdkWindow *window;

	window = gtk_widget_get_window (widget);

	if (window != NULL) {
		rb_debug ("hiding output window");
		gdk_window_hide (window);
	}

	GTK_WIDGET_CLASS (rb_vis_widget_parent_class)->hide (widget);
}

static void
rb_vis_widget_show (GtkWidget *widget)
{
	GdkWindow *window;

	window = gtk_widget_get_window (widget);

	if (window != NULL) {
		rb_debug ("showing output window");
		gdk_window_show (window);
	} else {
		rb_debug ("got show event before realized..");
	}

	GTK_WIDGET_CLASS (rb_vis_widget_parent_class)->show (widget);
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

