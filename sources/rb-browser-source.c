/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of browser-using source object
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
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <glade/glade.h>

#include "rb-source.h"
#include "rb-library-source.h"

#include "rhythmdb-query-model.h"
#include "rb-property-view.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
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
static GObject *rb_browser_source_constructor (GType type,
					       guint n_construct_properties,
					       GObjectConstructParam *construct_properties);
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
static void rb_browser_source_cmd_choose_genre (GtkAction *action,
						RBShell *shell);
static void rb_browser_source_cmd_choose_artist (GtkAction *action,
						 RBShell *shell);
static void rb_browser_source_cmd_choose_album (GtkAction *action,
						RBShell *shell);
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
static void impl_move_to_trash (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_reset_filters (RBSource *source);
static void impl_song_properties (RBSource *source);
static gboolean impl_show_popup (RBSource *source);
static GList *impl_get_search_actions (RBSource *source);
static void impl_browser_toggled (RBSource *asource, gboolean disclosed);

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

struct RBBrowserSourcePrivate
{
	RhythmDB *db;
	
	RBLibraryBrowser *browser;
	GtkWidget *vbox;

	GdkPixbuf *pixbuf;

	RBEntryView *songs;
	GtkWidget *paned;

	gboolean lock;

	char *search_text;
	RhythmDBQueryModel *cached_all_query;
	RhythmDBPropType search_prop;
	gboolean initialized;
	gboolean query_active;
	gboolean search_on_completion;

	GtkActionGroup *action_group;
	GtkActionGroup *search_action_group;
	
	RhythmDBEntryType entry_type;

	char *sorting_key;
	guint state_paned_notify_id;
	guint state_browser_notify_id;
	guint state_sorting_notify_id;
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
	{ "BrowserSourceSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), 0 },
	{ "BrowserSourceSearchArtists", NULL, N_("Artists"), NULL, N_("Search artists"), 1 },
	{ "BrowserSourceSearchAlbums", NULL, N_("Albums"), NULL, N_("Search albums"), 2 },
	{ "BrowserSourceSearchTitles", NULL, N_("Titles"), NULL, N_("Search titles"), 3 }
};

static const GtkTargetEntry songs_view_drag_types[] = {{  "text/uri-list", 0, 0 }};

enum
{
	PROP_0,
	PROP_ENTRY_TYPE,
	PROP_SORTING_KEY
};


G_DEFINE_ABSTRACT_TYPE (RBBrowserSource, rb_browser_source, RB_TYPE_SOURCE)

static void
rb_browser_source_class_init (RBBrowserSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_browser_source_dispose;
	object_class->finalize = rb_browser_source_finalize;
	object_class->constructor = rb_browser_source_constructor;

	object_class->set_property = rb_browser_source_set_property;
	object_class->get_property = rb_browser_source_get_property;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_property_views = impl_get_property_views;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_move_to_trash = impl_move_to_trash;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_search_actions = impl_get_search_actions;
	source_class->impl_browser_toggled = impl_browser_toggled;

	klass->impl_get_paned_key = (RBBrowserSourceStringFunc)rb_null_function;
	klass->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_pointer ("entry-type",
							       "Entry type",
							       "Type of the entries which should be displayed by this source",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SORTING_KEY,
					 g_param_spec_string ("sorting-key",
							      "Sorting key",
							      "GConf key for storing sort-order",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBBrowserSourcePrivate));
}

static void
rb_browser_source_init (RBBrowserSource *source)
{
	source->priv = RB_BROWSER_SOURCE_GET_PRIVATE (source);

	source->priv->search_prop = RHYTHMDB_PROP_SEARCH_MATCH;

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	/* Default value, should be overridden at object construction by the 
	 * "entry-type" property
	 */
	source->priv->entry_type = RHYTHMDB_ENTRY_TYPE_INVALID;

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
}

static void
rb_browser_source_dispose (GObject *object)
{
	RBBrowserSource *source;
	source = RB_BROWSER_SOURCE (object);

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->search_text) {
		g_free (source->priv->search_text);
		source->priv->search_text = NULL;
	}
	
	if (source->priv->cached_all_query) {
		g_object_unref (G_OBJECT (source->priv->cached_all_query));
		source->priv->cached_all_query = NULL;
	}
	
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

	rb_debug ("finalizing browser source");
	g_free (source->priv->sorting_key);
	eel_gconf_notification_remove (source->priv->state_browser_notify_id);
	eel_gconf_notification_remove (source->priv->state_paned_notify_id);
	eel_gconf_notification_remove (source->priv->state_sorting_notify_id);

	G_OBJECT_CLASS (rb_browser_source_parent_class)->finalize (object);
}

static void
rb_browser_source_songs_show_popup_cb (RBEntryView *view,
				       gboolean over_entry,
				       RBBrowserSource *source)
{
	if (over_entry)
		_rb_source_show_popup (RB_SOURCE (source), "/BrowserSourceViewPopup");
}

static RhythmDBPropType
search_action_to_prop (GtkAction *action)
{
	const char      *name;
	RhythmDBPropType prop;

	name = gtk_action_get_name (action);

	if (name == NULL) {
		prop = RHYTHMDB_PROP_SEARCH_MATCH;
	} else if (strcmp (name, "BrowserSourceSearchAll") == 0) {
		prop = RHYTHMDB_PROP_SEARCH_MATCH;		
	} else if (strcmp (name, "BrowserSourceSearchArtists") == 0) {
		prop = RHYTHMDB_PROP_ARTIST_FOLDED;
	} else if (strcmp (name, "BrowserSourceSearchAlbums") == 0) {
		prop = RHYTHMDB_PROP_ALBUM_FOLDED;
	} else if (strcmp (name, "BrowserSourceSearchTitles") == 0) {
		prop = RHYTHMDB_PROP_TITLE_FOLDED;
	} else {
		prop = RHYTHMDB_PROP_SEARCH_MATCH;
	}

	return prop;
}

static void
search_action_changed (GtkRadioAction  *action,
		       GtkRadioAction  *current,
		       RBShell         *shell)
{
	gboolean         active;
	RBBrowserSource *source;

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);

	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (current));

	if (active) {
		/* update query */
		source->priv->search_prop = search_action_to_prop (GTK_ACTION (current));
		rb_browser_source_do_query (source, FALSE);
		rb_source_notify_filter_changed (RB_SOURCE (source));
	}
}

