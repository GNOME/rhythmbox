/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-structures.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Rhythmbox authors hereby grants permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Ross Burton <ross@burtonini.com>
 */

#ifndef SJ_STRUCTURES_H
#define SJ_STRUCTURES_H

#include <glib/glist.h>
#include <gtk/gtktreemodel.h>

typedef struct _AlbumDetails AlbumDetails;
typedef struct _TrackDetails TrackDetails;


struct _TrackDetails {
  AlbumDetails *album;
  int number; /* track number */
  char *title;
  char *artist;
  char* artist_sortname; /* Can be NULL, so fall back onto artist */
  int duration; /* seconds */
  char* track_id;
  char* artist_id;
  GtkTreeIter iter; /* Temporary iterator for internal use */
};

struct _AlbumDetails {
  char* title;
  char* artist;
  char* artist_sortname;
  char *genre;
  int   number; /* number of tracks in the album */
  GList* tracks;
  GDate *release_date; /* MusicBrainz support multiple releases per album */
  char* album_id;
  char* artist_id;
};

void album_details_free(AlbumDetails *album);
void track_details_free(TrackDetails *track);

#endif
