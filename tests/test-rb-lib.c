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
	ck_assert_msg (rb_string_value_map_size (map) == 0, "new map should have 0 entries");

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, 42);
	rb_string_value_map_set (map, "foo", &val);
	g_value_unset (&val);
	ck_assert_msg (rb_string_value_map_size (map) == 1, "map with 1 entry added should have 1 entry");

	ck_assert_msg (rb_string_value_map_get (map, "foo", &val), "couldn't retrieve just-added entry");
	ck_assert_msg (g_value_get_int (&val) == 42, "entry returned wrong value");
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, "BAZ");
	rb_string_value_map_set (map, "bar", &val);
	g_value_unset (&val);
	ck_assert_msg (rb_string_value_map_size (map) == 2, "map with 2 entries added should have 2 entries");

	ck_assert_msg (rb_string_value_map_get (map, "foo", &val), "couldn't retrieve entry");
	ck_assert_msg (g_value_get_int (&val) == 42, "entry returned wrong value");
	g_value_unset (&val);

	ck_assert_msg (strcmp (g_value_get_string (rb_string_value_map_peek (map, "bar")), "BAZ") == 0,
		       "wrong string returned");

	ck_assert_msg (rb_string_value_map_get (map, "wombat", &val) == FALSE, "could retrieve non-existant entry");

	rb_string_value_map_remove (map, "foo");
	ck_assert_msg (rb_string_value_map_size (map) == 1, "map with second entry removed should have 1 entry");

	ck_assert_msg (strcmp (g_value_get_string (rb_string_value_map_peek (map, "bar")), "BAZ") == 0,
		       "wrong string returned");

	rb_string_value_map_remove (map, "bar");
	ck_assert_msg (rb_string_value_map_size (map) == 0, "map with both entries removed should have 0 entries");

	ck_assert_msg (rb_string_value_map_peek (map, "bar") == NULL, "removed entry should return NULL");
}
END_TEST

static Suite *
rb_file_helpers_suite (void)
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
	rb_threads_init ();
	rb_debug_init (TRUE);

	/* setup tests */
	s = rb_file_helpers_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_profile_end ("rb-file-helpers test suite");
	return ret;
}

