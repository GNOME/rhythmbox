/* 
   arch-tag: Implementation of Rhythmbox playlist parser

   Copyright (C) 2002, 2003 Bastien Nocera
   Copyright (C) 2003,2004 Colin Walters <walters@rhythmbox.org>

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Bastien Nocera <hadess@hadess.net>
 */

#include "config.h"
#include "totem-pl-parser.h"

#include "totemplparser-marshal.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#if HAVE_LIBGNOME_DESKTOP
#include <libgnome/gnome-desktop-item.h>
#endif
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>

#define READ_CHUNK_SIZE 8192
#define MIME_READ_CHUNK_SIZE 1024

typedef TotemPlParserResult (*PlaylistCallback) (TotemPlParser *parser, const char *url, gpointer data);
static gboolean totem_pl_parser_scheme_is_ignored (TotemPlParser *parser, const char *url);
static gboolean totem_pl_parser_ignore (TotemPlParser *parser, const char *url);
static TotemPlParserResult totem_pl_parser_parse_internal (TotemPlParser *parser, const char *url);
static TotemPlParserResult totem_pl_parser_add_asx (TotemPlParser *parser, const char *url, gpointer data);

typedef struct {
	char *mimetype;
	PlaylistCallback func;
} PlaylistTypes;

struct TotemPlParserPrivate
{
	GList *ignore_schemes;
	guint recurse_level;
	gboolean fallback;
};

/* Signals */
enum {
	ENTRY,
	LAST_SIGNAL
};

static int totem_pl_parser_table_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

static void totem_pl_parser_class_init (TotemPlParserClass *class);
static void totem_pl_parser_init       (TotemPlParser      *parser);
static void totem_pl_parser_finalize   (GObject *object);

G_DEFINE_TYPE(TotemPlParser, totem_pl_parser, G_TYPE_OBJECT)

static void
totem_pl_parser_class_init (TotemPlParserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = totem_pl_parser_finalize;

	/* Signals */
	totem_pl_parser_table_signals[ENTRY] =
		g_signal_new ("entry",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TotemPlParserClass, entry),
			      NULL, NULL,
			      totemplparser_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

GQuark
totem_pl_parser_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("totem_pl_parser_error");

	return quark;
}

TotemPlParser *
totem_pl_parser_new (void)
{
	return TOTEM_PL_PARSER (g_object_new (TOTEM_TYPE_PL_PARSER, NULL));
}

static const char *
my_gnome_vfs_get_mime_type_with_data (const char *uri, gpointer *data)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	char *buffer;
	const char *mimetype;
	GnomeVFSFileSize total_bytes_read;
	GnomeVFSFileSize bytes_read;

	*data = NULL;

	/* Open the file. */
	result = gnome_vfs_open (&handle, uri, GNOME_VFS_OPEN_READ);
	if (result != GNOME_VFS_OK)
		return NULL;

	/* Read the whole thing, up to MIME_READ_CHUNK_SIZE */
	buffer = NULL;
	total_bytes_read = 0;
	do {
		buffer = g_realloc (buffer, total_bytes_read
				+ MIME_READ_CHUNK_SIZE);
		result = gnome_vfs_read (handle,
				buffer + total_bytes_read,
				MIME_READ_CHUNK_SIZE,
				&bytes_read);
		if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_EOF) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return NULL;
		}

		/* Check for overflow. */
		if (total_bytes_read + bytes_read < total_bytes_read) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return NULL;
		}

		total_bytes_read += bytes_read;
	} while (result == GNOME_VFS_OK
			&& total_bytes_read < MIME_READ_CHUNK_SIZE);

	/* Close the file. */
	result = gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		g_free (buffer);
		return NULL;
	}

	/* Return the file. */
	*data = g_realloc (buffer, total_bytes_read);
	mimetype = gnome_vfs_get_mime_type_for_data (*data, total_bytes_read);

	return mimetype;
}

static char*
totem_pl_parser_base_url (const char *url)
{
	/* Yay, let's reconstruct the base by hand */
	GnomeVFSURI *uri, *parent;
	char *base;

	uri = gnome_vfs_uri_new (url);
	parent = gnome_vfs_uri_get_parent (uri);
	base = gnome_vfs_uri_to_string (parent, 0);

	gnome_vfs_uri_unref (uri);
	gnome_vfs_uri_unref (parent);

	return base;
}

static gboolean
write_string (GnomeVFSHandle *handle, const char *buf, GError **error)
{
	GnomeVFSResult res;
	GnomeVFSFileSize written;
	guint len;

	len = strlen (buf);
	res = gnome_vfs_write (handle, buf, len, &written);
	if (res != GNOME_VFS_OK || written < len) {
		g_set_error (error,
			     TOTEM_PL_PARSER_ERROR,
			     TOTEM_PL_PARSER_ERROR_VFS_WRITE,
			     _("Couldn't write parser: %s"),
			     gnome_vfs_result_to_string (res));
		gnome_vfs_close (handle);
		return FALSE;
	}

	return TRUE;
}

