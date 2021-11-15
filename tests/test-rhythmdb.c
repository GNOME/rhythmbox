/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 *
 */


#include "config.h"

#include <check.h>
#include <gtk/gtk.h>
#include <string.h>
#include <glib/gi18n.h>

#include "test-utils.h"

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#include "rhythmdb.h"
#include "rhythmdb-tree.h"
#include "rhythmdb-query-model.h"
#include "rb-podcast-entry-types.h"

static void
set_true (RhythmDBEntry *entry, gboolean *b)
{
	*b = TRUE;
}



/* tests */
START_TEST (test_rhythmdb_indexing)
{
	RhythmDBEntry *entry = NULL;
	GValue val = {0,};
	gboolean b;

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///whee.ogg");
	ck_assert_msg (entry != NULL, "failed to create entry");

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

	rhythmdb_commit (db);

	/* check the data is recorded correctly */
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		       "LOCATION set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		       "GENRE set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Nine Inch Nails") == 0,
		       "ARTIST set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Pretty Hate Machine") == 0,
		       "ALBUM set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		       "TITLE set incorrectly");

	/* check changing album */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Broken");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		       "LOCATION set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		       "GENRE set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Nine Inch Nails") == 0,
		       "ARTIST set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Broken") == 0,
		       "ALBUM set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		       "TITLE set incorrectly");

	/* check changing artist */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Evanescence");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		       "LOCATION set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		       "GENRE set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Evanescence") == 0,
		       "ARTIST set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Broken") == 0,
		       "ALBUM set incorrectly");
	ck_assert_msg (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		       "TITLE set incorrectly");

	/* check removal */
	rhythmdb_entry_delete (db, entry);
	entry = NULL;

	b = FALSE;
	rhythmdb_entry_foreach (db, (RhythmDBEntryForeachFunc) set_true, &b);
	ck_assert_msg (b == FALSE, "entry not deleted");
}
END_TEST

START_TEST (test_rhythmdb_multiple)
{
	RhythmDBEntry *entry1, *entry2, *entry3;

	/* add multiple entries */
	entry1 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///foo.mp3");
	rhythmdb_commit (db);
	ck_assert_msg (entry1 != NULL, "failed to create entry");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");

	entry2 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///bar.mp3");
	rhythmdb_commit (db);
	ck_assert_msg (entry2 != NULL, "failed to create entry");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry2, "entry missing");

	entry3 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///baz.mp3");
	rhythmdb_commit (db);
	ck_assert_msg (entry3 != NULL, "failed to create entry");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* check they're still there */
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry2, "entry missing");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* remove the middle one and check again */
	rhythmdb_entry_delete (db, entry2);
	rhythmdb_commit (db);

	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == NULL, "entry not deleted");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* and the others */
	rhythmdb_entry_delete (db, entry1);
	rhythmdb_entry_delete (db, entry3);
	rhythmdb_commit (db);

	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == NULL, "entry not deleted");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == NULL, "entry not deleted");
	ck_assert_msg (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == NULL, "entry not deleted");
}
END_TEST

START_TEST (test_rhythmdb_mirroring)
{
	GValue val = {0,};
	RhythmDBEntry *entry;
	const char *str;

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///foo.mp3");
	ck_assert_msg (entry != NULL, "failed to create entry");

	/* check the last-played date is mirrored */
	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, 1354285);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	ck_assert_msg (str && (strlen (str) > 0), "date not converted to string");

	/* check folded and sort-key varients */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "FOO");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_SORT_KEY);
	ck_assert_msg (str && (strlen (str) > 0), "sort-key not generated");
	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
	ck_assert_msg (str && (strcmp (str, "foo") == 0), "folded variant not generated");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "BAR");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_SORT_KEY);
	ck_assert_msg (str && (strlen (str) > 0), "sort-key not generated");
	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
	ck_assert_msg (str && (strcmp (str, "bar") == 0), "folded variant not generated");


}
END_TEST

static int
count_and_free_refstring_list (GList *list)
{
	int count;

	count = g_list_length (list);

	g_list_foreach (list, (GFunc)rb_refstring_unref, NULL);
	g_list_free (list);

	return count;
}

