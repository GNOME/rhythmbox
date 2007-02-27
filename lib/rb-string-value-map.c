/*
 *  Copyright (C) 2007 James Livingston <doclivingston@gmail.com>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include "rb-string-value-map.h"

#include "rb-util.h"

static void rb_string_value_map_finalize (GObject *obj);


struct RBStringValueMapPrivate {
	GHashTable *map;
};

G_DEFINE_TYPE (RBStringValueMap, rb_string_value_map, G_TYPE_OBJECT)


static void
rb_string_value_map_class_init (RBStringValueMapClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_string_value_map_finalize;

	g_type_class_add_private (klass, sizeof (RBStringValueMapPrivate));
}

static void
rb_string_value_map_init (RBStringValueMap *map)
{
	map->priv = G_TYPE_INSTANCE_GET_PRIVATE (map, RB_TYPE_STRING_VALUE_MAP, RBStringValueMapPrivate);

	map->priv->map = g_hash_table_new_full (g_str_hash,
						g_str_equal,
						(GDestroyNotify)g_free,
						(GDestroyNotify)rb_value_free);
}

static void
rb_string_value_map_finalize (GObject *obj)
{
	RBStringValueMap *map = RB_STRING_VALUE_MAP (obj);

	g_hash_table_destroy (map->priv->map);

	G_OBJECT_CLASS(rb_string_value_map_parent_class)->finalize (obj);
}

RBStringValueMap*
rb_string_value_map_new (void)
{
	return g_object_new (RB_TYPE_STRING_VALUE_MAP, NULL);
}


void
rb_string_value_map_set (RBStringValueMap *map,
			 const char *key,
			 const GValue *value)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_VALUE_TYPE (value));
	g_value_copy (value, val);
	g_hash_table_insert (map->priv->map, g_strdup(key), val);
}

gboolean
rb_string_value_map_remove (RBStringValueMap *map,
			    const char *key)
{
	return g_hash_table_remove (map->priv->map, key);
}

guint
rb_string_value_map_size (RBStringValueMap *map)
{
	return g_hash_table_size (map->priv->map);
}

gboolean
rb_string_value_map_get (RBStringValueMap *map,
			 const char *key,
			 GValue *out)
{
	GValue *val;

	val = g_hash_table_lookup (map->priv->map, key);

	if (val == NULL)
		return FALSE;

	g_value_init (out, G_VALUE_TYPE (val));
	g_value_copy (val, out);
	return TRUE;
}

const GValue*
rb_string_value_map_peek (RBStringValueMap *map,
			  const char *key)
{
	return g_hash_table_lookup (map->priv->map, key);
}

GHashTable*
rb_string_value_map_steal_hashtable (RBStringValueMap *map)
{
	GHashTable *old;

	old = map->priv->map;
	map->priv->map = g_hash_table_new_full (g_str_hash,
						g_str_equal,
						(GDestroyNotify)g_free,
						(GDestroyNotify)rb_value_free);

	return old;
}
