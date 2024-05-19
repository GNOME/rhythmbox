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
 * SECTION:rbfilehelpers
 * @short_description: An assortment of file and URI helper functions
 *
 * This is a variety of functions for dealing with files and URIs, including
 * locating installed files, finding user cache and config directories,
 * and dealing with file naming restrictions for various filesystems.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <libpeas/peas.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"

typedef struct _RBUriHandleRecursivelyAsyncData RBUriHandleRecursivelyAsyncData;

static void _uri_handle_recursively_free (RBUriHandleRecursivelyAsyncData *data);
static void _uri_handle_recursively_next_dir (RBUriHandleRecursivelyAsyncData *data);
static void _uri_handle_recursively_next_files (RBUriHandleRecursivelyAsyncData *data);

static GHashTable *files = NULL;

static char *dot_dir = NULL;
static char *user_data_dir = NULL;
static char *user_cache_dir = NULL;

static char *installed_paths[] = {
	SHARE_DIR "/",
	NULL
};

static const char *recurse_attributes =
		G_FILE_ATTRIBUTE_STANDARD_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_TYPE ","
		G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
		G_FILE_ATTRIBUTE_ID_FILE ","
		G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
		G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
		G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET;

/**
 * rb_locale_dir:
 *
 * Returns the locale directory identified at build configuration time.
 *
 * Return value: locale dir
 */
const char *
rb_locale_dir (void)
{
	return GNOMELOCALEDIR;
}

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

	for (i = 0; installed_paths[i] != NULL; i++) {
		ret = g_strconcat (installed_paths[i], filename, NULL);
		if (g_file_test (ret, G_FILE_TEST_EXISTS) == TRUE) {
			g_hash_table_insert (files, g_strdup (filename), ret);
			return (const char *) ret;
		}
		g_free (ret);
	}

	return NULL;
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

/**
 * rb_find_user_data_file:
 * @name: name of file to find
 *
 * Determines the full path to use for user-specific files, such as rhythmdb.xml,
 * within the user data directory (see @rb_user_data_dir).
 *
 * Returns: allocated string containing the location of the file to use, even if
 *  an error occurred.
 */
char *
rb_find_user_data_file (const char *name)
{
	return g_build_filename (rb_user_data_dir (), name, NULL);
}

/**
 * rb_find_user_cache_file:
 * @name: name of file to find
 *
 * Determines the full path to use for user-specific cached files
 * within the user cache directory.
 *
 * Returns: allocated string containing the location of the file to use, even if
 *  an error occurred.
 */
char *
rb_find_user_cache_file (const char *name)
{
	return g_build_filename (rb_user_cache_dir (), name, NULL);
}

/**
 * rb_find_plugin_data_file:
 * @plugin: the plugin object
 * @name: name of the file to find
 *
 * Locates a file under the plugin's data directory.
 *
 * Returns: allocated string containing the location of the file
 */
char *
rb_find_plugin_data_file (GObject *object, const char *name)
{
	PeasPluginInfo *info;
	char *ret = NULL;
	const char *plugin_name = "<unknown>";

	g_object_get (object, "plugin-info", &info, NULL);
	if (info != NULL) {
		char *tmp;

		tmp = g_build_filename (peas_plugin_info_get_data_dir (info), name, NULL);
		if (g_file_test (tmp, G_FILE_TEST_EXISTS)) {
			ret = tmp;
		} else {
			g_free (tmp);
		}

		plugin_name = peas_plugin_info_get_name (info);
	}
	
	if (ret == NULL) {
		const char *f;
		f = rb_file (name);
		if (f != NULL) {
			ret = g_strdup (f);
		}
	}

	if (ret == NULL) {
		rb_debug ("didn't find file '%s' for plugin '%s'", name, plugin_name);
	} else {
		rb_debug ("found '%s' when searching for file '%s' for plugin '%s'",
			  ret, name, plugin_name);
	}

	/* ensure it's an absolute path */
	if (ret != NULL && ret[0] != '/') {
		char *pwd = g_get_current_dir ();
		char *path = g_strconcat (pwd, G_DIR_SEPARATOR_S, ret, NULL);
		g_free (ret);
		g_free (pwd);
		ret = path;
	}

	return ret;
}

