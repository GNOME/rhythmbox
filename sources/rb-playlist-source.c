/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <unistd.h>
#include <string.h>

#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <totem-pl-parser.h>

#include "rb-entry-view.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"

#include "rb-playlist-xml.h"
#include "rb-static-playlist-source.h"
#include "rb-auto-playlist-source.h"

#include "rb-playlist-manager.h"

/**
 * SECTION:rb-playlist-source
 * @short_description: Base class for playlist sources
 *
 * This class provides some common infrastructure for playlist
 * sources.  A playlist, in this context, is a persistent user-defined
 * selection from a set of songs.  Playlists (static and auto) based 
 * on the main library are saved to the playlists.xml file stored
 * alongside the rhythmdb.xml file.  Playlists on portable music players
 * are saved on the device in the format the player itself supports.
 *
 * This class provides most of the source UI (excluding the search bar),
 * holds some of the framework for loading and saving the playlists.xml
 * file, and records which playlists need to be saved.
 */

static void rb_playlist_source_class_init (RBPlaylistSourceClass *klass);
static void rb_playlist_source_init (RBPlaylistSource *source);
static void rb_playlist_source_constructed (GObject *object);
static void rb_playlist_source_dispose (GObject *object);
static void rb_playlist_source_finalize (GObject *object);
static void rb_playlist_source_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_playlist_source_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);

/* source methods */
static char *impl_get_browser_key (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_song_properties (RBSource *source);
static gboolean impl_show_popup (RBSource *source);

static void rb_playlist_source_songs_show_popup_cb (RBEntryView *view,
						    gboolean over_entry,
						    RBPlaylistSource *playlist_view);
static void rb_playlist_source_drop_cb (GtkWidget *widget,
				     GdkDragContext *context,
				     gint x,
				     gint y,
				     GtkSelectionData *data,
				     guint info,
				     guint time,
				     gpointer user_data);

static void rb_playlist_source_row_deleted (GtkTreeModel *model,
					    GtkTreePath *path,
					    RBPlaylistSource *playlist);
static void default_show_entry_view_popup (RBPlaylistSource *source,
					   RBEntryView *view,
					   gboolean over_entry);
static void rb_playlist_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
					       RBPlaylistSource *source);
static void rb_playlist_source_track_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     GtkTreeModel *tree_model, GtkTreeIter *iter,
						     RBPlaylistSource *source);
static void default_mark_dirty (RBPlaylistSource *source);
static void rb_playlist_source_songs_sort_order_changed_cb (RBEntryView *view,
						RBStaticPlaylistSource *source);
static char *rb_playlist_source_make_sorting_key (RBPlaylistSource *source);

static void remove_from_playlist_cmd (GtkAction *action, RBSource *source);
static char *impl_get_delete_action (RBSource *source);

#define CONF_STATE_SORTING_PREFIX CONF_PREFIX "/state/sorting/"
#define PLAYLIST_SOURCE_SONGS_POPUP_PATH "/PlaylistViewPopup"
#define PLAYLIST_SOURCE_POPUP_PATH "/PlaylistSourcePopup"

static GtkActionEntry rb_playlist_source_actions [] =
{
	{ "RemoveFromPlaylist", GTK_STOCK_REMOVE, N_("Remove From Playlist"), NULL,
	  N_("Remove each selected song from the playlist"),
	  G_CALLBACK (remove_from_playlist_cmd) },
};


struct RBPlaylistSourcePrivate
{
	RhythmDB *db;
	GtkActionGroup *action_group;

	GHashTable *entries;

	RhythmDBQueryModel *model;

	RBEntryView *songs;

	gboolean dirty;
	gboolean is_local;
	gboolean dispose_has_run;

	char *title;
	char *sorting_name;
};

#define RB_PLAYLIST_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSourcePrivate))

enum
{
	PROP_0,
	PROP_DB,
	PROP_DIRTY,
	PROP_LOCAL,
	PROP_SORTING_NAME
};

static const GtkTargetEntry target_uri [] = { { "text/uri-list", 0, 0 } };

G_DEFINE_ABSTRACT_TYPE (RBPlaylistSource, rb_playlist_source, RB_TYPE_SOURCE);

