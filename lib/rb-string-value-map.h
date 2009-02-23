/*
 *  Copyright (C) 2007 James Livingston <doclivingston@gmail.com>
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

#ifndef RB_STRING_VALUE_MAP_H
#define RB_STRING_VALUE_MAP_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_STRING_VALUE_MAP            (rb_string_value_map_get_type ())
#define RB_STRING_VALUE_MAP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), RB_TYPE_STRING_VALUE_MAP, RBStringValueMap))
#define RB_STRING_VALUE_MAP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), RB_TYPE_STRING_VALUE_MAP, RBStringValueMapClass))
#define RB_IS_STRING_VALUE_MAP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), RB_TYPE_STRING_VALUE_MAP))
#define RB_IS_STRING_VALUE_MAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), RB_TYPE_STRING_VALUE_MAP))

typedef struct _RBStringValueMap RBStringValueMap;
typedef struct _RBStringValueMapClass RBStringValueMapClass;

typedef struct RBStringValueMapPrivate RBStringValueMapPrivate;

struct _RBStringValueMap
{
	GObject parent;

	RBStringValueMapPrivate *priv;
};

struct _RBStringValueMapClass
{
	GObjectClass parent;
};

GType    rb_string_value_map_get_type (void);

RBStringValueMap  *rb_string_value_map_new      (void);

void rb_string_value_map_set (RBStringValueMap *map, const char *key, const GValue *value);
gboolean rb_string_value_map_get (RBStringValueMap *map, const char *key, GValue *out);
const GValue* rb_string_value_map_peek (RBStringValueMap *map, const char *key);

gboolean rb_string_value_map_remove (RBStringValueMap *map, const char *key);
guint rb_string_value_map_size (RBStringValueMap *map);

GHashTable* rb_string_value_map_steal_hashtable (RBStringValueMap *map);

G_END_DECLS

#endif /* RB_STRING_VALUE_MAP_H */
