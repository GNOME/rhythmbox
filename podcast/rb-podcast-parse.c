/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include <string.h>

#include <totem-pl-parser.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "rb-debug.h"
#include "rb-util.h"
#include "rb-podcast-parse.h"
#include "rb-file-helpers.h"

typedef struct {
	RBPodcastChannel *channel;
	RBPodcastParseCallback callback;
	gpointer user_data;
} RBPodcastParseData;

GQuark
rb_podcast_parse_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_podcast_parse_error");

	return quark;
}

static void
playlist_metadata_foreach (const char *key,
			   const char *value,
			   gpointer data)
{
	RBPodcastChannel *channel = (RBPodcastChannel *) data;

	if (strcmp (key, TOTEM_PL_PARSER_FIELD_TITLE) == 0) {
		g_free (channel->title);
		channel->title = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_LANGUAGE) == 0) {
		g_free (channel->lang);
		channel->lang = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_DESCRIPTION) == 0) {
		g_free (channel->description);
		channel->description = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_AUTHOR) == 0) {
		g_free (channel->author);
		channel->author = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_CONTACT) == 0) {
		g_free (channel->contact);
		channel->contact = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_IMAGE_URI) == 0) {
		g_free (channel->img);
		channel->img = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_PUB_DATE) == 0) {
		channel->pub_date = totem_pl_parser_parse_date (value, FALSE);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_COPYRIGHT) == 0) {
		g_free (channel->copyright);
		channel->copyright = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_CONTENT_TYPE) == 0) {
		if (strcmp (value, "text/x-opml+xml") == 0) {
			channel->is_opml = TRUE;
		}
	}
}

static void
playlist_started (TotemPlParser *parser,
		  const char *uri,
		  GHashTable *metadata,
		  gpointer data)
{
	g_hash_table_foreach (metadata, (GHFunc) playlist_metadata_foreach, data);
}

static void
playlist_ended (TotemPlParser *parser,
		const char *uri,
		gpointer data)
{
	RBPodcastChannel *channel = (RBPodcastChannel *) data;

	channel->posts = g_list_reverse (channel->posts);
}

static void
entry_metadata_foreach (const char *key,
			const char *value,
			gpointer data)
{
	RBPodcastItem *item = (RBPodcastItem *) data;

	if (strcmp (key, TOTEM_PL_PARSER_FIELD_TITLE) == 0) {
		item->title = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_ID) == 0) {
		item->guid = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_URI) == 0) {
		item->url = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_DESCRIPTION) == 0) {
		item->description = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_AUTHOR) == 0) {
		item->author = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_PUB_DATE) == 0) {
		item->pub_date = totem_pl_parser_parse_date (value, FALSE);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_DURATION) == 0) {
		item->duration = totem_pl_parser_parse_duration (value, FALSE);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_FILESIZE) == 0) {
		item->filesize = g_ascii_strtoull (value, NULL, 10);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_IMAGE_URI) == 0) {
		item->img = g_strdup (value);
	}
}

static void
entry_parsed (TotemPlParser *parser,
	      const char *uri,
	      GHashTable *metadata,
	      gpointer data)
{
	RBPodcastChannel *channel = (RBPodcastChannel *) data;
	RBPodcastItem *item;
	char *scheme = NULL;

	item = g_new0 (RBPodcastItem, 1);
	g_hash_table_foreach (metadata, (GHFunc) entry_metadata_foreach, item);

	/* make sure the item URI is at least URI-like */
	if (item->url != NULL)
		scheme = g_uri_parse_scheme (item->url);

	if (scheme == NULL) {
		rb_debug ("ignoring podcast entry from feed %s with no/invalid uri %s",
			  channel->url,
			  item->url ? item->url : "<null>");
		rb_podcast_parse_item_free (item);
		return;
	}
	g_free (scheme);

	channel->posts = g_list_prepend (channel->posts, item);
}

static void
parse_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	RBPodcastParseData *data = user_data;
	RBPodcastChannel *channel = data->channel;
	GError *error = NULL;
	int result;

	channel->status = RB_PODCAST_PARSE_STATUS_ERROR;
	result = totem_pl_parser_parse_finish (TOTEM_PL_PARSER (source_object), res, &error);

	switch (result) {
	case -1:		/* some versions of totem-pl-parser return this when cancelled */
	case TOTEM_PL_PARSER_RESULT_CANCELLED:
		rb_debug ("parsing of %s cancelled", channel->url);

		/* ensure we have a G_IO_ERROR_CANCELLED error */
		g_clear_error (&error);
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, " ");
		break;

	case TOTEM_PL_PARSER_RESULT_ERROR:
	case TOTEM_PL_PARSER_RESULT_IGNORED:
	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
		rb_debug ("parsing %s as a podcast failed", channel->url);
		/* totem-pl-parser doesn't return interesting errors */
		g_clear_error (&error);
		g_set_error (&error,
			     RB_PODCAST_PARSE_ERROR,
			     RB_PODCAST_PARSE_ERROR_XML_PARSE,
			     _("Unable to parse the feed contents"));
		break;

	case TOTEM_PL_PARSER_RESULT_SUCCESS:
		if (error != NULL) {
			/* currently only happens when parsing was cancelled */
		} else if (channel->posts == NULL) {
			/*
			 * treat empty feeds, or feeds that don't contain any downloadable items, as
			 * an error.
			 */
			rb_debug ("parsing %s as a podcast succeeded, but the feed contains no downloadable items", channel->url);
			g_set_error (&error,
				     RB_PODCAST_PARSE_ERROR,
				     RB_PODCAST_PARSE_ERROR_NO_ITEMS,
				     _("The feed does not contain any downloadable items"));
		} else {
			channel->status = RB_PODCAST_PARSE_STATUS_SUCCESS;
			rb_debug ("parsing %s as a podcast succeeded", channel->url);
		}
		break;

	default:
		g_assert_not_reached ();
	}

	data->callback (channel, error, data->user_data);
	g_object_unref (source_object);
	g_clear_error (&error);
	g_free (data);
}

