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

/**
 * SECTION:rb-file-helpers
 * @short_description: An assortment of file and URI helper functions
 *
 * This is a variety of functions for dealing with files and URIs, including
 * locating installed files, finding user cache and config directories,
 * and dealing with file naming restrictions for various filesystems.
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <config.h>
#include <unistd.h>
#include <stdlib.h>

#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"

static GHashTable *files = NULL;

static char *dot_dir = NULL;
static char *user_data_dir = NULL;
static char *user_cache_dir = NULL;

static char *uninstalled_paths[] = {
	SHARE_UNINSTALLED_DIR "/",
	SHARE_UNINSTALLED_DIR "/ui/",
	SHARE_UNINSTALLED_DIR "/art/",
	SHARE_UNINSTALLED_BUILDDIR "/",
	SHARE_UNINSTALLED_BUILDDIR "/ui/",
	SHARE_UNINSTALLED_BUILDDIR "/art/",
	SHARE_DIR "/",
	SHARE_DIR "/art/",
	NULL
};

static char *installed_paths[] = {
	SHARE_DIR "/",
	SHARE_DIR "/art/",
	NULL
};

static char **search_paths;


/**
 * rb_file:
 * @filename: name of file to search for
 *
 * Searches for an installed file, returning the full path name
 * if found, NULL otherwise.
 *
 * Return value: Full file name, if found.  Must not be freed.
 */
const char *
rb_file (const char *filename)
{
	char *ret;
	int i;

	g_assert (files != NULL);

	ret = g_hash_table_lookup (files, filename);
	if (ret != NULL)
		return ret;

	for (i = 0; search_paths[i] != NULL; i++) {
		ret = g_strconcat (search_paths[i], filename, NULL);
		if (g_file_test (ret, G_FILE_TEST_EXISTS) == TRUE) {
			g_hash_table_insert (files, g_strdup (filename), ret);
			return (const char *) ret;
		}
		g_free (ret);
	}

	return NULL;
}

/**
 * rb_dot_dir:
 *
 * Deprecated.
 *
 * Return value: user's ~/.gnome2/rhythmbox/ dir
 */
const char *
rb_dot_dir (void)
{
	if (dot_dir == NULL) {
		dot_dir = g_build_filename (g_get_home_dir (),
					    ".gnome2",
					    "rhythmbox",
					    NULL);

		/* since we don't write any new files in this directory, we shouldn't
		 * create it if it doesn't already exist.
		 */
	}
	
	return dot_dir;
}

/**
 * rb_user_data_dir:
 *
 * This will create the rhythmbox user data directory, using the XDG Base
 * Directory specification.  If none of the XDG environment variables are
 * set, this will be ~/.local/share/rhythmbox.
 *
 * Returns: string holding the path to the rhythmbox user data directory, or
 * NULL if the directory does not exist and cannot be created.
 */
const char *
rb_user_data_dir (void)
{
	if (user_data_dir == NULL) {
		user_data_dir = g_build_filename (g_get_user_data_dir (),
						  "rhythmbox",
						  NULL);
		if (g_mkdir_with_parents (user_data_dir, 0700) == -1)
			rb_debug ("unable to create Rhythmbox's user data dir, %s", user_data_dir);
	}
	
	return user_data_dir;
}

/**
 * rb_user_cache_dir:
 *
 * This will create the rhythmbox user cache directory, using the XDG
 * Base Directory specification.  If none of the XDG environment
 * variables are set, this will be ~/.cache/rhythmbox.
 *
 * Returns: string holding the path to the rhythmbox user cache directory, or
 * NULL if the directory does not exist and could not be created.
 */
const char *
rb_user_cache_dir (void)
{
	if (user_cache_dir == NULL) {
		user_cache_dir = g_build_filename (g_get_user_cache_dir (),
						   "rhythmbox",
						   NULL);
		if (g_mkdir_with_parents (user_cache_dir, 0700) == -1)
			rb_debug ("unable to create Rhythmbox's user cache dir, %s", user_cache_dir);
	}

	return user_cache_dir;
}


/**
 * rb_music_dir:
 *
 * Returns the default directory for the user's music library.
 * This will usually be the 'Music' directory under the home directory.
 *
 * Return value: user's music directory.  must not be freed.
 */
const char *
rb_music_dir (void)
{
	const char *dir;
	dir = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
	if (dir == NULL) {
		dir = getenv ("HOME");
		if (dir == NULL) {
			dir = "/tmp";
		}
	}
	rb_debug ("user music dir: %s", dir);
	return dir;
}

