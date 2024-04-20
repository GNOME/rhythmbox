/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

/**
 * SECTION:rbshellclipboard
 * @short_description: cut and paste handling
 *
 * The clipboard is primarily responsible for handling cut and paste actions,
 * as well as various other actions that relate to a selected set of entries
 * from a source, such as move to trash, delete, and add to playlist.
 *
 * It updates the sensitivity of the various actions it handles when the selected
 * source changes, and when the track list selection changes.  The actual action
 * handlers are fairly simple, mostly calling #RBSource methods.
 *
 * For the 'add to playlist' action, the clipboard builds a menu containing
 * the available playlists that entries can be added to from the current selected
 * source.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-shell-clipboard.h"
#include "rb-play-queue-source.h"
#include "rb-display-page-model.h"
#include "rhythmdb.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-application.h"
#include "rb-builder-helpers.h"
#include "rb-display-page-menu.h"

static void rb_shell_clipboard_class_init (RBShellClipboardClass *klass);
static void rb_shell_clipboard_init (RBShellClipboard *shell_clipboard);
static void rb_shell_clipboard_dispose (GObject *object);
static void rb_shell_clipboard_finalize (GObject *object);
static void rb_shell_clipboard_constructed (GObject *object);
static void rb_shell_clipboard_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_shell_clipboard_get_property (GObject *object,
				   	     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);
static void rb_shell_clipboard_sync (RBShellClipboard *clipboard);
static void rb_shell_clipboard_set (RBShellClipboard *clipboard,
			            GList *nodes);
static void rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
						 RhythmDBEntry *entry,
						 RBShellClipboard *clipboard);
static void rb_shell_clipboard_entryview_changed_cb (RBEntryView *view,
						     RBShellClipboard *clipboard);
static void rb_shell_clipboard_entries_changed_cb (RBEntryView *view,
						   gpointer stuff,
						   RBShellClipboard *clipboard);

static void playlist_menu_notify_cb (GObject *object, GParamSpec *pspec, RBShellClipboard *clipboard);

static void cut_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void copy_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void paste_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void select_all_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void select_none_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void add_to_queue_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void properties_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void delete_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void move_to_trash_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);



struct RBShellClipboardPrivate
{
	RhythmDB *db;
	RBSource *source;
	RBStaticPlaylistSource *queue_source;

	GHashTable *signal_hash;

	GAsyncQueue *deleted_queue;

	guint idle_sync_id, idle_playlist_id;

	GList *entries;

	GMenu *delete_menu;
	GMenu *edit_menu;
	GMenuModel *playlist_menu;
};

#define RB_SHELL_CLIPBOARD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_CLIPBOARD, RBShellClipboardPrivate))

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_DB,
	PROP_QUEUE_SOURCE,
};


G_DEFINE_TYPE (RBShellClipboard, rb_shell_clipboard, G_TYPE_OBJECT)

static void
rb_shell_clipboard_class_init (RBShellClipboardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_shell_clipboard_dispose;
	object_class->finalize = rb_shell_clipboard_finalize;
	object_class->constructed = rb_shell_clipboard_constructed;

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

	g_type_class_add_private (klass, sizeof (RBShellClipboardPrivate));
}

static void
rb_shell_clipboard_init (RBShellClipboard *shell_clipboard)
{
	shell_clipboard->priv = RB_SHELL_CLIPBOARD_GET_PRIVATE (shell_clipboard);

	shell_clipboard->priv->signal_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								    NULL, g_free);

	shell_clipboard->priv->deleted_queue = g_async_queue_new ();
}

static void
unset_source_internal (RBShellClipboard *clipboard)
{
	if (clipboard->priv->source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (clipboard->priv->source);

		if (songs) {
			g_signal_handlers_disconnect_by_func (songs,
							      G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
							      clipboard);
			g_signal_handlers_disconnect_by_func (songs,
							      G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
							      clipboard);
		}

		g_signal_handlers_disconnect_by_func (clipboard->priv->source, G_CALLBACK (playlist_menu_notify_cb), clipboard);
	}
	clipboard->priv->source = NULL;
}

static void
rb_shell_clipboard_dispose (GObject *object)
{
	RBShellClipboard *shell_clipboard;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (object));

	shell_clipboard = RB_SHELL_CLIPBOARD (object);

	g_return_if_fail (shell_clipboard->priv != NULL);

	unset_source_internal (shell_clipboard);
	g_clear_object (&shell_clipboard->priv->playlist_menu);

	if (shell_clipboard->priv->idle_sync_id != 0) {
		g_source_remove (shell_clipboard->priv->idle_sync_id);
		shell_clipboard->priv->idle_sync_id = 0;
	}
	if (shell_clipboard->priv->idle_playlist_id != 0) {
		g_source_remove (shell_clipboard->priv->idle_playlist_id);
		shell_clipboard->priv->idle_playlist_id = 0;
	}

	G_OBJECT_CLASS (rb_shell_clipboard_parent_class)->dispose (object);
}

static void
rb_shell_clipboard_finalize (GObject *object)
{
	RBShellClipboard *shell_clipboard;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (object));

	shell_clipboard = RB_SHELL_CLIPBOARD (object);

	g_return_if_fail (shell_clipboard->priv != NULL);

	g_hash_table_destroy (shell_clipboard->priv->signal_hash);

	g_list_foreach (shell_clipboard->priv->entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (shell_clipboard->priv->entries);

	g_async_queue_unref (shell_clipboard->priv->deleted_queue);

	G_OBJECT_CLASS (rb_shell_clipboard_parent_class)->finalize (object);
}

static void
add_delete_menu_item (RBShellClipboard *clipboard)
{
	char *label;

	if (clipboard->priv->source) {
		label = rb_source_get_delete_label (clipboard->priv->source);
	} else {
		label = g_strdup (_("Remove"));
	}

	if (g_menu_model_get_n_items (G_MENU_MODEL (clipboard->priv->delete_menu)) > 0) {
		g_menu_remove (clipboard->priv->delete_menu, 0);
	}
	g_menu_append (clipboard->priv->delete_menu, label, "app.clipboard-delete");
	g_free (label);
}

static void
setup_add_to_playlist_menu (RBShellClipboard *clipboard)
{
	GMenuModel *new_menu = NULL;

	if (clipboard->priv->source) {
		g_object_get (clipboard->priv->source, "playlist-menu", &new_menu, NULL);
	}
	if (new_menu == clipboard->priv->playlist_menu) {
		g_clear_object (&new_menu);
		return;
	}
	g_clear_object (&clipboard->priv->playlist_menu);
	clipboard->priv->playlist_menu = new_menu;

	if (clipboard->priv->playlist_menu) {
		rb_menu_update_link (clipboard->priv->edit_menu,
				     "rb-playlist-menu-link",
				     G_MENU_MODEL (clipboard->priv->playlist_menu));
	} else {
		rb_menu_update_link (clipboard->priv->edit_menu, "rb-playlist-menu-link", NULL);
	}
}

static void
playlist_menu_notify_cb (GObject *object, GParamSpec *pspec, RBShellClipboard *clipboard)
{
	setup_add_to_playlist_menu (clipboard);
}

static void
rb_shell_clipboard_constructed (GObject *object)
{
	RBApplication *app;
	RBShellClipboard *clipboard;
	GtkBuilder *builder;
	GActionEntry actions[] = {
		{ "clipboard-cut", cut_action_cb },
		{ "clipboard-copy", copy_action_cb },
		{ "clipboard-paste", paste_action_cb },
		{ "clipboard-select-all", select_all_action_cb },
		{ "clipboard-select-none", select_none_action_cb },
		{ "clipboard-add-to-queue", add_to_queue_action_cb },
		{ "clipboard-properties", properties_action_cb },
		{ "clipboard-delete", delete_action_cb },
		{ "clipboard-trash", move_to_trash_action_cb },
	};

	RB_CHAIN_GOBJECT_METHOD (rb_shell_clipboard_parent_class, constructed, object);
	clipboard = RB_SHELL_CLIPBOARD (object);

	g_signal_connect_object (clipboard->priv->db,
				 "entry_deleted",
				 G_CALLBACK (rb_shell_clipboard_entry_deleted_cb),
				 clipboard, 0);

	g_action_map_add_action_entries (G_ACTION_MAP (g_application_get_default ()),
					 actions,
					 G_N_ELEMENTS (actions),
					 clipboard);

	app = RB_APPLICATION (g_application_get_default ());

	clipboard->priv->delete_menu = g_menu_new ();
	add_delete_menu_item (clipboard);
	rb_application_add_shared_menu (app, "delete-menu", G_MENU_MODEL (clipboard->priv->delete_menu));

	builder = rb_builder_load ("edit-menu.ui", NULL);
	clipboard->priv->edit_menu = G_MENU (gtk_builder_get_object (builder, "edit-menu"));
	rb_application_link_shared_menus (app, clipboard->priv->edit_menu);
	rb_application_add_shared_menu (app, "edit-menu", G_MENU_MODEL (clipboard->priv->edit_menu));
	g_object_unref (builder);
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
			g_signal_connect_object (songs,
						 "selection-changed",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (songs,
						 "entry-added",
						 G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (songs,
						 "entry-deleted",
						 G_CALLBACK (rb_shell_clipboard_entries_changed_cb),
						 clipboard, 0);
			g_signal_connect_object (songs,
						 "entries-replaced",
						 G_CALLBACK (rb_shell_clipboard_entryview_changed_cb),
						 clipboard, 0);
		}

		g_signal_connect (clipboard->priv->source,
				  "notify::playlist-menu",
				  G_CALLBACK (playlist_menu_notify_cb),
				  clipboard);
	}

	add_delete_menu_item (clipboard);

	setup_add_to_playlist_menu (clipboard);
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
	case PROP_DB:
		clipboard->priv->db = g_value_get_object (value);
		break;
	case PROP_QUEUE_SOURCE:
		clipboard->priv->queue_source = g_value_get_object (value);
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
	case PROP_DB:
		g_value_set_object (value, clipboard->priv->db);
		break;
	case PROP_QUEUE_SOURCE:
		g_value_set_object (value, clipboard->priv->queue_source);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_shell_clipboard_set_source:
 * @clipboard: the #RBShellClipboard
 * @source: the new selected #RBSource
 *
 * Updates the clipboard to reflect a newly selected source.
 */
