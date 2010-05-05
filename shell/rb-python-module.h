/*
 * Copyright (C) 2005 Raphael Slinckx
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

#ifndef RB_PYTHON_MODULE_H
#define RB_PYTHON_MODULE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_PYTHON_MODULE		(rb_python_module_get_type ())
#define RB_PYTHON_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PYTHON_MODULE, RBPythonModule))
#define RB_PYTHON_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_PYTHON_MODULE, RBPythonModuleClass))
#define RB_IS_PYTHON_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PYTHON_MODULE))
#define RB_IS_PYTHON_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_PYTHON_MODULE))
#define RB_PYTHON_MODULE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), RB_TYPE_PYTHON_MODULE, RBPythonModuleClass))

typedef struct
{
	GTypeModuleClass parent_class;
} RBPythonModuleClass;

typedef struct
{
	GTypeModule parent_instance;
} RBPythonModule;

GType			 rb_python_module_get_type		(void);

RBPythonModule	*rb_python_module_new		(const gchar* path,
								 const gchar *module);

GObject			*rb_python_module_new_object		(RBPythonModule *module);

/* --- python utils --- */

void			rb_python_module_init_python		(void);

gboolean		rb_python_init_successful		(void);

void			rb_python_garbage_collect		(void);

void			rb_python_shutdown			(void);

G_END_DECLS

#endif
