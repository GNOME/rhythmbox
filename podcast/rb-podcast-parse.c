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
#include "rb-podcast-parse.h"
#include "rb-file-helpers.h"

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
		channel->title = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_LANGUAGE) == 0) {
		channel->lang = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_DESCRIPTION) == 0) {
		channel->description = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_AUTHOR) == 0) {
		channel->author = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_CONTACT) == 0) {
		channel->contact = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_IMAGE_URI) == 0) {
		channel->img = g_strdup (value);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_PUB_DATE) == 0) {
		channel->pub_date = totem_pl_parser_parse_date (value, FALSE);
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_COPYRIGHT) == 0) {
		channel->copyright = g_strdup (value);
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

gboolean
rb_podcast_parse_load_feed (RBPodcastChannel *data,
			    const char *file_name,
			    gboolean existing_feed,
			    GError **error)
{
	GFile *file;
	GFileInfo *fileinfo;
	TotemPlParser *plparser;

	data->url = g_strdup (file_name);

	/* if the URL has a .rss, .xml or .atom extension (before the query string),
	 * don't bother checking the MIME type.
	 */
	if (rb_uri_could_be_podcast (file_name, &data->is_opml) || existing_feed) {
		rb_debug ("not checking mime type for %s (should be %s file)", file_name,
			  data->is_opml ? "OPML" : "Podcast");
	} else {
		GError *ferror = NULL;
		char *content_type;

		rb_debug ("checking mime type for %s", file_name);

		file = g_file_new_for_uri (file_name);
		fileinfo = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, &ferror);
		if (ferror != NULL) {
			g_set_error (error,
				     RB_PODCAST_PARSE_ERROR,
				     RB_PODCAST_PARSE_ERROR_FILE_INFO,
				     _("Unable to check file type: %s"),
				     ferror->message);
			g_object_unref (file);
			g_clear_error (&ferror);
			return FALSE;
		}

		content_type = g_file_info_get_attribute_as_string (fileinfo, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
		g_object_unref (file);
		g_object_unref (fileinfo);

		if (content_type != NULL
		    && strstr (content_type, "html") == NULL
		    && strstr (content_type, "xml") == NULL
		    && strstr (content_type, "rss") == NULL
		    && strstr (content_type, "opml") == NULL) {
			g_set_error (error,
				     RB_PODCAST_PARSE_ERROR,
				     RB_PODCAST_PARSE_ERROR_MIME_TYPE,
				     _("Unexpected file type: %s"),
				     content_type);
			g_free (content_type);
			return FALSE;
		} else if (content_type != NULL
			   && strstr (content_type, "opml") != NULL) {
			data->is_opml = TRUE;
		}

		g_free (content_type);
	}

	plparser = totem_pl_parser_new ();
	g_object_set (plparser, "recurse", FALSE, "force", TRUE, NULL);
	g_signal_connect (G_OBJECT (plparser), "entry-parsed", G_CALLBACK (entry_parsed), data);
	g_signal_connect (G_OBJECT (plparser), "playlist-started", G_CALLBACK (playlist_started), data);
	g_signal_connect (G_OBJECT (plparser), "playlist-ended", G_CALLBACK (playlist_ended), data);

	if (totem_pl_parser_parse (plparser, file_name, FALSE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
		rb_debug ("Parsing %s as a Podcast failed", file_name);
		g_set_error (error,
			     RB_PODCAST_PARSE_ERROR,
			     RB_PODCAST_PARSE_ERROR_XML_PARSE,
			     _("Unable to parse the feed contents"));
		g_object_unref (plparser);
		return FALSE;
	}
	g_object_unref (plparser);

	/* treat empty feeds, or feeds that don't contain any downloadable items, as
	 * an error.
	 */
	if (data->posts == NULL) {
		rb_debug ("Parsing %s as a podcast succeeded, but the feed contains no downloadable items", file_name);
		g_set_error (error,
			     RB_PODCAST_PARSE_ERROR,
			     RB_PODCAST_PARSE_ERROR_NO_ITEMS,
			     _("The feed does not contain any downloadable items"));
		return FALSE;
	}

	rb_debug ("Parsing %s as a Podcast succeeded", file_name);
	return TRUE;
}

RBPodcastChannel *
rb_podcast_parse_channel_copy (RBPodcastChannel *data)
{
	RBPodcastChannel *copy;
	copy = g_new0 (RBPodcastChannel, 1);
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

void
rb_podcast_parse_channel_free (RBPodcastChannel *data)
{
	g_return_if_fail (data != NULL);

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
	data = NULL;
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

	g_free (item);
}