/**
 * rb_find_plugin_resource:
 * @plugin: the plugin object
 * @name: name of the file to find
 *
 * Constructs a resource path for a plugin data file.
 *
 * Returns: allocated string containing the resource path
 */
char *
rb_find_plugin_resource (GObject *object, const char *name)
{
	PeasPluginInfo *info;
	const char *plugin_name;

	g_object_get (object, "plugin-info", &info, NULL);
	if (info == NULL)
		return NULL;

	plugin_name = peas_plugin_info_get_module_name (info);
	return g_strdup_printf ("/org/gnome/Rhythmbox/%s/%s", plugin_name, name);
}


/**
 * rb_file_helpers_init:
 *
 * Sets up file search paths for @rb_file.  Must be called on startup.
 */
void
rb_file_helpers_init (void)
{
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
	GFile *file;
	GFile *rfile;
	char *result = NULL;

	file = g_file_new_for_uri (uri);
	rfile = rb_file_resolve_symlink (file, error);
	g_object_unref (file);

	if (rfile != NULL) {
		result = g_file_get_uri (rfile);
		g_object_unref (rfile);
	}
	return result;
}

/**
 * rb_file_resolve_symlink:
 * @file: the file to process
 * @error: returns error information
 *
 * Attempts to resolve symlinks leading to @file and return a canonical location.
 *
 * Return value: (transfer full): a #GFile representing the canonical location, or NULL on error
 */