static char *
rb_find_user_file (const char *dir,
		   const char *name,
		   GError **error)
{
	GError *temp_err = NULL;
	char *srcpath;
	char *destpath;
	GFile *src;
	GFile *dest;
	char *use_path;

	/* if the file exists in the target dir, return the path */
	destpath = g_build_filename (dir, name, NULL);
	dest = g_file_new_for_path (destpath);
	if (g_file_query_exists (dest, NULL) == TRUE) {
		g_object_unref (dest);
		rb_debug ("found user dir path for '%s': %s", name, destpath);
		return destpath;
	}

	/* doesn't exist in the target dir, so try to move it from the .gnome2 dir */
	srcpath = g_build_filename (rb_dot_dir (), name, NULL);
	src = g_file_new_for_path (srcpath);

	if (g_file_query_exists (src, NULL)) {
		g_file_move (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &temp_err);
		if (temp_err != NULL) {
			rb_debug ("failed to move user file '%s' from .gnome2 dir, returning .gnome2 path %s: %s",
				  name, srcpath, temp_err->message);

			use_path = g_file_get_path (src);
			g_set_error (error,
				     temp_err->domain,
				     temp_err->code,
				     _("Unable to move %s to %s: %s"),
				     srcpath, destpath, temp_err->message);
			g_error_free (temp_err);
		} else {
			rb_debug ("moved user file '%s' from .gnome2 dir, returning user dir path %s",
				  name, destpath);
			use_path = g_file_get_path (dest);
		}
	} else {
		rb_debug ("no existing file for '%s', returning user dir path %s", name, destpath);
		use_path = g_file_get_path (dest);
	}

	g_free (srcpath);
	g_free (destpath);

	g_object_unref (src);
	g_object_unref (dest);

	return use_path;
}

/**
 * rb_find_user_data_file:
 * @name: name of file to find
 * @error: returns error information
 *
 * Determines the full path to use for user-specific files, such as rhythmdb.xml.
 * This first checks in the user data directory (see @rb_user_data_dir).
 * If the file does not exist in the user data directory, it then checks the
 * old .gnome2 directory, moving the file to the user data directory if found there.
 * If an error occurs while moving the file, this will be reported through @error 
 * and the .gnome2 path will be returned.
 *
 * Returns: allocated string containing the location of the file to use, even if
 *  an error occurred.
 */
char *
rb_find_user_data_file (const char *name,
			GError **error)
{
	return rb_find_user_file (rb_user_data_dir (), name, error);
}

/**
 * rb_find_user_cache_file:
 * @name: name of file to find
 * @error: returns error information
 *
 * Determines the full path to use for user-specific cached files.
 * This first checks in the user cache directory (see @rb_user_cache_dir).
 * If the file does not exist in the user cache directory, it then checks the
 * old .gnome2 directory, moving the file to the user cache directory if found there.
 * If an error occurs while moving the file, this will be reported through @error 
 * and the .gnome2 path will be returned.
 *
 * Returns: allocated string containing the location of the file to use, even if
 *  an error occurred.
 */
char *
rb_find_user_cache_file (const char *name,
			 GError **error)
{
	return rb_find_user_file (rb_user_cache_dir (), name, error);
}

/**
 * rb_file_helpers_init:
 * @uninstalled: if %TRUE, search in source and build directories
 * as well as installed locations
 *
 * Sets up file search paths for @rb_file.  Must be called on startup.
 */
void
rb_file_helpers_init (gboolean uninstalled)
{
	if (uninstalled)
		search_paths = uninstalled_paths;
	else
		search_paths = installed_paths;

	files = g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       (GDestroyNotify) g_free,
				       (GDestroyNotify) g_free);
}

/**
 * rb_file_helpers_shutdown:
 *
 * Frees various data allocated by file helper functions.
 * Should be called on shutdown.
 */
void
rb_file_helpers_shutdown (void)
{
	g_hash_table_destroy (files);
	g_free (dot_dir);
	g_free (user_data_dir);
	g_free (user_cache_dir);
}

#define MAX_LINK_LEVEL 5

/* not sure this is really useful */

/**
 * rb_uri_resolve_symlink:
 * @uri: the URI to process
 * @error: returns error information
 *
 * Attempts to resolve symlinks in @uri and return a canonical URI for the file
 * it identifies.
 *
 * Return value: resolved URI, or NULL on error
 */
char *
rb_uri_resolve_symlink (const char *uri, GError **error)
{
	GFile *file = NULL;
	GFileInfo *file_info = NULL;
	int link_count = 0;
	char *result = NULL;
	const char *attr = G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET;
	GError *l_error = NULL;
	
	file = g_file_new_for_uri (uri);

	while (link_count < MAX_LINK_LEVEL) {
		GFile *parent;
		GFile *new_file;
		const char *target;

		/* look for a symlink target */
		file_info = g_file_query_info (file,
					       attr,
					       G_FILE_QUERY_INFO_NONE,
					       NULL, &l_error);
		if (l_error != NULL) {
			/* argh */
			result = g_file_get_uri (file);
			rb_debug ("error querying %s: %s", result, l_error->message);
			g_free (result);
			result = NULL;
			break;
		} else if (g_file_info_has_attribute (file_info, attr) == FALSE) {
			/* no symlink, so return the path */
			result = g_file_get_uri (file);
			if (link_count > 0) {
				rb_debug ("resolved symlinks: %s -> %s", uri, result);
			}
			break;
		}

		/* resolve it and try again */
		new_file = NULL;
		parent = g_file_get_parent (file);
		if (parent == NULL) {
			/* dang */
			break;
		}

		target = g_file_info_get_attribute_byte_string (file_info, attr);
		new_file = g_file_resolve_relative_path (parent, target);
		g_object_unref (parent);

		g_object_unref (file_info);
		file_info = NULL;

		g_object_unref (file);
		file = new_file;

		if (file == NULL) {
			/* dang */
			break;
		}

		link_count++;
	}

	if (file != NULL) {
		g_object_unref (file);
	}
	if (file_info != NULL) {
		g_object_unref (file_info);
	}
	if (result == NULL && error == NULL) {
		rb_debug ("too many symlinks while resolving %s", uri);
		l_error = g_error_new (G_IO_ERROR,
				       G_IO_ERROR_TOO_MANY_LINKS,
				       _("Too many symlinks"));
	}
	if (l_error != NULL) {
		g_propagate_error (error, l_error);
	}

	return result;
}

