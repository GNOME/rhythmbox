/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
 *  arch-tag: Implementation of Internet Radio source object
 *
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>

#include "rb-iradio-source.h"

#include "rhythmdb-query-model.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "totem-pl-parser.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-station-properties-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"

static void rb_iradio_source_class_init (RBIRadioSourceClass *klass);
static void rb_iradio_source_init (RBIRadioSource *source);
static GObject *rb_iradio_source_constructor (GType type, guint n_construct_properties,
					      GObjectConstructParam *construct_properties);
static void rb_iradio_source_dispose (GObject *object);
static void rb_iradio_source_finalize (GObject *object);
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
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBIRadioSource *source);
static void rb_iradio_source_state_pref_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBIRadioSource *source);
static void rb_iradio_source_first_time_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBIRadioSource *source);
static void rb_iradio_source_show_browser (RBIRadioSource *source,
					   gboolean show);
static void rb_iradio_source_state_prefs_sync (RBIRadioSource *source);
static void genre_selected_cb (RBPropertyView *propview, const char *name,
			       RBIRadioSource *iradio_source);
static void genre_selection_reset_cb (RBPropertyView *propview, RBIRadioSource *iradio_source);
static void rb_iradio_source_songs_view_sort_order_changed_cb (RBEntryView *view, RBIRadioSource *source);
static char *guess_uri_scheme (const char *uri);

/* source methods */
static char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static void impl_delete (RBSource *source);
static void impl_song_properties (RBSource *source);
static RBSourceEOFType impl_handle_eos (RBSource *asource);
static gboolean impl_show_popup (RBSource *source);
static GList *impl_get_ui_actions (RBSource *source);

static void rb_iradio_source_do_query (RBIRadioSource *source);

void rb_iradio_source_show_columns_changed_cb (GtkToggleButton *button,
					     RBIRadioSource *source);
static void stations_view_drag_data_received_cb (GtkWidget *widget,
						 GdkDragContext *dc,
						 gint x, gint y,
						 GtkSelectionData *data,
						 guint info, guint time,
						 RBIRadioSource *source);
static void rb_iradio_source_cmd_new_station (GtkAction *action,
					      RBIRadioSource *source);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_STATION "/commands/CurrentStation"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"

#define CONF_UI_IRADIO_DIR CONF_PREFIX "/ui/iradio"
#define CONF_UI_IRADIO_COLUMNS_SETUP CONF_PREFIX "/ui/iradio/columns_setup"
#define CONF_STATE_IRADIO_DIR CONF_PREFIX "/state/iradio"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/iradio/paned_position"
#define CONF_STATE_IRADIO_SORTING CONF_PREFIX "/state/iradio/sorting"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/iradio/show_browser"

struct RBIRadioSourcePrivate
{
	RhythmDB *db;

	GtkWidget *vbox;
	GtkWidget *paned;
	GtkActionGroup *action_group;

	RBPropertyView *genres;
	RBEntryView *stations;
	gboolean setting_new_query;

	gboolean initialized;

	char *search_text;
	char *selected_genre;

	gboolean firstrun_done;
	
	RhythmDBEntryType entry_type;
};

#define RB_IRADIO_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_IRADIO_SOURCE, RBIRadioSourcePrivate))

static GtkActionEntry rb_iradio_source_actions [] =
{
	{ "MusicNewInternetRadioStation", GTK_STOCK_NEW, N_("New Internet _Radio Station"), "<control>I",
	  N_("Create a new Internet Radio station"),
	  G_CALLBACK (rb_iradio_source_cmd_new_station) }
};

static const GtkTargetEntry stations_view_drag_types[] = {
	{  "text/uri-list", 0, 0 },
	{  "_NETSCAPE_URL", 0, 1 },
};

enum
{
	PROP_0,
	PROP_ENTRY_TYPE
};

G_DEFINE_TYPE (RBIRadioSource, rb_iradio_source, RB_TYPE_SOURCE)

