/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of playlist source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <unistd.h>
#include <string.h>

#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <totem-pl-parser.h>

#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-playlist-source-recorder.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"

#include "rb-playlist-xml.h"
#include "rb-static-playlist-source.h"
#include "rb-auto-playlist-source.h"

static void rb_playlist_source_class_init (RBPlaylistSourceClass *klass);
static void rb_playlist_source_init (RBPlaylistSource *source);
static GObject *rb_playlist_source_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties);
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
static const char *impl_get_browser_key (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_move_to_trash (RBSource *asource);
static void impl_song_properties (RBSource *source);
static gboolean impl_show_popup (RBSource *source);
static GList *impl_get_ui_actions (RBSource *source);

static void rb_playlist_source_songs_show_popup_cb (RBEntryView *view, RBPlaylistSource *playlist_view);
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
					   RBEntryView *view);
static void rb_playlist_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
					       RBPlaylistSource *source);
static void rb_playlist_source_track_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						     GtkTreeModel *tree_model, GtkTreeIter *iter,
						     RBPlaylistSource *source);

#define PLAYLIST_SOURCE_SONGS_POPUP_PATH "/PlaylistViewPopup"
#define PLAYLIST_SOURCE_POPUP_PATH "/PlaylistSourcePopup"

struct RBPlaylistSourcePrivate
{
	RhythmDB *db;

	GHashTable *entries;

	RhythmDBEntryType entry_type;
	RhythmDBQueryModel *model;

	RBEntryView *songs;

	gboolean dirty;
	gboolean is_local;

	char *title;
};

#define RB_PLAYLIST_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSourcePrivate))

enum
{
	PROP_0,
	PROP_DB,
	PROP_DIRTY,
	PROP_LOCAL,
	PROP_ENTRY_TYPE
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
	object_class->constructor = rb_playlist_source_constructor;

	object_class->set_property = rb_playlist_source_set_property;
	object_class->get_property = rb_playlist_source_get_property;

	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_move_to_trash = impl_move_to_trash;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_ui_actions = impl_get_ui_actions;

	klass->impl_show_entry_view_popup = default_show_entry_view_popup;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "rhythmdb instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_DIRTY,
					 g_param_spec_boolean ("dirty",
							       "dirty",
							       "whether this playlist should be saved",
							       FALSE,
							       G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_LOCAL,
					 g_param_spec_boolean ("is-local",
							       "is-local",
							       "whether this playlist is attached to the local library",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_int ("entry-type",
							   "entry-type",
							   "The entry type this playlist accepts",
							   -1, G_MAXINT, -1,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBPlaylistSourcePrivate));
}

static void
rb_playlist_source_init (RBPlaylistSource *source)
{
	source->priv = RB_PLAYLIST_SOURCE_GET_PRIVATE (source);
}

static GObject *
rb_playlist_source_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	GObject *shell_player;
	RBPlaylistSource *source;
	RBPlaylistSourceClass *klass;
	RBShell *shell;

	klass = RB_PLAYLIST_SOURCE_CLASS (g_type_class_peek (RB_TYPE_PLAYLIST_SOURCE));

	source = RB_PLAYLIST_SOURCE (G_OBJECT_CLASS (rb_playlist_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (G_OBJECT (shell));

	source->priv->entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_signal_connect_object (G_OBJECT (source->priv->db), "entry_added",
				 G_CALLBACK (rb_playlist_source_entry_added_cb),
				 source, 0);

	source->priv->songs = rb_entry_view_new (source->priv->db, shell_player, 
					 	 NULL, TRUE, TRUE);

	rb_playlist_source_set_query_model (source, rhythmdb_query_model_new_empty (source->priv->db));

	{
		const char *title = _("Trac_k");
		const char *strings[3] = {0};

		GtkTreeViewColumn *column = gtk_tree_view_column_new ();
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
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
		rb_entry_view_append_column_custom (source->priv->songs, column, title,
						    "PlaylistTrack", NULL, 0);
	}

	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_YEAR);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_RATING);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LAST_PLAYED);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_FIRST_SEEN);
	rb_entry_view_set_columns_clickable (source->priv->songs, FALSE);

	rb_playlist_source_setup_entry_view (source, source->priv->songs);

	gtk_container_add (GTK_CONTAINER (source), GTK_WIDGET (source->priv->songs));
		
	gtk_widget_show_all (GTK_WIDGET (source));

	return G_OBJECT (source);
}

