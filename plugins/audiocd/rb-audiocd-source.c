/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005-2006 James Livingston  <doclivingston@gmail.com>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiocdsrc.h>

#include "rhythmdb.h"
#include "rb-shell.h"
#include "rb-audiocd-source.h"
#include "rb-device-source.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-source-toolbar.h"
#include "rb-shell-player.h"
#include "rb-audiocd-info.h"
#include "rb-musicbrainz-lookup.h"
#include "rb-application.h"

enum
{
	PROP_0,
	PROP_VOLUME,
};

static void rb_audiocd_source_dispose (GObject *object);
static void rb_audiocd_source_finalize (GObject *object);
static void rb_audiocd_source_constructed (GObject *object);
static void rb_audiocd_device_source_init (RBDeviceSourceInterface *interface);
static void impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void impl_delete_thyself (RBDisplayPage *page);

static guint impl_want_uri (RBSource *source, const char *uri);
static gboolean impl_uri_is_source (RBSource *source, const char *uri);
static RBEntryView *impl_get_entry_view (RBSource *source);

static gboolean update_artist_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_artist_sort_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_album_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_genre_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_year_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_disc_number_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);

static void rb_audiocd_source_load_disc_info (RBAudioCdSource *source);
static gboolean rb_audiocd_source_load_metadata (RBAudioCdSource *source);

static void reload_metadata_action_cb (GSimpleAction *, GVariant *, gpointer);
static void copy_tracks_action_cb (GSimpleAction *, GVariant *, gpointer);

static void extract_cell_data_func (GtkTreeViewColumn *column,
				    GtkCellRenderer *renderer,
				    GtkTreeModel *model,
				    GtkTreeIter *iter,
				    RBAudioCdSource *source);
static void extract_toggled_cb (GtkCellRendererToggle *renderer,
				char *path,
				RBAudioCdSource *source);
static void extract_column_clicked_cb (GtkTreeViewColumn *column,
				       RBAudioCdSource *source);

typedef struct
{
	gboolean extract;
} RBAudioCDEntryData;

struct _RBAudioCdSourcePrivate
{
	GVolume *volume;

	gchar *device_path;
	RBAudioCDInfo *disc_info;
	RBMusicBrainzData *mb_data;
	GList *releases;
	GList *tracks;

	GCancellable *cancel_disc_info;
	GtkWidget *infogrid;
	GtkWidget *info_bar;

	RBEntryView *entry_view;
	GtkWidget *artist_entry;
	GtkWidget *artist_sort_entry;
	GtkWidget *album_entry;
	GtkWidget *year_entry;
	GtkWidget *genre_entry;
	GtkWidget *disc_number_entry;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	RBAudioCdSource,
	rb_audiocd_source,
	RB_TYPE_SOURCE,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (RB_TYPE_DEVICE_SOURCE,
				       rb_audiocd_device_source_init))


/* entry type */
typedef struct _RhythmDBEntryType RBAudioCdEntryType;
typedef struct _RhythmDBEntryTypeClass RBAudioCdEntryTypeClass;

GType rb_audiocd_entry_type_get_type (void);

