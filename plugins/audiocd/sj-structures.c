/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-structures.c
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
 * Boston, MA 02111-1307, USA.*
 *
 * Authors: Ross Burton <ross@burtonini.com>
 */

#include "sj-structures.h"
#include <glib.h>

/*
 * Free a TrackDetails*
 */
void track_details_free(TrackDetails *track)
{
  g_return_if_fail (track != NULL);
  g_free (track->title);
  g_free (track->artist);
  g_free (track->track_id);
  g_free (track->artist_id);
  g_free (track->artist_sortname);
  g_free (track);
}

/*
 * Free a AlbumDetails*
 */
void album_details_free(AlbumDetails *album)
{
  g_return_if_fail (album != NULL);
  g_free (album->title);
  g_free (album->artist);
  g_free (album->genre);
  g_free (album->album_id);
  if (album->release_date) g_date_free (album->release_date);
  g_list_foreach (album->tracks, (GFunc)track_details_free, NULL);
  g_list_free (album->tracks);
  g_free (album->artist_sortname);
  g_free (album->asin);
  g_free (album->discogs);
  g_free (album->wikipedia);
  g_free (album);
}
