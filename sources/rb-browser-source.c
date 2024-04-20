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
 * SECTION:rbbrowsersource
 * @short_description: base class for sources that include genre/artist/album browsers
 *
 * This class simplifies implementation of sources that include genre/artist/album browsers.
 * It also handles searching (using the search box) and a few other UI niceties.
 * 
 * Instances of browser sources will use a query that will match all entries of
 * the entry type assigned to the source, so it's mostly suited for sources that
 * have an entry type of their own.
 */

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-source.h"
#include "rb-library-source.h"
#include "rb-source-search-basic.h"

#include "rhythmdb-query-model.h"
#include "rb-property-view.h"
#include "rb-entry-view.h"
#include "rb-library-browser.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-song-info.h"
#include "rb-search-entry.h"
#include "rb-source-toolbar.h"
#include "rb-shell-preferences.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

static void rb_browser_source_class_init (RBBrowserSourceClass *klass);
static void rb_browser_source_init (RBBrowserSource *source);
static void rb_browser_source_constructed (GObject *object);
static void rb_browser_source_dispose (GObject *object);
static void rb_browser_source_finalize (GObject *object);
static void rb_browser_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_browser_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void select_genre_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void select_artist_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);
static void select_album_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data);

static void songs_view_sort_order_changed_cb (GObject *object, GParamSpec *pspec, RBBrowserSource *source);
static void rb_browser_source_browser_changed_cb (RBLibraryBrowser *entry,
						  GParamSpec *param,
						  RBBrowserSource *source);

/* source methods */
static RBEntryView *impl_get_entry_view (RBSource *source);
static GList *impl_get_property_views (RBSource *source);
static void impl_delete_selected (RBSource *source);
static void impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_reset_filters (RBSource *source);
static void impl_song_properties (RBSource *source);
static void default_show_entry_popup (RBBrowserSource *source);
static void default_pack_content (RBBrowserSource *source, GtkWidget *content);

void rb_browser_source_browser_views_activated_cb (GtkWidget *widget,
						 RBBrowserSource *source);
static void songs_view_drag_data_received_cb (GtkWidget *widget,
					      GdkDragContext *dc,
					      gint x, gint y,
					      GtkSelectionData *data,
					      guint info, guint time,
					      RBBrowserSource *source);
static void rb_browser_source_do_query (RBBrowserSource *source,
					gboolean subset);
static void rb_browser_source_populate (RBBrowserSource *source);

struct RBBrowserSourcePrivate
{
	RhythmDB *db;

	RBLibraryBrowser *browser;
	RBEntryView *songs;
	RBSourceToolbar *toolbar;

	RhythmDBQueryModel *cached_all_query;
	RhythmDBQuery *search_query;
	RhythmDBPropType search_prop;
	gboolean populate;
	gboolean query_active;
	gboolean search_on_completion;
	RBSourceSearch *default_search;

	GMenu *popup;
	GMenu *search_popup;
	GAction *search_action;
};

#define RB_BROWSER_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_BROWSER_SOURCE, RBBrowserSourcePrivate))

static const GtkTargetEntry songs_view_drag_types[] = {
	{ "application/x-rhythmbox-entry", 0, 0 },
	{ "text/uri-list", 0, 1 }
};

enum
{
	PROP_0,
	PROP_BASE_QUERY_MODEL,
	PROP_POPULATE,
	PROP_SHOW_BROWSER
};

G_DEFINE_ABSTRACT_TYPE (RBBrowserSource, rb_browser_source, RB_TYPE_SOURCE)

