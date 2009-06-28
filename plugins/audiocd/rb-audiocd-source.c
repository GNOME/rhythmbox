/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of audiocd source object (based of the ipod source)
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
static GObject *rb_audiocd_source_constructor (GType type, guint n_construct_properties,
					        GObjectConstructParam *construct_properties);
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

static void metadata_gather_cb (RhythmDB *db, RhythmDBEntry *entry, RBStringValueMap *data, RBAudioCdSource *source);
static GValue *album_artist_metadata_request_cb (RhythmDB *db, RhythmDBEntry *entry, RBAudioCdSource *source);
static GValue *album_artist_sortname_metadata_request_cb (RhythmDB *db, RhythmDBEntry *entry, RBAudioCdSource *source);

static gboolean update_album_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_genre_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_year_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);
static gboolean update_disc_number_cb (GtkWidget *widget, GdkEventFocus *event, RBAudioCdSource *source);

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
#endif
} RBAudioCdSourcePrivate;

RB_PLUGIN_DEFINE_TYPE (RBAudioCdSource, rb_audiocd_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define AUDIOCD_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOCD_SOURCE, RBAudioCdSourcePrivate))

#ifdef HAVE_SJ_METADATA_GETTER
static AlbumDetails* multiple_album_dialog (GList *albums, RBAudioCdSource *source);
#endif

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

	object_class->constructor = rb_audiocd_source_constructor;
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

	if (priv->pipeline) {
		gst_object_unref (GST_OBJECT (priv->pipeline));
		priv->pipeline = NULL;
	}

	G_OBJECT_CLASS (rb_audiocd_source_parent_class)->dispose (object);
}

static GObject *
rb_audiocd_source_constructor (GType type,
			       guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	RBAudioCdSource *source;
	RBEntryView *entry_view;
	RhythmDB *db;
	RBPlugin *plugin;
	char *ui_file;

	source = RB_AUDIOCD_SOURCE (G_OBJECT_CLASS (rb_audiocd_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_object_set (G_OBJECT (source), "name", "Unknown Audio", NULL);

	/* we want audio cds to sort by track# by default */
	entry_view = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_set_sorting_order (entry_view, "Track", GTK_SORT_ASCENDING);

	/* hide the 'album' column */
	gtk_tree_view_column_set_visible (rb_entry_view_get_column (entry_view, RB_ENTRY_VIEW_COL_ALBUM), FALSE);

	/* enable in-place editing for titles, artists, and genres */
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_ARTIST, TRUE);
	rb_entry_view_set_column_editable (entry_view, RB_ENTRY_VIEW_COL_GENRE, TRUE);

	/* handle extra metadata requests for album artist and album artist sortname */
	db = get_db_for_source (source);
	g_signal_connect_object (G_OBJECT (db), "entry-extra-metadata-request::" RHYTHMDB_PROP_ALBUM_ARTIST,
				 G_CALLBACK (album_artist_metadata_request_cb), source, 0);
	g_signal_connect_object (G_OBJECT (db), "entry-extra-metadata-request::" RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME,
				 G_CALLBACK (album_artist_sortname_metadata_request_cb), source, 0);
	g_signal_connect_object (G_OBJECT (db), "entry-extra-metadata-gather",
				 G_CALLBACK (metadata_gather_cb), source, 0);
	g_object_unref (db);

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

		priv = AUDIOCD_SOURCE_GET_PRIVATE (source);

		builder = rb_builder_load (ui_file, NULL);
		g_free (ui_file);

		table = GTK_WIDGET (gtk_builder_get_object (builder, "album_info"));
		g_assert (table != NULL);

		priv->artist_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_entry"));
		priv->artist_sort_entry = GTK_WIDGET (gtk_builder_get_object (builder, "artist_sort_entry"));
		priv->album_entry = GTK_WIDGET (gtk_builder_get_object (builder, "album_entry"));
		priv->year_entry = GTK_WIDGET (gtk_builder_get_object (builder, "year_entry"));
		priv->genre_entry = GTK_WIDGET (gtk_builder_get_object (builder, "genre_entry"));
		priv->disc_number_entry = GTK_WIDGET (gtk_builder_get_object (builder, "disc_number_entry"));

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

	return G_OBJECT (source);
}

RBRemovableMediaSource *
rb_audiocd_source_new (RBPlugin *plugin,
		       RBShell *shell,
		       GVolume *volume)
{
	GObject *source;
	RhythmDBEntryType entry_type;
	RhythmDB *db;
	char *name;
	char *path;

	g_object_get (shell, "db", &db, NULL);

	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	name = g_strdup_printf ("audiocd: %s", path);
	entry_type = rhythmdb_entry_register_type (db, name);
	g_free (name);
	g_free (path);
	g_object_unref (db);

	entry_type->category = RHYTHMDB_ENTRY_NORMAL;
	entry_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc)rb_true_function;
	/* TODO save the metadata somewhere */
	entry_type->sync_metadata = (RhythmDBEntrySyncFunc)rb_null_function;

	source = g_object_new (RB_TYPE_AUDIOCD_SOURCE,
			       "entry-type", entry_type,
			       "volume", volume,
			       "shell", shell,
			       "sorting-key", NULL,
			       "source-group", RB_SOURCE_GROUP_DEVICES,
			       "plugin", plugin,
			       NULL);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
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
		       gboolean is_inserted,
		       RhythmDBPropType propid,
		       const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	if (!g_utf8_validate (str, -1, NULL)) {
		rb_debug ("Got invalid UTF-8 tag data");
		str = _("<Invalid unicode>");
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, str);
	if (is_inserted)
		rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	else
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
	RhythmDBEntryType entry_type;

	audio_path = g_strdup_printf ("cdda://%d#%s", track_number, priv->device_path);

	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	rb_debug ("Audio CD - create entry for track %d from %s", track_number, audio_path);
	entry = rhythmdb_entry_new (db, entry_type, audio_path);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
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

	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_ARTIST, NULL);
	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_ALBUM, NULL);
	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_GENRE, NULL);
	entry_set_string_prop (db, entry, FALSE, RHYTHMDB_PROP_MIMETYPE, "audio/x-raw-int");

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

	/* if we have multiple results, ask the user to pick one */
	if (g_list_length (albums) > 1) {
		album = multiple_album_dialog (albums, source);
		if (album == NULL)
			album = (AlbumDetails *)albums->data;
	} else
		album = (AlbumDetails *)albums->data;

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
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_TITLE, track->title);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_ARTIST, track->artist);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_ALBUM, album->title);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_GENRE, album->genre);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID, track->track_id);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID, track->artist_id);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID, album->album_id);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID, album->artist_id);
		entry_set_string_prop (db, entry, TRUE, RHYTHMDB_PROP_ARTIST_SORTNAME, track->artist_sortname);

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
	RhythmDBEntryType entry_type;

	rb_debug ("audio cd ejected");

	/* cancel the loading of metadata */
	rb_audiocd_load_metadata_cancel (RB_AUDIOCD_SOURCE (source));

	db = get_db_for_source (RB_AUDIOCD_SOURCE (source));

	g_object_get (source, "entry-type", &entry_type, NULL);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);
}

