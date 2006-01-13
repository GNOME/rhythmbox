/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2005 Jonathan Matthew <jonathan@kaolin.hn.org>
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
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>

#include "rb-play-queue-source.h"
#include "rb-playlist-xml.h"
#include "rb-song-info.h"
#include "rb-util.h"
#include "rb-debug.h"

static GObject *rb_play_queue_source_constructor (GType type, guint n_construct_properties,
						  GObjectConstructParam *construct_properties);
static void rb_play_queue_source_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rb_play_queue_source_track_info_cell_data_func (GtkTreeViewColumn *column,
							    GtkCellRenderer *renderer,
							    GtkTreeModel *tree_model,
							    GtkTreeIter *iter,
							    RBPlaylistSource *source);
static void rb_play_queue_sync_playing_state (GObject *entry_view,
					      GParamSpec *pspec,
					      RBPlayQueueSource *source);
static void rb_play_queue_source_row_inserted_cb (GtkTreeModel *model,
						  GtkTreePath *path,
						  GtkTreeIter *iter,
						  RBPlayQueueSource *source);
static void rb_play_queue_source_row_deleted_cb (GtkTreeModel *model,
						 GtkTreePath *path,
						 RBPlayQueueSource *source);
static void rb_play_queue_source_update_count (RBPlayQueueSource *source,
					       GtkTreeModel *model,
					       gint offset);
static void impl_show_entry_view_popup (RBPlaylistSource *source,
					RBEntryView *view);
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

#define PLAY_QUEUE_SOURCE_SONGS_POPUP_PATH "/QueuePlaylistViewPopup"
#define PLAY_QUEUE_SOURCE_SIDEBAR_POPUP_PATH "/QueueSidebarViewPopup"

typedef struct _RBPlayQueueSourcePrivate RBPlayQueueSourcePrivate;

struct _RBPlayQueueSourcePrivate
{
	RBEntryView *sidebar;
};

enum
{
	PROP_0,
	PROP_SIDEBAR
};

G_DEFINE_TYPE (RBPlayQueueSource, rb_play_queue_source, RB_TYPE_STATIC_PLAYLIST_SOURCE)
#define RB_PLAY_QUEUE_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSourcePrivate))

static void
rb_play_queue_source_class_init (RBPlayQueueSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructor = rb_play_queue_source_constructor;
	object_class->get_property = rb_play_queue_source_get_property;

	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_false_function;

	playlist_class->impl_show_entry_view_popup = impl_show_entry_view_popup;
	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;

	g_object_class_install_property (object_class,
					 PROP_SIDEBAR,
					 g_param_spec_object ("sidebar",
							      "sidebar",
							      "queue sidebar entry view",
							      RB_TYPE_ENTRY_VIEW,
							      G_PARAM_READABLE));

	g_type_class_add_private (klass, sizeof (RBPlayQueueSourcePrivate));
}

static void
rb_play_queue_source_init (RBPlayQueueSource *source)
{
}

static GObject *
rb_play_queue_source_constructor (GType type, guint n_construct_properties,
				  GObjectConstructParam *construct_properties)
{
	GObjectClass *parent_class = G_OBJECT_CLASS (rb_play_queue_source_parent_class);
	RBPlayQueueSource *source = RB_PLAY_QUEUE_SOURCE (
			parent_class->constructor (type, n_construct_properties, construct_properties));
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source); 
	GObject *shell_player;
	RBShell *shell;
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	RhythmDBQueryModel *model;

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (G_OBJECT (shell));
		
	priv->sidebar = rb_entry_view_new (db, shell_player, NULL, TRUE, TRUE);

	g_object_set (G_OBJECT (priv->sidebar), "vscrollbar-policy", GTK_POLICY_AUTOMATIC, NULL);

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc)
						 rb_play_queue_source_track_info_cell_data_func,
						 source, NULL);
	rb_entry_view_append_column_custom (priv->sidebar, column,
					    _("Queued songs"), "Title", NULL, NULL);
	rb_entry_view_set_columns_clickable (priv->sidebar, FALSE);
	rb_playlist_source_setup_entry_view (RB_PLAYLIST_SOURCE (source), priv->sidebar);

	model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (source));
	rb_entry_view_set_model (priv->sidebar, model);

	/* sync the state of the main entry view and the sidebar */
	g_signal_connect_object (G_OBJECT (rb_source_get_entry_view (RB_SOURCE (source))),
				 "notify::playing-state",
				 G_CALLBACK (rb_play_queue_sync_playing_state),
				 source, 0);

	/* update the queued song count when the query model changes */
	g_signal_connect_object (G_OBJECT (model), "row-inserted",
				 G_CALLBACK (rb_play_queue_source_row_inserted_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (model), "row-deleted",
				 G_CALLBACK (rb_play_queue_source_row_deleted_cb),
				 source, 0);

	rb_play_queue_source_update_count (source, GTK_TREE_MODEL (model), 0);
	
	return G_OBJECT (source);
}

