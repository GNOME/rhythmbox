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
#include "rb-audioscrobbler-entry.h"
#include "rb-debug.h"
#include "rb-util.h"

START_TEST (test_rb_audioscrobbler_entry)
{
	AudioscrobblerEntry *entry;
	AudioscrobblerEntry *reload;
	GString *as_gstring;

	entry = g_new0(AudioscrobblerEntry, 1);
	entry->title = g_strdup ("something or other");
	entry->artist = g_strdup ("someone & someone else");
	entry->album = g_strdup ("unknown + other things");
	entry->length = 11;
	entry->mbid = g_strdup ("");		/* ? */
	entry->play_time = time (0);

	as_gstring = g_string_new ("");
	rb_audioscrobbler_entry_save_to_string (as_gstring, entry);
	rb_debug ("string form: %s", as_gstring->str);
	ck_assert_msg (as_gstring->len != 0, "entry saved as string should not be empty");

	reload = rb_audioscrobbler_entry_load_from_string (as_gstring->str);
	ck_assert_msg (reload != NULL, "entry-as-string can be converted back to an entry");

	rb_audioscrobbler_entry_debug (entry, 0);
	rb_audioscrobbler_entry_debug (reload, 1);
	ck_assert_msg (strcmp (entry->title, reload->title) == 0, "title made it back OK");
	ck_assert_msg (strcmp (entry->artist, reload->artist) == 0, "artist made it back OK");
	ck_assert_msg (strcmp (entry->album, reload->album) == 0, "album made it back OK");
	ck_assert_msg (strcmp (entry->mbid, reload->mbid) == 0, "album made it back OK");
	ck_assert_msg (entry->length == reload->length, "length made it back OK");
	ck_assert_msg (entry->play_time == reload->play_time, "play time made it back OK");

	rb_audioscrobbler_entry_free (entry);
	rb_audioscrobbler_entry_free (reload);
	g_string_free (as_gstring, TRUE);
}
END_TEST

static Suite *
rb_audioscrobbler_suite (void)
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
	rb_threads_init ();
	rb_debug_init (TRUE);

	/* setup tests */
	s = rb_audioscrobbler_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	ret = srunner_ntests_failed (sr);
	srunner_free (sr);

	rb_profile_end ("rb-audioscrobbler test suite");
	return ret;
}

