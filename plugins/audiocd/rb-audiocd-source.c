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

/*
 * TODO
 *    * save user-edited metadata somewhere (use S-J stuff?)
 */

#include "config.h"

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/cdda/gstcddabasesrc.h>

#include "rb-plugin.h"
#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-shell.h"
#include "rb-audiocd-source.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-builder-helpers.h"

#ifdef HAVE_SJ_METADATA_GETTER
#include "sj-metadata-getter.h"
#include "sj-structures.h"
#endif

enum
{
	PROP_0,
	PROP_SEARCH_TYPE
};

static void rb_audiocd_source_dispose (GObject *object);
static void rb_audiocd_source_finalize (GObject *object);
static void rb_audiocd_source_constructed (GObject *object);
static void impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean impl_show_popup (RBSource *source);
static guint impl_want_uri (RBSource *source, const char *uri);
static gboolean impl_uri_is_source (RBSource *source, const char *uri);

static void impl_delete_thyself (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);

static void impl_pack_paned (RBBrowserSource *source, GtkWidget *paned);

static gpointer rb_audiocd_load_songs (RBAudioCdSource *source);
static void rb_audiocd_load_metadata (RBAudioCdSource *source, RhythmDB *db);
static void rb_audiocd_load_metadata_cancel (RBAudioCdSource *source);

static gboolean update_artist_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_artist_sort_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_album_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_genre_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_year_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_disc_number_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
#if defined(HAVE_SJ_METADATA_GETTER)
static void info_bar_response_cb (GtkInfoBar *info_bar, gint response_id, RBAudioCdSource *source);
#endif

#if defined(HAVE_SJ_METADATA_GETTER)
static void reload_metadata_cmd (GtkAction *action, RBAudioCdSource *source);
#endif
static void copy_tracks_cmd (GtkAction *action, RBAudioCdSource *source);

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

typedef struct
{
	gchar *device_path;
	GList *tracks;

	GstElement *pipeline;
	GstElement *cdda;
	GstElement *fakesink;

	GtkWidget *box;
	GtkWidget *artist_entry;
	GtkWidget *artist_sort_entry;
	GtkWidget *album_entry;
	GtkWidget *year_entry;
	GtkWidget *genre_entry;
	GtkWidget *disc_number_entry;

#ifdef HAVE_SJ_METADATA_GETTER
	SjMetadataGetter *metadata;

	GtkWidget *info_bar;
	GtkWidget *info_bar_label;

	char *submit_url;
#endif

	GtkActionGroup *action_group;
} RBAudioCdSourcePrivate;

RB_PLUGIN_DEFINE_TYPE (RBAudioCdSource, rb_audiocd_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define AUDIOCD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOCD_SOURCE, RBAudioCdSourcePrivate))

#ifdef HAVE_SJ_METADATA_GETTER
static AlbumDetails* multiple_album_dialog (GList *albums, RBAudioCdSource *source);
#endif

static GtkActionEntry rb_audiocd_source_actions[] = {
	{ "AudioCdCopyTracks", GTK_STOCK_CDROM, N_("_Extract to Library"), NULL,
	  N_("Copy tracks to the library"),
	  G_CALLBACK (copy_tracks_cmd) },
#if defined(HAVE_SJ_METADATA_GETTER)
	{ "AudioCdSourceReloadMetadata", GTK_STOCK_REFRESH, N_("Reload"), NULL,
	N_("Reload Album Information"),
	G_CALLBACK (reload_metadata_cmd) },
#endif
};

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
rb_audiocd_source_class_init (RBAudioCdSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->constructed = rb_audiocd_source_constructed;
	object_class->dispose = rb_audiocd_source_dispose;
	object_class->finalize = rb_audiocd_source_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	/* don't bother showing the browser/search bits */
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_uri_is_source = impl_uri_is_source;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_true_function;	/* shouldn't need this. */
	source_class->impl_want_uri = impl_want_uri;

	/* add the browser below the album info section */
	browser_source_class->impl_pack_paned = impl_pack_paned;

	g_object_class_override_property (object_class,
					  PROP_SEARCH_TYPE,
					  "search-type");

	g_type_class_add_private (klass, sizeof (RBAudioCdSourcePrivate));
}

