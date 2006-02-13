 /*
 *  arch-tag: Header for RhythmDB private bits
 *
 *  Copyright (C) 2004 Colin Walters <walters@rhythmbox.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef RHYTHMDB_PRIVATE_H
#define RHYTHMDB_PRIVATE_H

#include "config.h"
#include <rhythmdb.h>

G_BEGIN_DECLS

RhythmDBEntry * rhythmdb_entry_allocate		(RhythmDB *db, RhythmDBEntryType type);
void		rhythmdb_entry_insert		(RhythmDB *db, RhythmDBEntry *entry);

typedef struct {
	/* podcast */
	RBRefString *description;
	RBRefString *subtitle;
	RBRefString *summary;
	RBRefString *lang;
	RBRefString *copyright;
	RBRefString *image;
	gulong status;	/* 0-99: downloading
			   100: Complete
			   101: Error
			   102: wait
			   103: pause */
	gulong post_time;
} RhythmDBPodcastFields;


struct RhythmDBEntry_ {
	/* internal bits */
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif	
	gboolean inserted;
	gint refcount;
	void *data;
	gulong type;
	
	/* metadata */
	RBRefString *title;
	RBRefString *artist;
	RBRefString *album;
	RBRefString *genre;
	gulong tracknum;
	gulong discnum;
	gulong duration;
	gulong bitrate;
	double track_gain;
	double track_peak;
	double album_gain;
	double album_peak;
	GDate *date;

	/* filesystem */
	char *location;
	RBRefString *mountpoint;
	guint64 file_size;
	RBRefString *mimetype;
	gulong mtime;
	gulong first_seen;
	gulong last_seen;

	/* user data */
	gdouble rating;
	glong play_count;
	gulong last_played;

	/* cached data */
	RBRefString *last_played_str;
	RBRefString *first_seen_str;

	/* playback error string */
	char *playback_error;

	/* visibility (to hide entries on unmounted volumes) */
	gboolean hidden;

	/*Podcast*/
	RhythmDBPodcastFields *podcast;
};

G_END_DECLS

#endif /* __RHYTHMDB_PRIVATE_H */
