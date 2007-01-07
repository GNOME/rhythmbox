/*
 * @file libsexy/sexy-tooltip.h A flexible tooltip widget.
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
#ifndef _SEXY_TOOLTIP_H_
#define _SEXY_TOOLTIP_H_

typedef struct _SexyTooltip      SexyTooltip;
typedef struct _SexyTooltipClass SexyTooltipClass;
typedef struct _SexyTooltipPriv  SexyTooltipPriv;

#include <gtk/gtkwindow.h>

#define SEXY_TYPE_TOOLTIP            (sexy_tooltip_get_type())
#define SEXY_TOOLTIP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SEXY_TYPE_TOOLTIP, SexyTooltip))
#define SEXY_TOOLTIP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SEXY_TYPE_TOOLTIP, SexyTooltipClass))
#define SEXY_IS_TOOLTIP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SEXY_TYPE_TOOLTIP))
#define SEXY_IS_TOOLTIP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SEXY_TYPE_TOOLTIP))
#define SEXY_TOOLTIP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SEXY_TYPE_TOOLTIP, SexyTooltipClass))

struct _SexyTooltip
{
	GtkWindow parent;

	SexyTooltipPriv *priv;
};

struct _SexyTooltipClass
{
	GtkWindowClass parent_class;
};

G_BEGIN_DECLS

GType      sexy_tooltip_get_type(void);
GtkWidget *sexy_tooltip_new(void);
GtkWidget *sexy_tooltip_new_with_label(const gchar *text);
void       sexy_tooltip_position_to_widget(SexyTooltip *tooltip, GtkWidget *widget);
void       sexy_tooltip_position_to_rect(SexyTooltip *tooltip, GdkRectangle *rect, GdkScreen *screen);

G_END_DECLS

#endif /* _SEXY_TOOLTIP_H_ */
