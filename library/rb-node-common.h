/*
 *  arch-tag: Header with various definitions for Rhythmbox node database
 *
 *  Copyright (C) 2003 Xan Lopez <xan@masilla.org>
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
 */

#ifndef __RB_NODE_COMMON_H
#define __RB_NODE_COMMON_H

/* Databases */
#define RB_NODE_DB_LIBRARY "RBLibrary"
#define RB_NODE_DB_IRADIO "RBIRadio"

/* Root nodes */
enum
{
	LIBRARY_GENRES_NODE_ID = 0,
	LIBRARY_ARTISTS_NODE_ID = 1,
	LIBRARY_ALBUMS_NODE_ID = 2,
	LIBRARY_SONGS_NODE_ID = 3, 
	IRADIO_GENRES_NODE_ID = 4,
	IRADIO_STATIONS_NODE_ID = 5,
};

typedef enum
{
	RB_NODE_ALL_PRIORITY,
	RB_NODE_SPECIAL_PRIORITY,
	RB_NODE_NORMAL_PRIORITY
} RBNodePriority;

#endif /* RB_NODE_COMMON_H */