static void
rb_audiocd_source_init (RBAudioCdSource *self)
{
}

static void
rb_audiocd_source_finalize (GObject *object)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (object);

	g_free (priv->device_path);

#ifdef HAVE_SJ_METADATA_GETTER
	g_free (priv->submit_url);
	priv->submit_url = NULL;
#endif

	if (priv->tracks) {
		g_list_free (priv->tracks);
		priv->tracks = NULL;
	}

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->finalize (object);
}

static void
rb_audiocd_source_dispose (GObject *object)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (object);

	if (priv->action_group != NULL) {
		g_object_unref (priv->action_group);
		priv->action_group = NULL;
	}

	if (priv->pipeline) {
		gst_object_unref (GST_OBJECT (priv->pipeline));
		priv->pipeline = NULL;
	}

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->dispose (object);
}

static inline void
force_no_spacing (GtkWidget *widget)
{
	static gboolean first_time = TRUE;
	if (first_time) {
		gtk_rc_parse_string ("\n"
				     "   style \"audiocd-extract-header-style\"\n"
				     "   {\n"
				     "      GtkCheckButton::indicator-spacing=0\n"
				     "   }\n"
				     "\n"
				     "   widget \"*.audiocd-extract-header\" style \"audiocd-extract-header-style\"\n"
				     "\n");
		first_time = FALSE;
	}

	gtk_widget_set_name (widget, "audiocd-extract-header");
}

static void
rb_audiocd_source_constructed (GObject *object)
{
	RBAudioCdSourcePrivate *priv;
	RBAudioCdSource *source;
	RBEntryView *entry_view;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *extract;
	GtkWidget *widget;
	GtkAction *action;
	RBPlugin *plugin;
	RBShell *shell;
	char *ui_file;
	int toggle_width;

	RB_CHAIN_GOBJECT_METHOD (rb_audiocd_source_parent_class, constructed, object);
	source = RB_AUDIOCD_SOURCE (object);
	priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	g_object_set (G_OBJECT (source), "name", "Unknown Audio", NULL);

	g_object_get (source, "shell", &shell, NULL);
	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "AudioCdActions",
							       NULL, 0, NULL);
	_rb_action_group_add_source_actions (priv->action_group,
					     G_OBJECT (shell),
					     rb_audiocd_source_actions,
					     G_N_ELEMENTS (rb_audiocd_source_actions));
	g_object_unref (shell);

	action = gtk_action_group_get_action (priv->action_group,
					      "AudioCdCopyTracks");
	/* Translators: this is the toolbar button label
	   for Copy to Library action. */
	g_object_set (action, "short-label", _("Extract"), NULL);

#if !defined(HAVE_SJ_METADATA_GETTER)
	action = gtk_action_group_get_action (priv->action_group, "AudioCdSourceReloadMetadata");
	g_object_set (action, "visible", FALSE, NULL);
