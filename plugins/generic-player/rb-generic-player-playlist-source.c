/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007 Jonathan Matthew  <jonathan@d14n.org>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <totem-pl-parser.h>

#include "rb-generic-player-playlist-source.h"
#include "rb-generic-player-source.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#define PLAYLIST_SAVE_TIMEOUT	1

typedef struct
{
	char *playlist_path;		/* hmm, replace with GFile? */
	char *device_root;
	gint save_playlist_id;
	RBGenericPlayerSource *player_source;
	gboolean loading;
} RBGenericPlayerPlaylistSourcePrivate;

G_DEFINE_DYNAMIC_TYPE(RBGenericPlayerPlaylistSource,
		      rb_generic_player_playlist_source,
		      RB_TYPE_STATIC_PLAYLIST_SOURCE)

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE, RBGenericPlayerPlaylistSourcePrivate))

enum {
	PROP_0,
	PROP_PLAYLIST_PATH,
	PROP_DEVICE_ROOT,
	PROP_PLAYER_SOURCE
};

typedef struct {
	RBGenericPlayerPlaylistSource *source;
	TotemPlPlaylist *playlist;
	TotemPlParserType playlist_type;
} SavePlaylistData;


static void
impl_save_to_xml (RBPlaylistSource *source, xmlNodePtr node)
{
	/* do nothing; just to prevent weirdness */
}

static void
set_field_from_property (TotemPlPlaylist *playlist,
			 TotemPlPlaylistIter *iter,
			 RhythmDBEntry *entry,
			 RhythmDBPropType property,
			 const char *field)
{
	const char *value;

	value = rhythmdb_entry_get_string (entry, property);
	if (value != NULL) {
		totem_pl_playlist_set (playlist, iter, field, value, NULL);
	}
}

static gboolean
save_playlist_foreach (GtkTreeModel *model,
		       GtkTreePath *path,
		       GtkTreeIter *iter,
		       SavePlaylistData *data)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (data->source);
	RhythmDBEntry *entry;
	TotemPlPlaylistIter pl_iter;
	const char *host_uri;
	char *uri;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);
	if (entry == NULL) {
		return FALSE;
	}

	host_uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	uri = rb_generic_player_source_uri_to_playlist_uri (priv->player_source, host_uri, data->playlist_type);

	totem_pl_playlist_append (data->playlist, &pl_iter);
	totem_pl_playlist_set (data->playlist, &pl_iter, TOTEM_PL_PARSER_FIELD_URI, uri, NULL);
	set_field_from_property (data->playlist, &pl_iter, entry, RHYTHMDB_PROP_ARTIST, TOTEM_PL_PARSER_FIELD_AUTHOR);
	set_field_from_property (data->playlist, &pl_iter, entry, RHYTHMDB_PROP_GENRE, TOTEM_PL_PARSER_FIELD_GENRE);
	set_field_from_property (data->playlist, &pl_iter, entry, RHYTHMDB_PROP_ALBUM, TOTEM_PL_PARSER_FIELD_ALBUM);
	set_field_from_property (data->playlist, &pl_iter, entry, RHYTHMDB_PROP_TITLE, TOTEM_PL_PARSER_FIELD_TITLE);
	rhythmdb_entry_unref (entry);

	g_free (uri);
	return FALSE;
}

/* this probably belongs more in totem than here */
static const char *
playlist_format_extension (TotemPlParserType playlist_type)
{
	switch (playlist_type) {
	case TOTEM_PL_PARSER_PLS:
		return ".pls";
		break;
	case TOTEM_PL_PARSER_M3U:
	case TOTEM_PL_PARSER_M3U_DOS:
		return ".m3u";
		break;
	case TOTEM_PL_PARSER_IRIVER_PLA:
		return ".pla";
		break;
	case TOTEM_PL_PARSER_XSPF:
		return ".xspf";
		break;
	default:
		g_assert_not_reached ();
	}
}

