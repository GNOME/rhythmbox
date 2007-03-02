#include "config.h"


#include <check.h>
#include <gtk/gtk.h>
#include "test-utils.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-property-model.h"

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

static int
_get_property_count (RhythmDBPropertyModel *model, const char *artist)
{
	GtkTreeIter iter;
	int count;

	if (rhythmdb_property_model_iter_from_string (model, artist, &iter) == FALSE) {
		return 0;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &count, -1);
	return count;
}

/* tests property models attached to static query models */
START_TEST (test_rhythmdb_property_model_static)
{
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *model2;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *b;
	GtkTreeIter iter;

	start_test_case ();

	/* setup */
	model = rhythmdb_query_model_new_empty (db);
	g_object_set (model, "show-hidden", FALSE, NULL);
	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ARTIST);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///a.ogg");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///b.ogg");
	rhythmdb_commit (db);

	/* set artist values */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);

	end_step ();

	/* add to model */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, a, -1);
	wait_for_signal ();
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	rhythmdb_query_model_add_entry (model, b, -1);
	wait_for_signal ();
	fail_unless (rhythmdb_query_model_entry_to_iter (model, a, &iter));
	fail_unless (rhythmdb_query_model_entry_to_iter (model, b, &iter));
	/*fail_unless (_get_property_count (propmodel, _("All")) == 2);*/
	fail_unless (_get_property_count (propmodel, "x") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change one */
	set_waiting_signal (G_OBJECT (propmodel), "row-deleted");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	wait_for_signal ();
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 2);

	end_step ();

	/* hide it */
	set_entry_hidden (db, a, TRUE);
	rhythmdb_commit (db);
	/*fail_unless (_get_property_count (propmodel, _("All")) == 1);*/
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change back */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* unhide */
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	set_entry_hidden (db, a, FALSE);
	rhythmdb_commit (db);
	wait_for_signal ();

	/*fail_unless (_get_property_count (propmodel, _("All")) == 2);*/
	fail_unless (_get_property_count (propmodel, "x") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* remove one */
	set_waiting_signal (G_OBJECT (propmodel), "pre-row-deletion");
	rhythmdb_query_model_remove_entry (model, a);
	wait_for_signal ();
	/*fail_unless (_get_property_count (propmodel, _("All")) == 1);*/
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* switch model */
	model2 = rhythmdb_query_model_new_empty (db);
	g_object_set (model2, "show-hidden", FALSE, NULL);
	rhythmdb_query_model_add_entry (model2, a, -1);
	rhythmdb_query_model_add_entry (model2, b, -1);
	set_waiting_signal (G_OBJECT (propmodel), "row-inserted");
	g_object_set (propmodel, "query-model", model2, NULL);
	wait_for_signal ();

	fail_unless (_get_property_count (propmodel, "x") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* delete an entry */
	set_waiting_signal (G_OBJECT (db), "entry_deleted");
	rhythmdb_entry_delete (db, a);
	rhythmdb_commit (db);
	wait_for_signal ();
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* and the other */
	set_waiting_signal (G_OBJECT (propmodel), "row-deleted");
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);
	wait_for_signal ();
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (model2);
	g_object_unref (propmodel);
}
END_TEST

/* tests property models attached to query models with an actual query */
START_TEST (test_rhythmdb_property_model_query)
{
	RhythmDBQueryModel *model;
	RhythmDBQueryModel *model2;
	RhythmDBPropertyModel *propmodel;
	RhythmDBEntry *a, *b;
	GPtrArray *query;

	start_test_case ();

	/* setup */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_ARTIST, "x",
				      RHYTHMDB_QUERY_END);

	model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_ARTIST, "y",
				      RHYTHMDB_QUERY_END);
	model2 = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);


	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ARTIST);
	g_object_set (propmodel, "query-model", model, NULL);

	end_step ();

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///a.ogg");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///b.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	wait_for_signal ();

	fail_unless (_get_property_count (propmodel, "x") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* change b so it matches the query */
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 2);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* change b again */
	set_entry_string (db, b, RHYTHMDB_PROP_ARTIST, "xx");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 1);
	fail_unless (_get_property_count (propmodel, "xx") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* hide a */
	set_entry_hidden (db, a, TRUE);
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "xx") == 1);

	end_step ();

	/* change a */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "xx");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "xx") == 1);

	end_step ();

	/* unhide a */
	set_entry_hidden (db, a, FALSE);
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "xx") == 2);

	end_step ();

	/* change a -> y */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "y");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "xx") == 1);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_step ();

	/* switch to model2 */
	g_object_set (propmodel, "query-model", model2, NULL);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);
	fail_unless (_get_property_count (propmodel, "xx") == 0);

	end_step ();

	/* change a -> x */
	set_entry_string (db, a, RHYTHMDB_PROP_ARTIST, "x");
	rhythmdb_commit (db);
	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "xx") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 0);

	end_step ();

	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (model2);
	g_object_unref (propmodel);
}
END_TEST

