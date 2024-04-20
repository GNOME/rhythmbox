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

#include "config.h"

#include <string.h>
#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-auto-playlist-source.h"
#include "rb-library-browser.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-playlist-xml.h"
#include "rb-source-search-basic.h"
#include "rb-source-toolbar.h"
#include "rb-application.h"
#include "rb-builder-helpers.h"

/**
 * SECTION:rbautoplaylistsource
 * @short_description: automatic playlist source, based on a database query
 *
 * A playlist populated with the results of a database query.
 *
 * The query, limit, and sort settings are saved to the playlists file, so
 * they are persistent.
 *
 * Searching is implemented by appending the query criteria generated from
 * the search text to the query.  Browsing is implemented by using the base
 * query model (or a query model using the query generated from the search text,
 * there is some) as the input to a #RBLibraryBrowser.
 *
 * If the user has not set a sort order as part of the playlist definition,
 * the entry view columns are made clickable to allow the user to sort the
 * results.
 */

static void rb_auto_playlist_source_constructed (GObject *object);
static void rb_auto_playlist_source_dispose (GObject *object);
static void rb_auto_playlist_source_finalize (GObject *object);
static void rb_auto_playlist_source_set_property (GObject *object,
						  guint prop_id,
						  const GValue *value,
						  GParamSpec *pspec);
static void rb_auto_playlist_source_get_property (GObject *object,
						  guint prop_id,
						  GValue *value,
						  GParamSpec *pspec);

/* source methods */
static gboolean impl_receive_drag (RBDisplayPage *page, GtkSelectionData *data);
static void impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_reset_filters (RBSource *asource);

/* playlist methods */
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

static void rb_auto_playlist_source_songs_sort_order_changed_cb (GObject *object,
								 GParamSpec *pspec,
								 RBAutoPlaylistSource *source);
static void rb_auto_playlist_source_do_query (RBAutoPlaylistSource *source,
					      gboolean subset);

/* browser stuff */
static GList *impl_get_property_views (RBSource *source);
void rb_auto_playlist_source_browser_views_activated_cb (GtkWidget *widget,
							 RBAutoPlaylistSource *source);
static void rb_auto_playlist_source_browser_changed_cb (RBLibraryBrowser *entry,
							GParamSpec *pspec,
							RBAutoPlaylistSource *source);

enum
{
	PROP_0,
	PROP_BASE_QUERY_MODEL,
	PROP_SHOW_BROWSER
};

typedef struct _RBAutoPlaylistSourcePrivate RBAutoPlaylistSourcePrivate;

struct _RBAutoPlaylistSourcePrivate
{
	RhythmDBQueryModel *cached_all_query;
	GPtrArray *query;
	gboolean query_resetting;
	RhythmDBQueryModelLimitType limit_type;
	GVariant *limit_value;

	gboolean query_active;
	gboolean search_on_completion;

	GtkWidget *paned;
	RBLibraryBrowser *browser;
	RBSourceToolbar *toolbar;

	RBSourceSearch *default_search;
	RhythmDBQuery *search_query;
	GMenu *search_popup;
	GAction *search_action;
};

G_DEFINE_TYPE (RBAutoPlaylistSource, rb_auto_playlist_source, RB_TYPE_PLAYLIST_SOURCE)
#define GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_AUTO_PLAYLIST_SOURCE, RBAutoPlaylistSourcePrivate))

static void
rb_auto_playlist_source_class_init (RBAutoPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructed = rb_auto_playlist_source_constructed;
	object_class->dispose = rb_auto_playlist_source_dispose;
	object_class->finalize = rb_auto_playlist_source_finalize;
	object_class->set_property = rb_auto_playlist_source_set_property;
	object_class->get_property = rb_auto_playlist_source_get_property;

	page_class->receive_drag = impl_receive_drag;

	source_class->reset_filters = impl_reset_filters;
	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->search = impl_search;
	source_class->get_property_views = impl_get_property_views;

	playlist_class->save_contents_to_xml = impl_save_contents_to_xml;

	g_object_class_override_property (object_class, PROP_BASE_QUERY_MODEL, "base-query-model");
	g_object_class_override_property (object_class, PROP_SHOW_BROWSER, "show-browser");

	g_type_class_add_private (klass, sizeof (RBAutoPlaylistSourcePrivate));
}

