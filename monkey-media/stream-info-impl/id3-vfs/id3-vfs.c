/*  monkey-sound
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

#include "id3-vfs.h"

# ifdef HAVE_CONFIG_H
#  include "config.h"
# endif

# include "global.h"

# include <stdio.h>
# include <stdlib.h>
# include <string.h>

# ifdef HAVE_ASSERT_H
#  include <assert.h>
# endif

# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

# include "file.h"
# include "tag.h"
# include "field.h"

struct vfstag {
  struct id3_tag *tag;
  unsigned long location;
  id3_length_t length;
};

struct id3_vfs_file {
  GnomeVFSHandle *iofile;
  enum id3_vfs_mode mode;
  int flags;
  int options;
  struct id3_tag *primary;
  unsigned int ntags;
  struct vfstag *tags;
};

enum {
  ID3_FILE_FLAG_ID3V1 = 0x0001
};

/*
 * NAME:	query_tag()
 * DESCRIPTION:	check for a tag at a file's current position
 */
static
signed long query_tag(GnomeVFSHandle *iofile)
{
  GnomeVFSFileSize save_position, bytes_read = -1;
  id3_byte_t query[ID3_TAG_QUERYSIZE];
  signed long size;

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

  gnome_vfs_read(iofile, query, sizeof(query), &bytes_read);
  size = id3_tag_query(query, bytes_read);

  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

  return size;
}

/*
 * NAME:	read_tag()
 * DESCRIPTION:	read and parse a tag at a file's current position
 */
static
struct id3_tag *read_tag(GnomeVFSHandle *iofile, id3_length_t size)
{
  GnomeVFSFileSize bytes_read = -1;
  GnomeVFSResult res;
  id3_byte_t *data;
  struct id3_tag *tag = 0;

  data = malloc(size);
  if (data) {
    res = gnome_vfs_read (iofile, data, size, &bytes_read);
    if (res == GNOME_VFS_OK)
    {
      tag = id3_tag_parse(data, bytes_read);
    }

    free(data);
  }

  return tag;
}

/*
 * NAME:	update_primary()
 * DESCRIPTION:	update the primary tag with data from a new tag
 */
static
int update_primary(struct id3_tag *tag, struct id3_tag const *new)
{
  unsigned int i;

  if (!(new->extendedflags & ID3_TAG_EXTENDEDFLAG_TAGISANUPDATE))
    id3_tag_clearframes(tag);

  for (i = 0; i < new->nframes; ++i) {
    if (id3_tag_attachframe(tag, new->frames[i]) == -1)
      return -1;
  }

  return 0;
}

/*
 * NAME:	add_tag()
 * DESCRIPTION:	read, parse, and add a tag to a file structure
 */
static
int add_tag(struct id3_vfs_file *file, id3_length_t length)
{
  GnomeVFSFileSize location = -1;
  unsigned int i;
  struct vfstag filetag, *tags;
  struct id3_tag *tag;

  if (gnome_vfs_tell(file->iofile, &location) != GNOME_VFS_OK)
    return -1;

  /* check for duplication/overlap */
  {
    unsigned long begin1, end1, begin2, end2;

    begin1 = location;
    end1   = begin1 + length;

    for (i = 0; i < file->ntags; ++i) {
      begin2 = file->tags[i].location;
      end2   = begin2 + file->tags[i].length;

      if (begin1 == begin2 && end1 == end2)
	return 0;  /* duplicate */

      if (begin1 < end2 && end1 > begin2)
	return -1;  /* overlap */
    }
  }

  tags = realloc(file->tags, (file->ntags + 1) * sizeof(*tags));
  if (tags == 0)
    return -1;

  file->tags = tags;

  tag = read_tag(file->iofile, length);
  if (tag == 0)
    return -1;

  if (update_primary(file->primary, tag) == -1) {
    id3_tag_delete(tag);
    return -1;
  }

  filetag.tag      = tag;
  filetag.location = location;
  filetag.length   = length;

  file->tags[file->ntags++] = filetag;

  id3_tag_addref(tag);

  return 0;
}

/*
 * NAME:	search_tags()
 * DESCRIPTION:	search for tags in a file
 */