static int
totem_pl_parser_num_entries (TotemPlParser *parser, GtkTreeModel *model,
			     TotemPlParserIterFunc func)
{
	int num_entries, i, ignored;

	num_entries = gtk_tree_model_iter_n_children (model, NULL);
	ignored = 0;

	for (i = 1; i <= num_entries; i++)
	{
		GtkTreeIter iter;
		char *path, *url, *title;

		path = g_strdup_printf ("%d", i - 1);
		gtk_tree_model_get_iter_from_string (model, &iter, path);
		g_free (path);

		func (model, &iter, &url, &title);
		if (totem_pl_parser_scheme_is_ignored (parser, url) != FALSE)
			ignored++;

		g_free (url);
		g_free (title);
	}

	return num_entries - ignored;
}

static char *
totem_pl_parser_relative (const char *url, const char *output)
{
	char *url_base, *output_base;
	char *base, *needle;

	base = NULL;
	url_base = totem_pl_parser_base_url (url);
	output_base = totem_pl_parser_base_url (output);

	needle = strstr (url_base, output_base);
	if (needle != NULL)
	{
		GnomeVFSURI *uri;
		char *newurl;

		uri = gnome_vfs_uri_new (url);
		newurl = gnome_vfs_uri_to_string (uri, 0);
		if (newurl[strlen (output_base)] == '/') {
			base = g_strdup (newurl + strlen (output_base) + 1);
		} else {
			base = g_strdup (newurl + strlen (output_base));
		}
		gnome_vfs_uri_unref (uri);
		g_free (newurl);

		/* And finally unescape the string */
		newurl = gnome_vfs_unescape_string (base, NULL);
		g_free (base);
		base = newurl;
	}

	g_free (url_base);
	g_free (output_base);

	return base;
}

static gboolean
totem_pl_parser_write_pls (TotemPlParser *parser, GtkTreeModel *model,
		TotemPlParserIterFunc func, const char *output,
		GError **error)
{
	GnomeVFSHandle *handle;
	GnomeVFSResult res;
	int num_entries_total, num_entries, i;
	char *buf;
	gboolean success;

	num_entries = totem_pl_parser_num_entries (parser, model, func);
	num_entries_total = gtk_tree_model_iter_n_children (model, NULL);

	res = gnome_vfs_open (&handle, output, GNOME_VFS_OPEN_WRITE);
	if (res == GNOME_VFS_ERROR_NOT_FOUND) {
		res = gnome_vfs_create (&handle, output,
				GNOME_VFS_OPEN_WRITE, FALSE,
				GNOME_VFS_PERM_USER_WRITE
				| GNOME_VFS_PERM_USER_READ
				| GNOME_VFS_PERM_GROUP_READ);
	}

	if (res != GNOME_VFS_OK) {
		g_set_error(error,
			    TOTEM_PL_PARSER_ERROR,
			    TOTEM_PL_PARSER_ERROR_VFS_OPEN,
			    _("Couldn't open file '%s': %s"),
			    output, gnome_vfs_result_to_string (res));
		return FALSE;
	}

	buf = g_strdup ("[playlist]\n");
	success = write_string (handle, buf, error);
	g_free (buf);
	if (success == FALSE)
		return FALSE;

	buf = g_strdup_printf ("NumberOfEntries=%d\n", num_entries);
	success = write_string (handle, buf, error);
	g_free (buf);
	if (success == FALSE)
	{
		gnome_vfs_close (handle);
		return FALSE;
	}

	for (i = 1; i <= num_entries_total; i++) {
		GtkTreeIter iter;
		char *path, *url, *title, *relative;

		path = g_strdup_printf ("%d", i - 1);
		gtk_tree_model_get_iter_from_string (model, &iter, path);
		g_free (path);

		func (model, &iter, &url, &title);

		if (totem_pl_parser_scheme_is_ignored (parser, url) != FALSE)
		{
			g_free (url);
			g_free (title);
			continue;
		}

		relative = totem_pl_parser_relative (url, output);
		buf = g_strdup_printf ("File%d=%s\n", i,
				relative ? relative : url);
		g_free (relative);
		g_free (url);
		success = write_string (handle, buf, error);
		g_free (buf);
		if (success == FALSE)
		{
			gnome_vfs_close (handle);
			g_free (title);
			return FALSE;
		}

		buf = g_strdup_printf ("Title%d=%s\n", i, title);
		success = write_string (handle, buf, error);
		g_free (buf);
		g_free (title);
		if (success == FALSE)
		{
			gnome_vfs_close (handle);
			return FALSE;
		}
	}

	gnome_vfs_close (handle);
	return TRUE;
}

