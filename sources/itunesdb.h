/*  arch-tag: Header for itunesdb parser, based on code from gtkpod */
/* Time-stamp: <2003-11-29 12:08:57 jcs>
|
|  Copyright (C) 2002-2003 Jorg Schuler <jcsjcs at users.sourceforge.net>
|  Part of the gtkpod project.
|
|  URL: http://gtkpod.sourceforge.net/
| 
|  Most of the code in this file has been ported from the perl
|  script "mktunes.pl" (part of the gnupod-tools collection) written
|  by Adrian Ulrich <pab at blinkenlights.ch>.
|
|  gnupod-tools: http://www.blinkenlights.ch/cgi-bin/fm.pl?get=ipod
| 
|  The code contained in this file is free software; you can redistribute
|  it and/or modify it under the terms of the GNU Lesser General Public
|  License as published by the Free Software Foundation; either version
|  2.1 of the License, or (at your option) any later version.
|  
|  This file is distributed in the hope that it will be useful,
|  but WITHOUT ANY WARRANTY; without even the implied warranty of
|  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
|  Lesser General Public License for more details.
|  
|  You should have received a copy of the GNU Lesser General Public
|  License along with this code; if not, write to the Free Software
|  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
| 
|  iTunes and iPod are trademarks of Apple
| 
|  This product is not supported/written/published by Apple!
|
|  $Id$
*/

#ifndef __ITUNESDB_H__
#define __ITUNESDB_H__

#include <time.h>

/* Warning: iPodSong is the same as Track, but iPodPlaylist is different from
 * Playlist
 */

typedef struct
{
  gchar   *album;            /* album (utf8)          */
  gchar   *artist;           /* artist (utf8)         */
  gchar   *title;            /* title (utf8)          */
  gchar   *genre;            /* genre (utf8)          */
  gchar   *comment;          /* comment (utf8)        */
  gchar   *composer;         /* Composer (utf8)       */
  gchar   *fdesc;            /* ? (utf8)              */
  gchar   *ipod_path;        /* name of file on iPod: uses ":" instead of "/"*/
  gunichar2 *album_utf16;    /* album (utf16)         */
  gunichar2 *artist_utf16;   /* artist (utf16)        */
  gunichar2 *title_utf16;    /* title (utf16)         */
  gunichar2 *genre_utf16;    /* genre (utf16)         */
  gunichar2 *comment_utf16;  /* comment (utf16)       */
  gunichar2 *composer_utf16; /* Composer (utf16)      */
  gunichar2 *fdesc_utf16;    /* ? (utf16)             */
  gunichar2 *ipod_path_utf16;/* name of file on iPod: uses ":" instead of "/"*/
  gchar   *pc_path_utf8;     /* PC filename in utf8 encoding   */
  gchar   *pc_path_locale;   /* PC filename in locale encoding */
  guint32 ipod_id;           /* unique ID of track    */
  gint32  size;              /* size of file in bytes */
  gint32  oldsize;           /* used when updating tracks: size on iPod */
  gint32  tracklen;          /* Length of track in ms */
  gint32  cd_nr;             /* CD number             */
  gint32  cds;               /* number of CDs         */
  gint32  track_nr;          /* track number          */
  gint32  tracks;            /* number of tracks      */
  gint32  bitrate;           /* bitrate               */
  gint32  year;              /* year                  */
  gchar   *year_str;         /* year as string -- always identical to year */
  gint32  volume;            /* volume adjustment between -100 and +100    */
  guint32 time_played;       /* time of last play  (Mac type)              */
  guint32 time_modified;     /* time of last modification  (Mac type)      */
  guint32 rating;            /* star rating (stars * RATING_STEP (20))     */
  guint32 playcount;         /* number of times track was played           */
  guint32 recent_playcount;  /* times track was played since last sync     */
  gchar   *hostname;         /* name of host this file has been imported on*/
  gboolean transferred;      /* has file been transferred to iPod?         */
  gchar   *md5_hash;         /* md5 hash of file (or NULL)                 */
  gchar   *charset;          /* charset used for ID3 tags                  */
} iPodSong;

typedef struct
{
    gchar *name;            /* name of playlist in UTF8 */
    gunichar2 *name_utf16;  /* name of playlist in UTF16 */
    guint32 type;           /* 1: master play list (PL_TYPE_MPL) */
    GList *song_ids;       /* tracks in playlist (IDs of the tracks) */
} iPodPlaylist;



enum {
  MHOD_ID_TITLE = 1,
  MHOD_ID_PATH = 2,
  MHOD_ID_ALBUM = 3,
  MHOD_ID_ARTIST = 4,
  MHOD_ID_GENRE = 5,
  MHOD_ID_FDESC = 6,
  MHOD_ID_COMMENT = 8,
  MHOD_ID_COMPOSER = 12,
  MHOD_ID_PLAYLIST = 100
};

#define ITUNESDB_COPYBLK 65536      /* blocksize for cp () */

enum _iPodItemType {
	IPOD_ITEM_SONG,
	IPOD_ITEM_PLAYLIST
};

typedef enum _iPodItemType iPodItemType;


struct _iPodItem {
	iPodItemType type;
	gpointer data;
};
typedef struct _iPodItem iPodItem;


typedef struct _iPodParser iPodParser;

iPodItem *ipod_get_next_item (iPodParser *parser);
iPodParser *ipod_parser_new (const gchar *mount_point);
void ipod_parser_destroy (iPodParser *parser);
void ipod_item_destroy (iPodItem *item);
gchar *ipod_parser_get_mount_path (iPodParser *parser);

gchar *itunesdb_get_track_name_on_ipod (const gchar *path, iPodSong *s);

#ifdef ITUNESDB_WRITE
gboolean itunesdb_write (const gchar *path);
gboolean itunesdb_write_to_file (const gchar *filename);
gboolean itunesdb_copy_track_to_ipod (const gchar *path, Track *track,
				      const gchar *pcfile);
gboolean itunesdb_cp (const gchar *from_file, const gchar *to_file);
#endif

guint32 itunesdb_time_get_mac_time (void);
time_t itunesdb_time_mac_to_host (guint32 mactime);
guint32 itunesdb_time_host_to_mac (time_t time);
#endif 
