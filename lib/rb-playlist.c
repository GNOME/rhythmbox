/* 
   arch-tag: Implementation of Rhythmbox playlist parser

   Copyright (C) 2002, 2003 Bastien Nocera
   Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>

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
#include "rb-playlist.h"

#include "rb-marshal.h"
#include "rb-file-helpers.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#if HAVE_LIBGNOME_DESKTOP
#include <libgnome/gnome-desktop-item.h>
#endif
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

static gboolean
rb_playlist_parse_recurse (RBPlaylist *playlist, const char *uri,
			   gint recurse_level);

#define READ_CHUNK_SIZE 8192
#define MIME_READ_CHUNK_SIZE 1024

typedef gboolean (*PlaylistCallback) (RBPlaylist *playlist, const char *url,
				      guint recurse_level, gpointer data);

typedef struct {
	char *mimetype;
	PlaylistCallback func;
} PlaylistTypes;

struct RBPlaylistPrivate
{
	GladeXML *xml;

	int x, y;
};

/* Signals */
enum {
	ENTRY,
	LAST_SIGNAL
};

static int rb_playlist_table_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

static void rb_playlist_class_init (RBPlaylistClass *class);
static void rb_playlist_init       (RBPlaylist      *playlist);
static void rb_playlist_finalize   (GObject *object);

GtkType
rb_playlist_get_type (void)
{
	static GtkType rb_playlist_type = 0;

	if (!rb_playlist_type) {
		static const GTypeInfo rb_playlist_info = {
			sizeof (RBPlaylistClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) rb_playlist_class_init,
			(GClassFinalizeFunc) NULL,
			NULL /* class_data */,
			sizeof (RBPlaylist),
			0 /* n_preallocs */,
			(GInstanceInitFunc) rb_playlist_init,
		};

		rb_playlist_type = g_type_register_static (G_TYPE_OBJECT,
							   "RBPlaylist", &rb_playlist_info,
							   (GTypeFlags)0);
	}

	return rb_playlist_type;
}

static void
rb_playlist_class_init (RBPlaylistClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_playlist_finalize;

	/* Signals */
	rb_playlist_table_signals[ENTRY] =
		g_signal_new ("entry",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlaylistClass, entry),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

GQuark
rb_playlist_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("rb_playlist_error");

	return quark;
}

RBPlaylist *
rb_playlist_new (void)
{
	return RB_PLAYLIST (g_object_new (RB_TYPE_PLAYLIST, NULL));
}

const char *
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

	/* Read the whole thing. */
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

static gboolean
write_string (GnomeVFSHandle *handle, const char *buf, GError **error)
{
	GnomeVFSResult res;
	GnomeVFSFileSize written;
	int len;

	len = strlen (buf);
	res = gnome_vfs_write (handle, buf, len, &written);
	if (res != GNOME_VFS_OK || written < len) {
		g_set_error (error,
			     RB_PLAYLIST_ERROR,
			     RB_PLAYLIST_ERROR_VFS_WRITE,
			     _("Couldn't write playlist: %s"),
			     gnome_vfs_result_to_string (res));
		gnome_vfs_close (handle);
		return FALSE;
	}

	return TRUE;
}

