/*
 *  arch-tag: Implementation of various Rhythmbox utility functions for URIs and files
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

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <unistd.h>

#include "rb-file-helpers.h"
#include "rb-dialog.h"

static GHashTable *files = NULL;

static char *dot_dir = NULL;

const char *
rb_file (const char *filename)
{
	char *ret;
	int i;

	static char *paths[] =
	{
		SHARE_DIR "/",
		SHARE_DIR "/glade/",
		SHARE_DIR "/art/",
		SHARE_DIR "/views/",
		SHARE_DIR "/node-views/",
		SHARE_UNINSTALLED_DIR "/",
		SHARE_UNINSTALLED_DIR "/glade/",
		SHARE_UNINSTALLED_DIR "/art/",
		SHARE_UNINSTALLED_DIR "/node-views/"
	};
	
	g_assert (files != NULL);

	ret = g_hash_table_lookup (files, filename);
	if (ret != NULL)
		return ret;

	for (i = 0; i < (int) G_N_ELEMENTS (paths); i++)
	{
		ret = g_strconcat (paths[i], filename, NULL);
		if (g_file_test (ret, G_FILE_TEST_EXISTS) == TRUE)
		{
			g_hash_table_insert (files, g_strdup (filename), ret);
			return (const char *) ret;
		}
		g_free (ret);
	}

	rb_error_dialog (_("Failed to find %s"), filename);

	return NULL;
}

const char *
rb_dot_dir (void)
{
	if (dot_dir == NULL)
	{
		dot_dir = g_build_filename (g_get_home_dir (),
					    GNOME_DOT_GNOME,
					    "rhythmbox",
					    NULL);
	}
	
	return dot_dir;
}

void
rb_ensure_dir_exists (const char *dir)
{
	if (g_file_test (dir, G_FILE_TEST_IS_DIR) == FALSE)
	{
		if (g_file_test (dir, G_FILE_TEST_EXISTS) == TRUE)
			rb_error_dialog (_("%s exists, please move it out of the way."), dir);
		
		if (mkdir (dir, 488) != 0)
			rb_error_dialog (_("Failed to create directory %s."), dir);
	}
}

void
rb_file_helpers_init (void)
{
	files = g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       (GDestroyNotify) g_free,
				       (GDestroyNotify) g_free);
}

void
rb_file_helpers_shutdown (void)
{
	g_hash_table_destroy (files);

	g_free (dot_dir);
}

char *
rb_uri_resolve_symlink (const char *uri)
{
	GnomeVFSFileInfo *info;
	char *real;

	g_return_val_if_fail (uri != NULL, NULL);
	
	info = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (uri, info,
				 GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE);

	if (info->type == GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK) {
		GnomeVFSURI *vfsuri;
		GnomeVFSURI *new_vfsuri;

		vfsuri = gnome_vfs_uri_new (uri);
		new_vfsuri = gnome_vfs_uri_resolve_relative (vfsuri, info->symlink_name);
		real = gnome_vfs_uri_to_string (new_vfsuri, GNOME_VFS_URI_HIDE_NONE); 

		gnome_vfs_uri_unref (new_vfsuri);
		gnome_vfs_uri_unref (vfsuri);
	} else {
		real = g_strdup (uri);
	}

	gnome_vfs_file_info_unref (info);

	return real;
}

gboolean
rb_uri_is_directory (const char *uri)
{
	GnomeVFSFileInfo *info;
	gboolean dir;

	g_return_val_if_fail (uri != NULL, FALSE);

	info = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (uri, info,
				 GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY)
		dir = TRUE;
	else
		dir = FALSE;

	gnome_vfs_file_info_unref (info);

	return dir;
}

gboolean
rb_uri_exists (const char *uri)
{
	GnomeVFSURI *vuri;
	gboolean ret;
	
	g_return_val_if_fail (uri != NULL, FALSE);

	vuri = gnome_vfs_uri_new (uri);
	ret = gnome_vfs_uri_exists (vuri);
	gnome_vfs_uri_unref (vuri);

	return ret;
}

static gboolean
is_valid_scheme_character (char c)
{
	return g_ascii_isalnum (c) || c == '+' || c == '-' || c == '.';
}

/*
 * FIXME this is not the simplest or most time-efficent way
 * to do this.  Probably a far more clear way of doing this processing
 * is to split the path into segments, rather than doing the processing
 * in place.
 */