static void
rb_auto_playlist_source_init (RBAutoPlaylistSource *source)
{
}

static void
rb_auto_playlist_source_dispose (GObject *object)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	g_clear_object (&priv->cached_all_query);
	g_clear_object (&priv->default_search);
	g_clear_object (&priv->search_popup);
	g_clear_object (&priv->search_action);

	G_OBJECT_CLASS (rb_auto_playlist_source_parent_class)->dispose (object);
}

static void
rb_auto_playlist_source_finalize (GObject *object)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	if (priv->query) {
		rhythmdb_query_free (priv->query);
	}
	
	if (priv->search_query) {
		rhythmdb_query_free (priv->search_query);
	}

	if (priv->limit_value) {
		g_variant_unref (priv->limit_value);
	}

	G_OBJECT_CLASS (rb_auto_playlist_source_parent_class)->finalize (object);
}

static void
rb_auto_playlist_source_constructed (GObject *object)
{
	RBEntryView *songs;
	RBAutoPlaylistSource *source;
	RBAutoPlaylistSourcePrivate *priv;
	RBShell *shell;
	RhythmDBEntryType *entry_type;
	GtkAccelGroup *accel_group;
	GtkWidget *grid;
	GMenu *section;
	RBApplication *app = RB_APPLICATION (g_application_get_default ());

	RB_CHAIN_GOBJECT_METHOD (rb_auto_playlist_source_parent_class, constructed, object);

	source = RB_AUTO_PLAYLIST_SOURCE (object);
	priv = GET_PRIVATE (source);

	priv->paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "folder-saved-search-symbolic");

	g_object_get (RB_PLAYLIST_SOURCE (source), "entry-type", &entry_type, NULL);
	priv->browser = rb_library_browser_new (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
						entry_type);
	g_object_unref (entry_type);
	gtk_paned_pack1 (GTK_PANED (priv->paned), GTK_WIDGET (priv->browser), TRUE, FALSE);
	gtk_widget_set_no_show_all (GTK_WIDGET (priv->browser), TRUE);
	g_signal_connect_object (G_OBJECT (priv->browser), "notify::output-model",
				 G_CALLBACK (rb_auto_playlist_source_browser_changed_cb),
				 source, 0);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	g_signal_connect_object (songs, "notify::sort-order",
				 G_CALLBACK (rb_auto_playlist_source_songs_sort_order_changed_cb),
				 source, 0);

	priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH, NULL);

	/* set up toolbar */
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "accel-group", &accel_group, NULL);
	priv->toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (source), accel_group);

	g_object_unref (accel_group);
	g_object_unref (shell);

	priv->search_action = rb_source_create_search_action (RB_SOURCE (source));
	g_action_change_state (priv->search_action, g_variant_new_string ("search-match"));
	g_action_map_add_action (G_ACTION_MAP (g_application_get_default ()), priv->search_action);

	rb_source_search_basic_register (RHYTHMDB_PROP_SEARCH_MATCH, "search-match", _("Search all fields"));
	rb_source_search_basic_register (RHYTHMDB_PROP_ARTIST_FOLDED, "artist", _("Search artists"));
	rb_source_search_basic_register (RHYTHMDB_PROP_COMPOSER_FOLDED, "composer", _("Search composers"));
	rb_source_search_basic_register (RHYTHMDB_PROP_ALBUM_FOLDED, "album", _("Search albums"));
	rb_source_search_basic_register (RHYTHMDB_PROP_TITLE_FOLDED, "title", _("Search titles"));
	rb_source_search_basic_register (RHYTHMDB_PROP_GENRE_FOLDED, "genre", _("Search genres"));
	
	section = g_menu_new ();
	rb_source_search_add_to_menu (section, "app", priv->search_action, "search-match");
	rb_source_search_add_to_menu (section, "app", priv->search_action, "genre");
	rb_source_search_add_to_menu (section, "app", priv->search_action, "artist");
	rb_source_search_add_to_menu (section, "app", priv->search_action, "composer");
	rb_source_search_add_to_menu (section, "app", priv->search_action, "album");
	rb_source_search_add_to_menu (section, "app", priv->search_action, "title");

	priv->search_popup = g_menu_new ();
	g_menu_append_section (priv->search_popup, NULL, G_MENU_MODEL (section));
	rb_source_toolbar_add_search_entry_menu (priv->toolbar, G_MENU_MODEL (priv->search_popup), priv->search_action);

	/* reparent the entry view */
	g_object_ref (songs);
	gtk_container_remove (GTK_CONTAINER (source), GTK_WIDGET (songs));
	gtk_paned_pack2 (GTK_PANED (priv->paned), GTK_WIDGET (songs), TRUE, FALSE);

	grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_widget_set_margin_top (GTK_WIDGET (grid), 6);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (priv->toolbar), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), priv->paned, 0, 1, 1, 1);
	gtk_container_add (GTK_CONTAINER (source), grid);

	rb_source_bind_settings (RB_SOURCE (source), GTK_WIDGET (songs), priv->paned, GTK_WIDGET (priv->browser), TRUE);
	g_object_unref (songs);

	g_object_set (source,
		      "playlist-menu", rb_application_get_shared_menu (app, "playlist-page-menu"),
		      NULL);

	gtk_widget_show_all (GTK_WIDGET (source));
}

