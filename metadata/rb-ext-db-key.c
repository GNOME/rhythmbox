/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011  Jonathan Matthew  <jonathan@d14n.org>
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

#include <string.h>

#include "rb-ext-db-key.h"

#include "rb-debug.h"

/**
 * SECTION:rb-ext-db-key
 * @short_description: key for external metadata lookups
 *
 * An external metadata key consists of one or more required fields (such as
 * the album name for album art lookups), zero or more optional fields
 * (such as the artist name), and zero or more informational fields (such as
 * the musicbrainz album ID).
 */

typedef struct
{
	char *name;
	char *value;
	RBExtDBFieldType type;
} RBExtDBField;

struct _RBExtDBKey
{
	GList *fields;
};

static void
rb_ext_db_field_free (RBExtDBField *field)
{
	g_free (field->name);
	g_free (field->value);
	g_slice_free (RBExtDBField, field);
}

static RBExtDBField *
rb_ext_db_field_copy (RBExtDBField *field)
{
	RBExtDBField *copy;
	copy = g_slice_new0 (RBExtDBField);
	copy->name = g_strdup (field->name);
	copy->value = g_strdup (field->value);
	copy->type = field->type;
	return copy;
}

GType
rb_ext_db_key_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBExtDBKey",
						     (GBoxedCopyFunc) rb_ext_db_key_copy,
						     (GBoxedFreeFunc) rb_ext_db_key_free);
	}

	return type;
}

/**
 * rb_ext_db_key_copy:
 * @key: a #RBExtDBKey
 *
 * Copies a key.
 *
 * Return value: copied key
 */
RBExtDBKey *
rb_ext_db_key_copy (RBExtDBKey *key)
{
	RBExtDBKey *copy;
	GList *l;

	copy = g_slice_new0 (RBExtDBKey);
	for (l = key->fields; l != NULL; l = l->next) {
		copy->fields = g_list_append (copy->fields, rb_ext_db_field_copy (l->data));
	}
	return copy;
}

/**
 * rb_ext_db_key_free:
 * @key: a #RBExtDBKey
 *
 * Frees a key
 */
void
rb_ext_db_key_free (RBExtDBKey *key)
{
	g_list_free_full (key->fields, (GDestroyNotify) rb_ext_db_field_free);
	g_slice_free (RBExtDBKey, key);
}

/**
 * rb_ext_db_key_create:
 * @field: required field name
 * @value: value for field
 *
 * Creates a new metadata lookup key with a single required field.
 * Use @rb_ext_db_key_add_field to add more.
 *
 * Return value: the new key
 */
RBExtDBKey *
rb_ext_db_key_create (const char *field, const char *value)
{
	RBExtDBKey *key;

	key = g_slice_new0 (RBExtDBKey);
	rb_ext_db_key_add_field (key, field, RB_EXT_DB_FIELD_REQUIRED, value);

	return key;
}

/**
 * rb_ext_db_key_add_field:
 * @key: a #RBExtDBKey
 * @field: name of the field to add
 * @field_type: field type (required, optional, or informational)
 * @value: field value
 *
 * Adds a field to the key.  Does not check that the field does not
 * already exist.
 */
void
rb_ext_db_key_add_field (RBExtDBKey *key,
			 const char *field,
			 RBExtDBFieldType field_type,
			 const char *value)
{
	RBExtDBField *f;

	f = g_slice_new0 (RBExtDBField);
	f->name = g_strdup (field);
	f->value = g_strdup (value);
	f->type = field_type;
	key->fields = g_list_append (key->fields, f);
}

/**
 * rb_ext_db_key_get_field_names:
 * @key: a #RBExtDBKey
 *
 * Returns a NULL-terminated array containing the names of the fields
 * present in the key.
 *
 * Return value: (transfer full): array of field names
 */
char **
rb_ext_db_key_get_field_names (RBExtDBKey *key)
{
	char **names;
	GList *l;
	int i;

	names = g_new0 (char *, g_list_length (key->fields) + 1);
	i = 0;
	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		names[i++] = g_strdup (f->name);
	}

	return names;
}

/**
 * rb_ext_db_key_get_field:
 * @key: a #RBExtDBKey
 * @field: field to retrieve
 *
 * Extracts the value for the specified field.
 *
 * Return value: field value, or NULL
 */
const char *
rb_ext_db_key_get_field (RBExtDBKey *key, const char *field)
{
	GList *l;

	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		if (g_strcmp0 (field, f->name) == 0) {
			return f->value;
		}
	}

	return NULL;
}

/**
 * rb_ext_db_key_get_field_type:
 * @key: a #RBExtDBKey
 * @field: field to retrieve
 *
 * Extracts the field type for the specified field.
 *
 * Return value: field type value
 */