#endif


	/* we want audio cds to sort by track# by default */
	entry_view = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_set_sorting_order (entry_view, "Track", GTK_SORT_ASCENDING);

	/* enable in-place editing for titles, artists, and genres */
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_ARTIST, TRUE);
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_GENRE, TRUE);

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
	gtk_cell_renderer_get_size (renderer, GTK_WIDGET (entry_view), NULL, NULL, NULL, &toggle_width, NULL);
	gtk_tree_view_column_set_sizing (extract, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (extract, toggle_width + 10);

	rb_entry_view_insert_column_custom (entry_view, extract, "", "Extract", NULL, NULL, NULL, 1);
	gtk_widget_set_tooltip_text (gtk_tree_view_column_get_widget (extract),
	                             _("Select tracks to be extracted"));

	/* hide the 'album' column */
	gtk_tree_view_column_set_visible (rb_entry_view_get_column (entry_view, RB_ENTRY_VIEW_COL_ALBUM), FALSE);

	/* set up the album info widgets */
	g_object_get (source, "plugin", &plugin, NULL);
	ui_file = rb_plugin_find_file (plugin, "album-info.ui");
	g_object_unref (plugin);

	if (ui_file == NULL) {
		g_warning ("couldn't find album-info.ui");
	} else {
		RBAudioCdSourcePrivate *priv;
		GtkWidget *table;
		GtkBuilder *builder;
#if defined(HAVE_SJ_METADATA_GETTER)
		GtkWidget *box;
		char *message;
#endif

		priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

		builder = rb_builder_load (ui_file, NULL);
		g_free (ui_file);

		table = GTK_WIDGET (gtk_builder_get_object (builder, "album_info"));
		g_assert (table != NULL);

#if defined(HAVE_SJ_METADATA_GETTER)
		/* Info bar for non-Musicbrainz data */
		priv->info_bar = gtk_info_bar_new_with_buttons (_("S_ubmit Album"), GTK_RESPONSE_OK,
								_("Hide"), GTK_RESPONSE_CANCEL,
								NULL);
		message = g_strdup_printf ("<b>%s</b>\n%s", _("Could not find this album on MusicBrainz."),
					   _("You can improve the MusicBrainz database by adding this album."));
		priv->info_bar_label = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (priv->info_bar_label), message);
		gtk_label_set_justify (GTK_LABEL (priv->info_bar_label), GTK_JUSTIFY_LEFT);
		g_free (message);
		box = gtk_info_bar_get_content_area (GTK_INFO_BAR (priv->info_bar));
		gtk_container_add (GTK_CONTAINER (box), priv->info_bar_label);
		gtk_widget_show_all (box);
		gtk_widget_set_no_show_all (priv->info_bar, TRUE);
		g_signal_connect (G_OBJECT (priv->info_bar), "response",
				  G_CALLBACK (info_bar_response_cb), source);
		gtk_table_attach_defaults (GTK_TABLE (table), priv->info_bar, 0, 2, 0, 1);
#endif

		priv->artist_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_entry"));
		priv->artist_sort_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_sort_entry"));
		priv->album_entry = GTK_WIDGET (gtk_builder_get_object (builder, "album_entry"));
		priv->year_entry = GTK_WIDGET (gtk_builder_get_object (builder, "year_entry"));
		priv->genre_entry = GTK_WIDGET (gtk_builder_get_object (builder, "genre_entry"));
		priv->disc_number_entry = GTK_WIDGET (gtk_builder_get_object (builder, "disc_number_entry"));

		g_signal_connect_object (priv->artist_entry, "focus-out-event", G_CALLBACK (update_artist_cb), source, 0);
		g_signal_connect_object (priv->artist_sort_entry, "focus-out-event", G_CALLBACK (update_artist_sort_cb), source, 0);
		g_signal_connect_object (priv->album_entry, "focus-out-event", G_CALLBACK (update_album_cb), source, 0);
		g_signal_connect_object (priv->genre_entry, "focus-out-event", G_CALLBACK (update_genre_cb), source, 0);
		g_signal_connect_object (priv->year_entry, "focus-out-event", G_CALLBACK (update_year_cb), source, 0);
		g_signal_connect_object (priv->disc_number_entry, "focus-out-event", G_CALLBACK (update_disc_number_cb), source, 0);

		gtk_box_pack_start (GTK_BOX (priv->box), table, FALSE, FALSE, 0);
		gtk_box_reorder_child (GTK_BOX (priv->box), table, 0);
		g_object_unref (builder);
	}

	g_object_ref (G_OBJECT (source));
	g_thread_create ((GThreadFunc)rb_audiocd_load_songs, source, FALSE, NULL);
}