/**
 * rb_auto_playlist_source_new:
 * @shell: the #RBShell instance
 * @name: the name of the new playlist
 * @local: if TRUE, the playlist will be considered local
 *
 * Creates a new automatic playlist source, initially with an empty query.
 *
 * Return value: the new source
 */
RBSource *
rb_auto_playlist_source_new (RBShell *shell, const char *name, gboolean local)
{
	RBSource *source;
	GtkBuilder *builder;
	GMenu *toolbar;

	if (name == NULL)
		name = "";

	builder = rb_builder_load ("playlist-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "playlist-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	source = RB_SOURCE (g_object_new (RB_TYPE_AUTO_PLAYLIST_SOURCE,
					  "name", name,
					  "shell", shell,
					  "is-local", local,
					  "entry-type", RHYTHMDB_ENTRY_TYPE_SONG,
					  "toolbar-menu", toolbar,
					  "settings", NULL,
					  NULL));
	g_object_unref (builder);
	return source;
}

static void
rb_auto_playlist_source_set_property (GObject *object,
				      guint prop_id,
				      const GValue *value,
				      GParamSpec *pspec)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_SHOW_BROWSER:
		if (g_value_get_boolean (value))
			gtk_widget_show (GTK_WIDGET (priv->browser));
		else
			gtk_widget_hide (GTK_WIDGET (priv->browser));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_auto_playlist_source_get_property (GObject *object,
				      guint prop_id,
				      GValue *value,
				      GParamSpec *pspec)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_BASE_QUERY_MODEL:
		g_value_set_object (value, priv->cached_all_query);
		break;
	case PROP_SHOW_BROWSER:
		g_value_set_boolean (value, gtk_widget_get_visible (GTK_WIDGET (priv->browser)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_auto_playlist_source_new_from_xml:
 * @shell: the #RBShell instance
 * @name: playlist name
 * @node: libxml node containing the playlist
 *
 * Creates a new auto playlist source by parsing an XML-encoded query.
 *
 * Return value: the new source
 */
RBSource *
rb_auto_playlist_source_new_from_xml (RBShell *shell, const char *name, xmlNodePtr node)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (rb_auto_playlist_source_new (shell, name, TRUE));
	xmlNodePtr child;
	xmlChar *tmp;
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_NONE;
	GVariant *limit_value = NULL;
	gchar *sort_key = NULL;
	gint sort_direction = 0;

	child = node->children;
	while (xmlNodeIsText (child))
		child = child->next;

	query = rhythmdb_query_deserialize (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
					    child);

	tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_COUNT);
	if (!tmp) /* Backwards compatibility */
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT);
	if (tmp) {
		guint64 l = g_ascii_strtoull ((char *)tmp, NULL, 0);
		if (l > 0) {
			limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_COUNT;
			limit_value = g_variant_new_uint64 (l);
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_SIZE);
		if (tmp) {
			guint64 l = g_ascii_strtoull ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_SIZE;
				limit_value = g_variant_new_uint64 (l);
			}
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_TIME);
		if (tmp) {
			guint64 l = g_ascii_strtoull ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_TIME;
				limit_value = g_variant_new_uint64 (l);
			}
		}
	}

	sort_key = (gchar*) xmlGetProp (node, RB_PLAYLIST_SORT_KEY);
	if (sort_key && *sort_key) {
		tmp = xmlGetProp (node, RB_PLAYLIST_SORT_DIRECTION);
		if (tmp) {
			sort_direction = atoi ((char*) tmp);
			g_free (tmp);
		}
	} else {
		g_free (sort_key);
		sort_key = NULL;
		sort_direction = 0;
	}

	rb_auto_playlist_source_set_query (source, query,
					   limit_type,
					   limit_value,
					   sort_key,
					   sort_direction);
	g_free (sort_key);
	if (limit_value)
		g_variant_unref (limit_value);
	rhythmdb_query_free (query);

	return RB_SOURCE (source);
}