G_DEFINE_DYNAMIC_TYPE (RBAudioCdEntryType, rb_audiocd_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static void
rb_audiocd_entry_type_class_init (RBAudioCdEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
}

static void
rb_audiocd_entry_type_class_finalize (RBAudioCdEntryTypeClass *klass)
{
}

static void
rb_audiocd_entry_type_init (RBAudioCdEntryType *etype)
{
}

static RhythmDB *
get_db_for_source (RBAudioCdSource *source)
{
	RBShell *shell;
	RhythmDB *db;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

static void
rb_audiocd_device_source_init (RBDeviceSourceInterface *interface)
{
	/* nothing, the default implementations are fine */
}

static void
rb_audiocd_source_class_init (RBAudioCdSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->constructed = rb_audiocd_source_constructed;
	object_class->dispose = rb_audiocd_source_dispose;
	object_class->finalize = rb_audiocd_source_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	page_class->delete_thyself = impl_delete_thyself;

	source_class->can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_true_function;

	source_class->get_entry_view = impl_get_entry_view;
	source_class->uri_is_source = impl_uri_is_source;
	source_class->try_playlist = (RBSourceFeatureFunc) rb_true_function;	/* shouldn't need this. */
	source_class->want_uri = impl_want_uri;

	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_object ("volume",
							      "volume",
							      "volume",
							      G_TYPE_VOLUME,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_type_class_add_private (klass, sizeof (RBAudioCdSourcePrivate));
}

static void
rb_audiocd_source_class_finalize (RBAudioCdSourceClass *klass)
{
}

static void
rb_audiocd_source_init (RBAudioCdSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source,
						    RB_TYPE_AUDIOCD_SOURCE,
						    RBAudioCdSourcePrivate);
}

static void
rb_audiocd_source_finalize (GObject *object)
{
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (object);

	g_free (source->priv->device_path);

	if (source->priv->tracks) {
		g_list_free (source->priv->tracks);
	}

	if (source->priv->disc_info) {
		rb_audiocd_info_free (source->priv->disc_info);
	}
	if (source->priv->mb_data) {
		rb_musicbrainz_data_free (source->priv->mb_data);
	}

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->finalize (object);
}

static void
rb_audiocd_source_dispose (GObject *object)
{
	/*RBAudioCdSource *source = RB_AUDIOCD_SOURCE (object);*/

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->dispose (object);
}

static inline void
force_no_spacing (GtkWidget *widget)
{
	static GtkCssProvider *provider = NULL;

	if (provider == NULL) {
		const char *style =
			"GtkCheckButton {\n"
			"	-GtkCheckButton-indicator-spacing: 0\n"
			"}\n";

		provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_data (provider, style, -1, NULL);
	}

	gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
sort_order_changed_cb (GObject *object, GParamSpec *pspec, RBAudioCdSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
rb_audiocd_source_constructed (GObject *object)
{
	RBAudioCdSource *source;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *extract;
	GtkAccelGroup *accel_group;
	GtkBuilder *builder;
	GtkWidget *grid;
	GtkWidget *widget;
	GObject *plugin;
	RBShell *shell;
	RBShellPlayer *shell_player;
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	RhythmDBQuery *query;
	RhythmDBEntryType *entry_type;
	RBSourceToolbar *toolbar;
	int toggle_width;
	GActionEntry actions[] = {
		{ "audiocd-copy-tracks", copy_tracks_action_cb },
		{ "audiocd-reload-metadata", reload_metadata_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_audiocd_source_parent_class, constructed, object);
	source = RB_AUDIOCD_SOURCE (object);

	rb_device_source_set_display_details (RB_DEVICE_SOURCE (source));
	source->priv->device_path = g_volume_get_identifier (source->priv->volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "db", &db,
		      "shell-player", &shell_player,
		      "accel-group", &accel_group,
		      NULL);

	_rb_add_display_page_actions (G_ACTION_MAP (g_application_get_default ()), G_OBJECT (shell), actions, G_N_ELEMENTS (actions));

	/* source toolbar */
	toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (source), accel_group);
	g_object_unref (accel_group);

	g_object_get (source, "entry-type", &entry_type, NULL);
	query = rhythmdb_query_parse (db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);
	g_object_unref (entry_type);

	query_model = rhythmdb_query_model_new (db,
						query,
						(GCompareDataFunc) rhythmdb_query_model_track_sort_func,
						NULL, NULL, FALSE);
	rhythmdb_do_full_query_parsed (db, RHYTHMDB_QUERY_RESULTS (query_model), query);
	g_object_set (source, "query-model", query_model, NULL);
	rhythmdb_query_free (query);

	/* we want audio cds to sort by track# by default */
	source->priv->entry_view = rb_entry_view_new (db, G_OBJECT (shell_player), TRUE, FALSE);
	g_signal_connect_object (source->priv->entry_view,
				 "notify::sort-order",
				 G_CALLBACK (sort_order_changed_cb),
				 source, 0);
	rb_entry_view_set_sorting_order (source->priv->entry_view, "Track", GTK_SORT_ASCENDING);
	rb_entry_view_set_model (source->priv->entry_view, query_model);

	rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_TRACK_NUMBER, TRUE);
	rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_ARTIST, TRUE);
	rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_GENRE, FALSE);
	rb_entry_view_append_column (source->priv->entry_view, RB_ENTRY_VIEW_COL_DURATION, FALSE);

	/* enable in-place editing for titles, artists, and genres */
	rb_entry_view_set_column_editable (source->priv->entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_set_column_editable (source->priv->entry_view, RB_ENTRY_VIEW_COL_ARTIST, TRUE);
	rb_entry_view_set_column_editable (source->priv->entry_view, RB_ENTRY_VIEW_COL_GENRE, TRUE);

	/* create the 'extract' column */
	renderer = gtk_cell_renderer_toggle_new ();
	extract = gtk_tree_view_column_new ();
	gtk_tree_view_column_pack_start (extract, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (extract,
						 renderer,
						 (GtkTreeCellDataFunc) extract_cell_data_func,
						 source,
						 NULL);
	gtk_tree_view_column_set_clickable (extract, TRUE);
	widget = gtk_check_button_new ();
	g_object_set (widget, "active", TRUE, NULL);
	force_no_spacing (widget);
	gtk_widget_show_all (widget);
	g_signal_connect_object (extract, "clicked", G_CALLBACK (extract_column_clicked_cb), source, 0);
	gtk_tree_view_column_set_widget (extract, widget);

	g_signal_connect_object (renderer, "toggled", G_CALLBACK (extract_toggled_cb), source, 0);

	/* set column width */
	gtk_cell_renderer_get_preferred_width (renderer, GTK_WIDGET (source->priv->entry_view), NULL, &toggle_width);
	gtk_tree_view_column_set_sizing (extract, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (extract, toggle_width + 10);

	rb_entry_view_insert_column_custom (source->priv->entry_view, extract, "", "Extract", NULL, NULL, NULL, 1);
	gtk_widget_set_tooltip_text (gtk_tree_view_column_get_widget (extract),
	                             _("Select tracks to be extracted"));

	/* set up the album info widgets */
	g_object_get (source, "plugin", &plugin, NULL);
	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "album-info.ui", NULL);
	g_object_unref (plugin);

	source->priv->infogrid = GTK_WIDGET (gtk_builder_get_object (builder, "album_info"));
	g_assert (source->priv->infogrid != NULL);

	source->priv->artist_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_entry"));
	source->priv->artist_sort_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_sort_entry"));
	source->priv->album_entry = GTK_WIDGET (gtk_builder_get_object (builder, "album_entry"));
	source->priv->year_entry = GTK_WIDGET (gtk_builder_get_object (builder, "year_entry"));
	source->priv->genre_entry = GTK_WIDGET (gtk_builder_get_object (builder, "genre_entry"));
	source->priv->disc_number_entry = GTK_WIDGET (gtk_builder_get_object (builder, "disc_number_entry"));

	g_signal_connect_object (source->priv->artist_entry, "focus-out-event", G_CALLBACK (update_artist_cb), source, 0);
	g_signal_connect_object (source->priv->artist_sort_entry, "focus-out-event", G_CALLBACK (update_artist_sort_cb), source, 0);
	g_signal_connect_object (source->priv->album_entry, "focus-out-event", G_CALLBACK (update_album_cb), source, 0);
	g_signal_connect_object (source->priv->genre_entry, "focus-out-event", G_CALLBACK (update_genre_cb), source, 0);
	g_signal_connect_object (source->priv->year_entry, "focus-out-event", G_CALLBACK (update_year_cb), source, 0);
	g_signal_connect_object (source->priv->disc_number_entry, "focus-out-event", G_CALLBACK (update_disc_number_cb), source, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (toolbar), 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), source->priv->infogrid, 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (source->priv->entry_view), 0, 2, 1, 1);
	gtk_widget_set_margin_top (GTK_WIDGET (grid), 6);
	g_object_unref (builder);

	rb_source_bind_settings (RB_SOURCE (source), GTK_WIDGET (source->priv->entry_view), NULL, NULL, FALSE);

	gtk_widget_show_all (grid);
	gtk_container_add (GTK_CONTAINER (source), grid);

	source->priv->cancel_disc_info = g_cancellable_new ();
	rb_audiocd_source_load_disc_info (source);

	g_object_unref (db);
	g_object_unref (shell_player);
}

RBSource *
rb_audiocd_source_new (GObject *plugin,
		       RBShell *shell,
		       GVolume *volume)
{
	GObject *source;
	GSettings *settings;
	GMenu *toolbar;
	GtkBuilder *builder;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	char *name;
	char *path;

	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	name = g_strdup_printf ("audiocd: %s", path);
	g_free (path);

	g_object_get (shell, "db", &db, NULL);
	entry_type = g_object_new (rb_audiocd_entry_type_get_type (),
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   "type-data-size", sizeof(RBAudioCDEntryData),
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_object_unref (db);
	g_free (name);

	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "audiocd-toolbar.ui", NULL);
	toolbar = G_MENU (gtk_builder_get_object (builder, "audiocd-toolbar"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), toolbar);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.audiocd");
	source = g_object_new (RB_TYPE_AUDIOCD_SOURCE,
			       "entry-type", entry_type,
			       "volume", volume,
			       "shell", shell,
			       "plugin", plugin,
			       "load-status", RB_SOURCE_LOAD_STATUS_LOADING,
			       "show-browser", FALSE,
			       "settings", g_settings_get_child (settings, "source"),
			       "toolbar-menu", toolbar,
			       NULL);
	g_object_unref (settings);
	g_object_unref (builder);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_SOURCE (source);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		source->priv->volume = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		g_value_set_object (value, source->priv->volume);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
entry_set_string_prop (RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid,
		       gboolean default_to_empty,
		       const char *str)
{
	GValue value = {0,};

	if (!str) {
		if (default_to_empty) {
			str = "";
		} else {
			str = _("Unknown");
		}
	}

	if (!g_utf8_validate (str, -1, NULL)) {
		rb_debug ("Got invalid UTF-8 tag data");
		str = _("<Invalid unicode>");
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static void
clear_info_bar (RBAudioCdSource *source)
{
	if (source->priv->info_bar != NULL) {
		gtk_widget_hide (source->priv->info_bar);
		gtk_container_remove (GTK_CONTAINER (source->priv->infogrid), source->priv->info_bar);
		source->priv->info_bar = NULL;
	}
}

static void
show_info_bar (RBAudioCdSource *source, GtkWidget *info_bar)
{
	clear_info_bar (source);

	gtk_widget_show_all (info_bar);
	gtk_grid_attach (GTK_GRID (source->priv->infogrid), info_bar, 0, 0, 2, 1);
	source->priv->info_bar = info_bar;
}


static void
submit_info_bar_response_cb (GtkInfoBar *info_bar, gint response_id, RBAudioCdSource *source)
{
	GError *error = NULL;

	if (response_id == GTK_RESPONSE_OK) {
		char *submit_url;

		submit_url = rb_musicbrainz_create_submit_url (
			source->priv->disc_info->musicbrainz_disc_id,
			source->priv->disc_info->musicbrainz_full_disc_id);

		if (!gtk_show_uri (NULL, submit_url, GDK_CURRENT_TIME, &error)) {
			rb_debug ("Could not launch submit URL %s: %s", submit_url, error->message);
			g_error_free (error);
		}
		g_free (submit_url);
	}


	clear_info_bar (source);
}

static void
show_submit_info_bar (RBAudioCdSource *source)
{
	GtkWidget *info_bar;
	GtkWidget *label;
	GtkWidget *box;
	char *message;

	rb_debug ("showing musicbrainz submit info bar");

	info_bar = gtk_info_bar_new_with_buttons (_("S_ubmit Album"), GTK_RESPONSE_OK,
						  _("H_ide"), GTK_RESPONSE_CANCEL,
						  NULL);

	message = g_strdup_printf ("<b>%s</b>\n%s", _("Could not find this album on MusicBrainz."),
				   _("You can improve the MusicBrainz database by adding this album."));
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), message);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	g_free (message);

	box = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (box), label);

	g_signal_connect (G_OBJECT (info_bar), "response",
			  G_CALLBACK (submit_info_bar_response_cb), source);
	show_info_bar (source, info_bar);
}

static void
mb_error_info_bar_response_cb (GtkInfoBar *info_bar, gint response_id, RBAudioCdSource *source)
{
	if (response_id == GTK_RESPONSE_OK) {
		rb_audiocd_source_load_metadata (source);
	}
	clear_info_bar (source);
}

static void
show_lookup_error_info_bar (RBAudioCdSource *source, GError *error)
{
	GtkWidget *info_bar;
	GtkWidget *label;
	GtkWidget *box;
	char *message;

	rb_debug ("showing musicbrainz error info bar");

	info_bar = gtk_info_bar_new_with_buttons (_("_Retry"), GTK_RESPONSE_OK,
						  _("H_ide"), GTK_RESPONSE_CANCEL,
						  NULL);

	message = g_strdup_printf ("<b>%s</b>\n%s", _("Could not search MusicBrainz for album details."),
				   error->message);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), message);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	g_free (message);

	box = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (box), label);

	g_signal_connect (G_OBJECT (info_bar), "response",
			  G_CALLBACK (mb_error_info_bar_response_cb), source);
	show_info_bar (source, info_bar);
}

