/*
 *  arch-tag: Implementation of common metadata functions
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <config.h>

#include "rb-metadata.h"
#include "rb-debug.h"

GType
rb_metadata_get_field_type (RBMetaData *md, RBMetaDataField field)
{
	GHashTable *map = g_object_get_data (G_OBJECT (md),
					     "rb-metadata-field-type-map");
	
	if (!map) {
		map = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_TITLE),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ARTIST),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ALBUM),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_DATE),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_GENRE),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_COMMENT),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_TRACK_NUMBER),
				     GINT_TO_POINTER (G_TYPE_INT));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_MAX_TRACK_NUMBER),
				     GINT_TO_POINTER (G_TYPE_INT));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_DESCRIPTION),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_VERSION),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ISRC),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ORGANIZATION),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_COPYRIGHT),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_CONTACT),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_LICENSE),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_PERFORMER),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION),
				     GINT_TO_POINTER (G_TYPE_LONG));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_CODEC),
				     GINT_TO_POINTER (G_TYPE_STRING));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_BITRATE),
				     GINT_TO_POINTER (G_TYPE_INT));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_TRACK_GAIN),
				     GINT_TO_POINTER (G_TYPE_DOUBLE));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_TRACK_PEAK),
				     GINT_TO_POINTER (G_TYPE_DOUBLE));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ALBUM_GAIN),
				     GINT_TO_POINTER (G_TYPE_DOUBLE));
		g_hash_table_insert (map, GINT_TO_POINTER (RB_METADATA_FIELD_ALBUM_PEAK),
				     GINT_TO_POINTER (G_TYPE_DOUBLE));
		g_object_set_data_full (G_OBJECT (md), "rb-metadata-field-type-map",
					map, (GDestroyNotify) g_hash_table_destroy);
	}
	return GPOINTER_TO_INT (g_hash_table_lookup (map, GINT_TO_POINTER (field)));
}

GQuark
rb_metadata_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_metadata_error");

	return quark;
}

