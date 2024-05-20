/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libxml/tree.h>

#include "rb-iradio-source.h"
#include "rb-iradio-source-search.h"

#include "rhythmdb-query-model.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "totem-pl-parser.h"
#include "rb-dialog.h"
#include "rb-station-properties-dialog.h"
#include "rb-uri-dialog.h"
#include "rb-debug.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-metadata.h"
#include "rb-cut-and-paste-code.h"
#include "rb-source-search-basic.h"
#include "rb-source-toolbar.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

typedef struct _RhythmDBEntryType RBIRadioEntryType;
typedef struct _RhythmDBEntryTypeClass RBIRadioEntryTypeClass;

static void rb_iradio_source_class_init (RBIRadioSourceClass *klass);
static void rb_iradio_source_init (RBIRadioSource *source);
static void rb_iradio_source_constructed (GObject *object);
static void rb_iradio_source_dispose (GObject *object);
static void rb_iradio_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void rb_iradio_source_songs_show_popup_cb (RBEntryView *view,
						  gboolean over_entry,
						  RBIRadioSource *source);
static void genre_selected_cb (RBPropertyView *propview, const char *name,
			       RBIRadioSource *iradio_source);
static void genre_selection_reset_cb (RBPropertyView *propview, RBIRadioSource *iradio_source);
static void rb_iradio_source_songs_view_sort_order_changed_cb (GObject *object, GParamSpec *pspec, RBIRadioSource *source);
static char *guess_uri_scheme (const char *uri);

/* entry type */
static void rb_iradio_entry_type_class_init (RBIRadioEntryTypeClass *klass);
static void rb_iradio_entry_type_init (RBIRadioEntryType *etype);
GType rb_iradio_entry_type_get_type (void);

/* page methods */
static void impl_get_status (RBDisplayPage *page, char **text, gboolean *busy);

/* source methods */
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_delete_selected (RBSource *source);
static void impl_song_properties (RBSource *source);
static guint impl_want_uri (RBSource *source, const char *uri);
static void impl_add_uri (RBSource *source,
			  const char *uri,
			  const char *title,
			  const char *genre,
			  RBSourceAddCallback callback,
			  gpointer data,
			  GDestroyNotify destroy_data);
static void impl_get_playback_status (RBSource *source, char **text, float *progress);

static void rb_iradio_source_do_query (RBIRadioSource *source);
static void impl_reset_filters (RBSource *source);

void rb_iradio_source_show_columns_changed_cb (GtkToggleButton *button,
					     RBIRadioSource *source);
static void stations_view_drag_data_received_cb (GtkWidget *widget,
						 GdkDragContext *dc,
						 gint x, gint y,
						 GtkSelectionData *data,
						 guint info, guint time,
						 RBIRadioSource *source);
static void new_station_action_cb (GSimpleAction *, GVariant *, gpointer);

static void playing_source_changed_cb (RBShellPlayer *player,
				       RBSource *source,
				       RBIRadioSource *iradio_source);

enum
{
	PROP_0,
	PROP_SHOW_BROWSER
};

struct RBIRadioSourcePrivate
{
	RhythmDB *db;

	RBSourceToolbar *toolbar;
	RBPropertyView *genres;
	RBEntryView *stations;
	gboolean setting_new_query;

	char *selected_genre;
	RhythmDBQuery *search_query;
	RBSourceSearch *default_search;

	RBShellPlayer *player;

	gint info_available_id;

	gboolean dispose_has_run;

	GMenuModel *popup;
};

#define RB_IRADIO_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IRADIO_SOURCE, RBIRadioSourcePrivate))

static const GtkTargetEntry stations_view_drag_types[] = {
	{  "text/uri-list", 0, 0 },
	{  "_NETSCAPE_URL", 0, 1 },
};

G_DEFINE_DYNAMIC_TYPE (RBIRadioSource, rb_iradio_source, RB_TYPE_STREAMING_SOURCE);

