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
 * SECTION:rb-browser-source
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
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-search-entry.h"
#include "rb-preferences.h"
#include "rb-shell-preferences.h"

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
static void rb_browser_source_cmd_choose_genre (GtkAction *action, RBSource *source);
static void rb_browser_source_cmd_choose_artist (GtkAction *action, RBSource *source);
static void rb_browser_source_cmd_choose_album (GtkAction *action, RBSource *source);
static void songs_view_sort_order_changed_cb (RBEntryView *view, RBBrowserSource *source);
static void rb_browser_source_browser_changed_cb (RBLibraryBrowser *entry,
						  GParamSpec *param,
						  RBBrowserSource *source);

static void rb_browser_source_state_prefs_sync (RBBrowserSource *source);
static void rb_browser_source_state_pref_changed (GConfClient *client,
							 guint cnxn_id,
							 GConfEntry *entry,
							 RBBrowserSource *source);

static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBBrowserSource *source);
/* source methods */
static RBEntryView *impl_get_entry_view (RBSource *source);
static GList *impl_get_property_views (RBSource *source);
static void impl_delete (RBSource *source);
static void impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_reset_filters (RBSource *source);
static void impl_song_properties (RBSource *source);
static GList *impl_get_search_actions (RBSource *source);
static void impl_browser_toggled (RBSource *asource, gboolean disclosed);
static void default_show_entry_popup (RBBrowserSource *source);
static void default_pack_paned (RBBrowserSource *source, GtkWidget *paned);

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
	GtkWidget *paned;

	RhythmDBQueryModel *cached_all_query;
	RhythmDBQuery *search_query;
	RhythmDBPropType search_prop;
	gboolean populate;
	gboolean query_active;
	gboolean search_on_completion;
	RBSourceSearch *default_search;

	GtkActionGroup *action_group;
	GtkActionGroup *search_action_group;

	char *sorting_key;
	guint state_paned_notify_id;
	guint state_browser_notify_id;
	guint state_sorting_notify_id;

	gboolean dispose_has_run;
};

#define RB_BROWSER_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_BROWSER_SOURCE, RBBrowserSourcePrivate))

static GtkActionEntry rb_browser_source_actions [] =
{
	{ "BrowserSrcChooseGenre", NULL, N_("Browse This _Genre"), NULL,
	  N_("Set the browser to view only this genre"),
	  G_CALLBACK (rb_browser_source_cmd_choose_genre) },
	{ "BrowserSrcChooseArtist", NULL , N_("Browse This _Artist"), NULL,
	  N_("Set the browser to view only this artist"),
	  G_CALLBACK (rb_browser_source_cmd_choose_artist) },
	{ "BrowserSrcChooseAlbum", NULL, N_("Browse This A_lbum"), NULL,
	  N_("Set the browser to view only this album"),
	  G_CALLBACK (rb_browser_source_cmd_choose_album) }
};