/**
 * rb_uri_is_directory:
 * @uri: the URI to check
 *
 * Checks if @uri identifies a directory.
 *
 * Return value: %TRUE if @uri is a directory
 */
gboolean
rb_uri_is_directory (const char *uri)
{
	GFile *f;
	GFileInfo *fi;
	GFileType ftype;

	f = g_file_new_for_uri (uri);
	fi = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL);
	g_object_unref (f);
	if (fi == NULL) {
		/* ? */
		return FALSE;
	}

	ftype = g_file_info_get_attribute_uint32 (fi, G_FILE_ATTRIBUTE_STANDARD_TYPE);
	g_object_unref (fi);
	return (ftype == G_FILE_TYPE_DIRECTORY);
}

/**
 * rb_uri_exists:
 * @uri: a URI to check
 *
 * Checks if a URI identifies a resource that exists
 *
 * Return value: %TRUE if @uri exists
 */
gboolean
rb_uri_exists (const char *uri)
{
	GFile *f;
	gboolean exists;

	f = g_file_new_for_uri (uri);
	exists = g_file_query_exists (f, NULL);
	g_object_unref (f);
	return exists;
}

static gboolean
get_uri_perm (const char *uri, const char *perm_attribute)
{
	GFile *f;
	GFileInfo *info;
	GError *error = NULL;
	gboolean result;

	f = g_file_new_for_uri (uri);
	info = g_file_query_info (f, perm_attribute, 0, NULL, &error);
	if (error != NULL) {
		result = FALSE;
		g_error_free (error);
	} else {
		result = g_file_info_get_attribute_boolean (info, perm_attribute);
	}

	if (info != NULL) {
		g_object_unref (info);
	}
	g_object_unref (f);
	return result;
}

/**
 * rb_uri_is_readable:
 * @uri: a URI to check
 *
 * Checks if the user can read the resource identified by @uri
 *
 * Return value: %TRUE if @uri is readable
 */
gboolean
rb_uri_is_readable (const char *uri)
{
	return get_uri_perm (uri, G_FILE_ATTRIBUTE_ACCESS_CAN_READ);
}

/**
 * rb_uri_is_writable:
 * @uri: a URI to check
 *
 * Checks if the user can write to the resource identified by @uri
 *
 * Return value: %TRUE if @uri is writable
 */