static void
rb_playlist_source_class_init (RBPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_playlist_source_dispose;
	object_class->finalize = rb_playlist_source_finalize;
	object_class->constructed = rb_playlist_source_constructed;

	object_class->set_property = rb_playlist_source_set_property;
	object_class->get_property = rb_playlist_source_get_property;

	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_delete_action = impl_get_delete_action;

	klass->impl_show_entry_view_popup = default_show_entry_view_popup;
	klass->impl_mark_dirty = default_mark_dirty;

	/**
	 * RBPlaylistSource:db:
	 *
	 * The #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "rhythmdb instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READABLE));
	/**
	 * RBPlaylistSource:dirty:
	 *
	 * Whether the playlist has been changed since it was last saved
	 * to disk.
	 */
	g_object_class_install_property (object_class,
					 PROP_DIRTY,
					 g_param_spec_boolean ("dirty",
							       "dirty",
							       "whether this playlist should be saved",
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * RBPlaylistSource:is-local:
	 *
	 * Whether the playlist is attached to the local library.
	 * Remote DAAP playlists, for example, are not local.
	 */
	g_object_class_install_property (object_class,
					 PROP_LOCAL,
					 g_param_spec_boolean ("is-local",
							       "is-local",
							       "whether this playlist is attached to the local library",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBPlaylistSource:sorting-name:
	 *
	 * A unique-ish name for the playlist, used to construct gconf keys
	 * to store information relating to the playlist.
	 */
	g_object_class_install_property (object_class,
					 PROP_SORTING_NAME,
					 g_param_spec_string ("sorting-name",
					 		      "sorting-name",
							      "globally unique name for storing sort-order",
					 		      NULL,
							      G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBPlaylistSourcePrivate));
}

static void
rb_playlist_source_init (RBPlaylistSource *source)
{
	source->priv = RB_PLAYLIST_SOURCE_GET_PRIVATE (source);
}

static void
rb_playlist_source_set_db (RBPlaylistSource *source,
			   RhythmDB         *db)
{
	if (source->priv->db != NULL) {
		g_signal_handlers_disconnect_by_func (source->priv->db,
						      rb_playlist_source_entry_added_cb,
						      source);
		g_object_unref (source->priv->db);

	}

	source->priv->db = db;

	if (source->priv->db != NULL) {
		g_object_ref (source->priv->db);
		g_signal_connect_object (G_OBJECT (source->priv->db), "entry_added",
					 G_CALLBACK (rb_playlist_source_entry_added_cb),
					 source, 0);
	}

}

static void
rb_playlist_source_constructed (GObject *object)
{
	GObject *shell_player;
	RBPlaylistSource *source;
	RBShell *shell;
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	char *sorting_key;

	RB_CHAIN_GOBJECT_METHOD (rb_playlist_source_parent_class, constructed, object);
	source = RB_PLAYLIST_SOURCE (object);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	shell_player = rb_shell_get_player (shell);
	rb_playlist_source_set_db (source, db);
	g_object_unref (db);

	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "PlaylistActions",
								       NULL, 0,
								       shell);
	_rb_action_group_add_source_actions (source->priv->action_group,
					     G_OBJECT (shell),
					     rb_playlist_source_actions,
					     G_N_ELEMENTS (rb_playlist_source_actions));

	g_object_unref (shell);

	source->priv->entries = g_hash_table_new_full (rb_refstring_hash, rb_refstring_equal,
						       (GDestroyNotify)rb_refstring_unref, NULL);

	/* If a sorting key is available at construction time, use it. This may be
	 * NULL in which case a key may be assigned later through a set property. */
	sorting_key = rb_playlist_source_make_sorting_key (source);
	source->priv->songs = rb_entry_view_new (source->priv->db,
						 shell_player,
					 	 sorting_key, TRUE, TRUE);
	g_free (sorting_key);

	g_signal_connect_object (source->priv->songs,
				 "sort-order-changed",
				 G_CALLBACK (rb_playlist_source_songs_sort_order_changed_cb),
				 source, 0);

	query_model = rhythmdb_query_model_new_empty (source->priv->db);
	rb_playlist_source_set_query_model (source, query_model);
	g_object_unref (query_model);

	{
		const char *title = "";
		const char *strings[3] = {0};

		GtkTreeViewColumn *column = gtk_tree_view_column_new ();
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

		g_object_set(renderer,
			     "style", PANGO_STYLE_OBLIQUE,
			     "weight", PANGO_WEIGHT_LIGHT,
			     "xalign", 1.0,
			     NULL);

		gtk_tree_view_column_pack_start (column, renderer, TRUE);

		gtk_tree_view_column_set_resizable (column, TRUE);
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

		strings[0] = title;
		strings[1] = "9999";
		rb_entry_view_set_fixed_column_width (source->priv->songs, column, renderer,
						      strings);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_playlist_source_track_cell_data_func,
							 source, NULL);
		rb_entry_view_insert_column_custom (source->priv->songs, column, title,
						    "PlaylistTrack", NULL, 0, NULL, 0);
	}

	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_YEAR, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION, FALSE);
 	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_QUALITY, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_COMMENT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LOCATION, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_FIRST_SEEN, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_BPM, FALSE);
	rb_entry_view_set_columns_clickable (source->priv->songs, FALSE);

	rb_playlist_source_setup_entry_view (source, source->priv->songs);

	gtk_container_add (GTK_CONTAINER (source), GTK_WIDGET (source->priv->songs));

	gtk_widget_show_all (GTK_WIDGET (source));
}

