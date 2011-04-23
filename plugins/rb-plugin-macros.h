/*
 * Copyright (C) 2011  Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_PLUGIN_MACROS_H
#define RB_PLUGIN_MACROS_H

#include <libpeas/peas.h>

G_BEGIN_DECLS

enum {
	PROP_0,
	PROP_OBJECT
};

#define RB_DEFINE_PLUGIN(TYPE_NAME, TypeName, type_name, TYPE_CODE)	\
 GType type_name##_get_type (void) G_GNUC_CONST;			\
 static void impl_activate (PeasActivatable *plugin);			\
 static void impl_deactivate (PeasActivatable *plugin);			\
 static void peas_activatable_iface_init (PeasActivatableInterface *iface); \
 \
 G_DEFINE_DYNAMIC_TYPE_EXTENDED (TypeName,				\
		 		 type_name,				\
		 		 PEAS_TYPE_EXTENSION_BASE,		\
				 0,					\
				 G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_TYPE_ACTIVATABLE,\
					 			peas_activatable_iface_init)\
		 		 TYPE_CODE)				\
 \
 static void peas_activatable_iface_init (PeasActivatableInterface *iface) \
 {									\
	iface->activate = impl_activate;				\
	iface->deactivate = impl_deactivate;				\
 }									\
 \
 static void set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) \
 {									\
	switch (prop_id) {						\
	case PROP_OBJECT:						\
		g_object_set_data_full (object,				\
					"rb-shell",			\
					g_value_dup_object (value),	\
					g_object_unref);		\
		break;							\
	default:							\
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); \
		break;							\
	}								\
 }									\
 static void get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) \
 {									\
	switch (prop_id) {						\
	case PROP_OBJECT:						\
		g_value_set_object (value, g_object_get_data (object, "rb-shell")); \
		break;							\
	default:							\
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); \
		break;							\
	}								\
 }									\
 static void type_name##_class_init (TypeName##Class *klass)		\
 {									\
	GObjectClass *object_class = G_OBJECT_CLASS (klass);		\
	object_class->set_property = set_property;			\
	object_class->get_property = get_property;			\
	g_object_class_override_property (object_class, PROP_OBJECT, "object"); \
 }									\
 static void type_name##_class_finalize (TypeName##Class *klass)	\
 {									\
 }
 

G_END_DECLS

#endif /* RB_PLUGIN_MACROS_H */
