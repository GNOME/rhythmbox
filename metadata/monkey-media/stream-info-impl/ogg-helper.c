/*  monkey-sound
 *
 *  arch-tag: Implementation of various Ogg Vorbis loading functions
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

#include <libgnomevfs/gnome-vfs.h>
#include "ogg-helper.h"

size_t
ogg_helper_read (void *ptr, size_t size, size_t nmemb, void *datasource)
{
	GnomeVFSResult res;
	GnomeVFSFileSize bytes_read;
	GnomeVFSHandle *file = (GnomeVFSHandle *)datasource;

	res = gnome_vfs_read(file, ptr, (GnomeVFSFileSize) size, &bytes_read);
	if (res != GNOME_VFS_OK)
	{
		if (res == GNOME_VFS_ERROR_EOF)
			return 0;
		return -1;
	}

	return (size_t) bytes_read;
}

int
ogg_helper_seek (void *datasource, ogg_int64_t offset, int whence)
{
	GnomeVFSResult res;
	GnomeVFSHandle *file = (GnomeVFSHandle *)datasource;

	res = gnome_vfs_seek(file, whence, offset);
	if (res != GNOME_VFS_OK)
		return -1;

	return 0;
}

int
ogg_helper_close (void *datasource)
{
	GnomeVFSResult res;
	GnomeVFSHandle *file = (GnomeVFSHandle *)datasource;

	res = gnome_vfs_close(file);
	if (res != GNOME_VFS_OK)
		return -1;

	return 0;
}

/*
 * VorbisFile behaves inconsistently when it comes to close or not files
 * <xiphmont> If it's *not* a vorbis file, it doesn't close it.  If it
 * *is* but there's an error it does.  I need to correct that
 * <xiphmont> Like I said, this inconsistency only pops up on filesystem
 * fault.  Or a buggy callback ;-)
 *
 * Proposed work-around in applications:
 * <xiphmont> make the 'close' callback do nothing and close it yourself is
 * one way.
 *
 * Thanks to Monty for the help
 */
int
ogg_helper_close_dummy (void *datasource)
{
	return 0;
}

long
ogg_helper_tell (void *datasource)
{
	GnomeVFSFileSize offset;
	GnomeVFSResult res;
	GnomeVFSHandle *file = (GnomeVFSHandle *)datasource;

	res = gnome_vfs_tell (file, &offset);
	if (res != GNOME_VFS_OK)
		return -1;

	return (long) offset;
}

