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
#include <libgnomevfs/gnome-vfs-uri.h>

#include "rb-auto-playlist-source.h"
#include "rb-library-browser.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-stock-icons.h"
#include "rb-playlist-xml.h"

static GObject *rb_auto_playlist_source_constructor (GType type, guint n_construct_properties,
						      GObjectConstructParam *construct_properties);
static void rb_auto_playlist_source_finalize (GObject *object);

/* source methods */
static gboolean impl_show_popup (RBSource *source);
static gboolean impl_receive_drag (RBSource *asource, GtkSelectionData *data);
static void impl_search (RBSource *asource, const char *search_text);
static void impl_reset_filters (RBSource *asource);
static void impl_browser_toggled (RBSource *source, gboolean enabled);
static GList *impl_get_search_actions (RBSource *source);

/* playlist methods */
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

static void rb_auto_playlist_source_songs_sort_order_changed_cb (RBEntryView *view,
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
static void search_action_changed (GtkRadioAction *action,
				   GtkRadioAction *current,
				   RBShell *shell);

static GtkRadioActionEntry rb_auto_playlist_source_radio_actions [] =
{
	{ "AutoPlaylistSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), 0 },
	{ "AutoPlaylistSearchArtists", NULL, N_("Artists"), NULL, N_("Search artists"), 1 },
	{ "AutoPlaylistSearchAlbums", NULL, N_("Albums"), NULL, N_("Search albums"), 2 },
	{ "AutoPlaylistSearchTitles", NULL, N_("Titles"), NULL, N_("Search titles"), 3 }
};

#define AUTO_PLAYLIST_SOURCE_POPUP_PATH "/AutoPlaylistSourcePopup"

typedef struct _RBAutoPlaylistSourcePrivate RBAutoPlaylistSourcePrivate;

struct _RBAutoPlaylistSourcePrivate
{
	RhythmDBQueryModel *cached_all_query;
	GPtrArray *query;
	gboolean query_resetting;
	RhythmDBQueryModelLimitType limit_type;
	GValueArray *limit_value;

	gboolean query_active;
	gboolean search_on_completion;

	GtkWidget *paned;
	RBLibraryBrowser *browser;
	gboolean browser_shown;

	char *search_text;

	GtkActionGroup *action_group;
	RhythmDBPropType search_prop;
};

G_DEFINE_TYPE (RBAutoPlaylistSource, rb_auto_playlist_source, RB_TYPE_PLAYLIST_SOURCE)
#define RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_AUTO_PLAYLIST_SOURCE, RBAutoPlaylistSourcePrivate))

static void
rb_auto_playlist_source_class_init (RBAutoPlaylistSourceClass *klass)
{
	gint size;

	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructor = rb_auto_playlist_source_constructor;
	object_class->finalize = rb_auto_playlist_source_finalize;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_browser_toggled = impl_browser_toggled;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_get_property_views = impl_get_property_views;
	source_class->impl_get_search_actions = impl_get_search_actions;

	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;

	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	klass->pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						  GNOME_MEDIA_AUTO_PLAYLIST,
						  size,
						  0, NULL);

	g_type_class_add_private (klass, sizeof (RBAutoPlaylistSourcePrivate));
}

static void
rb_auto_playlist_source_init (RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourceClass *klass = RB_AUTO_PLAYLIST_SOURCE_GET_CLASS (source);

	rb_source_set_pixbuf (RB_SOURCE (source), klass->pixbuf);
}

static void
rb_auto_playlist_source_finalize (GObject *object)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (object);

	if (priv->cached_all_query)
		g_object_unref (G_OBJECT (priv->cached_all_query));

	if (priv->query)
		rhythmdb_query_free (priv->query);

	if (priv->limit_value)
		g_value_array_free (priv->limit_value);

	g_free (priv->search_text);

	G_OBJECT_CLASS (rb_auto_playlist_source_parent_class)->finalize (object);
}