RBSource *
rb_audiocd_source_new (RBPlugin *plugin,
		       RBShell *shell,
		       GVolume *volume)
{
	GObject *source;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	char *name;
	char *path;

	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	name = g_strdup_printf ("audiocd: %s", path);
	g_free (path);

	g_object_get (shell, "db", &db, NULL);
	entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
				   "db", db,
				   "name", name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   "type-data-size", sizeof(RBAudioCDEntryData),
				   NULL);
	entry_type->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	entry_type->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	rhythmdb_register_entry_type (db, entry_type);
	g_object_unref (db);
	g_free (name);

	source = g_object_new (RB_TYPE_AUDIOCD_SOURCE,
			       "entry-type", entry_type,
			       "volume", volume,
			       "shell", shell,
			       "sorting-key", NULL,
			       "source-group", RB_SOURCE_GROUP_DEVICES,
			       "plugin", plugin,
			       NULL);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_SOURCE (source);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_SEARCH_TYPE:
		/* ignored */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_SEARCH_TYPE:
		g_value_set_enum (value, RB_SOURCE_SEARCH_NONE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_pack_paned (RBBrowserSource *source, GtkWidget *paned)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	priv->box = gtk_vbox_new (FALSE, 6);
	gtk_widget_show_all (priv->box);
	gtk_container_add (GTK_CONTAINER (source), priv->box);
	gtk_box_pack_start (GTK_BOX (priv->box), paned, TRUE, TRUE, 0);
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

static RhythmDBEntry *
rb_audiocd_create_track_entry (RBAudioCdSource *source,
			       RhythmDB *db,
			       guint track_number)
{
	RhythmDBEntry *entry;
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	char *audio_path;
	guint64 duration;
	GValue value = {0, };
	gchar *str;
	RhythmDBEntryType *entry_type;
	RBAudioCDEntryData *extra_data;

	audio_path = g_strdup_printf ("cdda://%d#%s", track_number, priv->device_path);

	g_object_get (source, "entry-type", &entry_type, NULL);
	rb_debug ("Audio CD - create entry for track %d from %s", track_number, audio_path);
	entry = rhythmdb_entry_new (db, entry_type, audio_path);
	g_object_unref (entry_type);
	if (entry == NULL) {
		g_free (audio_path);
		return NULL;
	}

	/* generate track # */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, track_number);
	rhythmdb_entry_set (db, entry,
			    RHYTHMDB_PROP_TRACK_NUMBER,
			    &value);
	g_value_unset (&value);

	/* generate track name */
	g_value_init (&value, G_TYPE_STRING);
	str = g_strdup_printf (_("Track %u"), track_number);
	g_value_take_string (&value, str);
	rhythmdb_entry_set (db, entry,
			    RHYTHMDB_PROP_TITLE,
			    &value);
	g_value_unset (&value);

	/* determine the duration
	 * FIXME: http://bugzilla.gnome.org/show_bug.cgi?id=551011 */
	if (gst_tag_list_get_uint64 (GST_CDDA_BASE_SRC(priv->cdda)->tracks[track_number - 1].tags, GST_TAG_DURATION, &duration)) {
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, (gulong)(duration / GST_SECOND));
		rhythmdb_entry_set (db, entry,
				    RHYTHMDB_PROP_DURATION,
				    &value);
		g_value_unset (&value);
	} else {
		g_warning ("Failed to query cd track duration");
	}

	entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST, FALSE, NULL);
	entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM, FALSE, NULL);
	entry_set_string_prop (db, entry, RHYTHMDB_PROP_GENRE, FALSE, NULL);
	entry_set_string_prop (db, entry, RHYTHMDB_PROP_MIMETYPE, TRUE, "audio/x-raw-int");

	extra_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioCDEntryData);
	extra_data->extract = TRUE;

	rhythmdb_commit (db);
	g_free (audio_path);

	return entry;
}