gboolean
rb_uri_is_writable (const char *uri)
{
	return get_uri_perm (uri, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
}

/**
 * rb_uri_is_local:
 * @uri: a URI to check
 *
 * Checks if @uri identifies a local resource.  Currently this just
 * checks that it uses the 'file' URI scheme.
 *
 * Return value: %TRUE if @uri is local
 */
gboolean
rb_uri_is_local (const char *uri)
{
	return g_str_has_prefix (uri, "file://");
}

/**
 * rb_uri_is_hidden:
 * @uri: a URI to check
 *
 * Checks if @uri is hidden, according to the Unix filename convention.
 * If the filename component of @uri begins with a dot, the file is considered
 * hidden.
 *
 * Return value: %TRUE if @uri is hidden
 */
gboolean
rb_uri_is_hidden (const char *uri)
{
	return g_utf8_strrchr (uri, -1, '/')[1] == '.';
}

/**
 * rb_uri_could_be_podcast:
 * @uri: a URI to check
 * @is_opml: returns whether the URI identifies an OPML document
 *
 * Checks if @uri identifies a resource that is probably a podcast
 * (RSS or Atom feed).  This does not perform any IO, it just guesses
 * based on the URI itself.
 *
 * Return value: %TRUE if @uri may be a podcast
 */
gboolean
rb_uri_could_be_podcast (const char *uri, gboolean *is_opml)
{
	const char *query_string;

	if (is_opml != NULL)
		*is_opml = FALSE;

	/* feed:// URIs are always podcasts */
	if (g_str_has_prefix (uri, "feed:")) {
		rb_debug ("'%s' must be a podcast", uri);
		return TRUE;
	}

	/* Check the scheme is a possible one first */
	if (g_str_has_prefix (uri, "http") == FALSE &&
	    g_str_has_prefix (uri, "itpc:") == FALSE &&
	    g_str_has_prefix (uri, "itms:") == FALSE) {
	    	rb_debug ("'%s' can't be a Podcast or OPML file, not the right scheme", uri);
	    	return FALSE;
	}

	/* Now, check whether the iTunes Music Store link
	 * is a podcast */
	if (g_str_has_prefix (uri, "itms:") != FALSE
	    && strstr (uri, "phobos.apple.com") != NULL
	    && strstr (uri, "viewPodcast") != NULL)
		return TRUE;

	query_string = strchr (uri, '?');
	if (query_string == NULL) {
		query_string = uri + strlen (uri);
	}

	/* FIXME hacks */
	if (strstr (uri, "rss") != NULL ||
	    strstr (uri, "atom") != NULL ||
	    strstr (uri, "feed") != NULL) {
	    	rb_debug ("'%s' should be Podcast file, HACK", uri);
	    	return TRUE;
	} else if (strstr (uri, "opml") != NULL) {
		rb_debug ("'%s' should be an OPML file, HACK", uri);
		if (is_opml != NULL)
			*is_opml = TRUE;
		return TRUE;
	}

	if (strncmp (query_string - 4, ".rss", 4) == 0 ||
	    strncmp (query_string - 4, ".xml", 4) == 0 ||
	    strncmp (query_string - 5, ".atom", 5) == 0 ||
	    strncmp (uri, "itpc", 4) == 0 ||
	    (strstr (uri, "phobos.apple.com/") != NULL && strstr (uri, "viewPodcast") != NULL) ||
	    strstr (uri, "itunes.com/podcast") != NULL) {
	    	rb_debug ("'%s' should be Podcast file", uri);
	    	return TRUE;
	} else if (strncmp (query_string - 5, ".opml", 5) == 0) {
		rb_debug ("'%s' should be an OPML file", uri);
		if (is_opml != NULL)
			*is_opml = TRUE;
		return TRUE;
	}

	return FALSE;
}

/**
 * rb_uri_make_hidden:
 * @uri: a URI to construct a hidden version of
 *
 * Constructs a URI that is similar to @uri but which identifies
 * a hidden file.  This can be used for temporary files that should not
 * be visible to the user while they are in use.
 *
 * Return value: hidden URI, must be freed by the caller.
 */
char *
rb_uri_make_hidden (const char *uri)
{
	GFile *file;
	GFile *parent;
	char *shortname;
	char *dotted;
	char *ret = NULL;

	if (rb_uri_is_hidden (uri))
		return g_strdup (uri);

	file = g_file_new_for_uri (uri);

	shortname = g_file_get_basename (file);
	if (shortname == NULL) {
		g_object_unref (file);
		return NULL;
	}

	parent = g_file_get_parent (file);
	if (parent == NULL) {
		g_object_unref (file);
		g_free (shortname);
		return NULL;
	}
	g_object_unref (file);

	dotted = g_strdup_printf (".%s", shortname);
	g_free (shortname);

	file = g_file_get_child (parent, dotted);
	g_object_unref (parent);
	g_free (dotted);

	if (file != NULL) {
		ret = g_file_get_uri (file);
		g_object_unref (file);
	}
	return ret;
}

typedef struct {
	char *uri;
	GCancellable *cancel;
	RBUriRecurseFunc func;
	gpointer user_data;
	GDestroyNotify data_destroy;

	GMutex *results_lock;
	guint results_idle_id;
	GList *file_results;
	GList *dir_results;
} RBUriHandleRecursivelyAsyncData;

static gboolean
_should_process (GFileInfo *info)
{
	/* check that the file is non-hidden and readable */
	if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
		if (g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ) == FALSE) {
			return FALSE;
		}
	}
	if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN)) {
		if (g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN)) {
			return FALSE;
		}
	}
	return TRUE;
}

static void
_uri_handle_recurse (GFile *dir,
		     GCancellable *cancel,
		     GHashTable *handled,
		     RBUriRecurseFunc func,
		     gpointer user_data)
{
	GFileEnumerator *files;
	GFileInfo *info;
	GError *error = NULL;
	GFileType file_type;
	const char *file_id;
	gboolean file_handled;
	const char *attributes = 
		G_FILE_ATTRIBUTE_STANDARD_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_TYPE ","
		G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
		G_FILE_ATTRIBUTE_ID_FILE ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_READ;

	files = g_file_enumerate_children (dir, attributes, G_FILE_QUERY_INFO_NONE, cancel, &error);
	if (error != NULL) {
		char *where;

		/* handle the case where we're given a single file to process */
		if (error->code == G_IO_ERROR_NOT_DIRECTORY) {
			g_clear_error (&error);
			info = g_file_query_info (dir, attributes, G_FILE_QUERY_INFO_NONE, cancel, &error);
			if (error == NULL) {
				if (_should_process (info)) {
					(func) (dir, FALSE, user_data);
				}
				g_object_unref (info);
				return;
			}
		}

		where = g_file_get_uri (dir);
		rb_debug ("error enumerating %s: %s", where, error->message);
		g_free (where);
		g_error_free (error);
		return;
	}

	while (1) {
		GFile *child;
		gboolean is_dir;
		gboolean ret;

		ret = TRUE;
		info = g_file_enumerator_next_file (files, cancel, &error);
		if (error != NULL) {
			rb_debug ("error enumerating files: %s", error->message);
			break;
		} else if (info == NULL) {
			break;
		}

		if (_should_process (info) == FALSE) {
			g_object_unref (info);
			continue;
		}

		/* already handled? */
		file_id = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILE);
		if (file_id == NULL) {
			/* have to hope for the best, I guess */
			file_handled = FALSE;
		} else if (g_hash_table_lookup (handled, file_id) != NULL) {
			file_handled = TRUE;
		} else {
			file_handled = FALSE;
			g_hash_table_insert (handled, g_strdup (file_id), GINT_TO_POINTER (1));
		}

		/* type? */
		file_type = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_STANDARD_TYPE);
		switch (file_type) {
		case G_FILE_TYPE_DIRECTORY:
		case G_FILE_TYPE_MOUNTABLE:
			is_dir = TRUE;
			break;
		
		default:
			is_dir = FALSE;
			break;
		}

		if (file_handled == FALSE) {
			child = g_file_get_child (dir, g_file_info_get_name (info));
			ret = (func) (child, is_dir, user_data);

			if (is_dir) {
				_uri_handle_recurse (child, cancel, handled, func, user_data);
			}
			g_object_unref (child);
		}
	
		g_object_unref (info);

		if (ret == FALSE)
			break;
	}

	g_object_unref (files);
}