static void
remove_internal_relative_components (char *uri_current)
{
	char *segment_prev, *segment_cur;
	size_t len_prev, len_cur;

	len_prev = len_cur = 0;
	segment_prev = NULL;

	g_return_if_fail (uri_current != NULL);

	segment_cur = uri_current;

	while (*segment_cur) {
		len_cur = strcspn (segment_cur, "/");

		if (len_cur == 1 && segment_cur[0] == '.') {
			/* Remove "." 's */
			if (segment_cur[1] == '\0') {
				segment_cur[0] = '\0';
				break;
			} else {
				memmove (segment_cur, segment_cur + 2, strlen (segment_cur + 2) + 1);
				continue;
			}
		} else if (len_cur == 2 && segment_cur[0] == '.' && segment_cur[1] == '.' ) {
			/* Remove ".."'s (and the component to the left of it) that aren't at the
			 * beginning or to the right of other ..'s
			 */
			if (segment_prev) {
				if (! (len_prev == 2
				       && segment_prev[0] == '.'
				       && segment_prev[1] == '.')) {
				       	if (segment_cur[2] == '\0') {
						segment_prev[0] = '\0';
						break;
				       	} else {
						memmove (segment_prev, segment_cur + 3, strlen (segment_cur + 3) + 1);

						segment_cur = segment_prev;
						len_cur = len_prev;

						/* now we find the previous segment_prev */
						if (segment_prev == uri_current) {
							segment_prev = NULL;
						} else if (segment_prev - uri_current >= 2) {
							segment_prev -= 2;
							for ( ; segment_prev > uri_current && segment_prev[0] != '/' 
							      ; segment_prev-- );
							if (segment_prev[0] == '/') {
								segment_prev++;
							}
						}
						continue;
					}
				}
			}
		}

		/*Forward to next segment */

		if (segment_cur [len_cur] == '\0') {
			break;
		}
		 
		segment_prev = segment_cur;
		len_prev = len_cur;
		segment_cur += len_cur + 1;	
	}	
}

static gboolean
is_uri_partial (const char *uri)
{
	const char *current;

        /* RFC 2396 section 3.1 */
        for (current = uri ;
		*current
	        &&      ((*current >= 'a' && *current <= 'z')
	                 || (*current >= 'A' && *current <= 'Z')
	                 || (*current >= '0' && *current <= '9')
	                 || ('-' == *current)
			 || ('+' == *current)
                         || ('.' == *current)) ;
		current++);

	return  !(':' == *current);
}

/**
 * eel_uri_make_full_from_relative:
 * 
 * Returns a full URI given a full base URI, and a secondary URI which may
 * be relative.
 *
 * Return value: the URI (NULL for some bad errors).
 *
 * FIXME: This code has been copied from eel-mozilla-content-view
 * because eel-mozilla-content-view cannot link with libeel-extensions
 * due to lame license issues.  Really, this belongs in gnome-vfs, but was added
 * after the Gnome 1.4 gnome-vfs API freeze
 **/