gboolean
rb_playlist_write (RBPlaylist *playlist, GtkTreeModel *model,
		   RBPlaylistIterFunc func, const char *output, GError **error)
{
	GnomeVFSHandle *handle;
	GnomeVFSResult res;
	int num_entries, i;
	char *buf;
	gboolean success;

	num_entries = gtk_tree_model_iter_n_children (model, NULL);
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
			    RB_PLAYLIST_ERROR,
			    RB_PLAYLIST_ERROR_VFS_OPEN,
			    _("Couldn't open playlist: %s"),
			    gnome_vfs_result_to_string (res));
		return FALSE;
	}

	buf = g_strdup ("[playlist]\n");
	success = write_string (handle, buf, error);
	g_free (buf);
	if (success == FALSE)
		return FALSE;

	buf = g_strdup_printf ("numberofentries=%d\n", num_entries);
	success = write_string (handle, buf, error);
	g_free (buf);
	if (success == FALSE)
		return FALSE;

	for (i = 1; i <= num_entries; i++) {
		GtkTreeIter iter;
		char *path, *url, *title;

		path = g_strdup_printf ("%d", i - 1);
		gtk_tree_model_get_iter_from_string (model, &iter, path);
		g_free (path);
		
		func (model, &iter, &url, &title);

		buf = g_strdup_printf ("file%d=%s\n", i, url);
		success = write_string (handle, buf, error);
		g_free (buf);
		g_free (url);
		if (success == FALSE)
		{
			g_free (title);
			return FALSE;
		}

		buf = g_strdup_printf ("title%d=%s\n", i, title);
		success = write_string (handle, buf, error);
		g_free (buf);
		g_free (title);
		if (success == FALSE)
			return FALSE;
	}

	gnome_vfs_close (handle);
	return TRUE;
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

static char*
rb_playlist_base_url (const char *url)
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
rb_playlist_init (RBPlaylist *playlist)
{
	playlist->priv = g_new0 (RBPlaylistPrivate, 1);
}

static void
rb_playlist_finalize (GObject *object)
{
	RBPlaylist *playlist = RB_PLAYLIST (object);

	g_return_if_fail (object != NULL);
	g_return_if_fail (playlist->priv != NULL);

	if (G_OBJECT_CLASS (parent_class)->finalize != NULL) {
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
	}
}

static void
rb_playlist_add_one_url (RBPlaylist *playlist, const char *url, const char *title)
{
	
	g_signal_emit (G_OBJECT (playlist), rb_playlist_table_signals[ENTRY],
		       0, url, title, NULL);
}

static void
rb_playlist_add_one_url_ext (RBPlaylist *playlist, const char *url, const char *title,
			     const char *genre)
{
	g_signal_emit (G_OBJECT (playlist), rb_playlist_table_signals[ENTRY],
		       0, url, title, genre);
}

static gboolean
rb_playlist_add_ram (RBPlaylist *playlist, const char *url,
			 guint recurse_level, gpointer data)
{
	gboolean retval = FALSE;
	char *contents, **lines;
	int size, i;
	const char *split_char;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

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

		/* Either it's a URI, or it has a proper path ... */
		if (strstr(lines[i], "://") != NULL
				|| lines[i][0] == G_DIR_SEPARATOR) {
			/* .ram files can contain .smil entries */
			rb_playlist_parse_recurse (playlist, lines[i],
						   recurse_level);
			rb_playlist_add_one_url (playlist, lines[i], NULL);
		} else if (strcmp (lines[i], "--stop--") == 0) {
			/* For Real Media playlists, handle the stop command */
			break;
		} else {
			char *fullpath, *base;

			/* Try with a base */
			base = rb_playlist_base_url (url);

			fullpath = g_strdup_printf ("%s/%s", base, lines[i]);
			if (rb_playlist_parse_recurse (playlist, fullpath,
						       recurse_level) == TRUE)
				retval = TRUE;

			g_free (fullpath);
			g_free (base);
		}
	}

	g_strfreev (lines);

	return retval;
}