/**
 * rb_uri_handle_recursively:
 * @uri: URI to visit
 * @cancel: an optional #GCancellable to allow cancellation
 * @func: Callback function
 * @user_data: Data for callback function
 *
 * Calls @func for each file found under the directory identified by @uri.
 * If @uri identifies a file, calls @func for that instead.
 */
void
rb_uri_handle_recursively (const char *uri,
			   GCancellable *cancel,
			   RBUriRecurseFunc func,
			   gpointer user_data)
{
	GFile *file;
	GHashTable *handled;

	file = g_file_new_for_uri (uri);
	handled = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	_uri_handle_recurse (file, cancel, handled, func, user_data);

	g_hash_table_destroy (handled);
	g_object_unref (file);
}


/* runs in main thread */
static gboolean
_recurse_async_idle_cb (RBUriHandleRecursivelyAsyncData *data)
{
	GList *ul, *dl;

	g_mutex_lock (data->results_lock);

	for (ul = data->file_results, dl = data->dir_results;
	     ul != NULL;
	     ul = g_list_next (ul), dl = g_list_next (dl)) {
		g_assert (dl != NULL);

		data->func (G_FILE (ul->data), (GPOINTER_TO_INT (dl->data) == 1), data->user_data);
		g_object_unref (ul->data);
	}
	g_assert (dl == NULL);

	g_list_free (data->file_results);
	data->file_results = NULL;
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
	GList *i;

	if (data->results_idle_id) {
		g_source_remove (data->results_idle_id);
		_recurse_async_idle_cb (data); /* process last results */
	}

	for (i = data->file_results; i != NULL; i = i->next) {
		GFile *file = G_FILE (i->data);
		g_object_unref (file);
	}

	g_list_free (data->file_results);
	data->file_results = NULL;
	g_list_free (data->dir_results);
	data->dir_results = NULL;

	if (data->data_destroy != NULL) {
		(data->data_destroy) (data->user_data);
	}
	if (data->cancel != NULL) {
		g_object_unref (data->cancel);
	}

	g_free (data->uri);
	g_mutex_free (data->results_lock);
	return FALSE;
}

/* runs in worker thread */
static gboolean
_recurse_async_cb (GFile *file, gboolean dir, RBUriHandleRecursivelyAsyncData *data)
{
	g_mutex_lock (data->results_lock);

	data->file_results = g_list_prepend (data->file_results, g_object_ref (file));
	data->dir_results = g_list_prepend (data->dir_results, GINT_TO_POINTER (dir ? 1 : 0));
	if (data->results_idle_id == 0) {
		g_idle_add ((GSourceFunc)_recurse_async_idle_cb, data);
	}

	g_mutex_unlock (data->results_lock);
	return TRUE;
}

static gpointer
_recurse_async_func (RBUriHandleRecursivelyAsyncData *data)
{
	rb_uri_handle_recursively (data->uri,
				   data->cancel,
				   (RBUriRecurseFunc) _recurse_async_cb,
				   data);

	g_idle_add ((GSourceFunc)_recurse_async_data_free, data);
	return NULL;
}

/**
 * rb_uri_handle_recursively_async:
 * @uri: the URI to visit
 * @cancel: a #GCancellable to allow cancellation
 * @func: callback function
 * @user_data: data to pass to callback
 * @data_destroy: function to call to free @user_data
 *
 * Calls @func for each file found under the directory identified
 * by @uri, or if @uri identifies a file, calls it once
 * with that.
 *
 * Directory recursion happens on a separate thread, but the callbacks
 * are called on the main thread.
 *
 * If non-NULL, @destroy_data will be called once all files have been
 * processed, or when the operation is cancelled.
 */
