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

#include "rb-podcast-parse.h"

#include <string.h>

static gboolean debug = FALSE;

int main (int argc, char **argv)
{
	RBPodcastChannel *data;
	GList *l;
	GDate date = {0,};
	char datebuf[1024];
	GError *error = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	if (argv[2] != NULL && strcmp (argv[2], "--debug") == 0) {
		debug = TRUE;
	}

	data = g_new0 (RBPodcastChannel, 1);
	if (rb_podcast_parse_load_feed (data, argv[1], FALSE, &error) == FALSE) {
		g_warning ("Couldn't parse %s: %s", argv[1], error->message);
		g_clear_error (&error);
		return 1;
	}

	g_date_set_time_t (&date, data->pub_date);
	g_date_strftime (datebuf, 1024, "%F %T", &date);

	g_print ("Podcast title: %s\n", data->title);
	g_print ("Description: %s\n", data->description);
	g_print ("Author: %s\n", data->author);
	g_print ("Date: %s\n", datebuf);
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

	return 0;
}

