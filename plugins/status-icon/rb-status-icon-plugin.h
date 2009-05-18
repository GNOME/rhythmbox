/*
 *  Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RB_STATUS_ICON_PLUGIN_H
#define __RB_STATUS_ICON_PLUGIN_H

#include "rb-plugin.h"

G_BEGIN_DECLS

#define RB_TYPE_STATUS_ICON_PLUGIN         (rb_status_icon_plugin_get_type ())
#define RB_STATUS_ICON_PLUGIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STATUS_ICON_PLUGIN, RBStatusIconPlugin))
#define RB_STATUS_ICON_PLUGIN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STATUS_ICON_PLUGIN, RBStatusIconPluginClass))
#define RB_IS_STATUS_ICON_PLUGIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STATUS_ICON_PLUGIN))
#define RB_IS_STATUS_ICON_PLUGIN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STATUS_ICON_PLUGIN))
#define RB_STATUS_ICON_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STATUS_ICON_PLUGIN, RBStatusIconPluginClass))

typedef struct _RBStatusIconPlugin RBStatusIconPlugin;
typedef struct _RBStatusIconPluginClass RBStatusIconPluginClass;
typedef struct _RBStatusIconPluginPrivate RBStatusIconPluginPrivate;

struct _RBStatusIconPlugin
{
	RBPlugin parent;

	RBStatusIconPluginPrivate *priv;
};

struct _RBStatusIconPluginClass
{
	RBPluginClass parent;
};

GType		rb_status_icon_plugin_get_type		(void);

/* methods for icon implementations to call */

void		rb_status_icon_plugin_scroll_event	(RBStatusIconPlugin *plugin,
							 GdkEventScroll *event);

void		rb_status_icon_plugin_button_press_event (RBStatusIconPlugin *plugin,
							 GdkEventButton *event);

gboolean	rb_status_icon_plugin_set_tooltip	(GtkWidget *widget,
							 gint x,
							 gint y,
							 gboolean keyboard_tooltip,
							 GtkTooltip *tooltip,
							 RBStatusIconPlugin *plugin);

G_END_DECLS

#endif /* __RB_STATUS_ICON_PLUGIN_H */

