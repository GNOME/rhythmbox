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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __RB_FILE_HELPERS_H
#define __RB_FILE_HELPERS_H

G_BEGIN_DECLS

const char *	rb_file			(const char *filename);

const char *	rb_dot_dir		(void);

void		rb_ensure_dir_exists	(const char *dir);

char *		rb_uri_resolve_symlink	(const char *uri);
gboolean	rb_uri_is_directory	(const char *uri);
gboolean	rb_uri_exists		(const char *uri);
char *		rb_uri_resolve_relative	(const char *uri);
gboolean	rb_uri_is_readable	(const char *uri);
gboolean	rb_uri_is_writable	(const char *uri);
gboolean	rb_uri_is_iradio	(const char *uri);

void		rb_uri_handle_recursively(const char *uri,
					  GFunc func,
					  gboolean *cancelflag,
					  gpointer user_data);

void		rb_file_helpers_init	(void);
void		rb_file_helpers_shutdown(void);

G_END_DECLS

#endif /* __RB_FILE_HELPERS_H */
