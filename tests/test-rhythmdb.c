#include <check.h>
#include <gtk/gtk.h>
#include <string.h>

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#include "rhythmdb.h"
#include "rhythmdb-tree.h"
#include "rhythmdb-query-model.h"

#ifndef fail_if
#define fail_if(expr, ...) fail_unless(!(expr), "Failure '"#expr"' occured")
#endif


/* test utils */
gboolean waiting, signaled;
char *sig_name;
gulong sig_handler;
GObject *sig_object;

static void
mark_signal (void)
{
	g_signal_handler_disconnect (sig_object, sig_handler);
	sig_object = NULL;
	sig_handler = 0;

	if (signaled) {
		rb_debug ("got signal '%s' multiple times", sig_name);
	} else {
		rb_debug ("got signal '%s'", sig_name);
		signaled = TRUE;
		if (waiting)
			gtk_main_quit ();
	}
}

static void
set_waiting_signal (GObject *o, const char *name)
{
	signaled = FALSE;
	waiting = FALSE;
	sig_name = g_strdup (name);
	sig_object = o;
	sig_handler = g_signal_connect (o, sig_name, G_CALLBACK (mark_signal), NULL);
}

static void wait_for_signal (void)
{
	if (!signaled) {
		rb_debug ("waiting for signal '%s'", sig_name);
		waiting = TRUE;
		gtk_main ();
	} else {
		rb_debug ("no need to wait for signal '%s', already received", sig_name);
	}

	g_free (sig_name);
	sig_name = NULL;
}


/* common setup and teardown */
RhythmDB *db = NULL;
gboolean waiting_db, finalised_db;

static void
test_rhythmdb_setup (void)
{
	RhythmDBEntryType entry_type;
	db = rhythmdb_tree_new ("test");
	fail_unless (db != NULL, "failed to initialise DB");
	rhythmdb_start_action_thread (db);

	/* allow SONGs to be synced to for the tests */
	entry_type = RHYTHMDB_ENTRY_TYPE_SONG;
	entry_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc)rb_true_function;
	entry_type->sync_metadata = (RhythmDBEntrySyncFunc)rb_null_function;
}

static void
test_rhythmdb_shutdown (void)
{
	fail_unless (db != NULL, "failed to shutdown DB");
	rhythmdb_shutdown (db);

	/* release the reference, and wait until after finalisation */
	g_object_weak_ref (G_OBJECT (db), (GWeakNotify)gtk_main_quit, NULL);
	g_idle_add ((GSourceFunc)g_object_unref, db);
	gtk_main ();
	db = NULL;
}

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

START_TEST (test_rhythmdb_db_queries)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBQuery *query;
	GValue val = {0,};

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

	/* basic queries and conjunctions */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TITLE, "Sin",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);
	
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_ARTIST, "Nine Inch",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);
	
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_ALBUM, "Load",
				      RHYTHMDB_QUERY_END);
	fail_if (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);
	
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_SEARCH_MATCH, "Pretty Nine",
				      RHYTHMDB_QUERY_END);
	rhythmdb_query_preprocess (db, query);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	/* disjunctions */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sin",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Son",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);
	
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sun",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sin",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);
	
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sun",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Son",
				      RHYTHMDB_QUERY_END);
	fail_if (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	/* TODO: subqueries */

	rhythmdb_entry_delete (db, entry);
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
	TCase *tc_chain = tcase_create ("rhythmdb");

	suite_add_tcase (s, tc_chain);
	tcase_add_checked_fixture(tc_chain, test_rhythmdb_setup, test_rhythmdb_shutdown);
	/*tcase_add_test (tc_chain, test_refstring);*/
	tcase_add_test (tc_chain, test_rhythmdb_indexing);
	tcase_add_test (tc_chain, test_rhythmdb_multiple);
	tcase_add_test (tc_chain, test_rhythmdb_mirroring);
	/*tcase_add_test (tc_chain, test_rhythmdb_signals);*/
	/*tcase_add_test (tc_chain, test_rhythmdb_query);*/
	tcase_add_test (tc_chain, test_rhythmdb_db_queries);
	/*tcase_add_test (tc_chain, test_rhythmdb_query_model);*/
	/*tcase_add_test (tc_chain, test_rhythmdb_chained_query_model);*/
	/*tcase_add_test (tc_chain, test_rhythmdb_property_model);*/
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation1);
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation2);
	tcase_add_test (tc_chain, test_rhythmdb_deserialisation3);
	/*tcase_add_test (tc_chain, test_rhythmdb_serialisation);*/

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
