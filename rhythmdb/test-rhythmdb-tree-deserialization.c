/* 
 *  arch-tag: De-serialization tests for the RhythmDB tree database
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

int
main (int argc, char **argv)
{
	RhythmDB *db;
	GtkTreeModel *main_model;
	GtkTreeModel *genre_model;
	GtkTreeModel *artist_model;
	GtkTreeModel *album_model;
	GtkTreeIter iter;

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

	/**
	 *  TEST 1: Load with no entries
	 */
	rb_debug ("Test 1");

	db = rhythmdb_tree_new ("deserialization-test1.xml");

	rhythmdb_load (db);
	rhythmdb_load_join (db);

	rhythmdb_read_lock (db);

	rhythmdb_do_full_query (db, &main_model, &genre_model,
				&artist_model, &album_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	g_assert (!gtk_tree_model_get_iter_first (main_model, &iter));

	rhythmdb_read_unlock (db);

	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	rb_debug ("Test 1: PASS");

	/**
	 *  TEST 2: Load with 1 entry
	 */
	rb_debug ("Test 2");

	db = rhythmdb_tree_new ("deserialization-test2.xml");

	rhythmdb_load (db);
	rhythmdb_load_join (db);

	rhythmdb_read_lock (db);

	rhythmdb_do_full_query (db, &main_model, &genre_model,
				&artist_model, &album_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	rb_debug ("Test 2: PASS");

	/**
	 *  TEST 3: Load with 2 entres, of different types
	 */
	rb_debug ("Test 3");

	db = rhythmdb_tree_new ("deserialization-test3.xml");

	rhythmdb_load (db);

	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	rb_debug ("Test 3: PASS");


	/**
	 * THE END
	 */
	GDK_THREADS_LEAVE ();
	
	exit (0);
}
