/*
 * heavily based on code from Gedit, which is based on code from Ephiphany
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

#include "config.h"

#include "rb-module.h"
#include "rb-debug.h"

#include <gmodule.h>

typedef struct _RBModuleClass RBModuleClass;

struct _RBModuleClass
{
	GTypeModuleClass parent_class;
};

struct _RBModule
{
	GTypeModule parent_instance;

	GModule *library;

	gchar *path;
	gchar *name;
	GType type;
};

typedef GType (*RBModuleRegisterFunc) (GTypeModule *);

static void rb_module_init		(RBModule *action);
static void rb_module_class_init	(RBModuleClass *class);

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (RBModule, rb_module, G_TYPE_TYPE_MODULE)

static gboolean
rb_module_load (GTypeModule *gmodule)
{
	RBModule *module = RB_MODULE (gmodule);
	RBModuleRegisterFunc register_func;

	rb_debug ("Loading %s", module->path);

	module->library = g_module_open (module->path, 0);

	if (module->library == NULL) {
		g_warning ("%s", g_module_error ());

		return FALSE;
	}

	/* extract symbols from the lib */
	if (!g_module_symbol (module->library, "register_rb_plugin", (void *)&register_func)) {
		g_warning ("%s", g_module_error ());
		g_module_close (module->library);
		return FALSE;
	}

	g_assert (register_func);

	module->type = register_func (gmodule);
	if (module->type == 0) {
		g_warning ("Invalid rb plugin contained by module %s", module->path);
		return FALSE;
	}

	return TRUE;
}

static void
rb_module_unload (GTypeModule *gmodule)
{
	RBModule *module = RB_MODULE (gmodule);

	rb_debug ("Unloading %s", module->path);

	g_module_close (module->library);

	module->library = NULL;
	module->type = 0;
}

const gchar *
rb_module_get_path (RBModule *module)
{
	g_return_val_if_fail (RB_IS_MODULE (module), NULL);

	return module->path;
}

GObject *
rb_module_new_object (RBModule *module)
{
	GObject *obj;
	rb_debug ("Creating object of type %s", g_type_name (module->type));

	if (module->type == 0) {
		return NULL;
	}

	obj = g_object_new (module->type,
			    "name", module->name,
			    NULL);
	return obj;
}

static void
rb_module_init (RBModule *module)
{
	rb_debug ("RBModule %p initialising", module);
}

static void
rb_module_finalize (GObject *object)
{
	RBModule *module = RB_MODULE (object);

	rb_debug ("RBModule %p finalising", module);

	g_free (module->path);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_module_class_init (RBModuleClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GTypeModuleClass *module_class = G_TYPE_MODULE_CLASS (class);

	parent_class = (GObjectClass *) g_type_class_peek_parent (class);

	object_class->finalize = rb_module_finalize;

	module_class->load = rb_module_load;
	module_class->unload = rb_module_unload;
}

RBModule *
rb_module_new (const gchar *path, const char *module)
{
	RBModule *result;

	if (path == NULL || path[0] == '\0') {
		return NULL;
	}

	result = g_object_new (RB_TYPE_MODULE, NULL);

	g_type_module_set_name (G_TYPE_MODULE (result), path);
	result->path = g_strdup (path);
	result->name = g_strdup (module);

	return result;
}