static void
rb_iradio_source_class_init (RBIRadioSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_iradio_source_dispose;
	object_class->finalize = rb_iradio_source_finalize;
	object_class->constructor = rb_iradio_source_constructor;

	object_class->set_property = rb_iradio_source_set_property;
	object_class->get_property = rb_iradio_source_get_property;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_status  = impl_get_status;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_true_function;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_uint ("entry-type",
							    "Entry type",
							    "Type of the entries which should be displayed by this source",
							    0,
							    G_MAXINT,
							    RHYTHMDB_ENTRY_TYPE_IRADIO_STATION,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBIRadioSourcePrivate));
}

static void
rb_iradio_source_init (RBIRadioSource *source)
{
	gint size;
	GdkPixbuf *pixbuf;

	source->priv = RB_IRADIO_SOURCE_GET_PRIVATE (source);

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
	
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   "stock_channel",
					   size,
					   0, NULL);
	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
}

static void
rb_iradio_source_dispose (GObject *object)
{
	RBIRadioSource *source;

	source = RB_IRADIO_SOURCE (object);

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_iradio_source_parent_class)->dispose (object);
}

static void
rb_iradio_source_finalize (GObject *object)
{
	RBIRadioSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_SOURCE (object));

	source = RB_IRADIO_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing iradio source");

	G_OBJECT_CLASS (rb_iradio_source_parent_class)->finalize (object);
}

static GObject *
rb_iradio_source_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBIRadioSource *source;
	RBIRadioSourceClass *klass;
	RBShell *shell;
	GObject *shell_player;

	klass = RB_IRADIO_SOURCE_CLASS (g_type_class_peek (RB_TYPE_IRADIO_SOURCE));

	source = RB_IRADIO_SOURCE (G_OBJECT_CLASS (rb_iradio_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	source->priv->paned = gtk_hpaned_new ();

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (G_OBJECT (shell));

	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "IRadioActions",
								       rb_iradio_source_actions,
								       G_N_ELEMENTS (rb_iradio_source_actions),
								       source);

	/* set up stations view */
	source->priv->stations = rb_entry_view_new (source->priv->db, shell_player,
						    CONF_STATE_IRADIO_SORTING,
						    FALSE, FALSE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_GENRE, FALSE);
/* 	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_QUALITY, FALSE); */
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_RATING, FALSE);
/*	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);*/
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
	g_signal_connect_object (G_OBJECT (source->priv->stations),
				 "sort-order-changed",
				 G_CALLBACK (rb_iradio_source_songs_view_sort_order_changed_cb),
				 source, 0);
	
	/* set up drag and drop for the song tree view.
	 * we don't use RBEntryView's DnD support because it does too much.
	 * we just want to be able to drop stations in to add them.
	 */
	g_signal_connect_object (G_OBJECT (source->priv->stations), 
				 "drag_data_received",
				 G_CALLBACK (stations_view_drag_data_received_cb),
				 source, 0);
	gtk_drag_dest_set (GTK_WIDGET (source->priv->stations),
			   GTK_DEST_DEFAULT_ALL,
			   stations_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	g_signal_connect_object (G_OBJECT (source->priv->stations),
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->stations), "show_popup",
				 G_CALLBACK (rb_iradio_source_songs_show_popup_cb), source, 0);
	
	/* set up genre entry view */
	source->priv->genres = rb_property_view_new (source->priv->db,
						     RHYTHMDB_PROP_GENRE,
						     _("Genre"));
	g_signal_connect_object (G_OBJECT (source->priv->genres),
				 "property-selected",
				 G_CALLBACK (genre_selected_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->genres),
				 "property-selection-reset",
				 G_CALLBACK (genre_selection_reset_cb),
				 source, 0);

	g_object_set (G_OBJECT (source->priv->genres), "vscrollbar_policy",
		      GTK_POLICY_AUTOMATIC, NULL);
	g_object_ref (G_OBJECT (source->priv->genres));


	gtk_paned_pack2 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->stations), TRUE, FALSE);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

	rb_iradio_source_state_prefs_sync (source);
	eel_gconf_notification_add (CONF_STATE_IRADIO_DIR,
				    (GConfClientNotifyFunc) rb_iradio_source_state_pref_changed,
				    source);
	source->priv->firstrun_done = eel_gconf_get_boolean (CONF_FIRST_TIME);
	eel_gconf_notification_add (CONF_FIRST_TIME,
				    (GConfClientNotifyFunc) rb_iradio_source_first_time_changed,
				    source);
	gtk_widget_show_all (GTK_WIDGET (source));

	rb_iradio_source_do_query (source);

	return G_OBJECT (source);
}

