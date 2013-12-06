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
#include "rhythmdb-query-model.h"
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-thread-helpers.h"

static RhythmDBEntry *
create_entry (RhythmDB *db,
	      const char *location, const char *name, const char *album,
	      const char *artist, const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = {0, };

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, location);
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

#define DESTROY_MODELS()			\
G_STMT_START {					\
g_object_unref (G_OBJECT (main_model));		\
} G_STMT_END

static void
assert_entry_in_model (GtkTreeModel *model, RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	RhythmDBEntry *tree_entry;
	g_assert (gtk_tree_model_get_iter_first (model, &iter));
	gtk_tree_model_get (model, &iter, 0, &tree_entry, -1);
	while (tree_entry != entry) {
		g_assert (gtk_tree_model_iter_next (model, &iter));
		gtk_tree_model_get (model, &iter, 0, &tree_entry, -1);
	}
}

/* static void */
/* assert_string_in_model (GtkTreeModel *model, const char *str) */
/* { */
/* 	GtkTreeIter iter; */
/* 	char *tree_str; */
/* 	g_assert (gtk_tree_model_get_iter_first (model, &iter)); */
/* 	gtk_tree_model_get (model, &iter, 0, &tree_str, -1); */
/* 	while (strcmp (tree_str, str)) { */
/* 		g_free (tree_str); */
/* 		g_assert (gtk_tree_model_iter_next (model, &iter)); */
/* 		gtk_tree_model_get (model, &iter, 0, &tree_str, -1); */
/* 	} */
/* 	g_free (tree_str); */
/* } */

#define VERIFY_MAIN_MODEL(ENTRY) assert_entry_in_model (main_model, ENTRY);

#define VERIFY_META_MODELS(ALBUM, ARTIST, GENRE)
/* #define VERIFY_META_MODELS(ALBUM, ARTIST, GENRE)		\ */
/* G_STMT_START {							\ */
/* assert_string_in_model (genre_model, GENRE);			\ */
/* assert_string_in_model (artist_model, ARTIST);			\ */
/* assert_string_in_model (album_model, ALBUM);			\ */
/* } G_STMT_END */

static void
completed_cb (RhythmDBQueryModel *model, gboolean *complete)
{
	rb_debug ("query complete");
	*complete = TRUE;
}

static void
wait_for_model_completion (RhythmDBQueryModel *model)
{
	gboolean complete = FALSE;
	GTimeVal timeout;
	g_signal_connect (G_OBJECT (model), "complete",
			  G_CALLBACK (completed_cb), &complete);

	while (!complete) {
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC);

		rb_debug ("polling model for changes");
		rhythmdb_query_model_sync (model, &timeout);
		g_usleep (G_USEC_PER_SEC / 10.0);
	}
}

typedef void (*query_func) (RhythmDB *db, GtkTreeModel *model, ...);

int
main (int argc, char **argv)
{
	RhythmDB *db;
	RhythmDBEntry *entry, *entry2, *entry3, *entry4;
	GtkTreeModel *main_model;
	GtkTreeIter iter;
	guint testnum = 1;
	query_func qfunc;

	gtk_init (&argc, &argv);
	rb_thread_helpers_init ();
	rb_debug_init (TRUE);

	qfunc = rhythmdb_do_full_query;

begin:	

	db = rhythmdb_tree_new ("test");

	/*
	 *  TEST 1: Entry creation with album
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	entry = create_entry (db, "file:///sin.mp3",
			      "Sin", "Pretty Hate Machine", "Nine Inch Nails", "Rock");
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);
	
	testnum++;
	/*
	 *  TEST 2: Do a query for all songs, verify our single song is in it
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	qfunc (db, main_model,
	       RHYTHMDB_QUERY_PROP_EQUALS,
	       RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
	       RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 3: Do a query for songs named "Sin"
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Sin",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 4: Do a query for songs named "Cow", should be empty
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Cow",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (!gtk_tree_model_get_iter_first (main_model, &iter));

	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 5
	 *  Do a query for songs named "Cow" and "Sin", should be empty.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Cow",
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Sin",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (!gtk_tree_model_get_iter_first (main_model, &iter));

	DESTROY_MODELS ();
	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 6
	 *  Do a query for songs named "Cow" or "Sin", should have our song.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Cow",
				RHYTHMDB_QUERY_DISJUNCTION,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Sin",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 7
	 *  Do a query for songs with Genre "Rock", should have our song.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Rock",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 8
	 *  Do a query for songs with Genre "Nine Inch Nails",
	 *  should be empty.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Nine Inch Nails",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (!gtk_tree_model_get_iter_first (main_model, &iter));

	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);
	
	testnum++;
	/*
	 *  TEST 9
	 *  Do a query for songs with album "Pretty Hate Machine",
	 *  should have our song.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, "Pretty Hate Machine",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	/*
	 *  TEST 10
	 *  Do a query for songs with artist "Nine Inch Nails",
	 *  should have our song.
	 */
	g_print ("Test %d\n", testnum);
	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ARTIST, "Nine Inch Nails",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	DESTROY_MODELS ();

	rhythmdb_read_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	entry2 = create_entry (db, "file:///head like a hole.mp3",
			       "Head Like A Hole", "Pretty Hate Machine",
			       "Nine Inch Nails", "Rock");

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ARTIST, "Nine Inch Nails",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	VERIFY_MAIN_MODEL (entry2);
	DESTROY_MODELS ();
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, "Pretty Hate Machine",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	VERIFY_MAIN_MODEL (entry2);
	DESTROY_MODELS ();
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Rock",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	VERIFY_MAIN_MODEL (entry2);
	DESTROY_MODELS ();
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);

	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	entry3 = create_entry (db, "file:///angel.ogg",
			       "Angel", "Mezzanine", "Massive Attack", "Electronica");

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_GENRE, "Electronica",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Mezzanine", "Massive Attack", "Electronica");
	VERIFY_MAIN_MODEL (entry3);
	DESTROY_MODELS ();
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;
	g_print ("Test %d\n", testnum);
	rhythmdb_write_lock (db);

	entry4 = create_entry (db, "file:///killa bees.ogg",
			       "Killa Bees", "Armageddon", "Usual Suspects", "Drum N' Bass");

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Angel",
				RHYTHMDB_QUERY_DISJUNCTION,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Sin",
				RHYTHMDB_QUERY_DISJUNCTION,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Head Like A Hole",
				RHYTHMDB_QUERY_DISJUNCTION,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, "Killa Bees",
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));

	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (gtk_tree_model_iter_next (main_model, &iter));
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	VERIFY_META_MODELS ("Mezzanine", "Massive Attack", "Electronica");
	VERIFY_META_MODELS ("Armageddon", "Usual Suspects", "Drum N' Bass");
	VERIFY_META_MODELS ("Pretty Hate Machine", "Nine Inch Nails", "Rock");
	VERIFY_MAIN_MODEL (entry);
	VERIFY_MAIN_MODEL (entry2);
	VERIFY_MAIN_MODEL (entry3);
	VERIFY_MAIN_MODEL (entry4);
	DESTROY_MODELS ();
	
	rhythmdb_write_unlock (db);
	g_print ("Test %d\n", testnum);

	testnum++;

	if (qfunc == rhythmdb_do_full_query) {
		rhythmdb_shutdown (db);
		g_object_unref (G_OBJECT (db));
		qfunc = rhythmdb_do_full_query_async;
		goto begin;
	}

	/*
	 * THE END
	 */
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	
	exit (0);
}