static void
cd_error_info_bar_response_cb (GtkInfoBar *info_bar, gint response_id, RBAudioCdSource *source)
{
	if (response_id == GTK_RESPONSE_OK) {
		rb_audiocd_source_load_disc_info (source);
	}
	clear_info_bar (source);
}

static void
show_cd_error_info_bar (RBAudioCdSource *source, GError *error)
{
	GtkWidget *info_bar;
	GtkWidget *label;
	GtkWidget *box;
	char *message;

	rb_debug ("showing cd read error info bar");

	info_bar = gtk_info_bar_new_with_buttons (_("_Retry"), GTK_RESPONSE_OK,
						  _("H_ide"), GTK_RESPONSE_CANCEL,
						  NULL);

	message = g_strdup_printf ("<b>%s</b>\n%s", _("Could not read the CD device."),
				   error->message);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), message);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	g_free (message);

	box = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (box), label);

	g_signal_connect (G_OBJECT (info_bar), "response",
			  G_CALLBACK (cd_error_info_bar_response_cb), source);
	show_info_bar (source, info_bar);
}


static void
apply_musicbrainz_release (RBAudioCdSource *source, RBMusicBrainzData *release)
{
	RBMusicBrainzData *medium;
	RhythmDB *db;
	GList *l;
	const char *value;
	int disc_num = 0;
	gulong release_date = 0;
	const char *album;
	const char *album_id;
	const char *album_artist;
	const char *album_artist_id;
	const char *album_artist_sortname;

	medium = rb_musicbrainz_data_find_child (release,
						 "disc-id",
						 source->priv->disc_info->musicbrainz_disc_id);
	g_assert (medium != NULL);

	/* set album stuff in widgets */
	album = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_ALBUM);
	if (album != NULL) {
		rb_debug ("album title: %s", album);
		gtk_entry_set_text (GTK_ENTRY (source->priv->album_entry), album);
		g_object_set (source, "name", album, NULL);
	}

	album_artist = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST);
	if (album_artist != NULL) {
		rb_debug ("album artist: %s", album_artist);
		gtk_entry_set_text (GTK_ENTRY (source->priv->artist_entry), album_artist);
	}

	album_artist_sortname = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_SORTNAME);
	if (album_artist_sortname != NULL) {
		rb_debug ("album artist sortname: %s", album_artist_sortname);
		gtk_entry_set_text (GTK_ENTRY (source->priv->artist_sort_entry), album_artist_sortname);
	}

	value = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_DATE);
	if (value != NULL) {
		int year = 1;
		int month = 1;
		int day = 1;

		if (sscanf (value, "%u-%u-%u", &year, &month, &day) > 0) {
			GDate date;
			char *year_text;

			year_text = g_strdup_printf ("%d", year);
			gtk_entry_set_text (GTK_ENTRY (source->priv->year_entry), year_text);
			g_free (year_text);

			g_date_set_dmy (&date,
					(day == 0) ? 1 : day,
					(month == 0) ? 1 : month,
					year);
			release_date = g_date_get_julian (&date);
		} else {
			rb_debug ("unable to parse release date: %s", value);
		}
	}

	value = rb_musicbrainz_data_get_attr_value (medium, RB_MUSICBRAINZ_ATTR_DISC_NUMBER);
	if (value != NULL) {
		disc_num = strtol (value, NULL, 10);	/* 0 is ok if this fails */
		gtk_entry_set_text (GTK_ENTRY (source->priv->disc_number_entry), value);
		rb_debug ("disc number %d", disc_num);
	}

	album_id = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_ALBUM_ID);
	rb_debug ("musicbrainz_albumid: %s", album_id);

	album_artist_id = rb_musicbrainz_data_get_attr_value (release, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_ID);
	rb_debug ("musicbrainz_albumartistid: %s", album_artist_id);

	db = get_db_for_source (source);

	/* apply things to tracks */
	l = rb_musicbrainz_data_get_children (medium);
	for (l = rb_musicbrainz_data_get_children (medium); l != NULL; l = l->next) {
		RhythmDBEntry *entry;
		RBMusicBrainzData *mb_track = l->data;
		GList *tl;
		GValue value = {0, };
		const char *attr;
		int mb_track_num;
	
		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_TRACK_NUMBER);
		rb_debug ("processing musicbrainz track %s", attr);
		mb_track_num = strtol (attr, 0, 10);

		/* find matching track */
		entry = NULL;
		for (tl = source->priv->tracks; tl != NULL; tl = tl->next) {
			gulong tn;
			tn = rhythmdb_entry_get_ulong (tl->data, RHYTHMDB_PROP_TRACK_NUMBER);
			if (tn == mb_track_num) {
				entry = tl->data;
				break;
			}
		}

		if (entry == NULL) {
			g_warning ("couldn't find track entry for musicbrainz track %d",
				   mb_track_num);
			continue;
		}

		/* apply release stuff */
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM, FALSE, album);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, TRUE, album_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, TRUE, album_artist_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST, TRUE, album_artist);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, TRUE, album_artist_sortname);

		if (release_date != 0) {
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, release_date);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DATE, &value);
			g_value_unset (&value);
		}

		/* apply medium stuff */
		if (disc_num != 0) {
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, disc_num);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
			g_value_unset (&value);
		}

		/* apply track stuff */
		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_TITLE);
		rb_debug ("title: %s", attr);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_TITLE, FALSE, attr);

		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_TRACK_ID);
		rb_debug ("musicbrainz track id: %s", attr);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, TRUE, attr);

		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_ARTIST);
		rb_debug ("artist: %s", attr);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST, FALSE, attr);

		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_ARTIST_SORTNAME);
		rb_debug ("artist sortname: %s", attr);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, TRUE, attr);

		attr = rb_musicbrainz_data_get_attr_value (mb_track, RB_MUSICBRAINZ_ATTR_ARTIST_ID);
		rb_debug ("musicbrainz_artistid: %s", attr);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, TRUE, attr);
	}

	rhythmdb_commit (db);

	g_object_unref (db);
}