static
int search_tags(struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position = -1;
  signed long size;
  int result = 0;

  if (gnome_vfs_tell(file->iofile, &save_position) != GNOME_VFS_OK)
    return -1;

  /* look for an ID3v1 tag */

  if (gnome_vfs_seek(file->iofile, GNOME_VFS_SEEK_END,
      (GnomeVFSFileOffset)-128) == GNOME_VFS_OK) {
    size = query_tag(file->iofile);
    if (size > 0) {
      if (add_tag(file, size) == -1)
	goto fail;

      file->flags   |= ID3_FILE_FLAG_ID3V1;
      file->options |= ID3_FILE_OPTION_ID3V1;
    }
  }

  /* look for a tag at the beginning of the file */

  gnome_vfs_seek(file->iofile, GNOME_VFS_SEEK_START, 0);

  size = query_tag(file->iofile);
  if (size > 0) {
    struct id3_frame const *frame;

    if (add_tag(file, size) == -1)
      goto fail;

    /* locate tags indicated by SEEK frames */

    while ((frame =
	    id3_tag_findframe(file->tags[file->ntags - 1].tag, "SEEK", 0))) {
      long seek;

      seek = id3_field_getint(&frame->fields[0]);
      if (seek < 0 || gnome_vfs_seek
          (file->iofile, GNOME_VFS_SEEK_CURRENT,
	   (GnomeVFSFileOffset)seek) != GNOME_VFS_OK)
	break;

      size = query_tag(file->iofile);
      if (size <= 0)
	break;
      else if (add_tag(file, size) == -1)
	goto fail;
    }
  }

  /* look for a tag at the end of the file (before any ID3v1 tag) */

  if (gnome_vfs_seek(file->iofile, GNOME_VFS_SEEK_END,
      ((file->flags & ID3_FILE_FLAG_ID3V1) ? -128 : 0) + -10) == GNOME_VFS_OK) {
    size = query_tag(file->iofile);
    if (size < 0 && gnome_vfs_seek
        (file->iofile, GNOME_VFS_SEEK_CURRENT, (GnomeVFSFileOffset)size)
	== GNOME_VFS_OK) {
      size = query_tag(file->iofile);
      if (size > 0 && add_tag(file, size) == -1)
	goto fail;
    }
  }

  if (0) {
  fail:
    result = -1;
  }

  if (gnome_vfs_tell(file->iofile, &save_position) != GNOME_VFS_OK)
    return -1;

  return result;
}

/*
 * NAME:	finish_file()
 * DESCRIPTION:	release memory associated with a file
 */
static
void finish_file(struct id3_vfs_file *file)
{
  unsigned int i;

  if (file->primary) {
    id3_tag_delref(file->primary);
    id3_tag_delete(file->primary);
  }

  for (i = 0; i < file->ntags; ++i) {
    id3_tag_delref(file->tags[i].tag);
    id3_tag_delete(file->tags[i].tag);
  }

  if (file->tags)
    free(file->tags);

  free(file);
}

/*
 * NAME:	new_file()
 * DESCRIPTION:	create a new file structure and load tags
 */
static
struct id3_vfs_file *new_file(GnomeVFSHandle *iofile, enum id3_vfs_mode mode)
{
  struct id3_vfs_file *file;

  file = malloc(sizeof(*file));
  if (file == 0)
    goto fail;

  file->iofile  = iofile;
  file->mode    = mode;
  file->flags   = 0;
  file->options = 0;

  file->ntags   = 0;
  file->tags    = 0;

  file->primary = id3_tag_new();
  if (file->primary == 0)
    goto fail;

  id3_tag_addref(file->primary);

  /* load tags from the file */

  if (search_tags(file) == -1)
    goto fail;

  if (0) {
  fail:
    if (file) {
      finish_file(file);
      file = NULL;
    }
  }

  return file;
}

/*
 * NAME:	file->open()
 * DESCRIPTION:	open a file given its pathname
 */
struct id3_vfs_file *id3_vfs_open(char const *path, enum id3_vfs_mode mode)
{
  GnomeVFSHandle *iofile = NULL;
  struct id3_vfs_file *file;

  /* Try opening the file */
  if (gnome_vfs_open(&iofile, path,
      (mode == ID3_VFS_MODE_READWRITE) ?
      GNOME_VFS_OPEN_READ&GNOME_VFS_OPEN_WRITE : GNOME_VFS_OPEN_READ)
        != GNOME_VFS_OK)
    return 0;

  /* Try the different SEEK types that might fail */
  if ((gnome_vfs_seek(iofile, GNOME_VFS_SEEK_END, 0) != GNOME_VFS_OK)
      || gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, 0) != GNOME_VFS_OK)
  {
	  gnome_vfs_close (iofile);
	  return 0;
  }

  file = new_file(iofile, mode);
  if (file == 0)
    gnome_vfs_close(iofile);

  return file;
}

