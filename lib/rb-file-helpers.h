/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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
#include <gio/gio.h>

G_BEGIN_DECLS

const char *	rb_file			(const char *filename);
const char *	rb_user_data_dir	(void);
const char *	rb_user_cache_dir	(void);
const char *	rb_music_dir		(void);
const char *	rb_locale_dir		(void);

char *		rb_find_user_data_file	(const char *name);
char *		rb_find_user_cache_file	(const char *name);
char *		rb_find_plugin_data_file (GObject *plugin,
					 const char *name);
char *		rb_find_plugin_resource (GObject *plugin,
					 const char *name);

char *		rb_canonicalise_uri	(const char *uri);

gboolean	rb_uri_mkstemp		(const char *prefix, char **uri_ret,
					 GOutputStream **stream, GError **error);

char *		rb_uri_resolve_symlink	(const char *uri, GError **error);
GFile *		rb_file_resolve_symlink	(GFile *file, GError **error);
gboolean	rb_uri_is_directory	(const char *uri);
gboolean	rb_uri_exists		(const char *uri);
gboolean	rb_uri_is_readable	(const char *uri);
gboolean	rb_uri_is_writable	(const char *uri);
gboolean	rb_uri_is_local		(const char *uri);
gboolean	rb_uri_is_hidden	(const char *uri);
gboolean	rb_uri_could_be_podcast (const char *uri, gboolean *is_opml);
gboolean	rb_uri_is_descendant	(const char *uri, const char *ancestor);
char *		rb_uri_make_hidden      (const char *uri);
char *		rb_uri_get_dir_name	(const char *uri);
char *		rb_uri_get_short_path_name (const char *uri);
char *		rb_uri_get_mount_point  (const char *uri);


/* return TRUE to recurse further, FALSE to stop */
typedef gboolean (*RBUriRecurseFunc) (GFile *file, GFileInfo *info, gpointer data);

void		rb_uri_handle_recursively(const char *uri,
					  GCancellable *cancel,
					  RBUriRecurseFunc func,
					  gpointer user_data);

void		rb_uri_handle_recursively_async(const char *uri,
						GCancellable *cancel,
						RBUriRecurseFunc func,
						gpointer user_data,
						GDestroyNotify data_destroy);

char*		rb_uri_append_path	(const char *uri,
					 const char *path);
char*		rb_uri_append_uri	(const char *uri,
					 const char *fragment);

gboolean	rb_check_dir_has_space	(GFile *dir, guint64 bytes_needed);
gboolean	rb_check_dir_has_space_uri (const char *uri, guint64 bytes_needed);

GFile *		rb_file_find_extant_parent (GFile *file);

gboolean	rb_uri_create_parent_dirs (const char *uri, GError **error);

void		rb_file_helpers_init	(void);
void		rb_file_helpers_shutdown(void);

char *		rb_uri_get_filesystem_type (const char *uri, char **mount_point);
void		rb_sanitize_path_for_msdos_filesystem (char *path);
char *		rb_sanitize_uri_for_filesystem(const char *uri, const char *filesystem);

G_END_DECLS

#endif /* __RB_FILE_HELPERS_H */