void
rb_uri_handle_recursively_async (const char *uri,
				 GCancellable *cancel,
			         RBUriRecurseFunc func,
			         gpointer user_data,
				 GDestroyNotify data_destroy)
{
	RBUriHandleRecursivelyAsyncData *data = g_new0 (RBUriHandleRecursivelyAsyncData, 1);
	
	data->uri = g_strdup (uri);
	data->user_data = user_data;
	if (cancel != NULL) {
		data->cancel = g_object_ref (cancel);
	}
	data->data_destroy = data_destroy;

	data->results_lock = g_mutex_new ();
	data->func = func;
	data->user_data = user_data;

	g_thread_create ((GThreadFunc)_recurse_async_func, data, FALSE, NULL);
}

/**
 * rb_uri_mkstemp:
 * @prefix: URI prefix
 * @uri_ret: returns the temporary file URI
 * @stream: returns a @GOutputStream for the temporary file
 * @error: returns error information
 *
 * Creates a temporary file whose URI begins with @prefix, returning
 * the file URI and an output stream for writing to it.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_uri_mkstemp (const char *prefix, char **uri_ret, GOutputStream **stream, GError **error)
{
	GFile *file;
	char *uri = NULL;
	GFileOutputStream *fstream;
	GError *e = NULL;

	do {
		g_free (uri);
		uri = g_strdup_printf ("%s%06X", prefix, g_random_int_range (0, 0xFFFFFF));

		file = g_file_new_for_uri (uri);
		fstream = g_file_create (file, G_FILE_CREATE_PRIVATE, NULL, &e);
		if (e != NULL) {
			if (g_error_matches (e, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_error_free (e);
				e = NULL;
			}
		}
	} while (e == NULL && fstream == NULL);

	if (fstream != NULL) {
		*uri_ret = uri;
		*stream = G_OUTPUT_STREAM (fstream);
		return TRUE;
	} else {
		g_free (uri);
		return FALSE;
	}
}

/**
 * rb_canonicalise_uri:
 * @uri: URI to canonicalise
 *
 * Converts @uri to canonical URI form, ensuring it doesn't contain
 * any redundant directory fragments or unnecessarily escaped characters.
 * All URIs passed to #RhythmDB functions should be canonicalised.
 *
 * Return value: canonical URI, must be freed by caller
 */
char *
rb_canonicalise_uri (const char *uri)
{
	GFile *file;
	char *result = NULL;

	g_return_val_if_fail (uri != NULL, NULL);

	/* gio does more or less what we want, I think */
	file = g_file_new_for_commandline_arg (uri);
	result = g_file_get_uri (file);
	g_object_unref (file);

	return result;
}

/**
 * rb_uri_append_path:
 * @uri: the URI to append to
 * @path: the path fragment to append
 *
 * Creates a new URI consisting of @path appended to @uri.
 *
 * Return value: new URI, must be freed by caller
 */
char*
rb_uri_append_path (const char *uri, const char *path)
{
	GFile *file;
	GFile *relfile;
	char *result;

	/* all paths we get are relative, so skip
	 * leading slashes.
	 */
	while (path[0] == '/') {
		path++;
	}

	file = g_file_new_for_uri (uri);
	relfile = g_file_resolve_relative_path (file, path);
	result = g_file_get_uri (relfile);
	g_object_unref (relfile);
	g_object_unref (file);

	return result;
}

/**
 * rb_uri_append_uri:
 * @uri: the URI to append to
 * @fragment: the URI fragment to append
 *
 * Creates a new URI consisting of @fragment appended to @uri.
 * Generally isn't a good idea.
 *
 * Return value: new URI, must be freed by caller
 */
char*
rb_uri_append_uri (const char *uri, const char *fragment)
{
	char *path;
	char *rv;
	GFile *f = g_file_new_for_uri (fragment);

	path = g_file_get_path (f);
	if (path == NULL) {
		g_object_unref (f);
		return NULL;
	}

	rv = rb_uri_append_path (uri, path);
	g_free (path);
	g_object_unref (f);

	return rv;
}

/**
 * rb_uri_get_dir_name:
 * @uri: a URI
 *
 * Returns the directory component of @uri, that is, everything up
 * to the start of the filename.
 *
 * Return value: new URI for parent of @uri, must be freed by caller.
 */
char *
rb_uri_get_dir_name (const char *uri)
{
	GFile *file;
	GFile *parent;
	char *dirname;

	file = g_file_new_for_uri (uri);
	parent = g_file_get_parent (file);
	
	dirname = g_file_get_uri (parent);

	g_object_unref (parent);
	g_object_unref (file);
	return dirname;
}

/**
 * rb_uri_get_short_path_name:
 * @uri: a URI
 *
 * Returns the filename component of @uri, that is, everything after the
 * final slash and before the start of the query string or fragment.
 *
 * Return value: filename component of @uri, must be freed by caller
 */
