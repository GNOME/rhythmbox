#include "config.h"

#include <check.h>
#include <gtk/gtk.h>
#include "test-utils.h"
#include "rhythmdb-query-model.h"

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

START_TEST (test_rhythmdb_db_queries)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBQuery *query;
	GValue val = {0,};

	start_test_case ();

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

	end_step ();

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_ARTIST, "Nine Inch",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	end_step ();

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_ALBUM, "Load",
				      RHYTHMDB_QUERY_END);
	fail_if (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	end_step ();

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_SEARCH_MATCH, "Pretty Nine",
				      RHYTHMDB_QUERY_END);
	rhythmdb_query_preprocess (db, query);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	end_step ();

	/* disjunctions */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sin",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Son",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	end_step ();

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sun",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sin",
				      RHYTHMDB_QUERY_END);
	fail_unless (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	end_step ();

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Sun",
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "Son",
				      RHYTHMDB_QUERY_END);
	fail_if (rhythmdb_evaluate_query (db, query, entry), "query evaluated incorrectly");
	rhythmdb_query_free (query);

	/* TODO: subqueries */

	rhythmdb_entry_delete (db, entry);

	end_test_case ();
}
END_TEST

/* this tests that chained query models, where the base shows hidden entries
 * forwards visibility changes correctly. This is basically what static playlists do */
START_TEST (test_hidden_chain_filter)
{
	RhythmDBQueryModel *base_model;
	RhythmDBQueryModel *filter_model;
	RhythmDBQuery *query;
	RhythmDBEntry *entry;
	GtkTreeIter iter;
	GValue val = {0,};

	start_test_case ();

	/* setup */
	base_model = rhythmdb_query_model_new_empty (db);
	g_object_set (base_model, "show-hidden", TRUE, NULL);

	filter_model = rhythmdb_query_model_new_empty (db);
	g_object_set (filter_model, "base-model", base_model, NULL);
	query = g_ptr_array_new ();
	g_object_set (filter_model, "query", query, NULL);
	rhythmdb_query_free (query);

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///whee.ogg");
	rhythmdb_commit (db);

	g_value_init (&val, G_TYPE_BOOLEAN);


	/* add entry to base, should be in both */
	rhythmdb_query_model_add_entry (base_model, entry, -1);
	fail_unless (rhythmdb_query_model_entry_to_iter (base_model, entry, &iter));
	fail_unless (rhythmdb_query_model_entry_to_iter (filter_model, entry, &iter));

	end_step ();

	/* hide entry, should be in base and not filtered */
	g_value_set_boolean (&val, TRUE);
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_HIDDEN, &val);
	rhythmdb_commit (db);
	wait_for_signal ();

	fail_unless (rhythmdb_query_model_entry_to_iter (base_model, entry, &iter));
	fail_if (rhythmdb_query_model_entry_to_iter (filter_model, entry, &iter));

	end_step ();

	/* show entry again, should be in both */
	g_value_set_boolean (&val, FALSE);
	set_waiting_signal (G_OBJECT (db), "entry-changed");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_HIDDEN, &val);
	rhythmdb_commit (db);
	wait_for_signal ();

	fail_unless (rhythmdb_query_model_entry_to_iter (base_model, entry, &iter));
	fail_unless (rhythmdb_query_model_entry_to_iter (filter_model, entry, &iter));

	end_step ();

	/* tidy up */
	rhythmdb_entry_delete (db, entry);
	g_object_unref (base_model);
	g_object_unref (filter_model);
	g_value_unset (&val);

	end_test_case ();
}
END_TEST

static Suite *
rhythmdb_query_model_suite (void)
{
	Suite *s = suite_create ("rhythmdb-query-model");
	TCase *tc_chain = tcase_create ("rhythmdb-query-model-core");
	TCase *tc_bugs = tcase_create ("rhythmdb-query-model-bugs");

	suite_add_tcase (s, tc_chain);
	tcase_add_checked_fixture (tc_chain, test_rhythmdb_setup, test_rhythmdb_shutdown);
	suite_add_tcase (s, tc_bugs);
	tcase_add_checked_fixture (tc_bugs, test_rhythmdb_setup, test_rhythmdb_shutdown);

	/* test core functionality */
	tcase_add_test (tc_chain, test_rhythmdb_db_queries);

	/* tests for breakable bug fixes */
	tcase_add_test (tc_bugs, test_hidden_chain_filter);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	/* init stuff */
	rb_profile_start ("rhythmdb-query-model test suite");

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
	s = rhythmdb_query_model_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);


	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();
	gnome_vfs_shutdown ();

	rb_profile_end ("rhythmdb-query-model test suite");
	return ret;
}