static const char *
rb_playlist_get_extinfo_title (gboolean extinfo, char **lines, int i)
{
	const char *retval;

	if (extinfo == FALSE)
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

static gboolean
rb_playlist_add_m3u (RBPlaylist *playlist, const char *url,
		     guint recurse_level, gpointer data)
{
	gboolean retval = FALSE;
	char *contents, **lines;
	int size, i;
	char *split_char;
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

		/* Ignore comments, but mark it if we have extra info */
		if (lines[i][0] == '#') {
			if (strstr (lines[i], "#EXTINF") != NULL)
				extinfo = TRUE;
			continue;
		}

		/* Either it's a URI, or it has a proper path ... */
		if (strstr(lines[i], "://") != NULL
				|| lines[i][0] == G_DIR_SEPARATOR) {
			rb_playlist_parse_recurse (playlist, lines[i],
						   recurse_level);
			rb_playlist_add_one_url (playlist, lines[i],
						 rb_playlist_get_extinfo_title (extinfo, lines, i));
			retval = TRUE;
			extinfo = FALSE;
		} else if (lines[i][0] == '\\' && lines[i][1] == '\\') {
			/* ... Or it's in the windows smb form
			 * (\\machine\share\filename), Note drive names
			 * (C:\ D:\ etc) are unhandled (unknown base for
			 * drive letters) */
		        char *tmpurl;

			lines[i] = g_strdelimit (lines[i], "\\", '/');
			tmpurl = g_strjoin (NULL, "smb:", lines[i], NULL);

			rb_playlist_add_one_url (playlist, tmpurl, NULL);
			retval = TRUE;
			extinfo = FALSE;

			g_free (tmpurl);
		} else {
			/* Try with a base */
			char *fullpath, *base, sep;

			base = rb_playlist_base_url (url);
			sep = (split_char[0] == '\n' ? '/' : '\\');
			if (sep == '\\')
				lines[i] = g_strdelimit (lines[i], "\\", '/');
			fullpath = g_strdup_printf ("%s/%s", base, lines[i]);
			if (rb_playlist_parse_recurse (playlist, fullpath,
						       recurse_level) == TRUE)
				retval = TRUE;
			g_free (fullpath);
			g_free (base);
		}
	}

	g_strfreev (lines);

	return retval;
}

static gboolean
rb_playlist_add_asf_parser (RBPlaylist *playlist, const char *url,
			      guint recurse_level, gpointer data)
{
	gboolean retval = FALSE;
	char *contents, **lines, *ref;
	int size;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	lines = g_strsplit (contents, "\n", 0);
	g_free (contents);

	ref = read_ini_line_string (lines, "Ref1", FALSE);

	if (ref == NULL)
		goto bail;

	/* change http to mms, thanks Microsoft */
	if (strncmp ("http", ref, 4) == 0)
		memcpy(ref, "mmsh", 4);

	rb_playlist_add_one_url (playlist, ref, NULL);
	retval = TRUE;
	g_free (ref);

bail:
	g_strfreev (lines);

	return retval;
}

static gboolean
rb_playlist_add_pls (RBPlaylist *playlist, const char *url,
		     guint recurse_level, gpointer data)
{
	gboolean retval = FALSE;
	char *contents, **lines;
	int size, i, num_entries;
	char *split_char;
	gboolean dos_mode = FALSE;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

	contents = g_realloc (contents, size + 1);
	contents[size] = '\0';

	/* figure out whether we're a unix pls or dos pls */
	if (strstr(contents,"\x0d") == NULL)
		split_char = "\n";
	else {
		split_char = "\x0d\n";
		dos_mode = TRUE;
	}
	lines = g_strsplit (contents, split_char, 0);
	g_free (contents);

	/* [playlist] */
	if (g_ascii_strncasecmp (lines[0], "[playlist]",
				(gsize)strlen ("[playlist]")) != 0)
		goto bail;

	/* numberofentries=? */
	num_entries = read_ini_line_int (lines, "numberofentries");
	if (num_entries == -1)
		goto bail;

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

		if (file != NULL) {
			rb_playlist_add_one_url_ext (playlist, file, title, genre);
			retval = TRUE;
		} 

		g_free (file);
		g_free (title);
		g_free (genre);
	}

bail:
	g_strfreev (lines);

	return retval;
}

