/*
 *  Copyright (C) 2011 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_EXT_DB_KEY_H
#define RB_EXT_DB_KEY_H

#include <glib-object.h>

#include <stdbool.h> /* for tdb.h */
#include <sys/types.h>
#include <tdb.h>

#include <rhythmdb/rhythmdb-entry.h>

G_BEGIN_DECLS

typedef struct _RBExtDBKey RBExtDBKey;
struct _RBExtDBKey;

#define RB_TYPE_EXT_DB_KEY	(rb_ext_db_key_get_type ())
#define RB_EXT_DB_KEY(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_EXT_DB_KEY, RBExtDBKey))

GType			rb_ext_db_key_get_type		(void);

RBExtDBKey *		rb_ext_db_key_copy		(RBExtDBKey *key);
void			rb_ext_db_key_free		(RBExtDBKey *key);

RBExtDBKey *		rb_ext_db_key_create_lookup	(const char *field,
							 const char *value);
RBExtDBKey *		rb_ext_db_key_create_storage	(const char *field,
							 const char *value);
gboolean		rb_ext_db_key_is_lookup		(RBExtDBKey *key);

void			rb_ext_db_key_add_field		(RBExtDBKey *key,
							 const char *field,
							 const char *value);
char **			rb_ext_db_key_get_field_names	(RBExtDBKey *key);
const char *		rb_ext_db_key_get_field		(RBExtDBKey *key,
							 const char *field);
char **			rb_ext_db_key_get_field_values	(RBExtDBKey *key,
							 const char *field);
gboolean		rb_ext_db_key_field_matches	(RBExtDBKey *key,
							 const char *field,
							 const char *value);

void			rb_ext_db_key_add_info		(RBExtDBKey *key,
							 const char *name,
							 const char *value);
char **			rb_ext_db_key_get_info_names	(RBExtDBKey *key);
const char *		rb_ext_db_key_get_info		(RBExtDBKey *key,
							 const char *name);

gboolean		rb_ext_db_key_matches		(RBExtDBKey *a,
							 RBExtDBKey *b);
gboolean		rb_ext_db_key_is_null_match	(RBExtDBKey *lookup,
							 RBExtDBKey *store);

typedef gboolean	(*RBExtDBKeyLookupCallback)	(TDB_DATA data, RBExtDBKey *key, gpointer user_data);

void			rb_ext_db_key_lookups		(RBExtDBKey *key,
							 RBExtDBKeyLookupCallback callback,
							 gpointer user_data);

TDB_DATA		rb_ext_db_key_to_store_key	(RBExtDBKey *key);

char *			rb_ext_db_key_to_string		(RBExtDBKey *key);

G_END_DECLS

#endif /* RB_EXT_DB_KEY_H */
