/*  monkey-sound
 *  arch-tag: header for reading id3 tags over GnomeVFS
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     based upon file.c in libid3tag by Robert Leslie
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

#ifndef _ID3_VFS_H_
#define _ID3_VFS_H_

#include <libgnomevfs/gnome-vfs.h>
#include <id3tag.h>

enum id3_vfs_mode {
	ID3_VFS_MODE_READONLY = 0,
	ID3_VFS_MODE_READWRITE
};

struct id3_vfs_file *id3_vfs_open(char const *, enum id3_vfs_mode);
void id3_vfs_close(struct id3_vfs_file *);

struct id3_tag *id3_vfs_tag(struct id3_vfs_file const *);
int id3_vfs_update(struct id3_vfs_file *);

int id3_vfs_bitrate (struct id3_vfs_file *file, int *bitrate, int *samplerate,
		int *time, int *version, int *vbr, int *channels);

#endif /* _ID3_VFS_H_ */

