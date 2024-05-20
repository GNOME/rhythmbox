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

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>

#include "rb-entry-view.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-debug.h"
#include "rb-song-info.h"
#include "rb-builder-helpers.h"

#include "rb-playlist-xml.h"
#include "rb-static-playlist-source.h"
#include "rb-auto-playlist-source.h"

#include "rb-playlist-manager.h"
#include "rb-application.h"

/**
 * SECTION:rbplaylistsource
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
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_song_properties (RBSource *source);

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
static void rb_playlist_source_songs_sort_order_changed_cb (GObject *object,
							    GParamSpec *pspec,
							    RBPlaylistSource *source);

static char *impl_get_delete_label (RBSource *source);
static gboolean impl_can_remove (RBDisplayPage *page);
static void impl_remove (RBDisplayPage *page);

struct RBPlaylistSourcePrivate
{
	RhythmDB *db;

	GHashTable *entries;

	RhythmDBQueryModel *model;

	RBEntryView *songs;

	gboolean dirty;
	gboolean is_local;
	gboolean dispose_has_run;

	char *title;

	GMenu *popup;
};

#define RB_PLAYLIST_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSourcePrivate))

enum
{
	PROP_0,
	PROP_DB,
	PROP_DIRTY,
	PROP_LOCAL,
};

static const GtkTargetEntry target_uri [] = { { "text/uri-list", 0, 0 } };

static GSettingsBackend *playlist_settings_backend = NULL;

G_DEFINE_ABSTRACT_TYPE (RBPlaylistSource, rb_playlist_source, RB_TYPE_SOURCE);

static void
rb_playlist_source_class_init (RBPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	object_class->dispose = rb_playlist_source_dispose;
	object_class->finalize = rb_playlist_source_finalize;
	object_class->constructed = rb_playlist_source_constructed;
	object_class->set_property = rb_playlist_source_set_property;
	object_class->get_property = rb_playlist_source_get_property;

	source_class->get_entry_view = impl_get_entry_view;
	source_class->can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->song_properties = impl_song_properties;
	source_class->get_delete_label = impl_get_delete_label;

	page_class->can_remove = impl_can_remove;
	page_class->remove = impl_remove;

	klass->show_entry_view_popup = default_show_entry_view_popup;
	klass->mark_dirty = default_mark_dirty;

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

	g_type_class_add_private (klass, sizeof (RBPlaylistSourcePrivate));
}

static void
rb_playlist_source_init (RBPlaylistSource *source)
{
	source->priv = RB_PLAYLIST_SOURCE_GET_PRIVATE (source);
	
	if (playlist_settings_backend == NULL) {
		playlist_settings_backend = g_memory_settings_backend_new ();
	}
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
playlist_settings_changed_cb (GSettings *settings, char *key, RBPlaylistSource *source)
{
	rb_playlist_source_mark_dirty (source);
}

static void
rb_playlist_source_constructed (GObject *object)
{
	GObject *shell_player;
	RBPlaylistSource *source;
	RBShell *shell;
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	GtkBuilder *builder;
	GSettings *settings;

	RB_CHAIN_GOBJECT_METHOD (rb_playlist_source_parent_class, constructed, object);
	source = RB_PLAYLIST_SOURCE (object);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "db", &db,
		      "shell-player", &shell_player,
		      NULL);
	rb_playlist_source_set_db (source, db);
	g_object_unref (db);

	g_object_unref (shell);

	/* store playlist settings using the memory backend
	 * this means the settings path doesn't have to be consistent,
	 * it just has to be unique, so the address of the source object works.
	 * for local playlists, we write the settings into the playlist file on disk
	 * to make them persistent.
	 */
	g_object_get (source, "settings", &settings, NULL);
	if (settings == NULL) {
		char *path;
		path = g_strdup_printf ("/org/gnome/rhythmbox/playlist/%p/", source);
		settings = g_settings_new_with_backend_and_path ("org.gnome.rhythmbox.source",
								 playlist_settings_backend,
								 path);
		g_free (path);

		g_object_set (source, "settings", settings, NULL);
	}

	g_signal_connect (settings, "changed", G_CALLBACK (playlist_settings_changed_cb), source);
	g_object_unref (settings);

	builder = rb_builder_load ("playlist-popup.ui", NULL);
	source->priv->popup = G_MENU (gtk_builder_get_object (builder, "playlist-popup"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), source->priv->popup);
	g_object_ref (source->priv->popup);
	g_object_unref (builder);

	source->priv->entries = g_hash_table_new_full (rb_refstring_hash, rb_refstring_equal,
						       (GDestroyNotify)rb_refstring_unref, NULL);

	source->priv->songs = rb_entry_view_new (source->priv->db,
						 shell_player,
						 TRUE, TRUE);
	g_object_unref (shell_player);

	g_signal_connect_object (source->priv->songs,
				 "notify::sort-order",
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
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_COMPOSER, FALSE);
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

	switch (prop_id) {
	case PROP_LOCAL:
		source->priv->is_local = g_value_get_boolean (value);
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
	GtkWidget *menu;
	GMenuModel *playlist_menu;

	if (over_entry == FALSE)
		return;

	/* update add to playlist menu links */
	g_object_get (source, "playlist-menu", &playlist_menu, NULL);
	rb_menu_update_link (source->priv->popup, "rb-playlist-menu-link", playlist_menu);
	g_object_unref (playlist_menu);

	menu = gtk_menu_new_from_model (G_MENU_MODEL (source->priv->popup));
	gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (source), NULL);
	gtk_menu_popup (GTK_MENU (menu),
			NULL,
			NULL,
			NULL,
			NULL,
			3,
			gtk_get_current_event_time ());
}

