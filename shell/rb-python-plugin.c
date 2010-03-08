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

#include <config.h>

#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif

#include "rb-python-plugin.h"
#include "rb-plugin.h"
#include "rb-debug.h"

#define NO_IMPORT_PYGOBJECT
#include <pygobject.h>
#include <string.h>

static GObjectClass *parent_class;

static PyObject *
call_python_method (RBPythonObject *object,
		    RBShell       *shell,
		    gchar             *method)
{
	PyObject *py_ret = NULL;

	g_return_val_if_fail (PyObject_HasAttrString (object->instance, method), NULL);

	if (shell == NULL)
	{
		py_ret = PyObject_CallMethod (object->instance,
					      method,
					      NULL);
	}
	else
	{
		py_ret = PyObject_CallMethod (object->instance,
					      method,
					      "(N)",
					      pygobject_new (G_OBJECT (shell)));
	}

	if (!py_ret)
		PyErr_Print ();

	return py_ret;
}

static gboolean
check_py_object_is_gtk_widget (PyObject *py_obj)
{
	static PyTypeObject *_PyGtkWidget_Type = NULL;

	if (_PyGtkWidget_Type == NULL)
	{
		PyObject *module;

		if ((module = PyImport_ImportModule ("gtk")))
		{
			PyObject *moddict = PyModule_GetDict (module);
			_PyGtkWidget_Type = (PyTypeObject *) PyDict_GetItemString (moddict, "Widget");
		}

		if (_PyGtkWidget_Type == NULL)
		{
			PyErr_SetString(PyExc_TypeError, "could not find python gtk widget type");
			PyErr_Print();

			return FALSE;
		}
	}

	return PyObject_TypeCheck (py_obj, _PyGtkWidget_Type) ? TRUE : FALSE;
}

static void
impl_deactivate (RBPlugin *plugin,
		 RBShell *shell)
{
	PyGILState_STATE state = pyg_gil_state_ensure ();
	RBPythonObject *object = (RBPythonObject *)plugin;

	if (PyObject_HasAttrString (object->instance, "deactivate"))
	{
		PyObject *py_ret = call_python_method (object, shell, "deactivate");

		if (py_ret)
		{
			Py_XDECREF (py_ret);
		}
	}
	else
		RB_PLUGIN_CLASS (parent_class)->deactivate (plugin, shell);

	pyg_gil_state_release (state);
}

static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	PyGILState_STATE state = pyg_gil_state_ensure ();
	RBPythonObject *object = (RBPythonObject *)plugin;

	if (PyObject_HasAttrString (object->instance, "activate"))
	{
		PyObject *py_ret = call_python_method (object, shell, "activate");

		if (py_ret)
		{
			Py_XDECREF (py_ret);
		}
	}
	else
		RB_PLUGIN_CLASS (parent_class)->activate (plugin, shell);

	pyg_gil_state_release (state);
}

static GtkWidget *
impl_create_configure_dialog (RBPlugin *plugin)
{
	PyGILState_STATE state = pyg_gil_state_ensure ();
	RBPythonObject *object = (RBPythonObject *)plugin;
	GtkWidget *ret = NULL;

	if (PyObject_HasAttrString (object->instance, "create_configure_dialog"))
	{
		PyObject *py_ret = call_python_method (object, NULL, "create_configure_dialog");

		if (py_ret)
		{
			if (check_py_object_is_gtk_widget (py_ret))
			{
				ret = GTK_WIDGET (pygobject_get (py_ret));
				g_object_ref (ret);
			}
			else
			{
				PyErr_SetString(PyExc_TypeError, "return value for create_configure_dialog is not a GtkWidget");
				PyErr_Print();
			}

			Py_DECREF (py_ret);
		}
	}
	else
		ret = RB_PLUGIN_CLASS (parent_class)->create_configure_dialog (plugin);

	pyg_gil_state_release (state);
	return ret;
}

static gboolean
impl_is_configurable (RBPlugin *plugin)
{
	PyGILState_STATE state = pyg_gil_state_ensure ();
	RBPythonObject *object = (RBPythonObject *) plugin;
	PyObject *dict = object->instance->ob_type->tp_dict;
	gboolean result;

	if (dict == NULL)
		result = FALSE;
	else if (!PyDict_Check(dict))
		result = FALSE;
	else
		result = PyDict_GetItemString(dict, "create_configure_dialog") != NULL;

	pyg_gil_state_release (state);

	return result;
}

static void
rb_python_object_init (RBPythonObject *object)
{
	RBPythonObjectClass *class;
	PyGILState_STATE state = pyg_gil_state_ensure ();

	rb_debug ("Creating python plugin instance");

	class = (RBPythonObjectClass*) (((GTypeInstance*) object)->g_class);

	object->instance = PyObject_CallObject (class->type, NULL);
	if (object->instance == NULL)
		PyErr_Print();

	pyg_gil_state_release (state);
}

static void
rb_python_object_finalize (GObject *object)
{
	PyGILState_STATE state = pyg_gil_state_ensure ();

	rb_debug ("Finalizing python plugin instance");

	if (((RBPythonObject *) object)->instance) {
		Py_DECREF (((RBPythonObject *) object)->instance);
	}

	pyg_gil_state_release (state);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_python_object_class_init (RBPythonObjectClass *klass,
			     gpointer                class_data)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	klass->type = (PyObject*) class_data;

	object_class->finalize = rb_python_object_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	plugin_class->create_configure_dialog = impl_create_configure_dialog;
	plugin_class->is_configurable = impl_is_configurable;
}

GType
rb_python_object_get_type (GTypeModule *module,
			      PyObject    *type)
{
	GType gtype;
	gchar *type_name;

	GTypeInfo info = {
		sizeof (RBPythonObjectClass),
		NULL,		/* base_init */
		NULL,		/* base_finalize */
		(GClassInitFunc) rb_python_object_class_init,
		NULL,		/* class_finalize */
		type,		/* class_data */
		sizeof (RBPythonObject),
		0,		/* n_preallocs */
		(GInstanceInitFunc) rb_python_object_init,
	};

	Py_INCREF (type);

	type_name = g_strdup_printf ("%s+RBPythonPlugin",
				     PyString_AsString (PyObject_GetAttrString (type, "__name__")));

	rb_debug ("Registering python plugin instance: %s", type_name);
	gtype = g_type_module_register_type (module,
					     RB_TYPE_PLUGIN,
					     type_name,
					     &info, 0);
	g_free (type_name);

	return gtype;
}
