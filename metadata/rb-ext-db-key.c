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
 * SECTION:rbextdbkey
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
	GPtrArray *values;
	gboolean match_null;
} RBExtDBField;

struct _RBExtDBKey
{
	gboolean lookup;
	RBExtDBField *multi_field;
	GList *fields;
	GList *info;
};

static void
rb_ext_db_field_free (RBExtDBField *field)
{
	g_free (field->name);
	g_ptr_array_free (field->values, TRUE);
	g_slice_free (RBExtDBField, field);
}

static RBExtDBField *
rb_ext_db_field_copy (RBExtDBField *field)
{
	RBExtDBField *copy;
	int i;

	copy = g_slice_new0 (RBExtDBField);
	copy->name = g_strdup (field->name);
	copy->values = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < field->values->len; i++) {
		g_ptr_array_add (copy->values, g_strdup (g_ptr_array_index (field->values, i)));
	}
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
	copy->lookup = key->lookup;
	copy->multi_field = key->multi_field;
	for (l = key->fields; l != NULL; l = l->next) {
		copy->fields = g_list_append (copy->fields, rb_ext_db_field_copy (l->data));
	}
	for (l = key->info; l != NULL; l = l->next) {
		copy->info = g_list_append (copy->info, rb_ext_db_field_copy (l->data));
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
	g_list_free_full (key->info, (GDestroyNotify) rb_ext_db_field_free);
	g_slice_free (RBExtDBKey, key);
}

static RBExtDBKey *
do_create (const char *field, const char *value, gboolean lookup)
{
	RBExtDBKey *key;
	key = g_slice_new0 (RBExtDBKey);
	key->lookup = lookup;
	rb_ext_db_key_add_field (key, field, value);
	return key;
}

/**
 * rb_ext_db_key_create_lookup:
 * @field: required field name
 * @value: value for field
 *
 * Creates a new metadata lookup key with a single field.
 * Use @rb_ext_db_key_add_field to add more.
 *
 * Return value: the new key
 */
RBExtDBKey *
rb_ext_db_key_create_lookup (const char *field, const char *value)
{
	return do_create (field, value, TRUE);
}

/**
 * rb_ext_db_key_create_storage:
 * @field: required field name
 * @value: value for field
 *
 * Creates a new metadata storage key with a single field.
 * Use @rb_ext_db_key_add_field to add more.
 *
 * Return value: the new key
 */
RBExtDBKey *
rb_ext_db_key_create_storage (const char *field, const char *value)
{
	return do_create (field, value, FALSE);
}

/**
 * rb_ext_db_key_is_lookup:
 * @key: a #RBExtDBKey
 *
 * Returns %TRUE if the key is a lookup key
 *
 * Return value: whether the key is a lookup key
 */
gboolean
rb_ext_db_key_is_lookup (RBExtDBKey *key)
{
	return key->lookup;
}

static void
add_to_list (GList **list, RBExtDBField **multi, const char *name, const char *value)
{
	RBExtDBField *f;
	GList *l;
	int i;

	for (l = *list; l != NULL; l = l->next) {
		f = l->data;
		if (strcmp (f->name, name) == 0) {
			g_assert (multi != NULL);

			if (value != NULL) {
				for (i = 0; i < f->values->len; i++) {
					if (strcmp (g_ptr_array_index (f->values, i), value) == 0) {
						/* duplicate value */
						return;
					}
				}
				g_assert (*multi == NULL || *multi == f);
				g_ptr_array_add (f->values, g_strdup (value));
				*multi = f;
			} else {
				g_assert (*multi == NULL || *multi == f);
				f->match_null = TRUE;
				*multi = f;
			}
			return;
		}
	}

	f = g_slice_new0 (RBExtDBField);
	f->name = g_strdup (name);
	f->values = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (f->values, g_strdup (value));
	*list = g_list_append (*list, f);
}

static char **
get_list_names (GList *list)
{
	char **names;
	GList *l;
	int i;

	names = g_new0 (char *, g_list_length (list) + 1);
	i = 0;
	for (l = list; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		names[i++] = g_strdup (f->name);
	}

	return names;
}

static GPtrArray *
get_list_values (GList *list, const char *field)
{
	RBExtDBField *f;
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		f = l->data;
		if (strcmp (f->name, field) == 0) {
			return f->values;
		}
	}

	return NULL;
}


/**
 * rb_ext_db_key_add_field:
 * @key: a #RBExtDBKey
 * @field: name of the field to add
 * @value: field value
 *
 * Adds a field to the key, or an additional value to an existing field.
 */