static void
rb_play_queue_source_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SIDEBAR:
		g_value_set_object (value, priv->sidebar);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_play_queue_source_new (RBShell *shell)
{
	return RB_SOURCE (g_object_new (RB_TYPE_PLAY_QUEUE_SOURCE,
					"name", _("Queued songs"),
					"shell", shell,
					"is-local", TRUE,
					NULL));
}

void
rb_play_queue_source_sidebar_song_info (RBPlayQueueSource *source)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	GtkWidget *song_info = NULL;

	g_return_if_fail (priv->sidebar != NULL);

	song_info = rb_song_info_new (RB_SOURCE (source), priv->sidebar);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

void
rb_play_queue_source_sidebar_delete (RBPlayQueueSource *source)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	RBEntryView *sidebar = priv->sidebar;
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (sidebar);
	for (tem = sel; tem != NULL; tem = tem->next)
		rb_static_playlist_source_remove_entry (RB_STATIC_PLAYLIST_SOURCE (source),
							(RhythmDBEntry *) tem->data);
	g_list_free (sel);
}

static void
impl_show_entry_view_popup (RBPlaylistSource *source, RBEntryView *view)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	const char *popup = PLAY_QUEUE_SOURCE_SONGS_POPUP_PATH;
	if (view == priv->sidebar)
		popup = PLAY_QUEUE_SOURCE_SIDEBAR_POPUP_PATH;
	_rb_source_show_popup (RB_SOURCE (source), popup);
}

static void
rb_play_queue_source_track_info_cell_data_func (GtkTreeViewColumn *column,
						GtkCellRenderer *renderer,
						GtkTreeModel *tree_model,
						GtkTreeIter *iter,
						RBPlaylistSource *source)
{
	RhythmDBEntry *entry;
	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);
	const char *title;
	const char *artist;
	const char *album;
	char *markup;

	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	/* Translators: format is "<title> from <album> by <artist>" */
	markup = g_markup_printf_escaped("%s\n<span size=\"smaller\">%s <u>%s</u>\n%s <u>%s</u></span>",
				         title, _("from"), album, _("by"), artist);

	g_object_set (G_OBJECT (renderer), "markup", markup, NULL);
	g_free (markup);
}

static void 
rb_play_queue_sync_playing_state (GObject *entry_view,
				  GParamSpec *pspec,
				  RBPlayQueueSource *source)
{
	int state;
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	g_object_get (entry_view, "playing-state", &state, NULL);
	rb_entry_view_set_state (priv->sidebar, state);
}

static void 
rb_play_queue_source_row_inserted_cb (GtkTreeModel *model,
				      GtkTreePath *path,
				      GtkTreeIter *iter,
				      RBPlayQueueSource *source)
{
	rb_play_queue_source_update_count (source, model, 0);
}

static void 
rb_play_queue_source_row_deleted_cb (GtkTreeModel *model,
				     GtkTreePath *path,
				     RBPlayQueueSource *source)
{
	rb_play_queue_source_update_count (source, model, -1);
}

static void 
rb_play_queue_source_update_count (RBPlayQueueSource *source,
				   GtkTreeModel *model,
				   gint offset)
{
	gint count = gtk_tree_model_iter_n_children (model, NULL) + offset;
	char *name = _("Queued songs");

	if (count > 0)
		name = g_strdup_printf ("%s (%d)", name, count);

	g_object_set (G_OBJECT (source), "name", name, NULL);

	if (count > 0)
		g_free (name);
}

static void 
impl_save_contents_to_xml (RBPlaylistSource *source,
			   xmlNodePtr node)
{
	((RBPlaylistSourceClass*)rb_play_queue_source_parent_class)->impl_save_contents_to_xml (source, node);
	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_QUEUE);
}

