/* 
 *  arch-tag: Implementation of RhythmDB support for legacy databases
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include "rhythmdb-legacy.h"
#include <string.h>
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-debug.h"


RhythmDBEntry *
rhythmdb_legacy_parse_rbnode (RhythmDB *db, RhythmDBEntryType type,
			      xmlNodePtr node, guint *id)
{
	xmlNodePtr node_child;
	RhythmDBEntry *entry;
	char *location = NULL;
	char *title = NULL;
	char *genre = NULL;
	char *artist = NULL;
	char *album = NULL;
	guint rating = 0, play_count = 0;
	gint track_number = -1;
	gint quality = -1;
	glong duration = 0;
	glong last_played = 0;
	glong file_size = 0;
	glong mtime = 0;
	GValue val = {0, };
	char *xml;

	xml = xmlGetProp (node, "id");
	
	if (id && xml) 
		*id = atoi (xml);
	else if (id)
		*id = 0;
	g_free (xml);

	for (node_child = node->children; node_child != NULL; node_child = node_child->next) {
		if (strcmp (node_child->name, "property") == 0) {
			guint propid;

			xml = xmlGetProp (node_child, "id");
			propid = atoi (xml);
			g_free (xml);
				
			switch (propid)
			{
			case 0: /* RB_NODE_PROP_NAME */
				title = xmlNodeGetContent (node_child);
				break;
			case 2: /* RB_NODE_PROP_GENRE */
				genre = xmlNodeGetContent (node_child);
				break;
			case 3: /* RB_NODE_PROP_ARTIST */
				artist = xmlNodeGetContent (node_child);
				break;
			case 4: /* RB_NODE_PROP_ALBUM */
				album = xmlNodeGetContent (node_child);
				break;
			case 8: /* RB_NODE_PROP_TRACK_NUMBER */
				xml = xmlNodeGetContent (node_child);
				track_number = atoi (xml);
				g_free (xml);
				break;
			case 9: /* RB_NODE_PROP_DURATION */
				xml = xmlNodeGetContent (node_child);
				duration = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 11: /* RB_NODE_PROP_FILE_SIZE */
				xml = xmlNodeGetContent (node_child);
				file_size = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 12: /* RB_NODE_PROP_LOCATION */
			{
				char *tmp;
        			tmp = xmlNodeGetContent (node_child);
				location = gnome_vfs_escape_path_string (tmp);
				g_free (tmp);
			}
				break;
			case 13: /* RB_NODE_PROP_MTIME */
				xml = xmlNodeGetContent (node_child);
				mtime = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 15: /* RB_NODE_PROP_RATING */
				xml = xmlNodeGetContent (node_child);
				rating = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 16: /* RB_NODE_PROP_PLAY_COUNT */
				xml = xmlNodeGetContent (node_child);
				play_count = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 17: /* RB_NODE_PROP_LAST_PLAYED */
				xml = xmlNodeGetContent (node_child);
				last_played = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			case 22: /* RB_NODE_PROP_QUALITY */
				xml = xmlNodeGetContent (node_child);
				quality = g_ascii_strtoull (xml, NULL, 10);
				g_free (xml);
				break;
			}
		}
	}

	if (!(location && title)) {
		entry = NULL;
		goto free_out;
	}
	
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_lookup_by_location (db, location);

	if (entry) {
		rb_debug ("location \"%s\" already exists", location);
		rhythmdb_write_unlock (db);
		goto free_out;
	}

	entry = rhythmdb_entry_new (db, type, location);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, title);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	if (genre) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string_take_ownership (&val, genre);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
		g_value_unset (&val);
	}

	if (artist) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string_take_ownership (&val, artist);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
		g_value_unset (&val);
	}

	if (album) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string_take_ownership (&val, album);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
		g_value_unset (&val);
	}

	if (track_number >= 0) {
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, track_number);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
		g_value_unset (&val);
	}

	if (quality >= 0) {
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, quality);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_QUALITY, &val);
		g_value_unset (&val);
	}

	if (duration > 0) {
		g_value_init (&val, G_TYPE_LONG);
		g_value_set_long (&val, duration);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, file_size);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, mtime);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MTIME, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, rating);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, play_count);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAY_COUNT, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, last_played);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);
	g_value_unset (&val);

	rhythmdb_entry_ref_unlocked (db, entry);

	rhythmdb_write_unlock (db);

	g_free (location);
	return entry;
free_out:
	g_free (location);
	g_free (title);
	g_free (genre);
	g_free (artist);
	g_free (album);
	return NULL;
}