static GtkRadioActionEntry rb_browser_source_radio_actions [] =
{
	{ "BrowserSourceSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), RHYTHMDB_PROP_SEARCH_MATCH },
	{ "BrowserSourceSearchArtists", NULL, N_("Artists"), NULL, N_("Search artists"), RHYTHMDB_PROP_ARTIST_FOLDED },
	{ "BrowserSourceSearchAlbums", NULL, N_("Albums"), NULL, N_("Search albums"), RHYTHMDB_PROP_ALBUM_FOLDED },
	{ "BrowserSourceSearchTitles", NULL, N_("Titles"), NULL, N_("Search titles"), RHYTHMDB_PROP_TITLE_FOLDED }
};

static const GtkTargetEntry songs_view_drag_types[] = {
	{ "application/x-rhythmbox-entry", 0, 0 },
	{ "text/uri-list", 0, 1 }
};

enum
{
	PROP_0,
	PROP_SORTING_KEY,
	PROP_BASE_QUERY_MODEL,
	PROP_POPULATE,
	PROP_SEARCH_TYPE
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

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_property_views = impl_get_property_views;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_get_search_actions = impl_get_search_actions;
	source_class->impl_browser_toggled = impl_browser_toggled;

	klass->impl_pack_paned = default_pack_paned;
	klass->impl_get_paned_key = (RBBrowserSourceStringFunc)rb_null_function;
	klass->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;
	klass->impl_show_entry_popup = default_show_entry_popup;

	g_object_class_install_property (object_class,
					 PROP_SORTING_KEY,
					 g_param_spec_string ("sorting-key",
							      "Sorting key",
							      "GConf key for storing sort-order",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
					  PROP_SEARCH_TYPE,
					  "search-type");

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

	if (source->priv->dispose_has_run) {
		/* If dispose did already run, return. */
		return;
	}
	/* Make sure dispose does not run twice. */
	source->priv->dispose_has_run = TRUE;

	if (source->priv->db != NULL) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}

	if (source->priv->cached_all_query != NULL) {
		g_object_unref (source->priv->cached_all_query);
		source->priv->cached_all_query = NULL;
	}

	if (source->priv->action_group != NULL) {
		g_object_unref (source->priv->action_group);
		source->priv->action_group = NULL;
	}

	if (source->priv->default_search != NULL) {
		g_object_unref (source->priv->default_search);
		source->priv->default_search = NULL;
	}

	eel_gconf_notification_remove (source->priv->state_browser_notify_id);
	eel_gconf_notification_remove (source->priv->state_paned_notify_id);
	eel_gconf_notification_remove (source->priv->state_sorting_notify_id);

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

	g_free (source->priv->sorting_key);

	G_OBJECT_CLASS (rb_browser_source_parent_class)->finalize (object);
}

static void
rb_browser_source_songs_show_popup_cb (RBEntryView *view,
				       gboolean over_entry,
				       RBBrowserSource *source)
{
	if (over_entry) {
		RBBrowserSourceClass *klass = RB_BROWSER_SOURCE_GET_CLASS (source);

		klass->impl_show_entry_popup (source);
	} else {
		rb_source_show_popup (RB_SOURCE (source));
	}
}

static void
default_show_entry_popup (RBBrowserSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/BrowserSourceViewPopup");
}

static void
rb_browser_source_constructed (GObject *object)
{
	RBBrowserSource *source;
	RBBrowserSourceClass *klass;
	RBShell *shell;
	GObject *shell_player;
	char *browser_key;
	char *paned_key;
	RhythmDBEntryType *entry_type;

	RB_CHAIN_GOBJECT_METHOD (rb_browser_source_parent_class, constructed, object);

	source = RB_BROWSER_SOURCE (object);

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell, "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);

	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "BrowserSourceActions",
								       NULL, 0, NULL);
	_rb_action_group_add_source_actions (source->priv->action_group,
					     G_OBJECT (shell),
					     rb_browser_source_actions,
					     G_N_ELEMENTS (rb_browser_source_actions));

	/* only add the actions if we haven't already */
	if (gtk_action_group_get_action (source->priv->action_group,
					 rb_browser_source_radio_actions[0].name) == NULL) {
		gtk_action_group_add_radio_actions (source->priv->action_group,
						    rb_browser_source_radio_actions,
						    G_N_ELEMENTS (rb_browser_source_radio_actions),
						    0,
						    NULL,
						    NULL);

		rb_source_search_basic_create_for_actions (source->priv->action_group,
							   rb_browser_source_radio_actions,
							   G_N_ELEMENTS (rb_browser_source_radio_actions));
	}
	g_object_unref (shell);

	source->priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH);

	source->priv->paned = gtk_vpaned_new ();

	source->priv->browser = rb_library_browser_new (source->priv->db, entry_type);
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->browser), TRUE, FALSE);
	gtk_container_child_set (GTK_CONTAINER (source->priv->paned),
				 GTK_WIDGET (source->priv->browser),
				 "resize", FALSE,
				 NULL);
	g_signal_connect_object (G_OBJECT (source->priv->browser), "notify::output-model",
				 G_CALLBACK (rb_browser_source_browser_changed_cb),
				 source, 0);

	/* set up songs tree view */
	source->priv->songs = rb_entry_view_new (source->priv->db, shell_player,
						 source->priv->sorting_key,
						 TRUE, FALSE);

	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_YEAR, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION, FALSE);
 	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_QUALITY, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_BPM, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_COMMENT, FALSE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LOCATION, FALSE);

	g_signal_connect_object (G_OBJECT (source->priv->songs), "show_popup",
				 G_CALLBACK (rb_browser_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->songs),
				 "sort-order-changed",
				 G_CALLBACK (songs_view_sort_order_changed_cb),
				 source, 0);

	if (source->priv->sorting_key) {
		source->priv->state_sorting_notify_id =
			eel_gconf_notification_add (source->priv->sorting_key,
					    (GConfClientNotifyFunc) rb_browser_source_state_pref_changed,
					    source);
	}
	paned_key = rb_browser_source_get_paned_key (source);
	if (paned_key) {
		source->priv->state_paned_notify_id =
			eel_gconf_notification_add (paned_key,
					    (GConfClientNotifyFunc) rb_browser_source_state_pref_changed,
					    source);
		g_free (paned_key);
	}
	browser_key = rb_source_get_browser_key (RB_SOURCE (source));
	if (browser_key) {
		source->priv->state_browser_notify_id =
			eel_gconf_notification_add (browser_key,
					    (GConfClientNotifyFunc) rb_browser_source_state_pref_changed,
					    source);
		g_free (browser_key);
	}

	rb_browser_source_state_prefs_sync (source);

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

	/* this gets emitted when the paned thingie is moved */
	g_signal_connect_object (G_OBJECT (source->priv->songs),
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 source, 0);

	gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->songs), TRUE, FALSE);

	klass = RB_BROWSER_SOURCE_GET_CLASS (source);
	klass->impl_pack_paned (source, source->priv->paned);

	gtk_widget_show_all (GTK_WIDGET (source));

	/* use a throwaway model until the real one is ready */
	rb_library_browser_set_model (source->priv->browser,
				      rhythmdb_query_model_new_empty (source->priv->db),
				      FALSE);

	source->priv->cached_all_query = rhythmdb_query_model_new_empty (source->priv->db);
	rb_browser_source_populate (source);

	g_object_unref (entry_type);
}

