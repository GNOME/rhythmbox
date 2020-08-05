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

#include <gtk/gtk.h>
#include <rb-segmented-bar.h>
#include <rb-sync-state-ui.h>

#define KB (1000ULL)
#define MB (1000 * 1000ULL)
#define GB (1000 * 1000 * 1000ULL)

guint64 total_capacity = 100 * GB;

guint64 music_size = 0;
guint64 podcast_size = 40 * GB;
guint64 other_size = 15 * GB;
guint64 available_size = 0;

gdouble music_fraction = 0.0;
gdouble podcast_fraction = 0.0;
gdouble other_fraction = 0.0;
gdouble available_fraction = 0.0;

guint64 copy_size = 250 * MB;

gboolean toggle_enabled = TRUE;
guint64 toggle_labels_every = 2 * GB;
guint64 toggle_reflection_every = 4 * GB;

static void window_destroyed_cb (void)
{
    	gtk_main_quit ();
}

static gchar *value_formatter (gdouble percent, RBSyncBarData *data)
{
	return g_format_size (percent * data->capacity);
}

static void
update_fractions (void)
{
	music_fraction = (double) music_size / (double) total_capacity;
	podcast_fraction = (double) podcast_size / (double) total_capacity;
	other_fraction = (double) other_size / (double) total_capacity;
	available_fraction = (double) available_size / (double) total_capacity;
}

static gboolean
update_segments (RBSyncBarData *data)
{
	gboolean done = FALSE;
	gboolean flag;

	if (available_size <= copy_size) {
		copy_size = available_size;
		done = TRUE;
	}

	music_size += copy_size;
	available_size -= copy_size;

	update_fractions ();

	if (toggle_enabled && available_size % toggle_labels_every == 0) {
		g_object_get (G_OBJECT (data->widget), "show-labels", &flag, NULL);
		g_object_set (G_OBJECT (data->widget), "show-labels", !flag, NULL);
	}

	if (toggle_enabled && available_size % toggle_reflection_every == 0) {
		g_object_get (G_OBJECT (data->widget), "show-reflection", &flag, NULL);
		g_object_set (G_OBJECT (data->widget), "show-reflection", !flag, NULL);
	}

	if (done) {
		g_object_set (G_OBJECT (data->widget),
			      "show-labels", TRUE,
			      "show-reflection", TRUE,
			      NULL);
	}

	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (data->widget),
					 data->music_segment,
					 music_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (data->widget),
					 data->podcast_segment,
					 podcast_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (data->widget),
					 data->other_segment,
					 other_fraction);
	rb_segmented_bar_update_segment (RB_SEGMENTED_BAR (data->widget),
					 data->free_segment,
					 available_fraction);
	return (!done);
}

int main (int argc, char **argv)
{
	GtkWidget *window;
	RBSegmentedBar *bar;
	RBSyncBarData *data;

	gtk_init (&argc, &argv);

	data = g_new0 (RBSyncBarData, 1);
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	bar = RB_SEGMENTED_BAR (rb_segmented_bar_new ());

	if (g_strcmp0 (argv [1], "--rtl") == 0)
		gtk_widget_set_direction (GTK_WIDGET (bar), GTK_TEXT_DIR_RTL);

	g_assert (music_size + podcast_size + other_size <= total_capacity);
	available_size = (total_capacity - music_size - podcast_size - other_size);
	update_fractions ();

	data->widget = GTK_WIDGET (bar);
	data->capacity = total_capacity;
	data->music_segment = rb_segmented_bar_add_segment (bar, "Music", music_fraction, 0.2 , 0.4 , 0.65, 1);
	data->podcast_segment = rb_segmented_bar_add_segment (bar, "Podcast", podcast_fraction, 0.96, 0.47, 0   , 1);
	data->other_segment = rb_segmented_bar_add_segment (bar, "Other", other_fraction, 0.45, 0.82, 0.08, 1);
	data->free_segment = rb_segmented_bar_add_segment_default_color (bar, "Available", available_fraction);

	rb_segmented_bar_set_value_formatter (bar, (RBSegmentedBarValueFormatter) value_formatter, data);

	g_signal_connect(G_OBJECT (window), "destroy",
			 G_CALLBACK (window_destroyed_cb), NULL);
	g_timeout_add (50, (GSourceFunc) update_segments, data);

	g_object_set (G_OBJECT (bar), "margin", 18, NULL);
	gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (bar));
	gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size (GTK_WINDOW( window), 600, -1);
	gtk_window_set_title (GTK_WINDOW( window), "SegmentedBar Widget Test");
	gtk_widget_show_all (GTK_WIDGET (window));
	gtk_main ();

	g_free (data);

	return 0;
}