G_DEFINE_DYNAMIC_TYPE (RBIRadioEntryType, rb_iradio_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static void
rb_iradio_entry_type_class_init (RBIRadioEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
}

static void
rb_iradio_entry_type_class_finalize (RBIRadioEntryTypeClass *klass)
{
}

static void
rb_iradio_entry_type_init (RBIRadioEntryType *etype)
{
}

static void
rb_iradio_source_class_init (RBIRadioSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_iradio_source_dispose;
	object_class->constructed = rb_iradio_source_constructed;

	object_class->set_property = rb_iradio_source_set_property;
	object_class->get_property = rb_iradio_source_get_property;

	page_class->get_status  = impl_get_status;

	source_class->reset_filters = impl_reset_filters;
	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->delete_selected = impl_delete_selected;
	source_class->get_entry_view = impl_get_entry_view;
	source_class->search = impl_search;
	source_class->song_properties = impl_song_properties;
	source_class->want_uri = impl_want_uri;
	source_class->add_uri = impl_add_uri;
	source_class->get_playback_status = impl_get_playback_status;

	g_object_class_override_property (object_class,
					  PROP_SHOW_BROWSER,
					  "show-browser");

	g_type_class_add_private (klass, sizeof (RBIRadioSourcePrivate));
}

static void
rb_iradio_source_class_finalize (RBIRadioSourceClass *klass)
{
}

static void
rb_iradio_source_init (RBIRadioSource *source)
{
	source->priv = RB_IRADIO_SOURCE_GET_PRIVATE (source);
}

static void
rb_iradio_source_dispose (GObject *object)
{
	RBIRadioSource *source;

	source = RB_IRADIO_SOURCE (object);

	if (source->priv->dispose_has_run) {
		/* If dispose did already run, return. */
		return;
	}
	/* Make sure dispose does not run twice. */
	source->priv->dispose_has_run = TRUE;

	if (source->priv->player) {
		g_object_unref (source->priv->player);
		source->priv->player = NULL;
	}

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->default_search != NULL) {
		g_object_unref (source->priv->default_search);
		source->priv->default_search = NULL;
	}

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}

	G_OBJECT_CLASS (rb_iradio_source_parent_class)->dispose (object);
}

static void
rb_iradio_source_constructed (GObject *object)
{
	RBIRadioSource *source;
	RBShell *shell;
	GSettings *settings;
	GtkAccelGroup *accel_group;
	GtkWidget *grid;
	GtkWidget *paned;
	GActionEntry actions[] = {
		{ "iradio-new-station", new_station_action_cb },
	};

	RB_CHAIN_GOBJECT_METHOD (rb_iradio_source_parent_class, constructed, object);
	source = RB_IRADIO_SOURCE (object);

	paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "db", &source->priv->db,
		      "shell-player", &source->priv->player,
		      "accel-group", &accel_group,
		      NULL);
	g_object_unref (shell);

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "network-server-symbolic");

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.iradio");
	if (g_settings_get_boolean (settings, "initial-stations-loaded") == FALSE) {
		rb_iradio_source_add_from_playlist (source, "resource:///org/gnome/Rhythmbox/iradio/iradio-initial.xspf");
		g_settings_set_boolean (settings, "initial-stations-loaded", TRUE);
	}

	_rb_add_display_page_actions (G_ACTION_MAP (g_application_get_default ()), G_OBJECT (shell), actions, G_N_ELEMENTS (actions));

	/* set up stations view */
	source->priv->stations = rb_entry_view_new (source->priv->db, G_OBJECT (source->priv->player),
						    FALSE, FALSE);

	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_GENRE, FALSE);
