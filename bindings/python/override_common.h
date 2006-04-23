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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __OVERRIDE_COMMON_H
#define __OVERRIDE_COMMON_H

PyObject * _helper_wrap_gobject_glist (const GList *list);
PyObject * _helper_wrap_string_glist (const GList *list);
PyObject * _helper_wrap_pointer_glist (const GList *list,
				       GType boxed_type);
PyObject * _helper_wrap_boxed_glist (const GList *list,
				     GType boxed_type,
				     gboolean copy_boxed,
				     gboolean own_ref);
PyObject * _helper_wrap_boxed_gptrarray (GPtrArray *list,
					 GType type,
					 gboolean own_ref,
					 gboolean dealloc);
GList * _helper_unwrap_string_pylist (PyObject *py_list);
GList * _helper_unwrap_pointer_pylist (PyObject *py_list,
				       GType type);
GPtrArray* _helper_unwrap_boxed_gptrarray (PyObject *list,
					   GType type);
#endif /* __OVERRIDE_COMMON_H */

