/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "rb-podcast-search.h"

static void rb_podcast_search_class_init (RBPodcastSearchClass *klass);
static void rb_podcast_search_init (RBPodcastSearch *search);

enum {
	RESULT,
	FINISHED,
	LAST_SIGNAL
};

G_DEFINE_TYPE (RBPodcastSearch, rb_podcast_search, G_TYPE_OBJECT);

static guint signals[LAST_SIGNAL];

void
rb_podcast_search_start (RBPodcastSearch *search, const char *text, int max_results)
{
	RBPodcastSearchClass *klass = RB_PODCAST_SEARCH_GET_CLASS (search);
	klass->start (search, text, max_results);
}

void
rb_podcast_search_cancel (RBPodcastSearch *search)
{
	RBPodcastSearchClass *klass = RB_PODCAST_SEARCH_GET_CLASS (search);
	klass->cancel (search);
}

void
rb_podcast_search_result (RBPodcastSearch *search, RBPodcastChannel *data)
{
	g_signal_emit (search, signals[RESULT], 0, data);
}

void
rb_podcast_search_finished (RBPodcastSearch *search, gboolean successful)
{
	g_signal_emit (search, signals[FINISHED], 0, successful);
}

static void
rb_podcast_search_init (RBPodcastSearch *search)
{
}

static void
rb_podcast_search_class_init (RBPodcastSearchClass *klass)
{
	signals[RESULT] = g_signal_new ("result",
					RB_TYPE_PODCAST_SEARCH,
					G_SIGNAL_RUN_LAST,
					0,
					NULL, NULL,
					NULL,
					G_TYPE_NONE,
					1, G_TYPE_POINTER);
	signals[FINISHED] = g_signal_new ("finished",
					  RB_TYPE_PODCAST_SEARCH,
					  G_SIGNAL_RUN_LAST,
					  0,
					  NULL, NULL,
					  NULL,
					  G_TYPE_NONE,
					  1, G_TYPE_BOOLEAN);
}