void
rb_ext_db_key_add_field (RBExtDBKey *key,
			 const char *field,
			 const char *value)
{
	add_to_list (&key->fields, &key->multi_field, field, value);
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
	return get_list_names (key->fields);
}

/**
 * rb_ext_db_key_get_field:
 * @key: a #RBExtDBKey
 * @field: field to retrieve
 *
 * Extracts the value for a single-valued field.
 *
 * Return value: field value, or NULL
 */
const char *
rb_ext_db_key_get_field (RBExtDBKey *key, const char *field)
{
	GPtrArray *v = get_list_values (key->fields, field);
	if (v != NULL && v->len > 0) {
		return g_ptr_array_index (v, 0);
	} else {
		return NULL;
	}
}


/**
 * rb_ext_db_key_get_field_values:
 * @key: a #RBExtDBKey
 * @field: field to retrieve
 *
 * Extracts the values for the specified field.
 *
 * Return value: (transfer full): field values, or NULL
 */
char **
rb_ext_db_key_get_field_values (RBExtDBKey *key, const char *field)
{
	GPtrArray *v = get_list_values (key->fields, field);
	char **strv;
	int i;

	if (v == NULL) {
		return NULL;
	}

	strv = g_new0 (char *, v->len + 1);
	for (i = 0; i < v->len; i++) {
		strv[i] = g_strdup (g_ptr_array_index (v, i));
	}
	return strv;
}

/**
 * rb_ext_db_key_add_info:
 * @key: a #RBExtDBKey
 * @name: name of the field to add
 * @value: field value
 *
 * Adds an information field to the key.
 */
void
rb_ext_db_key_add_info (RBExtDBKey *key,
			const char *name,
			const char *value)
{
	add_to_list (&key->info, NULL, name, value);
}

/**
 * rb_ext_db_key_get_info_names:
 * @key: a #RBExtDBKey
 *
 * Returns a NULL-terminated array containing the names of the info
 * fields * present in the key.
 *
 * Return value: (transfer full): array of info field names
 */
char **
rb_ext_db_key_get_info_names (RBExtDBKey *key)
{
	return get_list_names (key->info);
}

/**
 * rb_ext_db_key_get_info:
 * @key: a #RBExtDBKey
 * @name: info field to retrieve
 *
 * Extracts the value for the specified info field.
 *
 * Return value: field value, or NULL
 */
const char *
rb_ext_db_key_get_info (RBExtDBKey *key, const char *name)
{
	GPtrArray *v = get_list_values (key->info, name);
	if (v != NULL && v->len > 0) {
		return g_ptr_array_index (v, 0);
	} else {
		return NULL;
	}
}