static void
rb_playlist_source_dispose (GObject *object)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	if (source->priv->dispose_has_run) {
		/* If dispose did already run, return. */
		rb_debug ("Dispose has already run for playlist source %p", object);
		return;
	}
	/* Make sure dispose does not run twice. */
	source->priv->dispose_has_run = TRUE;

	rb_debug ("Disposing playlist source %p", source);

	if (source->priv->db != NULL) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->model != NULL) {
		g_object_unref (source->priv->model);
		source->priv->model = NULL;
	}

	G_OBJECT_CLASS (rb_playlist_source_parent_class)->dispose (object);
}

static void
rb_playlist_source_finalize (GObject *object)
{
	RBPlaylistSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (object));

	source = RB_PLAYLIST_SOURCE (object);
	g_return_if_fail (source->priv != NULL);

	rb_debug ("Finalizing playlist source %p", source);

	g_hash_table_destroy (source->priv->entries);

	g_free (source->priv->title);
	source->priv = NULL;

	G_OBJECT_CLASS (rb_playlist_source_parent_class)->finalize (object);
}

static void
rb_playlist_source_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);
	char *sorting_key;

	switch (prop_id) {
	case PROP_LOCAL:
		source->priv->is_local = g_value_get_boolean (value);
		break;
	case PROP_SORTING_NAME:
		g_free (source->priv->sorting_name);
		source->priv->sorting_name = g_value_dup_string (value);

		/* propagate sorting key to the entry view */
		sorting_key = rb_playlist_source_make_sorting_key (source);
		g_object_set (source->priv->songs, "sort-key", sorting_key, NULL);
		g_free (sorting_key);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_source_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, source->priv->db);
		break;
	case PROP_DIRTY:
		g_value_set_boolean (value, source->priv->dirty);
		break;
	case PROP_LOCAL:
		g_value_set_boolean (value, source->priv->is_local);
		break;
	case PROP_SORTING_NAME:
		g_value_set_string (value, source->priv->sorting_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
default_show_entry_view_popup (RBPlaylistSource *source,
			       RBEntryView *view,
			       gboolean over_entry)
{
	if (over_entry)
		_rb_source_show_popup (RB_SOURCE (source), PLAYLIST_SOURCE_SONGS_POPUP_PATH);
}

static void
rb_playlist_source_songs_show_popup_cb (RBEntryView *view,
					gboolean over_entry,
					RBPlaylistSource *source)
{
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);
	if (klass->impl_show_entry_view_popup)
		klass->impl_show_entry_view_popup (source, view, over_entry);
}

static char *
impl_get_browser_key (RBSource *source)
{
	return NULL;
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	return source->priv->songs;
}

