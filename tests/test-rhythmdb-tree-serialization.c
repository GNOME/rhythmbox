/* 
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 */

#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-thread-helpers.h"

static RhythmDBEntry *
create_entry (RhythmDB *db, const char *name, const char *album,
	      const char *artist, const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = {0, };

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///yay.ogg");
	g_assert (entry);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, genre);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, artist);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, album);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, name);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	return entry;
}

static void
assert_xml_file_has_entry (const char *entry)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	
	doc = xmlParseFile ("test.xml");
	g_assert (doc != NULL);

	root = xmlDocGetRootElement (doc);
	
	child = root->children;
	for (; child != NULL; child = child->next) {
		xmlNodePtr sub_child;

		if (child->type != XML_ELEMENT_NODE)
			continue;
		
		for (sub_child = child->children; sub_child; sub_child = sub_child->next) {
			if (sub_child->type != XML_ELEMENT_NODE)
				continue;

			if (!strcmp (sub_child->name, "title")) {
				if (!strcmp (sub_child->children->content,
					     entry))
					goto out;
			}
		}
	}
	
	g_assert_not_reached ();
	out:
	xmlFreeDoc (doc);
}


int
main (int argc, char **argv)
{
	RhythmDB *db;
	RhythmDBEntry *entry;
	xmlDocPtr doc;

	gtk_init (&argc, &argv);
	rb_thread_helpers_init ();
	rb_debug_init (TRUE);

	db = rhythmdb_tree_new ("test.xml");

	/*
	 *  TEST 1: Save with no entries
	 */
	g_print ("Test 1\n");
	rhythmdb_read_lock (db);

	rhythmdb_save (db);

	doc = xmlParseFile ("test.xml");
	g_assert (doc != NULL);
	xmlFreeDoc (doc);

	rhythmdb_read_unlock (db);
	g_print ("Test 1: PASS\n");

	/*
	 *  TEST 2: Save with a single entry
	 */
	g_print ("Test 1\n");
	rhythmdb_write_lock (db);

	entry = create_entry (db, "Sin", "Pretty Hate Machine", "Nine Inch Nails", "Rock");

	rhythmdb_save (db);

	assert_xml_file_has_entry ("Sin");

	rhythmdb_write_unlock (db);

	/*
	 * THE END
	 */
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	
	exit (0);
}
