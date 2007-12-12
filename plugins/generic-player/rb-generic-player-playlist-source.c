/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007 Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include "rb-debug.h"
#include "rb-plugin.h"
#include "rb-file-helpers.h"

#define PLAYLIST_SAVE_TIMEOUT	1000

typedef struct
{
	char *playlist_path;
	char *device_root;
	gint save_playlist_id;
	RBGenericPlayerSource *player_source;
	gboolean loading;
} RBGenericPlayerPlaylistSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBGenericPlayerPlaylistSource,
		      rb_generic_player_playlist_source,
		      RB_TYPE_STATIC_PLAYLIST_SOURCE)

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE, RBGenericPlayerPlaylistSourcePrivate))

enum {
	PROP_0,
	PROP_PLAYLIST_PATH,
	PROP_DEVICE_ROOT,
	PROP_PLAYER_SOURCE
};


static void
rb_generic_player_playlist_source_save_to_xml (RBPlaylistSource *source, xmlNodePtr node)
{
	/* do nothing; just to prevent weirdness */
}

static void
save_playlist_entry (GtkTreeModel *model, GtkTreeIter *iter,
		     char **uri, char **title,
		     gboolean *custom_title,
		     RBGenericPlayerPlaylistSource *source)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDBEntry *entry;
	const char *host_uri;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model),
						    iter);
	if (entry == NULL) {
		return;
	}

	host_uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	*uri = rb_generic_player_source_uri_to_playlist_uri (priv->player_source, host_uri);
	*title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	*custom_title = TRUE;
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
	char *temp_uri;
	GError *error = NULL;
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	priv->save_playlist_id = 0;
	playlist_type = rb_generic_player_source_get_playlist_format (priv->player_source);

	g_object_get (source,
		      "name", &name,
		      "query-model", &query_model,
		      NULL);

	/* if we don't already have a name for this playlist, make one now */
	if (priv->playlist_path == NULL) {
		char *playlist_dir;
		char *mount_uri;
		char *filename;
		const char *ext;
		GnomeVFSURI *uri, *nuri;

		ext = playlist_format_extension (playlist_type);

		if (name == NULL || name[0] == '\0') {
			/* now what? */
			filename = g_strdup_printf ("unnamed%s", ext);
		} else {
			filename = g_strdup_printf ("%s%s", name, ext);
		}

		playlist_dir = rb_generic_player_source_get_playlist_path (priv->player_source);
		mount_uri = rb_generic_player_source_get_mount_path (priv->player_source);

		uri = gnome_vfs_uri_new (mount_uri);
		if (playlist_dir != NULL) {
			nuri = gnome_vfs_uri_append_path (uri, playlist_dir);
			gnome_vfs_uri_unref (uri);
			uri = nuri;
		}

		nuri = gnome_vfs_uri_append_path (uri, filename);
		gnome_vfs_uri_unref (uri);
		
		g_free (mount_uri);
		g_free (playlist_dir);

		priv->playlist_path = gnome_vfs_uri_to_string (nuri, GNOME_VFS_URI_HIDE_NONE);
		gnome_vfs_uri_unref (nuri);
	}

	temp_uri = g_strdup_printf ("%s%06X", priv->playlist_path, g_random_int_range (0, 0xFFFFFF));

	parser = totem_pl_parser_new ();
	if (rb_debug_matches ("totem_pl_parser_write_with_title", "totem-pl-parser.c")) {
		g_object_set (parser, "debug", TRUE, NULL);
	}
	if (totem_pl_parser_write_with_title (parser,
					      GTK_TREE_MODEL (query_model),
					      (TotemPlParserIterFunc) save_playlist_entry,
					      temp_uri,
					      name,
					      playlist_type,
					      source,
					      &error) == FALSE) {
		/* XXX report this more usefully */
		g_warning ("Playlist save failed: %s", error->message);
	} else {
		/* rename the new file over the old one */
		GnomeVFSResult result;

		result = gnome_vfs_move (temp_uri, priv->playlist_path, TRUE);
		if (result != GNOME_VFS_OK) {
			/* XXX report this more usefully */
			g_warning ("Replacing playlist failed: %s", gnome_vfs_result_to_string (result));
		}
	}

	g_clear_error (&error);
	g_free (name);
	g_free (temp_uri);
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
	char *path;

	if (priv->playlist_path == NULL) {
		/* this happens when we're creating a new playlist */
		rb_debug ("playlist has no path; obviously can't load it");
		g_object_set (source, "name", "", NULL);
		return TRUE;
	}

	priv->loading = TRUE;

	/* make a default name for the playlist based on the filename */
	path = g_filename_from_uri (priv->playlist_path, NULL, NULL);
	if (path != NULL) {
		char *name = g_path_get_basename (path);

		g_object_set (source, "name", name, NULL);
		g_free (name);
		g_free (path);
	}

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
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (parser), "recurse"))
		g_object_set (G_OBJECT (parser), "recurse", FALSE, NULL);

	switch (totem_pl_parser_parse_with_base (parser, priv->playlist_path, priv->device_root, FALSE)) {
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

	priv->loading = FALSE;
	return result;
}

