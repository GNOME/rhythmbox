/*
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_PLUGIN_MANAGER_H__
#define __RB_PLUGIN_MANAGER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Type checking and casting macros
 */
#define RB_TYPE_PLUGIN_MANAGER              (rb_plugin_manager_get_type())
#define RB_PLUGIN_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), RB_TYPE_PLUGIN_MANAGER, RBPluginManager))
#define RB_PLUGIN_MANAGER_CONST(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), RB_TYPE_PLUGIN_MANAGER, RBPluginManager const))
#define RB_PLUGIN_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), RB_TYPE_PLUGIN_MANAGER, RBPluginManagerClass))
#define RB_IS_PLUGIN_MANAGER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), RB_TYPE_PLUGIN_MANAGER))
#define RB_IS_PLUGIN_MANAGER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_PLUGIN_MANAGER))
#define RB_PLUGIN_MANAGER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), RB_TYPE_PLUGIN_MANAGER, RBPluginManagerClass))

/* Private structure type */
typedef struct _RBPluginManagerPrivate RBPluginManagerPrivate;

/*
 * Main object structure
 */
typedef struct
{
	GtkVBox vbox;

	/*< private > */
	RBPluginManagerPrivate *priv;
} RBPluginManager;

/*
 * Class definition
 */
typedef struct
{
	GtkVBoxClass parent_class;
} RBPluginManagerClass;

/*
 * Public methods
 */
GType		 rb_plugin_manager_get_type		(void) G_GNUC_CONST;

GtkWidget	*rb_plugin_manager_new		(void);

G_END_DECLS

#endif  /* __RB_PLUGIN_MANAGER_H__  */