static GObject *
rb_auto_playlist_source_constructor (GType type, guint n_construct_properties,
				      GObjectConstructParam *construct_properties)
{
	RBEntryView *songs;
	RBAutoPlaylistSource *source;
	GObjectClass *parent_class = G_OBJECT_CLASS (rb_auto_playlist_source_parent_class);
	RBAutoPlaylistSourcePrivate *priv;
	RBShell *shell;
	RhythmDBEntryType entry_type;

	source = RB_AUTO_PLAYLIST_SOURCE (
			parent_class->constructor (type, n_construct_properties, construct_properties));
	priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);

	priv->paned = gtk_vpaned_new ();

	g_object_get (RB_PLAYLIST_SOURCE (source), "entry-type", &entry_type, NULL);
	priv->browser = rb_library_browser_new (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
						entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	gtk_paned_pack1 (GTK_PANED (priv->paned), GTK_WIDGET (priv->browser), TRUE, FALSE);
	g_signal_connect_object (G_OBJECT (priv->browser), "notify::output-model",
				 G_CALLBACK (rb_auto_playlist_source_browser_changed_cb),
				 source, 0);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	g_signal_connect_object (G_OBJECT (songs), "sort-order-changed",
				 G_CALLBACK (rb_auto_playlist_source_songs_sort_order_changed_cb),
				 source, 0);

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "AutoPlaylistActions",
							       NULL, 0,
							       shell);
	gtk_action_group_add_radio_actions (priv->action_group,
					    rb_auto_playlist_source_radio_actions,
					    G_N_ELEMENTS (rb_auto_playlist_source_radio_actions),
					    0,
					    (GCallback)search_action_changed,
					    shell);
	priv->search_prop = RHYTHMDB_PROP_SEARCH_MATCH;
	g_object_unref (G_OBJECT (shell));

	/* reparent the entry view */
	g_object_ref (songs);
	gtk_container_remove (GTK_CONTAINER (source), GTK_WIDGET (songs));
	gtk_paned_pack2 (GTK_PANED (priv->paned), GTK_WIDGET (songs), TRUE, FALSE);
	gtk_container_add (GTK_CONTAINER (source), priv->paned);
	g_object_unref (songs);

	gtk_widget_show_all (GTK_WIDGET (source));

	return G_OBJECT (source);
}

RBSource *
rb_auto_playlist_source_new (RBShell *shell, const char *name, gboolean local)
{
	if (name == NULL)
		name = "";

	return RB_SOURCE (g_object_new (RB_TYPE_AUTO_PLAYLIST_SOURCE,
					"name", name,
					"shell", shell,
					"is-local", local,
					"entry-type", RHYTHMDB_ENTRY_TYPE_SONG,
					"sourcelist-group", RB_SOURCELIST_GROUP_PERSISTANT,
					NULL));
}

RBSource *
rb_auto_playlist_source_new_from_xml (RBShell *shell, xmlNodePtr node)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (rb_auto_playlist_source_new (shell, NULL, TRUE));
	xmlNodePtr child;
	xmlChar *tmp;
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_NONE;
	GValueArray *limit_value = NULL;
	gchar *sort_key = NULL;
	gint sort_direction = 0;
	GValue val = {0,};

	child = node->children;
	while (xmlNodeIsText (child))
		child = child->next;

	query = rhythmdb_query_deserialize (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
					    child);

	limit_value = g_value_array_new (0);
	tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_COUNT);
	if (!tmp) /* Backwards compatibility */
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT);
	if (tmp) {
		gulong l = strtoul ((char *)tmp, NULL, 0);
		if (l > 0) {
			limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_COUNT;

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, l);
			g_value_array_append (limit_value, &val);
			g_free (tmp);
			g_value_unset (&val);
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_SIZE);
		if (tmp) {
			guint64 l = g_ascii_strtoull ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_SIZE;

				g_value_init (&val, G_TYPE_UINT64);
				g_value_set_uint64 (&val, l);
				g_value_array_append (limit_value, &val);
				g_free (tmp);
				g_value_unset (&val);
			}
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_TIME);
		if (tmp) {
			gulong l = strtoul ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_TIME;

				g_value_init (&val, G_TYPE_ULONG);
				g_value_set_ulong (&val, l);
				g_value_array_append (limit_value, &val);
				g_free (tmp);
				g_value_unset (&val);
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
	g_value_array_free (limit_value);
	rhythmdb_query_free (query);

	return RB_SOURCE (source);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (source, AUTO_PLAYLIST_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
impl_reset_filters (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
	gboolean changed = FALSE;

	if (rb_library_browser_reset (priv->browser))
		changed = TRUE;

	if (priv->search_text != NULL) {
		changed = TRUE;
		g_free (priv->search_text);
		priv->search_text = NULL;
	}

	if (changed)
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (source), FALSE);
}

static void
impl_search (RBSource *source, const char *search_text)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
	char *old_search_text = NULL;
	gboolean subset = FALSE;
	const char *debug_search_text;

	if (search_text != NULL && search_text[0] == '\0')
		search_text = NULL;

	if (search_text == NULL && priv->search_text == NULL)
		return;
	if (search_text != NULL && priv->search_text != NULL
	    && !strcmp (search_text, priv->search_text))
		return;

	old_search_text = priv->search_text;
	if (search_text == NULL) {
		priv->search_text = NULL;
		debug_search_text = "(NULL)";
	} else {
		priv->search_text = g_strdup (search_text);
		debug_search_text = priv->search_text;

		if (old_search_text != NULL)
			subset = (g_str_has_prefix (priv->search_text, old_search_text));
	}
	g_free (old_search_text);

	/* we can only do subset searches once the original query is complete */
	if (priv->query_active && subset) {
		rb_debug ("deferring search for \"%s\" until query completion", debug_search_text);
		priv->search_on_completion = TRUE;
	} else {
		rb_debug ("doing search for \"%s\"", debug_search_text);
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (source), subset);
	}
}

