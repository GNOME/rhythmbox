/* 
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 */

#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "rb-debug.h"
#include "rb-thread-helpers.h"
#include "rb-file-helpers.h"
#include "rb-entry-view.h"

static RhythmDBEntry *
create_entry (RhythmDB *db,
	      const char *location, const char *name, const char *album,
	      const char *artist, const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = {0, };

	entry = rhythmdb_entry_new (db, RHYTHMDB_ENTRY_TYPE_IGNORE, location);
	g_assert (entry);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, genre);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, artist);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, album);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, name);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	return entry;
}

static void
completed_cb (RhythmDBQueryModel *model, gboolean *complete)
{
	rb_debug ("query complete");
	*complete = TRUE;
}

static void
wait_for_model_completion (RhythmDBQueryModel *model)
{
	gboolean complete = FALSE;
	GTimeVal timeout;
	g_signal_connect (G_OBJECT (model), "complete",
			  G_CALLBACK (completed_cb), &complete);

	while (!complete) {
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC);

		rb_debug ("polling model for changes");
		rhythmdb_query_model_sync (model, &timeout);
		g_usleep (G_USEC_PER_SEC / 10.0);
	}
}

int
main (int argc, char **argv)
{
	GtkWidget *main_window;
	GtkTreeModel *main_model;
	GtkTreeIter iter;
	RBEntryView *view;
	RhythmDB *db;
	RhythmDBEntry *entry;

	gtk_init (&argc, &argv);
	rb_thread_helpers_init ();
	rb_file_helpers_init ();
	rb_stock_icons_init ();
	rb_debug_init (TRUE);

	db = rhythmdb_tree_new ("test");

	rhythmdb_write_lock (db);

	entry = create_entry (db, "file:///sin.mp3",
			      "Sin", "Pretty Hate Machine", "Nine Inch Nails", "Rock");
	
	rhythmdb_write_unlock (db);

	rhythmdb_read_lock (db);

	main_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, main_model,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_IGNORE,
				RHYTHMDB_QUERY_END);

	wait_for_model_completion (RHYTHMDB_QUERY_MODEL (main_model));
	g_assert (gtk_tree_model_get_iter_first (main_model, &iter));

	main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	view = rb_entry_view_new (db, rb_file ("rb-entry-view-library.xml"));

	rb_entry_view_set_query_model (view, RHYTHMDB_QUERY_MODEL (main_model));

	gtk_container_add (GTK_CONTAINER (main_window), GTK_WIDGET (view));

	g_signal_connect (G_OBJECT (main_window), "destroy",
			  G_CALLBACK (gtk_main_quit), NULL);

	gtk_widget_show_all (GTK_WIDGET (main_window));

	gtk_main ();
	
	rhythmdb_shutdown (db);
	g_object_unref (G_OBJECT (db));
	
	exit (0);
}