/* 	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_QUALITY, FALSE); */
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_RATING, FALSE);
/*	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);*/
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
	g_signal_connect_object (source->priv->stations,
				 "notify::sort-order",
				 G_CALLBACK (rb_iradio_source_songs_view_sort_order_changed_cb),
				 source, 0);

	/* set up drag and drop for the song tree view.
	 * we don't use RBEntryView's DnD support because it does too much.
	 * we just want to be able to drop stations in to add them.
	 */
	g_signal_connect_object (source->priv->stations,
				 "drag_data_received",
				 G_CALLBACK (stations_view_drag_data_received_cb),
				 source, 0);
	gtk_drag_dest_set (GTK_WIDGET (source->priv->stations),
			   GTK_DEST_DEFAULT_ALL,
			   stations_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	g_signal_connect_object (source->priv->stations, "show_popup",
				 G_CALLBACK (rb_iradio_source_songs_show_popup_cb), source, 0);

	/* set up genre entry view */
	source->priv->genres = rb_property_view_new (source->priv->db,
						     RHYTHMDB_PROP_GENRE,
						     _("Genre"));
	gtk_widget_show_all (GTK_WIDGET (source->priv->genres));
	gtk_widget_set_no_show_all (GTK_WIDGET (source->priv->genres), TRUE);
	g_signal_connect_object (source->priv->genres,
				 "property-selected",
				 G_CALLBACK (genre_selected_cb),
				 source, 0);
	g_signal_connect_object (source->priv->genres,
				 "property-selection-reset",
				 G_CALLBACK (genre_selection_reset_cb),
				 source, 0);

	g_object_set (source->priv->genres, "vscrollbar_policy",
		      GTK_POLICY_AUTOMATIC, NULL);

	gtk_paned_pack1 (GTK_PANED (paned), GTK_WIDGET (source->priv->genres), FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (paned), GTK_WIDGET (source->priv->stations), TRUE, FALSE);

	/* set up toolbar */
	source->priv->toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (source), accel_group);
	rb_source_toolbar_add_search_entry (source->priv->toolbar, _("Search your internet radio stations"));

	grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_widget_set_margin_top (GTK_WIDGET (grid), 6);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (source->priv->toolbar), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), paned, 0, 1, 1, 1);

	gtk_container_add (GTK_CONTAINER (source), grid);

	rb_source_bind_settings (RB_SOURCE (source),
				 GTK_WIDGET (source->priv->stations),
				 paned,
				 GTK_WIDGET (source->priv->genres),
				 TRUE);

	gtk_widget_show_all (GTK_WIDGET (source));

	g_signal_connect_object (source->priv->player, "playing-source-changed",
				 G_CALLBACK (playing_source_changed_cb),
				 source, 0);

	source->priv->default_search = rb_iradio_source_search_new ();

	rb_iradio_source_do_query (source);

	g_object_unref (accel_group);
}