static void
rb_browser_source_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (object);

	switch (prop_id) {
	case PROP_SORTING_KEY:
		g_free (source->priv->sorting_key);
		source->priv->sorting_key = g_strdup (g_value_get_string (value));
		break;
	case PROP_POPULATE:
		source->priv->populate = g_value_get_boolean (value);

		/* if being set after construction, run the query now.  otherwise the constructor will do it. */
		if (source->priv->songs != NULL) {
			rb_browser_source_populate (source);
		}
		break;
	case PROP_SEARCH_TYPE:
		/* ignored */
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
	case PROP_SORTING_KEY:
		g_value_set_string (value, source->priv->sorting_key);
		break;
	case PROP_BASE_QUERY_MODEL:
		g_value_set_object (value, source->priv->cached_all_query);
		break;
	case PROP_POPULATE:
		g_value_set_boolean (value, source->priv->populate);
		break;
	case PROP_SEARCH_TYPE:
		g_value_set_enum (value, RB_SOURCE_SEARCH_INCREMENTAL);
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
rb_browser_source_cmd_choose_genre (GtkAction *action, RBSource *source)
{
	rb_debug ("choosing genre");

	if (RB_IS_BROWSER_SOURCE (source)) {
		browse_property (RB_BROWSER_SOURCE (source), RHYTHMDB_PROP_GENRE);
	}
}

static void
rb_browser_source_cmd_choose_artist (GtkAction *action, RBSource *source)
{
	rb_debug ("choosing artist");

	if (RB_IS_BROWSER_SOURCE (source)) {
		browse_property (RB_BROWSER_SOURCE (source), RHYTHMDB_PROP_ARTIST);
	}
}

static void
rb_browser_source_cmd_choose_album (GtkAction *action, RBSource *source)
{
	rb_debug ("choosing album");

	if (RB_IS_BROWSER_SOURCE (source)) {
		browse_property (RB_BROWSER_SOURCE (source), RHYTHMDB_PROP_ALBUM);
	}
}

static void
songs_view_sort_order_changed_cb (RBEntryView *view, RBBrowserSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (view);
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

	if (changed)
		rb_browser_source_do_query (source, FALSE);
}

static void
impl_delete (RBSource *asource)
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

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBBrowserSource *source)
{
	char *key = rb_browser_source_get_paned_key (source);;

	/* save state */
	rb_debug ("paned size allocate");
	if (key)
		eel_gconf_set_integer (key, gtk_paned_get_position (GTK_PANED (source->priv->paned)));
	g_free (key);
}

static void
rb_browser_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBBrowserSource *source)
{
	rb_debug ("state prefs changed");
	rb_browser_source_state_prefs_sync (source);
}

static void
rb_browser_source_state_prefs_sync (RBBrowserSource *source)
{
	char *paned_key;
	char *browser_key;

	rb_debug ("syncing state");
	paned_key = rb_browser_source_get_paned_key (source);
	if (paned_key)
		gtk_paned_set_position (GTK_PANED (source->priv->paned),
					eel_gconf_get_integer (paned_key));

	browser_key = rb_source_get_browser_key (RB_SOURCE (source));
	if (browser_key && eel_gconf_get_boolean (browser_key)) {
		gtk_widget_show (GTK_WIDGET (source->priv->browser));
	} else {
		gtk_widget_hide (GTK_WIDGET (source->priv->browser));
	}

	g_free (paned_key);
	g_free (browser_key);
}

static GList *
impl_get_search_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("BrowserSourceSearchTitles"));
	actions = g_list_prepend (actions, g_strdup ("BrowserSourceSearchAlbums"));
	actions = g_list_prepend (actions, g_strdup ("BrowserSourceSearchArtists"));
	actions = g_list_prepend (actions, g_strdup ("BrowserSourceSearchAll"));

	return actions;
}

static void
impl_browser_toggled (RBSource *asource, gboolean disclosed)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);

	if (disclosed) {
		gtk_widget_show (GTK_WIDGET (source->priv->browser));
	} else {
		gtk_widget_hide (GTK_WIDGET (source->priv->browser));
		rb_library_browser_reset (source->priv->browser);
	}
}

/**
 * rb_browser_source_get_paned_key:
 * @source: a #RBBrowserSource
 *
 * Retrieves the GConf key that stores the height of the browser pane for the source.
 * This is a virtual method that should be implemented by subclasses.
 *
 * Return value: allocated string containing the GConf key name
 */
char *
rb_browser_source_get_paned_key (RBBrowserSource *source)
{
	RBBrowserSourceClass *klass = RB_BROWSER_SOURCE_GET_CLASS (source);

	if (klass->impl_get_paned_key)
		return klass->impl_get_paned_key (source);
	else
		return NULL;
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

	return klass->impl_has_drop_support (source);
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
	rb_source_receive_drag (RB_SOURCE (source), selection_data);
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
default_pack_paned (RBBrowserSource *source, GtkWidget *paned)
{
	GtkWidget *box;

	box = gtk_vbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (box), paned, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (source), box);
}

