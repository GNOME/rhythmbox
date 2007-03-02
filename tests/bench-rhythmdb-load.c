#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#include "rhythmdb.h"
#include "rhythmdb-tree.h"

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
		g_print ("using ~/.gnome2/rhythmbox/rhythmdb.xml\n");
		name = g_strdup_printf ("%s/rhythmdb.xml", rb_dot_dir ());
	} else {
		name = g_strdup (argv[1]);
	}

	rb_profile_start ("load test");

	g_thread_init (NULL);
	rb_threads_init ();
	gtk_set_locale ();
	gtk_init (&argc, &argv);
	gnome_vfs_init ();
	rb_debug_init (FALSE);
	rb_refstring_system_init ();
	rb_file_helpers_init ();

	GDK_THREADS_ENTER ();

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
        gnome_vfs_shutdown ();


	rb_profile_end ("load test");
	return 0;
}