static gboolean
save_playlist (RBGenericPlayerPlaylistSource *source)
{
	TotemPlParser *parser;
	TotemPlParserType playlist_type;
	RhythmDBQueryModel *query_model;
	char *name;
	char *temp_path;
	char *temp_path_uri;
	GError *error = NULL;
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	GFile *file;
	gboolean result;
	SavePlaylistData data;

	priv->save_playlist_id = 0;
	playlist_type = rb_generic_player_source_get_playlist_format (priv->player_source);

	g_object_get (source,
		      "name", &name,
		      "base-query-model", &query_model,
		      NULL);

	/* if we don't already have a name for this playlist, make one now */
	if (priv->playlist_path == NULL) {
		char *playlist_dir;
		char *mount_uri;
		char *filename;
		const char *ext;
		GFile *dir;
		GFile *playlist;

		ext = playlist_format_extension (playlist_type);

		if (name == NULL || name[0] == '\0') {
			/* now what? */
			filename = g_strdup_printf ("unnamed%s", ext);
		} else {
			filename = g_strdup_printf ("%s%s", name, ext);
		}

		playlist_dir = rb_generic_player_source_get_playlist_path (priv->player_source);
		mount_uri = rb_generic_player_source_get_mount_path (priv->player_source);

		dir = g_file_new_for_uri (mount_uri);
		if (playlist_dir != NULL) {
			GFile *pdir;

			pdir = g_file_resolve_relative_path (dir, playlist_dir);
			g_object_unref (dir);
			dir = pdir;
		}

		playlist = g_file_resolve_relative_path (dir, filename);
		priv->playlist_path = g_file_get_path (playlist);
		
		g_free (mount_uri);
		g_free (playlist_dir);

		g_object_unref (dir);
	}

	temp_path = g_strdup_printf ("%s%06X", priv->playlist_path, g_random_int_range (0, 0xFFFFFF));

	temp_path_uri = g_filename_to_uri (temp_path, NULL, &error);
	if (error) {
		g_warning ("Error converting filename to uri: %s", error->message);
		goto cleanup;
	}

	if (!rb_uri_create_parent_dirs (temp_path_uri, &error)) {
		g_warning ("Failed creating parent directory while saving playlist: %s", error->message);
		goto cleanup;
	}

	file = g_file_new_for_path (temp_path);

	parser = totem_pl_parser_new ();
	data.source = source;
	data.playlist_type = playlist_type;
	data.playlist = totem_pl_playlist_new ();

	gtk_tree_model_foreach (GTK_TREE_MODEL (query_model),
				(GtkTreeModelForeachFunc) save_playlist_foreach,
				&data);
	if (rb_debug_matches ("totem_pl_parser_save", "totem-pl-parser.c")) {
		g_object_set (parser, "debug", TRUE, NULL);
	}

	result = totem_pl_parser_save (parser, data.playlist, file, name, playlist_type, &error);
	g_object_unref (data.playlist);
	data.playlist = NULL;

	if (result == FALSE) {
		/* XXX report this more usefully */
		g_warning ("Playlist save failed: %s", error ? error->message : "<no error>");
	} else {
		GFile *dest;

		dest = g_file_new_for_path (priv->playlist_path);
		g_file_move (file, dest, G_FILE_COPY_OVERWRITE | G_FILE_COPY_NO_FALLBACK_FOR_MOVE, NULL, NULL, NULL, &error);
		if (error != NULL) {
			/* XXX report this more usefully */
			g_warning ("moving %s => %s failed: %s", temp_path, priv->playlist_path, error->message);
		}

		g_object_unref (dest);
	}

	g_object_unref (parser);
	g_object_unref (file);
cleanup:
	g_clear_error (&error);
	g_free (name);
	g_free (temp_path);
	g_free (temp_path_uri);
	g_object_unref (query_model);

	return FALSE;
}

