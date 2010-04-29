/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2005 Jonathan Matthew <jonathan@kaolin.hn.org>
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

#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-play-queue-source.h"
#include "rb-playlist-xml.h"
#include "rb-song-info.h"
#include "rb-stock-icons.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-play-order-queue.h"

/**
 * SECTION:rb-play-queue-source
 * @short_description: source object for the play queue
 *
 * The main interesting thing about this source is that is
 * contains a second #RBEntryView to be displayed in the side
 * pane (beneath the source list).  This entry view displays
 * the track title, artist, and album in a single column,
 * split across three lines so the information mostly fits in
 * the usual horizontal space allowed for the side bar.
 */


static void rb_play_queue_source_constructed (GObject *object);
static void rb_play_queue_source_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rb_play_queue_source_track_info_cell_data_func (GtkTreeViewColumn *column,
							    GtkCellRenderer *renderer,
							    GtkTreeModel *tree_model,
							    GtkTreeIter *iter,
							    RBPlaylistSource *source);
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
					RBEntryView *view,
					gboolean over_entry);
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);
static void rb_play_queue_source_cmd_clear (GtkAction *action,
					    RBPlayQueueSource *source);
static void rb_play_queue_source_cmd_shuffle (GtkAction *action,
					      RBPlayQueueSource *source);
static GList *impl_get_ui_actions (RBSource *source);
static gboolean impl_show_popup (RBSource *asource);

#define PLAY_QUEUE_SOURCE_SONGS_POPUP_PATH "/QueuePlaylistViewPopup"
#define PLAY_QUEUE_SOURCE_SIDEBAR_POPUP_PATH "/QueueSidebarViewPopup"
#define PLAY_QUEUE_SOURCE_POPUP_PATH "/QueueSourcePopup"

typedef struct _RBPlayQueueSourcePrivate RBPlayQueueSourcePrivate;

struct _RBPlayQueueSourcePrivate
{
	RBEntryView *sidebar;
	GtkTreeViewColumn *sidebar_column;
	GtkActionGroup *action_group;
	RBPlayOrder *queue_play_order;
};

enum
{
	PROP_0,
	PROP_SIDEBAR,
	PROP_PLAY_ORDER
};

G_DEFINE_TYPE (RBPlayQueueSource, rb_play_queue_source, RB_TYPE_STATIC_PLAYLIST_SOURCE)
#define RB_PLAY_QUEUE_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSourcePrivate))

static GtkActionEntry rb_play_queue_source_actions [] =
{
	{ "ClearQueue", GTK_STOCK_CLEAR, N_("Clear _Queue"), NULL,
	  N_("Remove all songs from the play queue"),
	  G_CALLBACK (rb_play_queue_source_cmd_clear) },
	{ "ShuffleQueue", GNOME_MEDIA_SHUFFLE, N_("Shuffle Queue"), NULL,
	  N_("Shuffle the tracks in the play queue"),
	  G_CALLBACK (rb_play_queue_source_cmd_shuffle) }
};

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
rb_play_queue_source_dispose (GObject *object)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (object);

	if (priv->action_group != NULL) {
		g_object_unref (priv->action_group);
		priv->action_group = NULL;
	}

	if (priv->queue_play_order != NULL) {
		g_object_unref (priv->queue_play_order);
		priv->queue_play_order = NULL;
	}

	G_OBJECT_CLASS (rb_play_queue_source_parent_class)->dispose (object);
}

static void
rb_play_queue_source_finalize (GObject *object)
{
	/* do nothing */

	G_OBJECT_CLASS (rb_play_queue_source_parent_class)->finalize (object);
}

static void
rb_play_queue_source_class_init (RBPlayQueueSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructed = rb_play_queue_source_constructed;
	object_class->get_property = rb_play_queue_source_get_property;
	object_class->finalize = rb_play_queue_source_finalize;
	object_class->dispose  = rb_play_queue_source_dispose;

	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_show_popup = impl_show_popup;

	playlist_class->impl_show_entry_view_popup = impl_show_entry_view_popup;
	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;

	/**
	 * RBPlayQueueSource:sidebar:
	 *
	 * The #RBEntryView for the play queue side pane.
	 */
	g_object_class_install_property (object_class,
					 PROP_SIDEBAR,
					 g_param_spec_object ("sidebar",
							      "sidebar",
							      "queue sidebar entry view",
							      RB_TYPE_ENTRY_VIEW,
							      G_PARAM_READABLE));

	/**
	 * RBPlayQueueSource:play-order:
	 *
	 * Overrides the play-order property from #RBSource
	 */
	g_object_class_override_property (object_class,
					  PROP_PLAY_ORDER,
					  "play-order");

	g_type_class_add_private (klass, sizeof (RBPlayQueueSourcePrivate));
}

static void
rb_play_queue_source_init (RBPlayQueueSource *source)
{
}

