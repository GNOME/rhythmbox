/*
 * Copyright (C) 2005 Raphael Slinckx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA. 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pygobject.h>
#include <pygtk/pygtk.h>

#include <signal.h>

#include <gmodule.h>

#include "rb-python-module.h"
#include "rb-python-plugin.h"
#include "rb-debug.h"

#define RB_PYTHON_MODULE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), \
						 RB_TYPE_PYTHON_MODULE, \
						 RBPythonModulePrivate))

typedef struct
{
	gchar *module;
	gchar *path;
	GType type;
} RBPythonModulePrivate;

enum
{
	PROP_0,
	PROP_PATH,
	PROP_MODULE
};

/* Exported by pyrhythmdb module */
void pyrhythmdb_register_classes (PyObject *d);
void pyrhythmdb_add_constants (PyObject *module, const gchar *strip_prefix);
extern PyMethodDef pyrhythmdb_functions[];

/* Exported by pyrb module */
void pyrb_register_classes (PyObject *d);
void pyrb_add_constants (PyObject *module, const gchar *strip_prefix);
extern PyMethodDef pyrb_functions[];

/* We retreive this to check for correct class hierarchy */
static PyTypeObject *PyRBPlugin_Type;

G_DEFINE_TYPE (RBPythonModule, rb_python_module, G_TYPE_TYPE_MODULE);

static void
rb_python_module_init_python ()
{
	PyObject *pygtk, *mdict, *require;
	PyObject *rb, *rhythmdb, *gtk, *pygtk_version, *pygtk_required_version;
	PyObject *gettext, *install, *gettext_args;
	struct sigaction old_sigint;
	gint res;
	char *argv[] = { "rb", "rhythmdb", NULL };
	
	if (Py_IsInitialized ())
	{
		g_warning ("Python Should only be initialized once, since it's in class_init");
		g_return_if_reached ();
	}

	/* Hack to make python not overwrite SIGINT: this is needed to avoid
	 * the crash reported on gedit bug #326191 */
	
	/* Save old handler */
	res = sigaction (SIGINT, NULL, &old_sigint);  
	if (res != 0)
	{
		g_warning ("Error initializing Python interpreter: cannot get "
		           "handler to SIGINT signal (%s)",
		           strerror (errno));

		return;
	}

	/* Python initialization */
	Py_Initialize ();

	/* Restore old handler */
	res = sigaction (SIGINT, &old_sigint, NULL);
	if (res != 0)
	{
		g_warning ("Error initializing Python interpreter: cannot restore "
		           "handler to SIGINT signal (%s)",
		           strerror (errno));
		return;
	}

	PySys_SetArgv (1, argv);

	/* pygtk.require("2.0") */
	pygtk = PyImport_ImportModule ("pygtk");
	if (pygtk == NULL)
	{
		g_warning ("Could not import pygtk");
		return;
	}

	mdict = PyModule_GetDict (pygtk);
	require = PyDict_GetItemString (mdict, "require");
	PyObject_CallObject (require, Py_BuildValue ("(S)", PyString_FromString ("2.0")));

	/* import gobject */
	init_pygobject ();

	/* import gtk */
	init_pygtk ();

	/* gtk.pygtk_version < (2, 4, 0) */
	gtk = PyImport_ImportModule ("gtk");
	if (gtk == NULL)
	{
		g_warning ("Could not import gtk");
		return;
	}

	mdict = PyModule_GetDict (gtk);
	pygtk_version = PyDict_GetItemString (mdict, "pygtk_version");
	pygtk_required_version = Py_BuildValue ("(iii)", 2, 4, 0);
	if (PyObject_Compare (pygtk_version, pygtk_required_version) == -1)
	{
		g_warning("PyGTK %s required, but %s found.",
				  PyString_AsString (PyObject_Repr (pygtk_required_version)),
				  PyString_AsString (PyObject_Repr (pygtk_version)));
		Py_DECREF (pygtk_required_version);
		return;
	}
	Py_DECREF (pygtk_required_version);

	/* import rhythmdb */
	rhythmdb = Py_InitModule ("rhythmdb", pyrhythmdb_functions);
	mdict = PyModule_GetDict (rhythmdb);

	pyrhythmdb_register_classes (mdict);
	pyrhythmdb_add_constants (rhythmdb, "RHYTHMDB_");

	/* import rb */
	rb = Py_InitModule ("rb", pyrb_functions);
	mdict = PyModule_GetDict (rb);

	pyrb_register_classes (mdict);
	pyrb_add_constants (rb, "RB_");

	/* Retreive the Python type for rb.Plugin */
	PyRBPlugin_Type = (PyTypeObject *) PyDict_GetItemString (mdict, "Plugin"); 
	if (PyRBPlugin_Type == NULL)
	{
		PyErr_Print ();
		return;
	}

	/* i18n support */
	gettext = PyImport_ImportModule ("gettext");
	if (gettext == NULL)
	{
		g_warning ("Could not import gettext");
		return;
	}

	mdict = PyModule_GetDict (gettext);
	install = PyDict_GetItemString (mdict, "install");
	gettext_args = Py_BuildValue ("ss", GETTEXT_PACKAGE, GNOMELOCALEDIR);
	PyObject_CallObject (install, gettext_args);
	Py_DECREF (gettext_args);
}