gboolean
rb_audiocd_is_mount_audiocd (GMount *mount)
{
	gboolean result = FALSE;
#if GLIB_CHECK_VERSION(2,17,7)
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
#else
	GFile *file;
	
	file = g_mount_get_root (mount);
	result = g_file_has_uri_scheme (file, "cdda");
	g_object_unref (file);
#endif /* glib 2.17.7 */
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

	actions = g_list_prepend (actions, g_strdup ("RemovableSourceCopyAllTracks"));
	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));

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
metadata_gather_cb (RhythmDB *db, RhythmDBEntry *entry, RBStringValueMap *data, RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GValue value = {0,};

	if (_rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
		return;
	}

	if (gtk_entry_get_text_length (GTK_ENTRY (priv->artist_entry)) > 0) {
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, gtk_entry_get_text (GTK_ENTRY (priv->artist_entry)));
		rb_string_value_map_set (data, RHYTHMDB_PROP_ALBUM_ARTIST, &value);
		g_value_unset (&value);
	}

	if (gtk_entry_get_text_length (GTK_ENTRY (priv->artist_sort_entry)) > 0) {
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, gtk_entry_get_text (GTK_ENTRY (priv->artist_sort_entry)));
		rb_string_value_map_set (data, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, &value);
		g_value_unset (&value);
	}
}


static GValue *
album_artist_metadata_request_cb (RhythmDB *db, RhythmDBEntry *entry, RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GValue *value = NULL;

	if (_rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
		return NULL;
	}

	if (gtk_entry_get_text_length (GTK_ENTRY (priv->artist_entry)) > 0) {
		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, gtk_entry_get_text (GTK_ENTRY (priv->artist_entry)));
	}

	return value;
}

static GValue *
album_artist_sortname_metadata_request_cb (RhythmDB *db, RhythmDBEntry *entry, RBAudioCdSource *source)
{
	RBAudioCdSourcePrivate *priv = AUDIOCD_SOURCE_GET_PRIVATE (source);
	GValue *value = NULL;

	if (_rb_source_check_entry_type (RB_SOURCE (source), entry) == FALSE) {
		return NULL;
	}

	if (gtk_entry_get_text_length (GTK_ENTRY (priv->artist_sort_entry)) > 0) {
		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, gtk_entry_get_text (GTK_ENTRY (priv->artist_sort_entry)));
	}

	return value;
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
	int year;
	GDate date;
	GValue v = {0, };

	year = strtol (gtk_entry_get_text (GTK_ENTRY (widget)), NULL, 10);
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

