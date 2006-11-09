/*
 *  Copyright (C) 2006 Jonathan Matthew  <jonathan@kaolin.wh9.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifndef __RB_DAAP_PLUGIN_H
#define __RB_DAAP_PLUGIN_H

#include "rb-plugin.h"
#include "rb-daap-source.h"

G_BEGIN_DECLS

#define RB_TYPE_DAAP_PLUGIN         (rb_daap_plugin_get_type ())
#define RB_DAAP_PLUGIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_PLUGIN, RBDaapPlugin))
#define RB_DAAP_PLUGIN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_PLUGIN, RBDaapPluginClass))
#define RB_IS_DAAP_PLUGIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_PLUGIN))
#define RB_IS_DAAP_PLUGIN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_PLUGIN))
#define RB_DAAP_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_PLUGIN, RBDaapPluginClass))

typedef struct RBDaapPluginPrivate RBDaapPluginPrivate;

typedef struct
{
	RBPlugin parent;

	RBDaapPluginPrivate *priv;
} RBDaapPlugin;

typedef struct
{
	RBPluginClass parent;
} RBDaapPluginClass;

GType		rb_daap_plugin_get_type		(void);

GdkPixbuf *	rb_daap_plugin_get_icon 	(RBDaapPlugin *plugin,
					 	 gboolean password_protected,
					 	 gboolean connected);

RBDAAPSource *	rb_daap_plugin_find_source_for_uri (RBDaapPlugin *plugin,
						 const char *uri);

G_END_DECLS

#endif /* __RB_DAAP_PLUGIN_H */

