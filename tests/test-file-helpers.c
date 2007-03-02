#include "config.h"

#include <string.h>

#include <check.h>
#include <gtk/gtk.h>
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
	fail_unless (strcmp (out, expected) == 0);
	g_free (out);
}

START_TEST (test_rb_uri_get_short_path_name)
{
	char *in;
	char *out;

	/* nothing */
	in = NULL;
	out = rb_uri_get_short_path_name (in);
	fail_unless (out == NULL);
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

static Suite *
rb_file_helpers_suite ()
{
	Suite *s = suite_create ("rb-file-helpers");
	TCase *tc_chain = tcase_create ("rb-file-helpers-core");

	suite_add_tcase (s, tc_chain);

	tcase_add_test (tc_chain, test_rb_uri_get_short_path_name);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	rb_profile_start ("rb-file-helpers test suite");
	g_thread_init (NULL);
	rb_threads_init ();
	gtk_set_locale ();
	gtk_init (&argc, &argv);
	gnome_vfs_init ();
	rb_debug_init (TRUE);
	rb_file_helpers_init ();

	GDK_THREADS_ENTER ();

	/* setup tests */
	s = rb_file_helpers_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_file_helpers_shutdown ();
	gnome_vfs_shutdown ();

	rb_profile_end ("rb-file-helpers test suite");
	return ret;
}