static gboolean
rb_python_module_load (GTypeModule *gmodule)
{
	RBPythonModulePrivate *priv = RB_PYTHON_MODULE_GET_PRIVATE (gmodule);
	PyObject *main_module, *main_locals, *locals, *key, *value;
	PyObject *module, *fromlist;
	int pos = 0;

	main_module = PyImport_AddModule ("__main__");
	if (main_module == NULL)
	{
		g_warning ("Could not get __main__.");
		return FALSE;
	}

	/* If we have a special path, we register it */
	if (priv->path != NULL)
	{
		PyObject *sys_path = PySys_GetObject ("path");
		PyObject *path = PyString_FromString (priv->path);

		if (PySequence_Contains(sys_path, path) == 0)
			PyList_Insert (sys_path, 0, path);

		Py_DECREF(path);
	}
	
	main_locals = PyModule_GetDict (main_module);
	/* we need a fromlist to be able to import modules with a '.' in the
	   name. */
	fromlist = PyTuple_New(0);
	module = PyImport_ImportModuleEx (priv->module, main_locals,
					  main_locals, fromlist); 
	Py_DECREF (fromlist);
	if (!module) {
		PyErr_Print ();
		return FALSE;
	}

	locals = PyModule_GetDict (module);
	while (PyDict_Next (locals, &pos, &key, &value))
	{
		if (!PyType_Check(value))
			continue;

		if (PyObject_IsSubclass (value, (PyObject*) PyRBPlugin_Type))
		{
			priv->type = rb_python_object_get_type (gmodule, value);
			return TRUE;
		}
	}

	return FALSE;
}

static void
rb_python_module_unload (GTypeModule *module)
{
	RBPythonModulePrivate *priv = RB_PYTHON_MODULE_GET_PRIVATE (module);
	rb_debug ("Unloading python module");
	
	priv->type = 0;
}

GObject *
rb_python_module_new_object (RBPythonModule *module)
{
	RBPythonModulePrivate *priv = RB_PYTHON_MODULE_GET_PRIVATE (module);
	RBPythonObject *object;

	if (priv->type == 0)
		return NULL;

	rb_debug ("Creating object of type %s", g_type_name (priv->type));
	object = (RBPythonObject*) (g_object_new (priv->type, NULL));
	if (object->instance)
		return G_OBJECT (object);

	/* object could not be instantiated */
	g_warning ("could not instantiate python object");

	g_object_unref (G_OBJECT (object));
	return NULL;
}

static void
rb_python_module_init (RBPythonModule *module)
{
	rb_debug ("Init of python module");
}