static gboolean
rb_audiocd_get_cd_info (RBAudioCdSource *source,
			gint64 *num_tracks)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GstFormat fmt = gst_format_get_by_nick ("track");
	GstFormat out_fmt = fmt;
	if (!gst_element_query_duration (priv->cdda, &out_fmt, num_tracks) || out_fmt != fmt) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
rb_audiocd_scan_songs (RBAudioCdSource *source,
		       RhythmDB *db)
{
	gint64 i, num_tracks;
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
        GstStateChangeReturn ret;
	gboolean ok = TRUE;

	ret = gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_ASYNC) {
		ret = gst_element_get_state (priv->pipeline, NULL, NULL, 3 * GST_SECOND);
	}
        if (ret == GST_STATE_CHANGE_FAILURE) {
		gdk_threads_enter ();
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox couldn't access the CD."));
		gdk_threads_leave ();
		ok = FALSE;
	}

	if (ok && !rb_audiocd_get_cd_info (source, &num_tracks)) {
		gdk_threads_enter ();
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox couldn't read the CD information."));
		gdk_threads_leave ();
		ok = FALSE;
	}

	if (ok) {
		rb_debug ("importing Audio Cd %s - %d tracks", priv->device_path, (int)num_tracks);
		for (i = 1; i <= num_tracks; i++) {
			RhythmDBEntry* entry = rb_audiocd_create_track_entry (source, db, i);

			if (entry)
				priv->tracks = g_list_prepend (priv->tracks, entry);
			else
				g_warning ("Could not create audio cd track entry");
		}
		priv->tracks = g_list_reverse (priv->tracks);
	}

	if (gst_element_set_state (priv->pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
		rb_debug ("failed to set cd state");
	}

	return ok;
}

#ifdef HAVE_SJ_METADATA_GETTER

static void
reload_metadata_cmd (GtkAction *action, RBAudioCdSource *source)
{
	RhythmDB *db;

	g_return_if_fail (RB_IS_AUDIOCD_SOURCE (source));

	db = get_db_for_source (RB_AUDIOCD_SOURCE (source));
	rb_audiocd_load_metadata (RB_AUDIOCD_SOURCE (source), db);
	g_object_unref (db);
}

/*
 * Called by the Multiple Album dialog when the user hits return in
 * the list view
 */
static void
album_row_activated (GtkTreeView *treeview,
		     GtkTreePath *arg1,
		     GtkTreeViewColumn *arg2,
		     gpointer user_data)
{
	GtkDialog *dialog = GTK_DIALOG (user_data);
	g_assert (dialog != NULL);
	gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

/*
 * Utility function for when there are more than one albums
 * available. Borrowed from Sound Juicer.
 */
static AlbumDetails *
multiple_album_dialog (GList *albums, RBAudioCdSource *source)
{
	GtkWidget *dialog;
	GtkWidget *albums_listview;
	GtkListStore *albums_store;
	GtkTreeSelection *selection;
	AlbumDetails *album;
	GtkTreeIter iter;
	int response;
	GtkBuilder *builder;
	GtkTreeViewColumn *column;
	GtkCellRenderer *text_renderer;
	RBPlugin *plugin;
	char *builder_file;

	gdk_threads_enter ();

	g_object_get (source, "plugin", &plugin, NULL);
	g_assert (plugin != NULL);

	/* create dialog */
	builder_file = rb_plugin_find_file (plugin, "multiple-album.ui");
	g_object_unref (plugin);

	if (builder_file == NULL) {
		g_warning ("couldn't find multiple-album.ui");
		return NULL;
	}

	builder = rb_builder_load (builder_file, NULL);
	g_free (builder_file);

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "multiple_dialog"));
	g_assert (dialog != NULL);
	gtk_window_set_transient_for (GTK_WINDOW (dialog),
				      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source))));
	albums_listview = GTK_WIDGET (gtk_builder_get_object (builder, "albums_listview"));

	g_signal_connect (albums_listview, "row-activated", G_CALLBACK (album_row_activated), dialog);

	/* add columns */
	text_renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Title"),
							   text_renderer,
							   "text", 0,
							   NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (albums_listview), column);

	column = gtk_tree_view_column_new_with_attributes (_("Artist"),
							   text_renderer,
							   "text", 1,
							   NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (albums_listview), column);

	/* create model for the tree view */
	albums_store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_view_set_model (GTK_TREE_VIEW (albums_listview), GTK_TREE_MODEL (albums_store));

	for (; albums ; albums = g_list_next (albums)) {
		GtkTreeIter iter;
		AlbumDetails *album = (AlbumDetails*)(albums->data);
		gtk_list_store_append (albums_store, &iter);
		gtk_list_store_set (albums_store, &iter,
				    0, album->title,
				    1, album->artist,
				    2, album,
				    -1);
	}

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (albums_listview));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	/* select the first row */
	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (albums_store), &iter);
	gtk_tree_selection_select_iter (selection, &iter);

	gtk_widget_grab_focus (albums_listview);
	gtk_widget_show_all (dialog);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_hide (dialog);

	if (response == GTK_RESPONSE_DELETE_EVENT) {
		album = NULL;
	} else {
		gtk_tree_selection_get_selected (selection, NULL, &iter);
		gtk_tree_model_get (GTK_TREE_MODEL (albums_store), &iter, 2, &album, -1);
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));

	gdk_threads_leave ();
	g_object_unref (builder);
	return album;
}


