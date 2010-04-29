/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

/**
 * SECTION:rb-static-playlist-source
 * @short_description: Manually defined playlist class
 *
 * Static playlists are not defined by a query, but instead by manually selected
 * and ordered tracks.
 *
 * This class is used for static playlists built from the user's library, and is
 * also a base class for the play queue and for playlists on devices and network
 * shares.
 *
 * It has some ability to track locations that are not yet present in the database
 * and to add them to the playlist once they are added.
 */

#include "config.h"

#include <string.h>

#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-static-playlist-source.h"
#include "rb-library-browser.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-stock-icons.h"
#include "rb-file-helpers.h"
#include "rb-playlist-xml.h"
#include "rb-source-search-basic.h"

static void rb_static_playlist_source_constructed (GObject *object);
static void rb_static_playlist_source_dispose (GObject *object);
static void rb_static_playlist_source_finalize (GObject *object);
static void rb_static_playlist_source_set_property (GObject *object,
						    guint prop_id,
						    const GValue *value,
						    GParamSpec *pspec);
static void rb_static_playlist_source_get_property (GObject *object,
						    guint prop_id,
						    GValue *value,
						    GParamSpec *pspec);

/* source methods */
static GList * impl_cut (RBSource *source);
static RBTrackTransferBatch *impl_paste (RBSource *asource, GList *entries);
static void impl_delete (RBSource *source);
static void impl_search (RBSource *asource, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_browser_toggled (RBSource *source, gboolean enabled);
static void impl_reset_filters (RBSource *asource);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static GList *impl_get_search_actions (RBSource *source);
static guint impl_want_uri (RBSource *source, const char *uri);

static GPtrArray *construct_query_from_selection (RBStaticPlaylistSource *source);

/* playlist methods */
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

/* browser stuff */
static GList *impl_get_property_views (RBSource *source);
void rb_static_playlist_source_browser_views_activated_cb (GtkWidget *widget,
							 RBStaticPlaylistSource *source);
static void rb_static_playlist_source_browser_changed_cb (RBLibraryBrowser *entry,
							  GParamSpec *pspec,
							  RBStaticPlaylistSource *source);

static void rb_static_playlist_source_do_query (RBStaticPlaylistSource *source);

static void rb_static_playlist_source_add_id_list (RBStaticPlaylistSource *source,
						   GList *list);
static void rb_static_playlist_source_add_uri_list (RBStaticPlaylistSource *source,
						    GList *list);
static void rb_static_playlist_source_row_inserted (GtkTreeModel *model,
						    GtkTreePath *path,
						    GtkTreeIter *iter,
						    RBStaticPlaylistSource *source);
static gboolean rb_static_playlist_source_filter_entry_drop (RhythmDBQueryModel *model,
							     RhythmDBEntry *entry,
							     RBStaticPlaylistSource *source);
static void rb_static_playlist_source_non_entry_dropped (GtkTreeModel *model,
							 const char *uri,
							 int position,
							 RBStaticPlaylistSource *source);
static void rb_static_playlist_source_rows_reordered (GtkTreeModel *model,
						      GtkTreePath *path,
						      GtkTreeIter *iter,
						      gint *order_map,
						      RBStaticPlaylistSource *source);

static GtkRadioActionEntry rb_static_playlist_source_radio_actions [] =
{
	{ "StaticPlaylistSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), RHYTHMDB_PROP_SEARCH_MATCH },
	{ "StaticPlaylistSearchArtists", NULL, N_("Artists"), NULL, N_("Search artists"), RHYTHMDB_PROP_ARTIST_FOLDED },
	{ "StaticPlaylistSearchAlbums", NULL, N_("Albums"), NULL, N_("Search albums"), RHYTHMDB_PROP_ALBUM_FOLDED },
	{ "StaticPlaylistSearchTitles", NULL, N_("Titles"), NULL, N_("Search titles"), RHYTHMDB_PROP_TITLE_FOLDED }
};

enum
{
	PROP_0,
	PROP_BASE_QUERY_MODEL
};

G_DEFINE_TYPE (RBStaticPlaylistSource, rb_static_playlist_source, RB_TYPE_PLAYLIST_SOURCE)
#define RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), \
								RB_TYPE_STATIC_PLAYLIST_SOURCE, \
								RBStaticPlaylistSourcePrivate))

