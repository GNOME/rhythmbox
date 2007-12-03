/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of podcast parse
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include <libgnomevfs/gnome-vfs.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-debug.h"
#include "rb-podcast-parse.h"
#include "rb-file-helpers.h"

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
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_IMAGE_URL) == 0) {
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
	} else if (strcmp (key, TOTEM_PL_PARSER_FIELD_URL) == 0) {
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

	item = g_new0 (RBPodcastItem, 1);
	g_hash_table_foreach (metadata, (GHFunc) entry_metadata_foreach, item);
	channel->posts = g_list_prepend (channel->posts, item);
}

gboolean
rb_podcast_parse_load_feed (RBPodcastChannel *data,
			    const char *file_name)
{
	GnomeVFSResult result;
	GnomeVFSFileInfo *info;
	TotemPlParser *plparser;

	data->url = g_strdup (file_name);

	/* if the URL has a .rss, .xml or .atom extension (before the query string),
	 * don't bother checking the MIME type.
	 */
	if (rb_uri_could_be_podcast (file_name, &data->is_opml)) {
		rb_debug ("not checking mime type for %s (should be %s file)", file_name,
			  data->is_opml ? "OPML" : "Podcast");
	} else {
		gboolean invalid_mime_type;

		rb_debug ("checking mime type for %s", file_name);
		info = gnome_vfs_file_info_new ();

		result = gnome_vfs_get_file_info (file_name, info, GNOME_VFS_FILE_INFO_DEFAULT);

		if ((result != GNOME_VFS_OK)) {
			if (info->mime_type != NULL) {
				rb_debug ("Invalid mime-type in podcast feed %s", info->mime_type);
			} else {
				rb_debug ("Couldn't get mime type for %s: %s", file_name,
					  gnome_vfs_result_to_string (result));
			}
			gnome_vfs_file_info_unref (info);
			return TRUE;
		}

		if (info != NULL
		    && info->mime_type != NULL
		    && strstr (info->mime_type, "html") == NULL
		    && strstr (info->mime_type, "xml") == NULL
		    && strstr (info->mime_type, "rss") == NULL
		    && strstr (info->mime_type, "opml") == NULL) {
			invalid_mime_type = TRUE;
		} else if (info != NULL
			   && info->mime_type != NULL
			   && strstr (info->mime_type, "opml") != NULL) {
			data->is_opml = TRUE;
			invalid_mime_type = FALSE;
		} else {
			invalid_mime_type = FALSE;
		}

		if (invalid_mime_type) {
			GtkWidget *dialog;

			GDK_THREADS_ENTER ();
			dialog = gtk_message_dialog_new (NULL, 0,
							 GTK_MESSAGE_QUESTION,
							 GTK_BUTTONS_YES_NO,
							 _("The URL '%s' does not appear to be a podcast feed. "
							 "It may be the wrong URL, or the feed may be broken. "
							 "Would you like Rhythmbox to attempt to use it anyway?"),
							 file_name);

			if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES)
				invalid_mime_type = FALSE;

			gtk_widget_destroy (dialog);
			GDK_THREADS_LEAVE ();
		}

		gnome_vfs_file_info_unref (info);

		if (invalid_mime_type)
			return FALSE;
	}

	plparser = totem_pl_parser_new ();
	g_object_set (plparser, "recurse", FALSE, NULL);
	g_signal_connect (G_OBJECT (plparser), "entry-parsed", G_CALLBACK (entry_parsed), data);
	g_signal_connect (G_OBJECT (plparser), "playlist-started", G_CALLBACK (playlist_started), data);
	g_signal_connect (G_OBJECT (plparser), "playlist-ended", G_CALLBACK (playlist_ended), data);

	if (totem_pl_parser_parse (plparser, file_name, FALSE) != TOTEM_PL_PARSER_RESULT_SUCCESS) {
		rb_debug ("Parsing %s as a Podcast failed", file_name);
		g_object_unref (plparser);
		return FALSE;
	}
	rb_debug ("Parsing %s as a Podcast succeeded", file_name);

	return TRUE;
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

void
rb_podcast_parse_item_free (RBPodcastItem *item)
{
	g_return_if_fail (item != NULL);

	g_free (item->title);
	g_free (item->url);
	g_free (item->description);

	g_free (item);
}
