/*
 * @file libsexy/sexy-tooltip.c A flexible tooltip widget.
 *
 * @Copyright (C) 2006 Christian Hammond.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libsexy/sexy-tooltip.h>

struct _SexyTooltipPriv
{
};

static void sexy_tooltip_class_init(SexyTooltipClass *klass);
static void sexy_tooltip_init(SexyTooltip *tooltip);
static void sexy_tooltip_finalize(GObject *obj);
static gboolean sexy_tooltip_button_press_event(GtkWidget *widget, GdkEventButton *event);
static gboolean sexy_tooltip_expose_event(GtkWidget *widget, GdkEventExpose *event);

static GtkWindowClass *parent_class;

G_DEFINE_TYPE(SexyTooltip, sexy_tooltip, GTK_TYPE_WINDOW);

static void
sexy_tooltip_class_init(SexyTooltipClass *klass)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent(klass);

	gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = sexy_tooltip_finalize;

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->button_press_event = sexy_tooltip_button_press_event;
	widget_class->expose_event = sexy_tooltip_expose_event;
}

static void
sexy_tooltip_init(SexyTooltip *tooltip)
{
	GtkWindow *window;
	tooltip->priv = g_new0(SexyTooltipPriv, 1);

	window = GTK_WINDOW(tooltip);

	gtk_widget_set_app_paintable(GTK_WIDGET(tooltip), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(tooltip), FALSE);
	gtk_widget_set_name(GTK_WIDGET(tooltip), "gtk-tooltips");
	gtk_container_set_border_width(GTK_CONTAINER(tooltip), 4);

	gtk_widget_add_events(GTK_WIDGET(tooltip), GDK_BUTTON_PRESS_MASK);
}

static void
sexy_tooltip_finalize(GObject *obj)
{
	SexyTooltip *tooltip;

	g_return_if_fail(obj != NULL);
	g_return_if_fail(SEXY_IS_TOOLTIP(obj));

	tooltip = SEXY_TOOLTIP(obj);
	g_free(tooltip->priv);

	if (G_OBJECT_CLASS(parent_class)->finalize)
		G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static gboolean
sexy_tooltip_button_press_event(GtkWidget *widget, GdkEventButton *event)
{
	if (GTK_WIDGET_CLASS(parent_class)->button_press_event)
		GTK_WIDGET_CLASS(parent_class)->button_press_event(widget, event);
	gtk_widget_destroy(widget);
	return TRUE;
}

static gboolean
sexy_tooltip_expose_event(GtkWidget *widget, GdkEventExpose *event)
{
	GtkRequisition req;

	gtk_widget_size_request(widget, &req);
	gtk_widget_ensure_style(widget);
	gtk_paint_flat_box(widget->style, widget->window,
	                   GTK_STATE_NORMAL, GTK_SHADOW_OUT,
	                   NULL, widget, "tooltip",
	                   0, 0, req.width, req.height);

	return GTK_WIDGET_CLASS(parent_class)->expose_event(widget, event);
}

/**
 * sexy_tooltip_new
 *
 * Creates a new SexyTooltip widget
 *
 * Returns: a new #SexyTooltip
 */
GtkWidget *
sexy_tooltip_new(void)
{
	GtkWindow *window = g_object_new(SEXY_TYPE_TOOLTIP, NULL);
	window->type = GTK_WINDOW_POPUP;
	return GTK_WIDGET(window);
}

/**
 * sexy_tooltip_new_with_label:
 * @text: The text to show in the tooltip.
 *
 * Creates a new SexyTooltip widget with text content
 *
 * Returns: a new #SexyTooltip
 */
GtkWidget *
sexy_tooltip_new_with_label(const gchar *text)
{
	GtkWindow *window;
	GtkWidget *label;
	window = g_object_new(SEXY_TYPE_TOOLTIP, NULL);
	window->type = GTK_WINDOW_POPUP;

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), text);
	gtk_widget_show(label);
	gtk_container_add(GTK_CONTAINER(window), label);

	return GTK_WIDGET(window);
}

/**
 * sexy_tooltip_position_to_widget:
 * @tooltip: A #SexyTooltip.
 * @widget: The widget to position to.
 *
 * Helper function to position the tooltip window relative to an on-screen
 * widget.  This uses a simplified version of the positioning function used
 * by GtkTooltips.
 */
void
sexy_tooltip_position_to_widget(SexyTooltip *tooltip, GtkWidget *widget)
{
	GdkScreen *screen;
	gint x, y;
	GdkRectangle rect;

	screen = gtk_widget_get_screen(widget);
	gdk_window_get_root_origin(widget->window, &x, &y);

	rect.x = widget->allocation.x + x;
	rect.y = widget->allocation.y + y;
	rect.width  = widget->allocation.width;
	rect.height = widget->allocation.height;

	sexy_tooltip_position_to_rect(tooltip, &rect, screen);
}

/**
 * sexy_tooltip_position_to_rect:
 * @tooltip: A #SexyTooltip
 * @rect: A rectangle to position the tooltip near.
 * @screen: The screen to position the tooltip on.
 *
 * Helper function to position the tooltip window relative to an arbitrary
 * rectangle on a given screen.  This uses a simplified version of the
 * positioning function used by GtkTooltips.
 */
void
sexy_tooltip_position_to_rect(SexyTooltip *tooltip, GdkRectangle *rect, GdkScreen *screen)
{
	/* This uses the simplified tooltip placement algorithm in VMware's
	 * libview.  The comments here are also thanks to them (plangdale)
	 */
	GtkRequisition requisition;
	GdkRectangle monitor;
	int monitor_num;
	int x, y, w, h;

	gtk_widget_size_request(GTK_WIDGET(tooltip), &requisition);
	w = requisition.width;
	h = requisition.height;

	x = rect->x;
	y = rect->y;

	/* shift X to the center of the rect */
	x += rect->width / 2;

	/*
	 * x is shifted to the left by the tooltip's centre point + 4,
	 * so the tooltip is now 4 pixels to the right of where it would be
	 * if the widget centre-line was aligned with the tooltip's centre-line.
	 */
	x -= (w / 2 + 4);

	/*
	 * Now, the ideal x co-ordinate has been established, but we must
	 * verify if it is acceptable given screen constraints.
	 */
	monitor_num = gdk_screen_get_monitor_at_point (screen, x, y);
	gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

	/*
	 * If the right edge of the tooltip is off the right edge of
	 * the screen, move x to the left as much as is needed to
	 * get the tooltip in the screen.
	 *   or
	 * If the left edge of the tooltip is off the left edge of
	 * the screen, move x to the right as much as is needed to
	 * get the tooltip on the screen.
	 */
	if ((x + w) > monitor.x + monitor.width)
		x -= (x + w) - (monitor.x + monitor.width);
	else if (x < monitor.x)
		x = monitor.x;

	/*
	 * If the position of the bottom of the tooltip, if placed in
	 * the ideal location, would be off the bottom of the screen,
	 * then flip the tooltip to be 4 pixels above the widget.
	 *   or
	 * Put it in the ideal location, 4 pixels below the widget.
	 */
	if ((y + h + rect->height + 4) > monitor.y + monitor.height) {
		y = y - h - 4;
	} else {
		y = y + rect->height + 4;
	}

	gtk_window_move(GTK_WINDOW(tooltip), x, y);
}