static char *
totem_pl_parser_url_to_dos (const char *url, const char *output)
{
	char *retval, *i;

	retval = totem_pl_parser_relative (url, output);

	if (retval == NULL)
		retval = g_strdup (url);

	/* Don't change URIs, but change smb:// */
	if (g_str_has_prefix (retval, "smb://") != FALSE)
	{
		char *tmp;
		tmp = g_strdup (retval + strlen ("smb:"));
		g_free (retval);
		retval = tmp;
	}

	if (strstr (retval, "://") != NULL)
		return retval;

	i = retval;
	while (*i != '\0')
	{
		if (*i == '/')
			*i = '\\';
		i++;
	}

	return retval;
}

static gboolean
totem_pl_parser_write_m3u (TotemPlParser *parser, GtkTreeModel *model,
		TotemPlParserIterFunc func, const char *output,
		gboolean dos_compatible, GError **error)
{
	GnomeVFSHandle *handle;
	GnomeVFSResult res;
	int num_entries_total, i;
	gboolean success;
	char *buf;

	res = gnome_vfs_open (&handle, output, GNOME_VFS_OPEN_WRITE);
	if (res == GNOME_VFS_ERROR_NOT_FOUND) {
		res = gnome_vfs_create (&handle, output,
				GNOME_VFS_OPEN_WRITE, FALSE,
				GNOME_VFS_PERM_USER_WRITE
				| GNOME_VFS_PERM_USER_READ
				| GNOME_VFS_PERM_GROUP_READ);
	}

	if (res != GNOME_VFS_OK) {
		g_set_error(error,
			    TOTEM_PL_PARSER_ERROR,
			    TOTEM_PL_PARSER_ERROR_VFS_OPEN,
			    _("Couldn't open file '%s': %s"),
			    output, gnome_vfs_result_to_string (res));
		return FALSE;
	}

	num_entries_total = gtk_tree_model_iter_n_children (model, NULL);

	for (i = 1; i <= num_entries_total; i++) {
		GtkTreeIter iter;
		char *path, *url, *title;

		path = g_strdup_printf ("%d", i - 1);
		gtk_tree_model_get_iter_from_string (model, &iter, path);
		g_free (path);

		func (model, &iter, &url, &title);

		if (totem_pl_parser_scheme_is_ignored (parser, url) != FALSE)
		{
			g_free (url);
			g_free (title);
			continue;
		}

		if (dos_compatible != FALSE)
		{
			char *dos;

			dos = totem_pl_parser_url_to_dos (url, output);
			buf = g_strdup_printf ("%s\r\n", dos);
			g_free (dos);
		} else {
			char *relative;

			relative = totem_pl_parser_relative (url, output);
			buf = g_strdup_printf ("%s\n", relative);
			g_free (relative);
		}

		success = write_string (handle, url, error);

		if (success == FALSE)
		{
			gnome_vfs_close (handle);
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
totem_pl_parser_write (TotemPlParser *parser, GtkTreeModel *model,
		TotemPlParserIterFunc func, const char *output,
		TotemPlParserType type, GError **error)
{
	switch (type)
	{
	case TOTEM_PL_PARSER_PLS:
		return totem_pl_parser_write_pls (parser, model, func,
				output, error);
	case TOTEM_PL_PARSER_M3U:
	case TOTEM_PL_PARSER_M3U_DOS:
		return totem_pl_parser_write_m3u (parser, model, func,
				output, (type == TOTEM_PL_PARSER_M3U_DOS),
				error);
	default:
		g_assert_not_reached ();
	}

	return FALSE;
}

static GnomeVFSResult
my_eel_read_entire_file (const char *uri,
		int *file_size,
		char **file_contents)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	char *buffer;
	GnomeVFSFileSize total_bytes_read;
	GnomeVFSFileSize bytes_read;

	*file_size = 0;
	*file_contents = NULL;

	/* Open the file. */
	result = gnome_vfs_open (&handle, uri, GNOME_VFS_OPEN_READ);
	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* Read the whole thing. */
	buffer = NULL;
	total_bytes_read = 0;
	do {
		buffer = g_realloc (buffer, total_bytes_read + READ_CHUNK_SIZE);
		result = gnome_vfs_read (handle,
				buffer + total_bytes_read,
				READ_CHUNK_SIZE,
				&bytes_read);
		if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_EOF) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return result;
		}

		/* Check for overflow. */
		if (total_bytes_read + bytes_read < total_bytes_read) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return GNOME_VFS_ERROR_TOO_BIG;
		}

		total_bytes_read += bytes_read;
	} while (result == GNOME_VFS_OK);

	/* Close the file. */
	result = gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		g_free (buffer);
		return result;
	}

	/* Return the file. */
	*file_size = total_bytes_read;
	*file_contents = g_realloc (buffer, total_bytes_read);

	return GNOME_VFS_OK;
}

static int
read_ini_line_int (char **lines, const char *key)
{
	int retval = -1;
	int i;

	if (lines == NULL || key == NULL)
		return -1;

	for (i = 0; (lines[i] != NULL && retval == -1); i++) {
		if (g_ascii_strncasecmp (lines[i], key, strlen (key)) == 0) {
			char **bits;

			bits = g_strsplit (lines[i], "=", 2);
			if (bits[0] == NULL || bits [1] == NULL) {
				g_strfreev (bits);
				return -1;
			}

			retval = (gint) g_strtod (bits[1], NULL);
			g_strfreev (bits);
		}
	}

	return retval;
}

