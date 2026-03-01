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

static void
resolve_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBPodcastSearch *search = RB_PODCAST_SEARCH (source_object);
	const char *orig_url;
	char *url;
	GError *error = NULL;

	url = rb_podcast_search_resolve_finish (search, result, &orig_url, &error);
	g_print ("original URL: %s\n", orig_url);
	if (error != NULL) {
		g_print ("Resolver error: %s\n", error->message);
		g_clear_error (&error);
	} else if (url != NULL) {
		g_print ("Resolved URL: %s\n", url);
		g_free (url);
	} else {
		g_print ("Resolver returned nothing\n");
	}

	g_main_loop_quit ((GMainLoop *)data);
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

	if (rb_podcast_search_can_resolve (itunes, text) == FALSE) {
		g_print ("iTunes search didn't want to resolve %s\n", text);
	} else {
		rb_podcast_search_resolve (itunes, text, resolve_cb, loop);
		g_main_loop_run (loop);
	}

	return 0;
}