static void
rb_browser_source_class_init (RBBrowserSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_browser_source_dispose;
	object_class->finalize = rb_browser_source_finalize;
	object_class->constructed = rb_browser_source_constructed;

	object_class->set_property = rb_browser_source_set_property;
	object_class->get_property = rb_browser_source_get_property;

	source_class->reset_filters = impl_reset_filters;
	source_class->search = impl_search;
	source_class->get_entry_view = impl_get_entry_view;
	source_class->get_property_views = impl_get_property_views;
	source_class->song_properties = impl_song_properties;
	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->delete_selected = impl_delete_selected;

	klass->pack_content = default_pack_content;
	klass->has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;
	klass->show_entry_popup = default_show_entry_popup;

	g_object_class_override_property (object_class,
					  PROP_BASE_QUERY_MODEL,
					  "base-query-model");
	
	g_object_class_install_property (object_class,
					 PROP_POPULATE,
					 g_param_spec_boolean ("populate",
						 	       "populate",
							       "whether to populate the source",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_override_property (object_class,
					  PROP_SHOW_BROWSER,
					  "show-browser");

	g_type_class_add_private (klass, sizeof (RBBrowserSourcePrivate));
}

static void
rb_browser_source_init (RBBrowserSource *source)
{
	source->priv = RB_BROWSER_SOURCE_GET_PRIVATE (source);
}

static void
rb_browser_source_dispose (GObject *object)
{
	RBBrowserSource *source;
	source = RB_BROWSER_SOURCE (object);

	g_clear_pointer (&source->priv->search_query, rhythmdb_query_free);
	g_clear_object (&source->priv->db);
	g_clear_object (&source->priv->cached_all_query);
	g_clear_object (&source->priv->default_search);
	g_clear_object (&source->priv->popup);
	g_clear_object (&source->priv->search_popup);
	g_clear_object (&source->priv->search_action);

	G_OBJECT_CLASS (rb_browser_source_parent_class)->dispose (object);
}

static void
rb_browser_source_finalize (GObject *object)
{
	RBBrowserSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_BROWSER_SOURCE (object));

	source = RB_BROWSER_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	G_OBJECT_CLASS (rb_browser_source_parent_class)->finalize (object);
}

static void
rb_browser_source_songs_show_popup_cb (RBEntryView *view,
				       gboolean over_entry,
				       RBBrowserSource *source)
{
	if (over_entry) {
		RBBrowserSourceClass *klass = RB_BROWSER_SOURCE_GET_CLASS (source);

		klass->show_entry_popup (source);
	}
}

static void
default_show_entry_popup (RBBrowserSource *source)
{
	GtkWidget *menu;
	GMenuModel *playlist_menu;

	/* update add to playlist menu links */
	g_object_get (source, "playlist-menu", &playlist_menu, NULL);
	rb_menu_update_link (source->priv->popup, "rb-playlist-menu-link", playlist_menu);
	g_clear_object (&playlist_menu);

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
rb_browser_source_constructed (GObject *object)
{
	RBBrowserSource *source;
	RBBrowserSourceClass *klass;
	RBShell *shell;
	GObject *shell_player;
	GtkAccelGroup *accel_group;
	RhythmDBEntryType *entry_type;
	GtkWidget *content;
	GtkWidget *paned;
	GtkBuilder *builder;
	GMenu *section;
	GActionEntry actions[] = {
		{ "browser-select-genre", select_genre_action_cb },
		{ "browser-select-artist", select_artist_action_cb },
		{ "browser-select-album", select_album_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_browser_source_parent_class, constructed, object);

	source = RB_BROWSER_SOURCE (object);

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell,
		      "db", &source->priv->db,
		      "shell-player", &shell_player,
		      "accel-group", &accel_group,
		      NULL);

	_rb_add_display_page_actions (G_ACTION_MAP (g_application_get_default ()),
				      G_OBJECT (shell),
				      actions,
				      G_N_ELEMENTS (actions));
	g_object_unref (shell);


	source->priv->search_action = rb_source_create_search_action (RB_SOURCE (source));
	g_action_map_add_action (G_ACTION_MAP (g_application_get_default ()), source->priv->search_action);

	/* ensure search instances exist */
	rb_source_search_basic_register (RHYTHMDB_PROP_SEARCH_MATCH, "search-match", _("Search all fields"));
	rb_source_search_basic_register (RHYTHMDB_PROP_ARTIST_FOLDED, "artist", _("Search artists"));
	rb_source_search_basic_register (RHYTHMDB_PROP_COMPOSER_FOLDED, "composer", _("Search composers"));
	rb_source_search_basic_register (RHYTHMDB_PROP_ALBUM_FOLDED, "album", _("Search albums"));
	rb_source_search_basic_register (RHYTHMDB_PROP_TITLE_FOLDED, "title", _("Search titles"));
	rb_source_search_basic_register (RHYTHMDB_PROP_GENRE_FOLDED, "genre", _("Search genres"));
	
	section = g_menu_new ();
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "search-match");
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "genre");
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "artist");
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "composer");
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "album");
	rb_source_search_add_to_menu (section, "app", source->priv->search_action, "title");

	source->priv->search_popup = g_menu_new ();
	g_menu_append_section (source->priv->search_popup, NULL, G_MENU_MODEL (section));

	source->priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH, _("Search all fields"));

	paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);

	source->priv->browser = rb_library_browser_new (source->priv->db, entry_type);
	gtk_widget_set_no_show_all (GTK_WIDGET (source->priv->browser), TRUE);
	gtk_paned_pack1 (GTK_PANED (paned), GTK_WIDGET (source->priv->browser), TRUE, FALSE);
	gtk_container_child_set (GTK_CONTAINER (paned),
				 GTK_WIDGET (source->priv->browser),
				 "resize", FALSE,
				 NULL);
	g_signal_connect_object (G_OBJECT (source->priv->browser), "notify::output-model",
				 G_CALLBACK (rb_browser_source_browser_changed_cb),
				 source, 0);

	/* set up songs tree view */
	source->priv->songs = rb_entry_view_new (source->priv->db, shell_player,
						 TRUE, FALSE);

	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_COMPOSER, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_YEAR, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION, FALSE);
 	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_QUALITY, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_BPM, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_COMMENT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LOCATION, FALSE);

	g_signal_connect_object (G_OBJECT (source->priv->songs), "show_popup",
				 G_CALLBACK (rb_browser_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (source->priv->songs,
				 "notify::sort-order",
				 G_CALLBACK (songs_view_sort_order_changed_cb),
				 source, 0);

	rb_source_bind_settings (RB_SOURCE (source),
				 GTK_WIDGET (source->priv->songs),
				 paned,
				 GTK_WIDGET (source->priv->browser),
				 TRUE);

	if (rb_browser_source_has_drop_support (source)) {
		gtk_drag_dest_set (GTK_WIDGET (source->priv->songs),
				   GTK_DEST_DEFAULT_ALL,
				   songs_view_drag_types, G_N_ELEMENTS (songs_view_drag_types),
				   GDK_ACTION_COPY | GDK_ACTION_MOVE);	/* really accept move actions? */

		/* set up drag and drop for the song tree view.
		 * we don't use RBEntryView's DnD support because it does too much.
		 * we just want to be able to drop songs in to add them to the
		 * library.
		 */
		g_signal_connect_object (G_OBJECT (source->priv->songs),
					 "drag_data_received",
					 G_CALLBACK (songs_view_drag_data_received_cb),
					 source, 0);
	}

	gtk_paned_pack2 (GTK_PANED (paned), GTK_WIDGET (source->priv->songs), TRUE, FALSE);

	/* set up toolbar */
	source->priv->toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (source), accel_group);
	rb_source_toolbar_add_search_entry_menu (source->priv->toolbar, G_MENU_MODEL (source->priv->search_popup), source->priv->search_action);

	content = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (content), 6);
	gtk_grid_set_row_spacing (GTK_GRID (content), 6);
	gtk_widget_set_margin_top (content, 6);
	gtk_grid_attach (GTK_GRID (content), GTK_WIDGET (source->priv->toolbar), 0, 0, 1, 1);
	gtk_widget_set_vexpand (paned, TRUE);
	gtk_widget_set_hexpand (paned, TRUE);
	gtk_grid_attach (GTK_GRID (content), paned, 0, 1, 1, 1);

	klass = RB_BROWSER_SOURCE_GET_CLASS (source);
	klass->pack_content (source, content);

	gtk_widget_show_all (GTK_WIDGET (source));

	/* use a throwaway model until the real one is ready */
	rb_library_browser_set_model (source->priv->browser,
				      rhythmdb_query_model_new_empty (source->priv->db),
				      FALSE);

	source->priv->cached_all_query = rhythmdb_query_model_new_empty (source->priv->db);
	rb_browser_source_populate (source);

	builder = rb_builder_load ("browser-popup.ui", NULL);
	source->priv->popup = G_MENU (gtk_builder_get_object (builder, "browser-popup"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()),
					  source->priv->popup);
	g_object_ref (source->priv->popup);
	g_object_unref (builder);

	g_object_unref (entry_type);
	g_object_unref (shell_player);
	g_object_unref (accel_group);
}