static char*
read_ini_line_string (char **lines, const char *key, gboolean dos_mode)
{
	char *retval = NULL;
	int i;

	if (lines == NULL || key == NULL)
		return NULL;

	for (i = 0; (lines[i] != NULL && retval == NULL); i++) {
		if (g_ascii_strncasecmp (lines[i], key, strlen (key)) == 0) {
			char **bits;
			ssize_t len;

			bits = g_strsplit (lines[i], "=", 2);
			if (bits[0] == NULL || bits [1] == NULL) {
				g_strfreev (bits);
				return NULL;
			}

			retval = g_strdup (bits[1]);
			len = strlen (retval);
			if (dos_mode && len >= 2 && retval[len-2] == '\r') {
				retval[len-2] = '\n';
				retval[len-1] = '\0';
			}

			g_strfreev (bits);
		}
	}

	return retval;
}

static void
totem_pl_parser_init (TotemPlParser *parser)
{
	parser->priv = g_new0 (TotemPlParserPrivate, 1);
}

static void
totem_pl_parser_finalize (GObject *object)
{
	TotemPlParser *parser = TOTEM_PL_PARSER (object);

	g_return_if_fail (object != NULL);
	g_return_if_fail (parser->priv != NULL);

	g_list_foreach (parser->priv->ignore_schemes, (GFunc) g_free, NULL);

	if (G_OBJECT_CLASS (parent_class)->finalize != NULL) {
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
	}
}

static void
totem_pl_parser_add_one_url (TotemPlParser *parser, const char *url, const char *title)
{
	g_signal_emit (G_OBJECT (parser), totem_pl_parser_table_signals[ENTRY],
		       0, url, title, NULL);
}

static void
totem_pl_parser_add_one_url_ext (TotemPlParser *parser, const char *url, const char *title,
			     const char *genre)
{
	g_signal_emit (G_OBJECT (parser), totem_pl_parser_table_signals[ENTRY],
		       0, url, title, genre);
}

static TotemPlParserResult
totem_pl_parser_add_ram (TotemPlParser *parser, const char *url, gpointer data)
{
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;
	char *contents, **lines;
	int size, i;
	const char *split_char;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	/* figure out whether we're a unix or dos RAM file */
	if (strstr(contents,"\x0d") == NULL)
		split_char = "\n";
	else
		split_char = "\x0d\n";

	lines = g_strsplit (contents, split_char, 0);
	g_free (contents);

	for (i = 0; lines[i] != NULL; i++) {
		if (strcmp (lines[i], "") == 0)
			continue;

		retval = TOTEM_PL_PARSER_RESULT_SUCCESS;

		/* Either it's a URI, or it has a proper path ... */
		if (strstr(lines[i], "://") != NULL
				|| lines[i][0] == G_DIR_SEPARATOR) {
			/* .ram files can contain .smil entries */
			if (totem_pl_parser_parse_internal (parser, lines[i]) != TOTEM_PL_PARSER_RESULT_SUCCESS)
			{
				totem_pl_parser_add_one_url (parser,
						lines[i], NULL);
			}
		} else if (strcmp (lines[i], "--stop--") == 0) {
			/* For Real Media playlists, handle the stop command */
			break;
		} else {
			char *fullpath, *base;

			/* Try with a base */
			base = totem_pl_parser_base_url (url);

			fullpath = g_strdup_printf ("%s/%s", base, lines[i]);
			if (totem_pl_parser_parse_internal (parser, fullpath) != TOTEM_PL_PARSER_RESULT_SUCCESS)
			{
				totem_pl_parser_add_one_url (parser, fullpath, NULL);
			}
			g_free (fullpath);
			g_free (base);
		}
	}

	g_strfreev (lines);

	return retval;
}