static void
rb_generic_player_playlist_source_mark_dirty (RBPlaylistSource *source)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	if (priv->loading)
		return;

	/* save the playlist in a few seconds */
	if (priv->save_playlist_id != 0) {
		g_source_remove (priv->save_playlist_id);
	}

	priv->save_playlist_id = g_timeout_add (PLAYLIST_SAVE_TIMEOUT,
						(GSourceFunc) save_playlist,
						source);
}

RBSource *
rb_generic_player_playlist_source_new (RBShell *shell,
				       RBGenericPlayerSource *source,
				       const char *playlist_file,
				       const char *device_root,
				       RhythmDBEntryType entry_type)
{
	return RB_SOURCE (g_object_new (RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE,
					"shell", shell,
					"is-local", FALSE,
					"entry-type", entry_type,
					"source-group", RB_SOURCE_GROUP_DEVICES,
					"player-source", source,
					"playlist-path", playlist_file,
					"device-root", device_root,
					NULL));
}

void
rb_generic_player_playlist_delete_from_player (RBGenericPlayerPlaylistSource *source)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	if (priv->playlist_path != NULL) {
		GnomeVFSResult result;
		result = gnome_vfs_unlink (priv->playlist_path);

		if (result != GNOME_VFS_OK) {
			/* now what? */
		}
	} else {
		rb_debug ("playlist was never saved: nothing to delete");
	}
}

static void
rb_generic_player_playlist_source_init (RBGenericPlayerPlaylistSource *source)
{
}

static GObject *
rb_generic_player_playlist_source_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
	RBGenericPlayerPlaylistSource *source;

	source = RB_GENERIC_PLAYER_PLAYLIST_SOURCE (G_OBJECT_CLASS (rb_generic_player_playlist_source_parent_class) -> constructor (type, n_construct_properties, construct_properties));

	if (load_playlist (source) == FALSE) {
		rb_debug ("playlist didn't parse; killing the source");
		if (g_object_is_floating (source))
			g_object_ref_sink (source);
		g_object_unref (source);
		return NULL;
	}

	return G_OBJECT (source);
}

static void
rb_generic_player_playlist_source_dispose (GObject *object)
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
rb_generic_player_playlist_source_finalize (GObject *object)
{
	RBGenericPlayerPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	g_free (priv->playlist_path);

	G_OBJECT_CLASS (rb_generic_player_playlist_source_parent_class)->finalize (object);
}

static void
rb_generic_player_playlist_source_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
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
rb_generic_player_playlist_source_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
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

static gboolean
rb_generic_player_playlist_source_show_popup (RBSource *source)
{
	_rb_source_show_popup (source, "/GenericPlayerPlaylistSourcePopup");
	return TRUE;
}

static void
rb_generic_player_playlist_source_class_init (RBGenericPlayerPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructor = rb_generic_player_playlist_source_constructor;
	object_class->dispose = rb_generic_player_playlist_source_dispose;
	object_class->finalize = rb_generic_player_playlist_source_finalize;
	object_class->get_property = rb_generic_player_playlist_source_get_property;
	object_class->set_property = rb_generic_player_playlist_source_set_property;

	source_class->impl_show_popup = rb_generic_player_playlist_source_show_popup;

	playlist_class->impl_save_contents_to_xml = rb_generic_player_playlist_source_save_to_xml;
	playlist_class->impl_mark_dirty = rb_generic_player_playlist_source_mark_dirty;

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

