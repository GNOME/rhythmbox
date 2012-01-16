/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011  Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <gio/gio.h>

#include "rhythmdb.h"
#include "rhythmdb-private.h"

static const char *RHYTHMDB_OBJECT_PATH = "/org/gnome/Rhythmbox3/RhythmDB";
static const char *RHYTHMDB_INTERFACE_NAME = "org.gnome.Rhythmbox3.RhythmDB";

static const char *rhythmdb_dbus_spec =
"<node>"
"  <interface name='org.gnome.Rhythmbox3.RhythmDB'>"
"    <method name='GetEntryProperties'>"
"      <arg name='uri' type='s'/>"
"      <arg name='properties' type='a{sv}' direction='out'/>"
"    </method>"
"    <method name='SetEntryProperties'>"
"      <arg name='uri' type='s'/>"
"      <arg name='properties' type='a{sv}'/>"
"    </method>"
"  </interface>"
"</node>";

static void
return_entry_not_found (GDBusMethodInvocation *invocation, const char *uri)
{
	g_dbus_method_invocation_return_error (invocation,
					       G_DBUS_ERROR,
					       G_DBUS_ERROR_FILE_NOT_FOUND,
					       "No database entry %s exists",
					       uri);
}

static void
rhythmdb_method_call (GDBusConnection *connection,
		      const char *sender,
		      const char *object_path,
		      const char *interface_name,
		      const char *method_name,
		      GVariant *parameters,
		      GDBusMethodInvocation *invocation,
		      RhythmDB *db)
{
	const char *uri;
	RhythmDBEntry *entry;

	if (g_strcmp0 (object_path, RHYTHMDB_OBJECT_PATH) != 0 ||
	    g_strcmp0 (interface_name, RHYTHMDB_INTERFACE_NAME) != 0) {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
		return;
	}

	if (g_strcmp0 (method_name, "GetEntryProperties") == 0) {
		RBStringValueMap *properties;
		GHashTable *prop_hash;
		GHashTableIter iter;
		GVariantBuilder *builder;
		gpointer name_ptr, value_ptr;
		int count = 0;

		g_variant_get (parameters, "(&s)", &uri);

		entry = rhythmdb_entry_lookup_by_location (db, uri);
		if (entry == NULL) {
			return_entry_not_found (invocation, uri);
			return;
		}

		properties = rhythmdb_entry_gather_metadata (db, entry);
		prop_hash = rb_string_value_map_steal_hashtable (properties);
		g_object_unref (properties);

		g_hash_table_iter_init (&iter, prop_hash);
		builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
		while (g_hash_table_iter_next (&iter, &name_ptr, &value_ptr)) {
			GValue *value;
			GVariant *v;

			value = value_ptr;
			v = NULL;
			if (G_VALUE_HOLDS_STRING (value)) {
				if (g_value_get_string (value) != NULL) {
					v = g_variant_new_string (g_value_get_string (value));
				}
			} else if (G_VALUE_HOLDS_ULONG (value)) {
				v = g_variant_new_uint32 (g_value_get_ulong (value));
			} else if (G_VALUE_HOLDS_DOUBLE (value)) {
				v = g_variant_new_double (g_value_get_double (value));
			} else if (G_VALUE_HOLDS_BOOLEAN (value)) {
				v = g_variant_new_boolean (g_value_get_boolean (value));
			} else if (G_VALUE_HOLDS_UINT64 (value)) {
				v = g_variant_new_uint64 (g_value_get_uint64 (value));
			} else {
				g_assert_not_reached ();
			}
			if (v != NULL) {
				g_variant_builder_add (builder,
						       "{sv}",
						       (const char *)name_ptr,
						       v);
			}
			count++;
		}
		g_hash_table_destroy (prop_hash);

		/* make sure there's at least one entry in the dict */
		if (count == 0) {
			g_variant_builder_add (builder, "{sv}", "", g_variant_new_string (""));
		}

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(a{sv})", builder));
		g_variant_builder_unref (builder);

	} else if (g_strcmp0 (method_name, "SetEntryProperties") == 0) {
		GVariant *properties;
		GVariantIter iter;
		const char *name;
		GVariant *value;

		g_variant_get (parameters, "(&s@a{sv})", &uri, &properties);

		entry = rhythmdb_entry_lookup_by_location (db, uri);
		if (entry == NULL) {
			return_entry_not_found (invocation, uri);
			return;
		}

		/* set properties on entry, commit, etc. */
		g_variant_iter_init (&iter, properties);
		while (g_variant_iter_loop (&iter, "{&sv}", &name, &value)) {
			RhythmDBPropType propid;
			GValue v = {0,};

			propid = rhythmdb_propid_from_nice_elt_name (db, (xmlChar *)name);
			if (propid == -1) {
				/* can't really bail out and return an error at this point */
				g_warning ("RhythmDB property %s doesn't exist", name);
				continue;
			}

			if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)) {
				g_value_init (&v, G_TYPE_STRING);
				g_value_set_string (&v, g_variant_get_string (value, NULL));
			} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32)) {
				g_value_init (&v, G_TYPE_ULONG);
				g_value_set_ulong (&v, g_variant_get_uint32 (value));
			} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE)) {
				g_value_init (&v, G_TYPE_DOUBLE);
				g_value_set_double (&v, g_variant_get_double (value));
			} else {
				/* again, can't bail out */
				g_warning ("Can't convert variant type %s to rhythmdb property",
					   g_variant_get_type_string (value));
				continue;
			}

			rhythmdb_entry_set (db, entry, propid, &v);
			g_value_unset (&v);
		}

		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static GDBusInterfaceVTable rhythmdb_interface_vtable = {
	(GDBusInterfaceMethodCallFunc) rhythmdb_method_call,
	NULL,
	NULL
};

void
rhythmdb_dbus_register (RhythmDB *db)
{
	GDBusConnection *bus;
	GDBusInterfaceInfo *ifaceinfo;
	GDBusNodeInfo *nodeinfo;
	GError *error = NULL;

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus == NULL) {
		return;
	}

	nodeinfo = g_dbus_node_info_new_for_xml (rhythmdb_dbus_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to read RhythmDB D-Bus interface spec: %s", error->message);
		return;
	}

	ifaceinfo = g_dbus_node_info_lookup_interface (nodeinfo, RHYTHMDB_INTERFACE_NAME);
	db->priv->dbus_object_id = g_dbus_connection_register_object (bus,
								      RHYTHMDB_OBJECT_PATH,
								      ifaceinfo,
								      &rhythmdb_interface_vtable,
								      db,
								      NULL,
								      NULL);
}

void
rhythmdb_dbus_unregister (RhythmDB *db)
{
	GDBusConnection *bus;

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus == NULL) {
		return;
	}

	if (db->priv->dbus_object_id) {
		g_dbus_connection_unregister_object (bus,
						     db->priv->dbus_object_id);
		db->priv->dbus_object_id = 0;
	}
}