static const char *
totem_pl_parser_get_extinfo_title (gboolean extinfo, char **lines, int i)
{
	const char *retval;

	if (extinfo == FALSE || lines == NULL)
		return NULL;

	if (i == 0)
		return NULL;

	retval = strstr (lines[i-1], "#EXTINF:");
	retval = strstr (retval, ",");
	if (retval == NULL || retval[0] == '\0')
		return NULL;

	retval++;

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_m3u (TotemPlParser *parser, const char *url, gpointer data)
{
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;
	char *contents, **lines;
	int size, i;
	const char *split_char;
	gboolean extinfo;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	/* is TRUE if there's an EXTINF on the previous line */
	extinfo = FALSE;

	/* figure out whether we're a unix m3u or dos m3u */
	if (strstr(contents,"\x0d") == NULL)
		split_char = "\n";
	else
		split_char = "\x0d\n";

	lines = g_strsplit (contents, split_char, 0);
	g_free (contents);

	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0')
			continue;

		retval = TOTEM_PL_PARSER_RESULT_SUCCESS;

		/* Ignore comments, but mark it if we have extra info */
		if (lines[i][0] == '#') {
			if (strstr (lines[i], "#EXTINF") != NULL)
				extinfo = TRUE;
			continue;
		}

		/* Either it's a URI, or it has a proper path ... */
		if (strstr(lines[i], "://") != NULL
				|| lines[i][0] == G_DIR_SEPARATOR) {
			totem_pl_parser_add_one_url (parser, lines[i],
					totem_pl_parser_get_extinfo_title (extinfo, lines, i));
			extinfo = FALSE;
		} else if (lines[i][0] == '\\' && lines[i][1] == '\\') {
			/* ... Or it's in the windows smb form
			 * (\\machine\share\filename), Note drive names
			 * (C:\ D:\ etc) are unhandled (unknown base for
			 * drive letters) */
		        char *tmpurl;

			lines[i] = g_strdelimit (lines[i], "\\", '/');
			tmpurl = g_strjoin (NULL, "smb:", lines[i], NULL);

			totem_pl_parser_add_one_url (parser, lines[i],
					totem_pl_parser_get_extinfo_title (extinfo, lines, i));
			extinfo = FALSE;

			g_free (tmpurl);
		} else {
			/* Try with a base */
			char *fullpath, *base, sep;

			base = totem_pl_parser_base_url (url);
			sep = (split_char[0] == '\n' ? '/' : '\\');
			if (sep == '\\')
				lines[i] = g_strdelimit (lines[i], "\\", '/');
			fullpath = g_strdup_printf ("%s/%s", base, lines[i]);
			totem_pl_parser_add_one_url (parser, fullpath,
					totem_pl_parser_get_extinfo_title (extinfo, lines, i));
			g_free (fullpath);
			g_free (base);
			extinfo = FALSE;
		}
	}

	g_strfreev (lines);

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_asf_parser (TotemPlParser *parser, const char *url,
		gpointer data)
{
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;
	char *contents, **lines, *ref;
	int size;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	lines = g_strsplit (contents, "\n", 0);
	g_free (contents);

	ref = read_ini_line_string (lines, "Ref1", FALSE);

	if (ref == NULL) {
		g_strfreev (lines);
		return totem_pl_parser_add_asx (parser, url, data);
	}

	/* change http to mms, thanks Microsoft */
	if (g_str_has_prefix (ref, "http") != FALSE)
		memcpy(ref, "mmsh", 4);

	totem_pl_parser_add_one_url (parser, ref, NULL);
	retval = TOTEM_PL_PARSER_RESULT_SUCCESS;
	g_free (ref);

	g_strfreev (lines);

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_pls (TotemPlParser *parser, const char *url, gpointer data)
{
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;
	char *contents, **lines;
	int size, i, num_entries;
	char *split_char;
	gboolean dos_mode = FALSE;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	if (size == 0)
	{
		g_free (contents);
		return TOTEM_PL_PARSER_RESULT_SUCCESS;
	}

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	/* figure out whether we're a unix pls or dos pls */
	if (strstr(contents,"\x0d") == NULL)
	{
		split_char = "\n";
	} else {
		split_char = "\x0d\n";
		dos_mode = TRUE;
	}
	lines = g_strsplit (contents, split_char, 0);
	g_free (contents);

	/* [parser] */
	if (g_ascii_strncasecmp (lines[0], "[playlist]",
				(gsize)strlen ("[playlist]")) != 0)
		goto bail;

	/* numberofentries=? */
	num_entries = read_ini_line_int (lines, "numberofentries");
	if (num_entries == -1)
		goto bail;

	retval = TOTEM_PL_PARSER_RESULT_SUCCESS;

	for (i = 1; i <= num_entries; i++) {
		char *file, *title, *genre;
		char *file_key, *title_key, *genre_key;

		file_key = g_strdup_printf ("file%d", i);
		title_key = g_strdup_printf ("title%d", i);
		/* Genre is our own little extension */
		genre_key = g_strdup_printf ("genre%d", i);

		file = read_ini_line_string (lines, (const char*)file_key, dos_mode);
		title = read_ini_line_string (lines, (const char*)title_key, dos_mode);
		genre = read_ini_line_string (lines, (const char*)genre_key, dos_mode);

		g_free (file_key);
		g_free (title_key);
		g_free (genre_key);

		if (file != NULL)
			totem_pl_parser_add_one_url_ext (parser,
					file, title, genre);

		g_free (file);
		g_free (title);
		g_free (genre);
	}

bail:
	g_strfreev (lines);

	return retval;
}

static gboolean
parse_asx_entry (TotemPlParser *parser, char *base, xmlDocPtr doc,
		xmlNodePtr parent)
{
	xmlNodePtr node;
	char *title, *url;
	gboolean retval = FALSE;

	title = NULL;
	url = NULL;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		/* ENTRY should only have one ref and one title nodes */
		if (g_ascii_strcasecmp (node->name, "ref") == 0
				|| g_ascii_strcasecmp (node->name, "entryref") == 0) {
			url = xmlGetProp (node, "href");
			if (url == NULL)
				url = xmlGetProp (node, "HREF");
			continue;
		}

		if (g_ascii_strcasecmp (node->name, "title") == 0)
			title = xmlNodeListGetString(doc, node->children, 1);
	}

	if (url == NULL) {
		g_free (title);
		return FALSE;
	}

	if (strstr (url, "://") != NULL || url[0] == '/') {
		totem_pl_parser_add_one_url (parser, url, title);
		retval = TRUE;
	} else {
		char *fullpath;

		fullpath = g_strdup_printf ("%s/%s", base, url);
		/* .asx files can contain references to other .asx files */
		if (totem_pl_parser_parse_internal (parser, fullpath) != TOTEM_PL_PARSER_RESULT_SUCCESS)
			totem_pl_parser_add_one_url (parser, fullpath, NULL);

		g_free (fullpath);
	}

	g_free (title);
	g_free (url);

	return retval;
}

static gboolean
parse_asx_entries (TotemPlParser *parser, char *base, xmlDocPtr doc,
		xmlNodePtr parent)
{
	xmlNodePtr node;
	gboolean retval = FALSE;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "entry") == 0) {
			/* Whee found an entry here, find the REF and TITLE */
			if (parse_asx_entry (parser, base, doc, node) != FALSE)
				retval = TRUE;
		}
		if (g_ascii_strcasecmp (node->name, "entryref") == 0) {
			/* Found an entryref, give the parent instead of the
			 * children to the parser */
			if (parse_asx_entry (parser, base, doc, parent) != FALSE)
				retval = TRUE;
		}
	}

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_asx (TotemPlParser *parser, const char *url, gpointer data)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	char *contents = NULL, *base;
	int size;
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	doc = xmlParseMemory (contents, size);
	if (doc == NULL)
		doc = xmlRecoverMemory (contents, size);
	g_free (contents);

	/* If the document has no root, or no name */
	if(!doc || !doc->children || !doc->children->name) {
		if (doc != NULL)
			xmlFreeDoc(doc);
		return TOTEM_PL_PARSER_RESULT_ERROR;
	}

	base = totem_pl_parser_base_url (url);

	for (node = doc->children; node != NULL; node = node->next)
		if (parse_asx_entries (parser, base, doc, node) != FALSE)
			retval = TOTEM_PL_PARSER_RESULT_SUCCESS;

	g_free (base);
	xmlFreeDoc(doc);
	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_ra (TotemPlParser *parser, const char *url, gpointer data)
{
	if (data == NULL
			|| (g_str_has_prefix (data, "http://") == FALSE
			&& g_str_has_prefix (data, "rtsp://") == FALSE
			&& g_str_has_prefix (data, "pnm://") == FALSE)) {
		totem_pl_parser_add_one_url (parser, url, NULL);
		return TOTEM_PL_PARSER_RESULT_SUCCESS;
	}

	return totem_pl_parser_add_ram (parser, url, NULL);
}

