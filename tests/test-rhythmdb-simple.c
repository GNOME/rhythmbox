/* 
 *  arch-tag: Some simple tests for the RhythmDB tree database
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

#include "config.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <stdlib.h>
#include <string.h>
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-thread-helpers.h"

int
main (int argc, char **argv)
{
	RhythmDB *db;
	RhythmDBEntry *entry, *entry1, *entry2, *entry3, *entry4;
	GValue val = {0, };
	GValue val2 = {0, };
	char *orig_sort_key;
	const char *new_sort_key;	

	gtk_init (&argc, &argv);
	g_thread_init (NULL);
	gdk_threads_init ();
	rb_thread_helpers_init ();
	rb_debug_init (TRUE);

	GDK_THREADS_ENTER ();

	db = rhythmdb_tree_new ("test");

	/**
	 *  TEST 1: Single entry creation
	 */
	g_print ("Test 1\n");
	rhythmdb_write_lock (db);

	
	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///moo.mp3");
	g_assert (entry);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///moo.mp3") == entry);

	rhythmdb_write_unlock (db);
	g_print ("Test 1: PASS\n");


	/**
	 *  TEST 2: Property setting
	 */
	g_print ("Test 2\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "foo22");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	rhythmdb_write_unlock (db);
	g_print ("Test 2: PASS\n");


	/**
	 *  TEST 3: Single property reading
	 */
	g_print ("Test 3\n");
	rhythmdb_read_lock (db);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_TITLE, &val2);
	g_assert (!strcmp ("foo22", g_value_get_string (&val2)));
	g_value_unset (&val2);

	rhythmdb_read_unlock (db);
	g_print ("Test 3: PASS\n");

	/**
	 *  TEST 4: Resetting property
	 */
	g_print ("Test 4\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "baz9");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	rhythmdb_write_unlock (db);

	rhythmdb_read_lock (db);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_TITLE, &val2);
	g_assert (!strcmp ("baz9", g_value_get_string (&val2)));
	g_value_unset (&val2);

	rhythmdb_read_unlock (db);
	g_print ("Test 4: PASS\n");

	/**
	 *  TEST 5: Entry deletion
	 */
	g_print ("Test 5\n");
	rhythmdb_write_lock (db);

	rhythmdb_entry_delete (db, entry);

	rhythmdb_write_unlock (db);
	g_print ("Test 5: PASS\n");

	/**
	 *  TEST 6: Multiple entry creation
	 */
	g_print ("Test 6\n");
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///bar.mp3");
	g_assert (entry);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry);
	entry1 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///baz.mp3");
	g_assert (entry1);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry1);
	entry2 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///whee.mp3");
	g_assert (entry2);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///whee.mp3") == entry2);

	rhythmdb_write_unlock (db);
	g_print ("Test 6: PASS\n");

	/**
	 *  TEST 7: Multiple entry deletion
	 */
	g_print ("Test 7\n");
	rhythmdb_write_lock (db);

	rhythmdb_entry_delete (db, entry2);
	rhythmdb_entry_delete (db, entry1);
	rhythmdb_entry_delete (db, entry);

	rhythmdb_write_unlock (db);
	g_print ("Test 7: PASS\n");

	/**
	 *  TEST 8: Different entry types
	 */
	g_print ("Test 8\n");
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///crack.mp3");
	g_assert (entry);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///crack.mp3") == entry);
	entry1 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION, "file:///whee.mp3");
	g_assert (entry1);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///whee.mp3") == entry1);
	entry2 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///cow.mp3");
	g_assert (entry2);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///cow.mp3") == entry2);
	entry3 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION, "file:///bar.mp3");
	g_assert (entry3);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry3);
	entry4 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION, "file:///baz.mp3");
	g_assert (entry4);
	g_assert (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry4);

	rhythmdb_write_unlock (db);
	g_print ("Test 8: PASS\n");

	/**
	 *  TEST 9: Different entry types deletion
	 */
	g_print ("Test 9\n");
	rhythmdb_write_lock (db);

	rhythmdb_entry_delete (db, entry1);
	rhythmdb_entry_delete (db, entry3);
	rhythmdb_entry_delete (db, entry2);
	rhythmdb_entry_delete (db, entry);

	rhythmdb_write_unlock (db);
	g_print ("Test 9: PASS\n");

	/**
	 *  TEST 10: More property setting
	 */
	g_print ("Test 10\n");
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///bar.mp3");
	g_assert (entry);

	g_value_init (&val, G_TYPE_DOUBLE);
	g_value_set_double (&val, 5.0);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, 32525424);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MTIME, &val);
	g_value_unset (&val);

	rhythmdb_write_unlock (db);
	g_print ("Test 10: PASS\n");

	/**
	 *  TEST 11: Last played property mirroring
	 */
	g_print ("Test 11\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, 1354285);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_LAST_PLAYED_STR, &val2);
	g_assert (strlen (g_value_get_string (&val2)) > 0);
	g_value_unset (&val2);

	rhythmdb_write_unlock (db);
	
	g_print ("Test 11: PASS\n");

	/**
	 *  TEST 12: Sort keys mirroring
	 */
	g_print ("Test 12\n");
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///test-12.mp3");
	g_assert (entry);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "foo");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_TITLE_SORT_KEY, &val2);
	orig_sort_key = g_strdup (g_value_get_string (&val2));
	g_assert (strlen (orig_sort_key) > 0);
	g_value_unset (&val2);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Rock");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_GENRE_SORT_KEY, &val2);
	g_assert (strlen (g_value_get_string (&val2)) > 0);
	g_value_unset (&val2);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Nine Inch Nails");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_ARTIST_SORT_KEY, &val2);
	g_assert (strlen (g_value_get_string (&val2)) > 0);
	g_value_unset (&val2);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Broken");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_ALBUM_SORT_KEY, &val2);
	g_assert (strlen (g_value_get_string (&val2)) > 0);
	g_value_unset (&val2);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "bar");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val2, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_TITLE_SORT_KEY, &val2);
	new_sort_key = g_value_get_string (&val2);
	g_assert (strlen (new_sort_key) > 0);
	g_assert (strcmp (new_sort_key, orig_sort_key) != 0);
	g_free (orig_sort_key);
	g_value_unset (&val2);

	rhythmdb_write_unlock (db);
	
	g_print ("Test 12: PASS\n");
	
	/**
	 * THE END
	 */
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	GDK_THREADS_LEAVE ();

	exit (0);
}
