/* 
 * arch-tag: Implementation of Rhythmbox common bits
 *
 * Copyright (C) 2004 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include "rb-remote-common.h"

void
rb_remote_song_free (RBRemoteSong *song)
{
	g_free (song->title);
	g_free (song->artist);
	g_free (song->genre);
	g_free (song->album);
	g_free (song->uri);
	g_free (song);
}