static void
album_combo_changed_cb (GtkWidget *combo, RBAudioCdSource *source)
{
	GList *l;
	int active;

	active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
	if (active == -1)
		return;

	l = g_list_nth (source->priv->releases, active);
	if (l != NULL) {
		apply_musicbrainz_release (source, l->data);
	}
}

static void
show_multiple_release_info_bar (RBAudioCdSource *source)
{
	GtkWidget *info_bar;
	GtkWidget *label;
	GtkWidget *box;
	GtkWidget *combo;
	GList *l;

	rb_debug ("showing musicbrainz multiple release info bar");

	info_bar = gtk_info_bar_new ();

	label = gtk_label_new (_("This disc matches multiple albums. Select the correct album."));
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	box = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (box), label);

	combo = gtk_combo_box_text_new ();
	for (l = source->priv->releases; l != NULL; l = l->next) {
		const char *artist;
		const char *album;
		const char *country;
		char *text;

		artist = rb_musicbrainz_data_get_attr_value (l->data, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST);
		album = rb_musicbrainz_data_get_attr_value (l->data, RB_MUSICBRAINZ_ATTR_ALBUM);
		country = rb_musicbrainz_data_get_attr_value (l->data, RB_MUSICBRAINZ_ATTR_COUNTRY);
		text = g_strdup_printf ("%s - %s (%s)", artist, album, country);
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), NULL, text);
		g_free (text);
	}

	g_signal_connect (combo, "changed", G_CALLBACK (album_combo_changed_cb), source);
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	box = gtk_info_bar_get_action_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (box), combo);

	show_info_bar (source, info_bar);
}