char *
rb_uri_get_short_path_name (const char *uri)
{
	const char *start;
	const char *end;

	if (uri == NULL)
		return NULL;

	/* skip query string */
	end = g_utf8_strchr (uri, -1, '?');

	start = g_utf8_strrchr (uri, end ? (end - uri) : -1, '/');
	if (start == NULL) {
		/* no separator, just a single file name */
	} else if ((start + 1 == end) || *(start + 1) == '\0') {
		/* last character is the separator, so find the previous one */
		end = start;
		start = g_utf8_strrchr (uri, (end - uri)-1, '/');

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

/**
 * rb_check_dir_has_space:
 * @dir: a #GFile to check
 * @bytes_needed: number of bytes to check for
 *
 * Checks that the filesystem holding @file has at least @bytes_needed
 * bytes available.
 *
 * Return value: %TRUE if enough space is available.
 */
gboolean
rb_check_dir_has_space (GFile *dir,
			guint64 bytes_needed)
{
	GFile *extant;
	GFileInfo *fs_info;
	GError *error = NULL;
	guint64 free_bytes;

	extant = rb_file_find_extant_parent (dir);
	if (extant == NULL) {
		char *uri = g_file_get_uri (dir);
		g_warning ("Cannot get free space at %s: none of the directory structure exists", uri);
		g_free (uri);
		return FALSE;
	}

	fs_info = g_file_query_filesystem_info (extant,
						G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
						NULL,
						&error);
	g_object_unref (extant);

	if (error != NULL) {
		char *uri;
		uri = g_file_get_uri (dir);
		g_warning (_("Cannot get free space at %s: %s"), uri, error->message);
		g_free (uri);
		return FALSE;
	}

	free_bytes = g_file_info_get_attribute_uint64 (fs_info,
						       G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	g_object_unref (fs_info);
	if (bytes_needed >= free_bytes)
		return FALSE;

	return TRUE;
}

/**
 * rb_check_dir_has_space_uri:
 * @uri: a URI to check
 * @bytes_needed: number of bytes to check for
 *
 * Checks that the filesystem holding @uri has at least @bytes_needed
 * bytes available.
 *
 * Return value: %TRUE if enough space is available.
 */
gboolean
rb_check_dir_has_space_uri (const char *uri,
			    guint64 bytes_needed)
{
	GFile *file;
	gboolean result;

	file = g_file_new_for_uri (uri);
	result = rb_check_dir_has_space (file, bytes_needed);
	g_object_unref (file);

	return result;
}

/**
 * rb_uri_get_mount_point:
 * @uri: a URI
 *
 * Returns the mount point of the filesystem holding @uri.
 * If @uri is on a normal filesystem mount (such as /, /home,
 * /var, etc.) this will be NULL.
 *
 * Return value: filesystem mount point (must be freed by caller)
 *  or NULL.
 */
gchar *
rb_uri_get_mount_point (const char *uri)
{
	GFile *file;
	GMount *mount;
	char *mountpoint;
	GError *error = NULL;

	file = g_file_new_for_uri (uri);
	mount = g_file_find_enclosing_mount (file, NULL, &error);
	if (error != NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) == FALSE) {
			rb_debug ("finding mount for %s: %s", uri, error->message);
		}
		g_error_free (error);
		mountpoint = NULL;
	} else {
		GFile *root;
		root = g_mount_get_root (mount);
		mountpoint = g_file_get_uri (root);
		g_object_unref (root);
		g_object_unref (mount);
	}

	g_object_unref (file);
	return mountpoint;
}

static gboolean
check_file_is_directory (GFile *file, GError **error)
{
	GFileInfo *info;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, NULL, error);
	if (*error == NULL) {
		/* check it's a directory */
		GFileType filetype;
		gboolean ret = TRUE;

		filetype = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_STANDARD_TYPE);
		if (filetype != G_FILE_TYPE_DIRECTORY) {
			/* um.. */
			ret = FALSE;
		}

		g_object_unref (info);
		return ret;
	}

	if (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_clear_error (error);
	}
	return FALSE;
}


/**
 * rb_uri_create_parent_dirs:
 * @uri: a URI for which to create parent directories
 * @error: returns error information
 *
 * Ensures that all parent directories of @uri exist so that
 * @uri itself can be created directly.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_uri_create_parent_dirs (const char *uri, GError **error)
{
	GFile *file;
	GFile *parent;
	gboolean ret;

	/* ignore internal URI schemes */
	if (g_str_has_prefix (uri, "xrb")) {
		return TRUE;
	}

	file = g_file_new_for_uri (uri);
	parent = g_file_get_parent (file);
	g_object_unref (file);
	if (parent == NULL) {
		/* now what? */
		return TRUE;
	}

	ret = check_file_is_directory (parent, error);
	if (ret == FALSE && *error == NULL) {
		ret = g_file_make_directory_with_parents (parent, NULL, error);
	}

	g_object_unref (parent);
	return ret;
}

/**
 * rb_file_find_extant_parent:
 * @file: a #GFile to find an extant ancestor of
 *
 * Walks up the filesystem hierarchy to find a #GFile representing
 * the nearest extant ancestor of the specified file, which may be
 * the file itself if it exists.
 * 
 * Return value: #GFile for the nearest extant ancestor
 */