static void
metadata_cb (SjMetadataGetter *metadata,
	     GList *albums,
	     GError *error,
	     RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GList *cd_track = priv->tracks;
	RhythmDB *db;
	GValue true_value = {0,};
	AlbumDetails *album;

	g_assert (metadata == priv->metadata);

	if (error != NULL) {
		rb_debug ("Failed to load cd metadata: %s", error->message);
		/* TODO display error to user? */
		g_object_unref (metadata);
		priv->metadata = NULL;
		return;
	}
	if (albums == NULL) {
		rb_debug ("Musicbrainz didn't return any CD metadata, but didn't give an error");
		g_object_unref (metadata);
		priv->metadata = NULL;
		return;
	}
	if (cd_track == NULL) {
		/* empty cd? */
		rb_debug ("no tracks on the CD?");
		g_object_unref (metadata);
		priv->metadata = NULL;
		return;
	}

	db = get_db_for_source (source);

	g_value_init (&true_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&true_value, TRUE);

	g_free (priv->submit_url);
	priv->submit_url = NULL;

	/* if we have multiple results, ask the user to pick one */
	if (g_list_length (albums) > 1) {
		album = multiple_album_dialog (albums, source);
		if (album == NULL)
			album = (AlbumDetails *)albums->data;
	} else
		album = (AlbumDetails *)albums->data;

	if (album->metadata_source != SOURCE_MUSICBRAINZ) {
		priv->submit_url = sj_metadata_getter_get_submit_url (metadata);
		if (priv->submit_url != NULL)
			gtk_widget_show (priv->info_bar);
	}

	if (album->metadata_source == SOURCE_FALLBACK) {
		rb_debug ("ignoring CD metadata from fallback source");
		g_object_unref (metadata);
		priv->metadata = NULL;
		g_object_unref (db);
		return;
	}

	if (album->artist != NULL) {
		gtk_entry_set_text (GTK_ENTRY (priv->artist_entry), album->artist);
	}
	if (album->artist_sortname != NULL) {
		gtk_entry_set_text (GTK_ENTRY (priv->artist_sort_entry), album->artist_sortname);
	}
	if (album->title != NULL) {
		gtk_entry_set_text (GTK_ENTRY (priv->album_entry), album->title);
	}
	if (album->release_date != NULL) {
		char *year;
		year = g_strdup_printf ("%d", g_date_get_year (album->release_date));
		gtk_entry_set_text (GTK_ENTRY (priv->year_entry), year);
		g_free (year);
	}
	if (album->disc_number != 0) {
		char *num;
		num = g_strdup_printf ("%d", album->disc_number);
		gtk_entry_set_text (GTK_ENTRY (priv->disc_number_entry), num);
		g_free (num);
	}
	if (album->genre != NULL) {
		gtk_entry_set_text (GTK_ENTRY (priv->genre_entry), album->genre);
	}

	g_object_set (G_OBJECT (source), "name", album->title, NULL);
	rb_debug ("musicbrainz_albumid: %s", album->album_id);
	rb_debug ("musicbrainz_albumartistid: %s", album->artist_id);
	rb_debug ("album artist: %s", album->artist);
	rb_debug ("album artist sortname: %s", album->artist_sortname);
	rb_debug ("disc number: %d", album->disc_number);
	rb_debug ("genre: %s", album->genre);

	while (album->tracks && cd_track) {
		TrackDetails *track = (TrackDetails*)album->tracks->data;
		RhythmDBEntry *entry = cd_track->data;
		GValue value = {0, };

		rb_debug ("storing metadata for %s - %s - %s", track->artist, album->title, track->title);

		rb_debug ("musicbrainz_trackid: %s", track->track_id);
		rb_debug ("musicbrainz_artistid: %s", track->artist_id);
		rb_debug ("artist sortname: %s", track->artist_sortname);

		/* record track info in entry*/
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_TITLE, FALSE, track->title);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST, FALSE, track->artist);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM, FALSE, album->title);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_GENRE, FALSE, album->genre);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, TRUE, track->track_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, TRUE, track->artist_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, TRUE, album->album_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, TRUE, album->artist_id);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, TRUE, track->artist_sortname);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST, TRUE, album->artist);
		entry_set_string_prop (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, TRUE, album->artist_sortname);

		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->duration);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &value);
		g_value_unset (&value);

		if (album->disc_number != 0) {
			g_value_init (&value, G_TYPE_ULONG);
			g_value_set_ulong (&value, album->disc_number);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
			g_value_unset (&value);
		}

		/*album->release_date (could potentially have multiple values)*/
		/* in current sj-structures.h, however, it does not */

		if (album->release_date) {
			GType type = rhythmdb_get_property_type (db, RHYTHMDB_PROP_DATE);
			g_value_init (&value, type);
			g_value_set_ulong (&value, g_date_get_julian (album->release_date));
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DATE, &value);
			g_value_unset (&value);
		}

		rhythmdb_commit (db);

		album->tracks = g_list_next (album->tracks);
		cd_track = g_list_next (cd_track);
	}

	while (cd_track) {
		/* Musicbrainz doesn't report data tracks on multisession CDs.
		 * These aren't interesting to us anyway, so they should be hidden.
		 */
		RhythmDBEntry *entry = cd_track->data;
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_HIDDEN, &true_value);
		rhythmdb_commit (db);

		cd_track = g_list_next (cd_track);
	}

	/* And free the albums list, as it belongs to us, not
	 * the metadata getter */
	g_list_foreach (albums, (GFunc)album_details_free, NULL);
	g_list_free (albums);

	g_object_unref (metadata);
	priv->metadata = NULL;

	g_object_unref (db);
}

