/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Utility functions for Python bindings.
 * Stolen from Epiphany.
 *
 * Copyright (C) 2005 Adam Hooper <adamh@cvs.gnome.org>
 * Copyright (C) 2005 Christian Persch <chpe@cvs.gnome.org>
 * Copyright (C) 2005 Crispin Flowerday <gnome@flowerday.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include "config.h"

#define NO_IMPORT_PYGOBJECT
#define NO_IMPORT_PYGTK
#include <pygobject.h>
#include <pygtk/pygtk.h>

#include "override_common.h"

PyObject *
_helper_wrap_gobject_glist (const GList *list)
{
    PyObject *py_list;
    const GList *tmp;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for (tmp = list; tmp != NULL; tmp = tmp->next) {
        PyObject *py_obj = pygobject_new(G_OBJECT(tmp->data));

        if (py_obj == NULL) {
            Py_DECREF(py_list);
            return NULL;
        }
        PyList_Append(py_list, py_obj);
        Py_DECREF(py_obj);
    }
    return py_list;
}

PyObject *
_helper_wrap_pointer_glist (const GList *list, GType boxed_type)
{
    PyObject *py_list;
    const GList *tmp;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for (tmp = list; tmp != NULL; tmp = tmp->next) {
        PyObject *py_obj = pyg_pointer_new(boxed_type, tmp->data);

        if (py_obj == NULL) {
            Py_DECREF(py_list);
            return NULL;
        }
        PyList_Append(py_list, py_obj);
        Py_DECREF(py_obj);
    }
    return py_list;
}

PyObject *
_helper_wrap_boxed_glist (const GList *list,
			  GType boxed_type,
			  gboolean copy_boxed,
			  gboolean own_ref)
{
    PyObject *py_list;
    const GList *tmp;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for (tmp = list; tmp != NULL; tmp = tmp->next) {
        PyObject *py_obj = pyg_boxed_new(boxed_type, tmp->data, copy_boxed, own_ref);

        if (py_obj == NULL) {
            Py_DECREF(py_list);
            return NULL;
        }
        PyList_Append(py_list, py_obj);
        Py_DECREF(py_obj);
    }
    return py_list;
}

PyObject *
_helper_wrap_string_glist (const GList *list)
{
    const GList *tmp;
    PyObject *py_list;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for (tmp = list; tmp != NULL; tmp = tmp->next) {
        PyObject *str_obj =  PyString_FromString (tmp->data);

        if (str_obj == NULL) {
            Py_DECREF(py_list);
            return NULL;
        }
        PyList_Append(py_list, str_obj);
        Py_DECREF(str_obj);
    }
    return py_list;
}

PyObject *
_helper_wrap_boxed_gptrarray (GPtrArray *list, GType type, gboolean own_ref, gboolean dealloc)
{
    PyObject *py_list;
    int i;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for( i = 0; i < list->len; i++ ) {
        PyObject *obj = pyg_boxed_new (type, g_ptr_array_index(list,i), FALSE, own_ref);
        PyList_Append(py_list, obj);
        Py_DECREF(obj);
    }
    if (dealloc) g_ptr_array_free (list, TRUE);
    return py_list;
}

GList *
_helper_unwrap_pointer_pylist (PyObject *py_list, GType type)
{
	int size, i;
	GList *list = NULL;

	size = PyList_Size (py_list);
	for (i = 0; i < size; i++) {
        	PyObject *py_ptr;
        	gpointer ptr;

		py_ptr = PyList_GetItem (py_list, i);
		if (!pyg_pointer_check (py_ptr, type)) {
			g_list_free (list);
			return NULL;
		}
		ptr = pyg_pointer_get (py_ptr, void);
		list = g_list_prepend (list, ptr);
	}

	list = g_list_reverse (list);
	return list;
}

GList *
_helper_unwrap_boxed_pylist (PyObject *py_list, GType type)
{
	int size, i;
	GList *list = NULL;

	size = PyList_Size (py_list);
	for (i = 0; i < size; i++) {
        	PyObject *py_ptr;
        	gpointer ptr;

		py_ptr = PyList_GetItem (py_list, i);
		if (!pyg_boxed_check (py_ptr, type)) {
			g_list_free (list);
			return NULL;
		}
		ptr = pyg_boxed_get (py_ptr, void);
		list = g_list_prepend (list, ptr);
	}

	list = g_list_reverse (list);
	return list;
}

GList *
_helper_unwrap_gobject_pylist (PyObject *py_list)
{
	int size, i;
	GList *list = NULL;

	size = PyList_Size (py_list);
	for (i = 0; i < size; i++) {
        	PyObject *py_ptr;
        	gpointer ptr;

		py_ptr = PyList_GetItem (py_list, i);
		ptr = pygobject_get (py_ptr);
		list = g_list_prepend (list, ptr);
	}

	list = g_list_reverse (list);
	return list;
}

GList *
_helper_unwrap_string_pylist (PyObject *py_list, gboolean duplicate)
{
    int size, i;
    GList *list = NULL;

    size = PyList_Size (py_list);
    for (i = 0; i < size; i++) {
        PyObject *py_str;
        char *str;

	py_str = PyList_GetItem (py_list, i);
	str = PyString_AsString (py_str);
	if (duplicate)
		list = g_list_prepend (list, g_strdup (str));
	else
		list = g_list_prepend (list, str);
    }

    list = g_list_reverse (list);
    return list;
}

GPtrArray*
_helper_unwrap_boxed_gptrarray (PyObject *list, GType type)
{
	int size, i;
	GPtrArray *array;

	array = g_ptr_array_new ();
	size = PyList_Size (list);
	for (i = 0; i < size; i++) {
		PyObject *py_boxed;
		gpointer boxed;

		py_boxed = PyList_GetItem (list, i);
		if (!pyg_boxed_check (py_boxed, type)) {
			g_ptr_array_free (array, FALSE);
			return NULL;
		}

		boxed = pyg_boxed_get (py_boxed, void);
		g_ptr_array_add (array, boxed);
	}

	return array;
}

/* query model sorting stuff */
void
_rhythmdb_query_model_sort_data_free (PyRhythmDBQueryModelSortData *data)
{
	PyGILState_STATE __py_state;
	__py_state = pyg_gil_state_ensure();

	Py_DECREF (data->func);
	Py_DECREF (data->data);
	g_free (data);

	pyg_gil_state_release(__py_state);
}

int
_rhythmdb_query_model_sort_func (RhythmDBEntry *a, RhythmDBEntry *b, PyRhythmDBQueryModelSortData *data)
{
	PyObject *args;
	PyObject *py_result;
	PyObject *py_a, *py_b;
	int result;
	PyGILState_STATE __py_state;

	__py_state = pyg_gil_state_ensure();

	py_a = pyg_boxed_new (RHYTHMDB_TYPE_ENTRY, a, FALSE, FALSE);
	py_b = pyg_boxed_new (RHYTHMDB_TYPE_ENTRY, b, FALSE, FALSE);
	if (data->data)
		args = Py_BuildValue ("(OOO)", py_a, py_b, data->data);
	else
		args = Py_BuildValue ("(OO)", py_a, py_b);

	Py_DECREF (py_a);
	Py_DECREF (py_b);

	py_result = PyEval_CallObject (data->func, args);
	Py_DECREF (args);

	if (!py_result) {
		PyErr_Print();
		return 0;
	}
	result = PyInt_AsLong (py_result);

	Py_DECREF (py_result);
	pyg_gil_state_release(__py_state);

	return result;
}
/* end query model sorting stuff */
