/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of song cut/paste handler object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtk/gtkstock.h>

#include "rb-shell-clipboard.h"
#include "rb-playlist-manager.h"
#include "rb-play-queue-source.h"
#include "rb-sourcelist-model.h"
#include "rhythmdb.h"
#include "rb-debug.h"

static void rb_shell_clipboard_class_init (RBShellClipboardClass *klass);
static void rb_shell_clipboard_init (RBShellClipboard *shell_clipboard);
static GObject *rb_shell_clipboard_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties);
static void rb_shell_clipboard_finalize (GObject *object);
static void rb_shell_clipboard_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_shell_clipboard_get_property (GObject *object,
				   	     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);
static void rb_shell_clipboard_sync (RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_select_all (GtkAction *action,
					       RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_select_none (GtkAction *action,
						RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_cut (GtkAction *action,
			                RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_copy (GtkAction *action,
			                 RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_paste (GtkAction *action,
			                  RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_delete (GtkAction *action,
					   RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_queue_delete (GtkAction *action,
						 RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_move_to_trash (GtkAction *action,
						  RBShellClipboard *clipboard);
static void rb_shell_clipboard_set (RBShellClipboard *clipboard,
			            GList *nodes);
static gboolean rb_shell_clipboard_idle_poll_deletions (RBShellClipboard *clipboard);
static void rb_shell_clipboard_playlist_added_cb (RBPlaylistManager *mgr,
						  RBPlaylistSource *source,
						  RBShellClipboard *clipboard);
static void rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBShellClipboard *clipboard);
static void rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
						     RBShellClipboard *clipboard);
static void rb_shell_clipboard_entries_changed_cb (RBEntryView *view,
						   gpointer stuff,
						   RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_add_to_playlist_new (GtkAction *action,
							RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_add_song_to_queue (GtkAction *action,
						      RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_song_info (GtkAction *action,
					      RBShellClipboard *clipboard);
static void rb_shell_clipboard_cmd_queue_song_info (GtkAction *action,
						    RBShellClipboard *clipboard);
static void rebuild_playlist_menu (RBShellClipboard *clipboard);
static gboolean rebuild_playlist_menu_idle (RBShellClipboard *clipboard);

struct RBShellClipboardPrivate
{
	RhythmDB *db;
	RBSource *source;
	RBStaticPlaylistSource *queue_source;
	RBPlaylistManager *playlist_manager;

	GtkUIManager *ui_mgr;
	GtkActionGroup *actiongroup;
	guint playlist_menu_ui_id;

	GHashTable *signal_hash;

	GAsyncQueue *deleted_queue;

	guint idle_deletion_id, idle_sync_id, idle_playlist_id;

	GList *entries;
};

#define RB_SHELL_CLIPBOARD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboardPrivate))

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_ACTION_GROUP,
	PROP_DB,
	PROP_QUEUE_SOURCE,
	PROP_PLAYLIST_MANAGER,
	PROP_UI_MANAGER,
};

static GtkActionEntry rb_shell_clipboard_actions [] =
{
	{ "EditSelectAll", NULL, N_("Select _All"), "<control>A",
	  N_("Select all songs"),
	  G_CALLBACK (rb_shell_clipboard_cmd_select_all) },
	{ "EditSelectNone", NULL, N_("D_eselect All"), "<shift><control>A",
	  N_("Deselect all songs"),
	  G_CALLBACK (rb_shell_clipboard_cmd_select_none) },
	{ "EditCut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X",
	  N_("Cut selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_cut) },
	{ "EditCopy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
	  N_("Copy selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_copy) },
	{ "EditPaste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V",
	  N_("Paste selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_paste) },
	{ "EditDelete", GTK_STOCK_REMOVE, N_("_Remove"), NULL,
	  N_("Remove selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_delete) },
	{ "EditMovetoTrash", GTK_STOCK_DELETE, N_("_Move to Trash"), NULL,
	  N_("Move selection to the trash"),
	  G_CALLBACK (rb_shell_clipboard_cmd_move_to_trash) },

	{ "EditPlaylistAdd", NULL, N_("Add to P_laylist") },
	{ "EditPlaylistAddNew", GTK_STOCK_ADD, N_("_New Playlist..."), NULL,
	  N_("Add the selected songs to a new playlist"),
	  G_CALLBACK (rb_shell_clipboard_cmd_add_to_playlist_new) },
	{ "AddToQueue", GTK_STOCK_ADD, N_("Add _to Play Queue"), NULL,
	  N_("Add the selected songs to the play queue"),
	  G_CALLBACK (rb_shell_clipboard_cmd_add_song_to_queue) },
	{ "QueueDelete", GTK_STOCK_REMOVE, N_("Remove"), NULL,
	  N_("Remove selection"),
	  G_CALLBACK (rb_shell_clipboard_cmd_queue_delete) },

	{ "MusicProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), "<Alt>Return",
	  N_("Show information on the selected song"),
	  G_CALLBACK (rb_shell_clipboard_cmd_song_info) },
	{ "QueueMusicProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Show information on the selected song"),
	  G_CALLBACK (rb_shell_clipboard_cmd_queue_song_info) },
};
static guint rb_shell_clipboard_n_actions = G_N_ELEMENTS (rb_shell_clipboard_actions);

static const char *playlist_menu_paths[] = {
	"/MenuBar/EditMenu/EditPlaylistAddMenu/EditPlaylistAddPlaceholder",
	"/BrowserSourceViewPopup/BrowserSourcePopupPlaylistAdd/BrowserSourcePopupPlaylistAddPlaceholder",
	"/PlaylistViewPopup/PlaylistPopupPlaylistAdd/PlaylistPopupPlaylistAddPlaceholder",
};
static guint num_playlist_menu_paths = G_N_ELEMENTS (playlist_menu_paths);

G_DEFINE_TYPE (RBShellClipboard, rb_shell_clipboard, G_TYPE_OBJECT)

static void
rb_shell_clipboard_class_init (RBShellClipboardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_shell_clipboard_finalize;
	object_class->constructor = rb_shell_clipboard_constructor;

	object_class->set_property = rb_shell_clipboard_set_property;
	object_class->get_property = rb_shell_clipboard_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_ACTION_GROUP,
					 g_param_spec_object ("action-group",
							      "GtkActionGroup",
							      "GtkActionGroup object",
							      GTK_TYPE_ACTION_GROUP,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_QUEUE_SOURCE,
					 g_param_spec_object ("queue-source",
						 	      "RBPlaylistSource",
							      "RBPlaylistSource object",
							      RB_TYPE_PLAYLIST_SOURCE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_MANAGER,
					 g_param_spec_object ("playlist-manager",
						 	      "RBPlaylistManager",
							      "RBPlaylistManager object",
							      RB_TYPE_PLAYLIST_MANAGER,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
						 	      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBShellClipboardPrivate));
}

static void
rb_shell_clipboard_init (RBShellClipboard *shell_clipboard)
{
	shell_clipboard->priv = RB_SHELL_CLIPBOARD_GET_PRIVATE (shell_clipboard);

	shell_clipboard->priv->signal_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								    NULL, g_free);

	shell_clipboard->priv->deleted_queue = g_async_queue_new ();

	shell_clipboard->priv->idle_deletion_id = g_idle_add ((GSourceFunc) rb_shell_clipboard_idle_poll_deletions, shell_clipboard);
}

static void
unset_source_internal (RBShellClipboard *clipboard)
{
	if (clipboard->priv->source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (clipboard->priv->source);

		if (songs) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
							      clipboard);
			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
							      clipboard);
		}
	}
	clipboard->priv->source = NULL;
}

static void
rb_shell_clipboard_finalize (GObject *object)
{
	RBShellClipboard *shell_clipboard;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (object));

	shell_clipboard = RB_SHELL_CLIPBOARD (object);

	g_return_if_fail (shell_clipboard->priv != NULL);

	/* release references to the source */
	unset_source_internal (shell_clipboard);

	g_hash_table_destroy (shell_clipboard->priv->signal_hash);

	g_list_foreach (shell_clipboard->priv->entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (shell_clipboard->priv->entries);

	g_async_queue_unref (shell_clipboard->priv->deleted_queue);

	if (shell_clipboard->priv->idle_sync_id)
		g_source_remove (shell_clipboard->priv->idle_sync_id);
	if (shell_clipboard->priv->idle_deletion_id)
		g_source_remove (shell_clipboard->priv->idle_deletion_id);
	if (shell_clipboard->priv->idle_playlist_id)
		g_source_remove (shell_clipboard->priv->idle_playlist_id);

	G_OBJECT_CLASS (rb_shell_clipboard_parent_class)->finalize (object);
}

static GObject *
rb_shell_clipboard_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBShellClipboard *clip;
	RBShellClipboardClass *klass;
	GObjectClass *parent_class;

	klass = RB_SHELL_CLIPBOARD_CLASS (g_type_class_peek (RB_TYPE_SHELL_CLIPBOARD));

	parent_class = G_OBJECT_CLASS (rb_shell_clipboard_parent_class);
	clip = RB_SHELL_CLIPBOARD (parent_class->constructor (type,
							      n_construct_properties,
							      construct_properties));

	g_signal_connect_object (G_OBJECT (clip->priv->db),
				 "entry_deleted",
				 G_CALLBACK (rb_shell_clipboard_entry_deleted_cb),
				 clip, 0);

	return G_OBJECT (clip);
}

static void
rb_shell_clipboard_set_source_internal (RBShellClipboard *clipboard,
					RBSource *source)
{
	unset_source_internal (clipboard);

	clipboard->priv->source = source;
	rb_debug ("selected source %p", source);

	rb_shell_clipboard_sync (clipboard);

	if (clipboard->priv->source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (clipboard->priv->source);

		if (songs) {
			g_signal_connect_object (G_OBJECT (songs),
						 "selection-changed",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (G_OBJECT (songs),
						 "entry-added",
						 G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (G_OBJECT (songs),
						 "entry-deleted",
						 G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (G_OBJECT (songs),
						 "entries-replaced",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
		}
	}

	rebuild_playlist_menu (clipboard);
}

static void
rb_shell_clipboard_set_property (GObject *object,
			         guint prop_id,
			         const GValue *value,
			         GParamSpec *pspec)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		rb_shell_clipboard_set_source_internal (clipboard, g_value_get_object (value));
		break;
	case PROP_ACTION_GROUP:
		clipboard->priv->actiongroup = g_value_get_object (value);
		gtk_action_group_add_actions (clipboard->priv->actiongroup,
					      rb_shell_clipboard_actions,
					      rb_shell_clipboard_n_actions,
					      clipboard);
		break;
	case PROP_DB:
		clipboard->priv->db = g_value_get_object (value);
		break;
	case PROP_UI_MANAGER:
		clipboard->priv->ui_mgr = g_value_get_object (value);
		break;
	case PROP_PLAYLIST_MANAGER:
		if (clipboard->priv->playlist_manager != NULL) {
			g_signal_handlers_disconnect_by_func (clipboard->priv->playlist_manager,
							      G_CALLBACK (rb_shell_clipboard_playlist_added_cb),
							      clipboard);
		}

		clipboard->priv->playlist_manager = g_value_get_object (value);
		if (clipboard->priv->playlist_manager != NULL) {
			g_signal_connect_object (G_OBJECT (clipboard->priv->playlist_manager),
						 "playlist-added", G_CALLBACK (rb_shell_clipboard_playlist_added_cb),
						 clipboard, 0);

			rebuild_playlist_menu (clipboard);
		}

		break;
	case PROP_QUEUE_SOURCE:
		if (clipboard->priv->queue_source != NULL) {
			RBEntryView *sidebar;
			g_object_get (clipboard->priv->queue_source, "sidebar", &sidebar, NULL);
			g_signal_handlers_disconnect_by_func (sidebar,
							      G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
							      clipboard);
			g_object_unref (sidebar);
		}

		clipboard->priv->queue_source = g_value_get_object (value);
		if (clipboard->priv->queue_source != NULL) {
			RBEntryView *sidebar;
			g_object_get (clipboard->priv->queue_source, "sidebar", &sidebar, NULL);
			g_signal_connect_object (G_OBJECT (sidebar), "selection-changed",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
			g_object_unref (sidebar);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_shell_clipboard_get_property (GObject *object,
			         guint prop_id,
			         GValue *value,
			         GParamSpec *pspec)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, clipboard->priv->source);
		break;
	case PROP_ACTION_GROUP:
		g_value_set_object (value, clipboard->priv->actiongroup);
		break;
	case PROP_DB:
		g_value_set_object (value, clipboard->priv->db);
		break;
	case PROP_UI_MANAGER:
		g_value_set_object (value, clipboard->priv->ui_mgr);
		break;
	case PROP_PLAYLIST_MANAGER:
		g_value_set_object (value, clipboard->priv->playlist_manager);
		break;
	case PROP_QUEUE_SOURCE:
		g_value_set_object (value, clipboard->priv->queue_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_clipboard_set_source (RBShellClipboard *clipboard,
			       RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (clipboard));
	if (source != NULL)
		g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (clipboard), "source", source, NULL);
}

RBShellClipboard *
rb_shell_clipboard_new (GtkActionGroup *actiongroup,
			GtkUIManager *ui_mgr,
			RhythmDB *db)
{
	return g_object_new (RB_TYPE_SHELL_CLIPBOARD,
			     "action-group", actiongroup,
			     "ui-manager", ui_mgr,
			     "db", db,
			     NULL);
}

static gboolean
rb_shell_clipboard_sync_idle (RBShellClipboard *clipboard)
{
	GDK_THREADS_ENTER ();
	rb_shell_clipboard_sync (clipboard);
	clipboard->priv->idle_sync_id = 0;
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	RBEntryView *view;
	gboolean have_selection = FALSE;
	gboolean have_sidebar_selection = FALSE;
	gboolean can_cut = FALSE;
	gboolean can_paste = FALSE;
	gboolean can_delete = FALSE;
	gboolean can_copy = FALSE;
	gboolean can_add_to_queue = FALSE;
	gboolean can_move_to_trash = FALSE;
	gboolean can_select_all = FALSE;
	gboolean can_show_properties = FALSE;
	GtkAction *action;
	RhythmDBEntryType entry_type;

	if (clipboard->priv->source) {
		view = rb_source_get_entry_view (clipboard->priv->source);
		if (view) {
			have_selection = rb_entry_view_have_selection (view);
			can_select_all = !rb_entry_view_have_complete_selection (view);
		}
	}

	if (clipboard->priv->queue_source) {
		RBEntryView *sidebar;
		g_object_get (clipboard->priv->queue_source, "sidebar", &sidebar, NULL);
		have_sidebar_selection = rb_entry_view_have_selection (sidebar);
		g_object_unref (sidebar);
	}

	rb_debug ("syncing clipboard");

	if (g_list_length (clipboard->priv->entries) > 0)
		can_paste = rb_source_can_paste (clipboard->priv->source);

	if (have_selection) {
		can_cut = rb_source_can_cut (clipboard->priv->source);
		can_delete = rb_source_can_delete (clipboard->priv->source);
		can_copy = rb_source_can_copy (clipboard->priv->source);
		can_move_to_trash = rb_source_can_move_to_trash (clipboard->priv->source);
		can_show_properties = rb_source_can_show_properties (clipboard->priv->source);

		if (clipboard->priv->queue_source)
			can_add_to_queue = rb_source_can_add_to_queue (clipboard->priv->source);
	}

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditCut");
	g_object_set (G_OBJECT (action), "sensitive", can_cut, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditDelete");
	g_object_set (G_OBJECT (action), "sensitive", can_delete, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditMovetoTrash");
	g_object_set (G_OBJECT (action), "sensitive", can_move_to_trash, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditCopy");
	g_object_set (G_OBJECT (action), "sensitive", can_copy, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup,"EditPaste");
	g_object_set (G_OBJECT (action), "sensitive", can_paste, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditPlaylistAdd");
	g_object_set (G_OBJECT (action), "sensitive", can_copy, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "AddToQueue");
	g_object_set (G_OBJECT (action), "sensitive", can_add_to_queue, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "MusicProperties");
	g_object_set (G_OBJECT (action), "sensitive", can_show_properties, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "QueueMusicProperties");
	g_object_set (G_OBJECT (action), "sensitive", have_sidebar_selection, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "QueueDelete");
	g_object_set (G_OBJECT (action), "sensitive", have_sidebar_selection, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditSelectAll");
	g_object_set (G_OBJECT (action), "sensitive", can_select_all, NULL);

	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditSelectNone");
	g_object_set (G_OBJECT (action), "sensitive", have_selection, NULL);

	/* disable the whole add-to-playlist menu if we can't add to a playlist
	 * FIXME: change this when we support non-library playlist adding
	 */
	action = gtk_action_group_get_action (clipboard->priv->actiongroup, "EditPlaylistAdd");
	if (clipboard->priv->source != NULL) {
		g_object_get (clipboard->priv->source, "entry-type", &entry_type, NULL);
		gtk_action_set_sensitive (action, (entry_type == RHYTHMDB_ENTRY_TYPE_SONG));
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	} else {
		gtk_action_set_sensitive (action, FALSE);
	}
}

static GtkWidget*
get_focussed_widget (RBShellClipboard *clipboard)
{
	GtkWidget *window;
	GtkWidget *widget;

	/* FIXME: this should be better */
	window = gtk_widget_get_toplevel (GTK_WIDGET (clipboard->priv->source));
	widget = gtk_window_get_focus (GTK_WINDOW (window));

	return widget;
}

static void
rb_shell_clipboard_cmd_select_all (GtkAction *action,
				   RBShellClipboard *clipboard)
{
	RBEntryView *entryview;
	GtkWidget *widget;

	rb_debug ("select all");
	widget = get_focussed_widget (clipboard);
	if (GTK_IS_EDITABLE (widget)) {
		gtk_editable_select_region (GTK_EDITABLE (widget), 0, -1);
	} else {
		/* select all tracks in the entry view */
		entryview = rb_source_get_entry_view (clipboard->priv->source);
		rb_entry_view_select_all (entryview);
	}
}

static void
rb_shell_clipboard_cmd_select_none (GtkAction *action,
				    RBShellClipboard *clipboard)
{
	RBEntryView *entryview;
	GtkWidget *widget;

	rb_debug ("select none");
	widget = get_focussed_widget (clipboard);
	if (GTK_IS_EDITABLE (widget)) {
		gtk_editable_select_region (GTK_EDITABLE (widget), -1, -1);
	} else {
		entryview = rb_source_get_entry_view (clipboard->priv->source);
		rb_entry_view_select_none (entryview);
	}
}

static void
rb_shell_clipboard_cmd_cut (GtkAction *action,
			    RBShellClipboard *clipboard)
{
	rb_debug ("cut");
	rb_shell_clipboard_set (clipboard,
				rb_source_cut (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_copy (GtkAction *action,
			     RBShellClipboard *clipboard)
{
	rb_debug ("copy");
	rb_shell_clipboard_set (clipboard,
				rb_source_copy (clipboard->priv->source));
}

static void
rb_shell_clipboard_cmd_paste (GtkAction *action,
			      RBShellClipboard *clipboard)
{
	rb_debug ("paste");
	rb_source_paste (clipboard->priv->source, clipboard->priv->entries);
}

static void
rb_shell_clipboard_cmd_delete (GtkAction *action,
	                       RBShellClipboard *clipboard)
{
	rb_debug ("delete");
	rb_source_delete (clipboard->priv->source);
}

static void
rb_shell_clipboard_cmd_queue_delete (GtkAction *action,
				     RBShellClipboard *clipboard)
{
	rb_debug ("delete");
	rb_play_queue_source_sidebar_delete (RB_PLAY_QUEUE_SOURCE (clipboard->priv->queue_source));
}

static void
rb_shell_clipboard_cmd_move_to_trash (GtkAction *action,
				      RBShellClipboard *clipboard)
{
	rb_debug ("movetotrash");
	rb_source_move_to_trash (clipboard->priv->source);
}

static void
rb_shell_clipboard_set (RBShellClipboard *clipboard,
			GList *entries)
{
	if (clipboard->priv->entries != NULL) {
		g_list_foreach (clipboard->priv->entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (clipboard->priv->entries);
	}

	clipboard->priv->entries = entries;
}

static gboolean
rb_shell_clipboard_process_deletions (RBShellClipboard *clipboard)
{
	RhythmDBEntry *entry;

	if (clipboard->priv->entries) {
		GList *tem, *finished = NULL;
		gboolean processed = FALSE;

		while ((entry = g_async_queue_try_pop (clipboard->priv->deleted_queue)) != NULL) {
			clipboard->priv->entries = g_list_remove (clipboard->priv->entries, entry);
			finished = g_list_prepend (finished, entry);
			processed = TRUE;
		}

		if (processed)
			rb_shell_clipboard_sync (clipboard);

		for (tem = finished; tem; tem = tem->next)
			rhythmdb_entry_unref (tem->data);
		g_list_free (finished);

		return processed;
	} else {
		/* Fast path for when there's nothing in the clipboard */
		while ((entry = g_async_queue_try_pop (clipboard->priv->deleted_queue)) != NULL)
			rhythmdb_entry_unref (entry);
		return FALSE;
	}
}

static gboolean
rb_shell_clipboard_idle_poll_deletions (RBShellClipboard *clipboard)
{
	gboolean did_sync;

	GDK_THREADS_ENTER ();

	did_sync = rb_shell_clipboard_process_deletions (clipboard);

	if (did_sync)
		clipboard->priv->idle_deletion_id =
			g_idle_add_full (G_PRIORITY_LOW,
					 (GSourceFunc) rb_shell_clipboard_idle_poll_deletions,
					 clipboard, NULL);
	else
		clipboard->priv->idle_deletion_id =
			g_timeout_add (300,
				       (GSourceFunc) rb_shell_clipboard_idle_poll_deletions,
				       clipboard);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
				     RhythmDBEntry *entry,
				     RBShellClipboard *clipboard)
{
	rhythmdb_entry_ref (entry);
	g_async_queue_push (clipboard->priv->deleted_queue, entry);
}

static void
rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
					 RBShellClipboard *clipboard)
{
	if (clipboard->priv->idle_sync_id == 0)
		clipboard->priv->idle_sync_id = g_idle_add ((GSourceFunc) rb_shell_clipboard_sync_idle,
							    clipboard);
	rb_debug ("entryview changed");
}

static void
rb_shell_clipboard_entries_changed_cb (RBEntryView *view,
				       gpointer stuff,
				       RBShellClipboard *clipboard)
{
	rb_debug ("entryview changed");
	if (clipboard->priv->idle_sync_id == 0)
		clipboard->priv->idle_sync_id = g_idle_add ((GSourceFunc) rb_shell_clipboard_sync_idle,
							    clipboard);
}

static void
rb_shell_clipboard_cmd_add_to_playlist_new (GtkAction *action,
					    RBShellClipboard *clipboard)
{
	GList *entries;
	RBSource *playlist_source;

	rb_debug ("add to new playlist");

	entries = rb_source_copy (clipboard->priv->source);
	playlist_source = rb_playlist_manager_new_playlist (clipboard->priv->playlist_manager,
							    NULL, FALSE);
	rb_source_paste (playlist_source, entries);

	g_list_foreach (entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (entries);
}

static void
rb_shell_clipboard_cmd_add_song_to_queue (GtkAction *action,
					  RBShellClipboard *clipboard)
{
	rb_debug ("add to queue");
	rb_source_add_to_queue (clipboard->priv->source,
				RB_SOURCE (clipboard->priv->queue_source));
}

static void
rb_shell_clipboard_cmd_song_info (GtkAction *action,
				  RBShellClipboard *clipboard)
{
	rb_debug ("song info");

	rb_source_song_properties (clipboard->priv->source);
}

static void
rb_shell_clipboard_cmd_queue_song_info (GtkAction *action,
					RBShellClipboard *clipboard)
{
	rb_debug ("song info");
	rb_play_queue_source_sidebar_song_info (RB_PLAY_QUEUE_SOURCE (clipboard->priv->queue_source));
}

static void
rb_shell_clipboard_playlist_add_cb (GtkAction *action,
				    RBShellClipboard *clipboard)
{
	RBSource *playlist_source;
	GList *entries;

	rb_debug ("add to exisintg playlist");
	playlist_source = g_object_get_data (G_OBJECT (action), "playlist-source");

	entries = rb_source_copy (clipboard->priv->source);
	rb_source_paste (playlist_source, entries);

	g_list_foreach (entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (entries);
}

static char *
generate_action_name (RBStaticPlaylistSource *source,
		      RBShellClipboard *clipboard)
{
	return g_strdup_printf ("AddToPlaylistClipboardAction%p", source);
}

static void
rb_shell_clipboard_playlist_deleted_cb (RBStaticPlaylistSource *source,
					RBShellClipboard *clipboard)
{
	char *action_name;
	GtkAction *action;

	/* first rebuild the menu */
	rebuild_playlist_menu (clipboard);

	/* then remove the 'add to playlist' action for the deleted playlist */
	action_name = generate_action_name (source, clipboard);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup, action_name);
	g_assert (action);
	gtk_action_group_remove_action (clipboard->priv->actiongroup, action);
	g_free (action_name);
}

static void
rb_shell_clipboard_playlist_renamed_cb (RBStaticPlaylistSource *source,
					GParamSpec *spec,
					RBShellClipboard *clipboard)
{
	char *name, *action_name;
	GtkAction *action;

	g_object_get (source, "name", &name, NULL);

	action_name = generate_action_name (source, clipboard);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup, action_name);
	g_assert (action);
	g_free (action_name);

	g_object_set (action, "label", name, NULL);
	g_free (name);
	g_object_unref (action);
}

static void
rb_shell_clipboard_playlist_visible_cb (RBStaticPlaylistSource *source,
					GParamSpec *spec,
					RBShellClipboard *clipboard)
{
	gboolean visible = FALSE;
	char *action_name;
	GtkAction *action;

	g_object_get (source, "visibility", &visible, NULL);

	action_name = generate_action_name (source, clipboard);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup, action_name);
	g_assert (action);
	g_free (action_name);

	gtk_action_set_visible (action, visible);
	g_object_unref (G_OBJECT (action));
}

static gboolean
add_playlist_to_menu (GtkTreeModel *model,
		      GtkTreePath *path,
		      GtkTreeIter *iter,
		      RBShellClipboard *clipboard)
{
	RhythmDBEntryType entry_type;
	RhythmDBEntryType source_entry_type;
	RBSource *source = NULL;
	char *action_name;
	GtkAction *action;
	int i;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

	if (source == NULL) {
		return FALSE;
	}

	if (!RB_IS_STATIC_PLAYLIST_SOURCE (source)) {
		g_object_unref (source);
		return FALSE;
	}

	/* FIXME: allow add-to-playlist for iPods and the like,
	 * based on the currently selected source
	 */
	entry_type = RHYTHMDB_ENTRY_TYPE_SONG;
	g_object_get (source, "entry-type", &source_entry_type, NULL);
	if (source_entry_type != entry_type) {
		g_object_unref (source);
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, source_entry_type);
		return FALSE;
	}

	action_name = generate_action_name (RB_STATIC_PLAYLIST_SOURCE (source), clipboard);
	action = gtk_action_group_get_action (clipboard->priv->actiongroup, action_name);
	if (action == NULL) {
		char *name;

		g_object_get (source, "name", &name, NULL);
		action = gtk_action_new (action_name, name, NULL, NULL);
		gtk_action_group_add_action (clipboard->priv->actiongroup, action);
		g_free (name);

		g_object_set_data (G_OBJECT (action), "playlist-source", source);
		g_signal_connect_object (G_OBJECT (action),
					 "activate", G_CALLBACK (rb_shell_clipboard_playlist_add_cb),
					 clipboard, 0);

		g_signal_connect_object (source,
					 "deleted", G_CALLBACK (rb_shell_clipboard_playlist_deleted_cb),
					 clipboard, 0);
		g_signal_connect_object (source,
					 "notify::name", G_CALLBACK (rb_shell_clipboard_playlist_renamed_cb),
					 clipboard, 0);
		g_signal_connect_object (source,
					 "notify::visibility", G_CALLBACK (rb_shell_clipboard_playlist_visible_cb),
					 clipboard, 0);
	}

	for (i = 0; i < num_playlist_menu_paths; i++) {
		gtk_ui_manager_add_ui (clipboard->priv->ui_mgr, clipboard->priv->playlist_menu_ui_id,
				       playlist_menu_paths[i],
				       action_name, action_name,
				       GTK_UI_MANAGER_AUTO, FALSE);
	}

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, source_entry_type);
	g_free (action_name);
	g_object_unref (source);

	return FALSE;
}

static void
rebuild_playlist_menu (RBShellClipboard *clipboard)
{
	GtkTreeModel *model = NULL;
	GObject *sourcelist = NULL;

	rb_debug ("rebuilding add-to-playlist menu");

	if (clipboard->priv->playlist_menu_ui_id != 0) {
		gtk_ui_manager_remove_ui (clipboard->priv->ui_mgr,
					  clipboard->priv->playlist_menu_ui_id);
	} else {
		clipboard->priv->playlist_menu_ui_id =
			gtk_ui_manager_new_merge_id (clipboard->priv->ui_mgr);
	}

	if (clipboard->priv->playlist_manager != NULL) {
		g_object_get (clipboard->priv->playlist_manager, "sourcelist", &sourcelist, NULL);
	}

	if (sourcelist != NULL) {
		g_object_get (sourcelist, "model", &model, NULL);
		g_object_unref (sourcelist);
	}

	if (model != NULL) {
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc)add_playlist_to_menu, clipboard);
		g_object_unref (model);
	}
}

static gboolean
rebuild_playlist_menu_idle (RBShellClipboard *clipboard)
{
	GDK_THREADS_ENTER ();
	rebuild_playlist_menu (clipboard);
	clipboard->priv->idle_playlist_id = 0;
	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
rb_shell_clipboard_playlist_added_cb (RBPlaylistManager *mgr,
				      RBPlaylistSource *source,
				      RBShellClipboard *clipboard)
{
	if (!RB_IS_STATIC_PLAYLIST_SOURCE (source))
		return;

	if (clipboard->priv->idle_playlist_id == 0) {
		clipboard->priv->idle_playlist_id =
			g_idle_add ((GSourceFunc)rebuild_playlist_menu_idle, clipboard);
	}
}