static char *
eel_uri_make_full_from_relative (const char *base_uri, const char *relative_uri)
{
	char *result = NULL;

	/* See section 5.2 in RFC 2396 */

	if (base_uri == NULL && relative_uri == NULL) {
		result = NULL;
	} else if (base_uri == NULL) {
		result = g_strdup (relative_uri);
	} else if (relative_uri == NULL) {
		result = g_strdup (base_uri);
	} else if (!is_uri_partial (relative_uri)) {
		result = g_strdup (relative_uri);
	} else {
		char *mutable_base_uri;
		char *mutable_uri;

		char *uri_current;
		size_t base_uri_length;
		char *separator;

		mutable_base_uri = g_strdup (base_uri);
		uri_current = mutable_uri = g_strdup (relative_uri);

		/* Chew off Fragment and Query from the base_url */

		separator = strrchr (mutable_base_uri, '#'); 

		if (separator) {
			*separator = '\0';
		}

		separator = strrchr (mutable_base_uri, '?');

		if (separator) {
			*separator = '\0';
		}

		if ('/' == uri_current[0] && '/' == uri_current [1]) {
			/* Relative URI's beginning with the authority
			 * component inherit only the scheme from their parents
			 */

			separator = strchr (mutable_base_uri, ':');

			if (separator) {
				separator[1] = '\0';
			}			  
		} else if ('/' == uri_current[0]) {
			/* Relative URI's beginning with '/' absolute-path based
			 * at the root of the base uri
			 */

			separator = strchr (mutable_base_uri, ':');

			/* g_assert (separator), really */
			if (separator) {
				/* If we start with //, skip past the authority section */
				if ('/' == separator[1] && '/' == separator[2]) {
					separator = strchr (separator + 3, '/');
					if (separator) {
						separator[0] = '\0';
					}
				} else {
				/* If there's no //, just assume the scheme is the root */
					separator[1] = '\0';
				}
			}
		} else if ('#' != uri_current[0]) {
			/* Handle the ".." convention for relative uri's */

			/* If there's a trailing '/' on base_url, treat base_url
			 * as a directory path.
			 * Otherwise, treat it as a file path, and chop off the filename
			 */

			base_uri_length = strlen (mutable_base_uri);
			if ('/' == mutable_base_uri[base_uri_length-1]) {
				/* Trim off '/' for the operation below */
				mutable_base_uri[base_uri_length-1] = 0;
			} else {
				separator = strrchr (mutable_base_uri, '/');
				if (separator) {
					*separator = '\0';
				}
			}

			remove_internal_relative_components (uri_current);

			/* handle the "../"'s at the beginning of the relative URI */
			while (0 == strncmp ("../", uri_current, 3)) {
				uri_current += 3;
				separator = strrchr (mutable_base_uri, '/');
				if (separator) {
					*separator = '\0';
				} else {
					/* <shrug> */
					break;
				}
			}

			/* handle a ".." at the end */
			if (uri_current[0] == '.' && uri_current[1] == '.' 
			    && uri_current[2] == '\0') {

			    	uri_current += 2;
				separator = strrchr (mutable_base_uri, '/');
				if (separator) {
					*separator = '\0';
				}
			}

			/* Re-append the '/' */
			mutable_base_uri [strlen(mutable_base_uri)+1] = '\0';
			mutable_base_uri [strlen(mutable_base_uri)] = '/';
		}

		result = g_strconcat (mutable_base_uri, uri_current, NULL);
		g_free (mutable_base_uri); 
		g_free (mutable_uri); 
	}
	
	return result;
}

/* Note that NULL's and full paths are also handled by this function.
 * A NULL location will return the current working directory
 */
static char *
file_uri_from_local_relative_path (const char *location)
{
	char *current_dir;
	char *base_uri, *base_uri_slash;
	char *location_escaped;
	char *uri;

	current_dir = g_get_current_dir ();
	base_uri = gnome_vfs_get_uri_from_local_path (current_dir);
	/* g_get_current_dir returns w/o trailing / */
	base_uri_slash = g_strconcat (base_uri, "/", NULL);

	location_escaped = gnome_vfs_escape_path_string (location);

	uri = eel_uri_make_full_from_relative (base_uri_slash, location_escaped);

	g_free (location_escaped);
	g_free (base_uri_slash);
	g_free (base_uri);
	g_free (current_dir);

	return uri;
}

static gboolean
has_valid_scheme (const char *uri)
{
        const char *p;

        p = uri;

        if (!is_valid_scheme_character (*p)) {
                return FALSE;
        }

        do {
                p++;
        } while (is_valid_scheme_character (*p));

        return *p == ':';
}

/**
 * eel_make_uri_from_shell_arg:
 *
 * Similar to eel_make_uri_from_input, except that:
 * 
 * 1) guesses relative paths instead of http domains
 * 2) doesn't bother stripping leading/trailing white space
 * 3) doesn't bother with ~ expansion--that's done by the shell
 *
 * @location: a possibly mangled "uri"
 *
 * returns a newly allocated uri
 *
 **/
char *
rb_uri_resolve_relative (const char *location)
{
	char *uri;

	g_return_val_if_fail (location != NULL, g_strdup (""));

	switch (location[0]) {
	case '\0':
		uri = g_strdup ("");
		break;
	case '/':
		uri = gnome_vfs_get_uri_from_local_path (location);
		break;
	default:
		if (has_valid_scheme (location)) {
			uri = g_strdup (location);
		} else {
			uri = file_uri_from_local_relative_path (location);
		}
	}

	return uri;
}