static void
rb_iradio_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (object);

	switch (prop_id) {
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_uint (value);
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
	case PROP_ENTRY_TYPE:
		g_value_set_uint (value, source->priv->entry_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_iradio_source_new (RBShell *shell)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_IRADIO_SOURCE,
					  "name", _("Radio"),
					  "shell", shell,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, 
						 RHYTHMDB_ENTRY_TYPE_IRADIO_STATION);

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
			      const char *uri, const char *title, const char *genre)
{
	RhythmDBEntry *entry;
	GValue val = { 0, };
	char *real_uri = NULL;

	real_uri = guess_uri_scheme (uri);
	if (real_uri)
		uri = real_uri;

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
	if (entry) {
		rb_debug ("uri %s already in db", uri); 
		g_free (real_uri);
		return;
	}
	entry = rhythmdb_entry_new (source->priv->db, RHYTHMDB_ENTRY_TYPE_IRADIO_STATION, uri);
	if (entry == NULL) {
		g_free (real_uri);
		return;
	}

	g_value_init (&val, G_TYPE_STRING);
	if (title)
		g_value_set_static_string (&val, title);
	else
		g_value_take_string (&val,
				     gnome_vfs_format_uri_for_display (uri));

	rhythmdb_entry_set_uninserted (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_reset (&val);
		
	if ((!genre) || (strcmp (genre, "") == 0))
		genre = _("Unknown");
	g_value_set_string (&val, genre);
	rhythmdb_entry_set_uninserted (source->priv->db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);
	
	g_value_init (&val, G_TYPE_DOUBLE);
	g_value_set_double (&val, 0.0);
	rhythmdb_entry_set_uninserted (source->priv->db, entry, RHYTHMDB_PROP_RATING, &val);
	g_value_unset (&val);
	
	rhythmdb_commit (source->priv->db);

	g_free (real_uri);
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	if (source->priv->initialized) {
		if (search_text == NULL && source->priv->search_text == NULL)
			return;
		if (search_text != NULL &&
		    source->priv->search_text != NULL
		    && !strcmp (search_text, source->priv->search_text))
			return;
	}

	source->priv->initialized = TRUE;
	if (search_text != NULL && search_text[0] == '\0')
		search_text = NULL;

	g_free (source->priv->search_text);
	source->priv->search_text = g_strdup (search_text);
	rb_iradio_source_do_query (source);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);

	return source->priv->stations;
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_RETRY;
}

#if 0
struct RBIRadioAsyncPlayStatisticsData
{
	RBIRadioSource *source;
	RhythmDBEntry *entry;
};

static gboolean
rb_iradio_source_async_update_play_statistics (gpointer data)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (data);
	RhythmDBEntry *playing_entry;

	rb_debug ("entering async handler");

	GDK_THREADS_ENTER ();

	playing_entry = rb_entry_view_get_playing_entry (source->priv->stations);
	rb_debug ("async updating play statistics, entry: %p playing entry: %p",
		  source->priv->async_update_entry, playing_entry);
	if (source->priv->async_update_entry == playing_entry)
		rb_source_update_play_statistics (RB_SOURCE (source),
						  source->priv->db,
						  source->priv->async_update_entry);
	rhythmdb_entry_unref (source->priv->db, source->priv->async_update_entry);
	source->priv->async_update_entry = NULL;
		
	GDK_THREADS_LEAVE ();

	return FALSE;
}
#endif

