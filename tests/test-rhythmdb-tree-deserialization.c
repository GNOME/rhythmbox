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
#include "rhythmdb-query-model.h"
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-thread-helpers.h"

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
	}
}

int
main (int argc, char **argv)
{
	RhythmDB *db;
	GtkTreeModel *main_model;
	GtkTreeIter iter;

	gtk_init (&argc, &argv);
	g_thread_init (NULL);
	gdk_threads_init ();
	rb_thread_helpers_init ();
	rb_debug_init (TRUE);

	GDK_THREADS_ENTER ();

	/**
	 *  TEST 1: Load with no entries
	 */
	g_print ("Test 1\n");

	db = rhythmdb_tree_new ("deserialization-test1.xml");

	rhythmdb_load (db);
	rhythmdb_load_join (db);

	rhythmdb_read_lock (db);


	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));
	g_assert (!gtk_tree_model_get_iter_first (main_model, &iter));

	rhythmdb_read_unlock (db);

	g_object_unref (G_OBJECT (main_model));
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	g_print ("Test 1: PASS\n");

	/**
	 *  TEST 2: Load with 1 entry
	 */
	g_print ("Test 2\n");

	db = rhythmdb_tree_new ("deserialization-test2.xml");

	rhythmdb_load (db);
	rhythmdb_load_join (db);

	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));
	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));
	/* We should only have one entry. */
	g_assert (!gtk_tree_model_iter_next (main_model, &iter));

	g_object_unref (G_OBJECT (main_model));
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	g_print ("Test 2: PASS\n");

	/**
	 *  TEST 3: Load with 2 entres, of different types
	 */
	g_print ("Test 3\n");

	db = rhythmdb_tree_new ("deserialization-test3.xml");

	rhythmdb_load (db);

	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));

	g_print ("Test 3: PASS\n");


	/**
	 * THE END
	 */
	GDK_THREADS_LEAVE ();
	
	exit (0);
}