static void
impl_song_properties (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

	song_info = rb_song_info_new (asource, NULL);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static gboolean
impl_show_popup (RBSource *asource)
{
	_rb_source_show_popup (asource, PLAYLIST_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
rb_playlist_source_drop_cb (GtkWidget *widget,
			    GdkDragContext *context,
			    gint x,
			    gint y,
			    GtkSelectionData *data,
			    guint info,
			    guint time,
			    gpointer user_data)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (user_data);
	GtkTargetList *tlist;
	GdkAtom target;

	tlist = gtk_target_list_new (target_uri, G_N_ELEMENTS (target_uri));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	rb_source_receive_drag (RB_SOURCE (source), data);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

#if TOTEM_PL_PARSER_CHECK_VERSION(2,29,1)

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
playlist_iter_foreach (GtkTreeModel *model,
		       GtkTreePath *path,
		       GtkTreeIter *iter,
		       TotemPlPlaylist *playlist)
{
	TotemPlPlaylistIter pl_iter;
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	if (entry == NULL) {
		return FALSE;
	}

	totem_pl_playlist_append (playlist, &pl_iter);
	set_field_from_property (playlist, &pl_iter, entry, RHYTHMDB_PROP_LOCATION, TOTEM_PL_PARSER_FIELD_URI);
	set_field_from_property (playlist, &pl_iter, entry, RHYTHMDB_PROP_ARTIST, TOTEM_PL_PARSER_FIELD_AUTHOR);
	set_field_from_property (playlist, &pl_iter, entry, RHYTHMDB_PROP_GENRE, TOTEM_PL_PARSER_FIELD_GENRE);
	set_field_from_property (playlist, &pl_iter, entry, RHYTHMDB_PROP_ALBUM, TOTEM_PL_PARSER_FIELD_ALBUM);
	set_field_from_property (playlist, &pl_iter, entry, RHYTHMDB_PROP_TITLE, TOTEM_PL_PARSER_FIELD_TITLE);

	/* could possibly set duration, file size.. ? */

	return FALSE;
}


#else

static void
playlist_iter_func (GtkTreeModel *model,
		    GtkTreeIter *iter,
		    char **uri,
		    char **title,
		    gboolean *custom_title,
		    gpointer user_data)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	if (uri != NULL) {
		*uri = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
	}
	if (title != NULL) {
		*title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	}
	if (custom_title != NULL) {
		*custom_title = TRUE;
	}

	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}
}

#endif

/**
 * rb_playlist_source_save_playlist:
 * @source: a #RBPlaylistSource
 * @uri: destination URI
 * @export_type: format to save in
 *
 * Saves the playlist to an external file in a standard
 * format (M3U, PLS, or XSPF).
 */
void
rb_playlist_source_save_playlist (RBPlaylistSource *source,
				  const char *uri,
				  RBPlaylistExportType export_type)
{
	TotemPlParser *pl;
	GError *error = NULL;
	char *name;
	gint totem_format;
#if TOTEM_PL_PARSER_CHECK_VERSION(2,29,1)
	TotemPlPlaylist *playlist;
	GFile *file;
#endif

	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	rb_debug ("saving playlist");
	pl = totem_pl_parser_new ();

	g_object_get (source, "name", &name, NULL);

	switch (export_type) {
	case RB_PLAYLIST_EXPORT_TYPE_XSPF:
		totem_format = TOTEM_PL_PARSER_XSPF;
		break;
	case RB_PLAYLIST_EXPORT_TYPE_M3U:
		totem_format = TOTEM_PL_PARSER_M3U;
		break;
	case RB_PLAYLIST_EXPORT_TYPE_PLS:
	default:
		totem_format = TOTEM_PL_PARSER_PLS;
		break;
	}

#if TOTEM_PL_PARSER_CHECK_VERSION(2,29,1)
	file = g_file_new_for_uri (uri);
	playlist = totem_pl_playlist_new ();

	gtk_tree_model_foreach (GTK_TREE_MODEL (source->priv->model),
				(GtkTreeModelForeachFunc)playlist_iter_foreach,
				playlist);
	totem_pl_parser_save (pl, playlist, file, name, totem_format, &error);
	g_object_unref (playlist);
	g_object_unref (file);
#else
	totem_pl_parser_write_with_title (pl, GTK_TREE_MODEL (source->priv->model),
					  playlist_iter_func, uri, name,
					  totem_format,
					  NULL, &error);
#endif
	g_object_unref (pl);
	g_free (name);
	if (error != NULL) {
		rb_error_dialog (NULL, _("Couldn't save playlist"),
				 "%s", error->message);
		g_error_free (error);
	}
}

/* Adapted from yelp-toc-pager.c */
static xmlChar *
xml_get_and_trim_names (xmlNodePtr node)
{
	xmlNodePtr cur, keep = NULL;
	xmlChar *keep_lang = NULL;
	xmlChar *value;
	int j, keep_pri = INT_MAX;

	const gchar * const * langs = g_get_language_names ();

	value = NULL;

	for (cur = node->children; cur; cur = cur->next) {
		if (! xmlStrcmp (cur->name, RB_PLAYLIST_NAME)) {
			xmlChar *cur_lang = NULL;
			int cur_pri = INT_MAX;

			cur_lang = xmlNodeGetLang (cur);

			if (cur_lang) {
				for (j = 0; langs[j]; j++) {
					if (g_str_equal (cur_lang, langs[j])) {
						cur_pri = j;
						break;
					}
				}
			} else {
				cur_pri = INT_MAX - 1;
			}

			if (cur_pri <= keep_pri) {
				if (keep_lang)
					xmlFree (keep_lang);
				if (value)
					xmlFree (value);

				value = xmlNodeGetContent (cur);

				keep_lang = cur_lang;
				keep_pri = cur_pri;
				keep = cur;
			} else {
				if (cur_lang)
					xmlFree (cur_lang);
			}
		}
	}

	/* Delete all RB_PLAYLIST_NAME nodes */
	cur = node->children;
	while (cur) {
		xmlNodePtr this = cur;
		cur = cur->next;
		if (! xmlStrcmp (this->name, RB_PLAYLIST_NAME)) {
			xmlUnlinkNode (this);
			xmlFreeNode (this);
		}
	}

	return value;
}

static xmlChar *
get_playlist_name_from_xml (xmlNodePtr node)
{
	xmlChar *name;

	/* try to get and trim elements */
	name = xml_get_and_trim_names (node);

	if (name != NULL) {
		return name;
	}

	/* try the attribute */
	name = xmlGetProp (node, RB_PLAYLIST_NAME);

	return name;
}

/**
 * rb_playlist_source_new_from_xml:
 * @shell: the #RBShell instance
 * @node: libxml node containing the playlist
 *
 * Constructs a playlist source instance from the XML serialized
 * format.  This function knows about all the playlist types that
 * can be saved to disk, and it hands off the XML node to the
 * appropriate constructor based on the 'type' attribute of
 * the root node of the playlist.
 *
 * Return value: the playlist
 */
RBSource *
rb_playlist_source_new_from_xml	(RBShell *shell,
				 xmlNodePtr node)
{
	RBSource *source = NULL;
	xmlChar *tmp;
	xmlChar *name;

	g_return_val_if_fail (RB_IS_SHELL (shell), NULL);

	/* Try to get name from XML and remove translated names */
	name = get_playlist_name_from_xml (node);

	tmp = xmlGetProp (node, RB_PLAYLIST_TYPE);

	if (!xmlStrcmp (tmp, RB_PLAYLIST_AUTOMATIC))
		source = rb_auto_playlist_source_new_from_xml (shell, node);
	else if (!xmlStrcmp (tmp, RB_PLAYLIST_STATIC))
		source = rb_static_playlist_source_new_from_xml (shell, node);
	else if (!xmlStrcmp (tmp, RB_PLAYLIST_QUEUE)) {
		RBStaticPlaylistSource *queue;

		g_object_get (shell, "queue-source", &queue, NULL);
		rb_static_playlist_source_load_from_xml (queue, node);
		g_object_unref (queue);
	} else {
		g_warning ("attempting to load playlist '%s' of unknown type '%s'", name, tmp);
	}

	if (source != NULL) {
		g_object_set (G_OBJECT (source), "name", name, NULL);
	}

	xmlFree (name);
	xmlFree (tmp);

	return source;
}

/**
 * rb_playlist_source_save_to_xml:
 * @source: the playlist source to save
 * @parent_node: libxml node below which to save the playlist
 *
 * Converts the playlist to XML format, below the specified
 * parent node.
 */
void
rb_playlist_source_save_to_xml (RBPlaylistSource *source,
				xmlNodePtr parent_node)
{
	xmlNodePtr node;
	xmlChar *name;
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);

	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	node = xmlNewChild (parent_node, NULL, RB_PLAYLIST_PLAYLIST, NULL);
	g_object_get (source, "name", &name, NULL);
	xmlSetProp (node, RB_PLAYLIST_NAME, name);
	g_free (name);

	klass->impl_save_contents_to_xml (source, node);

	source->priv->dirty = FALSE;
}