typedef struct
{
	RhythmDBQueryModel *base_model;
	RhythmDBQueryModel *filter_model;

	GtkWidget *paned;
	RBLibraryBrowser *browser;
	gboolean browser_shown;

	RBSourceSearch *default_search;
	RhythmDBQuery *search_query;

	GtkActionGroup *action_group;
	gboolean dispose_has_run;
} RBStaticPlaylistSourcePrivate;

static gpointer playlist_pixbuf = NULL;

static void
rb_static_playlist_source_class_init (RBStaticPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructed = rb_static_playlist_source_constructed;
	object_class->dispose = rb_static_playlist_source_dispose;
	object_class->finalize = rb_static_playlist_source_finalize;
	object_class->set_property = rb_static_playlist_source_set_property;
	object_class->get_property = rb_static_playlist_source_get_property;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_cut = impl_cut;
	source_class->impl_paste = impl_paste;
	source_class->impl_delete = impl_delete;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_search = impl_search;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_browser_toggled = impl_browser_toggled;
	source_class->impl_get_property_views = impl_get_property_views;
	source_class->impl_get_search_actions = impl_get_search_actions;
	source_class->impl_want_uri = impl_want_uri;

	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;

	g_object_class_override_property (object_class,
					  PROP_BASE_QUERY_MODEL,
					  "base-query-model");

	g_type_class_add_private (klass, sizeof (RBStaticPlaylistSourcePrivate));
}

static void
rb_static_playlist_source_init (RBStaticPlaylistSource *source)
{
	if (playlist_pixbuf == NULL) {
		gint size;
		gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
		playlist_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
							    RB_STOCK_PLAYLIST,
							    size,
							    0, NULL);
		if (playlist_pixbuf) {
			g_object_add_weak_pointer (playlist_pixbuf,
					   (gpointer *) &playlist_pixbuf);

			rb_source_set_pixbuf (RB_SOURCE (source), playlist_pixbuf);

			/* drop the initial reference to the icon */
			g_object_unref (playlist_pixbuf);
		}
	} else {
		rb_source_set_pixbuf (RB_SOURCE (source), playlist_pixbuf);
	}
}

static void
rb_static_playlist_source_dispose (GObject *object)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	if (priv->dispose_has_run) {
		/* If dispose did already run, return. */
		rb_debug ("Dispose has already run for static playlist source %p", object);
		return;
	}
	/* Make sure dispose does not run twice. */
	priv->dispose_has_run = TRUE;

	rb_debug ("Disposing static playlist source %p", object);

	if (priv->base_model != NULL) {
		g_object_unref (priv->base_model);
		priv->base_model = NULL;
	}

	if (priv->filter_model != NULL) {
		g_object_unref (priv->filter_model);
		priv->filter_model = NULL;
	}

	if (priv->action_group != NULL) {
		g_object_unref (priv->action_group);
		priv->action_group = NULL;
	}

	if (priv->default_search != NULL) {
		g_object_unref (priv->default_search);
		priv->default_search = NULL;
	}

	G_OBJECT_CLASS (rb_static_playlist_source_parent_class)->dispose (object);
}

static void
rb_static_playlist_source_finalize (GObject *object)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	rb_debug ("Finalizing static playlist source %p", object);

	if (priv->search_query != NULL) {
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}

	G_OBJECT_CLASS (rb_static_playlist_source_parent_class)->finalize (object);
}