static void
impl_reset_filters (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	gboolean changed = FALSE;

	if (rb_library_browser_reset (priv->browser))
		changed = TRUE;

	if (priv->search_query != NULL) {
		changed = TRUE;
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}

	rb_source_toolbar_clear_search_entry (priv->toolbar);

	if (changed)
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (source), FALSE);
}

static void
impl_search (RBSource *asource, RBSourceSearch *search, const char *cur_text, const char *new_text)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (asource);
	RhythmDB *db;
	gboolean subset;

	if (search == NULL) {
		search = priv->default_search;
	}
	
	/* replace our search query */
	if (priv->search_query != NULL) {
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (asource));
	priv->search_query = rb_source_search_create_query (search, db, new_text);

	/* if we don't have the base query yet, we can't do searches */
	if (priv->cached_all_query == NULL) {
		rb_debug ("deferring search for \"%s\" until we have the base query", new_text ? new_text : "<NULL>");
		priv->search_on_completion = TRUE;
		return;
	}

	/* we can only do subset searches once the original query is complete */
	subset = rb_source_search_is_subset (search, cur_text, new_text);
	if (priv->query_active && subset) {
		rb_debug ("deferring search for \"%s\" until query completion", new_text ? new_text : "<NULL>");
		priv->search_on_completion = TRUE;
	} else {
		rb_debug ("doing search for \"%s\"", new_text ? new_text : "<NULL>");
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (asource), subset);
	}
}

static GList *
impl_get_property_views (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	GList *ret;

	ret =  rb_library_browser_get_property_views (priv->browser);
	return ret;
}

static RhythmDBPropType
rb_auto_playlist_source_drag_atom_to_prop (GdkAtom smasher)
{
	if (smasher == gdk_atom_intern ("text/x-rhythmbox-album", TRUE))
		return RHYTHMDB_PROP_ALBUM;
	else if (smasher == gdk_atom_intern ("text/x-rhythmbox-artist", TRUE))
		return RHYTHMDB_PROP_ARTIST;
	else if (smasher == gdk_atom_intern ("text/x-rhythmbox-genre", TRUE))
		return RHYTHMDB_PROP_GENRE;
	else {
		g_assert_not_reached ();
		return 0;
	}
}

static gboolean
impl_receive_drag (RBDisplayPage *page, GtkSelectionData *data)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (page);

	GdkAtom type;
	GPtrArray *subquery = NULL;
	gchar **names;
	guint propid;
	int i;
	RhythmDB *db;

	type = gtk_selection_data_get_data_type (data);

	/* ignore URI and entry ID lists */
	if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE))
		return TRUE;

	names = g_strsplit ((char *) gtk_selection_data_get_data (data), "\r\n", 0);
	propid = rb_auto_playlist_source_drag_atom_to_prop (type);

	g_object_get (page, "db", &db, NULL);

	for (i = 0; names[i]; i++) {
		if (subquery == NULL) {
			subquery = rhythmdb_query_parse (db,
							 RHYTHMDB_QUERY_PROP_EQUALS,
							 propid,
							 names[i],
							 RHYTHMDB_QUERY_END);
		} else {
			rhythmdb_query_append (db,
					       subquery,
					       RHYTHMDB_QUERY_DISJUNCTION,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       propid,
					       names[i],
					       RHYTHMDB_QUERY_END);
		}
	}

	g_strfreev (names);

	if (subquery != NULL) {
		RhythmDBEntryType *qtype;
		GPtrArray *query;

		g_object_get (source, "entry-type", &qtype, NULL);
		if (qtype == NULL)
			qtype = g_object_ref (RHYTHMDB_ENTRY_TYPE_SONG);

		query = rhythmdb_query_parse (db,
					      RHYTHMDB_QUERY_PROP_EQUALS,
					      RHYTHMDB_PROP_TYPE,
					      qtype,
					      RHYTHMDB_QUERY_SUBQUERY,
					      subquery,
					      RHYTHMDB_QUERY_END);
		rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (source), query,
						   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
						   NULL, 0);

		rhythmdb_query_free (subquery);
		rhythmdb_query_free (query);
		g_object_unref (qtype);
	}

	g_object_unref (db);

	return TRUE;
}