RBExtDBFieldType
rb_ext_db_key_get_field_type (RBExtDBKey *key, const char *field)
{
	GList *l;

	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		if (g_strcmp0 (field, f->name) == 0) {
			return f->type;
		}
	}

	/* check that the field exists before calling this */
	return RB_EXT_DB_FIELD_INFORMATIONAL;
}

static gboolean
match_field (RBExtDBKey *key, RBExtDBField *field)
{
	const char *value;

	if (field->type == RB_EXT_DB_FIELD_INFORMATIONAL)
		return TRUE;

	value = rb_ext_db_key_get_field (key, field->name);
	if (value == NULL) {
		if (field->type == RB_EXT_DB_FIELD_REQUIRED)
			return FALSE;
	} else {
		if (g_strcmp0 (value, field->value) != 0)
			return FALSE;
	}

	return TRUE;
}

/**
 * rb_ext_db_key_matches:
 * @a: first #RBExtDBKey
 * @b: second #RBExtDBKey
 *
 * Checks whether the fields specified in @a match @b.
 * For keys to match, they must have the same set of required fields,
 * and the values for all must match.  Optional fields must have the
 * same values if present in both.  Informational fields are ignored.
 *
 * Return value: %TRUE if the keys match
 */
gboolean
rb_ext_db_key_matches (RBExtDBKey *a, RBExtDBKey *b)
{
	GList *l;

	for (l = a->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		if (match_field (b, f) == FALSE) {
			return FALSE;
		}
	}

	for (l = b->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		if (match_field (a, f) == FALSE) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
create_store_key (RBExtDBKey *key, guint optional_count, guint optional_fields, TDB_DATA *data)
{
	GByteArray *k;
	GList *l;
	int opt = 0;
	guint8 nul = '\0';

	k = g_byte_array_sized_new (512);
	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		switch (f->type) {
		case RB_EXT_DB_FIELD_OPTIONAL:
			/* decide if we want to include this one */
			if (optional_fields != G_MAXUINT) {
				int bit = 1 << ((optional_count-1) - opt);
				opt++;
				if ((optional_fields & bit) == 0)
					break;
			}
			/* fall through */
		case RB_EXT_DB_FIELD_REQUIRED:
			g_byte_array_append (k, (guint8 *)f->name, strlen (f->name));
			g_byte_array_append (k, &nul, 1);
			g_byte_array_append (k, (guint8 *)f->value, strlen (f->value));
			g_byte_array_append (k, &nul, 1);
			break;

		case RB_EXT_DB_FIELD_INFORMATIONAL:
			break;

		default:
			g_assert_not_reached ();
			break;
		}

	}

	data->dsize = k->len;
	data->dptr = g_byte_array_free (k, FALSE);
}

/**
 * rb_ext_db_key_lookups: (skip):
 * @key: a #RBExtDBKey
 * @callback: a callback to process lookup keys
 * @user_data: data to pass to @callback
 *
 * Generates the set of possible lookup keys for @key and
 * passes them to @callback in order.  If the callback returns
 * %FALSE, processing will stop.
 *
 * This should only be used by the metadata store itself.
 * Metadata providers and consumers shouldn't need to do this.
 */
void
rb_ext_db_key_lookups (RBExtDBKey *key,
		       RBExtDBKeyLookupCallback callback,
		       gpointer user_data)
{
	int optional_count = 0;
	int optional_keys;
	GList *l;

	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *field = l->data;
		if (field->type == RB_EXT_DB_FIELD_OPTIONAL) {
			optional_count++;
		}
	}

	for (optional_keys = (1<<optional_count)-1; optional_keys >= 0; optional_keys--) {
		TDB_DATA sk;
		gboolean result;

		create_store_key (key, optional_count, optional_keys, &sk);

		result = callback (sk, user_data);
		g_free (sk.dptr);

		if (result == FALSE)
			break;
	}
}

/**
 * rb_ext_db_key_to_store_key: (skip):
 * @key: a @RBExtDBKey
 *
 * Generates the storage key for @key.  This is the value that should
 * be used to store an item identified by this key in the store.
 * The storage key includes all optional fields, so keys passed to
 * this function should be constructed using only the optional fields
 * that were used to locate the item.  The caller must free the data
 * pointer inside @data.
 *
 * This should only be used by the metadata store itself.
 * Metadata providers and consumers shouldn't need to do this.
 *
 * Return value: TDB_DATA structure containing storage key
 */
TDB_DATA
rb_ext_db_key_to_store_key (RBExtDBKey *key)
{
	TDB_DATA k = {0,};
	/* include all optional keys */
	create_store_key (key, G_MAXUINT, G_MAXUINT, &k);
	return k;
}



#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_ext_db_field_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY(RB_EXT_DB_FIELD_REQUIRED, "required"),
			ENUM_ENTRY(RB_EXT_DB_FIELD_OPTIONAL, "optional"),
			ENUM_ENTRY(RB_EXT_DB_FIELD_INFORMATIONAL, "informational"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("RBExtDBFieldType", values);
	}

	return etype;
}