static gboolean
parse_smil_video_entry (TotemPlParser *parser, char *base,
		char *url, char *title)
{
	if (strstr (url, "://") != NULL || url[0] == '/') {
		totem_pl_parser_add_one_url (parser, url, title);
	} else {
		char *fullpath;

		fullpath = g_strdup_printf ("%s/%s", base, url);
		totem_pl_parser_add_one_url (parser, fullpath, title);

		g_free (fullpath);
	}

	return TRUE;
}

static gboolean
parse_smil_entry (TotemPlParser *parser, char *base, xmlDocPtr doc,
		xmlNodePtr parent)
{
	xmlNodePtr node;
	char *title, *url;
	gboolean retval = FALSE;

	title = NULL;
	url = NULL;

	for (node = parent->children; node != NULL; node = node->next)
	{
		if (node->name == NULL)
			continue;

		/* ENTRY should only have one ref and one title nodes */
		if (g_ascii_strcasecmp (node->name, "video") == 0) {
			url = xmlGetProp (node, "src");
			title = xmlGetProp (node, "title");

			if (url != NULL) {
				if (parse_smil_video_entry (parser,
						base, url, title) != FALSE)
					retval = TRUE;
			}

			g_free (title);
			g_free (url);
		} else {
			if (parse_smil_entry (parser,
						base, doc, node) != FALSE)
				retval = TRUE;
		}
	}

	return retval;
}