static void
rb_playlist_source_dispose (GObject *object)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);
	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
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

	switch (prop_id)
	{
	case PROP_LOCAL:
		source->priv->is_local = g_value_get_boolean (value);
		break;
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_int (value);
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

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, source->priv->db);
		break;
	case PROP_DIRTY:
		g_value_set_boolean (value, source->priv->dirty);
		break;
	case PROP_LOCAL:
		g_value_set_boolean (value, source->priv->is_local);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_int (value, source->priv->entry_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
default_show_entry_view_popup (RBPlaylistSource *source, RBEntryView *view)
{
	_rb_source_show_popup (RB_SOURCE (source), PLAYLIST_SOURCE_SONGS_POPUP_PATH);
}

static void
rb_playlist_source_songs_show_popup_cb (RBEntryView *view,
					RBPlaylistSource *source)
{
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);
	if (klass->impl_show_entry_view_popup)
		klass->impl_show_entry_view_popup (source, view);
}


static const char *
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
impl_move_to_trash (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_move_to_trash (source->priv->db,
				(RhythmDBEntry *) tem->data);
		rhythmdb_commit (source->priv->db);
	}
	g_list_free (sel);
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

#ifndef TOTEM_PL_PARSER_CHECK_VERSION
static void
playlist_iter_func (GtkTreeModel *model,
		    GtkTreeIter *iter,
		    char **uri,
		    char **title,
		    gpointer user_data)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	*uri = g_strdup (entry->location);
	*title = g_strdup (rb_refstring_get (entry->title));
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

	*uri = g_strdup (entry->location);
	*title = g_strdup (rb_refstring_get (entry->title));
	*custom_title = FALSE;
}
#endif /* TOTEM_PL_PARSER_CHECK_VERSION */

void
rb_playlist_source_save_playlist (RBPlaylistSource *source, 
				  const char *uri,
				  gboolean m3u_format)
{
	TotemPlParser *playlist;
	GError *error = NULL;
	char *name;

	rb_debug ("saving playlist");
	playlist = totem_pl_parser_new ();

	g_object_get (G_OBJECT (source), "name", &name, NULL);

	totem_pl_parser_write_with_title (playlist, GTK_TREE_MODEL (source->priv->model),
					  playlist_iter_func, uri, name, 
					  m3u_format ? TOTEM_PL_PARSER_M3U : TOTEM_PL_PARSER_PLS,
					  NULL, &error);
	g_free (name);
	if (error != NULL)
		rb_error_dialog (NULL, _("Couldn't save playlist"),
				 "%s", error->message);
}

static gboolean
burn_playlist_iter_func (GtkTreeModel *model, GtkTreeIter *iter, char **uri, char **artist, char **title, gulong *duration)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	*uri = g_strdup (entry->location);
	*artist = g_strdup (rb_refstring_get (entry->artist));
	*title = g_strdup (rb_refstring_get (entry->title));
	*duration = entry->duration;

	return TRUE;
}

void
rb_playlist_source_burn_playlist (RBPlaylistSource *source)
{
	GtkWidget *recorder;
	GtkWidget *parent;
	char *name;
	RBShell *shell;
	gboolean res;
	GError *error;

	/* don't burn if the playlist is empty */
	if (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->model), NULL) == 0)
		return;

	rb_debug ("burning playlist");

	g_object_get (source, "name", &name, "shell", &shell, NULL);

	parent = gtk_widget_get_toplevel (GTK_WIDGET (source));
	recorder = rb_playlist_source_recorder_new (parent,
						    shell,
						    name);
	g_object_unref (shell);
	g_free (name);

	error = NULL;
	res = rb_playlist_source_recorder_add_from_model (RB_PLAYLIST_SOURCE_RECORDER (recorder),
							  GTK_TREE_MODEL (source->priv->model),
							  burn_playlist_iter_func,
							  &error);
	if (! res) {
		rb_error_dialog (GTK_WINDOW (parent),
				 _("Unable to create audio CD"),
				 "%s", error->message);
		g_error_free (error);

		gtk_widget_destroy (recorder);

		return;
	}

        g_signal_connect (recorder,
			  "response",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	gtk_widget_show (recorder);
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

RBSource *
rb_playlist_source_new_from_xml	(RBShell *shell,
				 xmlNodePtr node)
{
	RBSource *source = NULL;
	xmlChar *tmp;
	xmlChar *name;

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
		g_object_unref (G_OBJECT (queue));
		return NULL;
	} else {
		g_warning ("attempting to load playlist '%s' of unknown type '%s'", name, tmp);
	}

	if (source)
		g_object_set (G_OBJECT (source), "name", name, NULL);

	g_free (name);

	return source;
}

