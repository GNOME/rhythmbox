/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "mediaplayerid.h"

#include "rb-psp-source.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-auto-playlist-source.h"
#include "rhythmdb.h"


static void rb_psp_source_create_playlists (RBGenericPlayerSource *source);

G_DEFINE_DYNAMIC_TYPE (RBPspSource, rb_psp_source, RB_TYPE_GENERIC_PLAYER_SOURCE)


static void
rb_psp_source_class_init (RBPspSourceClass *klass)
{
	RBGenericPlayerSourceClass *generic_class = RB_GENERIC_PLAYER_SOURCE_CLASS (klass);
	generic_class->load_playlists = rb_psp_source_create_playlists;
}

static void
rb_psp_source_class_finalize (RBPspSourceClass *klass)
{
}

static void
rb_psp_source_init (RBPspSource *source)
{
}

static GFile *
find_dir_no_case (GFile *root, gboolean look_for_psp)
{
	GFileEnumerator *e;
	GFileInfo *info;
	GFile *ret;
	GFile *music_dir;

	ret = music_dir = NULL;
	e = g_file_enumerate_children (root, G_FILE_ATTRIBUTE_STANDARD_NAME","G_FILE_ATTRIBUTE_STANDARD_TYPE,
				       G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (e == NULL)
		return ret;

	while ((info = g_file_enumerator_next_file (e, NULL, NULL)) != NULL) {
		const char *name;

		name = g_file_info_get_name (info);
		if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY) {
			g_object_unref (info);
			continue;
		}

		if (g_ascii_strcasecmp (name, "MUSIC") == 0) {
			music_dir = g_file_resolve_relative_path (root, name);
			g_object_unref (info);
			if (look_for_psp)
				continue;
			else
				break;
		}

		if (look_for_psp) {
			if (g_ascii_strcasecmp (name, "PSP") == 0) {
				GFile *psp_dir;

				psp_dir = g_file_resolve_relative_path (root, name);
				ret = find_dir_no_case (psp_dir, FALSE);
				g_object_unref (psp_dir);

				if (ret != NULL) {
					g_object_unref (info);
					if (music_dir != NULL)
						g_object_unref (music_dir);
					music_dir = NULL;
					break;
				}
			}
		}
		g_object_unref (info);
	}
	g_object_unref (e);

	if (ret == NULL)
		ret = music_dir;

	return ret;
}

static GFile *
find_music_dir (GMount *mount)
{
	GFile *root;
	GFile *music_dir = NULL;

	root = g_mount_get_root (mount);
	if (root != NULL) {
		music_dir = find_dir_no_case (root, TRUE);
		/* FIXME create directories if they don't exist */
		g_object_unref (root);
	}

	return music_dir;
}

static void
visit_playlist_dirs (RBPspSource *source,
		     GFile *file)
{
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
	char *playlist_path;
	char *playlist_name;
	RBSource *playlist;
	GPtrArray *query;

	playlist_path = g_file_get_uri (file);		/* or _get_path? */

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell,
		      "db", &db,
		      NULL);

	/* FIXME this isn't good enough, we only need the files directly under the playlist directory,
	 * not sub-dirs */
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, entry_type,
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_LOCATION, playlist_path,
				      RHYTHMDB_QUERY_END);
	g_free (playlist_path);
        g_object_unref (entry_type);

	playlist_name = g_file_get_basename (file);
	playlist = rb_auto_playlist_source_new (shell, playlist_name, FALSE);
	g_free (playlist_name);

	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (playlist), query,
					   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
					   NULL, 0);
	rb_generic_player_source_add_playlist (RB_GENERIC_PLAYER_SOURCE (source), shell, RB_SOURCE (playlist));
	rhythmdb_query_free (query);

	g_object_unref (shell);
	g_object_unref (db);
}


static void
rb_psp_source_create_playlists (RBGenericPlayerSource *source)
{
	GMount *mount;
	GFile *music_dir;

	g_object_get (source, "mount", &mount, NULL);
	music_dir = find_music_dir (mount);
	g_object_unref (mount);

	if (music_dir != NULL) {
		GFileEnumerator *e;
		GFileInfo *info;

		e = g_file_enumerate_children (music_dir, G_FILE_ATTRIBUTE_STANDARD_NAME","G_FILE_ATTRIBUTE_STANDARD_TYPE,
					       G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (e != NULL) {
			while ((info = g_file_enumerator_next_file (e, NULL, NULL)) != NULL) {
				GFile *file;
				const char *name;
				if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY) {
					g_object_unref (info);
					continue;
				}
				name = g_file_info_get_name (info);
				file = g_file_resolve_relative_path (music_dir, name);
				visit_playlist_dirs (RB_PSP_SOURCE (source), file);
				g_object_unref (file);
				g_object_unref (info);
			}
			g_object_unref (e);
		}
		g_object_unref (music_dir);
	}
}

gboolean
rb_psp_is_mount_player (GMount *mount, MPIDDevice *device_info)
{
	char *model;
	gboolean result = FALSE;

	g_object_get (device_info, "model", &model, NULL);
	if (model != NULL && (g_str_equal (model, "PSP") || g_str_equal (model, "\"PSP\" MS"))) {
		result = TRUE;
	}
	g_free (model);

	return result;
}

void
_rb_psp_source_register_type (GTypeModule *module)
{
	rb_psp_source_register_type (module);
}
