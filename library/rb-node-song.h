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

/* management */
void             rb_node_song_set_location         (RBNode *node,
				                    const char *uri,
						    RBLibrary *library);
char            *rb_node_song_get_location         (RBNode *node);

void             rb_node_song_update_if_newer      (RBNode *node,
					            time_t mtime,
						    RBLibrary *library);

/* properties */
char            *rb_node_song_get_title            (RBNode *node);
char            *rb_node_song_get_track_number     (RBNode *node);
char            *rb_node_song_get_duration         (RBNode *node);
long             rb_node_song_get_duration_raw     (RBNode *node);
char            *rb_node_song_get_file_size        (RBNode *node);
GnomeVFSFileSize rb_node_song_get_file_size_raw    (RBNode *node);

/* inheritance path */
char            *rb_node_song_get_genre            (RBNode *node);
RBNode          *rb_node_song_get_genre_raw        (RBNode *node);
gboolean         rb_node_song_has_genre            (RBNode *node,
						    RBNode *genre);
char            *rb_node_song_get_artist           (RBNode *node);
RBNode          *rb_node_song_get_artist_raw       (RBNode *node);
gboolean         rb_node_song_has_artist           (RBNode *node,
						    RBNode *artist);
char            *rb_node_song_get_album            (RBNode *node);
RBNode          *rb_node_song_get_album_raw        (RBNode *node);
gboolean         rb_node_song_has_album            (RBNode *node,
						    RBNode *album);

G_END_DECLS

#endif /* __RB_NODE_SONG_H */