GFile *
rb_file_find_extant_parent (GFile *file)
{
	g_object_ref (file);
	while (g_file_query_exists (file, NULL) == FALSE) {
		GFile *parent;

		parent = g_file_get_parent (file);
		if (parent == NULL) {
			char *uri = g_file_get_uri (file);
			g_warning ("filesystem root %s apparently doesn't exist!", uri);
			g_free (uri);
			g_object_unref (file);
			return NULL;
		}

		g_object_unref (file);
		file = parent;
	}

	return file;
}

/**
 * rb_uri_get_filesystem_type:
 * @uri: URI to get filesystem type for
 * @mount_point: optionally returns the mount point for the filesystem as a URI
 *
 * Returns a string describing the type of the filesystem containing @uri.
 *
 * Return value: filesystem type string, must be freed by caller.
 */
char *
rb_uri_get_filesystem_type (const char *uri, char **mount_point)
{
	GFile *file;
	GFile *extant;
	GFileInfo *info;
	char *fstype = NULL;
	GError *error = NULL;

	if (mount_point != NULL) {
		*mount_point = NULL;
	}

	/* ignore our own internal URI schemes */
	if (g_str_has_prefix (uri, "xrb")) {
		return NULL;
	}

	/* if the file doesn't exist, walk up the directory structure
	 * until we find something that does.
	 */
	file = g_file_new_for_uri (uri);

	extant = rb_file_find_extant_parent (file);
	if (extant == NULL) {
		rb_debug ("unable to get filesystem type for %s: none of the directory structure exists", uri);
		g_object_unref (file);
		return NULL;
	}

	if (mount_point != NULL) {
		char *extant_uri;
		extant_uri = g_file_get_uri (extant);
		*mount_point = rb_uri_get_mount_point (extant_uri);
		g_free (extant_uri);
	}

	info = g_file_query_filesystem_info (extant, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE, NULL, &error);
	if (info != NULL) {
		fstype = g_file_info_get_attribute_as_string (info, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE);
		g_object_unref (info);
	} else {
		rb_debug ("error querying filesystem info: %s", error->message);
	}
	g_clear_error (&error);
	g_object_unref (file);
	g_object_unref (extant);
	return fstype;
}

/**
 * rb_sanitize_path_for_msdos_filesystem:
 * @path: a path to sanitize (modified in place)
 *
 * Modifies @path such that it represents a legal path for MS DOS
 * filesystems.
 */
void
rb_sanitize_path_for_msdos_filesystem (char *path)
{
	g_strdelimit (path, "\"", '\'');
	g_strdelimit (path, ":|<>*?\\", '_');
}

/**
 * rb_sanitize_uri_for_filesystem:
 * @uri: a URI to sanitize
 *
 * Removes characters from @uri that are not allowed by the filesystem
 * on which it would be stored.  At present, this only supports MS DOS
 * filesystems.
 *
 * Return value: sanitized copy of @uri, must be freed by caller.
 */
char *
rb_sanitize_uri_for_filesystem (const char *uri)
{
	char *mountpoint = NULL;
	char *filesystem;
	char *sane_uri = NULL;

	filesystem = rb_uri_get_filesystem_type (uri, &mountpoint);
	if (!filesystem)
		return g_strdup (uri);

	if (!strcmp (filesystem, "fat") ||
	    !strcmp (filesystem, "vfat") ||
	    !strcmp (filesystem, "msdos")) {
	    	char *hostname = NULL;
		GError *error = NULL;
		char *full_path;
		char *fat_path;

		full_path = g_filename_from_uri (uri, &hostname, &error);

		if (error) {
			g_error_free (error);
			g_free (filesystem);
			g_free (full_path);
			g_free (mountpoint);
			return g_strdup (uri);
		}

		/* if we got a mount point, don't sanitize it.  the mountpoint must be
		 * valid for the filesystem that contains it, but it may not be valid for
		 * the filesystem it contains.  for example, a vfat filesystem mounted
		 * at "/media/Pl1:".
		 */
		fat_path = full_path;
		if (mountpoint != NULL) {
			char *mount_path;
			mount_path = g_filename_from_uri (mountpoint, NULL, &error);
			if (error) {
				rb_debug ("can't convert mountpoint %s to a path: %s", mountpoint, error->message);
				g_error_free (error);
			} else if (g_str_has_prefix (full_path, mount_path)) {
				fat_path = full_path + strlen (mount_path);
			} else {
				rb_debug ("path %s doesn't begin with mount path %s somehow", full_path, mount_path);
			}

			g_free (mount_path);
		} else {
			rb_debug ("couldn't get mount point for %s", uri);
		}

		rb_debug ("sanitizing path %s", fat_path);
		rb_sanitize_path_for_msdos_filesystem (fat_path);

		/* create a new uri from this */
		sane_uri = g_filename_to_uri (full_path, hostname, &error);
		rb_debug ("sanitized URI: %s", sane_uri);

		g_free (hostname);
		g_free (full_path);

		if (error) {
			g_error_free (error);
			g_free (filesystem);
			g_free (mountpoint);
			return g_strdup (uri);
		}
	}

	/* add workarounds for other filesystems limitations here */

	g_free (filesystem);
	g_free (mountpoint);
	return sane_uri ? sane_uri : g_strdup (uri);
}