static void
metadata_cancelled_cb (SjMetadataGetter *metadata,
		       GList *albums,
		       GError *error,
		       gpointer old_source)
{
	/* NOTE: the source may have been finalised, and so should NOT be used*/
	g_list_foreach (albums, (GFunc)album_details_free, NULL);
	g_list_free (albums);
	g_object_unref (metadata);
}
#endif

static void
rb_audiocd_load_metadata (RBAudioCdSource *source,
			  RhythmDB *db)
{
#ifdef HAVE_SJ_METADATA_GETTER
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	priv->metadata = sj_metadata_getter_new ();
	sj_metadata_getter_set_cdrom (priv->metadata, priv->device_path);

	g_signal_connect (G_OBJECT (priv->metadata), "metadata",
			  G_CALLBACK (metadata_cb), source);
	sj_metadata_getter_list_albums (priv->metadata, NULL);
#endif
}

static void
rb_audiocd_load_metadata_cancel (RBAudioCdSource *source)
{
#ifdef HAVE_SJ_METADATA_GETTER
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

	if (priv->metadata) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (priv->metadata),
						      G_CALLBACK (metadata_cb), source);
		g_signal_connect (G_OBJECT (priv->metadata), "metadata",
				  G_CALLBACK (metadata_cancelled_cb), source);
	}
