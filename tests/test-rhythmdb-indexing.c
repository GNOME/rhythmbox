/* 
 *  arch-tag: Indexing (genre/artist/album) tests for the RhythmDB tree database
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

static void
verify_entry_metadata (RhythmDB *db, RhythmDBEntry *entry,
		       const char *name, const char *album,
		       const char *artist, const char *genre)
{
	GValue val = {0, };
	
	g_value_init (&val, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_assert (!strcmp (name, g_value_get_string (&val)));
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_assert (!strcmp (album, g_value_get_string (&val)));
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_assert (!strcmp (artist, g_value_get_string (&val)));
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	rhythmdb_entry_get (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_assert (!strcmp (genre, g_value_get_string (&val)));
	g_value_unset (&val);
}

int
main (int argc, char **argv)
{
	RhythmDB *db;
	RhythmDBEntry *entry;
	GValue val = {0, };

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

	db = rhythmdb_tree_new ("test");

	/**
	 *  TEST 1: Entry creation with album
	 */
	g_print ("Test 1\n");
	rhythmdb_write_lock (db);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///whee.ogg");
	g_assert (entry);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Rock");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Nine Inch Nails");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Pretty Hate Machine");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Sin");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);
	
	rhythmdb_write_unlock (db);
	g_print ("Test 1: PASS\n");


	/**
	 *  TEST 2: Read back indexed values
	 */
	g_print ("Test 2\n");
	rhythmdb_read_lock (db);

	verify_entry_metadata (db, entry, "Sin", "Pretty Hate Machine",
			       "Nine Inch Nails", "Rock");

	rhythmdb_read_unlock (db);
	g_print ("Test 2: PASS\n");

	/**
	 *  TEST 3: Changing album
	 */
	g_print ("Test 3\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Broken");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);
	rhythmdb_write_unlock (db);

	rhythmdb_read_lock (db);
	verify_entry_metadata (db, entry, "Sin", "Broken",
			       "Nine Inch Nails", "Rock");
	rhythmdb_read_unlock (db);

	g_print ("Test 3: PASS\n");

	/**
	 *  TEST 4: Changing artist
	 */
	g_print ("Test 4\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Evanescence");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	rhythmdb_write_unlock (db);

	rhythmdb_read_lock (db);
	verify_entry_metadata (db, entry, "Sin", "Broken",
			       "Evanescence", "Rock");
	rhythmdb_read_unlock (db);

	g_print ("Test 4: PASS\n");

	/**
	 *  TEST 5: Changing genre
	 */
	g_print ("Test 5\n");
	rhythmdb_write_lock (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Techno");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	rhythmdb_write_unlock (db);

	rhythmdb_read_lock (db);
	verify_entry_metadata (db, entry, "Sin", "Broken",
			       "Evanescence", "Techno");
	rhythmdb_read_unlock (db);

	g_print ("Test 5: PASS\n");

	/**
	 * THE END
	 */
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	GDK_THREADS_LEAVE ();
	
	exit (0);
}
