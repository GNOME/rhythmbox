/*
 *  arch-tag: Implementation of playlist source object
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <unistd.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-playlist.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-tree-view-column.h"

#define RB_PLAYLIST_XML_VERSION "1.0"

static void rb_playlist_source_class_init (RBPlaylistSourceClass *klass);
static void rb_playlist_source_init (RBPlaylistSource *source);
static GObject *rb_playlist_source_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties);
static void rb_playlist_source_finalize (GObject *object);
static void rb_playlist_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_playlist_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static gboolean impl_can_cut (RBSource *asource);
static GList * impl_cut (RBSource *source);
static void impl_paste (RBSource *asource, GList *entries);
static void impl_delete (RBSource *source);
static void impl_song_properties (RBSource *source);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_show_popup (RBSource *source);
static void rb_playlist_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
					       RBPlaylistSource *source);

static void rb_playlist_source_songs_show_popup_cb (RBEntryView *view, RBPlaylistSource *playlist_view);
static void rb_playlist_source_drop_cb (GtkWidget *widget,
				     GdkDragContext *context,
				     gint x,
				     gint y,
				     GtkSelectionData *data,
				     guint info,
				     guint time,
				     gpointer user_data);
static void rb_playlist_source_add_list_uri (RBPlaylistSource *source,
					  GList *list);

#define PLAYLIST_SOURCE_SONGS_POPUP_PATH "/popups/PlaylistSongsList"
#define PLAYLIST_SOURCE_POPUP_PATH "/popups/PlaylistSourceList"

struct RBPlaylistSourcePrivate
{
	RhythmDB *db;

	gboolean automatic;
	GHashTable *entries;

	RhythmDBQueryModel *model;

	GtkWidget *vbox;
	GdkPixbuf *normal_pixbuf;
	GdkPixbuf *smartypants_pixbuf;

	RBEntryView *songs;

	char *title;
	char *status;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
	PROP_DB,
	PROP_AUTOMATIC,
};

static GObjectClass *parent_class = NULL;

static const GtkTargetEntry target_uri [] = { { "text/uri-list", 0, 0 } };

GType
rb_playlist_source_get_type (void)
{
	static GType rb_playlist_source_type = 0;

	if (rb_playlist_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlaylistSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_playlist_source_class_init,
			NULL,
			NULL,
			sizeof (RBPlaylistSource),
			0,
			(GInstanceInitFunc) rb_playlist_source_init
		};

		rb_playlist_source_type = g_type_register_static (RB_TYPE_SOURCE,
								  "RBPlaylistSource",
								  &our_info, 0);
	}

	return rb_playlist_source_type;
}

static void
rb_playlist_source_class_init (RBPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_playlist_source_finalize;
	object_class->constructor = rb_playlist_source_constructor;

	object_class->set_property = rb_playlist_source_set_property;
	object_class->get_property = rb_playlist_source_get_property;

	source_class->impl_get_status = impl_get_status;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_cut = impl_can_cut;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = impl_can_cut;
	source_class->impl_cut = impl_cut;
	source_class->impl_paste = impl_paste;
	source_class->impl_delete = impl_delete;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_AUTOMATIC,
					 g_param_spec_boolean ("automatic",
							       "automatic",
							       "whether this playlist is a smartypants",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_playlist_source_track_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model, GtkTreeIter *iter,
					 RBPlaylistSource *source)
{
	char *str;
	int val;

	gtk_tree_model_get (tree_model, iter, 1, &val, -1);

	if (val >= 0)
		str = g_strdup_printf ("%d", val);
	else
		str = g_strdup ("");

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);
}


static GObject *
rb_playlist_source_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBPlaylistSource *source;
	RBPlaylistSourceClass *klass;
	GObjectClass *parent_class;  
	GtkWidget *dummy = gtk_tree_view_new ();

	klass = RB_PLAYLIST_SOURCE_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	source = RB_PLAYLIST_SOURCE (parent_class->constructor (type, n_construct_properties,
								construct_properties));

	g_signal_connect (G_OBJECT (source->priv->db), "entry_added",
			  G_CALLBACK (rb_playlist_source_entry_added_cb),
			  source);
	g_signal_connect (G_OBJECT (source->priv->db), "entry_restored",
			  G_CALLBACK (rb_playlist_source_entry_added_cb),
			  source);

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
		
	source->priv->model = rhythmdb_query_model_new_empty (source->priv->db);

	source->priv->entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		
	source->priv->songs = rb_entry_view_new (source->priv->db, NULL, TRUE, TRUE);

	rb_entry_view_set_model (source->priv->songs, RHYTHMDB_QUERY_MODEL (source->priv->model));

	{
		GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN (rb_tree_view_column_new ());
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_pack_start (column, renderer, TRUE);

		gtk_tree_view_column_set_clickable (column, TRUE);
		gtk_tree_view_column_set_cell_data_func (column, renderer,
							 (GtkTreeCellDataFunc)
							 rb_playlist_source_track_cell_data_func,
							 source, NULL);
		rb_entry_view_append_column_custom (source->priv->songs, column, 
						    _("Tra_ck"), "PlaylistTrack", NULL, NULL);
	}

	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_TITLE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_GENRE);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ARTIST);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_ALBUM);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_DURATION);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_RATING);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_PLAY_COUNT);
	rb_entry_view_append_column (source->priv->songs, RB_ENTRY_VIEW_COL_LAST_PLAYED);
	rb_entry_view_set_columns_clickable (source->priv->songs, FALSE);

	g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
			  G_CALLBACK (rb_playlist_source_songs_show_popup_cb), source);
		
	g_signal_connect (G_OBJECT (source->priv->songs), "drag_data_received",
			  G_CALLBACK (rb_playlist_source_drop_cb), source);
	gtk_drag_dest_set (GTK_WIDGET (source->priv->songs), GTK_DEST_DEFAULT_ALL,
			   target_uri, G_N_ELEMENTS (target_uri), GDK_ACTION_COPY);
		
	source->priv->normal_pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_PLAYLIST,
						       GTK_ICON_SIZE_LARGE_TOOLBAR,
						       NULL);
	source->priv->smartypants_pixbuf = gtk_widget_render_icon (dummy,
								   RB_STOCK_AUTOMATIC_PLAYLIST,
								   GTK_ICON_SIZE_LARGE_TOOLBAR,
								   NULL);
	gtk_widget_destroy (dummy);
		
	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), GTK_WIDGET (source->priv->songs));
		
	gtk_widget_show_all (GTK_WIDGET (source));
			
	return G_OBJECT (source);
}

static void
rb_playlist_source_songs_show_popup_cb (RBEntryView *view,
					RBPlaylistSource *playlist_view)
{
	rb_bonobo_show_popup (GTK_WIDGET (view), PLAYLIST_SOURCE_SONGS_POPUP_PATH);
}

static void
rb_playlist_source_init (RBPlaylistSource *source)
{
	source->priv = g_new0 (RBPlaylistSourcePrivate, 1);
}

static void
rb_playlist_source_finalize (GObject *object)
{
	RBPlaylistSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (object));

	source = RB_PLAYLIST_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	g_hash_table_destroy (source->priv->entries);

	g_free (source->priv->title);
	g_free (source->priv->status);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_playlist_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_DB:
		source->priv->db = g_value_get_object (value);
		break;
	case PROP_AUTOMATIC:
		source->priv->automatic = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, source->priv->db);
		break;
	case PROP_AUTOMATIC:
		g_value_set_boolean (value, source->priv->automatic);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_playlist_source_new (RhythmDB *db, gboolean automatic)
{
	RBSource *source;
	
	source = RB_SOURCE (g_object_new (RB_TYPE_PLAYLIST_SOURCE,
					  "name", _("Unknown"),
					  "db", db,
					  "automatic", automatic,
					  NULL));

	return source;
}

static void
rb_playlist_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
				   RBPlaylistSource *source)
{
	const char *location;

	if (source->priv->automatic)
		return;
	
	location = rhythmdb_entry_get_string (db, entry,
					      RHYTHMDB_PROP_LOCATION);
	if (g_hash_table_lookup (source->priv->entries, location)) {
		rhythmdb_query_model_add_entry (source->priv->model, entry);
	}
}

RhythmDBQueryModel *
rb_playlist_source_get_model (RBPlaylistSource *source)
{
	return source->priv->model;
}

void
rb_playlist_source_set_query (RBPlaylistSource *source,
			      GPtrArray *query,
			      guint limit_count,
			      guint limit_mb)
{
	RhythmDBQueryModel *query_model;
	GtkTreeModel *model;
	
	g_assert (source->priv->automatic);

	source->priv->model = query_model
		= rhythmdb_query_model_new_empty (source->priv->db);
	g_object_set (G_OBJECT (source->priv->model),
		      "max-count", limit_count,
		      "max-size", limit_mb, NULL);

	model = GTK_TREE_MODEL (query_model);

	rb_entry_view_set_model (source->priv->songs, RHYTHMDB_QUERY_MODEL (query_model));

	rhythmdb_read_lock (source->priv->db);
	rhythmdb_do_full_query_async_parsed (source->priv->db, model, query);

	g_object_unref (G_OBJECT (query_model));
	rb_entry_view_poll_model (source->priv->songs);
}

static const char *
impl_get_status (RBSource *asource)
{

	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	g_free (source->priv->status);
	source->priv->status = rhythmdb_compute_status_normal (rb_entry_view_get_num_entries (source->priv->songs),
							       rb_entry_view_get_duration (source->priv->songs),
							       rb_entry_view_get_total_size (source->priv->songs));
	return source->priv->status;
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return NULL;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	return source->priv->automatic ? source->priv->smartypants_pixbuf : source->priv->normal_pixbuf;
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	return source->priv->songs;
}

static gboolean
impl_can_cut (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	return !source->priv->automatic;
}

static GList *
impl_cut (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GList *sel = rb_entry_view_get_selected_entries (source->priv->songs);
	GList *tem;

	for (tem = sel; tem; tem = tem->next)
		rb_playlist_source_remove_entry (source, tem->data);

	return sel;
}

static void
impl_paste (RBSource *asource, GList *entries)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	for (; entries; entries = g_list_next (entries))
		rb_playlist_source_add_entry (source, entries->data);
}

static void
impl_delete (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GList *sel, *tem;
	sel = rb_entry_view_get_selected_entries (source->priv->songs);
	for (tem = sel; tem != NULL; tem = tem->next)
		rb_playlist_source_remove_entry (source, tem->data);
	g_list_free (sel);
}

static void
impl_song_properties (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

	song_info = rb_song_info_new (source->priv->songs);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GList *list;
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	list = gnome_vfs_uri_list_parse (data->data);

	if (list != NULL)
		rb_playlist_source_add_list_uri (source, list);
	else
		return FALSE;

	return TRUE;
}

static gboolean
impl_show_popup (RBSource *source)
{
	rb_bonobo_show_popup (GTK_WIDGET (source), PLAYLIST_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
rb_playlist_source_drop_cb (GtkWidget *widget,
			    GdkDragContext *context,
			    gint x,
			    gint y,
			    GtkSelectionData *data,
			    guint info,
			    guint time,
			    gpointer user_data)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (user_data);
	GtkTargetList *tlist;
	GdkAtom target;

	tlist = gtk_target_list_new (target_uri, G_N_ELEMENTS (target_uri));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	impl_receive_drag (RB_SOURCE (source), data);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

void
rb_playlist_source_add_location (RBPlaylistSource *source,
				 const char *location)
{
	RhythmDBEntry *entry;

	rhythmdb_read_lock (source->priv->db);
	if (g_hash_table_lookup (source->priv->entries, location)) {
		rhythmdb_read_unlock (source->priv->db);
		return;
	}

	g_hash_table_insert (source->priv->entries,
			     g_strdup (location), GINT_TO_POINTER (1));

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, location);
	if (entry)
		rhythmdb_entry_ref_unlocked (source->priv->db, entry);
	rhythmdb_read_unlock (source->priv->db);

	if (entry != NULL) {
		rhythmdb_query_model_add_entry (source->priv->model, entry);
		rhythmdb_entry_unref (source->priv->db, entry);
	}
}

void
rb_playlist_source_remove_location (RBPlaylistSource *source,
				    const char *location)
{
	RhythmDBEntry *entry;

	g_return_if_fail (g_hash_table_lookup (source->priv->entries, location) != NULL);
	g_hash_table_remove (source->priv->entries,
			     location);
	rhythmdb_read_lock (source->priv->db);
	entry = rhythmdb_entry_lookup_by_location (source->priv->db, location);
	rhythmdb_read_unlock (source->priv->db);
	if (entry != NULL)
		rhythmdb_query_model_remove_entry (source->priv->model, entry);
}

void
rb_playlist_source_add_entry (RBPlaylistSource *source,
			      RhythmDBEntry *entry)
{
	const char *location;

	rhythmdb_read_lock (source->priv->db);
	location = rhythmdb_entry_get_string (source->priv->db, entry,
					      RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (source->priv->db);

	rb_playlist_source_add_location (source, location);
}

void
rb_playlist_source_remove_entry (RBPlaylistSource *source,
			         RhythmDBEntry *entry)
{
	const char *location;

	rhythmdb_read_lock (source->priv->db);
	location = rhythmdb_entry_get_string (source->priv->db, entry,
					      RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (source->priv->db);

	rb_playlist_source_remove_location (source, location);
}

static void
rb_playlist_source_add_list_uri (RBPlaylistSource *source,
				 GList *list)
{
	GList *i, *uri_list = NULL;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return;

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;
		if (uri != NULL) {
			rhythmdb_read_lock (source->priv->db);
			rb_playlist_source_add_location (source, uri);
			rhythmdb_read_unlock (source->priv->db);
		}

		g_free (uri);
	}

	g_list_free (uri_list);
}

static void
playlist_iter_func (GtkTreeModel *model, GtkTreeIter *iter, char **uri, char **title)
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	g_object_get (G_OBJECT (model), "db", &db, NULL);
	
	gtk_tree_model_get (model, iter, 0, &entry, -1);

	rhythmdb_read_lock (db);

	*uri = g_strdup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_LOCATION));
	*title = g_strdup (rhythmdb_entry_get_string (db, entry, RHYTHMDB_PROP_TITLE));

	rhythmdb_read_unlock (db);
}

void
rb_playlist_source_save_playlist (RBPlaylistSource *source, const char *uri)
{
	RBPlaylist *playlist;
	GError *error = NULL;
	rb_debug ("saving playlist");

	playlist = rb_playlist_new ();

	rb_playlist_write (playlist, GTK_TREE_MODEL (source->priv->model),
			   playlist_iter_func, uri, &error);
	if (error != NULL)
		rb_error_dialog ("%s", error->message);
}

RBSource *
rb_playlist_source_new_from_xml	(RhythmDB *db,
				 xmlNodePtr node)
{
	RBPlaylistSource *source;
	xmlNodePtr child;
	char *tmp;

	source = RB_PLAYLIST_SOURCE (rb_playlist_source_new (db, FALSE));

	tmp = xmlGetProp (node, "type");
	if (!strcmp (tmp, "automatic"))
		source->priv->automatic = TRUE;
	g_free (tmp);
	tmp = xmlGetProp (node, "name");
	g_object_set (G_OBJECT (source), "name", tmp, NULL);
	g_free (tmp);

	if (source->priv->automatic) {
		GPtrArray *query;
		gchar *limit_str;
		guint limit_count = 0;
		guint limit_mb = 0;

		child = node->children;
		while (xmlNodeIsText (child))
			child = child->next;

		query = rhythmdb_query_deserialize (db, child);
		limit_str = xmlGetProp (node, "limit-count");
		if (!limit_str) /* Backwards compatibility */
			limit_str = xmlGetProp (node, "limit");
		if (limit_str) {
			limit_count = atoi (limit_str);
			g_free (limit_str);
		}
		limit_str = xmlGetProp (node, "limit-size");
		if (limit_str) {
			limit_mb = atoi (limit_str);
			g_free (limit_str);
		}
		rb_playlist_source_set_query (source, query,
					      limit_count,
					      limit_mb);
	} else {
		for (child = node->children; child; child = child->next) {
			char *location;

			if (xmlNodeIsText (child))
				continue;
		
			if (strcmp (child->name, "location"))
				continue;
		
			location = xmlNodeGetContent (child);
			rb_playlist_source_add_location (source, location);
		}
	}

	return RB_SOURCE (source);
}