static gboolean
parse_asx_entry (RBPlaylist *playlist, guint recurse_level,
		 char *base, xmlDocPtr doc, xmlNodePtr parent)
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
		if (g_ascii_strcasecmp (node->name, "ref") == 0) {
			url = xmlGetProp (node, "href");
			continue;
		}

		if (g_ascii_strcasecmp (node->name, "title") == 0)
			title = xmlNodeListGetString (doc, node->children, 1);
	}

	if (url == NULL) {
		g_free (title);
		return FALSE;
	}

	if (strstr (url, "://") != NULL || url[0] == '/') {
		rb_playlist_add_one_url (playlist, url, title);
		retval = TRUE;
	} else {
		char *fullpath;

		fullpath = g_strdup_printf ("%s/%s", base, url);
		/* .asx files can contain references to other .asx files */
		rb_playlist_parse_recurse (playlist, fullpath, recurse_level);

		g_free (fullpath);
	}

	g_free (title);
	g_free (url);

	return retval;
}

static gboolean
parse_asx_entries (RBPlaylist *playlist, guint recurse_level,
		   char *base, xmlDocPtr doc, xmlNodePtr parent)
{
	xmlNodePtr node;
	gboolean retval = FALSE;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "entry") == 0) {
			/* Whee found an entry here, find the REF and TITLE */
			if (parse_asx_entry (playlist, recurse_level, base, doc, node) == TRUE)
				retval = TRUE;
		}
	}

	return retval;
}

static gboolean
rb_playlist_add_asx (RBPlaylist *playlist, const char *url,
		     guint recurse_level, gpointer data)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	char *contents = NULL, *base;
	int size;
	gboolean retval = FALSE;

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
		return FALSE;
	}

	base = rb_playlist_base_url (url);

	for (node = doc->children; node != NULL; node = node->next)
		if (parse_asx_entries (playlist, recurse_level, base, doc, node) == TRUE)
			retval = TRUE;

	g_free (base);
	xmlFreeDoc(doc);
	return retval;
}

static gboolean
rb_playlist_add_ra (RBPlaylist *playlist, const char *url,
		    guint recurse_level, gpointer data)
{
	if (data == NULL
			|| (strncmp (data, "http://", strlen ("http://")) != 0
			&& strncmp (data, "rtsp://", strlen ("rtsp://")) != 0
			    && strncmp (data, "pnm://", strlen ("pnm://")) != 0)) {
		rb_playlist_add_one_url (playlist, url, NULL);
		return TRUE;
	}

	return rb_playlist_add_ram (playlist, url, recurse_level, NULL);
}

static gboolean
parse_smil_video_entry (RBPlaylist *playlist, char *base,
			char *url, char *title)
{
	if (strstr (url, "://") != NULL || url[0] == '/') {
		rb_playlist_add_one_url (playlist, url, title);
	} else {
		char *fullpath;

		fullpath = g_strdup_printf ("%s/%s", base, url);
		rb_playlist_add_one_url (playlist, fullpath, title);

		g_free (fullpath);
	}

	return TRUE;
}

static gboolean
parse_smil_entry (RBPlaylist *playlist, guint recurse_level, char *base,
		  xmlDocPtr doc, xmlNodePtr parent)
{
	xmlNodePtr node;
	char *title, *url;
	gboolean retval = FALSE;

	title = NULL;
	url = NULL;

	if (recurse_level > 5)
		return FALSE;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		/* ENTRY should only have one ref and one title nodes */
		if (g_ascii_strcasecmp (node->name, "video") == 0) {
			url = xmlGetProp (node, "src");
			title = xmlGetProp (node, "title");

			if (url != NULL) {
				if (parse_smil_video_entry (playlist,
							    base, url, title) == TRUE)
					retval = TRUE;
			}

			g_free (title);
			g_free (url);
		} else {
			if (parse_smil_entry (playlist, recurse_level+1,
					      base, doc, node) == TRUE)
				retval = TRUE;
		}
	}

	return retval;
}

static gboolean
parse_smil_entries (RBPlaylist *playlist, guint recurse_level,
		    char *base, xmlDocPtr doc, xmlNodePtr parent)
{
	xmlNodePtr node;
	gboolean retval = FALSE;

	for (node = parent->children; node != NULL; node = node->next) {
		if (node->name == NULL)
			continue;

		if (g_ascii_strcasecmp (node->name, "body") == 0) {
			if (parse_smil_entry (playlist, recurse_level,
					      base, doc, node) == TRUE)
				retval = TRUE;
		}

	}

	return retval;
}

