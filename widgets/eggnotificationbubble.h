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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __EGG_NOTIFICATION_BUBBLE_H__
#define __EGG_NOTIFICATION_BUBBLE_H__

#include <gtk/gtkwidget.h>
#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

#define EGG_TYPE_NOTIFICATION_BUBBLE                  (egg_notification_bubble_get_type ())
#define EGG_NOTIFICATION_BUBBLE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_NOTIFICATION_BUBBLE, EggNotificationBubble))
#define EGG_NOTIFICATION_BUBBLE_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_NOTIFICATION_BUBBLE, EggNotificationBubbleClass))
#define EGG_IS_NOTIFICATION_BUBBLE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_NOTIFICATION_BUBBLE))
#define EGG_IS_NOTIFICATION_BUBBLE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), EGG_TYPE_NOTIFICATION_BUBBLE))
#define EGG_NOTIFICATION_BUBBLE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), EGG_TYPE_NOTIFICATION_BUBBLE, EggNotificationBubbleClass))


typedef struct _EggNotificationBubble	 EggNotificationBubble;
typedef struct _EggNotificationBubbleClass EggNotificationBubbleClass;
typedef struct _EggNotificationBubbleData	 EggNotificationBubbleData;

struct _EggNotificationBubble
{
  GtkObject parent_instance;

  GtkWidget *widget;

  guint timeout_id;

  char *bubble_header_text;
  char *bubble_body_text;
  GtkWidget *icon;

  gboolean active;
  GtkWidget *bubble_window;
  GtkWidget *main_hbox;
  GtkWidget *bubble_header_label;
  GtkWidget *bubble_body_label;
};

struct _EggNotificationBubbleClass
{
  GtkObjectClass parent_class;

  void (*clicked) (void);
  void (*timeout) (void);

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

GType		 egg_notification_bubble_get_type	   (void) G_GNUC_CONST;
EggNotificationBubble*	 egg_notification_bubble_new	   (void);

void             egg_notification_bubble_attach (EggNotificationBubble *bubble, GtkWidget *widget);

void		 egg_notification_bubble_set (EggNotificationBubble   *bubble,
					      const gchar	      *notification_header,
					      GtkWidget	              *icon,
					      const gchar             *notification_body);

void             egg_notification_bubble_show (EggNotificationBubble *bubble, guint timeout);
void             egg_notification_bubble_hide (EggNotificationBubble *bubble);

gboolean         egg_notification_bubble_get_info_from_tip_window (GtkWindow    *window,
                                                        EggNotificationBubble **bubble,
                                                        GtkWidget   **current_widget);

G_END_DECLS

#endif /* __EGG_NOTIFICATION_BUBBLE_H__ */
