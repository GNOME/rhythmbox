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

#include <check.h>
#include <gtk/gtk.h>

#include "test-utils.h"
#include "rhythmdb.h"
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-util.h"

static gboolean init_in_tests;
static int argc_;
static char **argv_;

void
init_once (gboolean test)
{
	if (test != init_in_tests)
		return;

	gtk_init (&argc_, &argv_);
}

void
init_setup (SRunner *runner, int argc, char **argv)
{
	init_in_tests = (srunner_fork_status (runner) == CK_FORK);
	argc_ = argc;
	argv_ = argv;
}

void
start_test_case (void)
{
	fprintf (stderr, "\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
}

void
end_step (void)
{
	while (gtk_events_pending ())
		gtk_main_iteration_do (FALSE);
	fprintf (stderr, "----------------------------------------------------------------\n");
}

void
end_test_case (void)
{
	fprintf (stderr, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

gboolean waiting, signaled;
char *sig_name;
gulong sig_handler;
GObject *sig_object;

static void
mark_signal (void)
{
	g_signal_handler_disconnect (sig_object, sig_handler);
	sig_object = NULL;
	sig_handler = 0;

	if (signaled) {
		rb_debug ("got signal '%s' multiple times", sig_name);
	} else {
		rb_debug ("got signal '%s'", sig_name);
		signaled = TRUE;
		if (waiting)
			gtk_main_quit ();
	}
}

gulong
set_waiting_signal_with_callback (GObject *o, const char *name, GCallback callback, gpointer data)
{
	gulong custom_sig_handler;
	custom_sig_handler = g_signal_connect (o, name, callback, data);
	set_waiting_signal (o, name);
	return custom_sig_handler;
}

void
set_waiting_signal (GObject *o, const char *name)
{
	signaled = FALSE;
	waiting = FALSE;
	sig_name = g_strdup (name);
	sig_object = o;
	sig_handler = g_signal_connect (o, sig_name, G_CALLBACK (mark_signal), NULL);
}

void
wait_for_signal (void)
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


/* common setup and teardown */
RhythmDB *db = NULL;
gboolean waiting_db, finalised_db;

void
test_rhythmdb_setup (void)
{
	RhythmDBEntryTypeClass *etype_class;

	init_once (TRUE);

	db = rhythmdb_tree_new ("test");
	ck_assert_msg (db != NULL, "failed to initialise DB");
	rhythmdb_start_action_thread (db);

	/* allow songs and ignored entries to be synced to for the tests */
	etype_class = RHYTHMDB_ENTRY_TYPE_GET_CLASS (RHYTHMDB_ENTRY_TYPE_SONG);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc)rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc)rb_null_function;

	etype_class = RHYTHMDB_ENTRY_TYPE_GET_CLASS (RHYTHMDB_ENTRY_TYPE_IGNORE);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc)rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc)rb_null_function;
}

static gboolean
idle_unref (gpointer data)
{
	g_object_unref (data);
	return FALSE;
}

void
test_rhythmdb_shutdown (void)
{
	ck_assert_msg (db != NULL, "failed to shutdown DB");
	rhythmdb_shutdown (db);

	/* release the reference, and wait until after finalisation */
	g_object_weak_ref (G_OBJECT (db), (GWeakNotify)gtk_main_quit, NULL);
	g_idle_add (idle_unref, db);
	gtk_main ();
	db = NULL;
}

void
set_entry_string (RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType prop, const char *value)
{
	GValue v = {0,};

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, value);
	rhythmdb_entry_set (db, entry, prop, &v);
	g_value_unset (&v);
}

void
set_entry_ulong (RhythmDB *db, RhythmDBEntry *entry, RhythmDBPropType prop, gulong value)
{
	GValue v = {0,};

	g_value_init (&v, G_TYPE_ULONG);
	g_value_set_ulong (&v, value);
	rhythmdb_entry_set (db, entry, prop, &v);
	g_value_unset (&v);
}

void
set_entry_hidden (RhythmDB *db, RhythmDBEntry *entry, gboolean hidden)
{
	GValue v = {0,};

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, hidden);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_HIDDEN, &v);
	g_value_unset (&v);
}


