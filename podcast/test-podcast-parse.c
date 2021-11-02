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

#include "rb-debug.h"
#include "rb-util.h"
#include "rb-podcast-parse.h"

#include <string.h>

static void
parse_cb (RBPodcastChannel *channel, GError *error, gpointer user_data)
{
	GMainLoop *ml = user_data;
	GList *l;
	GDate date = {0,};
	char datebuf[1024];

	if (error) {
		g_warning ("Couldn't parse %s: %s", channel->url, error->message);
		g_main_loop_quit (ml);
		return;
	}

	g_date_set_time_t (&date, channel->pub_date);
	g_date_strftime (datebuf, 1024, "%F %T", &date);

	g_print ("Podcast title: %s\n", channel->title);
	g_print ("Description: %s\n", channel->description);
	g_print ("Author: %s\n", channel->author);
	g_print ("Date: %s\n", datebuf);
	g_print ("\n");

	for (l = channel->posts; l != NULL; l = l->next) {
		RBPodcastItem *item = l->data;

		g_date_set_time_t (&date, item->pub_date);
		g_date_strftime (datebuf, 1024, "%F %T", &date);

		g_print ("\tItem title: %s\n", item->title);
		g_print ("\tGUID: %s\n", item->guid);
		g_print ("\tURL: %s\n", item->url);
		g_print ("\tAuthor: %s\n", item->author);
		g_print ("\tDate: %s\n", datebuf);
		g_print ("\tDescription: %s\n", item->description);
		g_print ("\n");
	}

	rb_podcast_parse_channel_unref (channel);
	g_main_loop_quit (ml);
}

int
main (int argc, char **argv)
{
	RBPodcastChannel *data;
	GMainLoop *ml;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	rb_threads_init ();

	if (argv[2] != NULL && strcmp (argv[2], "--debug") == 0) {
		rb_debug_init (TRUE);
	}

	ml = g_main_loop_new (NULL, FALSE);
	data = rb_podcast_parse_channel_new ();
	data->url = g_strdup (argv[1]);
	rb_podcast_parse_load_feed (data, NULL, parse_cb, ml);

	g_main_loop_run (ml);

	return 0;
}
