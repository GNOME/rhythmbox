/* EggNotificationBubble
 * Copyright (C) 2005 Colin Walters <walters@verbum.org>
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
 * Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include "eggnotificationbubble.h"

#define DEFAULT_DELAY 500           /* Default delay in ms */
#define STICKY_DELAY 0              /* Delay before popping up next bubble
                                     * if we're sticky
                                     */
#define STICKY_REVERT_DELAY 1000    /* Delay before sticky bubble revert
				     * to normal
                                     */

#define BORDER_SIZE 15

static void egg_notification_bubble_class_init        (EggNotificationBubbleClass *klass);
static void egg_notification_bubble_init              (EggNotificationBubble      *bubble);
static void egg_notification_bubble_destroy           (GtkObject        *object);
static void egg_notification_bubble_detach            (EggNotificationBubble *bubble);

static void egg_notification_bubble_event_handler     (GtkWidget   *widget,
						       GdkEvent    *event,
						       gpointer     user_data);
static gint egg_notification_bubble_paint_window      (EggNotificationBubble *bubble);
static void egg_notification_bubble_unset_bubble_window  (EggNotificationBubble *bubble);

static GtkObjectClass *parent_class;

enum
{
	NOTIFICATION_CLICKED,
	NOTIFICATION_TIMEOUT,
	LAST_SIGNAL
};

static guint egg_notification_bubble_signals[LAST_SIGNAL] = { 0 };