static void
rb_playlist_source_row_deleted (GtkTreeModel *model,
				GtkTreePath *path,
				RBPlaylistSource *source)
{
	RhythmDBEntry *entry;
	RBRefString *location;

	entry = rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (model),
							 path);

	location = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_LOCATION);
	if (g_hash_table_remove (source->priv->entries, location))
		source->priv->dirty = TRUE;

	rb_refstring_unref (location);
	rhythmdb_entry_unref (entry);
}

static void
rb_playlist_source_entry_added_cb (RhythmDB *db,
				   RhythmDBEntry *entry,
				   RBPlaylistSource *source)
{
	RBRefString *location;

	location = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_LOCATION);

	if (g_hash_table_lookup (source->priv->entries, location)) {
		if (_rb_source_check_entry_type (RB_SOURCE (source), entry)) {
			rhythmdb_query_model_add_entry (source->priv->model, entry, -1);
			source->priv->dirty = TRUE;
		} else {
			g_hash_table_remove (source->priv->entries, location);
		}
	}

	rb_refstring_unref (location);
}

static void
rb_playlist_source_track_cell_data_func (GtkTreeViewColumn *column,
					 GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model,
					 GtkTreeIter *iter,
					 RBPlaylistSource *source)
{
	char *str;
	int val;

	gtk_tree_model_get (tree_model, iter, 1, &val, -1);

	if (val >= 0)
		str = g_strdup_printf ("%d", val);
	else
		str = g_strdup ("");

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);
}