static gboolean
match_field (RBExtDBKey *key, RBExtDBField *field)
{
	GPtrArray *values;
	int i;
	int j;

	values = get_list_values (key->fields, field->name);
	if (values == NULL) {
		return field->match_null;
	}

	for (i = 0; i < field->values->len; i++) {
		const char *a = g_ptr_array_index (field->values, i);
		for (j = 0; j < values->len; j++) {
			const char *b = g_ptr_array_index (values, j);
			if (strcmp (a, b) == 0)
				return TRUE;
		}
	}
	return FALSE;
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

/**
 * rb_ext_db_key_field_matches:
 * @key: an #RBExtDBKey
 * @field: a field to check
 * @value: a value to match against
 *
 * Checks whether a specified field in @key matches a value.
 * This can be used to match keys against other types of data.
 * To match keys against each other, use @rb_ext_db_key_matches.
 *
 * Return value: %TRUE if the field matches the value
 */
gboolean
rb_ext_db_key_field_matches (RBExtDBKey *key, const char *field, const char *value)
{
	GPtrArray *v;
	int i;

	v = get_list_values (key->fields, field);
	if (v == NULL) {
		/* if the key doesn't have this field, anything matches */
		return TRUE;
	}

	if (value == NULL) {
		if (key->multi_field == NULL) {
			/* no multi field, so null can't match */
			return FALSE;
		}

		if (g_strcmp0 (field, key->multi_field->name) == 0) {
			/* this is the multi field, so null might match */
			return key->multi_field->match_null;
		} else {
			/* this isn't the multi field, null can't match */
			return FALSE;
		}
	}

	for (i = 0; i < v->len; i++) {
		if (strcmp (g_ptr_array_index (v, i), value) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * rb_ext_db_key_is_null_match:
 * @lookup: lookup key to compare against
 * @store: store key to compare
 *
 * Checks whether @store has a null value in an optional
 * field of @lookup, which means it's not the most specific
 * possible match for a query.
 *
 * Return value: %TRUE if the storage key has a null value for the lookup key's optional field
 */
gboolean
rb_ext_db_key_is_null_match (RBExtDBKey *lookup, RBExtDBKey *store)
{
	GPtrArray *values;

	if (lookup->multi_field == NULL)
		return FALSE;
	if (lookup->multi_field->match_null == FALSE)
		return FALSE;

	values = get_list_values (store->fields, lookup->multi_field->name);
	return (values == NULL || values->len == 0);
}

static void
flatten_store_key (RBExtDBKey *key, TDB_DATA *data)
{
	GByteArray *k;
	GList *l;
	guint8 nul = '\0';

	g_assert (key->lookup == FALSE);

	k = g_byte_array_sized_new (512);
	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		const char *value;

		value = g_ptr_array_index (f->values, 0);
		g_byte_array_append (k, (guint8 *)f->name, strlen (f->name));
		g_byte_array_append (k, &nul, 1);
		g_byte_array_append (k, (guint8 *)value, strlen (value));
		g_byte_array_append (k, &nul, 1);
	}

	data->dsize = k->len;
	data->dptr = g_byte_array_free (k, FALSE);
}

static RBExtDBKey *
create_store_key (RBExtDBKey *key, int option)
{
	RBExtDBKey *skey = NULL;
	GList *l;
	int nopt;

	g_assert (key->lookup);
	if (key->multi_field == NULL) {
		nopt = 1;
	} else if (key->multi_field->match_null) {
		nopt = key->multi_field->values->len + 1;
	} else {
		nopt = key->multi_field->values->len;
	}
	if (option >= nopt)
		return NULL;

	for (l = key->fields; l != NULL; l = l->next) {
		RBExtDBField *f = l->data;
		const char *value;

		if (f != key->multi_field) {
			value = g_ptr_array_index (f->values, 0);
		} else if (option < f->values->len) {
			value = g_ptr_array_index (f->values, option);
		} else {
			continue;
		}
		if (skey == NULL)
			skey = rb_ext_db_key_create_storage (f->name, value);
		else
			rb_ext_db_key_add_field (skey, f->name, value);
	}

	return skey;
}

/**
 * rb_ext_db_key_lookups:
 * @key: a #RBExtDBKey
 * @callback: (scope call): a callback to process lookup keys
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
	int i = 0;
	while (TRUE) {
		RBExtDBKey *s;
		TDB_DATA sk;
		gboolean result;

		s = create_store_key (key, i);
		if (s == NULL)
			break;

		flatten_store_key (s, &sk);
		result = callback (sk, s, user_data);
		g_free (sk.dptr);
		rb_ext_db_key_free (s);

		if (result == FALSE)
			break;

		i++;
	}
}

/**
 * rb_ext_db_key_to_store_key: (skip)
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
	RBExtDBKey *sk;

	if (key->lookup) {
		sk = create_store_key (key, 0);
		if (sk != NULL) {
			flatten_store_key (sk, &k);
			rb_ext_db_key_free (sk);
		}
	} else {
		flatten_store_key (key, &k);
	}

	return k;
}

static void
append_field (GString *s, RBExtDBField *f)
{
	int i;

	g_string_append_printf (s, " %s%s{", f->name, f->match_null ? "~" : "=");
	for (i = 0; i < f->values->len; i++) {
		if (i != 0)
			g_string_append (s, "\",\"");
		else
			g_string_append (s, "\"");
		g_string_append (s, g_ptr_array_index (f->values, i));
	}
	if (i != 0)
		g_string_append (s, "\"}");
	else
		g_string_append (s, "}");
}

/**
 * rb_ext_db_key_to_string:
 * @key: a @RBExtDBKey
 *
 * Generates a readable string format from the key.
 *
 * Return value: (transfer full): string form of the key
 */
char *
rb_ext_db_key_to_string (RBExtDBKey *key)
{
	GString *s;
	GList *l;

	s = g_string_sized_new (100);
	g_string_append (s, key->lookup ? "[lookup]" : "[storage]");
	for (l = key->fields; l != NULL; l = l->next) {
		append_field (s, l->data);
	}

	if (key->lookup && key->info != NULL) {
		g_string_append (s, " info: ");
		for (l = key->info; l != NULL; l = l->next) {
			append_field (s, l->data);
		}
	}

	return g_string_free (s, FALSE);
}