/*
 * NAME:	file->fdopen()
 * DESCRIPTION:	open a file using an existing file descriptor
 *
 * Unsupported with the vfs interface
 */

/*
 * NAME:	file->close()
 * DESCRIPTION:	close a file and delete its associated tags
 */
void id3_vfs_close(struct id3_vfs_file *file)
{
  gnome_vfs_close(file->iofile);

  finish_file(file);
}

/*
 * NAME:	file->tag()
 * DESCRIPTION:	return the primary tag structure for a file
 */
struct id3_tag *id3_vfs_tag(struct id3_vfs_file const *file)
{
  return file->primary;
}

/*
 * NAME:	file->update()
 * DESCRIPTION:	rewrite tag(s) to a file
 */
int id3_vfs_update(struct id3_vfs_file *file)
{
  id3_length_t size;
  unsigned char id3v1_data[128], *id3v1 = 0, *id3v2 = 0;

  if (file->mode != ID3_VFS_MODE_READWRITE)
    return -1;

  if (file->options & ID3_FILE_OPTION_ID3V1) {
    file->primary->options |= ID3_TAG_OPTION_ID3V1;

    size = id3_tag_render(file->primary, 0);
    if (size) {
      assert(size == sizeof(id3v1_data));

      size = id3_tag_render(file->primary, id3v1_data);
      if (size) {
	assert(size == sizeof(id3v1_data));
	id3v1 = id3v1_data;
      }
    }
  }

  file->primary->options &= ~ID3_TAG_OPTION_ID3V1;

  size = id3_tag_render(file->primary, 0);
  if (size) {
    id3v2 = malloc(size);
    if (id3v2 == 0)
      return -1;

    size = id3_tag_render(file->primary, id3v2);
    if (size == 0) {
      free(id3v2);
      id3v2 = 0;
    }
  }

  /* ... */

  if (id3v2)
    free(id3v2);

  return 0;
}

int bitrates[2][16] = {
{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,},          /* MPEG2 */
{0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,}}; /* MPEG1 */

long samprates[2][3] = {
{ 22050, 24000, 16000 },  /* MPEG2 */
{ 44100, 48000, 32000 }}; /* MPEG1 */

static int extractI4(unsigned char *buf)
{
	int x;
	/* big endian extract */

	x = buf[0];
	x <<= 8;
	x |= buf[1];
	x <<= 8;
	x |= buf[2];
	x <<= 8;
	x |= buf[3];

	return(x);
}

/* check for valid MPEG header */
static int is_mphead(unsigned char *buf)
{
	if (buf[0] != 0xff) return(0);		
	if ((buf[1] & 0xf0) != 0xf0) return(0);	 /* 12 bits framesync */
	
	return(1); 
}

/* check for valid "Xing" VBR header */
static int is_xhead(unsigned char *buf)
{
	if (buf[0] != 'X') return(0);
	if (buf[1] != 'i') return(0);
	if (buf[2] != 'n') return(0);
	if (buf[3] != 'g') return(0);
	
	return(1);
}

int id3_vfs_bitrate (struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position;
  GnomeVFSHandle *iofile = file->iofile;
  GnomeVFSResult res;
	int bitrate = 0;
	GnomeVFSFileSize length_read;
	guchar buffer[8192];

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

	gnome_vfs_seek (iofile, GNOME_VFS_SEEK_START, 0);

  res = gnome_vfs_read (iofile, buffer, sizeof (buffer), &length_read);
  if ((res == GNOME_VFS_OK) && (length_read > 512))
  {
	  int i = 0;
		int ver,srindex,brindex,xbytes,xframes;
		long long total_bytes, magic1, magic2;
	
	  while(!is_mphead(buffer+i)) {
		  i++;
		  if (i>length_read-4) goto bitdone;  /* no valid header, give up */
	  }	

	  ver = (buffer[i+1] & 0x08) >> 3;
	  brindex = (buffer[i+2] & 0xf0) >> 4;
	  srindex = (buffer[i+2] & 0x0c) >> 2;

	  /* but if there is a Xing header we'll use that instead... */	
	  i=0;
	  while(!is_xhead(buffer+i)) {
		  i++;
		  if (i>length_read-16)
			{
			  bitrate = (bitrates[ver][brindex]);
		    goto bitdone;
			}
	  }

	  xframes = extractI4(buffer+i+8);
	  xbytes = extractI4(buffer+i+12);

	  if (xframes <= 0) {
		  bitrate = 0;
		  goto bitdone;
	  }

	  total_bytes = (long long) samprates[ver][srindex] * (long long) xbytes;
	  magic1 = total_bytes / (long long) (576 + ver * 576);
	  magic2 = magic1 / (long long) xframes;
	  bitrate = (int) ((long long) magic2 / (long long) 125);
  }

bitdone:
  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

	return bitrate;
}