static void
musicbrainz_lookup_cb (GObject *obj, GAsyncResult *result, RBAudioCdSource **source_ptr)
{
	RBAudioCdSource *source;
	GError *error = NULL;
	GList *l;

	source = *source_ptr;
	if (source == NULL) {
		rb_debug ("cd source was destroyed");
		g_free (source_ptr);
		return;
	}
	g_object_remove_weak_pointer (G_OBJECT (source), (gpointer *)source_ptr);
	g_free (source_ptr);

	if (source->priv->releases != NULL) {
		g_list_free (source->priv->releases);
		source->priv->releases = NULL;
	}
	if (source->priv->mb_data != NULL) {
		rb_musicbrainz_data_free (source->priv->mb_data);
	}

	g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);

	source->priv->mb_data = rb_musicbrainz_lookup_finish (result, &error);
	if (error != NULL) {
		if (error->domain == G_IO_ERROR &&
		    error->code == G_IO_ERROR_CANCELLED) {
			/* do nothing */
		} else if (error->domain == RB_MUSICBRAINZ_ERROR &&
		    error->code == RB_MUSICBRAINZ_ERROR_NOT_FOUND) {
			show_submit_info_bar (source);
		} else {
			show_lookup_error_info_bar (source, error);
		}
		g_clear_error (&error);
		return;
	}

	/* find the release and medium matching the disc id */
	l = rb_musicbrainz_data_get_children (source->priv->mb_data);
	if (l == NULL) {
		show_submit_info_bar (source);
		return;
	}

	for (; l != NULL; l = l->next) {
		RBMusicBrainzData *m;

		m = rb_musicbrainz_data_find_child (l->data,
						    "disc-id",
						    source->priv->disc_info->musicbrainz_disc_id);
		if (m == NULL)
			continue;

		source->priv->releases = g_list_append (source->priv->releases, l->data);
	}

	if (source->priv->releases == NULL) {
		show_submit_info_bar (source);
	} else if (g_list_length (source->priv->releases) > 1) {
		show_multiple_release_info_bar (source);
	} else {
		apply_musicbrainz_release (source, source->priv->releases->data);
	}
}

