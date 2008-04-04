#include "config.h"

#include <string.h>
#include <glib-object.h>

#include <check.h>
#include "test-utils.h"
#include "rb-audioscrobbler-entry.h"
#include "rb-debug.h"
#include "rb-util.h"

START_TEST (test_rb_audioscrobbler_entry)
{
	AudioscrobblerEntry *entry;
	AudioscrobblerEntry *reload;
	char *as_string;

	entry = g_new0(AudioscrobblerEntry, 1);
	entry->title = g_strdup ("something or other");
	entry->artist = g_strdup ("someone & someone else");
	entry->album = g_strdup ("unknown + other things");
	entry->length = 11;
	entry->mbid = g_strdup ("");		/* ? */
	entry->play_time = time (0);

	as_string = rb_audioscrobbler_entry_save_to_string (entry);
	rb_debug ("string form: %s", as_string);
	fail_unless (strlen (as_string) != 0, "entry saved as string should not be empty");

	reload = rb_audioscrobbler_entry_load_from_string (as_string);
	fail_unless (reload != NULL, "entry-as-string can be converted back to an entry");

	rb_audioscrobbler_entry_debug (entry, 0);
	rb_audioscrobbler_entry_debug (reload, 1);
	fail_unless (strcmp (entry->title, reload->title) == 0, "title made it back OK");
	fail_unless (strcmp (entry->artist, reload->artist) == 0, "artist made it back OK");
	fail_unless (strcmp (entry->album, reload->album) == 0, "album made it back OK");
	fail_unless (strcmp (entry->mbid, reload->mbid) == 0, "album made it back OK");
	fail_unless (entry->length == reload->length, "length made it back OK");
	fail_unless (entry->play_time == reload->play_time, "play time made it back OK");

	rb_audioscrobbler_entry_free (entry);
	rb_audioscrobbler_entry_free (reload);
	g_free (as_string);
}
END_TEST

static Suite *
rb_audioscrobbler_suite ()
{
	Suite *s = suite_create ("rb-audioscrobbler");
	TCase *tc_chain = tcase_create ("rb-audioscrobbler-entry");

	suite_add_tcase (s, tc_chain);

	tcase_add_test (tc_chain, test_rb_audioscrobbler_entry);

	return s;
}

int
main (int argc, char **argv)
{
	int ret;
	SRunner *sr;
	Suite *s;

	rb_profile_start ("rb-audioscrobbler test suite");
	g_thread_init (NULL);
	rb_threads_init ();
	g_type_init ();
	rb_debug_init (TRUE);

	GDK_THREADS_ENTER ();

	/* setup tests */
	s = rb_audioscrobbler_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_profile_end ("rb-audioscrobbler test suite");
	return ret;
}