static void
rb_static_playlist_source_constructed (GObject *object)
{
	RBStaticPlaylistSource *source;
	RBStaticPlaylistSourcePrivate *priv;
	RBPlaylistSource *psource;
	RBEntryView *songs;
	RBShell *shell;
	RhythmDBEntryType *entry_type;

	RB_CHAIN_GOBJECT_METHOD (rb_static_playlist_source_parent_class, constructed, object);

	source = RB_STATIC_PLAYLIST_SOURCE (object);
	priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	psource = RB_PLAYLIST_SOURCE (source);

	priv->base_model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (psource));
	g_object_set (priv->base_model, "show-hidden", TRUE, NULL);
	g_object_ref (priv->base_model);
	g_signal_connect_object (priv->base_model,
				 "filter-entry-drop",
				 G_CALLBACK (rb_static_playlist_source_filter_entry_drop),
				 source, 0);

	priv->paned = gtk_vpaned_new ();

	g_object_get (source, "shell", &shell, NULL);
	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "StaticPlaylistActions",
							       NULL, 0,
							       shell);
	if (gtk_action_group_get_action (priv->action_group,
					 rb_static_playlist_source_radio_actions[0].name) == NULL) {
		gtk_action_group_add_radio_actions (priv->action_group,
						    rb_static_playlist_source_radio_actions,
						    G_N_ELEMENTS (rb_static_playlist_source_radio_actions),
						    0,
						    NULL,
						    NULL);
		rb_source_search_basic_create_for_actions (priv->action_group,
							   rb_static_playlist_source_radio_actions,
							   G_N_ELEMENTS (rb_static_playlist_source_radio_actions));
	}
	priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH);

	g_object_unref (shell);

	g_object_get (source, "entry-type", &entry_type, NULL);
	priv->browser = rb_library_browser_new (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
						entry_type);
	if (entry_type != NULL) {
		g_object_unref (entry_type);
	}

	gtk_paned_pack1 (GTK_PANED (priv->paned), GTK_WIDGET (priv->browser), TRUE, FALSE);
	g_signal_connect_object (priv->browser, "notify::output-model",
				 G_CALLBACK (rb_static_playlist_source_browser_changed_cb),
				 source, 0);

	rb_library_browser_set_model (priv->browser, priv->base_model, FALSE);
	rb_static_playlist_source_do_query (source);

	/* reparent the entry view */
	songs = rb_source_get_entry_view (RB_SOURCE (source));
	g_object_ref (songs);
	gtk_container_remove (GTK_CONTAINER (source), GTK_WIDGET (songs));
	gtk_paned_pack2 (GTK_PANED (priv->paned), GTK_WIDGET (songs), TRUE, FALSE);
	gtk_container_add (GTK_CONTAINER (source), priv->paned);
	g_object_unref (songs);

	/* watch these to find out when things are dropped into the entry view */
	g_signal_connect_object (priv->base_model, "row-inserted",
				 G_CALLBACK (rb_static_playlist_source_row_inserted),
				 source, 0);
	g_signal_connect_object (priv->base_model, "non-entry-dropped",
				 G_CALLBACK (rb_static_playlist_source_non_entry_dropped),
				 source, 0);
	g_signal_connect_object (priv->base_model, "rows-reordered",
				 G_CALLBACK (rb_static_playlist_source_rows_reordered),
				 source, 0);

	gtk_widget_show_all (GTK_WIDGET (source));
}

/**
 * rb_static_playlist_source_new:
 * @shell: the #RBShell
 * @name: the playlist name
 * @sorting_name: the sorting name for the playlist (GConf key friendly)
 * @local: if %TRUE, the playlist is local to the library
 * @entry_type: type of database entries that can be added to the playlist.
 *
 * Creates a new static playlist source.
 *
 * Return value: new playlist.
 */
RBSource *
rb_static_playlist_source_new (RBShell *shell, const char *name, const char *sorting_name, gboolean local, RhythmDBEntryType *entry_type)
{
	if (name == NULL)
		name = "";

	if (sorting_name == NULL)
		sorting_name = "";

	return RB_SOURCE (g_object_new (RB_TYPE_STATIC_PLAYLIST_SOURCE,
					"name", name,
					"sorting-name", sorting_name,
					"shell", shell,
					"is-local", local,
					"entry-type", entry_type,
					"source-group", RB_SOURCE_GROUP_PLAYLISTS,
					"search-type", RB_SOURCE_SEARCH_INCREMENTAL,
					NULL));
}

static void
rb_static_playlist_source_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec)
{
	/*RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);*/

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_static_playlist_source_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_BASE_QUERY_MODEL:
		g_value_set_object (value, priv->base_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_static_playlist_source_load_from_xml:
 * @source: an #RBStaticPlaylistSource
 * @node: XML node to load from
 *
 * Loads the playlist contents from the specified XML document node.
 */
void
rb_static_playlist_source_load_from_xml (RBStaticPlaylistSource *source, xmlNodePtr node)
{
	xmlNodePtr child;

	for (child = node->children; child; child = child->next) {
		xmlChar *location;

		if (xmlNodeIsText (child))
			continue;

		if (xmlStrcmp (child->name, RB_PLAYLIST_LOCATION))
			continue;

		location = xmlNodeGetContent (child);
		rb_static_playlist_source_add_location (source,
						        (char *) location, -1);
		xmlFree (location);
	}
}

/**
 * rb_static_playlist_source_new_from_xml:
 * @shell: the #RBShell
 * @node: XML node containing playlist entries
 *
 * Constructs a new playlist from the given XML document node.
 *
 * Return value: playlist read from XML
 */
RBSource *
rb_static_playlist_source_new_from_xml (RBShell *shell, xmlNodePtr node)
{
	RBSource *psource = rb_static_playlist_source_new (shell,
							   NULL,
							   NULL,
							   TRUE,
							   RHYTHMDB_ENTRY_TYPE_SONG);
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (psource);

	rb_static_playlist_source_load_from_xml (source, node);

	return RB_SOURCE (source);
}

static GList *
impl_cut (RBSource *asource)
{
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);
	RBEntryView *songs = rb_source_get_entry_view (asource);
	GList *sel = rb_entry_view_get_selected_entries (songs);
	GList *tem;

	for (tem = sel; tem; tem = tem->next)
		rb_static_playlist_source_remove_entry (source, (RhythmDBEntry *) tem->data);

	return sel;
}

static RBTrackTransferBatch *
impl_paste (RBSource *asource, GList *entries)
{
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);

	for (; entries; entries = g_list_next (entries))
		rb_static_playlist_source_add_entry (source, entries->data, -1);

	return NULL;
}

