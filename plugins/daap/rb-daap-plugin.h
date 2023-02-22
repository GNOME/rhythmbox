/*
 *  Copyright (C) 2006 Jonathan Matthew  <jonathan@kaolin.wh9.net>
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

#ifndef __RB_DAAP_PLUGIN_H
#define __RB_DAAP_PLUGIN_H

#include <libpeas/peas.h>
#include <libdmapsharing/dmap.h>

#include "rb-daap-source.h"

G_BEGIN_DECLS

#define RB_TYPE_DAAP_PLUGIN         (rb_daap_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBDaapPlugin, rb_daap_plugin, RB, DAAP_PLUGIN, PeasExtensionBase)

GType		rb_daap_plugin_get_type		(void);

GIcon *		rb_daap_plugin_get_icon 	(RBDaapPlugin *plugin,
					 	 gboolean password_protected,
					 	 gboolean connected);

RBDAAPSource *	rb_daap_plugin_find_source_for_uri (RBDaapPlugin *plugin,
						 const char *uri);
gboolean	rb_daap_plugin_shutdown		(RBDaapPlugin *plugin);

G_END_DECLS

#endif /* __RB_DAAP_PLUGIN_H */