static char *
impl_get_status (RBSource *asource)
{
	char *ret;
	RhythmDBQueryModel *model;
	guint num_entries;

	g_object_get (G_OBJECT (asource), "query-model", &model, NULL);
	num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (G_OBJECT (model));

	ret = g_strdup_printf (ngettext ("%d station", "%d stations", num_entries),
			       num_entries);
	return ret;
}

static const char *
impl_get_browser_key (RBSource *asource)
{
	return CONF_STATE_SHOW_BROWSER;
}

static void
impl_delete (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GList *l;

	for (l = rb_entry_view_get_selected_entries (source->priv->stations); l != NULL;
	     l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
		rhythmdb_commit (source->priv->db);
	}
}

static void
impl_song_properties (RBSource *asource)
{
	RBIRadioSource *source = RB_IRADIO_SOURCE (asource);
	GtkWidget *dialog = rb_station_properties_dialog_new (source->priv->stations);
	rb_debug ("in song properties");
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBIRadioSource *source)
{
	/* save state */
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_iradio_source_state_prefs_sync (RBIRadioSource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	rb_iradio_source_show_browser (source,
				       eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER));
}

static void
rb_iradio_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBIRadioSource *source)
{
	rb_debug ("state prefs changed");
	rb_iradio_source_state_prefs_sync (source);
}

static void
rb_iradio_source_songs_view_sort_order_changed_cb (RBEntryView *view,
						   RBIRadioSource *source)
{
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (view);
}

static void
rb_iradio_source_songs_show_popup_cb (RBEntryView *view,
				      gboolean over_entry,
				      RBIRadioSource *source)
{
	if (G_OBJECT (source) == NULL) {
		return;
	}
	if (over_entry)
		_rb_source_show_popup (RB_SOURCE (source), "/IRadioViewPopup");
	else
		_rb_source_show_popup (RB_SOURCE (source), "/IRadioSourcePopup");
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
genre_selection_reset_cb (RBPropertyView *propview, RBIRadioSource *iradio_source)
{
	if (iradio_source->priv->setting_new_query)
		return;
	
	g_free (iradio_source->priv->selected_genre);
	iradio_source->priv->selected_genre = NULL;

	rb_iradio_source_do_query (iradio_source);

	rb_source_notify_filter_changed (RB_SOURCE (iradio_source));
}

static void
rb_iradio_source_show_browser (RBIRadioSource *source,
			       gboolean show)
{
	GtkWidget *genreswidget = GTK_WIDGET (source->priv->genres);

	if (show == TRUE) {
		gtk_paned_pack1 (GTK_PANED (source->priv->paned), genreswidget, FALSE, FALSE);
		gtk_widget_show_all (genreswidget);
	} else if (show == FALSE) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (source->priv->paned));
		gtk_widget_hide (genreswidget);
		if (g_list_find (children, genreswidget))
		    gtk_container_remove (GTK_CONTAINER (source->priv->paned), genreswidget);
		g_list_free (children);
	}
}

static void
rb_iradio_source_do_query (RBIRadioSource *source)
{
	RhythmDBEntryType entry_type;
	RhythmDBQueryModel *genre_query_model = NULL;
	RhythmDBQueryModel *station_query_model = NULL;
	RhythmDBPropertyModel *genre_model;
	GPtrArray *query;

	/* don't update the selection while we're rebuilding the query */
	source->priv->setting_new_query = TRUE;

	/* construct and run the query for the search box.
	 * this is used as the model for the genre view.
	 */
	
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);
	genre_query_model = rhythmdb_query_model_new_empty (source->priv->db);

	if (source->priv->search_text) {
		GPtrArray *subquery = rhythmdb_query_parse (source->priv->db,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_GENRE_FOLDED,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_DISJUNCTION,
							    RHYTHMDB_QUERY_PROP_LIKE,
							    RHYTHMDB_PROP_TITLE_FOLDED,
							    source->priv->search_text,
							    RHYTHMDB_QUERY_END);
		rb_debug ("searching for \"%s\"", source->priv->search_text);
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY,
				       subquery,
				       RHYTHMDB_QUERY_END);
	}

	genre_model = rb_property_view_get_model (source->priv->genres);
	g_object_set (G_OBJECT (genre_model), "query-model", genre_query_model, NULL);

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
	g_object_unref (G_OBJECT (genre_model));

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
		station_query_model = genre_query_model;
	}

	rb_entry_view_set_model (source->priv->stations, station_query_model);
	g_object_set (G_OBJECT (source), "query-model", station_query_model, NULL);

	g_object_unref (G_OBJECT (genre_query_model));
	if (station_query_model != genre_query_model)
		g_object_unref (G_OBJECT (station_query_model));

	source->priv->setting_new_query = FALSE;
}

