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

#ifndef __LIBRARY_PRIVATE_H
#define __LIBRARY_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

#include "rb-library.h"
#include "rb-node.h"
#include "rb-node-search.h"

typedef struct 
{
	/* Determines whether the thread is working, unlock to inform of new data */
	GMutex *run_thread;
	
	/* Protects access to the library data structures */
	GStaticRWLock *library_data;
	
	GMutex *songs_to_update;
	GMutex *nodes_to_signal;
} LibraryPrivateMutices;

struct _LibraryPrivate
{
	/* actual data structures, grab the library_data mutex */
	RBNode *root;
	GHashTable *artist_to_node;
	GHashTable *album_to_node;
	GHashTable *uri_to_node;
	GHashTable *id_to_node;

	guint lastid;

	/* The library's associated on-disk file */
	gchar *file;

	/* songs for the thread to update, grab the songs_to_update mutex */
        GList *songs_to_update;

	GList *nodes_to_signal;

	/* this thread is used to update the cache */
	GThread *thread;
	
	LibraryPrivateMutices *mutex;

	RBNodeSearch *search;

	RBNode *all_songs;
	RBNode *all_albums;
};


/* all the different signals */
enum
{
	NODE_CREATED,
	NODE_CHANGED,
	NODE_DELETED,
	LAST_SIGNAL
};

typedef struct 
{
	int signal_index;
	RBNode *node;
} LibraryPrivateSignal;

int  library_private_build_id            (Library *l);
void library_private_append_node_signal  (Library *l, RBNode *node, int signal_index);

void library_private_remove_song         (Library *l, RBNode *song);
void library_private_add_song            (Library *l, RBNode *song, RBNode *artist, RBNode *album);

RBNode *library_private_add_artist_if_needed (Library *l, const char *name);
RBNode *library_private_add_album_if_needed  (Library *l, const char *name, RBNode *artist);


#define XML_NODE_ARTIST "Artist"
#define XML_NODE_ALBUM "Album"
#define XML_NODE_SONG "Song"

G_END_DECLS

#endif /* __LIBRARY_PRIVATE_H */