static GList *
impl_get_property_views (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
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

static void
impl_browser_toggled (RBSource *source, gboolean enabled)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);

	priv->browser_shown = enabled;

	if (enabled)
		gtk_widget_show (GTK_WIDGET (priv->browser));
	else
		gtk_widget_hide (GTK_WIDGET (priv->browser));
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (asource);

	GPtrArray *subquery = NULL;
	gchar **names;
	guint propid;
	int i;
	RhythmDB *db;

	/* ignore URI lists */
	if (data->type == gdk_atom_intern ("text/uri-list", TRUE))
		return TRUE;

	names = g_strsplit ((char *)data->data, "\r\n", 0);
	propid = rb_auto_playlist_source_drag_atom_to_prop (data->type);
	g_object_get (G_OBJECT (asource), "db", &db, NULL);

	for (i=0; names[i]; i++) {
		if (subquery == NULL)
			subquery = rhythmdb_query_parse (db,
							 RHYTHMDB_QUERY_PROP_EQUALS,
							 propid,
							 names[i],
							 RHYTHMDB_QUERY_END);
		else
			rhythmdb_query_append (db,
					       subquery,
					       RHYTHMDB_QUERY_DISJUNCTION,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       propid,
					       names[i],
					       RHYTHMDB_QUERY_END);
	}

	g_strfreev (names);

	if (subquery) {
		RhythmDBEntryType qtype;
		GPtrArray *query;

		g_object_get (G_OBJECT (source), "entry-type", &qtype, NULL);
		if (qtype == RHYTHMDB_ENTRY_TYPE_INVALID)
			qtype = RHYTHMDB_ENTRY_TYPE_SONG;

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
	}

	g_object_unref (G_OBJECT (db));

	return TRUE;
}

static void
_save_write_ulong (xmlNodePtr node, GValueArray *limit_value, const xmlChar *key)
{
	gulong l;
	gchar *str;

	l = g_value_get_ulong (g_value_array_get_nth (limit_value, 0));
	str = g_strdup_printf ("%u", (guint)l);
	xmlSetProp (node, key, BAD_CAST str);
	g_free (str);
}

static void
_save_write_uint64 (xmlNodePtr node, GValueArray *limit_value, const xmlChar *key)
{
	guint64 l;
	gchar *str;

	l = g_value_get_uint64 (g_value_array_get_nth (limit_value, 0));
	str = g_strdup_printf ("%" G_GUINT64_FORMAT, l);
	xmlSetProp (node, key, BAD_CAST str);
	g_free (str);
}

static void
impl_save_contents_to_xml (RBPlaylistSource *psource, xmlNodePtr node)
{
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type;
	GValueArray *limit_value = NULL;
	const gchar *sort_key;
	gint sort_direction;
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (psource);

	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_AUTOMATIC);

	rb_auto_playlist_source_get_query (source,
					    &query,
					    &limit_type, &limit_value,
					    &sort_key, &sort_direction);

	switch (limit_type) {
	case RHYTHMDB_QUERY_MODEL_LIMIT_NONE:
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_COUNT:
		_save_write_ulong (node, limit_value, RB_PLAYLIST_LIMIT_COUNT);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_SIZE:
		_save_write_uint64 (node, limit_value, RB_PLAYLIST_LIMIT_SIZE);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_TIME:
		_save_write_ulong (node, limit_value, RB_PLAYLIST_LIMIT_TIME);
		break;

	default:
		g_assert_not_reached ();
	}

	if (sort_key && *sort_key) {
		gchar *temp_str;

		xmlSetProp (node, RB_PLAYLIST_SORT_KEY, BAD_CAST sort_key);
		temp_str = g_strdup_printf ("%d", sort_direction);
		xmlSetProp (node, RB_PLAYLIST_SORT_DIRECTION, BAD_CAST temp_str);
		g_free (temp_str);
	}

	rhythmdb_query_serialize (rb_playlist_source_get_db (psource), query, node);
	rhythmdb_query_free (query);
}

static void
rb_auto_playlist_source_query_complete_cb (RhythmDBQueryModel *model,
					   RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);

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
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	RhythmDBQueryModel *query_model;
	GPtrArray *query;

	g_assert (priv->cached_all_query);

	if (!priv->search_text) {
		rb_library_browser_set_model (priv->browser,
					      priv->cached_all_query,
					      FALSE);
		return;
	}

	query = rhythmdb_query_copy (priv->query);
	rhythmdb_query_append (db, query,
			       RHYTHMDB_QUERY_PROP_LIKE, priv->search_prop, priv->search_text,
			       RHYTHMDB_QUERY_END);

	if (subset) {
		/* just apply the new query to the existing query model */
		g_object_get (G_OBJECT (priv->browser), "input-model", &query_model, NULL);
		g_assert (query_model != priv->cached_all_query);
		g_object_set (G_OBJECT (query_model), "query", query, NULL);
		rhythmdb_query_model_reapply_query (query_model, FALSE);
		g_object_unref (G_OBJECT (query_model));
	} else {
		/* otherwise, we need a new query model */
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
		g_object_unref (G_OBJECT (query_model));
	}
	rhythmdb_query_free (query);
}