START_TEST (test_rhythmdb_keywords)
{
	RhythmDBEntry *entry;
	GList *list;
	gboolean ret;
	RBRefString *keyword_foo, *keyword_bar, *keyword_baz;

	keyword_foo = rb_refstring_new ("foo");
	keyword_bar = rb_refstring_new ("bar");
	keyword_baz = rb_refstring_new ("baz");

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///foo.mp3");
	ck_assert_msg (entry != NULL, "failed to create entry");

	/* new entries should have 0 keywords */
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 0, "new entry had keywords");

	/* adding one keyword */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	ck_assert_msg (ret == FALSE, "entry incorrectly reported as having keyword already");
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after one was added");

	/* has added keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_foo);
	ck_assert_msg (ret == TRUE, "reported not having just-added keyword");

	/* add keyword again */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	ck_assert_msg (ret == TRUE, "entry incorrectly reported as not keyword already");

	/* check keyword count*/
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after one was re-added");

	/* ensure it has only that keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_bar);
	ck_assert_msg (ret == FALSE, "reported having wrong keyword");

	/* remove the keyword */
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	ck_assert_msg (ret == TRUE, "reported having not previously having keyword");

	/* has removed keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_foo);
	ck_assert_msg (ret == FALSE, "reported having just-removed keyword");

	/* check count is back to zero */
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 0, "entry has keywords after they were removed");

	/* try removing keyword again */
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	ck_assert_msg (ret == FALSE, "reported previously having already removed keyword");

	/* add and remove several keywords */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	ck_assert_msg (ret == FALSE, "reported previously having already removed keyword");
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_bar);
	ck_assert_msg (ret == FALSE, "reported previously having already never-added keyword");
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_baz);
	ck_assert_msg (ret == FALSE, "reported previously having already never-added keyword");

	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 3, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	ck_assert_msg (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 2, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_bar);
	ck_assert_msg (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_baz);
	ck_assert_msg (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	ck_assert_msg (count_and_free_refstring_list (list) == 0, "entry wrong number of keywords after several were added");
}
END_TEST

START_TEST (test_rhythmdb_deserialisation1)
{
	RhythmDBQueryModel *model;

	/* empty db */
	g_object_set (G_OBJECT (db), "name", "deserialization-test1.xml", NULL);
	set_waiting_signal (G_OBJECT (db), "load-complete");
	rhythmdb_load (db);
	wait_for_signal ();

	model = rhythmdb_query_model_new_empty (db);
	g_object_set (G_OBJECT (model), "show-hidden", TRUE, NULL);
	set_waiting_signal (G_OBJECT (model), "complete");
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (model),
				NULL,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	ck_assert_msg (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 0, "deserialisation incorrect");
	g_object_unref (model);
}
END_TEST

START_TEST (test_rhythmdb_deserialisation2)
{
	RhythmDBQueryModel *model;

	/* single entry db */
	g_object_set (G_OBJECT (db), "name", "deserialization-test2.xml", NULL);
	set_waiting_signal (G_OBJECT (db), "load-complete");
	rhythmdb_load (db);
	wait_for_signal ();

	model = rhythmdb_query_model_new_empty (db);
	g_object_set (G_OBJECT (model), "show-hidden", TRUE, NULL);
	set_waiting_signal (G_OBJECT (model), "complete");
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	/* FIXME: this fails for some reason
	ck_assert_msg (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1, "deserialisation incorrect");*/
	g_object_unref (model);

	/* TODO: check values */
}
END_TEST

START_TEST (test_rhythmdb_deserialisation3)
{
	RhythmDBQueryModel *model;

	/* two entries of different types db */
	g_object_set (G_OBJECT (db), "name", "deserialization-test3.xml", NULL);
	set_waiting_signal (G_OBJECT (db), "load-complete");
	rhythmdb_load (db);
	wait_for_signal ();

	model = rhythmdb_query_model_new_empty (db);
	g_object_set (G_OBJECT (model), "show-hidden", TRUE, NULL);
	set_waiting_signal (G_OBJECT (model), "complete");
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (model),
				NULL,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	/* FIXME: this fails for some reason
	ck_assert_msg (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1, "deserialisation incorrect");*/
	g_object_unref (model);

	/* TODO: check values */
}
END_TEST


