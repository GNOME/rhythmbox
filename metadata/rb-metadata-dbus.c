/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

/*
 * Common junk for the out-of-process metadata reader.
 */

#include <config.h>
#include <glib.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"

static gboolean
_get_basic_checked (DBusMessageIter *iter, gpointer value, int type)
{
	if (dbus_message_iter_get_arg_type (iter) != type) {
		rb_debug ("Expected D-BUS type '%c', got '%c'",
			  type, dbus_message_iter_get_arg_type (iter));
		return FALSE;
	}
	dbus_message_iter_get_basic (iter, value);
	dbus_message_iter_next (iter);
	return TRUE;
}

gboolean 
rb_metadata_dbus_get_boolean (DBusMessageIter *iter, gboolean *value)
{
	return _get_basic_checked (iter, value, DBUS_TYPE_BOOLEAN);
}

gboolean
rb_metadata_dbus_get_uint32 (DBusMessageIter *iter, guint32 *value)
{
	return _get_basic_checked (iter, value, DBUS_TYPE_UINT32);
}


gboolean 
rb_metadata_dbus_get_string (DBusMessageIter *iter, gchar **value)
{
	gchar *msg_value;
	if (!_get_basic_checked (iter, &msg_value, DBUS_TYPE_STRING))
		return FALSE;
	*value = g_strdup (msg_value);
	return TRUE;
}

gboolean 
rb_metadata_dbus_add_to_message (RBMetaData *md, DBusMessageIter *iter)
{
	DBusMessageIter a_iter;
	RBMetaDataField field;
	const char *etype = 
		DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING 
			DBUS_TYPE_UINT32_AS_STRING DBUS_TYPE_VARIANT_AS_STRING 
		DBUS_DICT_ENTRY_END_CHAR_AS_STRING;
	rb_debug ("opening container type %s", etype);
	if (!dbus_message_iter_open_container (iter, DBUS_TYPE_ARRAY, etype, &a_iter)) {
		return FALSE;
	}

	for (field = RB_METADATA_FIELD_TITLE; field < RB_METADATA_FIELD_LAST; field++) {
		GType vtype = rb_metadata_get_field_type (field);
		GValue v = {0,};
		DBusMessageIter d_iter;
		DBusMessageIter v_iter;
		const char *v_sig = NULL;
		
		if (!rb_metadata_get (md, field, &v))
			continue;

		if (!dbus_message_iter_open_container (&a_iter, DBUS_TYPE_DICT_ENTRY, NULL, &d_iter)) {
			return FALSE;
		}

		if (!dbus_message_iter_append_basic (&d_iter, DBUS_TYPE_UINT32, &field)) {
			return FALSE;
		}

		switch (vtype) {
		case G_TYPE_ULONG:
			v_sig = DBUS_TYPE_UINT32_AS_STRING;
			break;
		case G_TYPE_DOUBLE:
			v_sig = DBUS_TYPE_DOUBLE_AS_STRING;
			break;
		case G_TYPE_STRING:
			v_sig = DBUS_TYPE_STRING_AS_STRING;
			break;
		}
		if (!dbus_message_iter_open_container (&d_iter, DBUS_TYPE_VARIANT, v_sig, &v_iter)) {
			return FALSE;
		}

		/* not exactly stolen from dbus-gvalue.c */
		switch (vtype) {
		case G_TYPE_ULONG:
			{
				dbus_uint32_t n = g_value_get_ulong (&v);
				if (!dbus_message_iter_append_basic (&v_iter, DBUS_TYPE_UINT32, &n)) {
					return FALSE;
				}
				break;
			}

		case G_TYPE_DOUBLE:
			{
				double n = g_value_get_double (&v);
				if (!dbus_message_iter_append_basic (&v_iter, DBUS_TYPE_DOUBLE, &n)) {
					return FALSE;
				}
				break;
			}

		case G_TYPE_STRING:
			{
				const char *s = g_value_get_string (&v);
				if (!s)
					s = "";
				if (!dbus_message_iter_append_basic (&v_iter, DBUS_TYPE_STRING, &s)) {
					return FALSE;
				}
				break;
			}

		default:
			g_assert_not_reached ();
			break;
		}

		g_value_unset (&v);
		if (!dbus_message_iter_close_container (&d_iter, &v_iter)) {
			return FALSE;
		}

		if (!dbus_message_iter_close_container (&a_iter, &d_iter)) {
			return FALSE;
		}
	}
	
	if (!dbus_message_iter_close_container (iter, &a_iter)) {
		return FALSE;
	}

	return TRUE;
}

gboolean 
rb_metadata_dbus_read_from_message (RBMetaData *md, GHashTable *metadata, DBusMessageIter *iter)
{
	DBusMessageIter a_iter;
	int current_type;
	
	if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_ARRAY) {
		rb_debug ("Expected D-BUS array, got type '%c'",
			  dbus_message_iter_get_arg_type (iter));
		return FALSE;
	}

	dbus_message_iter_recurse (iter, &a_iter);
	
	current_type = dbus_message_iter_get_arg_type (&a_iter);
	if (current_type != DBUS_TYPE_INVALID && current_type != DBUS_TYPE_DICT_ENTRY) {
		rb_debug ("Expected D-BUS dict entry, got type '%c'", (guchar) current_type);
		return FALSE;
	}

	while (current_type != DBUS_TYPE_INVALID) {
		DBusMessageIter e_iter;
		DBusMessageIter v_iter;
		RBMetaDataField field;
		GValue *val;

		dbus_message_iter_recurse (&a_iter, &e_iter);

		if (!rb_metadata_dbus_get_uint32 (&e_iter, &field)) {
			return FALSE;
		}

		if (dbus_message_iter_get_arg_type (&e_iter) != DBUS_TYPE_VARIANT) {
			rb_debug ("Expected D-BUS variant type for value; got type '%c'",
				  dbus_message_iter_get_arg_type (&e_iter));
			return FALSE;
		}

		dbus_message_iter_recurse (&e_iter, &v_iter);
		val = g_new0 (GValue, 1);
		switch (dbus_message_iter_get_arg_type (&v_iter)) {
		case DBUS_TYPE_UINT32:
			{
				dbus_uint32_t n;
				dbus_message_iter_get_basic (&v_iter, &n);
				g_value_init (val, G_TYPE_ULONG);
				g_value_set_ulong (val, n);
				break;
			}

		case DBUS_TYPE_DOUBLE:
			{
				double n;
				dbus_message_iter_get_basic (&v_iter, &n);
				g_value_init (val, G_TYPE_DOUBLE);
				g_value_set_double (val, n);
				break;
			}

		case DBUS_TYPE_STRING:
			{
				const gchar *n;
				dbus_message_iter_get_basic (&v_iter, &n);
				g_value_init (val, G_TYPE_STRING);
				g_value_take_string (val, g_strdup (n));
				break;
			}

		default:
			g_assert_not_reached ();
			break;
		}

		g_hash_table_insert (metadata, GINT_TO_POINTER (field), val);
		
		dbus_message_iter_next (&a_iter);
		current_type = dbus_message_iter_get_arg_type (&a_iter);
	}

	return TRUE;
}