static GObject *
rb_browser_source_constructor (GType type,
			       guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBBrowserSource *source;
	RBBrowserSourceClass *klass;
	RBShell *shell;
	GObject *shell_player;
	char *browser_key;

	klass = RB_BROWSER_SOURCE_CLASS (g_type_class_peek (RB_TYPE_BROWSER_SOURCE));

	source = RB_BROWSER_SOURCE (G_OBJECT_CLASS (rb_browser_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);
	
	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "BrowserSourceActions",
								       rb_browser_source_actions,
								       G_N_ELEMENTS (rb_browser_source_actions),
								       shell);

	gtk_action_group_add_radio_actions (source->priv->action_group,
					    rb_browser_source_radio_actions,
					    G_N_ELEMENTS (rb_browser_source_radio_actions),
					    0,
					    (GCallback)search_action_changed,
					    shell);
	g_object_unref (G_OBJECT (shell));

	source->priv->paned = gtk_vpaned_new ();

	source->priv->browser = rb_library_browser_new (source->priv->db);
	gtk_paned_pack1 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->browser), TRUE, FALSE);
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
	if (rb_browser_source_get_paned_key (source)) {
		source->priv->state_paned_notify_id =
			eel_gconf_notification_add (rb_browser_source_get_paned_key (source),
					    (GConfClientNotifyFunc) rb_browser_source_state_pref_changed,
					    source);
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
				   songs_view_drag_types, 1,
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

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);
	gtk_widget_show_all (GTK_WIDGET (source));

	source->priv->cached_all_query = rhythmdb_query_model_new_empty (source->priv->db);
	rb_library_browser_set_model (source->priv->browser, source->priv->cached_all_query, TRUE);
	rhythmdb_do_full_query_async (source->priv->db,
				      RHYTHMDB_QUERY_RESULTS (source->priv->cached_all_query),
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, source->priv->entry_type,
				      RHYTHMDB_QUERY_END);

	return G_OBJECT (source);
}


