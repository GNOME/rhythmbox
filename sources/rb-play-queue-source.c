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
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "rb-play-queue-source.h"
#include "rb-playlist-xml.h"
#include "rb-song-info.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-play-order-queue.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

/**
 * SECTION:rbplayqueuesource
 * @short_description: source object for the play queue
 *
 * The main interesting thing about this source is that is
 * contains a second #RBEntryView to be displayed in the side
 * pane (beneath the source list).  This entry view displays
 * the track title, artist, and album in a single column,
 * split across three lines so the information mostly fits in
 * the usual horizontal space allowed for the side bar.
 */

static const char *RB_PLAY_QUEUE_DBUS_PATH = "/org/gnome/Rhythmbox3/PlayQueue";
static const char *RB_PLAY_QUEUE_IFACE_NAME = "org.gnome.Rhythmbox3.PlayQueue";

static const char *rb_play_queue_dbus_spec =
"<node>"
"  <interface name='org.gnome.Rhythmbox3.PlayQueue'>"
"    <method name='AddToQueue'>"
"      <arg type='s' name='uri'/>"
"    </method>"
"    <method name='RemoveFromQueue'>"
"      <arg type='s' name='uri'/>"
"    </method>"
"    <method name='ClearQueue'/>"
"  </interface>"
"</node>";

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
static gboolean impl_can_remove (RBDisplayPage *page);
static gboolean impl_check_entry_type (RBSource *source, RhythmDBEntry *entry);

static void queue_clear_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void queue_shuffle_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void queue_delete_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void queue_properties_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void queue_save_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);


static void rb_play_queue_dbus_method_call (GDBusConnection *connection,
					    const char *sender,
					    const char *object_path,
					    const char *interface_name,
					    const char *method_name,
					    GVariant *parameters,
					    GDBusMethodInvocation *invocation,
					    RBPlayQueueSource *source);

typedef struct _RBPlayQueueSourcePrivate RBPlayQueueSourcePrivate;

struct _RBPlayQueueSourcePrivate
{
	RBEntryView *sidebar;
	GtkTreeViewColumn *sidebar_column;
	RBPlayOrder *queue_play_order;

	guint dbus_object_id;
	GDBusConnection *bus;

	GMenu *popup;
	GMenu *sidepane_popup;

	guint update_count_idle_id;
};

enum
{
	PROP_0,
	PROP_SIDEBAR,
	PROP_PLAY_ORDER
};

G_DEFINE_TYPE (RBPlayQueueSource, rb_play_queue_source, RB_TYPE_STATIC_PLAYLIST_SOURCE)
#define RB_PLAY_QUEUE_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSourcePrivate))