static void
rb_python_module_finalize (GObject *object)
{
	RBPythonModulePrivate *priv = RB_PYTHON_MODULE_GET_PRIVATE (object);
	rb_debug ("Finalizing python module %s", g_type_name (priv->type));

	g_free (priv->module);
	g_free (priv->path);

	G_OBJECT_CLASS (rb_python_module_parent_class)->finalize (object);
}

static void
rb_python_module_get_property (GObject    *object,
				  guint       prop_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
	/* no readable properties */
	g_return_if_reached ();
}

static void
rb_python_module_set_property (GObject      *object,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
	RBPythonModule *mod = RB_PYTHON_MODULE (object);

	switch (prop_id)
	{
		case PROP_MODULE:
			RB_PYTHON_MODULE_GET_PRIVATE (mod)->module = g_value_dup_string (value);
			break;
		case PROP_PATH:
			RB_PYTHON_MODULE_GET_PRIVATE (mod)->path = g_value_dup_string (value);
			break;
		default:
			g_return_if_reached ();
	}
}

static void
rb_python_module_class_init (RBPythonModuleClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GTypeModuleClass *module_class = G_TYPE_MODULE_CLASS (class);

	object_class->finalize = rb_python_module_finalize;
	object_class->get_property = rb_python_module_get_property;
	object_class->set_property = rb_python_module_set_property;

	g_object_class_install_property
			(object_class,
			 PROP_MODULE,
			 g_param_spec_string ("module",
					      "Module Name",
					      "The python module to load for this plugin",
					      NULL,
					      G_PARAM_WRITABLE | G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));
					      
	g_object_class_install_property
			(object_class,
			 PROP_PATH,
			 g_param_spec_string ("path",
					      "Path",
					      "The python path to use when loading this module",
					      NULL,
					      G_PARAM_WRITABLE | G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (RBPythonModulePrivate));
	
	module_class->load = rb_python_module_load;
	module_class->unload = rb_python_module_unload;

	/* Init python subsystem, this should happen only once
	 * in the process lifetime, and doing it here is ok since
	 * class_init is called once */
	rb_python_module_init_python ();
}

RBPythonModule *
rb_python_module_new (const gchar *path,
			 const gchar *module)
{
	RBPythonModule *result;

	if (module == NULL || module[0] == '\0')
		return NULL;

	result = g_object_new (RB_TYPE_PYTHON_MODULE,
			       "module", module,
			       "path", path,
			       NULL);

	g_type_module_set_name (G_TYPE_MODULE (result), module);

	return result;
}


/* --- these are not module methods, they are here out of convenience --- */

static gint idle_garbage_collect_id = 0;

static gboolean
run_gc (gpointer data)
{
	while (PyGC_Collect ())
		;

	idle_garbage_collect_id = 0;

	return FALSE;
}

void
rb_python_garbage_collect ()
{
	if (Py_IsInitialized() && idle_garbage_collect_id == 0) {
		idle_garbage_collect_id = g_idle_add (run_gc, NULL);
	}
}

static gboolean
finalise_collect_cb (gpointer data)
{
	while (PyGC_Collect ())
		;

	/* useful if python is refusing to give up it's shell reference for some reason.
	PyRun_SimpleString ("import gc, gobject\nfor o in gc.get_objects():\n\tif isinstance(o, gobject.GObject):\n\t\tprint o, gc.get_referrers(o)");
	*/
	
	return TRUE;
}

void
rb_python_shutdown ()
{
	if (Py_IsInitialized ()) {
		if (idle_garbage_collect_id != 0) {
			g_source_remove (idle_garbage_collect_id);
			idle_garbage_collect_id = 0;
		}

		run_gc (NULL);
		/* this helps to force python to give up it's shell reference */
		g_timeout_add (1000, finalise_collect_cb, NULL);

		/* disable for now, due to bug 334188
		Py_Finalize ();*/
	}
}