#endif
}

static gpointer
rb_audiocd_load_songs (RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	GVolume *volume;

	g_object_get (source, "volume", &volume, NULL);
	priv->device_path = g_volume_get_identifier (volume,
						     G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	g_object_unref (volume);

	db = get_db_for_source (source);

	rb_debug ("loading Audio CD from %s", priv->device_path);
	/* create a cdda gstreamer element, to get cd info from */
	priv->cdda = gst_element_make_from_uri (GST_URI_SRC, "cdda://", NULL);
	if (!priv->cdda) {
		gdk_threads_enter ();
		rb_error_dialog (NULL, _("Couldn't load Audio CD"),
					_("Rhythmbox could not get access to the CD device."));
		gdk_threads_leave ();
		goto error_out;
	}

	rb_debug ("cdda longname: %s", gst_element_factory_get_longname (gst_element_get_factory (priv->cdda)));
	g_object_set (G_OBJECT (priv->cdda), "device", priv->device_path, NULL);
	priv->pipeline = gst_pipeline_new ("pipeline");
	priv->fakesink = gst_element_factory_make ("fakesink", "fakesink");
	gst_bin_add_many (GST_BIN (priv->pipeline), priv->cdda, priv->fakesink, NULL);
	gst_element_link (priv->cdda, priv->fakesink);

	/* disable paranoia (if using cdparanoia) since we're only reading track information here.
	 * this reduces cdparanoia's cache size, so the process is much faster.
	 */
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "paranoia-mode"))
		g_object_set (source, "paranoia-mode", 0, NULL);

	if (rb_audiocd_scan_songs (source, db))
		rb_audiocd_load_metadata (source, db);

error_out:
	g_object_unref (db);
	g_object_unref (source);

	return NULL;
}

static void
impl_delete_thyself (RBSource *source)
{
	RhythmDB *db;
	RhythmDBEntryType *entry_type;

	rb_debug ("audio cd ejected");

	/* cancel the loading of metadata */
	rb_audiocd_load_metadata_cancel (RB_AUDIOCD_SOURCE (source));

	db = get_db_for_source (RB_AUDIOCD_SOURCE (source));

	g_object_get (source, "entry-type", &entry_type, NULL);
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

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/AudioCdSourcePopup");
	return TRUE;
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("AudioCdCopyTracks"));
	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));

#ifdef HAVE_SJ_METADATA_GETTER
	actions = g_list_prepend (actions, g_strdup ("AudioCdSourceReloadMetadata"));
#endif

	return actions;
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
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	RhythmDB *db;
	GList *i;

	db = get_db_for_source (source);

	for (i = priv->tracks; i != NULL; i = i->next) {
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

#if defined(HAVE_SJ_METADATA_GETTER)
static void
info_bar_response_cb (GtkInfoBar *info_bar, gint response_id, RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GError *error = NULL;

	g_return_if_fail (priv->submit_url != NULL);

	if (response_id == GTK_RESPONSE_OK) {
		if (!gtk_show_uri (NULL, priv->submit_url, GDK_CURRENT_TIME, &error)) {
			rb_debug ("Could not launch submit URL %s: %s", priv->submit_url, error->message);
			g_error_free (error);
			return;
		}
	}

	gtk_widget_hide (priv->info_bar);
}
#endif

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
copy_tracks_cmd (GtkAction *action, RBAudioCdSource *source)
{
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
