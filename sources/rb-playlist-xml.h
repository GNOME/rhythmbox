/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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

/*
 * Playlist XML tags and attributes
 */

#ifndef __RB_PLAYLIST_XML_H
#define __RB_PLAYLIST_XML_H

#include <libxml/tree.h>

#define RB_PLAYLIST_XML_VERSION "1.0"

#define RB_PLAYLIST_PLAYLIST (xmlChar *) "playlist"

/* common attributes for all playlists */
#define RB_PLAYLIST_TYPE (xmlChar *) "type"
#define RB_PLAYLIST_NAME (xmlChar *) "name"
#define RB_PLAYLIST_SHOW_BROWSER (xmlChar *) "show-browser"
#define RB_PLAYLIST_BROWSER_POSITION (xmlChar *) "browser-position"
#define RB_PLAYLIST_SEARCH_TYPE (xmlChar *) "search-type"

/* values for the 'type' attribute */
#define RB_PLAYLIST_AUTOMATIC (xmlChar *) "automatic"
#define RB_PLAYLIST_STATIC (xmlChar *) "static"
#define RB_PLAYLIST_QUEUE (xmlChar *) "queue"

/* attributes for static playlists */
#define RB_PLAYLIST_LOCATION (xmlChar *) "location"

/* attributes for auto playlists */
#define RB_PLAYLIST_LIMIT_COUNT (xmlChar *) "limit-count"
#define RB_PLAYLIST_LIMIT_SIZE (xmlChar *) "limit-size"
#define RB_PLAYLIST_LIMIT_TIME (xmlChar *) "limit-time"
#define RB_PLAYLIST_SORT_KEY (xmlChar *) "sort-key"
#define RB_PLAYLIST_SORT_DIRECTION (xmlChar *) "sort-direction"
#define RB_PLAYLIST_LIMIT (xmlChar *) "limit"

#endif	/* __RB_PLAYLIST_XML_H */
