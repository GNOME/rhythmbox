/* 
 *  arch-tag: Implementation of RhythmDB support for legacy databases
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

#include "rhythmdb-legacy.h"
#include <string.h>
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-debug.h"


RhythmDBEntry *
rhythmdb_legacy_parse_rbnode (RhythmDB *db, RhythmDBEntryType type,
			      xmlNodePtr node)
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
	GValue val = {0, };

	for (node_child = node->children; node_child != NULL; node_child = node_child->next) {
		if (strcmp (node_child->name, "property") == 0) {
			char *xml = xmlGetProp (node_child, "id");
			guint propid;

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
			case 12: /* RB_NODE_PROP_LOCATION */
				location = xmlNodeGetContent (node_child);
				break;
			case 15: /* RB_NODE_PROP_RATING */
				xml = xmlNodeGetContent (node_child);
				rating = atoi (xml);
				g_free (xml);
				break;
			case 16: /* RB_NODE_PROP_PLAY_COUNT */
				xml = xmlNodeGetContent (node_child);
				play_count = atoi (xml);
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

	if (track_number >= 0) {
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, track_number);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
		g_value_unset (&val);
	}
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, title);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_reset (&val);
	if (genre) {
		g_value_set_string_take_ownership (&val, genre);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	}
	if (artist) {
		g_value_set_string_take_ownership (&val, artist);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	}
	if (album) {
		g_value_set_string_take_ownership (&val, album);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	}

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