void
rb_playlist_source_save_to_xml (RBPlaylistSource *source, xmlNodePtr parent_node)
{
	xmlNodePtr node;
	char *name;
	GtkTreeIter iter;
	
	node = xmlNewChild (parent_node, NULL, "playlist", NULL);
	g_object_get (G_OBJECT (source), "name", &name, NULL);
	xmlSetProp (node, "name", name);
	xmlSetProp (node, "type", source->priv->automatic ? "automatic" : "static");
	
	if (!source->priv->automatic) {
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->model),
						    &iter))
			return;

		do { 
			xmlNodePtr child_node = xmlNewChild (node, NULL, "location", NULL);
			RhythmDBEntry *entry;
			const char *location;
			char *encoded;

			gtk_tree_model_get (GTK_TREE_MODEL (source->priv->model), &iter, 0, &entry, -1);

			rhythmdb_read_lock (source->priv->db);
			location = rhythmdb_entry_get_string (source->priv->db, entry,
							      RHYTHMDB_PROP_LOCATION);
			rhythmdb_read_unlock (source->priv->db);
			
			encoded = xmlEncodeEntitiesReentrant (NULL, location);			
			
			xmlNodeSetContent (child_node, encoded);
			g_free (encoded);
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->model),
						   &iter));
	} else {
		GPtrArray *query;
		guint max_count;
		guint max_size_mb;
		char *limit_str;

		g_object_get (G_OBJECT (source->priv->model),
			      "max-count", &max_count,
			      "max-size", &max_size_mb,
			      "query", &query, NULL);
		limit_str = g_strdup_printf ("%d", max_count);
		xmlSetProp (node, "limit-count", limit_str);
		g_free (limit_str);
		limit_str = g_strdup_printf ("%d", max_size_mb);
		xmlSetProp (node, "limit-size", limit_str);
		g_free (limit_str);
		rhythmdb_query_serialize (source->priv->db, query, node);
	}
}
