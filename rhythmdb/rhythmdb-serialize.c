/* 
 *  arch-tag: Implementation of RhythmDB support for (de)serialization
 *
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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

#include "rhythmdb-serialize.h"
#include <string.h>
#include "rb-string-helpers.h"
#include "rb-debug.h"

inline const char *
rhythmdb_elt_name_from_propid (gint propid)
{
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		return "type";
	case RHYTHMDB_PROP_TITLE:
		return "title";
	case RHYTHMDB_PROP_TITLE_FOLDED:
		return "title-folded";
	case RHYTHMDB_PROP_GENRE:
		return "genre";
	case RHYTHMDB_PROP_GENRE_FOLDED:
		return "genre-folded";
	case RHYTHMDB_PROP_ARTIST:
		return "artist";
	case RHYTHMDB_PROP_ARTIST_FOLDED:
		return "artist-folded";
	case RHYTHMDB_PROP_ALBUM:
		return "album";
	case RHYTHMDB_PROP_ALBUM_FOLDED:
		return "album-folded";
	case RHYTHMDB_PROP_TRACK_NUMBER:
		return "track-number";
	case RHYTHMDB_PROP_DURATION:
		return "duration";
	case RHYTHMDB_PROP_FILE_SIZE:
		return "file-size";
	case RHYTHMDB_PROP_LOCATION:
		return "location";
	case RHYTHMDB_PROP_MTIME:
		return "mtime";
	case RHYTHMDB_PROP_RATING:
		return "rating";
	case RHYTHMDB_PROP_PLAY_COUNT:
		return "play-count";
	case RHYTHMDB_PROP_LAST_PLAYED:
		return "last-played";
	case RHYTHMDB_PROP_QUALITY:
		return "quality";
	default:
		g_assert_not_reached ();
	}
	return NULL;
}

static void
write_encoded_gvalue (xmlNodePtr node,
		      GValue *val)
{
	char *strval;
	char *quoted;

	switch (G_VALUE_TYPE (val))
	{
	case G_TYPE_STRING:
		strval = g_value_dup_string (val);
		break;
	case G_TYPE_BOOLEAN:
		strval = g_strdup_printf ("%d", g_value_get_boolean (val));
		break;
	case G_TYPE_INT:
		strval = g_strdup_printf ("%d", g_value_get_int (val));
		break;
	case G_TYPE_LONG:
		strval = g_strdup_printf ("%ld", g_value_get_long (val));
		break;
	case G_TYPE_FLOAT:
		strval = g_strdup_printf ("%f", g_value_get_float (val));
		break;
	case G_TYPE_DOUBLE:
		strval = g_strdup_printf ("%f", g_value_get_double (val));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	quoted = xmlEncodeEntitiesReentrant (NULL, strval);
	g_free (strval);
	xmlNodeSetContent (node, quoted);
	g_free (quoted);
}

void
rhythmdb_query_serialize (RhythmDB *db, GPtrArray *query,
			  xmlNodePtr parent)
{
	guint i;
	xmlNodePtr node = xmlNewChild (parent, NULL, "conjunction", NULL);
	xmlNodePtr subnode;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		
		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
			subnode = xmlNewChild (node, NULL, "subquery", NULL);
			rhythmdb_query_serialize (db, data->subquery, subnode);
			break;
		case RHYTHMDB_QUERY_PROP_LIKE:
			subnode = xmlNewChild (node, NULL, "like", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_elt_name_from_propid (data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
			subnode = xmlNewChild (node, NULL, "equals", NULL);
			xmlSetProp (subnode, "prop", rhythmdb_elt_name_from_propid (data->propid));
			write_encoded_gvalue (subnode, data->val);
			break;
		case RHYTHMDB_QUERY_DISJUNCTION:
			subnode = xmlNewChild (node, NULL, "disjunction", NULL);
			break;
		case RHYTHMDB_QUERY_END:
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
			g_assert_not_reached ();
			break;
		}		
	}
}

GPtrArray *
rhythmdb_query_deserialize (RhythmDB *db, xmlNodePtr node)
{
	GPtrArray *query = rhythmdb_query_parse (db,
						 RHYTHMDB_QUERY_PROP_EQUALS,
						 RHYTHMDB_PROP_TYPE,
						 RHYTHMDB_ENTRY_TYPE_SONG,
						 RHYTHMDB_QUERY_END);
	return query;
}