long id3_vfs_samplerate (struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position;
  GnomeVFSHandle *iofile = file->iofile;
  GnomeVFSResult res;
	long samprate = 0;
	GnomeVFSFileSize length_read;
	guchar buffer[8192];

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

	gnome_vfs_seek (iofile, GNOME_VFS_SEEK_START, 0);

  res = gnome_vfs_read (iofile, buffer, sizeof (buffer), &length_read);
  if ((res == GNOME_VFS_OK) && (length_read > 512))
  {
	  int i = 0;
		int ver,srindex;
	
	  while(!is_mphead(buffer+i)) {
		  i++;
		  if (i>length_read-4) goto sampdone;  /* no valid header, give up */
	  }	

	  ver = (buffer[i+1] & 0x08) >> 3;
	  srindex = (buffer[i+2] & 0x0c) >> 2;

		samprate = (long) (samprates[ver][srindex]);
  }

sampdone:
  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

	return samprate;
}

int id3_vfs_channels (struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position;
  GnomeVFSHandle *iofile = file->iofile;
  GnomeVFSResult res;
	int channels = 0;
	GnomeVFSFileSize length_read;
	guchar buffer[8192];

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

	gnome_vfs_seek (iofile, GNOME_VFS_SEEK_START, 0);

  res = gnome_vfs_read (iofile, buffer, sizeof (buffer), &length_read);
  if ((res == GNOME_VFS_OK) && (length_read > 512))
  {
	  int i = 0;
	
	  while(!is_mphead(buffer+i)) {
		  i++;
		  if (i>length_read-4) goto chandone;  /* no valid header, give up */
	  }	

		channels = (((buffer[i+3] & 0xc0) >> 6) == 2) ? 1 : 2;
  }

chandone:
  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

	return channels;
}

gboolean id3_vfs_vbr (struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position;
  GnomeVFSHandle *iofile = file->iofile;
  GnomeVFSResult res;
	gboolean vbr = FALSE;
	GnomeVFSFileSize length_read;
	guchar buffer[8192];

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

	gnome_vfs_seek (iofile, GNOME_VFS_SEEK_START, 0);

  res = gnome_vfs_read (iofile, buffer, sizeof (buffer), &length_read);
  if ((res == GNOME_VFS_OK) && (length_read > 512))
  {
	  int i = 0;
	
	  while(!is_mphead(buffer+i)) {
		  i++;
		  if (i>length_read-4) goto vbrdone;  /* no valid header, give up */
	  }	

	  /* but if there is a Xing header we'll use that instead... */	
	  i=0;
	  while(!is_xhead(buffer+i)) {
		  i++;
		  if (i>length_read-16) goto vbrdone;
	  }

		vbr = TRUE;
  }

vbrdone:
  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

	return vbr;
}

int id3_vfs_version (struct id3_vfs_file *file)
{
  GnomeVFSFileSize save_position;
  GnomeVFSHandle *iofile = file->iofile;
  GnomeVFSResult res;
	int version = 0;
	GnomeVFSFileSize length_read;
	guchar buffer[8192];

  if (gnome_vfs_tell(iofile, &save_position) != GNOME_VFS_OK)
    return 0;

	gnome_vfs_seek (iofile, GNOME_VFS_SEEK_START, 0);

  res = gnome_vfs_read (iofile, buffer, sizeof (buffer), &length_read);
  if ((res == GNOME_VFS_OK) && (length_read > 512))
  {
	  int i = 0;
	
	  while(!is_mphead(buffer+i)) {
		  i++;
		  if (i>length_read-4) goto verdone;  /* no valid header, give up */
	  }	

		version = (buffer[i+1] & 0x08) >> 3;
  }

verdone:
  if (gnome_vfs_seek(iofile, GNOME_VFS_SEEK_START, save_position)
      != GNOME_VFS_OK)
    return 0;

	return version;
}