static void
handle_playlist_start_cb (TotemPlParser *playlist,
			  const char *uri,
			  GHashTable *metadata,
			  RBGenericPlayerPlaylistSource *source)
{
	const char *title;
	title = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_TITLE);
	if (title != NULL) {
		rb_debug ("got name '%s' for playlist", title);
		g_object_set (source, "name", title, NULL);
	}
}

static void
handle_playlist_entry_cb (TotemPlParser *playlist,
			  const char *uri,
			  GHashTable *metadata,
			  RBGenericPlayerPlaylistSource *source)
{
	char *local_uri;
	char *name;
	char *canon_uri;
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	local_uri = rb_generic_player_source_uri_from_playlist_uri (priv->player_source, uri);
	if (local_uri == NULL)
		return;

	canon_uri = rb_canonicalise_uri (local_uri);

	g_object_get (source, "name", &name, NULL);
	rb_debug ("adding '%s' as '%s' to playlist '%s' (%s)", uri, canon_uri, name, priv->playlist_path);
	rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (source), canon_uri, -1);
	g_free (canon_uri);
	g_free (local_uri);
	g_free (name);
}

static gboolean
load_playlist (RBGenericPlayerPlaylistSource *source)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	TotemPlParser *parser;
	gboolean result;
	GFile *file;
	char *name;
	char *uri;

	if (priv->playlist_path == NULL) {
		/* this happens when we're creating a new playlist */
		rb_debug ("playlist has no path; obviously can't load it");
		g_object_set (source, "name", "", NULL);
		return TRUE;
	}

	priv->loading = TRUE;
	file = g_file_new_for_path (priv->playlist_path);

	/* make a default name for the playlist based on the filename */
	name = g_file_get_basename (file);
	g_object_set (source, "name", name, NULL);
	g_free (name);

	parser = totem_pl_parser_new ();
	if (rb_debug_matches ("totem_pl_parser_parse", "totem-pl-parser.c")) {
		g_object_set (parser, "debug", TRUE, NULL);
	}

	rb_generic_player_source_set_supported_formats (priv->player_source, parser);
	g_signal_connect (parser,
			  "entry-parsed", G_CALLBACK (handle_playlist_entry_cb),
			  source);
	g_signal_connect (parser,
			  "playlist-started", G_CALLBACK (handle_playlist_start_cb),
			  source);
	g_object_set (G_OBJECT (parser), "recurse", FALSE, NULL);

	uri = g_file_get_uri (file);
	switch (totem_pl_parser_parse_with_base (parser, uri, priv->device_root, FALSE)) {
	case TOTEM_PL_PARSER_RESULT_SUCCESS:
		rb_debug ("playlist parsed successfully");
		result = TRUE;
		break;

	case TOTEM_PL_PARSER_RESULT_ERROR:
		rb_debug ("playlist parser returned an error");
		result = FALSE;
		break;

	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
		rb_debug ("playlist parser didn't handle the file");
		result = FALSE;
		break;

	case TOTEM_PL_PARSER_RESULT_IGNORED:
		rb_debug ("playlist parser ignored the file");
		result = FALSE;
		break;
	default:
		g_assert_not_reached ();
	}
	g_free (uri);
	g_object_unref (file);

	priv->loading = FALSE;
	return result;
}

static void
impl_mark_dirty (RBPlaylistSource *source)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	if (priv->loading)
		return;

	/* save the playlist in a few seconds */
	if (priv->save_playlist_id != 0) {
		g_source_remove (priv->save_playlist_id);
	}

	priv->save_playlist_id = g_timeout_add_seconds (PLAYLIST_SAVE_TIMEOUT,
							(GSourceFunc) save_playlist,
							source);
}

