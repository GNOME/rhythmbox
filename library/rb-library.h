/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#ifndef __LIBRARY_H
#define __LIBRARY_H

#include "rb-node.h"

G_BEGIN_DECLS

#define TYPE_LIBRARY            (library_get_type ())
#define LIBRARY(obj)	        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_LIBRARY, Library))
#define LIBRARY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_LIBRARY, LibraryClass))
#define IS_LIBRARY(obj)	        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_LIBRARY))
#define IS_LIBRARY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_LIBRARY))
#define LIBRARY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_LIBRARY, LibraryClass))

typedef struct _Library        		Library;
typedef struct _LibraryClass 		LibraryClass;

typedef struct _LibraryPrivate 		LibraryPrivate;

struct _Library
{
	GObject base;
	
	LibraryPrivate *priv;
};

struct _LibraryClass
{
	GObjectClass parent_class;

	/* signals */
	void (*node_created) (Library *l, RBNode *node);
	void (*node_changed) (Library *l, RBNode *node);
	void (*node_deleted) (Library *l, RBNode *node);
};

typedef enum
{
	LIBRARY_NODE_ROOT,
	LIBRARY_NODE_ARTIST,
	LIBRARY_NODE_ALBUM,
	LIBRARY_NODE_SONG
} LibraryNodeType;

GType            library_get_type       (void) G_GNUC_CONST;

Library         *library_new            (void);

void             library_release_brakes (Library *l);

RBNode          *library_add_uri        (Library *l, const gchar *uri);
void             library_remove_uri     (Library *l, const gchar *uri);

RBNode          *library_get_root       (Library *l);

RBNode		*library_node_from_id   (Library *l, int id);
RBNode          *library_search         (Library *l, const char *search_text);

RBNode		*library_get_all_albums (Library *l);
RBNode		*library_get_all_songs  (Library *l);

#define NODE_PROPERTY_NAME         "name"
#define NODE_PROPERTY_TYPE         "type"

#define SONG_PROPERTY_DATE         "date"
#define SONG_PROPERTY_GENRE        "genre"
#define SONG_PROPERTY_COMMENT      "comment"
#define SONG_PROPERTY_CODEC_INFO   "codecinfo"
#define SONG_PROPERTY_TRACK_NUMBER "tracknum"
#define SONG_PROPERTY_BIT_RATE     "bitrate"
#define SONG_PROPERTY_FILE_SIZE    "filesize"
#define SONG_PROPERTY_DURATION     "duration"
#define SONG_PROPERTY_MTIME        "mtime"
#define SONG_PROPERTY_URI          "uri"

G_END_DECLS

#endif /* __LIBRARY_H */