#define BARRIER_TEST_THREADS	10

typedef struct {
	RhythmDBEntry *entry;
	int committed;
	GThread *thread;
} BarrierTestData;

static gpointer
test_worker (gpointer xdata)
{
	BarrierTestData *data = xdata;
	GValue val = {0, };
	const char *str;

	rb_debug ("worker thread");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Bbb");
	rhythmdb_entry_set (db, data->entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	rb_debug ("checking");
	str = rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_TITLE);
	ck_assert_msg (str && (strcmp (str, "Aaa") == 0), "changes should not be visible on worker thread yet");

	rb_debug ("committing");
	rhythmdb_commit (db);

	rb_debug ("checking again");
	str = rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_TITLE);
	ck_assert_msg (str && (strcmp (str, "Bbb") == 0), "changes should be visible on worker thread now");

	rb_debug ("done");
	data->committed = 1;
	return NULL;
}


START_TEST (test_rhythmdb_thread_barrier)
{
	BarrierTestData data[BARRIER_TEST_THREADS];
	const char *str;
	GValue val = {0, };
	int i;

	for (i = 0; i < BARRIER_TEST_THREADS; i++) {
		char *uri;


		uri = g_strdup_printf ("file:///bar%d.ogg", i);
		data[i].committed = 0;
		data[i].entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, uri);
		ck_assert_msg (data[i].entry != NULL, "failed to create entry");

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_static_string (&val, "Aaa");
		rhythmdb_entry_set (db, data[i].entry, RHYTHMDB_PROP_TITLE, &val);
		g_value_unset (&val);

		g_free (uri);
	}
	rhythmdb_commit (db);

	end_step ();

	for (i = 0; i < BARRIER_TEST_THREADS; i++) {
		data[i].thread = g_thread_new ("test-worker", test_worker, &data[i]);
	}

	rb_debug ("letting worker threads run");
	g_usleep (G_USEC_PER_SEC / 10);

	for (i = 0; i < BARRIER_TEST_THREADS; i++) {
		/* worker threads' commits shouldn't finish until we let the event queue run */
		ck_assert_msg (data[i].committed == 0, "worker thread should not be able to commit");
		str = rhythmdb_entry_get_string (data[i].entry, RHYTHMDB_PROP_TITLE);
		ck_assert_msg (str && (strcmp (str, "Aaa") == 0), "worker thread changes should not be visible yet");
	}

	rb_debug ("processing events from worker threads");
	end_step ();

	rb_debug ("joining worker threads");
	for (i = 0; i < BARRIER_TEST_THREADS; i++) {
		g_thread_join (data[i].thread);
	}

	rb_debug ("checking final state");
	for (i = 0; i < BARRIER_TEST_THREADS; i++) {
		str = rhythmdb_entry_get_string (data[i].entry, RHYTHMDB_PROP_TITLE);
		ck_assert_msg (str && (strcmp (str, "Bbb") == 0), "worker thread changes be visible now");
	}
}
END_TEST

START_TEST (test_rhythmdb_podcast_upgrade)
{
	RhythmDBEntry *entry;
	const char *mountpoint;

	/* load db with old podcasts setups */
	g_object_set (G_OBJECT (db), "name", TEST_DIR "/podcast-upgrade.xml", NULL);
	set_waiting_signal (G_OBJECT (db), "load-complete");
	rhythmdb_load (db);
	wait_for_signal ();

	entry = rhythmdb_entry_lookup_by_location (db, "file:///home/tester/Desktop/BBC%20Xtra/xtra_20080906-1226a.mp3");

	ck_assert_msg (entry != NULL, "entry missing");
	ck_assert_msg (rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST, "entry isn't a podcast");
	mountpoint = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);

	ck_assert_msg (mountpoint != NULL, "no mountpoint for podcast");
	ck_assert_msg (strcmp (mountpoint, "http://downloads.bbc.co.uk/podcasts/worldservice/xtra/xtra_20080906-1226a.mp3") == 0, "wrong mountpoint for podcast");

	entry = rhythmdb_entry_lookup_by_location (db, "http://downloads.bbc.co.uk/podcasts/worldservice/xtra/xtra_20080903-1217a.mp3");
	ck_assert_msg (entry != NULL, "entry not upgraded");
	ck_assert_msg (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) == NULL, "wrong mountpoint for podcast");
}
END_TEST