static gboolean
rb_audiocd_source_load_metadata (RBAudioCdSource *source)
{
	RBAudioCdSource **source_ptr;
	const char *disc_includes[] = { "recordings", "artist-credits", NULL };

	if (source->priv->disc_info->musicbrainz_disc_id == NULL) {
		rb_debug ("not doing musicbrainz lookup as we don't have a disc id");
		return FALSE;
	}

	source_ptr = g_new0 (RBAudioCdSource *, 1);
	*source_ptr = source;
	g_object_add_weak_pointer (G_OBJECT (source), (gpointer *)source_ptr);
	rb_debug ("looking up musicbrainz data for disc %s",
		  source->priv->disc_info->musicbrainz_disc_id);
	rb_musicbrainz_lookup ("discid",
			       source->priv->disc_info->musicbrainz_disc_id,
			       disc_includes,
			       source->priv->cancel_disc_info,
			       (GAsyncReadyCallback) musicbrainz_lookup_cb,
			       source_ptr);
	return TRUE;
}

static void
reload_metadata_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (data);
	rb_audiocd_source_load_metadata (source);
}

static void
disc_info_cb (GObject *obj, GAsyncResult *result, RBAudioCdSource **source_ptr)
{
	RBAudioCdSource *source;
	GError *error = NULL;
	RhythmDB *db;
	int i;

	source = *source_ptr;
	if (source == NULL) {
		rb_debug ("cd source was destroyed");
		g_free (source_ptr);
		return;
	}
	g_object_remove_weak_pointer (G_OBJECT (source), (gpointer *)source_ptr);
	g_free (source_ptr);

	source->priv->disc_info = rb_audiocd_info_finish (result, &error);
	if (error != NULL) {
		if (error->domain == G_IO_ERROR &&
		    error->code == G_IO_ERROR_CANCELLED) {
			/* do nothing */
		} else {
			show_cd_error_info_bar (source, error);
		}
		g_clear_error (&error);

		g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);
		return;
	}

	if (source->priv->disc_info->album_artist != NULL) {
		gtk_entry_set_text (GTK_ENTRY (source->priv->artist_entry), source->priv->disc_info->album_artist);
	}
	if (source->priv->disc_info->album != NULL) {
		gtk_entry_set_text (GTK_ENTRY (source->priv->album_entry), source->priv->disc_info->album);
		g_object_set (source, "name", source->priv->disc_info->album, NULL);
	}
	if (source->priv->disc_info->genre != NULL) {
		gtk_entry_set_text (GTK_ENTRY (source->priv->genre_entry), source->priv->disc_info->genre);
	}

	db = get_db_for_source (source);
	for (i = 0; i < source->priv->disc_info->num_tracks; i++) {
		RhythmDBEntry *entry;
		char *audio_path;
		GValue value = {0, };
		gchar *str;
		RhythmDBEntryType *entry_type;
		RBAudioCDEntryData *extra_data;
		RBAudioCDTrack *track = &source->priv->disc_info->tracks[i];

		/* ignore data tracks */
		if (track->is_audio == FALSE) {
			rb_debug ("ignoring non-audio track %d", track->track_num);
			continue;
		}

		audio_path = g_strdup_printf ("cdda://%s#%d", source->priv->disc_info->device, track->track_num);

		g_object_get (source, "entry-type", &entry_type, NULL);
		rb_debug ("creating entry for track %d from %s", track->track_num, source->priv->disc_info->device);
		entry = rhythmdb_entry_new (db, entry_type, audio_path);
		g_object_unref (entry_type);
		if (entry == NULL) {
			g_warning ("unable to create entry %s", audio_path);
			g_free (audio_path);
			continue;
		}

		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->track_num);
		rhythmdb_entry_set (db, entry,
				    RHYTHMDB_PROP_TRACK_NUMBER,
				    &value);
		g_value_unset (&value);

		g_value_init (&value, G_TYPE_STRING);
		str = g_strdup_printf (_("Track %u"), track->track_num);
		g_value_take_string (&value, str);
		rhythmdb_entry_set (db, entry,
				    RHYTHMDB_PROP_TITLE,
				    &value);
		g_value_unset (&value);

		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->duration / 1000);
		rhythmdb_entry_set (db, entry,
				    RHYTHMDB_PROP_DURATION,
				    &value);
		g_value_unset (&value);

		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST, FALSE, track->artist);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_TITLE, FALSE, track->title);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM, FALSE, source->priv->disc_info->album);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST, FALSE, source->priv->disc_info->album_artist);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_GENRE, FALSE, source->priv->disc_info->genre);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MEDIA_TYPE, TRUE, "audio/x-raw-int");

		extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
		extra_data->extract = TRUE;

		rhythmdb_commit (db);
		g_free (audio_path);

		source->priv->tracks = g_list_prepend (source->priv->tracks, entry);
	}

	g_object_unref (db);

	if (rb_audiocd_source_load_metadata (source) == FALSE) {
		g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);
	}
}

