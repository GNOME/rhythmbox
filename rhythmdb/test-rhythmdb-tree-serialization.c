/* 
 *  arch-tag: Serialization tests for the RhythmDB tree database
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
 */

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

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///yay.ogg");
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
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_NAME, &val);
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

			if (!strcmp (sub_child->name, "name")) {
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
	g_thread_init (NULL);
	gdk_threads_init ();
	rb_thread_helpers_init ();
	rb_debug_init (TRUE);

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	GDK_THREADS_ENTER ();

	db = rhythmdb_tree_new ("test.xml");

	/**
	 *  TEST 1: Save with no entries
	 */
	rb_debug ("Test 1");
	rhythmdb_read_lock (db);

	rhythmdb_save (db);

	doc = xmlParseFile ("test.xml");
	g_assert (doc != NULL);
	xmlFreeDoc (doc);

	rhythmdb_read_unlock (db);
	rb_debug ("Test 1: PASS");

	/**
	 *  TEST 2: Save with a single entry
	 */
	rb_debug ("Test 1");
	rhythmdb_write_lock (db);

	entry = create_entry (db, "Sin", "Pretty Hate Machine", "Nine Inch Nails", "Rock");

	rhythmdb_save (db);

	assert_xml_file_has_entry ("Sin");

	rhythmdb_write_unlock (db);

	/**
	 * THE END
	 */
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	GDK_THREADS_LEAVE ();
	
	exit (0);
}
