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

#include <libgnomevfs/gnome-vfs-ops.h>

#include "rb-library.h"
#include "rb-node.h"

G_BEGIN_DECLS

/* properties */
enum
{
	RB_SONG_PROP_GENRE             = 1,
	RB_SONG_PROP_ARTIST            = 2,
	RB_SONG_PROP_ALBUM             = 3,
	RB_SONG_PROP_REAL_GENRE        = 4,
	RB_SONG_PROP_REAL_ARTIST       = 5,
	RB_SONG_PROP_REAL_ALBUM        = 6,
	RB_SONG_PROP_TRACK_NUMBER      = 7,
	RB_SONG_PROP_REAL_TRACK_NUMBER = 8,
	RB_SONG_PROP_DURATION          = 9,
	RB_SONG_PROP_REAL_DURATION     = 10,
	RB_SONG_PROP_FILE_SIZE         = 11,
	RB_SONG_PROP_LOCATION          = 12,
	RB_SONG_PROP_MTIME             = 13
};

/* management */
void             rb_node_song_init                 (RBNode *node,
				                    const char *uri,
						    RBLibrary *library);

void             rb_node_song_update_if_changed    (RBNode *node,
						    RBLibrary *library);

void             rb_node_song_restore              (RBNode *node);

/* inheritance path */
RBNode          *rb_node_song_get_genre            (RBNode *node);
gboolean         rb_node_song_has_genre            (RBNode *node,
						    RBNode *genre);
RBNode          *rb_node_song_get_artist           (RBNode *node);
gboolean         rb_node_song_has_artist           (RBNode *node,
						    RBNode *artist);
RBNode          *rb_node_song_get_album            (RBNode *node);
gboolean         rb_node_song_has_album            (RBNode *node,
						    RBNode *album);

G_END_DECLS

#endif /* __RB_NODE_SONG_H */