static gboolean
rb_playlist_add_smil (RBPlaylist *playlist, const char *url,
		      guint recurse_level, gpointer data)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	char *contents = NULL, *base;
	int size;
	gboolean retval = FALSE;

	if (my_eel_read_entire_file (url, &size, &contents) != GNOME_VFS_OK)
		return FALSE;

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
			xmlFreeDoc(doc);
		return FALSE;
	}

	base = rb_playlist_base_url (url);

	for (node = doc->children; node != NULL; node = node->next)
		if (parse_smil_entries (playlist, recurse_level, base, doc, node) == TRUE)
			retval = TRUE;

	return FALSE;
}

static gboolean
rb_playlist_add_asf (RBPlaylist *playlist, const char *url,
		     guint recurse_level, gpointer data)
{
	if (data == NULL) {
		rb_playlist_add_one_url (playlist, url, NULL);
		return TRUE;
	}

	if (strncmp (data, "[Reference]", strlen ("[Reference]")) != 0) {
		rb_playlist_add_one_url (playlist, url, NULL);
		return TRUE;
	}

	return rb_playlist_add_asf_parser (playlist, url, recurse_level, data);
}

#if HAVE_LIBGNOME_DESKTOP
static gboolean
rb_playlist_add_desktop (RBPlaylist *playlist, const char *url,
			 guint recurse_level, gpointer data)
{
	GnomeDesktopItem *ditem;
	int type;
	gboolean retval;
	const char *path, *display_name;

	ditem = gnome_desktop_item_new_from_file (url, 0, NULL);
	if (ditem == NULL)
		return FALSE;

	type = gnome_desktop_item_get_entry_type (ditem);
	if (type != GNOME_DESKTOP_ITEM_TYPE_LINK) {
		gnome_desktop_item_unref (ditem);
		return FALSE;
	}

	path = gnome_desktop_item_get_string (ditem, "URL");
	if (path == NULL) {
		gnome_desktop_item_unref (ditem);
		return FALSE;
	}
	display_name = gnome_desktop_item_get_localestring (ditem, "Name");
	retval = rb_playlist_add_url (playlist, path, display_name);
	gnome_desktop_item_unref (ditem);

	return retval;
}
#endif

static gboolean
rb_playlist_add_directory (RBPlaylist *playlist, const char *url,
			   guint recurse_level, gpointer data)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSFileInfo *info;
	GnomeVFSResult res;
	gboolean retval = FALSE;

	if (gnome_vfs_directory_open (&handle, url, GNOME_VFS_FILE_INFO_DEFAULT)
	    != GNOME_VFS_OK)
		return FALSE;

	info = gnome_vfs_file_info_new ();
	res = gnome_vfs_directory_read_next (handle, info);
	while (res == GNOME_VFS_OK) {
		char *str, *fullpath;

		if (info->name != NULL && (strcmp (info->name, ".") == 0
					|| strcmp (info->name, "..") == 0)) {
			res = gnome_vfs_directory_read_next (handle, info);
			continue;
		}

		str = g_build_filename (G_DIR_SEPARATOR_S,
					     url, info->name, NULL);
		if (strstr (str, "://") != NULL)
			fullpath = str + 1;
		else
			fullpath = str;

		if (rb_playlist_parse_recurse (playlist, fullpath, recurse_level) == FALSE)
			rb_playlist_add_one_url (playlist, fullpath, NULL);

		retval = TRUE;
		g_free (str);
		res = gnome_vfs_directory_read_next (handle, info);
	}

	gnome_vfs_directory_close (handle);
	gnome_vfs_file_info_unref (info);
	return retval;
}

