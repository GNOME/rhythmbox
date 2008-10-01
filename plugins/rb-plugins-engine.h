/*
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002-2005 - Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_PLUGINS_ENGINE_H__
#define __RB_PLUGINS_ENGINE_H__

#include <glib.h>
#include <rb-shell.h>

typedef struct _RBPluginInfo RBPluginInfo;

gboolean	 rb_plugins_engine_init 		(RBShell *shell);
void		 rb_plugins_engine_shutdown 		(void);

void		 rb_plugins_engine_garbage_collect	(void);

GList*		rb_plugins_engine_get_plugins_list 	(void);

gboolean 	 rb_plugins_engine_activate_plugin 	(RBPluginInfo *info);
gboolean 	 rb_plugins_engine_deactivate_plugin	(RBPluginInfo *info);
gboolean 	 rb_plugins_engine_plugin_is_active 	(RBPluginInfo *info);
gboolean 	 rb_plugins_engine_plugin_is_visible 	(RBPluginInfo *info);

gboolean	 rb_plugins_engine_plugin_is_configurable
							(RBPluginInfo *info);
void	 	 rb_plugins_engine_configure_plugin	(RBPluginInfo *info,
							 GtkWindow *parent);

const gchar*	rb_plugins_engine_get_plugin_name	(RBPluginInfo *info);
const gchar*	rb_plugins_engine_get_plugin_description
							(RBPluginInfo *info);

const gchar**	rb_plugins_engine_get_plugin_authors	(RBPluginInfo *info);
const gchar*	rb_plugins_engine_get_plugin_website	(RBPluginInfo *info);
const gchar*	rb_plugins_engine_get_plugin_copyright	(RBPluginInfo *info);
GdkPixbuf *	rb_plugins_engine_get_plugin_icon	(RBPluginInfo *info);

#endif  /* __RB_PLUGINS_ENGINE_H__ */