static void
impl_delete (RBSource *asource)
{
	RBEntryView *songs = rb_source_get_entry_view (asource);
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rb_static_playlist_source_remove_entry (source, (RhythmDBEntry *) tem->data);
	}
	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
impl_reset_filters (RBSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	gboolean changed = FALSE;

	if (rb_library_browser_reset (priv->browser))
		changed = TRUE;

	if (priv->search_query != NULL) {
		changed = TRUE;
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}

	if (changed) {
		rb_static_playlist_source_do_query (RB_STATIC_PLAYLIST_SOURCE (source));
		rb_source_notify_filter_changed (source);
	}
}

static void
impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;

	if (search == NULL) {
		search = priv->default_search;
	}

	/* replace our search query */
	if (priv->search_query != NULL) {
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	priv->search_query = rb_source_search_create_query (search, db, new_text);

	rb_static_playlist_source_do_query (RB_STATIC_PLAYLIST_SOURCE (source));
}

static GList *
impl_get_property_views (RBSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	GList *ret;

	ret =  rb_library_browser_get_property_views (priv->browser);
	return ret;
}

static void
impl_browser_toggled (RBSource *source, gboolean enabled)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);

	priv->browser_shown = enabled;

	if (enabled)
		gtk_widget_show (GTK_WIDGET (priv->browser));
	else
		gtk_widget_hide (GTK_WIDGET (priv->browser));
}

static GPtrArray *
construct_query_from_selection (RBStaticPlaylistSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (psource));
	GPtrArray *query = NULL;

	query = g_ptr_array_new();

	if (priv->search_query != NULL) {
		rhythmdb_query_append (db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY, priv->search_query,
				       RHYTHMDB_QUERY_END);
	}

	return query;
}

static void
rb_static_playlist_source_do_query (RBStaticPlaylistSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDB *db = rb_playlist_source_get_db (psource);
	GPtrArray *query;

	if (priv->filter_model != NULL) {
		g_object_unref (priv->filter_model);
	}
	priv->filter_model = rhythmdb_query_model_new_empty (db);
	g_object_set (priv->filter_model, "base-model", priv->base_model, NULL);

	query = construct_query_from_selection (source);
	g_object_set (priv->filter_model, "query", query, NULL);
	rhythmdb_query_free (query);

	rhythmdb_query_model_reapply_query (priv->filter_model, TRUE);
	rb_library_browser_set_model (priv->browser, priv->filter_model, FALSE);
}

static void
rb_static_playlist_source_browser_changed_cb (RBLibraryBrowser *browser,
					      GParamSpec *pspec,
					      RBStaticPlaylistSource *source)
{
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));
	RhythmDBQueryModel *query_model;

	g_object_get (browser, "output-model", &query_model, NULL);
	rb_entry_view_set_model (songs, query_model);
	rb_playlist_source_set_query_model (RB_PLAYLIST_SOURCE (source), query_model);
	g_object_unref (query_model);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GdkAtom type;
	GList *list;
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);

	type = gtk_selection_data_get_data_type (data);

        if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE)) {
		list = rb_uri_list_parse ((char *)gtk_selection_data_get_data (data));
		if (list == NULL)
			return FALSE;

		if (type == gdk_atom_intern ("text/uri-list", TRUE))
			rb_static_playlist_source_add_uri_list (source, list);
		else
			rb_static_playlist_source_add_id_list (source, list);
		rb_list_deep_free (list);
	}

        return TRUE;
}

