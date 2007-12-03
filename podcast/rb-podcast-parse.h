/*
 *  arch-tag: Headfile of rss parse of podcast
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

#ifndef RB_PODCAST_PARSE_H
#define RB_PODCAST_PARSE_H

#include <glib.h>

typedef struct
{
	char* title;
	char* url;
	char* description;
	char* author;
	guint64 pub_date;
	gulong duration;
	guint64 filesize;
} RBPodcastItem;

typedef struct
{
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
} RBPodcastChannel;

gboolean rb_podcast_parse_load_feed	(RBPodcastChannel *data, const char *file_name);
void rb_podcast_parse_channel_free 	(RBPodcastChannel *data);
void rb_podcast_parse_item_free 	(RBPodcastItem *data);

#endif /* RB_PODCAST_PARSE_H */