static void
rb_iradio_source_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (object);

	switch (prop_id) {
	case PROP_SHOW_BROWSER:
		gtk_widget_set_visible (GTK_WIDGET (source->priv->genres), g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_iradio_source_get_property (GObject *object,
			       guint prop_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (object);

	switch (prop_id) {
	case PROP_SHOW_BROWSER:
		g_value_set_boolean (value, gtk_widget_get_visible (GTK_WIDGET (source->priv->genres)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_iradio_source_new (RBShell *shell, GObject *plugin)
{
	RBSource *source;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	GSettings *settings;
	GtkBuilder *builder;
	GMenu *toolbar;

	g_object_get (shell, "db", &db, NULL);

	entry_type = rhythmdb_entry_type_get_by_name (db, "iradio");
	if (entry_type == NULL) {
		entry_type = g_object_new (rb_iradio_entry_type_get_type (),
					   "db", db,
					   "name", "iradio",
					   "save-to-disk", TRUE,
					   "category", RHYTHMDB_ENTRY_STREAM,
					   NULL);
		rhythmdb_register_entry_type (db, entry_type);
	}
	g_object_unref (db);

	builder = rb_builder_load_plugin_file (plugin, "iradio-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "iradio-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.iradio");
	source = RB_SOURCE (g_object_new (RB_TYPE_IRADIO_SOURCE,
					  "name", _("Radio"),
					  "shell", shell,
					  "entry-type", entry_type,
					  "plugin", plugin,
					  "settings", g_settings_get_child (settings, "source"),
					  "toolbar-menu", toolbar,
					  NULL));
	g_object_unref (settings);
	g_object_unref (builder);
	rb_shell_register_entry_type_for_source (shell, source, entry_type);
	return source;
}

static char *
guess_uri_scheme (const char *uri)
{
	const char *scheme;

	/* if the URI has no scheme, it might be an absolute path, or it might be
	 * host:port for HTTP.
	 */
	scheme = strstr (uri, "://");
	if (scheme == NULL) {
		if (uri[0] == '/') {
			return g_strdup_printf ("file://%s", uri);
		} else {
			return g_strdup_printf ("http://%s", uri);
		}
	}

	return NULL;
}

void
rb_iradio_source_add_station (RBIRadioSource *source,
			      const char *uri,
			      const char *title,
			      const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = { 0, };
	char *real_uri = NULL;
	char *fixed_title;
	char *fixed_genre = NULL;
	RhythmDBEntryType *entry_type;

	real_uri = guess_uri_scheme (uri);
	if (real_uri)
		uri = real_uri;

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
	if (entry) {
		rb_debug ("uri %s already in db", uri);
		g_free (real_uri);
		return;
	}

	g_object_get (source, "entry-type", &entry_type, NULL);
	entry = rhythmdb_entry_new (source->priv->db, entry_type, uri);
	g_object_unref (entry_type);
	if (entry == NULL) {
		g_free (real_uri);
		return;
	}

	g_value_init (&val, G_TYPE_STRING);
	if (title) {
		fixed_title = rb_make_valid_utf8 (title, '?');
	} else {
		fixed_title = g_uri_unescape_string (uri, NULL);
	}
	g_value_take_string (&val, fixed_title);

	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_reset (&val);

	if ((!genre) || (strcmp (genre, "") == 0)) {
		genre = _("Unknown");
	} else {
		fixed_genre = rb_make_valid_utf8 (genre, '?');
		genre = fixed_genre;
	}

	g_value_set_string (&val, genre);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);
	g_free (fixed_genre);

	g_value_init (&val, G_TYPE_DOUBLE);
	g_value_set_double (&val, 0.0);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);

	rhythmdb_commit (source->priv->db);

	g_free (real_uri);
}
static void
impl_search (RBSource *asource,
	     RBSourceSearch *search,
	     const char *cur_text,
	     const char *new_text)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
	}

	if (search == NULL) {
		search = source->priv->default_search;
	}
	source->priv->search_query = rb_source_search_create_query (search, source->priv->db, new_text);

	rb_iradio_source_do_query (source);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	return source->priv->stations;
}

static void
impl_get_status (RBDisplayPage *page,
		 char **text,
		 gboolean *busy)
{
	RhythmDBQueryModel *model;
	guint num_entries;
	RBIRadioSource *source = RB_IRADIO_SOURCE (page);

	g_object_get (source, "query-model", &model, NULL);
	num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (model);

	*text = g_strdup_printf (ngettext ("%d station", "%d stations", num_entries),
				 num_entries);
}

static void
impl_get_playback_status (RBSource *source, char **text, float *progress)
{
	rb_streaming_source_get_progress (RB_STREAMING_SOURCE (source), text, progress);
}

static void
impl_delete_selected (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GList *sel;
	GList *l;

	sel = rb_entry_view_get_selected_entries (source->priv->stations);
	for (l = sel; l != NULL; l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
		rhythmdb_commit (source->priv->db);
	}

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
impl_song_properties (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GObject *plugin;
	GtkWidget *dialog;

	g_object_get (source, "plugin", &plugin, NULL);
	dialog = rb_station_properties_dialog_new (plugin, source->priv->stations);
	g_object_unref (plugin);

	rb_debug ("in song properties");
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	if (g_str_has_prefix (uri, "http://")) {
		/* other entry types might have
		 * more specific guesses for HTTP
		 */
		return 50;
	} else if (g_str_has_prefix (uri, "pnm://") ||
		   g_str_has_prefix (uri, "rtsp://") ||
		   g_str_has_prefix (uri, "mms://") ||
		   g_str_has_prefix (uri, "mmsh://")) {
		return 100;
	}

	return 0;
}

static void
impl_add_uri (RBSource *source,
	      const char *uri,
	      const char *title,
	      const char *genre,
	      RBSourceAddCallback callback,
	      gpointer data,
	      GDestroyNotify destroy_data)
{
	if (rb_uri_is_local (uri)) {
		rb_iradio_source_add_from_playlist (RB_IRADIO_SOURCE (source), uri);
	} else {
		rb_iradio_source_add_station (RB_IRADIO_SOURCE (source),
					      uri, title, genre);
	}
	if (callback != NULL) {
		callback (source, uri, data);
		if (destroy_data != NULL) {
			destroy_data (data);
		}
	}
}

static void
rb_iradio_source_songs_view_sort_order_changed_cb (GObject *object,
						   GParamSpec *pspec,
						   RBIRadioSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
rb_iradio_source_songs_show_popup_cb (RBEntryView *view,
				      gboolean over_entry,
				      RBIRadioSource *source)
{
	GtkWidget *menu;

	if (over_entry == FALSE)
		return;

	if (source->priv->popup == NULL) {
		GtkBuilder *builder;
		GObject *plugin;
		g_object_get (source, "plugin", &plugin, NULL);
		builder = rb_builder_load_plugin_file (plugin, "iradio-popup.ui", NULL);
		g_object_unref (plugin);

		source->priv->popup = G_MENU_MODEL (gtk_builder_get_object (builder, "iradio-popup"));
		g_object_ref (source->priv->popup);
		g_object_unref (builder);
	}

	menu = gtk_menu_new_from_model (source->priv->popup);
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
genre_selected_cb (RBPropertyView *propview, const char *name,
		   RBIRadioSource *iradio_source)
{
	if (iradio_source->priv->setting_new_query)
		return;

	g_free (iradio_source->priv->selected_genre);
	iradio_source->priv->selected_genre = g_strdup (name);
	rb_iradio_source_do_query (iradio_source);

	rb_source_notify_filter_changed (RB_SOURCE (iradio_source));
}

static void
genre_selection_reset_cb (RBPropertyView *propview,
			  RBIRadioSource *iradio_source)
{
	if (iradio_source->priv->setting_new_query)
		return;

	g_free (iradio_source->priv->selected_genre);
	iradio_source->priv->selected_genre = NULL;

	rb_iradio_source_do_query (iradio_source);

	rb_source_notify_filter_changed (RB_SOURCE (iradio_source));
}

static void
rb_iradio_source_do_query (RBIRadioSource *source)
{
	RhythmDBQueryModel *genre_query_model = NULL;
	RhythmDBQueryModel *station_query_model = NULL;
	RhythmDBPropertyModel *genre_model;
	GPtrArray *query;
	RhythmDBEntryType *entry_type;

	/* don't update the selection while we're rebuilding the query */
	source->priv->setting_new_query = TRUE;

	/* construct and run the query for the search box.
	 * this is used as the model for the genre view.
	 */

	g_object_get (source, "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);
	g_object_unref (entry_type);

	if (source->priv->search_query != NULL) {
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY,
				       source->priv->search_query,
				       RHYTHMDB_QUERY_END);
	}

	genre_model = rb_property_view_get_model (source->priv->genres);

	genre_query_model = rhythmdb_query_model_new_empty (source->priv->db);
	g_object_set (genre_model, "query-model", genre_query_model, NULL);

	rhythmdb_do_full_query_parsed (source->priv->db,
				       RHYTHMDB_QUERY_RESULTS (genre_query_model),
				       query);

	rhythmdb_query_free (query);
	query = NULL;

	/* check the selected genre is still available, and if not, select 'all' */
	if (source->priv->selected_genre != NULL) {
		GList *sel = NULL;

		if (!rhythmdb_property_model_iter_from_string (genre_model,
							       source->priv->selected_genre,
							       NULL)) {
			g_free (source->priv->selected_genre);
			source->priv->selected_genre = NULL;
		}

		sel = g_list_prepend (sel, source->priv->selected_genre);
		rb_property_view_set_selection (source->priv->genres, sel);
		g_list_free (sel);
	}

	/* if a genre is selected, construct a new query for it, and create
	 * a new model based on the search box query model.  otherwise, just
	 * reuse the search box query model.
	 */

	if (source->priv->selected_genre != NULL) {
		rb_debug ("matching on genre \"%s\"", source->priv->selected_genre);

		station_query_model = rhythmdb_query_model_new_empty (source->priv->db);
		query = rhythmdb_query_parse (source->priv->db,
					      RHYTHMDB_QUERY_PROP_EQUALS,
					      RHYTHMDB_PROP_GENRE,
					      source->priv->selected_genre,
					      RHYTHMDB_QUERY_END);

		g_object_set (station_query_model,
			      "query", query,
			      "base-model", genre_query_model,
			      NULL);

		rhythmdb_query_free (query);
		query = NULL;
	} else {
		station_query_model = g_object_ref (genre_query_model);
	}

	rb_entry_view_set_model (source->priv->stations, station_query_model);
	g_object_set (source, "query-model", station_query_model, NULL);

	g_object_unref (genre_query_model);
	g_object_unref (station_query_model);

	source->priv->setting_new_query = FALSE;
}

static void
impl_reset_filters (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}
	rb_source_toolbar_clear_search_entry (source->priv->toolbar);

	rb_property_view_set_selection (source->priv->genres, NULL);
}

static void
handle_playlist_entry_cb (TotemPlParser *playlist,
			  const char *uri,
			  GHashTable *metadata,
			  RBIRadioSource *source)
{
	const char *title, *genre;

	title = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_TITLE);
	genre = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_GENRE);
	rb_iradio_source_add_station (source, uri, title, genre);
}

void
rb_iradio_source_add_from_playlist (RBIRadioSource *source,
				    const char     *uri)
{
	TotemPlParser *parser = totem_pl_parser_new ();
	char *real_uri;

	real_uri = guess_uri_scheme (uri);
	if (real_uri)
		uri = real_uri;

	g_signal_connect_object (parser, "entry-parsed",
				 G_CALLBACK (handle_playlist_entry_cb),
				 source, 0);
	g_object_set (parser, "recurse", FALSE, NULL);

	switch (totem_pl_parser_parse (parser, uri, FALSE)) {
	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
	case TOTEM_PL_PARSER_RESULT_IGNORED:
		/* maybe it's the actual stream URL, then */
		rb_iradio_source_add_station (source, uri, NULL, NULL);
		break;

	default:
	case TOTEM_PL_PARSER_RESULT_SUCCESS:
	case TOTEM_PL_PARSER_RESULT_ERROR:
		break;
	}
	g_object_unref (parser);
	g_free (real_uri);
}

static void
stations_view_drag_data_received_cb (GtkWidget *widget,
				     GdkDragContext *dc,
				     gint x,
				     gint y,
				     GtkSelectionData *selection_data,
				     guint info,
				     guint time,
				     RBIRadioSource *source)
{
	GList *uri_list, *i;

	rb_debug ("parsing uri list");
	uri_list = rb_uri_list_parse ((char *) gtk_selection_data_get_data (selection_data));
	if (uri_list == NULL)
		return;

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = NULL;

		uri = i->data;
		if (uri != NULL) {
			rb_iradio_source_add_station (source, uri, NULL, NULL);
		}

		if (info == 1) {
			/* for _NETSCAPE_URL drags, this item is the link text */
			i = i->next;
		}
	}

	rb_list_deep_free (uri_list);
	return;
}

static void
new_station_location_added (RBURIDialog    *dialog,
			    const char     *uri,
			    RBIRadioSource *source)
{
	rb_iradio_source_add_station (source, uri, NULL, NULL);
}

static void
new_station_response_cb (GtkDialog *dialog, int response, gpointer meh)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
new_station_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (data);
	GtkWidget *dialog;

	rb_debug ("Got new station command");

	/* should prevent multiple dialogs?  going to kill this nonsense anyway soon.. */

	dialog = rb_uri_dialog_new (_("New Internet Radio Station"), _("URL of internet radio station:"));
	g_signal_connect_object (dialog, "location-added",
				 G_CALLBACK (new_station_location_added),
				 source, 0);
	g_signal_connect (dialog, "response", G_CALLBACK (new_station_response_cb), NULL);

	gtk_widget_show_all (dialog);
}

static void
info_available_cb (RBPlayer *backend,
		   const char *uri,
		   RBMetaDataField field,
		   GValue *value,
		   RBIRadioSource *source)
{
	RhythmDBEntry *entry;
        RhythmDBPropType entry_field = 0;
        gboolean set_field = FALSE;
	char *str = NULL;

	/* coherence check */
	if (!rb_player_opened (backend)) {
		rb_debug ("Got info_available but not playing");
		return;
	}

	entry = rb_shell_player_get_playing_entry (source->priv->player);
	if (rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE)
		return;

	/* validate the value */
	switch (field) {
	case RB_METADATA_FIELD_TITLE:
	case RB_METADATA_FIELD_ARTIST:
	case RB_METADATA_FIELD_GENRE:
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_ORGANIZATION:
		str = g_value_dup_string (value);
		if (!g_utf8_validate (str, -1, NULL)) {
			g_warning ("Invalid UTF-8 from internet radio: %s", str);
			g_free (str);
			return;
		}
		break;
	default:
		break;
	}


	switch (field) {
		/* streaming song information */
	case RB_METADATA_FIELD_TITLE:
	{
		rb_streaming_source_set_streaming_title (RB_STREAMING_SOURCE (source), str);
		break;
	}
	case RB_METADATA_FIELD_ARTIST:
	{
		rb_streaming_source_set_streaming_artist (RB_STREAMING_SOURCE (source), str);
		break;
	}

		/* station information */
	case RB_METADATA_FIELD_GENRE:
	{
		const char *existing;

		/* check if the db entry already has a genre; if so, don't change it */
		existing = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
		if ((existing == NULL) ||
		    (strcmp (existing, "") == 0) ||
		    (strcmp (existing, _("Unknown")) == 0)) {
			entry_field = RHYTHMDB_PROP_GENRE;
			rb_debug ("setting genre of iradio station to %s", str);
			set_field = TRUE;
		} else {
			rb_debug ("iradio station already has genre: %s; ignoring %s", existing, str);
		}
		break;
	}
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_ORGANIZATION:
	{
		const char *existing;
		const char *location;

		/* check if the db entry already has a title; if so, don't change it.
		 * consider title==URI to be the same as no title, since that's what
		 * happens for stations imported by DnD or commandline args.
		 * if the station title really is the same as the URI, then surely
		 * the station title in the stream metadata will say that too..
		 */
		existing = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		if ((existing == NULL) ||
		    (strcmp (existing, "") == 0) ||
		    (strcmp (existing, location) == 0)) {
			entry_field = RHYTHMDB_PROP_TITLE;
			rb_debug ("setting title of iradio station to %s", str);
			set_field = TRUE;
		} else {
			rb_debug ("iradio station already has title: %s; ignoring %s", existing, str);
		}
		break;
	}
	case RB_METADATA_FIELD_BITRATE:
		if (!rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE)) {
			gulong bitrate;

			/* GStreamer sends us bitrate in bps, but we need it in kbps*/
			bitrate = g_value_get_ulong (value);
			g_value_set_ulong (value, bitrate/1000);

			rb_debug ("setting bitrate of iradio station to %lu",
				  g_value_get_ulong (value));
			entry_field = RHYTHMDB_PROP_BITRATE;
			set_field = TRUE;
		}
		break;
	default:
		break;
	}

	if (set_field && entry_field != 0) {
		rhythmdb_entry_set (source->priv->db, entry, entry_field, value);
		rhythmdb_commit (source->priv->db);
	}

	g_free (str);
}

static void
playing_source_changed_cb (RBShellPlayer *player,
			   RBSource *source,
			   RBIRadioSource *iradio_source)
{
	GObject *backend;

	g_object_get (player, "player", &backend, NULL);

	if (source == RB_SOURCE (iradio_source) && (iradio_source->priv->info_available_id == 0)) {
		rb_debug ("connecting info-available signal handler");
		iradio_source->priv->info_available_id =
			g_signal_connect_object (backend, "info",
						 G_CALLBACK (info_available_cb),
						 iradio_source, 0);
	} else if (iradio_source->priv->info_available_id) {
		rb_debug ("disconnecting info-available signal handler");
		g_signal_handler_disconnect (backend,
					     iradio_source->priv->info_available_id);
		iradio_source->priv->info_available_id = 0;
	}

	g_object_unref (backend);
}

void
_rb_iradio_source_register_type (GTypeModule *module)
{
	rb_iradio_entry_type_register_type (module);
	rb_iradio_source_register_type (module);
}