static void
rb_audiocd_source_load_disc_info (RBAudioCdSource *source)
{
	RBAudioCdSource **source_ptr;

	source_ptr = g_new0 (RBAudioCdSource *, 1);
	*source_ptr = source;
	g_object_add_weak_pointer (G_OBJECT (source), (gpointer *)source_ptr);
	rb_audiocd_info_get (source->priv->device_path,
			     source->priv->cancel_disc_info,
			     (GAsyncReadyCallback) disc_info_cb,
			     source_ptr);
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (page);

	rb_debug ("audio cd ejected");

	if (source->priv->cancel_disc_info) {
		g_cancellable_cancel (source->priv->cancel_disc_info);
	}

	db = get_db_for_source (source);

	g_object_get (page, "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_object_unref (entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);
}

gboolean
rb_audiocd_is_mount_audiocd (GMount *mount)
{
	gboolean result = FALSE;
	char **types;
	guint i;
	GError *error = NULL;

	types = g_mount_guess_content_type_sync (mount, FALSE, NULL, &error);
	if (types == NULL) {
		rb_debug ("error guessing content type: %s", error->message);
		g_clear_error (&error);
	} else {
		for (i = 0; types[i] != NULL; i++) {
			if (g_str_equal (types[i], "x-content/audio-cdda") != FALSE) {
				result = TRUE;
				break;
			}
		}
		g_strfreev (types);
	}
	return result;
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	GVolume *volume;
	GMount *mount;
	GFile *file;
	int retval;

	retval = 0;

	file = g_file_new_for_uri (uri);
	if (g_file_has_uri_scheme (file, "cdda") == FALSE) {
		g_object_unref (file);
		return 0;
	}

	g_object_get (G_OBJECT (source),
		      "volume", &volume,
		      NULL);
	if (volume == NULL)
		return 0;

	mount = g_volume_get_mount (volume);
	if (mount) {
		GFile *root;

		root = g_mount_get_root (mount);
		retval = g_file_equal (root, file) ? 100 : 0;
		g_object_unref (mount);
		g_object_unref (root);
	}
	g_object_unref (file);

	return retval;
}

static RBEntryView *
impl_get_entry_view (RBSource *source)
{
	RBAudioCdSource *cdsource = RB_AUDIOCD_SOURCE (source);
	return cdsource->priv->entry_view;
}

static gboolean
impl_uri_is_source (RBSource *source, const char *uri)
{
	if (impl_want_uri (source, uri) == 100)
		return TRUE;
	return FALSE;
}

static void
update_tracks (RBAudioCdSource *source, RhythmDBPropType property, GValue *value)
{
	RhythmDB *db;
	GList *i;

	db = get_db_for_source (source);

	for (i = source->priv->tracks; i != NULL; i = i->next) {
		rhythmdb_entry_set (db, i->data, property, value);
	}

	rhythmdb_commit (db);
	g_object_unref (db);
}

static void
update_tracks_string (RBAudioCdSource *source, RhythmDBPropType property, const char *str)
{
	GValue v = {0, };
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, str);
	update_tracks (source, property, &v);
	g_value_unset (&v);
}

static gboolean
update_artist_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	update_tracks_string (source, RHYTHMDB_PROP_ALBUM_ARTIST, gtk_entry_get_text (GTK_ENTRY (widget)));
	return FALSE;
}