static void
rb_play_queue_source_constructed (GObject *object)
{
	RBPlayQueueSource *source;
	RBPlayQueueSourcePrivate *priv;
	GObject *shell_player;
	RBShell *shell;
	RhythmDB *db;
	GtkCellRenderer *renderer;
	RhythmDBQueryModel *model;
	GtkAction *action;

	RB_CHAIN_GOBJECT_METHOD (rb_play_queue_source_parent_class, constructed, object);

	source = RB_PLAY_QUEUE_SOURCE (object);
	priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));

	g_object_get (source, "shell", &shell, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (shell);

	priv->queue_play_order = rb_queue_play_order_new (RB_SHELL_PLAYER (shell_player));

	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "PlayQueueActions",
							       rb_play_queue_source_actions,
							       G_N_ELEMENTS (rb_play_queue_source_actions),
							       source);
	action = gtk_action_group_get_action (priv->action_group,
					      "ClearQueue");
	/* Translators: this is the toolbutton label for Clear Queue action */
	g_object_set (G_OBJECT (action), "short-label", _("Clear"), NULL);

	priv->sidebar = rb_entry_view_new (db, shell_player, NULL, TRUE, TRUE);

	g_object_set (G_OBJECT (priv->sidebar), "vscrollbar-policy", GTK_POLICY_AUTOMATIC, NULL);

	priv->sidebar_column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (priv->sidebar_column, renderer, TRUE);
	gtk_tree_view_column_set_sizing (priv->sidebar_column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_expand (priv->sidebar_column, TRUE);
	gtk_tree_view_column_set_clickable (priv->sidebar_column, FALSE);
	gtk_tree_view_column_set_cell_data_func (priv->sidebar_column, renderer,
						 (GtkTreeCellDataFunc)
						 rb_play_queue_source_track_info_cell_data_func,
						 source, NULL);
	rb_entry_view_append_column_custom (priv->sidebar, priv->sidebar_column,
					    _("Play Queue"), "Title", NULL, 0, NULL);
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
	case PROP_PLAY_ORDER:
		g_value_set_object (value, priv->queue_play_order);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_play_queue_source_new:
 * @shell: the #RBShell instance
 *
 * Creates the play queue source object.
 * 
 * Return value: the play queue source
 */
RBSource *
rb_play_queue_source_new (RBShell *shell)
{
	return RB_SOURCE (g_object_new (RB_TYPE_PLAY_QUEUE_SOURCE,
					"name", _("Play Queue"),
					"shell", shell,
					"is-local", TRUE,
					"entry-type", NULL,
					"source-group", RB_SOURCE_GROUP_LIBRARY,
					NULL));
}

/**
 * rb_play_queue_source_sidebar_song_info:
 * @source: the #RBPlayQueueSource
 *
 * Creates and displays a #RBSongInfo for the currently selected
 * entry in the side pane play queue view
 */
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

/**
 * rb_play_queue_source_sidebar_delete:
 * @source: the #RBPlayQueueSource
 *
 * Deletes the selected entries from the play queue side pane.
 * This is called by the #RBShellClipboard.
 */
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

/**
 * rb_play_queue_source_clear_queue:
 * @source: the #RBPlayQueueSource
 *
 * Clears the play queue.
 */
void
rb_play_queue_source_clear_queue (RBPlayQueueSource *source)
{
	GtkTreeIter iter;
	RhythmDBEntry *entry;
	RhythmDBQueryModel *model;

	model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (source));
	while (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter)) {
		entry = rhythmdb_query_model_iter_to_entry (model, &iter);

		if (entry != NULL) {
			rhythmdb_query_model_remove_entry (model, entry);
			rhythmdb_entry_unref (entry);
		}
	}
}

static void
impl_show_entry_view_popup (RBPlaylistSource *source,
			    RBEntryView *view,
			    gboolean over_entry)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	const char *popup = PLAY_QUEUE_SOURCE_SONGS_POPUP_PATH;
	if (view == priv->sidebar)
		popup = PLAY_QUEUE_SOURCE_SIDEBAR_POPUP_PATH;
	else if (!over_entry)
		return;
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
	const char *title;
	const char *artist;
	const char *album;
	char *markup;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	/* Translators: format is "<title> from <album> by <artist>" */
	markup = g_markup_printf_escaped ("%s\n<span size=\"smaller\">%s <i>%s</i>\n%s <i>%s</i></span>",
					  title, _("from"), album, _("by"), artist);

	g_object_set (G_OBJECT (renderer), "markup", markup, NULL);

	g_free (markup);
	rhythmdb_entry_unref (entry);
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
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	char *name = _("Play Queue");
	GtkAction *action;

	/* update source name */
	if (count > 0)
		name = g_strdup_printf ("%s (%d)", name, count);

	g_object_set (G_OBJECT (source), "name", name, NULL);
	gtk_tree_view_column_set_title (priv->sidebar_column, name);

	if (count > 0)
		g_free (name);

	/* make 'clear queue' and 'shuffle queue' actions sensitive when there are entries in the queue */
	action = gtk_action_group_get_action (priv->action_group,
					      "ClearQueue");
	g_object_set (G_OBJECT (action), "sensitive", (count > 0), NULL);

	action = gtk_action_group_get_action (priv->action_group, "ShuffleQueue");
	g_object_set (G_OBJECT (action), "sensitive", (count > 0), NULL);
}

static void
impl_save_contents_to_xml (RBPlaylistSource *source,
			   xmlNodePtr node)
{
	((RBPlaylistSourceClass*)rb_play_queue_source_parent_class)->impl_save_contents_to_xml (source, node);
	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_QUEUE);
}

static void
rb_play_queue_source_cmd_clear (GtkAction *action,
				RBPlayQueueSource *source)
{
	rb_play_queue_source_clear_queue (source);
}

static void
rb_play_queue_source_cmd_shuffle (GtkAction *action,
				  RBPlayQueueSource *source)
{
	RhythmDBQueryModel *model;

	model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (source));
	rhythmdb_query_model_shuffle_entries (model);
}

static gboolean
impl_show_popup (RBSource *asource)
{
	_rb_source_show_popup (asource, PLAY_QUEUE_SOURCE_POPUP_PATH);
	return TRUE;
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("ClearQueue"));
	return actions;
}

