/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#ifndef __RB_NODE_SONG_H
#define __RB_NODE_SONG_H

#include "rb-node.h"
#include "rb-library.h"

G_BEGIN_DECLS

/* properties */
enum
{
	RB_NODE_SONG_PROP_GENRE              = 1,
	RB_NODE_SONG_PROP_ARTIST             = 2,
	RB_NODE_SONG_PROP_ALBUM              = 3,
	RB_NODE_SONG_PROP_REAL_GENRE         = 4,
	RB_NODE_SONG_PROP_REAL_ARTIST        = 5,
	RB_NODE_SONG_PROP_REAL_ALBUM         = 6,
	RB_NODE_SONG_PROP_TRACK_NUMBER       = 7,
	RB_NODE_SONG_PROP_REAL_TRACK_NUMBER  = 8,
	RB_NODE_SONG_PROP_DURATION           = 9,
	RB_NODE_SONG_PROP_REAL_DURATION      = 10,
	RB_NODE_SONG_PROP_FILE_SIZE          = 11,
	RB_NODE_SONG_PROP_LOCATION           = 12,
	RB_NODE_SONG_PROP_MTIME              = 13,
	RB_NODE_SONG_PROP_RESERVED           = 14,
	RB_NODE_SONG_PROP_RATING	     = 15,
 	RB_NODE_SONG_PROP_NUM_PLAYS          = 16,
 	RB_NODE_SONG_PROP_LAST_PLAYED        = 17,
	RB_NODE_SONG_PROP_LAST_PLAYED_SIMPLE = 18,
	RB_NODE_SONG_PROP_ARTIST_SORT_KEY    = 19,
	RB_NODE_SONG_PROP_ALBUM_SORT_KEY     = 20,
	RB_NODE_SONG_PROP_TITLE_SORT_KEY     = 21
};

#define RB_TYPE_NODE_SONG         (rb_node_song_get_type ())
#define RB_NODE_SONG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_SONG, RBNodeSong))
#define RB_NODE_SONG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_SONG, RBNodeSongClass))
#define RB_IS_NODE_SONG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_SONG))
#define RB_IS_NODE_SONG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_SONG))
#define RB_NODE_SONG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_SONG, RBNodeSongClass))

typedef struct RBNodeSongPrivate RBNodeSongPrivate;

typedef struct
{
	RBNode parent;
} RBNodeSong;

typedef struct
{
	RBNodeClass parent;
} RBNodeSongClass;

GType       rb_node_song_get_type              (void);

RBNodeSong *rb_node_song_new                   (const char *location,
					        RBLibrary *library);

/* if the stored mtime on the node differs from the file's actual mtime,
 * resync the node */
void        rb_node_song_update_if_changed     (RBNodeSong *song,
					        RBLibrary *library);

/* convenience property wrappers: */
RBNode     *rb_node_song_get_genre             (RBNodeSong *song);
gboolean    rb_node_song_has_genre             (RBNodeSong *song,
				                RBNode *genre,
					        RBLibrary *library);

RBNode     *rb_node_song_get_artist            (RBNodeSong *song);
gboolean    rb_node_song_has_artist            (RBNodeSong *song,
				                RBNode *artist,
					        RBLibrary *library);

RBNode     *rb_node_song_get_album             (RBNodeSong *song);
gboolean    rb_node_song_has_album             (RBNodeSong *song,
				                RBNode *album,
					        RBLibrary *library);

/* Update 'play' statistics */
void        rb_node_song_update_play_statistics (RBNode *node);


G_END_DECLS

#endif /* __RB_NODE_SONG_H */