static void
impl_save_contents_to_xml (RBPlaylistSource *source,
			   xmlNodePtr node)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	GtkTreeIter iter;

	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_STATIC);

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->base_model), &iter))
		return;

	do {
		xmlNodePtr child_node = xmlNewChild (node, NULL, RB_PLAYLIST_LOCATION, NULL);
		RhythmDBEntry *entry;
		xmlChar *encoded;
		const char *location;

		gtk_tree_model_get (GTK_TREE_MODEL (priv->base_model), &iter, 0, &entry, -1);

		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		encoded = xmlEncodeEntitiesReentrant (NULL, BAD_CAST location);

		xmlNodeSetContent (child_node, encoded);

		g_free (encoded);
		rhythmdb_entry_unref (entry);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->base_model), &iter));
}

static void
rb_static_playlist_source_add_id_list (RBStaticPlaylistSource *source,
				       GList *list)
{
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	GList *i;
	gint id;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = i->next) {
		RhythmDBEntry *entry;

		id = strtoul ((const char *)i->data, NULL, 0);
		if (id == 0)
			continue;

		entry = rhythmdb_entry_lookup_by_id (rb_playlist_source_get_db (psource), id);
		if (entry == NULL) {
			rb_debug ("received id %d, but can't find the entry", id);
			continue;
		}

		rb_static_playlist_source_add_entry (source, entry, -1);
	}
}

static void
rb_static_playlist_source_add_uri_list (RBStaticPlaylistSource *source,
					GList *list)
{
	GList *i, *uri_list = NULL;
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDBEntry *entry;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = g_list_next (i)) {
		char *uri = (char *) i->data;
		uri_list = g_list_prepend (uri_list, rb_canonicalise_uri (uri));
	}

	uri_list = g_list_reverse (uri_list);
	if (uri_list == NULL)
		return;

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;
		if (uri != NULL) {
			entry = rhythmdb_entry_lookup_by_location (rb_playlist_source_get_db (psource), uri);
			if (entry == NULL)
				rhythmdb_add_uri (rb_playlist_source_get_db (psource), uri);

			rb_static_playlist_source_add_location (source, uri, -1);
		}

		g_free (uri);
	}
	g_list_free (uri_list);
}

static void
rb_static_playlist_source_add_location_internal (RBStaticPlaylistSource *source,
						 const char *location,
						 gint index)
{
	RhythmDB *db;
	RhythmDBEntry *entry;
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	if (rb_playlist_source_location_in_map (psource, location))
		return;

	db = rb_playlist_source_get_db (psource);
	entry = rhythmdb_entry_lookup_by_location (db, location);
	if (entry) {
		RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);

		if (_rb_source_check_entry_type (RB_SOURCE (source), entry)) {
			rhythmdb_entry_ref (entry);
			rhythmdb_query_model_add_entry (priv->base_model, entry, index);
			rhythmdb_entry_unref (entry);
		}
	}

	rb_playlist_source_add_to_map (psource, location);

	rb_playlist_source_mark_dirty (psource);
}

static gboolean
_add_location_cb (GFile *file,
		  gboolean dir,
		  RBStaticPlaylistSource *source)
{
	if (!dir) {
		char *uri;

		uri = g_file_get_uri (file);
		rb_static_playlist_source_add_location_internal (source, uri, -1);
		g_free (uri);
	}
	return TRUE;	
}

/**
 * rb_static_playlist_source_add_location:
 * @source: an #RBStaticPlaylistSource
 * @location: location (URI) to add to the playlist
 * @index: position at which to add the location (-1 to add at the end)
 *
 * If the location matches an entry in the database, the entry is added
 * to the playlist.  Otherwise, if it identifies a directory, the contents
 * of that directory are added.
 */
void
rb_static_playlist_source_add_location (RBStaticPlaylistSource *source,
					const char *location,
					gint index)
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	entry = rhythmdb_entry_lookup_by_location (db, location);

	/* if there is an entry, it won't be a directory */
	if (entry == NULL && rb_uri_is_directory (location))
		rb_uri_handle_recursively (location,
					   NULL,
					   (RBUriRecurseFunc) _add_location_cb,
					   source);
	else
		rb_static_playlist_source_add_location_internal (source, location, index);

}

/**
 * rb_static_playlist_source_add_locations:
 * @source: an #RBStaticPlaylistSource
 * @locations: a #GList of strings to add
 *
 * Adds the locations specified in @locations to the playlist.
 * See @rb_static_playlist_source_add_location for details.
 */
void
rb_static_playlist_source_add_locations (RBStaticPlaylistSource *source,
					 GList *locations)
{
	GList *l;

	for (l = locations; l; l = l->next) {
		const gchar *uri = (const gchar *)l->data;
		rb_static_playlist_source_add_location (source, uri, -1);
	}
}

