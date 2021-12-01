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

#include <check.h>
#include <gtk/gtk.h>
#include <locale.h>
#include "test-utils.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-debug.h"

static void
test_get_short_path_name (const char *in, const char *expected)
{
	char *out;

	out = rb_uri_get_short_path_name (in);
	rb_debug ("extracting short path from \"%s\", expecting \"%s\", got \"%s\"", in, expected, out);
	ck_assert (strcmp (out, expected) == 0);
	g_free (out);
}

START_TEST (test_rb_uri_get_short_path_name)
{
	char *in;
	char *out;

	init_once (TRUE);

	/* nothing */
	in = NULL;
	out = rb_uri_get_short_path_name (in);
	ck_assert (out == NULL);
	g_free (out);

	/* just a file name */
	test_get_short_path_name ("something.ogg", "something.ogg");

	/* relative file name */
	test_get_short_path_name ("x/something.ogg", "something.ogg");

	/* full path name */
	test_get_short_path_name ("/var/lib/something.ogg", "something.ogg");

	/* URI with a single path component */
	test_get_short_path_name ("file://something.ogg", "something.ogg");

	/* URI with multiple path components */
	test_get_short_path_name ("file:///home/nobody/something.ogg", "something.ogg");

	/* URI with query string */
	test_get_short_path_name ("http://example.com/something.ogg?q=z&h=w", "something.ogg");

	/* non-standard URI protocol */
	test_get_short_path_name ("daap://10.0.0.1:3523/databases/1/items/46343.ogg", "46343.ogg");

	/* non-standard URI protocol with query string */
	test_get_short_path_name ("daap://10.0.0.1:3523/databases/1/items/46383.ogg?session=2463435", "46383.ogg");

	/* trailing slash */
	test_get_short_path_name ("/usr/share/nothing/", "nothing");
}
END_TEST

START_TEST (test_rb_check_dir_has_space)
{
	init_once (TRUE);
	ck_assert (rb_check_dir_has_space_uri ("file:///tmp", 1));
	ck_assert (rb_check_dir_has_space_uri ("file:///etc/passwd", 1));
	ck_assert (rb_check_dir_has_space_uri ("file:///tmp/NONEXISTANT_FILE", 1));
	ck_assert (rb_check_dir_has_space_uri ("file:///tmp/NONEXISTANT/THISDOESNTEXISTEITHER/NORDOESTHIS", G_MAXUINT64) == FALSE);
}
END_TEST

START_TEST (test_rb_uri_is_descendant)
{
	ck_assert (rb_uri_is_descendant ("file:///tmp", "file:///"));
	ck_assert (rb_uri_is_descendant ("file:///tmp/2", "file:///"));
	ck_assert (rb_uri_is_descendant ("file:///tmp/2", "file:///tmp"));
	ck_assert (rb_uri_is_descendant ("file:///tmp/2", "file:///tmp/"));
	ck_assert (rb_uri_is_descendant ("file:///tmp/", "file:///tmp") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp", "file:///tmp/") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp/", "file:///tmp/") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp", "file:///tmp") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp2", "file:///tmp") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp/2", "file:///tmp2") == FALSE);
	ck_assert (rb_uri_is_descendant ("file:///tmp/22", "file:///tmp/2") == FALSE);
}
END_TEST

static Suite *
rb_file_helpers_suite (void)
{
	Suite *s = suite_create ("rb-file-helpers");
	TCase *tc_chain = tcase_create ("rb-file-helpers-core");

	suite_add_tcase (s, tc_chain);

	tcase_add_test (tc_chain, test_rb_uri_get_short_path_name);
	tcase_add_test (tc_chain, test_rb_check_dir_has_space);
	tcase_add_test (tc_chain, test_rb_uri_is_descendant);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	rb_profile_start ("rb-file-helpers test suite");
	rb_threads_init ();
	setlocale (LC_ALL, "");
	rb_debug_init (TRUE);
	rb_file_helpers_init ();

	/* setup tests */
	s = rb_file_helpers_suite ();
	sr = srunner_create (s);

	init_setup (sr, argc, argv);
	init_once (FALSE);

	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_file_helpers_shutdown ();

	rb_profile_end ("rb-file-helpers test suite");
	return ret;
}