/**
 * rb_playlist_source_setup_entry_view:
 * @source: the #RBPlaylistSource
 * @entry_view: the new #RBEntryView to set up
 *
 * Connects signal handlers and sets up drag and drop support for
 * an entry view to be used by a playlist source.  This only needs
 * to be called if the playlist subclass is creating a new entry view.
 */
void
rb_playlist_source_setup_entry_view (RBPlaylistSource *source,
				     RBEntryView *entry_view)
{
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	g_signal_connect_object (entry_view, "show_popup",
				 G_CALLBACK (rb_playlist_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (entry_view, "drag_data_received",
				 G_CALLBACK (rb_playlist_source_drop_cb), source, 0);
	gtk_drag_dest_set (GTK_WIDGET (entry_view),
			   GTK_DEST_DEFAULT_ALL,
			   target_uri,
			   G_N_ELEMENTS (target_uri),
			   GDK_ACTION_COPY);
}

/**
 * rb_playlist_source_set_query_model:
 * @source: the #RBPlaylistSource
 * @model: the new #RhythmDBQueryModel
 *
 * Sets a new query model for the playlist.  This updates the
 * entry view to use the new query model and also updates the
 * source query-model property.
 *
 * This needs to be called when the playlist subclass
 * creates a new query model.
 */
void
rb_playlist_source_set_query_model (RBPlaylistSource *source,
				    RhythmDBQueryModel *model)
{
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	if (source->priv->model != NULL) {
		/* if the query model is replaced, the set of entries in
		 * the playlist will change, so we should mark the playlist dirty.
		 */
		source->priv->dirty = TRUE;
		g_signal_handlers_disconnect_by_func (source->priv->model,
						      G_CALLBACK (rb_playlist_source_row_deleted),
						      source);
		g_object_unref (source->priv->model);
	}

	source->priv->model = model;

	if (source->priv->model != NULL) {
		g_object_ref (source->priv->model);
		g_signal_connect_object (source->priv->model, "row_deleted",
					 G_CALLBACK (rb_playlist_source_row_deleted), source, 0);
	}

	rb_entry_view_set_model (source->priv->songs, RHYTHMDB_QUERY_MODEL (source->priv->model));

	g_object_set (source, "query-model", source->priv->model, NULL);
}

/**
 * rb_playlist_source_get_db:
 * @source: a #RBPlaylistSource
 *
 * Returns the #RhythmDB instance.  The caller must not
 * unref the object once finished with it.
 *
 * Return value: the #RhythmDB instance
 */
RhythmDB *
rb_playlist_source_get_db (RBPlaylistSource *source)
{
	g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE (source), NULL);

	return source->priv->db;
}

/**
 * rb_playlist_source_get_query_model:
 * @source: a #RBPlaylistSource
 *
 * Returns the current #RhythmDBQueryModel for the playlist.
 * The caller must not unref the object once finished with it.
 *
 * Return value: the current #RhythmDBQueryModel
 */
RhythmDBQueryModel *
rb_playlist_source_get_query_model (RBPlaylistSource *source)
{
	g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE (source), NULL);

	return source->priv->model;
}