/* tests property models attached to chained query models */
START_TEST (test_rhythmdb_property_model_query_chain)
{
	RhythmDBQueryModel *base_model;
	RhythmDBQueryModel *model;
	RhythmDBPropertyModel *propmodel;
	RhythmDBQuery *query;
	RhythmDBEntry *a, *b;

	start_test_case ();

	/* setup */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_LIKE,
				        RHYTHMDB_PROP_SEARCH_MATCH, "y",
				      RHYTHMDB_QUERY_END);

	base_model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TRACK_NUMBER, 1,
				      RHYTHMDB_QUERY_END);
	model = rhythmdb_query_model_new (db, query, (GCompareDataFunc)rhythmdb_query_model_location_sort_func, NULL, NULL, FALSE);
	rhythmdb_query_free (query);

	rhythmdb_query_model_chain (model, base_model, TRUE);

	propmodel = rhythmdb_property_model_new (db, RHYTHMDB_PROP_ALBUM);
	g_object_set (propmodel, "query-model", model, NULL);

	/* create test entries */
	set_waiting_signal (G_OBJECT (db), "entry_added");
	a = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///a.ogg");
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "x");
	set_entry_ulong (db, a, RHYTHMDB_PROP_TRACK_NUMBER, 1);
	rhythmdb_commit (db);
	wait_for_signal ();

	set_waiting_signal (G_OBJECT (db), "entry_added");
	b = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_SONG, "file:///b.ogg");
	set_entry_string (db, b, RHYTHMDB_PROP_ALBUM, "y");
	set_entry_ulong (db, b, RHYTHMDB_PROP_TRACK_NUMBER, 1);
	rhythmdb_commit (db);
	wait_for_signal ();

	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);

	end_step ();

	/* change entry a so it matches the child query */
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "yy");
	rhythmdb_commit (db);

	fail_unless (_get_property_count (propmodel, "x") == 0);
	fail_unless (_get_property_count (propmodel, "y") == 1);
	fail_unless (_get_property_count (propmodel, "yy") == 1);

	end_step ();

	/* change entry a again */
	set_entry_string (db, a, RHYTHMDB_PROP_ALBUM, "y");
	rhythmdb_commit (db);

	fail_unless (_get_property_count (propmodel, "y") == 2);
	fail_unless (_get_property_count (propmodel, "yy") == 0);

	end_step ();

	/* change entry b again */
	set_entry_string (db, b, RHYTHMDB_PROP_ALBUM, "z");
	rhythmdb_commit (db);

	fail_unless (_get_property_count (propmodel, "y") == 1);
	fail_unless (_get_property_count (propmodel, "z") == 0);

	end_step ();

	rhythmdb_entry_delete (db, a);
	rhythmdb_entry_delete (db, b);
	rhythmdb_commit (db);

	end_test_case ();

	g_object_unref (model);
	g_object_unref (base_model);
	g_object_unref (propmodel);
}
END_TEST


static Suite *
rhythmdb_property_model_suite (void)
{
	Suite *s = suite_create ("rhythmdb-property-model");
	TCase *tc_chain = tcase_create ("rhythmdb-property-model-core");
	TCase *tc_bugs = tcase_create ("rhythmdb-property-model-bugs");

	suite_add_tcase (s, tc_chain);
	tcase_add_checked_fixture (tc_chain, test_rhythmdb_setup, test_rhythmdb_shutdown);
	suite_add_tcase (s, tc_bugs);
	tcase_add_checked_fixture (tc_bugs, test_rhythmdb_setup, test_rhythmdb_shutdown);

	/* test core functionality */
	tcase_add_test (tc_chain, test_rhythmdb_property_model_static);
	tcase_add_test (tc_chain, test_rhythmdb_property_model_query);
	tcase_add_test (tc_chain, test_rhythmdb_property_model_query_chain);

	/* tests for breakable bug fixes */
/*	tcase_add_test (tc_bugs, test_hidden_chain_filter);*/

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	/* init stuff */
	rb_profile_start ("rhythmdb-property-model test suite");

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
	s = rhythmdb_property_model_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);


	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();
	gnome_vfs_shutdown ();

	rb_profile_end ("rhythmdb-property-model test suite");
	return ret;
}