static gboolean
parse_smil_entries (TotemPlParser *parser, char *base, xmlDocPtr doc,
		xmlNodePtr parent)
{
	xmlNodePtr node;
	gboolean retval = FALSE;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "body") == 0) {
			if (parse_smil_entry (parser, base,
						doc, node) != FALSE)
				retval = TRUE;
		}

	}

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_smil (TotemPlParser *parser, const char *url, gpointer data)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	char *contents = NULL, *base;
	int size;
	gboolean retval = TOTEM_PL_PARSER_RESULT_UNHANDLED;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	doc = xmlParseMemory (contents, size);
	if (doc == NULL)
		doc = xmlRecoverMemory (contents, size);
	g_free (contents);

	/* If the document has no root, or no name */
	if(!doc || !doc->children
			|| !doc->children->name
			|| g_ascii_strcasecmp (doc->children->name,
				"smil") != 0) {
		if (doc != NULL)
			xmlFreeDoc (doc);
		return TOTEM_PL_PARSER_RESULT_ERROR;
	}

	base = totem_pl_parser_base_url (url);

	for (node = doc->children; node != NULL; node = node->next)
		if (parse_smil_entries (parser, base, doc, node) != FALSE)
			retval = TOTEM_PL_PARSER_RESULT_SUCCESS;

	xmlFreeDoc (doc);

	return retval;
}

static TotemPlParserResult
totem_pl_parser_add_asf (TotemPlParser *parser, const char *url, gpointer data)
{
	if (data != NULL &&
		g_str_has_prefix (data, "[Reference]") == FALSE) {
		totem_pl_parser_add_one_url (parser, url, NULL);
		return TOTEM_PL_PARSER_RESULT_SUCCESS;
	}

	return totem_pl_parser_add_asf_parser (parser, url, data);
}

#if HAVE_LIBGNOME_DESKTOP
static TotemPlParserResult
totem_pl_parser_add_desktop (TotemPlParser *parser, const char *url, gpointer data)
{
	GnomeDesktopItem *ditem;
	int type;
	const char *path, *display_name;

	ditem = gnome_desktop_item_new_from_file (url, 0, NULL);
	if (ditem == NULL)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	type = gnome_desktop_item_get_entry_type (ditem);
	if (type != GNOME_DESKTOP_ITEM_TYPE_LINK) {
		gnome_desktop_item_unref (ditem);
		return TOTEM_PL_PARSER_RESULT_ERROR;
	}

	path = gnome_desktop_item_get_string (ditem, "URL");
	if (path == NULL) {
		gnome_desktop_item_unref (ditem);
		return TOTEM_PL_PARSER_RESULT_ERROR;
	}

	display_name = gnome_desktop_item_get_localestring (ditem, "Name");
	if (totem_pl_parser_ignore (parser, path) == FALSE) {
		totem_pl_parser_add_one_url (parser, path, display_name);
	} else {
		if (totem_pl_parser_parse_internal (parser, path) != TOTEM_PL_PARSER_RESULT_SUCCESS)
			totem_pl_parser_add_one_url (parser, path, display_name);
	}
	gnome_desktop_item_unref (ditem);

	return TOTEM_PL_PARSER_RESULT_SUCCESS;
}
#endif

static int
totem_pl_parser_dir_compare (GnomeVFSFileInfo *a, GnomeVFSFileInfo *b)
{
	if (a->name == NULL) {
		if (b->name == NULL)
			return 0;
		else
			return -1;
	} else {
		if (b->name == NULL)
			return 1;
		else
			return strcmp (a->name, b->name);
	}
}

static TotemPlParserResult
totem_pl_parser_add_directory (TotemPlParser *parser, const char *url,
			   gpointer data)
{
	GList *list, *l;
	GnomeVFSResult res;

	res = gnome_vfs_directory_list_load (&list, url,
			GNOME_VFS_FILE_INFO_DEFAULT);
	if (res != GNOME_VFS_OK)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	list = g_list_sort (list, (GCompareFunc) totem_pl_parser_dir_compare);
	l = list;

	while (l != NULL) {
		char *str, *fullpath;
		GnomeVFSFileInfo *info = l->data;

		if (info->name != NULL && (strcmp (info->name, ".") == 0
					|| strcmp (info->name, "..") == 0)) {
			l = l->next;
			continue;
		}

		str = g_build_filename (url, info->name, NULL);
		if (strstr (str, "://") != NULL && str[0] == '/')
			fullpath = str + 1;
		else
			fullpath = str;

		if (totem_pl_parser_parse_internal (parser, fullpath) != TOTEM_PL_PARSER_RESULT_SUCCESS)
			totem_pl_parser_add_one_url (parser, fullpath, NULL);

		g_free (str);
		l = l->next;
	}

	g_list_foreach (list, (GFunc) gnome_vfs_file_info_unref, NULL);
	g_list_free (list);

	return TOTEM_PL_PARSER_RESULT_SUCCESS;
}

/* These ones need a special treatment, mostly parser formats */
static PlaylistTypes special_types[] = {
	{ "audio/x-mpegurl", totem_pl_parser_add_m3u },
	{ "audio/x-ms-asx", totem_pl_parser_add_asx },
	{ "audio/x-scpls", totem_pl_parser_add_pls },
	{ "application/x-smil", totem_pl_parser_add_smil },
#if HAVE_LIBGNOME_DESKTOP
	{ "application/x-gnome-app-info", totem_pl_parser_add_desktop },
#endif	
	{ "x-directory/normal", totem_pl_parser_add_directory },
	{ "video/x-ms-wvx", totem_pl_parser_add_asx },
	{ "audio/x-ms-wax", totem_pl_parser_add_asx },
};