START_TEST (test_rhythmdb_modify_after_delete)
{
	RhythmDBEntry *entry;
	GValue val = {0,};

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///whee.ogg");
	ck_assert_msg (entry != NULL, "failed to create entry");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Anything");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Nothing");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Something");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Thing");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	rhythmdb_commit (db);
	rhythmdb_entry_ref (entry);

	rhythmdb_entry_delete (db, entry);
	rhythmdb_commit (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Something Else");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	rhythmdb_commit (db);
	rhythmdb_entry_unref (entry);
}
END_TEST

static void
commit_change_merge_cb (RhythmDB *db, RhythmDBEntry *entry, GArray *changes, gpointer ok)
{
	int expected = GPOINTER_TO_INT (ok);
	ck_assert_msg (changes->len == expected, "commit change lists merged");
}

START_TEST (test_rhythmdb_commit_change_merging)
{
	RhythmDBEntry *entry;
	GValue val = {0,};

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, "file:///whee.ogg");
	ck_assert_msg (entry != NULL, "failed to create entry");

	rhythmdb_commit (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Anything");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	rhythmdb_commit (db);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Nothing");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_signal_connect (G_OBJECT (db), "entry-changed", G_CALLBACK (commit_change_merge_cb), GINT_TO_POINTER (2));
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	rhythmdb_commit (db);
	wait_for_signal ();
}
END_TEST

static Suite *
rhythmdb_suite (void)
{
	Suite *s = suite_create ("rhythmdb");
	TCase *tc_chain = tcase_create ("rhythmdb-core");
	TCase *tc_bugs = tcase_create ("rhythmdb-bugs");

	suite_add_tcase (s, tc_chain);
	tcase_add_checked_fixture (tc_chain, test_rhythmdb_setup, test_rhythmdb_shutdown);
	suite_add_tcase (s, tc_bugs);
	tcase_add_checked_fixture (tc_bugs, test_rhythmdb_setup, test_rhythmdb_shutdown);

	/* test core functionality */
	/*tcase_add_test (tc_chain, test_refstring);*/
	tcase_add_test (tc_chain, test_rhythmdb_indexing);
	tcase_add_test (tc_chain, test_rhythmdb_multiple);
	tcase_add_test (tc_chain, test_rhythmdb_mirroring);
	tcase_add_test (tc_chain, test_rhythmdb_keywords);
	/*tcase_add_test (tc_chain, test_rhythmdb_signals);*/
	/*tcase_add_test (tc_chain, test_rhythmdb_query);*/
	/* FIXME: add some keywords to the deserialisation tests */
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation1);
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation2);
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation3);
	/*tcase_add_test (tc_chain, test_rhythmdb_serialisation);*/

	/* tests for entry changes and commits from worker threads */
	tcase_add_test (tc_chain, test_rhythmdb_thread_barrier);

	/* tests for breakable bug fixes */
	tcase_add_test (tc_chain, test_rhythmdb_podcast_upgrade);
	tcase_add_test (tc_chain, test_rhythmdb_modify_after_delete);
	tcase_add_test (tc_chain, test_rhythmdb_commit_change_merging);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	g_log_set_always_fatal (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);

	/* init stuff */
	rb_profile_start ("rhythmbox test suite");

	rb_threads_init ();
	rb_debug_init (TRUE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();

	/* setup tests */
	s = rhythmdb_suite ();
	sr = srunner_create (s);

	init_setup (sr, argc, argv);
	init_once (FALSE);

	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();

	rb_profile_end ("rhythmbox test suite");
	return ret;
}