/**
 * rb_static_playlist_source_remove_location:
 * @source: an #RBStaticPlaylistSource
 * @location: location to remove
 *
 * Removes the specified location from the playlist.  This affects both
 * the location map and the query model, whether an entry exists for the
 * location or not.
 */
void
rb_static_playlist_source_remove_location (RBStaticPlaylistSource *source,
					   const char *location)
{
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDB *db;
	RhythmDBEntry *entry;

	g_return_if_fail (rb_playlist_source_location_in_map (psource, location));

	db = rb_playlist_source_get_db (psource);
	entry = rhythmdb_entry_lookup_by_location (db, location);

	if (entry != NULL) {
		RhythmDBQueryModel *model = rb_playlist_source_get_query_model (psource);

		/* if this fails, the model and the playlist are out of sync */
		g_assert (rhythmdb_query_model_remove_entry (model, entry));
		rb_playlist_source_mark_dirty (psource);
	}
}

/**
 * rb_static_playlist_source_add_entry:
 * @source: an #RBStaticPlaylistSource
 * @entry: entry to add to the playlist
 * @index: position at which to add it (-1 to add at the end)
 *
 * Adds the specified entry to the playlist.
 */
void
rb_static_playlist_source_add_entry (RBStaticPlaylistSource *source,
				     RhythmDBEntry *entry,
				     gint index)
{
	const char *location;

	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	rb_static_playlist_source_add_location_internal (source, location, index);
}

/**
 * rb_static_playlist_source_remove_entry:
 * @source: an #RBStaticPlaylistSource
 * @entry: the entry to remove
 *
 * Removes the specified entry from the playlist.
 */
void
rb_static_playlist_source_remove_entry (RBStaticPlaylistSource *source,
					RhythmDBEntry *entry)
{
	const char *location;

	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	rb_static_playlist_source_remove_location (source, location);
}

/**
 * rb_static_playlist_source_move_entry:
 * @source: an #RBStaticPlaylistSource
 * @entry: the entry to move
 * @index: new location for the entry
 *
 * Moves an entry within the playlist.
 */
void
rb_static_playlist_source_move_entry (RBStaticPlaylistSource *source,
				      RhythmDBEntry *entry,
				      gint index)
{
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);

	rhythmdb_query_model_move_entry (priv->base_model, entry, index);

	rb_playlist_source_mark_dirty (psource);
}

static void
rb_static_playlist_source_non_entry_dropped (GtkTreeModel *model,
					     const char *uri,
					     int position,
					     RBStaticPlaylistSource *source)
{
	g_assert (g_utf8_strlen (uri, -1) > 0);

	rhythmdb_add_uri (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)), uri);
	rb_static_playlist_source_add_location (source, uri, position);
}

static void
rb_static_playlist_source_row_inserted (GtkTreeModel *model,
					GtkTreePath *path,
					GtkTreeIter *iter,
					RBStaticPlaylistSource *source)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	rb_static_playlist_source_add_entry (source, entry, -1);

	rhythmdb_entry_unref (entry);
}

static void
rb_static_playlist_source_rows_reordered (GtkTreeModel *model,
					  GtkTreePath *path,
					  GtkTreeIter *iter,
					  gint *order_map,
					  RBStaticPlaylistSource *source)
{
	rb_playlist_source_mark_dirty (RB_PLAYLIST_SOURCE (source));
}

static gboolean
rb_static_playlist_source_filter_entry_drop (RhythmDBQueryModel *model,
					     RhythmDBEntry *entry, 
					     RBStaticPlaylistSource *source)
{
	if (_rb_source_check_entry_type (RB_SOURCE (source), entry)) {
		rb_debug ("allowing drop of entry %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return TRUE;
	}
	rb_debug ("preventing drop of entry %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	return FALSE;
}

static GList *
impl_get_search_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("StaticPlaylistSearchTitles"));
	actions = g_list_prepend (actions, g_strdup ("StaticPlaylistSearchAlbums"));
	actions = g_list_prepend (actions, g_strdup ("StaticPlaylistSearchArtists"));
	actions = g_list_prepend (actions, g_strdup ("StaticPlaylistSearchAll"));

	return actions;
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	/* take anything local, on smb, or sftp */
	if (rb_uri_is_local (uri) ||
	    g_str_has_prefix (uri, "smb://") ||
	    g_str_has_prefix (uri, "sftp://"))
		return 25;	/* less than what the library returns */

	return 0;
}
