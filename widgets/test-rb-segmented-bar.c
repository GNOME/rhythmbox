/*
 *  Copyright (C) 2008 Christophe Fergeau <teuf@gnome.org>
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

/* compile with:
 gcc -o test-rb-segmented-bar -I. $(pkg-config --cflags --libs gtk+-2.0)
    ./rb-segmented-bar.c ./test-rb-segmented-bar.c
 */
#include <gtk/gtk.h>
#include <rb-segmented-bar.h>

static void window_destroyed_cb (void)
{
    	gtk_main_quit ();
}

static gchar *value_formatter (gdouble percent, gpointer data)
{
	gsize total_size = GPOINTER_TO_SIZE (data);

	return g_format_size (percent * total_size*1024*1024*1024);
}

int main (int argc, char **argv)
{
	GtkWidget *window;
	RBSegmentedBar *bar;
	const gsize total_size = 100; /* in GB */

	gtk_init (&argc, &argv);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	bar = RB_SEGMENTED_BAR (rb_segmented_bar_new ());
	g_object_set (G_OBJECT (bar),
		      "show-reflection", TRUE,
		      "show-labels", TRUE,
		      NULL);

	rb_segmented_bar_add_segment (bar, "audio", 0.61, 0.2 , 0.4 , 0.65, 1);
	rb_segmented_bar_add_segment (bar, "video", 0.11, 0.96, 0.47, 0   , 1);
	rb_segmented_bar_add_segment (bar, "other", 0.05, 0.45, 0.82, 0.08, 1);
	rb_segmented_bar_add_segment_default_color (bar, "empty", 0.23);
	rb_segmented_bar_set_value_formatter (bar, value_formatter,
					      GSIZE_TO_POINTER (total_size));

	g_signal_connect(G_OBJECT (window), "destroy",
			 G_CALLBACK (window_destroyed_cb), NULL);
	gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (bar));
	gtk_widget_show_all (GTK_WIDGET (window));
	gtk_main ();

	return 0;
}
