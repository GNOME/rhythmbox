/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007 James Livingston
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <gtk/gtk.h>
#include <string.h>
#include <locale.h>

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#include "rhythmdb.h"
#include "rhythmdb-tree.h"
#include "rb-podcast-entry-types.h"

/* test utils */
gboolean waiting, signaled;
char *sig_name;


static void
mark_signal (void)
{
	if (signaled) {
		rb_debug ("got signal '%s' multiple times", sig_name);
	} else {
		rb_debug ("got signal '%s'", sig_name);
		signaled = TRUE;
		if (waiting)
			gtk_main_quit ();
	}
}

static void
set_waiting_signal (GObject *o, const char *name)
{
	signaled = FALSE;
	waiting = FALSE;
	sig_name = g_strdup (name);
	g_signal_connect (o, sig_name, G_CALLBACK (mark_signal), NULL);
}

static void wait_for_signal (void)
{
	if (!signaled) {
		rb_debug ("waiting for signal '%s'", sig_name);
		waiting = TRUE;
		gtk_main ();
	} else {
		rb_debug ("no need to wait for signal '%s', already received", sig_name);
	}

	g_free (sig_name);
	sig_name = NULL;
}


int 
main (int argc, char **argv)
{
	RhythmDB *db;
	char *name;
	int i;

	if (argc < 2) {
		name = g_build_filename (rb_user_data_dir(), "rhythmdb.xml", NULL);
		g_print ("using %s\n", name);
	} else {
		name = g_strdup (argv[1]);
	}

	rb_profile_start ("load test");

	rb_threads_init ();
	setlocale (LC_ALL, "");
	gtk_init (&argc, &argv);
	rb_debug_init (FALSE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();

	db = rhythmdb_tree_new ("test");
	g_object_set (G_OBJECT (db), "name", name, NULL);
	g_free (name);

	for (i = 1; i <= 10; i++) {
		int j;
		rb_profile_start ("10 rhythmdb loads");
		for (j = 1; j <= 10; j++) {
			set_waiting_signal (G_OBJECT (db), "load-complete");
			rhythmdb_load (db);
			wait_for_signal ();

			rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_SONG);
			rhythmdb_entry_delete_by_type (db, rhythmdb_entry_type_get_by_name (db, "iradio"));
			rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED);
			rhythmdb_entry_delete_by_type (db, RHYTHMDB_ENTRY_TYPE_PODCAST_POST);
		}
		rb_profile_end ("10 rhythmdb loads");
		g_print ("completed %d loads\n", i * 10);
	}

	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	db = NULL;
	
	rb_file_helpers_shutdown ();
        rb_refstring_system_shutdown ();

	rb_profile_end ("load test");
	return 0;
}