RBSource *
rb_generic_player_playlist_source_new (RBShell *shell,
				       RBGenericPlayerSource *player_source,
				       const char *playlist_file,
				       const char *device_root,
				       RhythmDBEntryType *entry_type,
				       GMenuModel *playlist_menu)
{
	RBSource *source;
	source = RB_SOURCE (g_object_new (RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE,
					  "shell", shell,
					  "is-local", FALSE,
					  "entry-type", entry_type,
					  "player-source", player_source,
					  "playlist-path", playlist_file,
					  "device-root", device_root,
					  "playlist-menu", playlist_menu,
					  NULL));

	if (load_playlist (RB_GENERIC_PLAYER_PLAYLIST_SOURCE (source)) == FALSE) {
		rb_debug ("playlist didn't parse; killing the source");
		if (g_object_is_floating (source))
			g_object_ref_sink (source);
		g_object_unref (source);
		return NULL;
	}

	return source;
}

static gboolean
impl_can_remove (RBDisplayPage *page)
{
	/* maybe check if read only? */
	return TRUE;
}

static void
impl_remove (RBDisplayPage *page)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (page);

	if (priv->playlist_path != NULL) {
		GError *error = NULL;
		GFile *playlist;

		playlist = g_file_new_for_path (priv->playlist_path);
		g_file_delete (playlist, NULL, &error);
		if (error != NULL) {
			g_warning ("Deleting playlist %s failed: %s", priv->playlist_path, error->message);
			g_clear_error (&error);
		}
		g_object_unref (playlist);
	} else {
		rb_debug ("playlist was never saved: nothing to delete");
	}

	rb_display_page_delete_thyself (page);
}

static void
rb_generic_player_playlist_source_init (RBGenericPlayerPlaylistSource *source)
{
}

static void
impl_dispose (GObject *object)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	if (priv->save_playlist_id != 0) {
		g_source_remove (priv->save_playlist_id);
		save_playlist (RB_GENERIC_PLAYER_PLAYLIST_SOURCE (object));
	}

	if (priv->player_source != NULL) {
		g_object_unref (priv->player_source);
		priv->player_source = NULL;
	}

	G_OBJECT_CLASS (rb_generic_player_playlist_source_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	g_free (priv->playlist_path);

	G_OBJECT_CLASS (rb_generic_player_playlist_source_parent_class)->finalize (object);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PLAYER_SOURCE:
		g_value_set_object (value, priv->player_source);
		break;
	case PROP_PLAYLIST_PATH:
		g_value_set_string (value, priv->playlist_path);
		break;
	case PROP_DEVICE_ROOT:
		g_value_set_string (value, priv->device_root);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PLAYER_SOURCE:
		priv->player_source = RB_GENERIC_PLAYER_SOURCE (g_value_dup_object (value));
		break;
	case PROP_PLAYLIST_PATH:
		priv->playlist_path = g_value_dup_string (value);
		break;
	case PROP_DEVICE_ROOT:
		priv->device_root = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_generic_player_playlist_source_class_init (RBGenericPlayerPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;

	page_class->can_remove = impl_can_remove;
	page_class->remove = impl_remove;

	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;

	playlist_class->save_contents_to_xml = impl_save_to_xml;
	playlist_class->mark_dirty = impl_mark_dirty;

	g_object_class_install_property (object_class,
					 PROP_PLAYER_SOURCE,
					 g_param_spec_object ("player-source",
						 	      "player-source",
							      "player source",
							      RB_TYPE_GENERIC_PLAYER_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_PATH,
					 g_param_spec_string ("playlist-path",
						 	      "playlist-path",
							      "path to playlist file",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
					 PROP_DEVICE_ROOT,
					 g_param_spec_string ("device-root",
						 	      "device-root",
							      "path to root of the device",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (klass, sizeof (RBGenericPlayerPlaylistSourcePrivate));
}

static void
rb_generic_player_playlist_source_class_finalize (RBGenericPlayerPlaylistSourceClass *klass)
{
}

void
_rb_generic_player_playlist_source_register_type (GTypeModule *module)
{
	rb_generic_player_playlist_source_register_type (module);
}