void
rb_auto_playlist_source_set_query (RBAutoPlaylistSource *source,
				   GPtrArray *query,
				   RhythmDBQueryModelLimitType limit_type,
				   GValueArray *limit_value,
				   const char *sort_key,
				   gint sort_direction)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));

	priv->query_resetting = TRUE;
	if (priv->query)
		rhythmdb_query_free (priv->query);
	if (priv->cached_all_query)
		g_object_unref (G_OBJECT (priv->cached_all_query));

	/* playlists that aren't limited, with a particular sort order, are user-orderable */
	rb_entry_view_set_columns_clickable (songs, (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE));
	rb_entry_view_set_sorting_order (songs, sort_key, sort_direction);

	priv->query = rhythmdb_query_copy (query);
	priv->limit_type = limit_type;
	if (priv->limit_value)
		g_value_array_free (priv->limit_value);
	priv->limit_value = limit_value ? g_value_array_copy (limit_value) : NULL;

	priv->cached_all_query = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
					      "db", db,
					      "limit-type", priv->limit_type,
					      "limit-value", priv->limit_value,
					      NULL);
	rb_library_browser_set_model (priv->browser, priv->cached_all_query, TRUE);
	rhythmdb_do_full_query_async_parsed (db,
					     RHYTHMDB_QUERY_RESULTS (priv->cached_all_query),
					     query);

	priv->query_resetting = FALSE;
}

void
rb_auto_playlist_source_get_query (RBAutoPlaylistSource *source,
				    GPtrArray **query,
				    RhythmDBQueryModelLimitType *limit_type,
				    GValueArray **limit_value,
				    const char **sort_key,
				    gint *sort_direction)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));

	*query = rhythmdb_query_copy (priv->query);
	*limit_type = priv->limit_type;
	*limit_value = (priv->limit_value) ? g_value_array_copy (priv->limit_value) : NULL;

	rb_entry_view_get_sorting_order (songs, sort_key, sort_direction);
}

static void
rb_auto_playlist_source_songs_sort_order_changed_cb (RBEntryView *view, RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);

	/* don't process this if we are in the middle of setting a query */
	if (priv->query_resetting)
		return;
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (view);
}

static void
rb_auto_playlist_source_browser_changed_cb (RBLibraryBrowser *browser,
					    GParamSpec *pspec,
					    RBAutoPlaylistSource *source)
{
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));
	RhythmDBQueryModel *query_model;

	g_object_get (G_OBJECT (browser), "output-model", &query_model, NULL);
	rb_entry_view_set_model (songs, query_model);
	rb_playlist_source_set_query_model (RB_PLAYLIST_SOURCE (source), query_model);
	g_object_unref (G_OBJECT (query_model));

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static GList *
impl_get_search_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchTitles"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchAlbums"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchArtists"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchAll"));

	return actions;
}

static RhythmDBPropType
search_action_to_prop (GtkAction *action)
{
	const char      *name;
	RhythmDBPropType prop;

	name = gtk_action_get_name (action);

	if (name == NULL) {
		prop = RHYTHMDB_PROP_SEARCH_MATCH;
	} else if (strcmp (name, "AutoPlaylistSearchAll") == 0) {
		prop = RHYTHMDB_PROP_SEARCH_MATCH;
	} else if (strcmp (name, "AutoPlaylistSearchArtists") == 0) {
		prop = RHYTHMDB_PROP_ARTIST_FOLDED;
	} else if (strcmp (name, "AutoPlaylistSearchAlbums") == 0) {
		prop = RHYTHMDB_PROP_ALBUM_FOLDED;
	} else if (strcmp (name, "AutoPlaylistSearchTitles") == 0) {
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
	RBAutoPlaylistSourcePrivate *priv;
	gboolean active;
	RBAutoPlaylistSource *source;

	g_object_get (G_OBJECT (shell), "selected-source", &source, NULL);
	priv = RB_AUTO_PLAYLIST_SOURCE_GET_PRIVATE (source);

	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (current));

	if (active) {
		/* update query */
		priv->search_prop = search_action_to_prop (GTK_ACTION (current));
		rb_auto_playlist_source_do_query (source, FALSE);
		rb_source_notify_filter_changed (RB_SOURCE (source));
	}
}