static void
_save_write_uint64 (xmlNodePtr node, GVariant *limit_value, const xmlChar *key)
{
	guint64 l;
	gchar *str;

	l = g_variant_get_uint64 (limit_value);
	str = g_strdup_printf ("%" G_GUINT64_FORMAT, l);
	xmlSetProp (node, key, BAD_CAST str);
	g_free (str);
}

static void
impl_save_contents_to_xml (RBPlaylistSource *psource,
			   xmlNodePtr node)
{
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type;
	GVariant *limit_value = NULL;
	char *sort_key;
	gint sort_direction;
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (psource);

	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_AUTOMATIC);

	sort_key = NULL;
	rb_auto_playlist_source_get_query (source,
					   &query,
					   &limit_type,
					   &limit_value,
					   &sort_key,
					   &sort_direction);

	switch (limit_type) {
	case RHYTHMDB_QUERY_MODEL_LIMIT_NONE:
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_COUNT:
		_save_write_uint64 (node, limit_value, RB_PLAYLIST_LIMIT_COUNT);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_SIZE:
		_save_write_uint64 (node, limit_value, RB_PLAYLIST_LIMIT_SIZE);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_TIME:
		_save_write_uint64 (node, limit_value, RB_PLAYLIST_LIMIT_TIME);
		break;

	default:
		g_assert_not_reached ();
	}

	if (sort_key && *sort_key) {
		char *temp_str;

		xmlSetProp (node, RB_PLAYLIST_SORT_KEY, BAD_CAST sort_key);
		temp_str = g_strdup_printf ("%d", sort_direction);
		xmlSetProp (node, RB_PLAYLIST_SORT_DIRECTION, BAD_CAST temp_str);

		g_free (temp_str);
	}

	rhythmdb_query_serialize (rb_playlist_source_get_db (psource), query, node);
	rhythmdb_query_free (query);

	if (limit_value != NULL) {
		g_variant_unref (limit_value);
	}
	g_free (sort_key);
}

static void
rb_auto_playlist_source_query_complete_cb (RhythmDBQueryModel *model,
					   RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	priv->query_active = FALSE;
	if (priv->search_on_completion) {
		priv->search_on_completion = FALSE;
		rb_debug ("performing deferred search");
		/* this is only done for subset searches */
		rb_auto_playlist_source_do_query (source, TRUE);
	}
}

static void
rb_auto_playlist_source_do_query (RBAutoPlaylistSource *source, gboolean subset)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	GPtrArray *query;

	/* this doesn't add a ref */
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));

	g_assert (priv->cached_all_query);

	if (priv->search_query == NULL) {
		rb_library_browser_set_model (priv->browser,
					      priv->cached_all_query,
					      FALSE);
		return;
	}

	query = rhythmdb_query_copy (priv->query);
	rhythmdb_query_append (db, query,
			       RHYTHMDB_QUERY_SUBQUERY, priv->search_query,
			       RHYTHMDB_QUERY_END);

	g_object_get (priv->browser, "input-model", &query_model, NULL);

	if (subset && query_model != priv->cached_all_query) {
		/* just apply the new query to the existing query model */
		g_object_set (query_model, "query", query, NULL);
		rhythmdb_query_model_reapply_query (query_model, FALSE);
		g_object_unref (query_model);
	} else {
		/* otherwise, we need a new query model */
		g_object_unref (query_model);

		query_model = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
					    "db", db,
					    "limit-type", priv->limit_type,
					    "limit-value", priv->limit_value,
					    NULL);
		rhythmdb_query_model_chain (query_model, priv->cached_all_query, FALSE);
		rb_library_browser_set_model (priv->browser, query_model, TRUE);

		priv->query_active = TRUE;
		priv->search_on_completion = FALSE;
		g_signal_connect_object (G_OBJECT (query_model),
					 "complete", G_CALLBACK (rb_auto_playlist_source_query_complete_cb),
					 source, 0);
		rhythmdb_do_full_query_async_parsed (db,
						     RHYTHMDB_QUERY_RESULTS (query_model),
						     query);
		g_object_unref (query_model);
	}

	rhythmdb_query_free (query);
}