static void
rb_browser_source_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (object);

	switch (prop_id) {
	case PROP_POPULATE:
		source->priv->populate = g_value_get_boolean (value);

		/* if being set after construction, run the query now.  otherwise the constructor will do it. */
		if (source->priv->songs != NULL) {
			rb_browser_source_populate (source);
		}
		break;
	case PROP_SHOW_BROWSER:
		if (g_value_get_boolean (value)) {
			gtk_widget_show (GTK_WIDGET (source->priv->browser));
		} else {
			gtk_widget_hide (GTK_WIDGET (source->priv->browser));
			rb_library_browser_reset (source->priv->browser);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_browser_source_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (object);

	switch (prop_id) {
	case PROP_BASE_QUERY_MODEL:
		g_value_set_object (value, source->priv->cached_all_query);
		break;
	case PROP_POPULATE:
		g_value_set_boolean (value, source->priv->populate);
		break;
	case PROP_SHOW_BROWSER:
		g_value_set_boolean (value, gtk_widget_get_visible (GTK_WIDGET (source->priv->browser)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
cached_all_query_complete_cb (RhythmDBQueryModel *model, RBBrowserSource *source)
{
	rb_library_browser_set_model (source->priv->browser,
				      source->priv->cached_all_query,
				      FALSE);
}

static void
rb_browser_source_populate (RBBrowserSource *source)
{
	RhythmDBEntryType *entry_type;

	if (source->priv->populate == FALSE)
		return;

	/* only connect the model to the browser when it's complete.  this avoids
	 * thousands of row-added signals, which is ridiculously slow with a11y enabled.
	 */
	g_signal_connect_object (source->priv->cached_all_query,
				 "complete",
				 G_CALLBACK (cached_all_query_complete_cb),
				 source, 0);

	g_object_get (source, "entry-type", &entry_type, NULL);
	rhythmdb_do_full_query_async (source->priv->db,
				      RHYTHMDB_QUERY_RESULTS (source->priv->cached_all_query),
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, entry_type,
				      RHYTHMDB_QUERY_END);
	g_object_unref (entry_type);
}

static void
browse_property (RBBrowserSource *source, RhythmDBPropType prop)
{
	GList *props;
	RBPropertyView *view;

	props = rb_source_gather_selected_properties (RB_SOURCE (source), prop);
	view = rb_library_browser_get_property_view (source->priv->browser, prop);
	if (view) {
		rb_property_view_set_selection (view, props);
	}

	rb_list_deep_free (props);
}

static void
select_genre_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	rb_debug ("choosing genre");

	if (RB_IS_BROWSER_SOURCE (data)) {
		browse_property (RB_BROWSER_SOURCE (data), RHYTHMDB_PROP_GENRE);
	}
}

static void
select_artist_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	rb_debug ("choosing artist");

	if (RB_IS_BROWSER_SOURCE (data)) {
		browse_property (RB_BROWSER_SOURCE (data), RHYTHMDB_PROP_ARTIST);
	}
}

static void
select_album_action_cb (GSimpleAction *action, GVariant *parameters, gpointer data)
{
	rb_debug ("choosing album");

	if (RB_IS_BROWSER_SOURCE (data)) {
		browse_property (RB_BROWSER_SOURCE (data), RHYTHMDB_PROP_ALBUM);
	}
}

static void
songs_view_sort_order_changed_cb (GObject *object, GParamSpec *pspec, RBBrowserSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
impl_search (RBSource *asource, RBSourceSearch *search, const char *cur_text, const char *new_text)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
	gboolean subset;

	if (search == NULL) {
		search = source->priv->default_search;
	}

	/* replace our search query */
	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}
	source->priv->search_query = rb_source_search_create_query (search, source->priv->db, new_text);

	/* for subset searches, we have to wait until the query
	 * has finished before we can refine the results.
	 */
	subset = rb_source_search_is_subset (search, cur_text, new_text);
	if (source->priv->query_active && subset) {
		rb_debug ("deferring search for \"%s\" until query completion", new_text ? new_text : "<NULL>");
		source->priv->search_on_completion = TRUE;
	} else {
		rb_debug ("doing search for \"%s\"", new_text ? new_text : "<NULL>");
		rb_browser_source_do_query (source, subset);
	}
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);

	return source->priv->songs;
}

static GList *
impl_get_property_views (RBSource *asource)
{
	GList *ret;
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);

	ret =  rb_library_browser_get_property_views (source->priv->browser);
	return ret;
}

