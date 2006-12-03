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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
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
#include "rb-debug.h"

static GHashTable *files = NULL;

static char *dot_dir = NULL;

const char *
rb_file (const char *filename)
{
	char *ret;
	int i;

	static char *paths[] = {
#ifdef SHARE_UNINSTALLED_DIR
		SHARE_UNINSTALLED_DIR "/",
		SHARE_UNINSTALLED_DIR "/ui/",
		SHARE_UNINSTALLED_DIR "/glade/",
		SHARE_UNINSTALLED_DIR "/art/",
#endif
		SHARE_DIR "/",
		SHARE_DIR "/glade/",
		SHARE_DIR "/art/",
	};
	
	g_assert (files != NULL);

	ret = g_hash_table_lookup (files, filename);
	if (ret != NULL)
		return ret;

	for (i = 0; i < (int) G_N_ELEMENTS (paths); i++) {
		ret = g_strconcat (paths[i], filename, NULL);
		if (g_file_test (ret, G_FILE_TEST_EXISTS) == TRUE) {
			g_hash_table_insert (files, g_strdup (filename), ret);
			return (const char *) ret;
		}
		g_free (ret);
	}

	return NULL;
}

const char *
rb_dot_dir (void)
{
	if (dot_dir == NULL) {
		dot_dir = g_build_filename (g_get_home_dir (),
					    GNOME_DOT_GNOME,
					    "rhythmbox",
					    NULL);
		if (mkdir (dot_dir, 0750) == -1)
			rb_debug ("unable to create Rhythmbox's dot dir");
	}
	
	return dot_dir;
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

#define MAX_LINK_LEVEL 5

char *
rb_uri_resolve_symlink (const char *uri)
{
	gint link_count;
	GnomeVFSFileInfo *info;
	char *followed;

	g_return_val_if_fail (uri != NULL, NULL);

	info = gnome_vfs_file_info_new ();
	gnome_vfs_get_file_info (uri, info, GNOME_VFS_FILE_INFO_DEFAULT);

	if (info->type != GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK) {
		gnome_vfs_file_info_unref (info);
		return g_strdup (uri);
	}

	link_count = 0;
	followed = g_strdup (uri);
	while (link_count < MAX_LINK_LEVEL) {
		GnomeVFSURI *vfs_uri;
		GnomeVFSURI *new_vfs_uri;
		char *escaped_path;

		vfs_uri = gnome_vfs_uri_new (followed);
		escaped_path = gnome_vfs_escape_path_string (info->symlink_name);
		new_vfs_uri = gnome_vfs_uri_resolve_relative (vfs_uri,
							      escaped_path);
		g_free (escaped_path);

		g_free (followed);
		followed = gnome_vfs_uri_to_string (new_vfs_uri,
						    GNOME_VFS_URI_HIDE_NONE); 
		link_count++;

		gnome_vfs_uri_unref (new_vfs_uri);
		gnome_vfs_uri_unref (vfs_uri);

		gnome_vfs_file_info_clear (info);
		gnome_vfs_get_file_info (followed, info,
					 GNOME_VFS_FILE_INFO_DEFAULT);

		if (info->type != GNOME_VFS_FILE_TYPE_SYMBOLIC_LINK) {
			gnome_vfs_file_info_unref (info);
			return followed;
		}
	}

	/* Too many symlinks */

	gnome_vfs_file_info_unref (info);

	return NULL;
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
rb_uri_is_local (const char *text_uri)
{
	return g_str_has_prefix (text_uri, "file://");
}

/*
 * gnome_vfs_uri_new escapes a few extra characters that
 * gnome_vfs_escape_path doesn't ('&' and '=').  If we
 * don't adjust our URIs to match, we end up with duplicate
 * entries, one with the characters encoded and one without.
 */
static char *
escape_extra_gnome_vfs_chars (char *uri)
{
	if (strspn (uri, "&=") != strlen (uri)) {
		char *tmp = gnome_vfs_escape_set (uri, "&=");
		g_free (uri);
		return tmp;
	}

	return uri;
}

typedef struct {
	char *uri;
	RBUriRecurseFunc func;
	gpointer user_data;
	gboolean *cancel_flag;
	GDestroyNotify data_destroy;
} RBUriHandleRecursivelyData;

typedef struct {
	RBUriHandleRecursivelyData data;

	/* real data */
	RBUriRecurseFunc func;
	gpointer user_data;

	GMutex *results_lock;
	guint results_idle_id;
	GList *uri_results;
	GList *dir_results;
} RBUriHandleRecursivelyAsyncData;

static void
_rb_uri_recurse_data_free (RBUriHandleRecursivelyData *data)
{
	g_free (data->uri);
	if (data->data_destroy)
		data->data_destroy (data->user_data);

	g_free (data);
}

static gboolean
rb_uri_handle_recursively_cb (const gchar *rel_path,
			      GnomeVFSFileInfo *info,
			      gboolean recursing_will_loop,
			      RBUriHandleRecursivelyData *data,
			      gboolean *recurse)
{
	char *path, *escaped_rel_path;
	gboolean dir;

	dir = (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY);

	if (data->cancel_flag && *data->cancel_flag)
		return TRUE;

	/* skip hidden and unreadable files and directories */
	if (g_str_has_prefix (rel_path, ".") ||
	    ((info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_ACCESS) &&
	    !(info->permissions & GNOME_VFS_PERM_ACCESS_READABLE))) {
		*recurse = FALSE;
		return TRUE;
	}

	escaped_rel_path = gnome_vfs_escape_path_string (rel_path);
	escaped_rel_path = escape_extra_gnome_vfs_chars (escaped_rel_path);
	path = g_build_filename (data->uri, escaped_rel_path, NULL);
	(data->func) (path, dir, data->user_data);
	g_free (escaped_rel_path);
	g_free (path);

	*recurse = !recursing_will_loop;
	return TRUE;
}

void
rb_uri_handle_recursively (const char *text_uri,
		           RBUriRecurseFunc func,
			   gboolean *cancelflag,
		           gpointer user_data)
{
	RBUriHandleRecursivelyData *data = g_new0 (RBUriHandleRecursivelyData, 1);
	GnomeVFSFileInfoOptions flags;
	GnomeVFSResult result;
	
	data->uri = g_strdup (text_uri);
	data->func = func;
	data->user_data = user_data;
	data->cancel_flag = cancelflag;
	data->data_destroy = NULL;

	flags = GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FOLLOW_LINKS |
		GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS;
	result = gnome_vfs_directory_visit (text_uri,
					    flags,
					    GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK,
					    (GnomeVFSDirectoryVisitFunc)rb_uri_handle_recursively_cb,
					    data);
	_rb_uri_recurse_data_free (data);
}

/* runs in main thread */
static gboolean
_recurse_async_idle_cb (RBUriHandleRecursivelyAsyncData *data)
{
	GList *ul, *dl;

	g_mutex_lock (data->results_lock);

	for (ul = data->uri_results, dl = data->dir_results;
	     ul != NULL;
	     ul = g_list_next (ul), dl = g_list_next (dl)) {
		g_assert (dl != NULL);

		data->func ((const char*)ul->data, (GPOINTER_TO_INT (dl->data) == 1), data->user_data);
		g_free (ul->data);
	}
	g_assert (dl == NULL);


	g_list_free (data->uri_results);
	data->uri_results = NULL;
	g_list_free (data->dir_results);
	data->dir_results = NULL;

	data->results_idle_id = 0;
	g_mutex_unlock (data->results_lock);
	return FALSE;
}

/* runs in main thread */
static gboolean
_recurse_async_data_free (RBUriHandleRecursivelyAsyncData *data)
{
	if (data->results_idle_id) {
		g_source_remove (data->results_idle_id);
		_recurse_async_idle_cb (data); /* process last results */
	}

	g_list_free (data->uri_results);
	data->uri_results = NULL;
	g_list_free (data->dir_results);
	data->dir_results = NULL;

	g_mutex_free (data->results_lock);
	_rb_uri_recurse_data_free (&data->data);

	return FALSE;
}

/* runs in worker thread */
static void
_recurse_async_cb (const char *uri, gboolean dir, RBUriHandleRecursivelyAsyncData *data)
{
	g_mutex_lock (data->results_lock);

	data->uri_results = g_list_prepend (data->uri_results, g_strdup (uri));
	data->dir_results = g_list_prepend (data->dir_results, GINT_TO_POINTER (dir ? 1 : 0));
	if (data->results_idle_id == 0)
		g_idle_add ((GSourceFunc)_recurse_async_idle_cb, data);

	g_mutex_unlock (data->results_lock);
}

static gpointer
_recurse_async_func (RBUriHandleRecursivelyAsyncData *data)
{
	GnomeVFSFileInfoOptions flags;
	GnomeVFSResult result;

	flags = GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
		GNOME_VFS_FILE_INFO_FOLLOW_LINKS |
		GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS;
	result = gnome_vfs_directory_visit (data->data.uri,
					    flags,
					    GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK,
					    (GnomeVFSDirectoryVisitFunc)rb_uri_handle_recursively_cb,
					    &data->data);
	
	g_idle_add ((GSourceFunc)_recurse_async_data_free, data);
	return NULL;
}

void
rb_uri_handle_recursively_async (const char *text_uri,
			         RBUriRecurseFunc func,
				 gboolean *cancelflag,
			         gpointer user_data,
				 GDestroyNotify data_destroy)
{
	RBUriHandleRecursivelyAsyncData *data = g_new0 (RBUriHandleRecursivelyAsyncData, 1);
	
	data->data.uri = g_strdup (text_uri);
	data->func = (RBUriRecurseFunc)_recurse_async_cb;
	data->data.user_data = user_data;
	data->data.cancel_flag = cancelflag;
	data->data.data_destroy = data_destroy;

	data->results_lock = g_mutex_new ();
	data->data.func = func;
	data->user_data = data;

	g_thread_create ((GThreadFunc)_recurse_async_func, data, FALSE, NULL);
}


GnomeVFSResult
rb_uri_mkstemp (const char *prefix, char **uri_ret, GnomeVFSHandle **ret)
{
	GnomeVFSHandle *handle = NULL;
	char *uri = NULL;
	GnomeVFSResult result = GNOME_VFS_ERROR_FILE_EXISTS;
	
	
	do {
		g_free (uri);
		uri = g_strdup_printf ("%s%06X", prefix, g_random_int_range (0, 0xFFFFFF));
		result = gnome_vfs_create (&handle, uri, GNOME_VFS_OPEN_WRITE | GNOME_VFS_OPEN_RANDOM, TRUE,  0644);
	} while (result == GNOME_VFS_ERROR_FILE_EXISTS);

	if (result == GNOME_VFS_OK) {
		*uri_ret = uri;
		*ret = handle;
	} else {
		g_free (uri);
	}
	return result;
}


char *
rb_canonicalise_uri (const char *uri)
{
	char *result = NULL;

	if (uri[0] == '/') {
		/* local path */
		char *tmp;
		result = gnome_vfs_make_path_name_canonical (uri);
		tmp = gnome_vfs_escape_path_string (result);
		g_free (result);
		tmp = escape_extra_gnome_vfs_chars (tmp);
		result = g_strconcat ("file://", tmp, NULL);
		g_free (tmp);
	} else  if (g_str_has_prefix (uri, "file://")) {
	    	/* local file, rhythmdb wants this path escaped */
		char *tmp1, *tmp2;
		tmp1  = gnome_vfs_unescape_string (uri + 7, NULL);  /* ignore "file://" */
		tmp2 = gnome_vfs_escape_path_string (tmp1);
		g_free (tmp1);
		tmp2 = escape_extra_gnome_vfs_chars (tmp2);
		result = g_strconcat ("file://", tmp2, NULL); /* re-add scheme */
		g_free (tmp2);
	} else {
		GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);

		if (vfsuri != NULL) {
			/* non-local uri, leave as-is */
			gnome_vfs_uri_unref (vfsuri);
			result = g_strdup (uri);
		} else {
			/* this may just mean that gnome-vfs doesn't recognise the
			 * uri scheme, so return it as is */
			rb_debug ("Error processing probable URI %s", uri);
			result = g_strdup (uri);
		}
	}

	return result;
}

char*
rb_uri_append_path (const char *uri, const char *path)
{
	GnomeVFSURI *vfs_uri, *full_uri;
	char *result;

	vfs_uri = gnome_vfs_uri_new (uri);
	full_uri = gnome_vfs_uri_append_path (vfs_uri, path);
	gnome_vfs_uri_unref (vfs_uri);
	result = gnome_vfs_uri_to_string (full_uri, GNOME_VFS_URI_HIDE_NONE);
	gnome_vfs_uri_unref (full_uri);

	return result;
}

char*
rb_uri_append_uri (const char *uri, const char *fragment)
{
	GnomeVFSURI *vfs_uri, *full_uri;
	char *result;

	vfs_uri = gnome_vfs_uri_new (uri);
	full_uri = gnome_vfs_uri_append_string (vfs_uri, fragment);
	gnome_vfs_uri_unref (vfs_uri);
	result = gnome_vfs_uri_to_string (full_uri, GNOME_VFS_URI_HIDE_NONE);
	gnome_vfs_uri_unref (full_uri);

	return result;
}

char *
rb_uri_get_dir_name (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	char *dirname;

	vfs_uri = gnome_vfs_uri_new (uri);
	dirname = gnome_vfs_uri_extract_dirname (vfs_uri);
	gnome_vfs_uri_unref (vfs_uri);

	return dirname;
}

char *
rb_uri_get_short_path_name (const char *uri)
{
	const char *start;
	const char *end;

	if (uri == NULL)
		return NULL;

	/* skip query string */
	end = g_utf8_strchr (uri, -1, '?');

	start = g_utf8_strrchr (uri, end ? (end - uri) : -1, GNOME_VFS_URI_PATH_CHR);
	if (start == NULL) {
		/* no separator, just a single file name */
	} else if ((start + 1 == end) || *(start + 1) == '\0') {
		/* last character is the separator, so find the previous one */
		end = start;
		start = g_utf8_strrchr (uri, (end - uri)-1, GNOME_VFS_URI_PATH_CHR);

		if (start != NULL)
			start++;
	} else {
		start++;
	}

	if (start == NULL)
		start = uri;

	if (end == NULL) {
		return g_strdup (start);
	} else {
		return g_strndup (start, (end - start));
	}
}