void
rb_playlist_source_save_to_xml (RBPlaylistSource *source, xmlNodePtr parent_node)
{
	xmlNodePtr node;
	xmlChar *name;
	RBPlaylistSourceClass *klass = RB_PLAYLIST_SOURCE_GET_CLASS (source);

	node = xmlNewChild (parent_node, NULL, RB_PLAYLIST_PLAYLIST, NULL);
	g_object_get (G_OBJECT (source), "name", &name, NULL);
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
	RhythmDBEntry *entry = 
		rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (model),
							 path);
	g_hash_table_remove (source->priv->entries, entry->location);
}

static void
rb_playlist_source_entry_added_cb (RhythmDB *db, 
				   RhythmDBEntry *entry,
				   RBPlaylistSource *source)
{
	const char *location;

	location = entry->location;
	if (g_hash_table_lookup (source->priv->entries, location)) {
		rhythmdb_query_model_add_entry (source->priv->model, entry, -1);
		source->priv->dirty = TRUE;
	}
}

static void
rb_playlist_source_track_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model, GtkTreeIter *iter,
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

void
rb_playlist_source_setup_entry_view (RBPlaylistSource *source,
				     RBEntryView *entry_view)
{
	g_signal_connect_object (G_OBJECT (entry_view), "show_popup",
				 G_CALLBACK (rb_playlist_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (G_OBJECT (entry_view), "drag_data_received",
				 G_CALLBACK (rb_playlist_source_drop_cb), source, 0);
	gtk_drag_dest_set (GTK_WIDGET (entry_view), GTK_DEST_DEFAULT_ALL,
			   target_uri, G_N_ELEMENTS (target_uri), GDK_ACTION_COPY);
}

void
rb_playlist_source_set_query_model (RBPlaylistSource *source,
				    RhythmDBQueryModel *model)
{
	if (source->priv->model) {
		/* if the query model is replaced, the set of entries in
		 * the playlist will change, so we should mark the playlist dirty.
		 */
		source->priv->dirty = TRUE;
		g_signal_handlers_disconnect_by_func (G_OBJECT (source->priv->model),
						      G_CALLBACK (rb_playlist_source_row_deleted),
						      source);
		g_object_unref (source->priv->model);
	}
	source->priv->model = model;

	if (model) {
		g_object_ref (G_OBJECT (model));
		g_signal_connect_object (G_OBJECT (source->priv->model), "row_deleted",
					 G_CALLBACK (rb_playlist_source_row_deleted), source, 0);
	}

	rb_entry_view_set_model (source->priv->songs, RHYTHMDB_QUERY_MODEL (source->priv->model));
	
	g_object_set (G_OBJECT (source), "query-model", source->priv->model, NULL);
}

RhythmDB *
rb_playlist_source_get_db (RBPlaylistSource *source)
{
	return source->priv->db;
}

RhythmDBQueryModel *
rb_playlist_source_get_query_model (RBPlaylistSource *source)
{
	return source->priv->model;
}

void
rb_playlist_source_mark_dirty (RBPlaylistSource *source)
{
	source->priv->dirty = TRUE;
}

gboolean
rb_playlist_source_location_in_map (RBPlaylistSource *source,
				    const char *location)
{
	return (g_hash_table_lookup (source->priv->entries, location) != NULL);
}

gboolean
rb_playlist_source_add_to_map (RBPlaylistSource *source,
			       const char *location)
{
	if (g_hash_table_lookup (source->priv->entries, location)) {
		return FALSE;
	}

	g_hash_table_insert (source->priv->entries,
			     g_strdup (location), GINT_TO_POINTER (1));
	return TRUE;
}

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, "MusicPlaylistBurnPlaylist");

	return actions;
}