static gboolean
have_uid (guint uid)
{
	return (uid == getuid ());
}

static gboolean
have_gid (guint gid)
{
	gid_t gids[100];
	int n_groups, i;

	n_groups = getgroups (100, gids);

	for (i = 0; i < n_groups; i++)
	{
		if (gids[i] == getegid ())
			continue;
		if (gids[i] == gid)
			return TRUE;
	}

	return FALSE;
}

gboolean
rb_uri_is_readable (const char *text_uri)
{
	GnomeVFSFileInfo *info;
	gboolean ret = FALSE;

	info = gnome_vfs_file_info_new ();
	if (info == NULL)
		return FALSE;
	if (gnome_vfs_get_file_info (text_uri, info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS) != GNOME_VFS_OK)
		return FALSE;

	if ((info->permissions & GNOME_VFS_PERM_OTHER_READ) ||
	    ((info->permissions & GNOME_VFS_PERM_USER_READ) &&
	     (have_uid (info->uid) == TRUE)) ||
	    ((info->permissions & GNOME_VFS_PERM_GROUP_READ) &&
	     (have_gid (info->gid) == TRUE)))
		ret = TRUE;

	gnome_vfs_file_info_unref (info);

	return ret;
}

gboolean
rb_uri_is_writable (const char *text_uri)
{
	GnomeVFSFileInfo *info;
	gboolean ret = FALSE;

	info = gnome_vfs_file_info_new ();
	if (info == NULL)
		return FALSE;
	if (gnome_vfs_get_file_info (text_uri, info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS) != GNOME_VFS_OK)
		return FALSE;

	if ((info->permissions & GNOME_VFS_PERM_OTHER_WRITE) ||
	    ((info->permissions & GNOME_VFS_PERM_USER_WRITE) &&
	     (have_uid (info->uid) == TRUE)) ||
	    ((info->permissions & GNOME_VFS_PERM_GROUP_WRITE) &&
	     (have_gid (info->gid) == TRUE)))
		ret = TRUE;

	gnome_vfs_file_info_unref (info);

	return ret;
}

gboolean
rb_uri_is_iradio (const char *uri)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	if (strstr (uri, "://") == NULL)
		return FALSE;

	if (strncmp ("http", uri, 4) == 0)
		return TRUE;
	if (strncmp ("pnm", uri, 3) == 0)
		return TRUE;
	if (strncmp ("rtsp", uri, 4) == 0)
		return TRUE;

	return FALSE;
}

void
rb_uri_handle_recursively (const char *text_uri,
		           GFunc func,
			   gboolean *cancelflag,
		           gpointer user_data)
{
	GList *list, *l;
	GnomeVFSURI *uri;

	uri = gnome_vfs_uri_new (text_uri);

	if (gnome_vfs_directory_list_load (&list, text_uri,
				           (GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
					    GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
					    GNOME_VFS_FILE_INFO_FOLLOW_LINKS)) != GNOME_VFS_OK)
		return;

	for (l = list; l != NULL; l = g_list_next (l))
	{
		GnomeVFSFileInfo *info;
		GnomeVFSURI *file_uri;
		char *file_uri_text;

		if (cancelflag && *cancelflag)
			break;

		info = (GnomeVFSFileInfo *) l->data;
		
		file_uri = gnome_vfs_uri_append_path (uri, info->name);
		if (file_uri == NULL)
			continue;
		file_uri_text = gnome_vfs_uri_to_string (file_uri,
							 GNOME_VFS_URI_HIDE_NONE);
		gnome_vfs_uri_unref (file_uri);

		if (info->type != GNOME_VFS_FILE_TYPE_REGULAR)
		{
			if ((info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) &&
			    (info->name) && (strcmp (info->name, "."))
			    && (strcmp (info->name, "..")))
			{
				rb_uri_handle_recursively (file_uri_text,
							   func,
							   cancelflag,
							   user_data);

			}
		}
		else
			(*func) (file_uri_text, user_data);

		g_free (file_uri_text);
	}

	gnome_vfs_file_info_list_free (list);
	gnome_vfs_uri_unref (uri);
}