static gboolean
update_artist_sort_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	update_tracks_string (source, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, gtk_entry_get_text (GTK_ENTRY (widget)));
	return FALSE;
}

static gboolean
update_album_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	update_tracks_string (source, RHYTHMDB_PROP_ALBUM, gtk_entry_get_text (GTK_ENTRY (widget)));
	return FALSE;
}

static gboolean
update_genre_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	update_tracks_string (source, RHYTHMDB_PROP_GENRE, gtk_entry_get_text (GTK_ENTRY (widget)));
	return FALSE;
}

static gboolean
update_year_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	const char *text;
	int year;
	GDate date;
	GValue v = {0, };

	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (text[0] == '\0') {
		return FALSE;
	}

	year = strtol (text, NULL, 10);
	g_date_clear (&date, 1);
	g_date_set_dmy (&date, 1, 1, year);

	g_value_init (&v, G_TYPE_ULONG);
	g_value_set_ulong (&v, g_date_get_julian (&date));
	update_tracks (source, RHYTHMDB_PROP_DATE, &v);
	g_value_unset (&v);

	return FALSE;
}

static gboolean
update_disc_number_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source)
{
	GValue v = {0, };

	g_value_init (&v, G_TYPE_ULONG);
	g_value_set_ulong (&v, strtoul (gtk_entry_get_text (GTK_ENTRY (widget)), NULL, 10));
	update_tracks (source, RHYTHMDB_PROP_DISC_NUMBER, &v);
	g_value_unset (&v);

	return FALSE;
}

static void
extract_cell_data_func (GtkTreeViewColumn *column,
			GtkCellRenderer *renderer,
			GtkTreeModel *tree_model,
			GtkTreeIter *iter,
			RBAudioCdSource *source)
{
	RBAudioCDEntryData *extra_data;
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (tree_model), iter);
	if (entry != NULL) {
		extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
		gtk_cell_renderer_toggle_set_active (GTK_CELL_RENDERER_TOGGLE (renderer), extra_data->extract);
		rhythmdb_entry_unref (entry);
	}
}

static void
extract_toggled_cb (GtkCellRendererToggle *renderer, char *path_str, RBAudioCdSource *source)
{
	RhythmDBQueryModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;

	g_object_get (source, "query-model", &model, NULL);

	path = gtk_tree_path_new_from_string (path_str);
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path)) {
		RhythmDBEntry *entry;

		entry = rhythmdb_query_model_iter_to_entry (model, &iter);
		if (entry != NULL) {
			RBAudioCDEntryData *extra_data;

			extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
			extra_data->extract = !extra_data->extract;
			rhythmdb_entry_unref (entry);

			gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
		}
	}
	gtk_tree_path_free (path);
	g_object_unref (model);
}


static gboolean
set_extract (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	RBAudioCDEntryData *extra_data;
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model),
						    iter);
	if (entry != NULL) {
		extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
		extra_data->extract = GPOINTER_TO_INT (data);

		gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, iter);
		rhythmdb_entry_unref (entry);
	}
	return FALSE;
}

static void
extract_column_clicked_cb (GtkTreeViewColumn *column, RBAudioCdSource *source)
{
	RhythmDBQueryModel *model;
	gboolean extract;
	GtkWidget *checkbox;

	/* toggle the state of the checkbox in the header */
	checkbox = gtk_tree_view_column_get_widget (column);

	g_object_get (checkbox, "active", &extract, NULL);
	extract = !extract;
	g_object_set (checkbox, "active", extract, NULL);

	/* set the extraction state for all tracks to match */
	g_object_get (source, "query-model", &model, NULL);
	gtk_tree_model_foreach (GTK_TREE_MODEL (model), set_extract, GINT_TO_POINTER (extract));
	g_object_unref (model);
}

static gboolean
copy_entry (RhythmDBQueryModel *model,
	    GtkTreePath *path,
	    GtkTreeIter *iter,
	    GList **list)
{
	RBAudioCDEntryData *extra_data;
	RhythmDBEntry *entry;
	GList *l;

	entry = rhythmdb_query_model_iter_to_entry (model, iter);
	extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
	if (extra_data->extract) {
		rb_debug ("adding track %s to transfer list",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		l = g_list_append (*list, entry);
		*list = l;
	} else {
		rb_debug ("skipping track %s",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		rhythmdb_entry_unref (entry);
	}
	return FALSE;
}

static void
copy_tracks_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBAudioCdSource *source = RB_AUDIOCD_SOURCE (data);
	RBShell *shell;
	RBSource *library;
	RhythmDBQueryModel *model;
	GList *list = NULL;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "library-source", &library, NULL);
	g_object_unref (shell);

	g_object_get (source, "query-model", &model, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (model), (GtkTreeModelForeachFunc)copy_entry, &list);
	if (list != NULL) {
		rb_source_paste (library, list);
		g_list_free (list);
	}

	g_object_unref (model);
	g_object_unref (library);
}

void
_rb_audiocd_source_register_type (GTypeModule *module)
{
	rb_audiocd_source_register_type (module);
	rb_audiocd_entry_type_register_type (module);
}