static void
impl_reset_filters (RBSource *asource)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
	gboolean changed = FALSE;

	rb_debug ("Resetting search filters");

	if (rb_library_browser_reset (source->priv->browser))
		changed = TRUE;

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
		changed = TRUE;
	}

	rb_source_toolbar_clear_search_entry (source->priv->toolbar);

	if (changed)
		rb_browser_source_do_query (source, FALSE);
}

static void
impl_delete_selected (RBSource *asource)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_delete (source->priv->db, tem->data);
		rhythmdb_commit (source->priv->db);
	}
	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
impl_song_properties (RBSource *asource)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
 	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

 	song_info = rb_song_info_new (asource, NULL);

        g_return_if_fail (song_info != NULL);

 	if (song_info)
 		gtk_widget_show_all (song_info);
 	else
		rb_debug ("failed to create dialog, or no selection!");
}

/**
 * rb_browser_source_has_drop_support:
 * @source: a #RBBrowserSource
 *
 * This is a virtual method that should be implemented by subclasses.  It returns %TRUE
 * if drag and drop target support for the source should be activated.
 *
 * Return value: %TRUE if drop support should be activated
 */
gboolean
rb_browser_source_has_drop_support (RBBrowserSource *source)
{
	RBBrowserSourceClass *klass = RB_BROWSER_SOURCE_GET_CLASS (source);

	return klass->has_drop_support (source);
}

