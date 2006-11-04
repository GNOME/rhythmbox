
#include <check.h>
#include <gtk/gtk.h>

#include "test-utils.h"
#include "rhythmdb.h"
#include "rhythmdb-tree.h"
#include "rb-debug.h"
#include "rb-util.h"

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
	RhythmDBEntryType entry_type;
	db = rhythmdb_tree_new ("test");
	fail_unless (db != NULL, "failed to initialise DB");
	rhythmdb_start_action_thread (db);

	/* allow SONGs to be synced to for the tests */
	entry_type = RHYTHMDB_ENTRY_TYPE_SONG;
	entry_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc)rb_true_function;
	entry_type->sync_metadata = (RhythmDBEntrySyncFunc)rb_null_function;
	entry_type->check_file_exists = FALSE;
}

void
test_rhythmdb_shutdown (void)
{
	fail_unless (db != NULL, "failed to shutdown DB");
	rhythmdb_shutdown (db);

	/* release the reference, and wait until after finalisation */
	g_object_weak_ref (G_OBJECT (db), (GWeakNotify)gtk_main_quit, NULL);
	g_idle_add ((GSourceFunc)g_object_unref, db);
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


