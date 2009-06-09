/*
 * This is a based on gedit-module.h from gedit, which is based on
 * Epiphany source code.
 *
 * Copyright (C) 2003 Marco Pesenti Gritti
 * Copyright (C) 2003, 2004 Christian Persch
 * Copyright (C) 2005 - Paolo Maggi
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

#ifndef RB_MODULE_H
#define RB_MODULE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_MODULE		(rb_module_get_type ())
#define RB_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_MODULE, RBModule))
#define RB_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_MODULE, RBModuleClass))
#define RB_IS_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_MODULE))
#define RB_IS_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_MODULE))
#define RB_MODULE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), RB_TYPE_MODULE, RBModuleClass))

typedef struct _RBModule	RBModule;

GType		 rb_module_get_type		(void) G_GNUC_CONST;;

RBModule	*rb_module_new		(const gchar *path, const char *module);

const gchar	*rb_module_get_path		(RBModule *module);

GObject		*rb_module_new_object	(RBModule *module);

G_END_DECLS

#endif
