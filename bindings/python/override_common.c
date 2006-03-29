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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define NO_IMPORT_PYGOBJECT
#include "pygobject.h"
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
_helper_wrap_string_glist (const GList *list)
{
    const GList *tmp;
    PyObject *py_list;

    if ((py_list = PyList_New(0)) == NULL) {
        return NULL;
    }
    for (tmp = list; tmp != NULL; tmp = tmp->next) {
        PyObject *str_obj =  PyString_FromString ((char*)tmp->data);

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
_helper_wrap_boxed_gptrarray (GType type, GPtrArray *list, gboolean own_ref, gboolean dealloc)
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
