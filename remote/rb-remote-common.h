/* 
 * arch-tag: Header for Rhythmbox remote common bits 
 *
 * Copyright (C) 2004 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_REMOTE_COMMON_H__
#define __RB_REMOTE_COMMON_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct 
{
	char *title;
	char *artist;
	char *genre;
	char *album;			
	char *uri;
	long track_number;
	long duration;
	long bitrate;
	long filesize;
	double rating;
	long play_count;
	long last_played;			
	long disc_number;
	double track_gain;
	double track_peak;
	double album_gain;
	double album_peak;
} RBRemoteSong;

void rb_remote_song_free (RBRemoteSong *song);

G_END_DECLS

#endif /* __RB_REMOTE_COMMON_H__ */