static const GDBusInterfaceVTable play_queue_vtable = {
	(GDBusInterfaceMethodCallFunc) rb_play_queue_dbus_method_call,
	NULL,
	NULL
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

	g_clear_object (&priv->queue_play_order);

	if (priv->update_count_idle_id) {
		g_source_remove (priv->update_count_idle_id);
		priv->update_count_idle_id = 0;
	}

	if (priv->bus != NULL) {
		if (priv->dbus_object_id) {
			g_dbus_connection_unregister_object (priv->bus, priv->dbus_object_id);
			priv->dbus_object_id = 0;
		}
		g_object_unref (priv->bus);
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
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructed = rb_play_queue_source_constructed;
	object_class->get_property = rb_play_queue_source_get_property;
	object_class->finalize = rb_play_queue_source_finalize;
	object_class->dispose  = rb_play_queue_source_dispose;

	source_class->can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_rename = (RBSourceFeatureFunc) rb_false_function;
	source_class->check_entry_type = impl_check_entry_type;

	page_class->can_remove = impl_can_remove;

	playlist_class->show_entry_view_popup = impl_show_entry_view_popup;
	playlist_class->save_contents_to_xml = impl_save_contents_to_xml;

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
	GtkBuilder *builder;
	RhythmDBQueryModel *model;
	RBApplication *app;
	GActionEntry actions[] = {
		{ "queue-clear", queue_clear_action_cb },
		{ "queue-shuffle", queue_shuffle_action_cb },
		{ "queue-delete", queue_delete_action_cb },
		{ "queue-properties", queue_properties_action_cb },
		{ "queue-save", queue_save_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_play_queue_source_parent_class, constructed, object);

	app = RB_APPLICATION (g_application_get_default ());
	source = RB_PLAY_QUEUE_SOURCE (object);
	priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "shell-player", &shell_player, NULL);
	g_object_unref (shell);

	priv->queue_play_order = rb_queue_play_order_new (RB_SHELL_PLAYER (shell_player));

	g_action_map_add_action_entries (G_ACTION_MAP (app),
					 actions,
					 G_N_ELEMENTS (actions),
					 source);

	priv->sidebar = rb_entry_view_new (db, shell_player, TRUE, TRUE);
	g_object_unref (shell_player);

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (priv->sidebar)),
				     "sidebar-queue");

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

	/* load popup menus */
	builder = rb_builder_load ("queue-popups.ui", NULL);
	priv->popup = G_MENU (gtk_builder_get_object (builder, "queue-source-popup"));
	priv->sidepane_popup = G_MENU (gtk_builder_get_object (builder, "queue-sidepane-popup"));
	rb_application_link_shared_menus (app, priv->popup);
	rb_application_link_shared_menus (app, priv->sidepane_popup);
	g_object_ref (priv->popup);
	g_object_ref (priv->sidepane_popup);
	g_object_unref (builder);

	/* register dbus interface */
	priv->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (priv->bus) {
		GDBusNodeInfo *node_info;
		GError *error = NULL;

		node_info = g_dbus_node_info_new_for_xml (rb_play_queue_dbus_spec, &error);
		if (error != NULL) {
			g_warning ("Unable to parse playlist manager dbus spec: %s", error->message);
			g_clear_error (&error);
			return;
		}

		priv->dbus_object_id = g_dbus_connection_register_object (priv->bus,
									  RB_PLAY_QUEUE_DBUS_PATH,
									  g_dbus_node_info_lookup_interface (node_info, RB_PLAY_QUEUE_IFACE_NAME),
									  &play_queue_vtable,
									  source,
									  NULL,
									  &error);
		if (error != NULL) {
			g_warning ("Unable to register play queue dbus object: %s", error->message);
			g_clear_error (&error);
		}
	}
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
	RBSource *source;
	GtkBuilder *builder;
	GMenu *toolbar;

	builder = rb_builder_load ("queue-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "queue-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	source = RB_SOURCE (g_object_new (RB_TYPE_PLAY_QUEUE_SOURCE,
					  "name", _("Play Queue"),
					  "shell", shell,
					  "is-local", TRUE,
					  "entry-type", NULL,
					  "toolbar-menu", toolbar,
					  "show-browser", FALSE,
					  NULL));
	g_object_unref (builder);
	return source;
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
	GtkWidget *menu;
	GMenu *popup;
	RBApplication *app;

	if (view == priv->sidebar) {
		popup = priv->sidepane_popup;
	} else {
		popup = priv->popup;
	}

	app = RB_APPLICATION (g_application_get_default ());
	rb_menu_update_link (popup, "rb-playlist-menu-link", rb_application_get_shared_menu (app, "playlist-page-menu"));

	menu = gtk_menu_new_from_model (G_MENU_MODEL (popup));
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

	markup = g_markup_printf_escaped ("%s\n<span size=\"smaller\">%s <i>%s</i>\n%s <i>%s</i></span>",
					  /* Translators: format is "<title> by <artist> from <album>" */
					  title, _("by"), artist, _("from"), album);

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

static gboolean
update_count_idle (RBPlayQueueSource *source)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	RhythmDBQueryModel *model;
	char *name = _("Play Queue");
	int count;
       
	model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (source));
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);

	/* update source name */
	if (count > 0)
		name = g_strdup_printf ("%s (%d)", name, count);

	g_object_set (G_OBJECT (source), "name", name, NULL);
	gtk_tree_view_column_set_title (priv->sidebar_column, name);

	if (count > 0)
		g_free (name);

	priv->update_count_idle_id = 0;
	return FALSE;
}