void
rb_shell_clipboard_set_source (RBShellClipboard *clipboard,
			       RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_CLIPBOARD (clipboard));
	if (source != NULL)
		g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (clipboard), "source", source, NULL);
}

/**
 * rb_shell_clipboard_new:
 * @db: the #RhythmDB instance
 *
 * Creates the #RBShellClipboard instance
 *
 * Return value: the #RBShellClipboard
 */
RBShellClipboard *
rb_shell_clipboard_new (RhythmDB *db)
{
	return g_object_new (RB_TYPE_SHELL_CLIPBOARD,
			     "db", db,
			     NULL);
}

static gboolean
rb_shell_clipboard_sync_idle (RBShellClipboard *clipboard)
{
	rb_shell_clipboard_sync (clipboard);
	clipboard->priv->idle_sync_id = 0;
	return FALSE;
}

static void
rb_shell_clipboard_sync (RBShellClipboard *clipboard)
{
	RBEntryView *view;
	gboolean have_selection = FALSE;
	gboolean can_cut = FALSE;
	gboolean can_paste = FALSE;
	gboolean can_delete = FALSE;
	gboolean can_copy = FALSE;
	gboolean can_add_to_queue = FALSE;
	gboolean can_move_to_trash = FALSE;
	gboolean can_select_all = FALSE;
	gboolean can_show_properties = FALSE;
	GApplication *app;
	GAction *gaction;

	app = g_application_get_default ();

	if (clipboard->priv->source) {
		view = rb_source_get_entry_view (clipboard->priv->source);
		if (view) {
			have_selection = rb_entry_view_have_selection (view);
			can_select_all = !rb_entry_view_have_complete_selection (view);
		}
	}

	rb_debug ("syncing clipboard");

	if (clipboard->priv->source != NULL && g_list_length (clipboard->priv->entries) > 0)
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

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-delete");
	g_object_set (gaction, "enabled", can_delete, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-trash");
	g_object_set (gaction, "enabled", can_move_to_trash, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-cut");
	g_object_set (gaction, "enabled", can_cut, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-copy");
	g_object_set (gaction, "enabled", can_copy, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-paste");
	g_object_set (gaction, "enabled", can_paste, NULL);
	
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-add-to-queue");
	g_object_set (gaction, "enabled", can_add_to_queue, NULL);
	
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-properties");
	g_object_set (gaction, "enabled", can_show_properties, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-select-all");
	g_object_set (gaction, "enabled", can_select_all, NULL);
	
	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "clipboard-select-none");
	g_object_set (gaction, "enabled", have_selection, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-add-to");
	g_object_set (gaction, "enabled", have_selection, NULL);

	gaction = g_action_map_lookup_action (G_ACTION_MAP (app), "playlist-add-to-new");
	g_object_set (gaction, "enabled", have_selection, NULL);
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
select_all_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	RBEntryView *entryview;
	GtkWidget *widget;

	rb_debug ("select all");
	widget = get_focussed_widget (clipboard);
	if (GTK_IS_EDITABLE (widget)) {
		gtk_editable_select_region (GTK_EDITABLE (widget), 0, -1);
	} else {
		/* select all tracks in the entry view */
		entryview = rb_source_get_entry_view (clipboard->priv->source);
		if (entryview != NULL) {
			rb_entry_view_select_all (entryview);
		}
	}
}

static void
select_none_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	RBEntryView *entryview;
	GtkWidget *widget;

	rb_debug ("select none");
	widget = get_focussed_widget (clipboard);
	if (GTK_IS_EDITABLE (widget)) {
		gtk_editable_select_region (GTK_EDITABLE (widget), -1, -1);
	} else {
		entryview = rb_source_get_entry_view (clipboard->priv->source);
		if (entryview != NULL) {
			rb_entry_view_select_none (entryview);
		}
	}
}

static void
cut_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_shell_clipboard_set (clipboard, rb_source_cut (clipboard->priv->source));
}

static void
copy_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_shell_clipboard_set (clipboard, rb_source_copy (clipboard->priv->source));
}

static void
paste_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_source_paste (clipboard->priv->source, clipboard->priv->entries);
}

static void
delete_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_source_delete_selected (clipboard->priv->source);
}

static void
move_to_trash_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_source_move_to_trash (clipboard->priv->source);
}

/* NOTE: takes ownership of the entries */
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

static void
rb_shell_clipboard_entry_deleted_cb (RhythmDB *db,
				     RhythmDBEntry *entry,
				     RBShellClipboard *clipboard)
{
	GList *l;

	l = g_list_find (clipboard->priv->entries, entry);
	if (l != NULL) {
		clipboard->priv->entries = g_list_delete_link (clipboard->priv->entries, l);
		rhythmdb_entry_unref (entry);
		if (clipboard->priv->idle_sync_id == 0)
			clipboard->priv->idle_sync_id = g_idle_add ((GSourceFunc) rb_shell_clipboard_sync_idle,
								    clipboard);
	}
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
add_to_queue_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_source_add_to_queue (clipboard->priv->source,
				RB_SOURCE (clipboard->priv->queue_source));
}

static void
properties_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShellClipboard *clipboard = RB_SHELL_CLIPBOARD (data);
	rb_source_song_properties (clipboard->priv->source);
}
