/*
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

#ifndef RB_PODCAST_PARSE_H
#define RB_PODCAST_PARSE_H

#include <glib.h>
#include <gio/gio.h>

typedef enum
{
	RB_PODCAST_PARSE_ERROR_FILE_INFO,		/* error getting podcast file info */
	RB_PODCAST_PARSE_ERROR_MIME_TYPE,		/* podcast has unexpected mime type */
	RB_PODCAST_PARSE_ERROR_XML_PARSE,		/* error parsing podcast xml */
	RB_PODCAST_PARSE_ERROR_NO_ITEMS,		/* feed doesn't contain any downloadable items */
} RBPodcastParseError;

typedef enum
{
	RB_PODCAST_PARSE_STATUS_UNPARSED,		/* feed unparsed */
	RB_PODCAST_PARSE_STATUS_SUCCESS,		/* feed parse succeeded */
	RB_PODCAST_PARSE_STATUS_ERROR,			/* feed parse failed */
} RBPodcastParseStatus;

#define RB_PODCAST_PARSE_ERROR rb_podcast_parse_error_quark ()
GQuark rb_podcast_parse_error_quark (void);

typedef struct
{
	char* title;
	char* url;
	char* description;
	char* author;
	char* guid;
	char* img;
	guint64 pub_date;
	gint64 duration;
	guint64 filesize;
} RBPodcastItem;

typedef struct
{
	int refcount;

	char* url;
	char* title;
	char* lang;
    	char* description;
	char* author;
	char* contact;
	char* img;
	guint64 pub_date;
    	char* copyright;

    	gboolean is_opml;

	GList *posts;
	int num_posts;
	RBPodcastParseStatus status;
} RBPodcastChannel;

GType	rb_podcast_channel_get_type (void);
GType	rb_podcast_item_get_type (void);
#define RB_TYPE_PODCAST_CHANNEL	(rb_podcast_channel_get_type ())
#define RB_TYPE_PODCAST_ITEM (rb_podcast_item_get_type ())

typedef void (*RBPodcastParseCallback) (RBPodcastChannel *data, GError *error, gpointer user_data);

void	rb_podcast_parse_load_feed (RBPodcastChannel *data,
				    GCancellable *cancellable,
				    RBPodcastParseCallback callback,
				    gpointer user_data);

RBPodcastChannel *rb_podcast_parse_channel_new (void);
RBPodcastChannel *rb_podcast_parse_channel_copy (RBPodcastChannel *data);
RBPodcastItem *rb_podcast_parse_item_copy (RBPodcastItem *data);
RBPodcastChannel *rb_podcast_parse_channel_ref	(RBPodcastChannel *data);
void rb_podcast_parse_channel_unref 	(RBPodcastChannel *data);
void rb_podcast_parse_item_free 	(RBPodcastItem *data);

#endif /* RB_PODCAST_PARSE_H */
