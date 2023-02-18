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
#include "rb-debug.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#define RB_TYPE_PODCAST_SEARCH_ITUNES         (rb_podcast_search_itunes_get_type ())
#define RB_PODCAST_SEARCH_ITUNES(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_SEARCH_ITUNES, RBPodcastSearchITunes))
#define RB_PODCAST_SEARCH_ITUNES_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_SEARCH_ITUNES, RBPodcastSearchITunesClass))
#define RB_IS_PODCAST_SEARCH_ITUNES(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_SEARCH_ITUNES))
#define RB_IS_PODCAST_SEARCH_ITUNES_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_SEARCH_ITUNES))
#define RB_PODCAST_SEARCH_ITUNES_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_SEARCH_ITUNES, RBPodcastSearchITunesClass))

typedef struct _RBPodcastSearchITunes RBPodcastSearchITunes;
typedef struct _RBPodcastSearchITunesClass RBPodcastSearchITunesClass;

struct _RBPodcastSearchITunes
{
	RBPodcastSearch parent;

	SoupSession *session;
};

struct _RBPodcastSearchITunesClass
{
	RBPodcastSearchClass parent;
};

static void rb_podcast_search_itunes_class_init (RBPodcastSearchITunesClass *klass);
static void rb_podcast_search_itunes_init (RBPodcastSearchITunes *search);

G_DEFINE_TYPE (RBPodcastSearchITunes, rb_podcast_search_itunes, RB_TYPE_PODCAST_SEARCH);

#define ITUNES_SEARCH_URI	"https://itunes.apple.com/search"

static void
process_results (RBPodcastSearchITunes *search, JsonParser *parser)
{
	JsonObject *container;
	JsonArray *results;
	guint i;

	container = json_node_get_object (json_parser_get_root (parser));
	results = json_node_get_array (json_object_get_member (container, "results"));

	for (i = 0; i < json_array_get_length (results); i++) {
		JsonObject *feed;
		RBPodcastChannel *channel;

		feed = json_array_get_object_element (results, i);

		/* check wrapperType==track, kind==podcast ? */

		channel = rb_podcast_parse_channel_new ();

		channel->url = g_strdup (json_object_get_string_member (feed, "collectionViewUrl"));
		channel->title = g_strdup (json_object_get_string_member (feed, "collectionName"));
		channel->author = g_strdup (json_object_get_string_member (feed, "artistName"));
		channel->img = g_strdup (json_object_get_string_member (feed, "artworkUrl100"));	/* 100? */
		channel->is_opml = FALSE;

		channel->num_posts = json_object_get_int_member (feed, "trackCount");

		rb_debug ("got result %s (%s)", channel->title, channel->url);
		rb_podcast_search_result (RB_PODCAST_SEARCH (search), channel);
		rb_podcast_parse_channel_unref (channel);
	}
}

static void
search_response_cb (GObject *source, GAsyncResult *result, RBPodcastSearchITunes *search)
{
	SoupSession *session = SOUP_SESSION (source);
	SoupMessage *message;
	GBytes *bytes;
	const char *body;
	size_t size;
	JsonParser *parser;
	GError *error = NULL;

	bytes = soup_session_send_and_read_finish (session, result, &error);
	if (error != NULL) {
		rb_debug ("search request failed: %s", error->message);
		g_error_free (error);
		rb_podcast_search_finished (RB_PODCAST_SEARCH (search), FALSE);
		return;
	}

	message = soup_session_get_async_result_message (session, result);
	if (soup_message_get_status (message) != SOUP_STATUS_OK) {
		const char *reason;

		reason = soup_message_get_reason_phrase (message);
		rb_debug ("search request bad status: %s", reason);
		rb_podcast_search_finished (RB_PODCAST_SEARCH (search), FALSE);
		return;
	}

	body = g_bytes_get_data (bytes, &size);
	if (size == 0) {
		rb_debug ("no response data");
		rb_podcast_search_finished (RB_PODCAST_SEARCH (search), TRUE);
		g_bytes_unref (bytes);
		return;
	}
	g_assert (body != NULL);

	parser = json_parser_new ();
	if (json_parser_load_from_data (parser, body, size, &error)) {
		process_results (search, parser);
	} else {
		rb_debug ("unable to parse response data: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (parser);
	rb_podcast_search_finished (RB_PODCAST_SEARCH (search), TRUE);
	g_bytes_unref (bytes);
}

static void
impl_start (RBPodcastSearch *bsearch, const char *text, int max_results)
{
	RBPodcastSearchITunes *search = RB_PODCAST_SEARCH_ITUNES (bsearch);
	SoupMessage *message;
	char *limit;
	char *query;

	search->session = soup_session_new ();

	limit = g_strdup_printf ("%d", max_results);

	query = soup_form_encode ("term", text,
				  "media", "podcast",
				  "entity", "podcast",
				  "limit", limit,
				  "version", "2",
				  "output", "json",
				  NULL);

	message = soup_message_new_from_encoded_form (SOUP_METHOD_GET,
						      ITUNES_SEARCH_URI,
						      query);

	soup_session_send_and_read_async (search->session,
					  message,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) search_response_cb,
					  search);

	g_free (limit);
}

static void
impl_cancel (RBPodcastSearch *bsearch)
{
	RBPodcastSearchITunes *search = RB_PODCAST_SEARCH_ITUNES (bsearch);

	if (search->session != NULL) {
		soup_session_abort (search->session);
	}
}

static void
impl_dispose (GObject *object)
{
	RBPodcastSearchITunes *search = RB_PODCAST_SEARCH_ITUNES (object);

	if (search->session != NULL) {
		soup_session_abort (search->session);
		g_object_unref (search->session);
		search->session = NULL;
	}

	G_OBJECT_CLASS (rb_podcast_search_itunes_parent_class)->dispose (object);
}

static void
rb_podcast_search_itunes_init (RBPodcastSearchITunes *search)
{
	/* do nothing? */
}

static void
rb_podcast_search_itunes_class_init (RBPodcastSearchITunesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPodcastSearchClass *search_class = RB_PODCAST_SEARCH_CLASS (klass);

	object_class->dispose = impl_dispose;

	search_class->cancel = impl_cancel;
	search_class->start = impl_start;
}