static void
rb_browser_source_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (object);

	switch (prop_id) {
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_pointer (value);
		break;
	case PROP_SORTING_KEY:
		g_free (source->priv->sorting_key);
		source->priv->sorting_key = g_strdup (g_value_get_string (value));
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
	case PROP_ENTRY_TYPE:
		g_value_set_pointer (value, source->priv->entry_type);
		break;
	case PROP_SORTING_KEY:
		g_value_set_string (value, source->priv->sorting_key);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_browser_source_cmd_choose_genre (GtkAction *action,
				    RBShell *shell)
{
	GList *props;
	RBBrowserSource *source;

	rb_debug ("choosing genre");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_source_gather_selected_properties (RB_SOURCE (source), RHYTHMDB_PROP_GENRE);
	rb_library_browser_set_selection (source->priv->browser, RHYTHMDB_PROP_GENRE, props);
	rb_list_deep_free (props);
	g_object_unref (source);
}

static void
rb_browser_source_cmd_choose_artist (GtkAction *action,
				     RBShell *shell)
{
	GList *props;	
	RBBrowserSource *source;

	rb_debug ("choosing artist");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_source_gather_selected_properties (RB_SOURCE (source), RHYTHMDB_PROP_ARTIST);
	rb_library_browser_set_selection (source->priv->browser, RHYTHMDB_PROP_ARTIST, props);
	rb_list_deep_free (props);
	g_object_unref (source);
}

static void
rb_browser_source_cmd_choose_album (GtkAction *action,
				    RBShell *shell)
{
	GList *props;	
	RBBrowserSource *source;

	rb_debug ("choosing album");

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	props = rb_source_gather_selected_properties (RB_SOURCE (source), RHYTHMDB_PROP_ALBUM);
	rb_library_browser_set_selection (source->priv->browser, RHYTHMDB_PROP_ALBUM, props);
	rb_list_deep_free (props);
	g_object_unref (source);
}

static void
songs_view_sort_order_changed_cb (RBEntryView *view, RBBrowserSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (view);
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
	char *old_search_text = NULL;
	gboolean subset = FALSE;
	const char *debug_search_text;

	if (search_text != NULL && search_text[0] == '\0')
		search_text = NULL;

	if (search_text == NULL && source->priv->search_text == NULL)
		return;
	if (search_text != NULL && source->priv->search_text != NULL
	    && !strcmp (search_text, source->priv->search_text))
		return;

	old_search_text = source->priv->search_text;
	if (search_text == NULL) {
		source->priv->search_text = NULL;
		debug_search_text = "(NULL)";
	} else {
		source->priv->search_text = g_strdup (search_text);
		debug_search_text = source->priv->search_text;

		if (old_search_text != NULL)
			subset = (g_str_has_prefix (source->priv->search_text, old_search_text));
	}
	g_free (old_search_text);
	
	/* we can't do subset searches until the original query is complete, because they
	 * reuse the query model.
	 */
	if (source->priv->query_active && subset) {
		rb_debug ("deferring search for \"%s\" until query completion", debug_search_text);
		source->priv->search_on_completion = TRUE;
	} else {
		rb_debug ("doing search for \"%s\"", debug_search_text);
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

	if (source->priv->search_text != NULL)
		changed = TRUE;
	g_free (source->priv->search_text);
	source->priv->search_text = NULL;

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
	g_list_free (sel);
}

static void
impl_move_to_trash (RBSource *asource)
{
	RBBrowserSource *source = RB_BROWSER_SOURCE (asource);
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
	const char *key = rb_browser_source_get_paned_key (source);;

	/* save state */
	rb_debug ("paned size allocate");
	if (key)
		eel_gconf_set_integer (key, gtk_paned_get_position (GTK_PANED (source->priv->paned)));
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
	const char *paned_key;
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

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/BrowserSourcePopup");
	return TRUE;
}

const char *
rb_browser_source_get_paned_key (RBBrowserSource *source)
{
	RBBrowserSourceClass *klass = RB_BROWSER_SOURCE_GET_CLASS (source);

	if (klass->impl_get_paned_key)
		return klass->impl_get_paned_key (source);
	else
		return NULL;
}

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
	
	g_object_get (G_OBJECT (browser), "output-model", &query_model, NULL);
	rb_entry_view_set_model (source->priv->songs, query_model);
	g_object_set (RB_SOURCE (source), "query-model", query_model, NULL);
	g_object_unref (G_OBJECT (query_model));
	
	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static void
rb_browser_source_query_complete_cb (RhythmDBQueryModel *query_model,
					    RBBrowserSource *source)
{
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
	RhythmDBEntryType entry_type;
	
	/* use the cached 'all' query to optimise the no-search case */
	if (!source->priv->search_text) {
		rb_library_browser_set_model (source->priv->browser,
					      source->priv->cached_all_query,
					      FALSE);
		return;
	}
	
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_PROP_LIKE,
				      source->priv->search_prop,
				      source->priv->search_text,
				      RHYTHMDB_QUERY_END);

	if (subset) {
		/* if we're appending text to an existing search string, the results will be a subset
		 * of the existing results, so we can just change the query on the existing query
		 * model and reapply the query.
		 */
		g_object_get (G_OBJECT (source->priv->browser), "input-model", &query_model, NULL);
		g_object_set (G_OBJECT (query_model), "query", query, NULL);
		rhythmdb_query_model_reapply_query (query_model, FALSE);
		g_object_unref (G_OBJECT (query_model));
	} else {/* otherwise build a query based on the search text and feed it to the browser */
		query_model = rhythmdb_query_model_new_empty (source->priv->db);
		rb_library_browser_set_model (source->priv->browser, query_model, TRUE);
		source->priv->query_active = TRUE;
		source->priv->search_on_completion = FALSE;
		g_signal_connect_object (G_OBJECT (query_model),
					 "complete", G_CALLBACK (rb_browser_source_query_complete_cb),
					 source, 0);
		rhythmdb_do_full_query_async_parsed (source->priv->db,
						     RHYTHMDB_QUERY_RESULTS (query_model),
						     query);
		g_object_unref (G_OBJECT (query_model));
	}
	rhythmdb_query_free (query);
}

