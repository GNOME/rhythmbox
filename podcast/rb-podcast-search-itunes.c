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

#define ITUNES_SEARCH_URI	"https://itunes.apple.com/WebObjects/MZStoreServices.woa/ws/wsSearch"

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

		channel = g_new0 (RBPodcastChannel, 1);

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
search_response_cb (SoupSession *session, SoupMessage *msg, RBPodcastSearchITunes *search)
{
	JsonParser *parser;
	GError *error = NULL;
	int code;

	g_object_get (msg, SOUP_MESSAGE_STATUS_CODE, &code, NULL);
	if (code != 200) {
		char *reason;

		g_object_get (msg, SOUP_MESSAGE_REASON_PHRASE, &reason, NULL);
		rb_debug ("search request failed: %s", reason);
		g_free (reason);
		rb_podcast_search_finished (RB_PODCAST_SEARCH (search), FALSE);
		return;
	}

	if (msg->response_body->data == NULL) {
		rb_debug ("no response data");
		rb_podcast_search_finished (RB_PODCAST_SEARCH (search), TRUE);
		return;
	}

	parser = json_parser_new ();
	if (json_parser_load_from_data (parser, msg->response_body->data, msg->response_body->length, &error)) {
		process_results (search, parser);
	} else {
		rb_debug ("unable to parse response data: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (parser);
	rb_podcast_search_finished (RB_PODCAST_SEARCH (search), TRUE);
}

static void
impl_start (RBPodcastSearch *bsearch, const char *text, int max_results)
{
	SoupURI *uri;
	SoupMessage *message;
	char *limit;
	RBPodcastSearchITunes *search = RB_PODCAST_SEARCH_ITUNES (bsearch);

	search->session = soup_session_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
							 SOUP_TYPE_PROXY_RESOLVER_DEFAULT,
							 NULL);

	uri = soup_uri_new (ITUNES_SEARCH_URI);
	limit = g_strdup_printf ("%d", max_results);
	soup_uri_set_query_from_fields (uri,
					"term", text,
					"media", "podcast",
					"entity", "podcast",
					"limit", limit,
					"version", "2",
					"output", "json",
					NULL);
	g_free (limit);

	message = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	soup_uri_free (uri);

	soup_session_queue_message (search->session, message, (SoupSessionCallback) search_response_cb, search);
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