static void
rb_playlist_source_songs_show_popup_cb (RBEntryView *view,
					gboolean over_entry,
					RBPlaylistSource *source)
{
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);
	if (klass->show_entry_view_popup)
		klass->show_entry_view_popup (source, view, over_entry);
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

	rb_display_page_receive_drag (RB_DISPLAY_PAGE (source), data);

	gtk_drag_finish (context, TRUE, FALSE, time);
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
	TotemPlPlaylist *playlist;
	GFile *file;

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

	file = g_file_new_for_uri (uri);
	playlist = totem_pl_playlist_new ();

	gtk_tree_model_foreach (GTK_TREE_MODEL (source->priv->model),
				(GtkTreeModelForeachFunc)playlist_iter_foreach,
				playlist);
	totem_pl_parser_save (pl, playlist, file, name, totem_format, &error);
	g_object_unref (playlist);
	g_object_unref (file);
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
	xmlNodePtr cur;
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

static void
apply_source_settings (RBPlaylistSource *source, xmlNodePtr node)
{
	GSettings *settings;
	xmlChar *value;

	g_object_get (source, "settings", &settings, NULL);
	if (settings == NULL)
		return;

	value = xmlGetProp (node, RB_PLAYLIST_SHOW_BROWSER);
	if (value != NULL) {
		g_settings_set_boolean (settings,
					"show-browser",
					(g_strcmp0 ((char *)value, "true") == 0));
		xmlFree (value);
	}
	
	value = xmlGetProp (node, RB_PLAYLIST_BROWSER_POSITION);
	if (value != NULL) {
		long position;
		char *end;

		position = strtol ((char *)value, &end, 10);
		if (end != (char *)value) {
			g_settings_set_int (settings, "paned-position", position);
		}
		xmlFree (value);
	}

	value = xmlGetProp (node, RB_PLAYLIST_SEARCH_TYPE);
	if (value != NULL) {
		g_settings_set_string (settings, "search-type", (char *)value);
		xmlFree (value);
	}

	g_object_unref (settings);
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
		source = rb_auto_playlist_source_new_from_xml (shell, (const char *)name, node);
	else if (!xmlStrcmp (tmp, RB_PLAYLIST_STATIC))
		source = rb_static_playlist_source_new_from_xml (shell, (const char *)name, node);
	else if (!xmlStrcmp (tmp, RB_PLAYLIST_QUEUE)) {
		RBStaticPlaylistSource *queue;

		g_object_get (shell, "queue-source", &queue, NULL);
		rb_static_playlist_source_load_from_xml (queue, node);
		apply_source_settings (RB_PLAYLIST_SOURCE (queue), node);
		g_object_unref (queue);
	} else {
		g_warning ("attempting to load playlist '%s' of unknown type '%s'", name, tmp);
	}

	xmlFree (name);
	xmlFree (tmp);

	if (source != NULL) {
		apply_source_settings (RB_PLAYLIST_SOURCE (source), node);
	}
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
	GSettings *settings;
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);

	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	node = xmlNewChild (parent_node, NULL, RB_PLAYLIST_PLAYLIST, NULL);
	g_object_get (source, "name", &name, NULL);
	xmlSetProp (node, RB_PLAYLIST_NAME, name);
	g_free (name);

	g_object_get (source, "settings", &settings, NULL);
	if (settings) {
		char *p;
		xmlSetProp (node,
			    RB_PLAYLIST_SHOW_BROWSER,
			    (xmlChar *)(g_settings_get_boolean (settings, "show-browser") ? "true" : "false"));

		p = g_strdup_printf ("%d", g_settings_get_int (settings, "paned-position"));
		xmlSetProp (node, RB_PLAYLIST_BROWSER_POSITION, (xmlChar *)p);
		g_free (p);

		xmlSetProp (node, RB_PLAYLIST_SEARCH_TYPE, (xmlChar *)g_settings_get_string (settings, "search-type"));
		g_object_unref (settings);
	}


	klass->save_contents_to_xml (source, node);

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
		if (rb_source_check_entry_type (RB_SOURCE (source), entry)) {
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
 * Return value: (transfer none): the #RhythmDB instance
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
 * Return value: (transfer none): the current #RhythmDBQueryModel
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
	klass->mark_dirty (source);
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
rb_playlist_source_songs_sort_order_changed_cb (GObject *object,
						GParamSpec *pspec,
						RBPlaylistSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static char *
impl_get_delete_label (RBSource *source)
{
	return g_strdup (_("Remove from Playlist"));
}

static gboolean
impl_can_remove (RBDisplayPage *page)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (page);
	return (source->priv->is_local);
}

void
impl_remove (RBDisplayPage *page)
{
	rb_display_page_delete_thyself (page);
}