GFile *
rb_file_resolve_symlink (GFile *file, GError **error)
{
	GFileInfo *file_info = NULL;
	int link_count = 0;
	GFile *result = NULL;
	GFile *current;
	char *furi;
	char *ruri;
	const char *attr = G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET;
	GError *l_error = NULL;

	current = g_object_ref (file);
	while (link_count < MAX_LINK_LEVEL) {
		GFile *parent;
		GFile *new_file;
		const char *target;

		/* look for a symlink target */
		file_info = g_file_query_info (current,
					       attr,
					       G_FILE_QUERY_INFO_NONE,
					       NULL, &l_error);
		if (l_error != NULL) {
			/* argh */
			ruri = g_file_get_uri (current);
			rb_debug ("error querying %s: %s", ruri, l_error->message);
			g_free (ruri);
			result = NULL;
			break;
		} else if (g_file_info_has_attribute (file_info, attr) == FALSE) {
			/* no symlink, so return the path */
			result = g_object_ref (current);
			if (link_count > 0) {
				furi = g_file_get_uri (file);
				ruri = g_file_get_uri (result);
				rb_debug ("resolved symlinks: %s -> %s", furi, ruri);
				g_free (furi);
				g_free (ruri);
			}
			break;
		}

		/* resolve it and try again */
		new_file = NULL;
		parent = g_file_get_parent (current);
		if (parent == NULL) {
			/* dang */
			break;
		}

		target = g_file_info_get_attribute_byte_string (file_info, attr);
		new_file = g_file_resolve_relative_path (parent, target);
		g_object_unref (parent);

		g_object_unref (file_info);
		file_info = NULL;

		g_object_unref (current);
		current = new_file;

		if (current == NULL) {
			/* dang */
			break;
		}

		link_count++;
	}

	g_clear_object (&current);
	g_clear_object (&file_info);
	if (result == NULL && error == NULL) {
		furi = g_file_get_uri (file);
		rb_debug ("too many symlinks while resolving %s", furi);
		g_free (furi);
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
	    g_str_has_prefix (uri, "itms:") == FALSE &&
	    g_str_has_prefix (uri, "itmss:") == FALSE) {
	    	rb_debug ("'%s' can't be a Podcast or OPML file, not the right scheme", uri);
	    	return FALSE;
	}

	/* Now, check whether the iTunes Music Store link
	 * is a podcast */
	if (g_str_has_prefix (uri, "itms:") != FALSE
	    && strstr (uri, "phobos.apple.com") != NULL
	    && strstr (uri, "viewPodcast") != NULL)
		return TRUE;

	/* Check for new itmss stype iTunes Music Store link
	 * to a podcast */
	if (g_str_has_prefix (uri, "itmss:") != FALSE
	    && strstr (uri, "podcast") != NULL)
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


static gboolean
_uri_handle_file (GFile *dir, GFileInfo *fileinfo, GHashTable *handled, RBUriRecurseFunc func, gpointer user_data, GFile **descend)
{
	const char *file_id;
	gboolean is_dir;
	gboolean ret;
	GFileType file_type;
	GFile *child;

	*descend = NULL;
	if (_should_process (fileinfo) == FALSE) {
		rb_debug ("ignoring %s", g_file_info_get_name (fileinfo));
		return TRUE;
	}

	/* already handled? */
	file_id = g_file_info_get_attribute_string (fileinfo, G_FILE_ATTRIBUTE_ID_FILE);
	if (file_id == NULL) {
		/* have to hope for the best, I guess */
	} else if (g_hash_table_lookup (handled, file_id) != NULL) {
		return TRUE;
	} else {
		g_hash_table_insert (handled, g_strdup (file_id), GINT_TO_POINTER (1));
	}

	/* type? */
	file_type = g_file_info_get_attribute_uint32 (fileinfo, G_FILE_ATTRIBUTE_STANDARD_TYPE);
	switch (file_type) {
	case G_FILE_TYPE_DIRECTORY:
	case G_FILE_TYPE_MOUNTABLE:
		is_dir = TRUE;
		break;
	default:
		is_dir = FALSE;
		break;
	}

	child = g_file_get_child (dir, g_file_info_get_name (fileinfo));
	ret = (func) (child, fileinfo, user_data);
	if (is_dir && ret) {
		*descend = child;
	} else {
		g_object_unref (child);
	}
	return ret;
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
	GFile *descend;

	files = g_file_enumerate_children (dir, recurse_attributes, G_FILE_QUERY_INFO_NONE, cancel, &error);
	if (error != NULL) {
		char *where;

		/* handle the case where we're given a single file to process */
		if (error->code == G_IO_ERROR_NOT_DIRECTORY) {
			g_clear_error (&error);
			info = g_file_query_info (dir, recurse_attributes, G_FILE_QUERY_INFO_NONE, cancel, &error);
			if (error == NULL) {
				if (_should_process (info)) {
					(func) (dir, info, user_data);
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
		info = g_file_enumerator_next_file (files, cancel, &error);
		if (error != NULL) {
			rb_debug ("error enumerating files: %s", error->message);
			break;
		} else if (info == NULL) {
			break;
		}

		if (_uri_handle_file (dir, info, handled, func, user_data, &descend) == FALSE)
			break;

		if (descend) {
			_uri_handle_recurse (descend, cancel, handled, func, user_data);
			g_object_unref (descend);
		}
	}

	g_object_unref (files);
}

/**
 * rb_uri_handle_recursively:
 * @uri: URI to visit
 * @cancel: an optional #GCancellable to allow cancellation
 * @func: (scope call): Callback function
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

struct _RBUriHandleRecursivelyAsyncData {
	GCancellable *cancel;
	RBUriRecurseFunc func;
	gpointer user_data;
	GDestroyNotify data_destroy;

	GHashTable *handled;

	GQueue *dirs_left;
	GFile *current;
	GFileEnumerator *enumerator;
};


static void
_uri_handle_recursively_free (RBUriHandleRecursivelyAsyncData *data)
{
	if (data->data_destroy)
		data->data_destroy (data->user_data);
	g_clear_object (&data->current);
	g_clear_object (&data->enumerator);
	g_clear_object (&data->cancel);
	g_hash_table_destroy (data->handled);
	g_queue_free_full (data->dirs_left, g_object_unref);
	g_free (data);
}

static void
_uri_handle_recursively_process_files (GObject *src, GAsyncResult *result, gpointer ptr)
{
	GList *files;
	GList *l;
	GFile *descend;
	GError *error = NULL;
	RBUriHandleRecursivelyAsyncData *data = ptr;

	files = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (src), result, &error);
	if (error != NULL) {
		rb_debug ("error enumerating files: %s", error->message);
		_uri_handle_recursively_next_dir (data);
		g_clear_error (&error);
		return;
	}

	if (files == NULL) {
		_uri_handle_recursively_next_dir (data);
		return;
	}

	rb_debug ("got %d file(s)", g_list_length (files));
	for (l = files; l != NULL; l = l->next) {
		descend = NULL;
		if (_uri_handle_file (data->current, l->data, data->handled, data->func, data->user_data, &descend) == FALSE) {
			rb_debug ("callback returned false");
			g_cancellable_cancel (data->cancel);
			break;
		} else if (descend) {
			char *uri = g_file_get_uri (descend);
			rb_debug ("adding dir %s to processing list", uri);
			g_free (uri);
			g_queue_push_tail (data->dirs_left, descend);
		}
	}

	g_list_free_full (files, g_object_unref);
	_uri_handle_recursively_next_files (data);
}

static void
_uri_handle_recursively_next_files (RBUriHandleRecursivelyAsyncData *data)
{
	g_file_enumerator_next_files_async (data->enumerator,
					    16,		/* or something */
					    G_PRIORITY_DEFAULT,
					    data->cancel,
					    _uri_handle_recursively_process_files,
					    data);
}

static void
_uri_handle_recursively_enum_files (GObject *src, GAsyncResult *result, gpointer ptr)
{
	GError *error = NULL;
	RBUriHandleRecursivelyAsyncData *data = ptr;

	data->enumerator = g_file_enumerate_children_finish (G_FILE (src), result, &error);
	if (error != NULL) {
		if (error->code == G_IO_ERROR_NOT_DIRECTORY) {
			GFileInfo *info;

			g_clear_error (&error);
			info = g_file_query_info (G_FILE (src), recurse_attributes, G_FILE_QUERY_INFO_NONE, data->cancel, &error);
			if (error == NULL) {
				if (_should_process (info)) {
					(data->func) (G_FILE (src), info, data->user_data);
				}
				g_object_unref (info);
			}
		} else {
			rb_debug ("error enumerating folder: %s", error->message);
		}
		g_clear_error (&error);
		_uri_handle_recursively_next_dir (data);
	} else {
		_uri_handle_recursively_next_files (data);
	}
}

static void
_uri_handle_recursively_next_dir (RBUriHandleRecursivelyAsyncData *data)
{
	g_clear_object (&data->current);
	g_clear_object (&data->enumerator);
	data->current = g_queue_pop_head (data->dirs_left);
	if (data->current != NULL) {
		g_file_enumerate_children_async (data->current,
						 recurse_attributes,
						 G_FILE_QUERY_INFO_NONE,
						 G_PRIORITY_DEFAULT,
						 data->cancel,
						 _uri_handle_recursively_enum_files,
						 data);
	} else {
		rb_debug ("nothing more to do");
		_uri_handle_recursively_free (data);
	}
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
	
	rb_debug ("processing %s", uri);
	if (cancel != NULL) {
		data->cancel = g_object_ref (cancel);
	} else {
		data->cancel = g_cancellable_new ();
	}

	data->func = func;
	data->user_data = user_data;
	data->data_destroy = data_destroy;
	data->handled = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	data->dirs_left = g_queue_new ();
	g_queue_push_tail (data->dirs_left, g_file_new_for_uri (uri));
	_uri_handle_recursively_next_dir (data);
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
		g_propagate_error (error, e);
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
	GError *err = NULL;

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

	ret = g_file_make_directory_with_parents (parent, NULL, &err);
	if (err != NULL) {
		if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_error_free (err);
			ret = TRUE;
		} else {
			g_propagate_error (error, err);
		}
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
 * Return value: (transfer full): #GFile for the nearest extant ancestor
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
 * rb_uri_is_descendant:
 * @uri: URI to check
 * @ancestor: a URI to check against
 *
 * Checks if @uri refers to a path beneath @ancestor, such that removing some number
 * of path segments of @uri would result in @ancestor.
 * It doesn't do any filesystem operations, it just looks at the URIs as strings.
 * The URI strings should be built by looking at a filesystem rather than user input,
 * and must not have path segments that are empty (multiple slashes) or '.' or '..'.
 *
 * Given this input, checking if one URI is a descendant of another is pretty simple.
 * A descendant URI must have the ancestor as a prefix, and if the ancestor ends with
 * a slash, there must be at least one character after that, otherwise the following
 * character must be a slash with at least one character after it.
 *
 * Return value: %TRUE if @uri is a descendant of @ancestor
 */
gboolean
rb_uri_is_descendant (const char *uri, const char *ancestor)
{
	int len;

	if (g_str_has_prefix (uri, ancestor) == FALSE)
		return FALSE;

	len = strlen(ancestor);
	if (ancestor[len - 1] == '/') {
		/*
		 * following character in uri must be a new path segment, not
		 * the end of the uri.  not considering multiple slashes here.
		 */
		return (uri[len] != '\0');
	} else {
		/*
		 * following character in uri must be a separator, with something after it.
		 * not considering multiple slashes here.
		 */
		return ((uri[len] == '/') && strlen(uri) > (len + 1));
	}
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

static void
sanitize_msdos_path (char *path)
{
	g_strdelimit (path, "\"", '\'');
	g_strdelimit (path, ":|<>*?\\", '_');
}

/**
 * rb_sanitize_path_for_msdos_filesystem:
 * @path: a path segment to sanitize (modified in place)
 *
 * Modifies @path such that it represents a legal path for MS DOS
 * filesystems.  Note that it replaces forward slash characters,
 * so it's only appropriate for use with individual path segments
 * rather than entire paths.
 */
void
rb_sanitize_path_for_msdos_filesystem (char *path)
{
	sanitize_msdos_path (path);
	g_strdelimit (path, "/", '-');
}

/**
 * rb_sanitize_uri_for_filesystem:
 * @uri: a URI to sanitize
 * @filesystem: (allow-none): a specific filesystem to sanitize for
 *
 * Removes characters from @uri that are not allowed by the filesystem
 * on which it would be stored, or a specific type of filesystem if specified.
 * At present, this only supports MS DOS filesystems.
 *
 * Return value: sanitized copy of @uri, must be freed by caller.
 */
char *
rb_sanitize_uri_for_filesystem (const char *uri, const char *filesystem)
{
	char *free_fs = NULL;
	char *mountpoint = NULL;
	char *sane_uri = NULL;

	if (filesystem == NULL) {
		free_fs = rb_uri_get_filesystem_type (uri, &mountpoint);
		if (!free_fs)
			return g_strdup (uri);

		filesystem = free_fs;
	}

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
			g_free (free_fs);
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
		sanitize_msdos_path (fat_path);

		/* create a new uri from this */
		sane_uri = g_filename_to_uri (full_path, hostname, &error);
		rb_debug ("sanitized URI: %s", sane_uri);

		g_free (hostname);
		g_free (full_path);

		if (error) {
			g_error_free (error);
			g_free (free_fs);
			g_free (mountpoint);
			return g_strdup (uri);
		}
	}

	/* add workarounds for other filesystems limitations here */

	g_free (free_fs);
	g_free (mountpoint);
	return sane_uri ? sane_uri : g_strdup (uri);
}