GType
egg_notification_bubble_get_type (void)
{
  static GType bubble_type = 0;

  if (!bubble_type)
    {
      static const GTypeInfo bubble_info =
      {
	sizeof (EggNotificationBubbleClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	(GClassInitFunc) egg_notification_bubble_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (EggNotificationBubble),
	0,		/* n_preallocs */
	(GInstanceInitFunc) egg_notification_bubble_init,
      };

      bubble_type = g_type_register_static (GTK_TYPE_OBJECT, "EggNotificationBubble",
					      &bubble_info, 0);
    }

  return bubble_type;
}

static void
egg_notification_bubble_class_init (EggNotificationBubbleClass *class)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) class;

  parent_class = g_type_class_peek_parent (class);

  object_class->destroy = egg_notification_bubble_destroy;

  egg_notification_bubble_signals[NOTIFICATION_CLICKED] =
    g_signal_new ("clicked",
		  EGG_TYPE_NOTIFICATION_BUBBLE,
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (EggNotificationBubbleClass, clicked),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
  egg_notification_bubble_signals[NOTIFICATION_TIMEOUT] =
    g_signal_new ("timeout",
		  EGG_TYPE_NOTIFICATION_BUBBLE,
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (EggNotificationBubbleClass, timeout),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
egg_notification_bubble_init (EggNotificationBubble *bubble)
{
  bubble->bubble_window = NULL;
}

static void
bubble_window_display_closed (GdkDisplay  *display,
			   gboolean     was_error,
			   EggNotificationBubble *bubble)
{
  egg_notification_bubble_unset_bubble_window (bubble);
}

static void
disconnect_bubble_window_display_closed (EggNotificationBubble *bubble)
{
  g_signal_handlers_disconnect_by_func (gtk_widget_get_display (bubble->bubble_window),
					(gpointer) bubble_window_display_closed,
					bubble);
}

static void
egg_notification_bubble_unset_bubble_window (EggNotificationBubble *bubble)
{
  if (bubble->bubble_window)
    {
      disconnect_bubble_window_display_closed (bubble);
      
      gtk_widget_destroy (bubble->bubble_window);
      bubble->bubble_window = NULL;
    }
}

static void
egg_notification_bubble_destroy (GtkObject *object)
{
  EggNotificationBubble *bubble = EGG_NOTIFICATION_BUBBLE (object);

  g_return_if_fail (bubble != NULL);

  if (bubble->timeout_id)
    {
      g_source_remove (bubble->timeout_id);
      bubble->timeout_id = 0;
    }

  egg_notification_bubble_detach (bubble);

  egg_notification_bubble_unset_bubble_window (bubble);

  GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static void
force_window (EggNotificationBubble *bubble)
{
  g_return_if_fail (EGG_IS_NOTIFICATION_BUBBLE (bubble));

  if (!bubble->bubble_window)
    {
      GtkWidget *vbox;

      bubble->bubble_window = gtk_window_new (GTK_WINDOW_POPUP);
      gtk_widget_add_events (bubble->bubble_window, GDK_BUTTON_PRESS_MASK);
      gtk_widget_set_app_paintable (bubble->bubble_window, TRUE);
      gtk_window_set_resizable (GTK_WINDOW (bubble->bubble_window), FALSE);
      gtk_widget_set_name (bubble->bubble_window, "gtk-tooltips");
      gtk_container_set_border_width (GTK_CONTAINER (bubble->bubble_window), BORDER_SIZE + 5);

      g_signal_connect_swapped (bubble->bubble_window,
				"expose_event",
				G_CALLBACK (egg_notification_bubble_paint_window), 
				bubble);

      bubble->bubble_header_label = gtk_label_new (NULL);
      bubble->bubble_body_label = gtk_label_new (NULL);
      gtk_label_set_line_wrap (GTK_LABEL (bubble->bubble_header_label), TRUE);
      gtk_label_set_line_wrap (GTK_LABEL (bubble->bubble_body_label), TRUE);
      gtk_misc_set_alignment (GTK_MISC (bubble->bubble_header_label), 0.5, 0.5);
      gtk_misc_set_alignment (GTK_MISC (bubble->bubble_body_label), 0.5, 0.5);
      gtk_widget_show (bubble->bubble_header_label);
      gtk_widget_show (bubble->bubble_body_label);

      bubble->main_hbox = gtk_hbox_new (FALSE, 10);
      gtk_container_add (GTK_CONTAINER (bubble->main_hbox), bubble->bubble_body_label);
      
      vbox = gtk_vbox_new (FALSE, 5);
      gtk_container_add (GTK_CONTAINER (vbox), bubble->bubble_header_label);
      gtk_container_add (GTK_CONTAINER (vbox), bubble->main_hbox);
      gtk_container_add (GTK_CONTAINER (bubble->bubble_window), vbox);

      g_signal_connect (bubble->bubble_window,
			"destroy",
			G_CALLBACK (gtk_widget_destroyed),
			&bubble->bubble_window);
      g_signal_connect_after (bubble->bubble_window, "event-after",
                              G_CALLBACK (egg_notification_bubble_event_handler),
			      bubble);
    }
}

void
egg_notification_bubble_attach (EggNotificationBubble *bubble,
				GtkWidget *widget)
{
  bubble->widget = widget;

  g_signal_connect_object (widget, "destroy",
			   G_CALLBACK (g_object_unref),
			   bubble, G_CONNECT_SWAPPED);
}

void
egg_notification_bubble_set (EggNotificationBubble *bubble,
			     const gchar *bubble_header_text,
			     GtkWidget   *icon,
			     const gchar *bubble_body_text)
{
  g_return_if_fail (EGG_IS_NOTIFICATION_BUBBLE (bubble));

  g_free (bubble->bubble_header_text);
  g_free (bubble->bubble_body_text);
  if (bubble->icon)
    {
      if (bubble->active)
	gtk_container_remove (GTK_CONTAINER (bubble->main_hbox), bubble->icon);
      g_object_unref (G_OBJECT (bubble->icon));
      bubble->icon = NULL;
    }
  
  bubble->bubble_header_text = g_strdup (bubble_header_text);
  bubble->bubble_body_text = g_strdup (bubble_body_text);
  if (icon)
    bubble->icon = g_object_ref (G_OBJECT (icon));
}

static gint
egg_notification_bubble_paint_window (EggNotificationBubble *bubble)
{
  GtkRequisition req;

  gtk_widget_size_request (bubble->bubble_window, &req);
  gtk_paint_flat_box (bubble->bubble_window->style, bubble->bubble_window->window,
		      GTK_STATE_NORMAL, GTK_SHADOW_OUT, 
		      NULL, GTK_WIDGET (bubble->bubble_window), "notification",
		      0, 0, req.width, req.height);
  return FALSE;
}

static void
subtract_rectangle (GdkRegion *region, GdkRectangle *rectangle)
{
  GdkRegion *temp_region;

  temp_region = gdk_region_rectangle (rectangle);
  gdk_region_subtract (region, temp_region);
  gdk_region_destroy (temp_region);
}

static GdkRegion *
add_bevels_to_rectangle (GdkRectangle *rectangle)
{
  GdkRectangle temp_rect;
  GdkRegion *region = gdk_region_rectangle (rectangle);

  temp_rect.width = 5;
  temp_rect.height = 1;
  
  /* Top left */
  temp_rect.x = rectangle->x;
  temp_rect.y = rectangle->y;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.width -= 2;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.width -= 1;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.width -= 1;
  temp_rect.height = 2;
  subtract_rectangle (region, &temp_rect);


  /* Top right */
  temp_rect.width = 5;
  temp_rect.height = 1;
  
  temp_rect.x = (rectangle->x + rectangle->width) - temp_rect.width;
  temp_rect.y = rectangle->y;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.x += 2;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.x += 1;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y += 1;
  temp_rect.x += 1;
  temp_rect.height = 2;
  subtract_rectangle (region, &temp_rect);

  /* Bottom right */
  temp_rect.width = 5;
  temp_rect.height = 1;
  
  temp_rect.x = (rectangle->x + rectangle->width) - temp_rect.width;
  temp_rect.y = (rectangle->y + rectangle->height) - temp_rect.height;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.x += 2;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.x += 1;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.x += 1;
  temp_rect.height = 2;
  subtract_rectangle (region, &temp_rect);

  /* Bottom left */
  temp_rect.width = 5;
  temp_rect.height = 1;
  
  temp_rect.x = rectangle->x;
  temp_rect.y = rectangle->y + rectangle->height;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.width -= 2;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.width -= 1;
  subtract_rectangle (region, &temp_rect);

  temp_rect.y -= 1;
  temp_rect.width -= 1;
  temp_rect.height = 2;
  subtract_rectangle (region, &temp_rect);

  return region;
}

static gboolean
idle_notification_expired (gpointer data)
{
  EggNotificationBubble *bubble = data;

  GDK_THREADS_ENTER ();

  g_signal_emit (bubble, egg_notification_bubble_signals[NOTIFICATION_TIMEOUT], 0);
  egg_notification_bubble_hide (bubble);

  GDK_THREADS_LEAVE ();
  return FALSE;
}

static void
draw_bubble (EggNotificationBubble *bubble, guint timeout)
{
  GtkRequisition requisition;
  GtkWidget *widget;
  GtkStyle *style;
  gint x, y, w, h;
  GdkScreen *screen;
  gint monitor_num;
  GdkRectangle monitor;
  GdkPoint triangle_points[3];
  char *markuptext;
  char *markupquoted;
  GdkRectangle rectangle;
  GdkRegion *region;
  GdkRegion *triangle_region;
  enum {
    ORIENT_TOP = 0,
    ORIENT_BOTTOM = 1
  } orient;
  guint rectangle_border;
  guint triangle_offset;

  if (!bubble->bubble_window)
    force_window (bubble);

  gtk_widget_ensure_style (bubble->bubble_window);
  style = bubble->bubble_window->style;
  
  widget = bubble->widget;

  screen = gtk_widget_get_screen (widget);

  if (bubble->icon)
    {
      gtk_box_pack_start_defaults (GTK_BOX (bubble->main_hbox), bubble->icon);
      gtk_box_reorder_child (GTK_BOX (bubble->main_hbox), bubble->icon, 0);
    }

  markupquoted = g_markup_escape_text (bubble->bubble_header_text, -1);
  markuptext = g_strdup_printf ("<b>%s</b>", markupquoted);
  gtk_label_set_markup (GTK_LABEL (bubble->bubble_header_label), markuptext);
  g_free (markuptext);
  g_free (markupquoted);
  gtk_label_set_text (GTK_LABEL (bubble->bubble_body_label), bubble->bubble_body_text);
  
  gtk_window_move (GTK_WINDOW (bubble->bubble_window), 0, 2 * gdk_screen_get_height (screen));
  gtk_widget_show_all (bubble->bubble_window);

  gtk_widget_size_request (bubble->bubble_window, &requisition);
  w = requisition.width;
  h = requisition.height;

  gdk_window_get_origin (widget->window, &x, &y);
  if (GTK_WIDGET_NO_WINDOW (widget))
    {
      x += widget->allocation.x;
      y += widget->allocation.y;
    }

  orient = ORIENT_BOTTOM;

  triangle_offset = 20;

  x -= triangle_offset;
    
  monitor_num = gdk_screen_get_monitor_at_window (screen, widget->window);
  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  if ((x + w) > monitor.x + monitor.width) {
    gint offset = (x + w) - (monitor.x + monitor.width);
    triangle_offset += offset;
    x -= offset;
  } else if (x < monitor.x) {
    gint offset = monitor.x - x;
    triangle_offset -= offset;
    x += offset;
  }

  if ((y + h + widget->allocation.height + 4) > monitor.y + monitor.height)
    {
      y -= (h - 4);
      orient = ORIENT_TOP;
    }
  else
    y = y + widget->allocation.height + 4;

  /* Overlap the arrow with the object slightly */
  if (orient == ORIENT_BOTTOM)
    y -= 5;
  else
    y += 5;

  rectangle_border = BORDER_SIZE-2; 

  rectangle.x = rectangle_border;
  rectangle.y = rectangle_border;
  rectangle.width = w - (rectangle_border * 2);
  rectangle.height = h - (rectangle_border * 2);
  region = add_bevels_to_rectangle (&rectangle);

  triangle_points[0].x = triangle_offset;
  triangle_points[0].y = orient == ORIENT_BOTTOM ? BORDER_SIZE : h - BORDER_SIZE;
  triangle_points[1].x = triangle_points[0].x + 20;
  triangle_points[1].y = triangle_points[0].y;
  triangle_points[2].x = (triangle_points[1].x + triangle_points[0].x) /2;
  triangle_points[2].y = orient == ORIENT_BOTTOM ? 0 : h;

  triangle_region = gdk_region_polygon (triangle_points, 3, GDK_WINDING_RULE);

  gdk_region_union (region, triangle_region);
  gdk_region_destroy (triangle_region);

  gdk_window_shape_combine_region (bubble->bubble_window->window, region, 0, 0);

  gtk_window_move (GTK_WINDOW (bubble->bubble_window), x, y);
  bubble->active = TRUE;
  if (bubble->timeout_id)
    {
      g_source_remove (bubble->timeout_id);
      bubble->timeout_id = 0;
    }
  if (timeout > 0)
    bubble->timeout_id = g_timeout_add (timeout, idle_notification_expired, bubble);
}

void
egg_notification_bubble_show (EggNotificationBubble *bubble, guint timeout)
{
  draw_bubble (bubble, timeout);
}

void
egg_notification_bubble_hide (EggNotificationBubble *bubble)
{
  if (bubble->bubble_window)
    gtk_widget_hide (bubble->bubble_window);
  if (bubble->timeout_id)
    {
      g_source_remove (bubble->timeout_id);
      bubble->timeout_id = 0;
    }
}

EggNotificationBubble*
egg_notification_bubble_new (void)
{
  return g_object_new (EGG_TYPE_NOTIFICATION_BUBBLE, NULL);
}

static void
egg_notification_bubble_event_handler (GtkWidget *widget,
				       GdkEvent  *event,
				       gpointer user_data)
{
  EggNotificationBubble *bubble;

  bubble = EGG_NOTIFICATION_BUBBLE (user_data);

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      g_signal_emit (bubble, egg_notification_bubble_signals[NOTIFICATION_CLICKED], 0);
      break;
    default:
      break;
    }
}

static void
egg_notification_bubble_detach (EggNotificationBubble *bubble)
{
  g_return_if_fail (bubble->widget);

  g_object_unref (bubble->widget);
}