static void
handle_playlist_entry_cb (TotemPlParser *playlist, const char *uri, const char *title,
			  const char *genre, RBIRadioSource *source)
{
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

	g_signal_connect_object (G_OBJECT (parser), "entry",
				 G_CALLBACK (handle_playlist_entry_cb),
				 source, 0);
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (parser), "recurse"))
		g_object_set (G_OBJECT (parser), "recurse", FALSE, NULL);

	switch (totem_pl_parser_parse (parser, uri, TRUE)) {
	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
	case TOTEM_PL_PARSER_RESULT_IGNORED:
		/* maybe it's the actual stream URL, then */
		rb_iradio_source_add_station (source, uri, NULL, NULL);
		break;

	case TOTEM_PL_PARSER_RESULT_SUCCESS:
	case TOTEM_PL_PARSER_RESULT_ERROR:
		break;
	}
	g_object_unref (G_OBJECT (parser));
	g_free (real_uri);
}

static void
rb_iradio_source_first_time_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBIRadioSource *source)
{
	char *uri;

	if (source->priv->firstrun_done || !gconf_value_get_bool (entry->value))
		return;

	uri = gnome_vfs_get_uri_from_local_path (rb_file ("iradio-initial.pls"));
	rb_iradio_source_add_from_playlist (source, uri);
	g_free (uri);

	source->priv->firstrun_done = TRUE;
}

static void
stations_view_drag_data_received_cb (GtkWidget *widget,
				     GdkDragContext *dc,
				     gint x, gint y,
				     GtkSelectionData *selection_data, 
				     guint info, guint time,
				     RBIRadioSource *source)
{
	GList *list, *uri_list, *i;

	rb_debug ("parsing uri list");
	list = gnome_vfs_uri_list_parse ((char *)selection_data->data);

	if (list == NULL)
		return;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_prepend (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return;

	rb_debug ("adding uris");

	i = uri_list;
	while (i != NULL) {
		char *uri = NULL;

		/* as totem source says, "Super _NETSCAPE_URL trick" */
		if (info == 1) {
			if (i != NULL)
				g_free (i->data);
			i = i->next;
			if (i == NULL)
				break;
		}

		uri = i->data;
		if (uri != NULL) {
			rb_iradio_source_add_station (source, uri, NULL, NULL);
		}

		g_free (uri);
		
		if (i != NULL)
			i = i->next;
	}

	g_list_free (uri_list);
	return;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/IRadioSourcePopup");
	return TRUE;
}

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, "MusicNewInternetRadioStation");

	return actions;
}

static void
new_station_location_added (RBNewStationDialog *dialog,
			    const char         *uri,
			    RBIRadioSource     *source)
{
	rb_iradio_source_add_from_playlist (source, uri);
}

static void
rb_iradio_source_cmd_new_station (GtkAction *action,
				  RBIRadioSource *source)
{
	GtkWidget *dialog;

	rb_debug ("Got new station command");

	dialog = rb_new_station_dialog_new (source->priv->stations);
	g_signal_connect_object (dialog, "location-added",
				 G_CALLBACK (new_station_location_added),
				 source, 0);

	gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);
}