static void
default_mark_dirty (RBPlaylistSource *source)
{
	source->priv->dirty = TRUE;
}

/**
 * rb_playlist_source_mark_dirty:
 * @source: a #RBPlaylistSource
 *
 * Marks the playlist dirty.  This generally means that the playlist
 * will be saved to disk fairly soon, but the exact meaning can vary
 * between playlist types.
 */
void
rb_playlist_source_mark_dirty (RBPlaylistSource *source)
{
	RBPlaylistSourceClass *klass;
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);
	klass->impl_mark_dirty (source);
	g_object_notify (G_OBJECT (source), "dirty");
}

/**
 * rb_playlist_source_location_in_map:
 * @source: a #RBPlaylistSource
 * @location: a URI to check
 *
 * Returns TRUE if the specified URI is in the playlist entry map
 *
 * Return value: %TRUE if the URI is present
 */
gboolean
rb_playlist_source_location_in_map (RBPlaylistSource *source,
				    const char *location)
{
	RBRefString *refstr;
	gboolean found;

	g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE (source), FALSE);

	refstr = rb_refstring_find (location);
	if (refstr == NULL) {
		return FALSE;
	}

	found = (g_hash_table_lookup (source->priv->entries, refstr) != NULL);
	rb_refstring_unref (refstr);

	return found;
}

/**
 * rb_playlist_source_add_to_map:
 * @source: a #RBPlaylistSource
 * @location: a URI to add
 *
 * Adds a URI to the playlist's entry map.  This is useful when the
 * URI is being added to the database, but no entry exists for it yet.
 * When the entry is created, it will be added to the query model.
 *
 * Return value: TRUE if the URI was added to the entry map,
 *  FALSE if it was already there.
 */
gboolean
rb_playlist_source_add_to_map (RBPlaylistSource *source,
			       const char *location)
{
	RBRefString *refstr;

	g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE (source), FALSE);

	refstr = rb_refstring_new (location);
	if (g_hash_table_lookup (source->priv->entries, refstr)) {
		rb_refstring_unref (refstr);
		return FALSE;
	}

	g_hash_table_insert (source->priv->entries,
			     refstr, GINT_TO_POINTER (1));

	return TRUE;
}

static void
rb_playlist_source_songs_sort_order_changed_cb (RBEntryView *view,
						RBStaticPlaylistSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (view);
}

/* takes the 'sorting-name' property and produces a full GConf key for storing
 * the playlist's column sort order
 */
static char *
rb_playlist_source_make_sorting_key (RBPlaylistSource *source)
{
	char *sorting_name;
	char *sorting_key;
	char *sorting_key_tail;
	g_object_get (source, "sorting-name", &sorting_name, NULL);

	if (sorting_name && sorting_name[0] != '\0') {
		/* escape invalid characters in the key name */
		sorting_key_tail = gconf_escape_key (sorting_name, -1);

		/* bind column sort order to a full gconf key */
		sorting_key = g_strjoin (NULL, CONF_STATE_SORTING_PREFIX, 
					sorting_key_tail, NULL);

		g_free (sorting_key_tail);
	} else {
		sorting_key = NULL;
	}
	g_free (sorting_name);

	return sorting_key;
}

static void
remove_from_playlist_cmd (GtkAction *action, RBSource *source)
{
	rb_source_delete (source);
}

static char *
impl_get_delete_action (RBSource *source)
{
	return g_strdup ("RemoveFromPlaylist");
}