void
rb_podcast_parse_load_feed (RBPodcastChannel *channel,
			    GCancellable *cancellable,
			    RBPodcastParseCallback callback,
			    gpointer user_data)
{
	TotemPlParser *plparser;
	RBPodcastParseData *data;

	data = g_new0 (RBPodcastParseData, 1);
	data->channel = channel;
	data->callback = callback;
	data->user_data = user_data;

	plparser = totem_pl_parser_new ();
	g_object_set (plparser, "recurse", FALSE, "force", TRUE, NULL);
	g_signal_connect (plparser, "entry-parsed", G_CALLBACK (entry_parsed), channel);
	g_signal_connect (plparser, "playlist-started", G_CALLBACK (playlist_started), channel);
	g_signal_connect (plparser, "playlist-ended", G_CALLBACK (playlist_ended), channel);

	totem_pl_parser_parse_async (plparser, channel->url, FALSE, cancellable, parse_cb, data);
}

RBPodcastChannel *
rb_podcast_parse_channel_new (void)
{
	RBPodcastChannel *data;
	data = g_new0 (RBPodcastChannel, 1);
	data->refcount = 1;
	return data;
}

RBPodcastChannel *
rb_podcast_parse_channel_copy (RBPodcastChannel *data)
{
	RBPodcastChannel *copy = rb_podcast_parse_channel_new ();
	copy->url = g_strdup (data->url);
	copy->title = g_strdup (data->title);
	copy->lang = g_strdup (data->lang);
	copy->description = g_strdup (data->description);
	copy->author = g_strdup (data->author);
	copy->contact = g_strdup (data->contact);
	copy->img = g_strdup (data->img);
	copy->pub_date = data->pub_date;
	copy->copyright = g_strdup (data->copyright);
	copy->is_opml = data->is_opml;

	if (data->posts != NULL) {
		GList *l;
		for (l = data->posts; l != NULL; l = l->next) {
			RBPodcastItem *copyitem;
			copyitem = rb_podcast_parse_item_copy (l->data);
			data->posts = g_list_prepend (data->posts, copyitem);
		}
		data->posts = g_list_reverse (data->posts);
	} else {
		copy->num_posts = data->num_posts;
	}

	return copy;
}

RBPodcastChannel *
rb_podcast_parse_channel_ref (RBPodcastChannel *data)
{
	data->refcount++;
	return data;
}

void
rb_podcast_parse_channel_unref (RBPodcastChannel *data)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (data->refcount > 0);

	g_assert (rb_is_main_thread ());

	if (--data->refcount > 0) {
		return;
	}

	g_list_foreach (data->posts, (GFunc) rb_podcast_parse_item_free, NULL);
	g_list_free (data->posts);
	data->posts = NULL;

	g_free (data->url);
	g_free (data->title);
	g_free (data->lang);
	g_free (data->description);
	g_free (data->author);
	g_free (data->contact);
	g_free (data->img);
	g_free (data->copyright);

	g_free (data);
}

RBPodcastItem *
rb_podcast_parse_item_copy (RBPodcastItem *item)
{
	RBPodcastItem *copy;
	copy = g_new0 (RBPodcastItem, 1);
	copy->title = g_strdup (item->title);
	copy->url = g_strdup (item->url);
	copy->description = g_strdup (item->description);
	copy->author = g_strdup (item->author);
	copy->pub_date = item->pub_date;
	copy->duration = item->duration;
	copy->filesize = item->filesize;
	return copy;
}

void
rb_podcast_parse_item_free (RBPodcastItem *item)
{
	g_return_if_fail (item != NULL);

	g_free (item->title);
	g_free (item->url);
	g_free (item->description);
	g_free (item->author);
	g_free (item->img);

	g_free (item);
}

GType
rb_podcast_channel_get_type (void)
{
	static GType type = 0;
	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBPodcastChannel",
						     (GBoxedCopyFunc)rb_podcast_parse_channel_copy,
						     (GBoxedFreeFunc)rb_podcast_parse_channel_unref);
	}
	return type;
}

GType
rb_podcast_item_get_type (void)
{
	static GType type = 0;
	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBPodcastItem",
						     (GBoxedCopyFunc)rb_podcast_parse_item_copy,
						     (GBoxedFreeFunc)rb_podcast_parse_item_free);
	}
	return type;
}
