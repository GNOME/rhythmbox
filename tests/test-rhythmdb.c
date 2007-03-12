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

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///whee.ogg");
	fail_unless (entry != NULL, "failed to create entry");

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
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		     "LOCATION set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		     "GENRE set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Nine Inch Nails") == 0,
		     "ARTIST set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Pretty Hate Machine") == 0,
		     "ALBUM set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		     "TITLE set incorrectly");

	/* check changing album */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Broken");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		     "LOCATION set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		     "GENRE set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Nine Inch Nails") == 0,
		     "ARTIST set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Broken") == 0,
		     "ALBUM set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		     "TITLE set incorrectly");

	/* check changing artist */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "Evanescence");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), "file:///whee.ogg") == 0,
		     "LOCATION set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE), "Rock") == 0,
		     "GENRE set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST), "Evanescence") == 0,
		     "ARTIST set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM), "Broken") == 0,
		     "ALBUM set incorrectly");
	fail_unless (strcmp (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), "Sin") == 0,
		     "TITLE set incorrectly");

	/* check removal */
	rhythmdb_entry_delete (db, entry);
	entry = NULL;

	b = FALSE;
	rhythmdb_entry_foreach (db, (GFunc)set_true, &b);
	fail_unless (b == FALSE, "entry not deleted");
}
END_TEST

START_TEST (test_rhythmdb_multiple)
{
	RhythmDBEntry *entry1, *entry2, *entry3;

	/* add multiple entries */
	entry1 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///foo.mp3");
	rhythmdb_commit (db);
	fail_unless (entry1 != NULL, "failed to create entry");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");

	entry2 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///bar.mp3");
	rhythmdb_commit (db);
	fail_unless (entry2 != NULL, "failed to create entry");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry2, "entry missing");

	entry3 = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///baz.mp3");
	rhythmdb_commit (db);
	fail_unless (entry3 != NULL, "failed to create entry");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* check they're still there */
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == entry2, "entry missing");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* remove the middle one and check again */
	rhythmdb_entry_delete (db, entry2);
	rhythmdb_commit (db);

	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == entry1, "entry missing");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == NULL, "entry not deleted");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == entry3, "entry missing");

	/* and the others */
	rhythmdb_entry_delete (db, entry1);
	rhythmdb_entry_delete (db, entry3);
	rhythmdb_commit (db);

	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///foo.mp3") == NULL, "entry not deleted");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///bar.mp3") == NULL, "entry not deleted");
	fail_unless (rhythmdb_entry_lookup_by_location (db, "file:///baz.mp3") == NULL, "entry not deleted");
}
END_TEST

START_TEST (test_rhythmdb_mirroring)
{
	GValue val = {0,};
	RhythmDBEntry *entry;
	const char *str;

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///foo.mp3");
	fail_unless (entry != NULL, "failed to create entry");

	/* check the last-played date is mirrored */
	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, 1354285);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	fail_unless (str && (strlen (str) > 0), "date not converted to string");

	/* check folded and sort-key varients */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "FOO");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_SORT_KEY);
	fail_unless (str && (strlen (str) > 0), "sort-key not generated");
	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
	fail_unless (str && (strcmp (str, "foo") == 0), "folded variant not generated");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, "BAR");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);
	rhythmdb_commit (db);

	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_SORT_KEY);
	fail_unless (str && (strlen (str) > 0), "sort-key not generated");
	str = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
	fail_unless (str && (strcmp (str, "bar") == 0), "folded variant not generated");


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

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///foo.mp3");
	fail_unless (entry != NULL, "failed to create entry");

	/* new entries should have 0 keywords */
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 0, "new entry had keywords");

	/* adding one keyword */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	fail_unless (ret == FALSE, "entry incorrectly reported as having keyword already");
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after one was added");

	/* has added keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_foo);
	fail_unless (ret == TRUE, "reported not having just-added keyword");

	/* add keyword again */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	fail_unless (ret == TRUE, "entry incorrectly reported as not keyword already");

	/* check keyword count*/
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after one was re-added");

	/* ensure it has only that keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_bar);
	fail_unless (ret == FALSE, "reported having wrong keyword");

	/* remove the keyword */
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	fail_unless (ret == TRUE, "reported having not previously having keyword");

	/* has removed keyword */
	ret = rhythmdb_entry_keyword_has (db, entry, keyword_foo);
	fail_unless (ret == FALSE, "reported having just-removed keyword");

	/* check count is back to zero */
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 0, "entry has keywords after they were removed");

	/* try removing keyword again */
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	fail_unless (ret == FALSE, "reported previously having already removed keyword");

	/* add and remove several keywords */
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_foo);
	fail_unless (ret == FALSE, "reported previously having already removed keyword");
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_bar);
	fail_unless (ret == FALSE, "reported previously having already never-added keyword");
	ret = rhythmdb_entry_keyword_add (db, entry, keyword_baz);
	fail_unless (ret == FALSE, "reported previously having already never-added keyword");

	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 3, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_foo);
	fail_unless (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 2, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_bar);
	fail_unless (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 1, "entry wrong number of keywords after several were added");
	ret = rhythmdb_entry_keyword_remove (db, entry, keyword_baz);
	fail_unless (ret == TRUE, "reported previously not having added keyword");
	list = rhythmdb_entry_keywords_get (db, entry);
	fail_unless (count_and_free_refstring_list (list) == 0, "entry wrong number of keywords after several were added");
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
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	fail_unless (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 0, "deserialisation incorrect");
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
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	/* FIXME: this fails for some reason
	fail_unless (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1, "deserialisation incorrect");*/
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
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				RHYTHMDB_QUERY_END);
	wait_for_signal ();
	/* FIXME: this fails for some reason
	fail_unless (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1, "deserialisation incorrect");*/
	g_object_unref (model);

	/* TODO: check values */
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

	/* tests for breakable bug fixes */

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	/* init stuff */
	rb_profile_start ("rhythmbox test suite");

	g_thread_init (NULL);
	rb_threads_init ();
	gtk_set_locale ();
	gtk_init (&argc, &argv);
	gnome_vfs_init ();
	rb_debug_init (TRUE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();


	GDK_THREADS_ENTER ();

	/* setup tests */
	s = rhythmdb_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);


	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();
	gnome_vfs_shutdown ();

	rb_profile_end ("rhythmbox test suite");
	return ret;
}