/**
 * rb_auto_playlist_source_set_query: (skip)
 * @source: the #RBAutoPlaylistSource
 * @query: (transfer none): the new database query
 * @limit_type: the playlist limit type
 * @limit_value: the playlist limit value
 * @sort_key: the sorting key
 * @sort_order: the sorting direction (as a #GtkSortType)
 *
 * Sets the database query used to populate the playlist, and also the limit on
 * playlist size, and the sorting type used.
 */
void
rb_auto_playlist_source_set_query (RBAutoPlaylistSource *source,
				   GPtrArray *query,
				   RhythmDBQueryModelLimitType limit_type,
				   GVariant *limit_value,
				   const char *sort_key,
				   gint sort_order)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));

	priv->query_resetting = TRUE;
	if (priv->query) {
		rhythmdb_query_free (priv->query);
	}

	if (priv->cached_all_query) {
		g_object_unref (G_OBJECT (priv->cached_all_query));
	}

	if (priv->limit_value) {
		g_variant_unref (priv->limit_value);
	}

	/* playlists that aren't limited, with a particular sort order, are user-orderable */
	rb_entry_view_set_columns_clickable (songs, (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE));
	rb_entry_view_set_sorting_order (songs, sort_key, sort_order);

	priv->query = rhythmdb_query_copy (query);
	priv->limit_type = limit_type;
	priv->limit_value = limit_value ? g_variant_ref (limit_value) : NULL;

	priv->cached_all_query = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
					       "db", db,
					       "limit-type", priv->limit_type,
					       "limit-value", priv->limit_value,
					       NULL);
	rb_library_browser_set_model (priv->browser, priv->cached_all_query, TRUE);
	rhythmdb_do_full_query_async_parsed (db,
					     RHYTHMDB_QUERY_RESULTS (priv->cached_all_query),
					     priv->query);

	priv->query_resetting = FALSE;
}

/**
 * rb_auto_playlist_source_get_query: (skip)
 * @source: the #RBAutoPlaylistSource
 * @query: (out caller-allocates) (transfer full): returns the database query for the playlist
 * @limit_type: (out callee-allocates): returns the playlist limit type
 * @limit_value: (out) (transfer full): returns the playlist limit value
 * @sort_key: (out callee-allocates) (transfer full): returns the playlist sorting key
 * @sort_order: (out callee-allocates): returns the playlist sorting direction (as a #GtkSortType)
 *
 * Extracts the current query, playlist limit, and sorting settings for the playlist.
 */
void
rb_auto_playlist_source_get_query (RBAutoPlaylistSource *source,
				   GPtrArray **query,
				   RhythmDBQueryModelLimitType *limit_type,
				   GVariant **limit_value,
				   char **sort_key,
				   gint *sort_order)
{
	RBAutoPlaylistSourcePrivate *priv;
	RBEntryView *songs;

 	g_return_if_fail (RB_IS_AUTO_PLAYLIST_SOURCE (source));

	priv = GET_PRIVATE (source);
	songs = rb_source_get_entry_view (RB_SOURCE (source));

	*query = rhythmdb_query_copy (priv->query);
	*limit_type = priv->limit_type;
	*limit_value = (priv->limit_value) ? g_variant_ref (priv->limit_value) : NULL;

	rb_entry_view_get_sorting_order (songs, sort_key, sort_order);
}

static void
rb_auto_playlist_source_songs_sort_order_changed_cb (GObject *object, GParamSpec *pspec, RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	/* don't process this if we are in the middle of setting a query */
	if (priv->query_resetting)
		return;
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
rb_auto_playlist_source_browser_changed_cb (RBLibraryBrowser *browser,
					    GParamSpec *pspec,
					    RBAutoPlaylistSource *source)
{
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));
	RhythmDBQueryModel *query_model;

	g_object_get (browser, "output-model", &query_model, NULL);
	rb_entry_view_set_model (songs, query_model);
	rb_playlist_source_set_query_model (RB_PLAYLIST_SOURCE (source), query_model);
	g_object_unref (query_model);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

