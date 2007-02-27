#include <string.h>
#include <glib-object.h>

#include <check.h>
#include "test-utils.h"
#include "rb-util.h"
#include "rb-string-value-map.h"
#include "rb-debug.h"

START_TEST (test_rb_string_value_map)
{
	RBStringValueMap *map;
	GValue val = {0,};

	map = rb_string_value_map_new ();
	fail_unless (rb_string_value_map_size (map) == 0, "new map should have 0 entries");

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, 42);
	rb_string_value_map_set (map, "foo", &val);
	g_value_unset (&val);
	fail_unless (rb_string_value_map_size (map) == 1, "map with 1 entry added should have 1 entry");

	fail_unless (rb_string_value_map_get (map, "foo", &val), "couldn't retrieve just-added entry");
	fail_unless (g_value_get_int (&val) == 42, "entry returned wrong value");
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, "BAZ");
	rb_string_value_map_set (map, "bar", &val);
	g_value_unset (&val);
	fail_unless (rb_string_value_map_size (map) == 2, "map with 2 entries added should have 2 entries");

	fail_unless (rb_string_value_map_get (map, "foo", &val), "couldn't retrieve entry");
	fail_unless (g_value_get_int (&val) == 42, "entry returned wrong value");
	g_value_unset (&val);

	fail_unless (strcmp (g_value_get_string (rb_string_value_map_peek (map, "bar")), "BAZ") == 0,
		     "wrong string returned");

	fail_unless (rb_string_value_map_get (map, "wombat", &val) == FALSE, "could retrieve non-existant entry");

	rb_string_value_map_remove (map, "foo");
	fail_unless (rb_string_value_map_size (map) == 1, "map with second entry removed should have 1 entry");

	fail_unless (strcmp (g_value_get_string (rb_string_value_map_peek (map, "bar")), "BAZ") == 0,
		     "wrong string returned");

	rb_string_value_map_remove (map, "bar");
	fail_unless (rb_string_value_map_size (map) == 0, "map with both entries removed should have 0 entries");

	fail_unless (rb_string_value_map_peek (map, "bar") == NULL, "removed entry should return NULL");
}
END_TEST

static Suite *
rb_file_helpers_suite ()
{
	Suite *s = suite_create ("rb-utils");
	TCase *tc_chain = tcase_create ("rb-utils-core");

	suite_add_tcase (s, tc_chain);

	tcase_add_test (tc_chain, test_rb_string_value_map);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	rb_profile_start ("rb-utils test suite");
	g_thread_init (NULL);
	rb_threads_init ();
	g_type_init ();
	rb_debug_init (TRUE);

	GDK_THREADS_ENTER ();

	/* setup tests */
	s = rb_file_helpers_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_profile_end ("rb-file-helpers test suite");
	return ret;
}

