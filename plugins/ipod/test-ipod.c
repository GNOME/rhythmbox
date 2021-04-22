/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2021 The Rhythmbox authors
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

#include <locale.h>

#include "rb-util.h"
#include "rb-debug.h"
#include "rb-ipod-helpers.h"

typedef struct {
	const char *uri;
	AfcUriStatus status;
} AfcTestData;

AfcTestData afc_test_data[] = {
	{ "afc://aaaaaaaaaa1234567890bbbb:1", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbb:10", AFC_URI_PORT_UNKNOWN },
	{ "afc://aaaaaaaaaa1234567890bbbb:3", AFC_URI_NOT_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbb:", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbb", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbb/", AFC_URI_IS_IPOD },
	{ "afc://12345678-AAAAAAAAAAAAAAAA:1/", AFC_URI_IS_IPOD },
	{ "afc://12345678-AAAAAAAAAAAAAAAA:1", AFC_URI_IS_IPOD },
	{ "afc://12345678-AAAAAAAAAAAAAAAA:10/", AFC_URI_PORT_UNKNOWN },
	{ "afc://12345678-AAAAAAAAAAAAAAAA:3", AFC_URI_NOT_IPOD },
	{ "afc://12345678-AAAAAAAAAAAAAAAA:3/", AFC_URI_NOT_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890:1", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890:10", AFC_URI_PORT_UNKNOWN },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890:3", AFC_URI_NOT_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890:", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb1234567890/", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901:1", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901:10", AFC_URI_PORT_UNKNOWN },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901:3", AFC_URI_NOT_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901:", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901", AFC_URI_IS_IPOD },
	{ "afc://aaaaaaaaaa1234567890bbbbbbbbbb12345678901/", AFC_URI_IS_IPOD }
};

static void
test_afc_uri_is_ipod (void)
{
	int i;
	AfcUriStatus status;

	for (i = 0; i < G_N_ELEMENTS (afc_test_data); i++) {
		status = rb_ipod_helpers_afc_uri_parse (afc_test_data[i].uri);
		if (status != afc_test_data[i].status) {
			g_test_message ("URI %s returned %d (expected: %d)",
					afc_test_data[i].uri,
					status,
					afc_test_data[i].status);
		}
		g_assert_cmpint (status, ==, afc_test_data[i].status);
	}
}

int
main (int argc, char **argv)
{
	int ret;

	/* init stuff */
	rb_profile_start ("rb-ipod-helpers test suite");

	rb_threads_init ();
	setlocale (LC_ALL, "");
	rb_debug_init (TRUE);
	g_test_init (&argc, &argv, NULL);

	g_test_bug_base ("https://gitlab.gnome.org/GNOME/rhythmbox/");
	g_test_add_func ("/ipod/uri", test_afc_uri_is_ipod);

	ret = g_test_run ();

	rb_profile_end ("rb-ipod-helpers test suite");

	return ret;
}
