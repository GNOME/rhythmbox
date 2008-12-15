/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-structures.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Ross Burton <ross@burtonini.com>
 */

#ifndef SJ_STRUCTURES_H
#define SJ_STRUCTURES_H

#include <glib.h>

typedef enum _MetadataSource MetadataSource;

typedef struct _AlbumDetails AlbumDetails;
typedef struct _TrackDetails TrackDetails;

enum _MetadataSource {
  SOURCE_UNKNOWN = 0,
  SOURCE_CDTEXT,
  SOURCE_FREEDB,
  SOURCE_MUSICBRAINZ,
  SOURCE_FALLBACK
};

struct _TrackDetails {
  AlbumDetails *album;
  int number; /* track number */
  char *title;
  char *artist;
  char* artist_sortname; /* Can be NULL, so fall back onto artist */
  int duration; /* seconds */
  char* track_id;
  char* artist_id;
};

struct _AlbumDetails {
  char* title;
  char* artist;
  char* artist_sortname;
  char *genre;
  int   number; /* number of tracks in the album */
  int   disc_number;
  GList* tracks;
  GDate *release_date; /* MusicBrainz support multiple releases per album */
  char* album_id;
  char* artist_id;
  char* asin;
  char* discogs;
  char* wikipedia;
  MetadataSource metadata_source;
  gboolean is_spoken_word;
};

void album_details_free(AlbumDetails *album);
void track_details_free(TrackDetails *track);

#endif
