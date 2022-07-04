#include <check.h>
#include <gtk/gtk.h>
#include <string.h>

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#include "rhythmdb.h"
#include "rhythmdb-tree.h"

#include "rb-query-creator.h"

#include "test-utils.h"

static gboolean
rhythmdb_query_equal (const RhythmDBQuery *q1, const RhythmDBQuery *q2)
{
	int i;

	if (q1 == NULL && q2 == NULL)
		return TRUE;
	else if (q1 == NULL || q2 == NULL)
		return FALSE;

	if (q1->len != q2->len)
		return FALSE;

	for (i = 0; i < q1->len; i++) {
		RhythmDBQueryData *data1;
		RhythmDBQueryData *data2;

		data1 = g_ptr_array_index (q1, i);
		data2 = g_ptr_array_index (q2, i);

		if (data1->type != data2->type)
			return FALSE;
		if (data1->propid != data2->propid)
			return FALSE;
		if ((data1->val == NULL && data2->val != NULL) ||
		    (data1->val != NULL && data2->val == NULL))
			return FALSE;
		if ((data1->val != NULL && data2->val != NULL) &&
		    (rb_gvalue_compare (data1->val, data2->val) != 0))
			return FALSE;
		if (!rhythmdb_query_equal (data1->subquery, data2->subquery))
			return FALSE;
	}

	return TRUE;
}


static void
query_creator_test_load_query (RhythmDB *db,
			       RhythmDBQuery *query,
			       RhythmDBQueryModelLimitType limit_type,
			       GVariant *limit_value,
			       const char *sort_column,
			       gint sort_direction)
{
	GtkWidget *creator;
	RhythmDBQuery *squery;
	RhythmDBQuery *query2 = NULL;
	GVariant *limit_value2 = NULL;
	const char *sort_column2 = NULL;
	RhythmDBQueryModelLimitType limit_type2;
	gint sort_direction2;
	char *str1, *str2;

	squery = rhythmdb_query_parse (db,
				       RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_SONG,
				       RHYTHMDB_QUERY_SUBQUERY, query,
				       RHYTHMDB_QUERY_END);

	creator = rb_query_creator_new_from_query (db,
						   squery,
						   limit_type, limit_value,
						   sort_column, sort_direction);

	str1 = rhythmdb_query_to_string (db, squery);
	ck_assert_msg (creator != NULL,
		       "could not create query editor for %s", str1);

	/* check queries */
	query2 = rb_query_creator_get_query (RB_QUERY_CREATOR (creator));
	str2 = rhythmdb_query_to_string (db, query2);
	ck_assert_msg (rhythmdb_query_equal (squery, query2),
		       "queries differ: %s; %s", str1, str2);
	rhythmdb_query_free (query2);
	g_free (str2);
	g_free (str1);

	/* check limits */
	rb_query_creator_get_limit (RB_QUERY_CREATOR (creator),
				    &limit_type2, &limit_value2);
	ck_assert_msg (limit_type == limit_type2,
		       "limit types differ: %d; %d", limit_type, limit_type2);
	if (limit_value == NULL) {
		ck_assert_msg (limit_value == limit_value2, "wasn't supposed to get a limit value");
	} else {
		ck_assert_msg (g_variant_equal (limit_value, limit_value2),
			       "limit values differ: %s; %s", g_variant_print (limit_value, TRUE), g_variant_print (limit_value2, TRUE));
	}
	if (limit_value2)
		g_variant_unref (limit_value2);

	/* check sorting */
	rb_query_creator_get_sort_order (RB_QUERY_CREATOR (creator),
					 &sort_column2, &sort_direction2);
	ck_assert_msg (strcmp (sort_column2, sort_column) == 0,
		       "sort columns differ: %s; %s", sort_column, sort_column2);
	ck_assert_msg (sort_direction2 == sort_direction,
		       "sort directions differ: %d; %d", sort_direction, sort_direction2);

	rhythmdb_query_free (squery);
	gtk_widget_destroy (creator);
}

START_TEST (test_query_creator_load_query_empty)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_query_simple)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TITLE, "foo",
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_query_multiple)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_ARTIST_FOLDED, "bar",
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_ARTIST_FOLDED, "bar",
				      RHYTHMDB_QUERY_PROP_LESS, RHYTHMDB_PROP_DURATION, 47UL,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_query_disjunction)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_YEAR_EQUALS, RHYTHMDB_PROP_DATE, 729025/*1997*/,
				      RHYTHMDB_QUERY_DISJUNCTION,
				      RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN, RHYTHMDB_PROP_LAST_PLAYED, 500,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_count)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_COUNT, g_variant_new_uint64 (47),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_minutes)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_TIME, g_variant_new_uint64 (37 * 60),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_hours)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_TIME, g_variant_new_uint64 (41 * 60 * 60),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_days)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_TIME, g_variant_new_uint64 (13 * 60 * 60 * 24),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_mb)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_SIZE, g_variant_new_uint64 (13),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_limit_gb)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_SIZE, g_variant_new_uint64 (14 * 1000),
				       "Title", GTK_SORT_ASCENDING);
	rhythmdb_query_free (query);
}
END_TEST

START_TEST (test_query_creator_load_sort_artist_dec)
{
	RhythmDBQuery *query;

	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_END);
	query_creator_test_load_query (db,
				       query,
				       RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
				       "Artist", GTK_SORT_DESCENDING);
	rhythmdb_query_free (query);
}
END_TEST

static Suite *
rb_query_creator_suite (void)
{
	Suite *s = suite_create ("RBQueryCreator");
	TCase *tc_qls = tcase_create ("query_load-save");

	/* test loading and retrieving various queries,
	 * ensuring the result is identical to the original
	 */
	suite_add_tcase (s, tc_qls);
	tcase_add_checked_fixture (tc_qls, test_rhythmdb_setup, test_rhythmdb_shutdown);
	tcase_add_test (tc_qls, test_query_creator_load_query_empty);
	tcase_add_test (tc_qls, test_query_creator_load_query_simple);
	tcase_add_test (tc_qls, test_query_creator_load_query_multiple);
	tcase_add_test (tc_qls, test_query_creator_load_query_disjunction);
	tcase_add_test (tc_qls, test_query_creator_load_limit_count);
	tcase_add_test (tc_qls, test_query_creator_load_limit_minutes);
	tcase_add_test (tc_qls, test_query_creator_load_limit_hours);
	tcase_add_test (tc_qls, test_query_creator_load_limit_days);
	tcase_add_test (tc_qls, test_query_creator_load_limit_mb);
	tcase_add_test (tc_qls, test_query_creator_load_limit_gb);
	tcase_add_test (tc_qls, test_query_creator_load_sort_artist_dec);

	return s;
}
	
int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	g_log_set_always_fatal (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);

	rb_threads_init ();
	rb_debug_init (TRUE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();

	/* setup tests */
	s = rb_query_creator_suite ();
	sr = srunner_create (s);

	init_setup (sr, argc, argv);
	init_once (FALSE);
	
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_file_helpers_shutdown ();
	rb_refstring_system_shutdown ();

	return ret;
}