static void
rb_play_queue_source_update_count (RBPlayQueueSource *source,
				   GtkTreeModel *model,
				   gint offset)
{
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	GAction *action;
	int count;

	if (priv->update_count_idle_id == 0) {
		priv->update_count_idle_id = g_idle_add ((GSourceFunc) update_count_idle, source);
	}

	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) + offset;
	/* make 'clear queue' and 'shuffle queue' actions sensitive when there are entries in the queue */
	action = g_action_map_lookup_action (G_ACTION_MAP (g_application_get_default ()),
					     "queue-clear");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (action), (count > 0));

	action = g_action_map_lookup_action (G_ACTION_MAP (g_application_get_default ()),
					     "queue-shuffle");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (action), (count > 0));
}

static void
impl_save_contents_to_xml (RBPlaylistSource *source,
			   xmlNodePtr node)
{
	((RBPlaylistSourceClass*)rb_play_queue_source_parent_class)->save_contents_to_xml (source, node);
	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_QUEUE);
}

static void
queue_clear_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	rb_play_queue_source_clear_queue (RB_PLAY_QUEUE_SOURCE (data));
}

static void
queue_shuffle_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RhythmDBQueryModel *model;

	model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (data));
	rhythmdb_query_model_shuffle_entries (model);
}

static void
queue_delete_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBPlayQueueSource *source = RB_PLAY_QUEUE_SOURCE (data);
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
queue_properties_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBPlayQueueSource *source = RB_PLAY_QUEUE_SOURCE (data);
	RBPlayQueueSourcePrivate *priv = RB_PLAY_QUEUE_SOURCE_GET_PRIVATE (source);
	GtkWidget *song_info = NULL;

	g_return_if_fail (priv->sidebar != NULL);

	song_info = rb_song_info_new (RB_SOURCE (source), priv->sidebar);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static void
queue_save_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	RBShell *shell;
	RBPlaylistManager *mgr;

	g_object_get (data, "shell", &shell, NULL);
	g_object_get (shell, "playlist-manager", &mgr, NULL);
	rb_playlist_manager_save_playlist_file (mgr, RB_SOURCE (data));
	g_object_unref (mgr);
	g_object_unref (shell);
}


static void
rb_play_queue_dbus_method_call (GDBusConnection *connection,
				const char *sender,
				const char *object_path,
				const char *interface_name,
				const char *method_name,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				RBPlayQueueSource *source)
{
	RhythmDBEntry *entry;
	RhythmDB *db;
	const char *uri;

	if (g_strcmp0 (interface_name, RB_PLAY_QUEUE_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
		return;
	}

	if (g_strcmp0 (method_name, "AddToQueue") == 0) {
		g_variant_get (parameters, "(&s)", &uri);

		db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
		entry = rhythmdb_entry_lookup_by_location (db, uri);
		if (entry == NULL) {
			RBSource *urisource;
			RBShell *shell;

			g_object_get (source, "shell", &shell, NULL);
			urisource = rb_shell_guess_source_for_uri (shell, uri);
			g_object_unref (shell);

			if (urisource != NULL) {
				rb_source_add_uri (urisource, uri, NULL, NULL, NULL, NULL, NULL);
			} else {
				g_dbus_method_invocation_return_error (invocation,
								       RB_SHELL_ERROR,
								       RB_SHELL_ERROR_NO_SOURCE_FOR_URI,
								       _("No registered source can handle URI %s"),
								       uri);
				return;
			}
		}
		rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (source),
							uri, -1);

		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "RemoveFromQueue") == 0) {
		g_variant_get (parameters, "(&s)", &uri);

		if (rb_playlist_source_location_in_map (RB_PLAYLIST_SOURCE (source), uri)) {
			rb_static_playlist_source_remove_location (RB_STATIC_PLAYLIST_SOURCE (source), uri);
		}

		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "ClearQueue") == 0) {
		rb_play_queue_source_clear_queue (RB_PLAY_QUEUE_SOURCE (source));
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static gboolean
impl_can_remove (RBDisplayPage *page)
{
	return FALSE;
}

static gboolean
impl_check_entry_type (RBSource *source, RhythmDBEntry *entry)
{
	return (rhythmdb_entry_get_playback_uri (entry) != NULL);
}
