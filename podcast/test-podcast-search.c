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

#include <locale.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include "rb-util.h"
#include "rb-debug.h"
#include "rb-podcast-search.h"

#include <string.h>

static int done = 0;

static void
result_cb (RBPodcastSearch *search, RBPodcastChannel *data)
{
	char datebuf[1025];
	GDate date;

	g_date_set_time_t (&date, data->pub_date);
	g_date_strftime (datebuf, 1024, "%F %T", &date);

	g_print ("Result from %s\n", G_OBJECT_TYPE_NAME (search));

	g_print ("Podcast title: %s\n", data->title);
	g_print ("Description: %s\n", data->description);
	g_print ("Author: %s\n", data->author);
	g_print ("Date: %s\n", datebuf);

	if (data->num_posts > 0) {
		g_print ("Number of episodes: %d\n", data->num_posts);
		g_print ("\n");
	} else {
		GList *l;
		g_print ("Number of episodes: %d\n", g_list_length (data->posts));
		g_print ("\n");
		for (l = data->posts; l != NULL; l = l->next) {
			RBPodcastItem *item = l->data;

			g_date_set_time_t (&date, item->pub_date);
			g_date_strftime (datebuf, 1024, "%F %T", &date);

			g_print ("\tItem title: %s\n", item->title);
			g_print ("\tURL: %s\n", item->url);
			g_print ("\tAuthor: %s\n", item->author);
			g_print ("\tDate: %s\n", datebuf);
			g_print ("\tDescription: %s\n", item->description);
			g_print ("\n");
		}
	}
}

static void
finished_cb (RBPodcastSearch *search, gboolean successful, GMainLoop *loop)
{
	g_print ("Search %s finished (%d)\n", G_OBJECT_TYPE_NAME (search), successful);
	done++;
	if (done == 1) {
		g_main_loop_quit (loop);
	}
}

int main (int argc, char **argv)
{
	GMainLoop *loop;
	RBPodcastSearch *itunes;
	char *text;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	rb_threads_init ();

	text = argv[1];
	if (argv[2] != NULL && strcmp (argv[2], "--debug") == 0) {
		rb_debug_init (TRUE);
	}

	loop = g_main_loop_new (NULL, FALSE);

	itunes = RB_PODCAST_SEARCH (g_object_new (rb_podcast_search_itunes_get_type (), NULL));

	g_signal_connect (itunes, "result", G_CALLBACK (result_cb), NULL);
	g_signal_connect (itunes, "finished", G_CALLBACK (finished_cb), loop);

	rb_podcast_search_start (itunes, text, 10);

	g_main_loop_run (loop);

	return 0;
}