/* These ones need a special treatment, mostly playlist formats */
static PlaylistTypes special_types[] = {
	{ "audio/x-mpegurl", rb_playlist_add_m3u },
	{ "audio/x-ms-asx", rb_playlist_add_asx },
	{ "audio/x-scpls", rb_playlist_add_pls },
	{ "application/x-smil", rb_playlist_add_smil },
#if HAVE_LIBGNOME_DESKTOP
	{ "application/x-gnome-app-info", rb_playlist_add_desktop },
#endif	
	{ "x-directory/normal", rb_playlist_add_directory },
	{ "video/x-ms-wvx", rb_playlist_add_asx },
	{ "video/x-ms-wax", rb_playlist_add_asx },
};

/* These ones are "dual" types, might be a video, might be a playlist */
static PlaylistTypes dual_types[] = {
	{ "audio/x-real-audio", rb_playlist_add_ra },
	{ "audio/x-pn-realaudio", rb_playlist_add_ra },
	{ "application/vnd.rn-realmedia", rb_playlist_add_ra },
	{ "audio/x-pn-realaudio-plugin", rb_playlist_add_ra },
	{ "text/plain", rb_playlist_add_ra },
	{ "video/x-ms-asf", rb_playlist_add_asf },
	{ "video/x-ms-wmv", rb_playlist_add_asf },
};

static gboolean
rb_playlist_add_url_from_data (RBPlaylist *playlist, const char *url,
			       guint recurse_level)
{
	const char *mimetype;
	gboolean retval;
	gpointer data;
	int i;

	mimetype = my_gnome_vfs_get_mime_type_with_data (url, &data);
	if (mimetype == NULL)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS(special_types); i++) {
		if (mimetype == NULL)
			break;
		if (strcmp (special_types[i].mimetype, mimetype) == 0) {
			retval = (* special_types[i].func) (playlist, url,
							    recurse_level, data);
			g_free (data);
			return retval;
		}
	}

	for (i = 0; i < G_N_ELEMENTS(dual_types); i++) {
		if (strcmp (dual_types[i].mimetype, mimetype) == 0) {
			retval = (* dual_types[i].func) (playlist, url,
							 recurse_level, data);
			g_free (data);
			return retval;
		}
	}

	g_free (data);

	return FALSE;
}

static gboolean
rb_playlist_parse_recurse (RBPlaylist *playlist, const char *uri,
			   gint recurse_level)
{
	const char *mimetype;
	int i;

	g_return_val_if_fail (uri != NULL, FALSE);

	if (recurse_level > 3)
		return FALSE;

	if (recurse_level > 1 && rb_uri_is_iradio (uri)) {
		rb_playlist_add_one_url (playlist, uri, NULL);
		return TRUE;
	}

	recurse_level++;

	mimetype = gnome_vfs_get_mime_type (uri);

	if (mimetype == NULL)
		return rb_playlist_add_url_from_data (playlist, uri, recurse_level);

	for (i = 0; i < G_N_ELEMENTS(special_types); i++)
		if (strcmp (special_types[i].mimetype, mimetype) == 0)
			return (* special_types[i].func) (playlist, uri, recurse_level, NULL);

	for (i = 0; i < G_N_ELEMENTS(dual_types); i++)
		if (strcmp (dual_types[i].mimetype, mimetype) == 0)
			return rb_playlist_add_url_from_data (playlist, uri, recurse_level);

	return FALSE;
}
		

gboolean
rb_playlist_parse (RBPlaylist *playlist, const char *url)
{
	return rb_playlist_parse_recurse (playlist, url, 1);
}

gboolean
rb_playlist_can_handle (const char *url)
{
	const char *mimetype;
	int i;

	g_return_val_if_fail (url != NULL, FALSE);

	mimetype = gnome_vfs_get_mime_type (url);

	if (mimetype == NULL)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS(special_types); i++)
		if (strcmp (special_types[i].mimetype, mimetype) == 0)
			return TRUE;

	for (i = 0; i < G_N_ELEMENTS(dual_types); i++)
		if (strcmp (dual_types[i].mimetype, mimetype) == 0)
			return TRUE;

	return FALSE;
}
