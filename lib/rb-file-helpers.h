/*
 *  arch-tag: Header for various Rhythmbox utility functions for URIs and files
 *
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_FILE_HELPERS_H
#define __RB_FILE_HELPERS_H

#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>

G_BEGIN_DECLS

const char *	rb_file			(const char *filename);
const char *	rb_dot_dir		(void);
const char *	rb_music_dir		(void);

char *		rb_canonicalise_uri	(const char *uri);

GnomeVFSResult	rb_uri_mkstemp		(const char *prefix, char **uri,
					 GnomeVFSHandle **handle);

char *		rb_uri_resolve_symlink	(const char *uri);
gboolean	rb_uri_is_directory	(const char *uri);
gboolean	rb_uri_exists		(const char *uri);
char *		rb_uri_resolve_relative	(const char *uri);
gboolean	rb_uri_is_readable	(const char *uri);
gboolean	rb_uri_is_writable	(const char *uri);
gboolean	rb_uri_is_local		(const char *uri);
gboolean	rb_uri_is_hidden	(const char *uri);
char *		rb_uri_get_dir_name	(const char *uri);
char *		rb_uri_get_short_path_name (const char *uri);

typedef void (*RBUriRecurseFunc) (const char *uri, gboolean dir, gpointer data);

void		rb_uri_handle_recursively(const char *uri,
					  RBUriRecurseFunc func,
					  gboolean *cancelflag,
					  gpointer user_data);

void		rb_uri_handle_recursively_async(const char *uri,
						RBUriRecurseFunc func,
						gboolean *cancelflag,
						gpointer user_data,
						GDestroyNotify data_destroy);

char*		rb_uri_append_path	(const char *uri,
					 const char *path);
char*		rb_uri_append_uri	(const char *uri,
					 const char *fragment);

void		rb_file_helpers_init	(void);
void		rb_file_helpers_shutdown(void);

G_END_DECLS

#endif /* __RB_FILE_HELPERS_H */