static void
songs_view_drag_data_received_cb (GtkWidget *widget,
				  GdkDragContext *dc,
				  gint x, gint y,
				  GtkSelectionData *selection_data,
				  guint info, guint time,
				  RBBrowserSource *source)
{
	rb_debug ("data dropped on the library source song view");
	rb_display_page_receive_drag (RB_DISPLAY_PAGE (source), selection_data);
}

static void
rb_browser_source_browser_changed_cb (RBLibraryBrowser *browser,
				      GParamSpec *pspec,
				      RBBrowserSource *source)
{
	RhythmDBQueryModel *query_model;

	g_object_get (browser, "output-model", &query_model, NULL);
	rb_entry_view_set_model (source->priv->songs, query_model);
	g_object_set (source, "query-model", query_model, NULL);
	g_object_unref (query_model);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static void
rb_browser_source_query_complete_cb (RhythmDBQueryModel *query_model,
				     RBBrowserSource *source)
{
	rb_library_browser_set_model (source->priv->browser, query_model, FALSE);

	source->priv->query_active = FALSE;
	if (source->priv->search_on_completion) {
		rb_debug ("performing deferred search");
		source->priv->search_on_completion = FALSE;
		/* this is only done for subset queries */
		rb_browser_source_do_query (source, TRUE);
	}
}

static void
rb_browser_source_do_query (RBBrowserSource *source, gboolean subset)
{
	RhythmDBQueryModel *query_model;
	GPtrArray *query;
	RhythmDBEntryType *entry_type;

	/* use the cached 'all' query to optimise the no-search case */
	if (source->priv->search_query == NULL) {
		rb_library_browser_set_model (source->priv->browser,
					      source->priv->cached_all_query,
					      FALSE);
		return;
	}

	g_object_get (source, "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_SUBQUERY,
				      source->priv->search_query,
				      RHYTHMDB_QUERY_END);
	g_object_unref (entry_type);

	if (subset) {
		/* if we're appending text to an existing search string, the results will be a subset
		 * of the existing results, so rather than doing a whole new query, we can copy the
		 * results to a new query model with a more restrictive query.
		 */
		RhythmDBQueryModel *old;
		g_object_get (source->priv->browser, "input-model", &old, NULL);

		query_model = rhythmdb_query_model_new_empty (source->priv->db);
		g_object_set (query_model, "query", query, NULL);
		rhythmdb_query_model_copy_contents (query_model, old);
		g_object_unref (old);

		rb_library_browser_set_model (source->priv->browser, query_model, FALSE);
		g_object_unref (query_model);

	} else {
		/* otherwise build a query based on the search text, and feed it to the browser
		 * when the query finishes.
		 */ 
		query_model = rhythmdb_query_model_new_empty (source->priv->db);
		source->priv->query_active = TRUE;
		source->priv->search_on_completion = FALSE;
		g_signal_connect_object (query_model,
					 "complete", G_CALLBACK (rb_browser_source_query_complete_cb),
					 source, 0);
		rhythmdb_do_full_query_async_parsed (source->priv->db,
						     RHYTHMDB_QUERY_RESULTS (query_model),
						     query);
		g_object_unref (query_model);
	}

	rhythmdb_query_free (query);
}

static void
default_pack_content (RBBrowserSource *source, GtkWidget *content)
{
	gtk_container_add (GTK_CONTAINER (source), content);
}
