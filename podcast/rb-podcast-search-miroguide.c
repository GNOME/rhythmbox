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
#include <libsoup/soup-gnome.h>
#include <json-glib/json-glib.h>
#include <totem-pl-parser.h>

#define RB_TYPE_PODCAST_SEARCH_MIROGUIDE         (rb_podcast_search_miroguide_get_type ())
#define RB_PODCAST_SEARCH_MIROGUIDE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_SEARCH_MIROGUIDE, RBPodcastSearchMiroGuide))
#define RB_PODCAST_SEARCH_MIROGUIDE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_SEARCH_MIROGUIDE, RBPodcastSearchMiroGuideClass))
#define RB_IS_PODCAST_SEARCH_MIROGUIDE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_SEARCH_MIROGUIDE))
#define RB_IS_PODCAST_SEARCH_MIROGUIDE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_SEARCH_MIROGUIDE))
#define RB_PODCAST_SEARCH_MIROGUIDE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_SEARCH_MIROGUIDE, RBPodcastSearchMiroGuideClass))

typedef struct _RBPodcastSearchMiroGuide RBPodcastSearchMiroGuide;
typedef struct _RBPodcastSearchMiroGuideClass RBPodcastSearchMiroGuideClass;

struct _RBPodcastSearchMiroGuide
{
	RBPodcastSearch parent;

	SoupSession *session;
};

struct _RBPodcastSearchMiroGuideClass
{
	RBPodcastSearchClass parent;
};

static void rb_podcast_search_miroguide_class_init (RBPodcastSearchMiroGuideClass *klass);
static void rb_podcast_search_miroguide_init (RBPodcastSearchMiroGuide *search);

G_DEFINE_TYPE (RBPodcastSearchMiroGuide, rb_podcast_search_miroguide, RB_TYPE_PODCAST_SEARCH);

#define MIROGUIDE_SEARCH_URI	"http://www.miroguide.com/api/get_feeds"

static void
process_results (RBPodcastSearchMiroGuide *search, JsonParser *parser)
{
	JsonArray *results;
	guint i;

	results = json_node_get_array (json_parser_get_root (parser));

	for (i = 0; i < json_array_get_length (results); i++) {
		JsonObject *feed;
		JsonArray *items;
		RBPodcastChannel *channel;
		int j;

		feed = json_array_get_object_element (results, i);

		channel = g_new0 (RBPodcastChannel, 1);
		channel->url = g_strdup (json_object_get_string_member (feed, "url"));
		channel->title = g_strdup (json_object_get_string_member (feed, "name"));
		channel->author = g_strdup (json_object_get_string_member (feed, "publisher"));		/* hrm */
		channel->img = g_strdup (json_object_get_string_member (feed, "thumbnail_url"));
		channel->is_opml = FALSE;
		rb_debug ("feed %d: url %s, name \"%s\"", i, channel->url, channel->title);

		items = json_object_get_array_member (feed, "item");
		for (j = 0; j < json_array_get_length (items); j++) {
			JsonObject *episode = json_array_get_object_element (items, j);
			RBPodcastItem *item;

			item = g_new0 (RBPodcastItem, 1);
			item->title = g_strdup (json_object_get_string_member (episode, "name"));
			item->url = g_strdup (json_object_get_string_member (episode, "url"));
			item->description = g_strdup (json_object_get_string_member (episode, "description"));
			item->pub_date = totem_pl_parser_parse_date (json_object_get_string_member (episode, "date"), FALSE);
			item->filesize = json_object_get_int_member (episode, "size");
			rb_debug ("item %d: title \"%s\", url %s", j, item->title, item->url);

			channel->posts = g_list_prepend (channel->posts, item);
		}
		channel->posts = g_list_reverse (channel->posts);
		rb_debug ("finished parsing items");

		rb_podcast_search_result (RB_PODCAST_SEARCH (search), channel);
		rb_podcast_parse_channel_free (channel);
	}
}

static void
search_response_cb (SoupSession *session, SoupMessage *msg, RBPodcastSearchMiroGuide *search)
{
	JsonParser *parser;
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
	if (json_parser_load_from_data (parser, msg->response_body->data, msg->response_body->length, NULL)) {
		process_results (search, parser);
	} else {
		rb_debug ("unable to parse response data");
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
	RBPodcastSearchMiroGuide *search = RB_PODCAST_SEARCH_MIROGUIDE (bsearch);

	search->session = soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
							       SOUP_TYPE_GNOME_FEATURES_2_26,
							       NULL);

	uri = soup_uri_new (MIROGUIDE_SEARCH_URI);
	limit = g_strdup_printf ("%d", max_results);
	soup_uri_set_query_from_fields (uri,
					"filter", "audio",
					"filter_value", "1",
					"filter", "name",
					"filter_value", text,
					"sort", "popular",	/* hmm */
					"limit", limit,
					"datatype", "json",
					NULL);
	g_free (limit);

	message = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	soup_uri_free (uri);

	soup_session_queue_message (search->session, message, (SoupSessionCallback) search_response_cb, search);
}

static void
impl_cancel (RBPodcastSearch *bsearch)
{
	RBPodcastSearchMiroGuide *search = RB_PODCAST_SEARCH_MIROGUIDE (bsearch);
	if (search->session != NULL) {
		soup_session_abort (search->session);
	}
}

static void
impl_dispose (GObject *object)
{
	RBPodcastSearchMiroGuide *search = RB_PODCAST_SEARCH_MIROGUIDE (object);

	if (search->session != NULL) {
		soup_session_abort (search->session);
		g_object_unref (search->session);
		search->session = NULL;
	}

	G_OBJECT_CLASS (rb_podcast_search_miroguide_parent_class)->dispose (object);
}

static void
rb_podcast_search_miroguide_init (RBPodcastSearchMiroGuide *search)
{
	/* do nothing? */
}

static void
rb_podcast_search_miroguide_class_init (RBPodcastSearchMiroGuideClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPodcastSearchClass *search_class = RB_PODCAST_SEARCH_CLASS (klass);

	object_class->dispose = impl_dispose;

	search_class->start = impl_start;
	search_class->cancel = impl_cancel;
}