/* These ones are "dual" types, might be a video, might be a parser */
static PlaylistTypes dual_types[] = {
	{ "audio/x-real-audio", totem_pl_parser_add_ra },
	{ "audio/x-pn-realaudio", totem_pl_parser_add_ra },
	{ "application/vnd.rn-realmedia", totem_pl_parser_add_ra },
	{ "audio/x-pn-realaudio-plugin", totem_pl_parser_add_ra },
	{ "text/plain", totem_pl_parser_add_ra },
	{ "video/x-ms-asf", totem_pl_parser_add_asf },
	{ "video/x-ms-wmv", totem_pl_parser_add_asf },
};

static gboolean
totem_pl_parser_scheme_is_ignored (TotemPlParser *parser, const char *url)
{
	GList *l;

	if (parser->priv->ignore_schemes == NULL)
		return FALSE;

	for (l = parser->priv->ignore_schemes; l != NULL; l = l->next)
	{
		const char *scheme = l->data;
		if (g_str_has_prefix (url, scheme) != FALSE)
			return TRUE;
	}

	return FALSE;
}

static gboolean
totem_pl_parser_ignore (TotemPlParser *parser, const char *url)
{
	const char *mimetype;
	guint i;

	if (totem_pl_parser_scheme_is_ignored (parser, url) != FALSE)
		return TRUE;

	mimetype = gnome_vfs_get_file_mime_type (url, NULL, TRUE);
	if (mimetype == NULL || strcmp (mimetype, "application/octet-stream") == 0)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS (special_types); i++)
		if (strcmp (special_types[i].mimetype, mimetype) == 0)
			return FALSE;

	for (i = 0; i < G_N_ELEMENTS (dual_types); i++)
		if (strcmp (dual_types[i].mimetype, mimetype) == 0)
			return FALSE;

	/* It's a remote file that could be an m3u file */
	if (strcmp (mimetype, "audio/x-mp3") == 0)
	{
		if (strstr (url, "m3u") != NULL)
			return FALSE;
	}

	return TRUE;
}

static TotemPlParserResult
totem_pl_parser_parse_internal (TotemPlParser *parser, const char *url)
{
	const char *mimetype;
	guint i;
	gpointer data = NULL;
	gboolean ret = FALSE;

	if (parser->priv->recurse_level > 4)
		return TOTEM_PL_PARSER_RESULT_ERROR;

	parser->priv->recurse_level++;

	if (totem_pl_parser_ignore (parser, url) != FALSE)
		return TOTEM_PL_PARSER_RESULT_UNHANDLED;

	mimetype = gnome_vfs_get_mime_type (url);
	if (!mimetype)
		mimetype = my_gnome_vfs_get_mime_type_with_data (url, &data);

	if (mimetype == NULL)
		return TOTEM_PL_PARSER_RESULT_UNHANDLED;

	for (i = 0; i < G_N_ELEMENTS(special_types); i++) {
		if (strcmp (special_types[i].mimetype, mimetype) == 0) {
			ret = (* special_types[i].func) (parser, url, data);
			g_free (data);
			break;
		}
	}

	for (i = 0; i < G_N_ELEMENTS(dual_types); i++) {
		if (strcmp (dual_types[i].mimetype, mimetype) == 0) {
			if (data == NULL) {
				mimetype = my_gnome_vfs_get_mime_type_with_data (url, &data);
			}
			ret = (* dual_types[i].func) (parser, url, data);
			g_free (data);
			break;
		}
	}

	if (ret == FALSE && parser->priv->fallback) {
		totem_pl_parser_add_one_url (parser, url, NULL);
		return TOTEM_PL_PARSER_RESULT_SUCCESS;
	}

	if (ret)
		return TOTEM_PL_PARSER_RESULT_SUCCESS;
	else
		return TOTEM_PL_PARSER_RESULT_UNHANDLED;
}

TotemPlParserResult
totem_pl_parser_parse (TotemPlParser *parser, const char *url,
		       gboolean fallback)
{
	g_return_val_if_fail (TOTEM_IS_PL_PARSER (parser), TOTEM_PL_PARSER_RESULT_UNHANDLED);
	g_return_val_if_fail (url != NULL, TOTEM_PL_PARSER_RESULT_UNHANDLED);

	parser->priv->recurse_level = 0;
	parser->priv->fallback = fallback;
	return totem_pl_parser_parse_internal (parser, url);
}

void
totem_pl_parser_add_ignored_scheme (TotemPlParser *parser,
		const char *scheme)
{
	g_return_if_fail (TOTEM_IS_PL_PARSER (parser));

	parser->priv->ignore_schemes = g_list_prepend
		(parser->priv->ignore_schemes, g_strdup (scheme));
}

