/*  monkey-sound
 *
 *  arch-tag: Header for various Ogg Vorbis loading functions
 *
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
 */

#ifndef _OGG_HELPER_H_
#define _OGG_HELPER_H_

#include <vorbis/vorbisfile.h>

size_t ogg_helper_read (void *ptr, size_t size, size_t nmemb,
		void *datasource);
int ogg_helper_seek (void *datasource, ogg_int64_t offset, int whence);
int ogg_helper_close (void *datasource);
int ogg_helper_close_dummy (void *datasource);
long ogg_helper_tell (void *datasource);

#endif /* _OGG_HELPER_H_ */
