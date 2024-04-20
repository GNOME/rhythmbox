/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Olivier Martin <olive.martin@gmail.com>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 * Yes, this code is ugly.
 */

#include "config.h"

#include <string.h>
#include <time.h>
#include <math.h>

#define EPSILON 0.0001

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-song-info.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-source.h"
#include "rb-shell.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
static void rb_song_info_constructed (GObject *object);

static void rb_song_info_show (GtkWidget *widget);
static void rb_song_info_dispose (GObject *object);
static void rb_song_info_finalize (GObject *object);
static void rb_song_info_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_song_info_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_song_info_response_cb (GtkDialog *dialog,
				      int response_id,
				      RBSongInfo *song_info);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);
static void rb_song_info_populate_dialog_multiple (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_filesize (RBSongInfo *song_info);
static void rb_song_info_update_play_count (RBSongInfo *song_info);
static void rb_song_info_update_last_played (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_buttons (RBSongInfo *song_info);
static void rb_song_info_update_rating (RBSongInfo *song_info);
static void rb_song_info_update_year (RBSongInfo *song_info);
static void rb_song_info_update_date_added (RBSongInfo *song_info);
static void rb_song_info_update_playback_error (RBSongInfo *song_info);

static void rb_song_info_backward_clicked_cb (GtkWidget *button,
					      RBSongInfo *song_info);
static void rb_song_info_forward_clicked_cb (GtkWidget *button,
					     RBSongInfo *song_info);
static void rb_song_info_query_model_changed_cb (GObject *source,
						 GParamSpec *pspec,
						 RBSongInfo *song_info);
static void rb_song_info_base_query_model_changed_cb (GObject *source,
						      GParamSpec *pspec,
						      RBSongInfo *song_info);
static void rb_song_info_rated_cb (RBRating *rating,
				   double score,
				   RBSongInfo *song_info);
static void rb_song_info_mnemonic_cb (GtkWidget *target);
static void rb_song_info_sync_entries (RBSongInfo *dialog);

struct RBSongInfoPrivate
{
	RhythmDB *db;
	RBSource *source;
	RBEntryView *entry_view;
	RhythmDBQueryModel *query_model;
	RhythmDBQueryModel *base_query_model;

	/* information on the displayed song */
	RhythmDBEntry *current_entry;
	GList *selected_entries;

	gboolean editable;

	/* the dialog widgets */
	GtkWidget   *backward;
	GtkWidget   *forward;
	GtkWidget   *notebook;

	GtkWidget   *title;
	GtkWidget   *artist;
	GtkWidget   *album;
	GtkWidget   *album_artist;
	GtkWidget   *composer;
	GtkWidget   *genre;
	GtkWidget   *track_cur;
	GtkWidget   *track_total;
	GtkWidget   *disc_cur;
	GtkWidget   *disc_total;
	GtkWidget   *year;
	GtkWidget   *comment;
	GtkTextBuffer *comment_buffer;
	GtkWidget   *playback_error_box;
	GtkWidget   *playback_error_label;
	GtkWidget   *bpm;

	GtkWidget   *artist_sortname;
	GtkWidget   *album_sortname;
	GtkWidget   *album_artist_sortname;
	GtkWidget   *composer_sortname;

	GtkWidget   *bitrate;
	GtkWidget   *duration;
	GtkWidget   *name;
	GtkWidget   *location;
	GtkWidget   *filesize;
	GtkWidget   *date_added;
	GtkWidget   *play_count;
	GtkWidget   *last_played;
	GtkWidget   *rating;

	RhythmDBPropertyModel* albums;
	RhythmDBPropertyModel* artists;
	RhythmDBPropertyModel* genres;
};

#define RB_SONG_INFO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SONG_INFO, RBSongInfoPrivate))

/**
 * SECTION:rbsonginfo
 * @short_description: song properties dialog
 *
 * Displays song properties and, if we know how to edit tags in the file,
 * allows the user to edit them.
 *
 * This class has two modes.  It can display and edit properties of a single
 * entry, in which case it uses a #GtkNotebook to split the properties across
 * 'basic' and 'details' pages, and it can display and edit properties of
 * multiple entries at a time, in which case a smaller set of properties is
 * displayed in a single set.
 *
 * In single-entry mode, it is possible to add extra pages to the #GtkNotebook
 * widget in the dialog.  The 'create-song-info' signal is emitted by the #RBShell
 * object, allowing signal handlers to add pages by calling #rb_song_info_append_page.
 * The lyrics plugin is currently the only place where this ability is used.
 * In this mode, the dialog features 'back' and 'forward' buttons that move to the
 * next or previous entries from the currently displayed track list.
 *
 * In multiple-entry mode, only the set of properties that can usefully be set
 * across multiple entries at once are displayed.
 *
 * When the dialog is closed, any changes made will be applied to the entry (or entries)
 * that were displayed in the dialog.  For songs in the library, this will result
 * in the song tags being updated on disk.  For other entry types, this only updates
 * the data store in the database.
 */

enum
{
	PRE_METADATA_CHANGE,
	POST_METADATA_CHANGE,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_ENTRY_VIEW,
	PROP_CURRENT_ENTRY,
	PROP_SELECTED_ENTRIES
};

static guint rb_song_info_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSongInfo, rb_song_info, GTK_TYPE_DIALOG)

static void
rb_song_info_class_init (RBSongInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;
	object_class->constructed = rb_song_info_constructed;

	widget_class->show = rb_song_info_show;

	/**
	 * RBSongInfo:source:
	 *
	 * The #RBSource that created the song properties window.  Used to update
	 * for track list changes, and to find the sets of albums, artist, and genres
	 * to use for tag edit completion.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
					                      "RBSource",
					                      "RBSource object",
					                      RB_TYPE_SOURCE,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSongInfo:entry-view:
	 *
	 * The #RBEntryView for the source that created the song properties window.  Used
	 * find the set of selected entries, and to change the selection when the 'back' and
	 * 'forward' buttons are pressed.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSongInfo:current-entry:
	 *
	 * The #RhythmDBEntry that is currently being displayed.  Will be NULL for
	 * multiple-entry song properties windows.
	 */
	g_object_class_install_property (object_class,
					 PROP_CURRENT_ENTRY,
					 g_param_spec_boxed ("current-entry",
					                     "RhythmDBEntry",
					                     "RhythmDBEntry object",
							     RHYTHMDB_TYPE_ENTRY,
					                     G_PARAM_READABLE));

	/**
	 * RBSongInfo:selected-entries:
	 *
	 * The set of #RhythmDBEntry objects currently being displayed.  Valid for both
	 * single-entry and multiple-entry song properties windows.
	 */
	g_object_class_install_property (object_class,
					 PROP_SELECTED_ENTRIES,
					 g_param_spec_boxed ("selected-entries",
							     "selected entries",
							     "List of selected entries, if this is a multiple-entry dialog",
							     G_TYPE_ARRAY,
							     G_PARAM_READABLE));

	object_class->dispose = rb_song_info_dispose;
	object_class->finalize = rb_song_info_finalize;

	/**
	 * RBSongInfo::pre-metadata-change:
	 * @song_info: the #RBSongInfo instance
	 * @entry: the #RhythmDBEntry being changed
	 *
	 * Emitted just before the changes made in the song properties window
	 * are applied to the database.  This is only emitted in the single-entry
	 * case.
	 */
	rb_song_info_signals[PRE_METADATA_CHANGE] =
		g_signal_new ("pre-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, pre_metadata_change),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);

	/**
	 * RBSongInfo::post-metadata-change:
	 * @song_info: the #RBSongInfo instance
	 * @entry: the #RhythmDBEntry that was changed
	 *
	 * Emitted just after changes have been applied to the database.
	 * Probably useless.
	 */
	rb_song_info_signals[POST_METADATA_CHANGE] =
		g_signal_new ("post-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, post_metadata_change),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);

	g_type_class_add_private (klass, sizeof (RBSongInfoPrivate));
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	/* create the dialog and some buttons backward - forward - close */
	song_info->priv = RB_SONG_INFO_GET_PRIVATE (song_info);

	g_signal_connect_object (G_OBJECT (song_info),
				 "response",
				 G_CALLBACK (rb_song_info_response_cb),
				 song_info, 0);

	gtk_container_set_border_width (GTK_CONTAINER (song_info), 5);
	gtk_window_set_resizable (GTK_WINDOW (song_info), TRUE);
	gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (song_info))), 2);
}

static void
rb_song_info_show (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (rb_song_info_parent_class)->show)
		GTK_WIDGET_CLASS (rb_song_info_parent_class)->show (widget);

	rb_song_info_update_playback_error (RB_SONG_INFO (widget));
}

static void
rb_song_info_construct_single (RBSongInfo *song_info, GtkBuilder *builder, gboolean editable)
{
	song_info->priv->backward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   _("_Back"),
							   GTK_RESPONSE_NONE);

	g_signal_connect_object (G_OBJECT (song_info->priv->backward),
				 "clicked",
				 G_CALLBACK (rb_song_info_backward_clicked_cb),
				 song_info, 0);

	song_info->priv->forward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   _("_Forward"),
							   GTK_RESPONSE_NONE);

	g_signal_connect_object (G_OBJECT (song_info->priv->forward),
				 "clicked",
				 G_CALLBACK (rb_song_info_forward_clicked_cb),
				 song_info, 0);

	gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));

	/* get the widgets from the XML */
	song_info->priv->notebook      = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_vbox"));
	song_info->priv->title         = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_title"));
	song_info->priv->track_cur     = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_track_cur"));
	song_info->priv->bitrate       = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_bitrate"));
	song_info->priv->duration      = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_duration"));
	song_info->priv->bpm           = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_bpm"));
	song_info->priv->location = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_location"));
	song_info->priv->filesize = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_filesize"));
	song_info->priv->date_added    = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_dateadded"));
	song_info->priv->play_count    = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_playcount"));
	song_info->priv->last_played   = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_lastplayed"));
	song_info->priv->name = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_name"));
	song_info->priv->comment = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_comment"));
	song_info->priv->comment_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (song_info->priv->comment));

	rb_builder_boldify_label (builder, "title_label");
	rb_builder_boldify_label (builder, "trackn_label");
	rb_builder_boldify_label (builder, "name_label");
	rb_builder_boldify_label (builder, "location_label");
	rb_builder_boldify_label (builder, "filesize_label");
	rb_builder_boldify_label (builder, "date_added_label");
	rb_builder_boldify_label (builder, "last_played_label");
	rb_builder_boldify_label (builder, "play_count_label");
	rb_builder_boldify_label (builder, "duration_label");
	rb_builder_boldify_label (builder, "bitrate_label");
	rb_builder_boldify_label (builder, "bpm_label");
	rb_builder_boldify_label (builder, "comment_label");

	/* whenever you press a mnemonic, the associated GtkEntry's text gets highlighted */
	g_signal_connect_object (G_OBJECT (song_info->priv->title),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->track_cur),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->comment),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);

	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->title), editable);
	gtk_editable_set_editable  (GTK_EDITABLE (song_info->priv->track_cur), editable);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (song_info->priv->comment), editable);

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->title);
}

static void
rb_song_info_construct_multiple (RBSongInfo *song_info, GtkBuilder *builder, gboolean editable)
{
	gtk_window_set_title (GTK_WINDOW (song_info),
			      _("Multiple Song Properties"));
	gtk_widget_grab_focus (song_info->priv->artist);

	song_info->priv->notebook = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_notebook"));
}

static void
rb_song_info_add_completion (GtkEntry *entry, RhythmDBPropertyModel *propmodel)
{
	GtkEntryCompletion* completion;

	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model (completion, GTK_TREE_MODEL (propmodel));
	gtk_entry_completion_set_text_column (completion, RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE);
	gtk_entry_set_completion (entry, completion);
	g_object_unref (completion);
}

static void
rb_song_info_constructed (GObject *object)
{
	RBSongInfo *song_info;
	GList *selected_entries;
	GList *tem;
	gboolean editable = TRUE;
	RBShell *shell;
	AtkObject *lobj, *robj;
	GtkBuilder *builder;
	GtkWidget *content_area;

	RB_CHAIN_GOBJECT_METHOD (rb_song_info_parent_class, constructed, object);

	song_info = RB_SONG_INFO (object);

	selected_entries = rb_entry_view_get_selected_entries (song_info->priv->entry_view);

	g_return_if_fail (selected_entries != NULL);

	for (tem = selected_entries; tem; tem = tem->next) {
		if (!rhythmdb_entry_can_sync_metadata (selected_entries->data)) {
			editable = FALSE;
			break;
		}
	}

	song_info->priv->editable = editable;

	if (selected_entries->next == NULL) {
		song_info->priv->current_entry = selected_entries->data;
		song_info->priv->selected_entries = NULL;

		g_list_foreach (selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (selected_entries);
	} else {
		song_info->priv->current_entry = NULL;
		song_info->priv->selected_entries = selected_entries;
	}

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (song_info));
	if (song_info->priv->current_entry) {
		builder = rb_builder_load ("song-info.ui", song_info);
		gtk_container_add (GTK_CONTAINER (content_area),
				   GTK_WIDGET (gtk_builder_get_object (builder, "song_info_vbox")));
	} else {
		builder = rb_builder_load ("song-info-multiple.ui", song_info);
		gtk_container_add (GTK_CONTAINER (content_area),
				   GTK_WIDGET (gtk_builder_get_object (builder, "song_info_notebook")));
	}

	song_info->priv->artist = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_artist"));
	song_info->priv->composer = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_composer"));
	song_info->priv->album = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_album"));
	song_info->priv->album_artist = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_album_artist"));
	song_info->priv->composer = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_composer"));
	song_info->priv->genre = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_genre"));
	song_info->priv->year = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_year"));
	song_info->priv->playback_error_box = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_error_box"));
	song_info->priv->playback_error_label = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_error_label"));
	song_info->priv->track_total   = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_track_total"));
	song_info->priv->disc_cur = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_disc_cur"));
	song_info->priv->disc_total = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_disc_total"));

	song_info->priv->artist_sortname = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_artist_sortname"));
	song_info->priv->album_sortname = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_album_sortname"));
	song_info->priv->album_artist_sortname = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_album_artist_sortname"));
	song_info->priv->composer_sortname = GTK_WIDGET (gtk_builder_get_object (builder, "song_info_composer_sortname"));

	rb_song_info_add_completion (GTK_ENTRY (song_info->priv->genre), song_info->priv->genres);
	rb_song_info_add_completion (GTK_ENTRY (song_info->priv->artist), song_info->priv->artists);
	rb_song_info_add_completion (GTK_ENTRY (song_info->priv->album), song_info->priv->albums);

	rb_builder_boldify_label (builder, "album_label");
	rb_builder_boldify_label (builder, "artist_label");
	rb_builder_boldify_label (builder, "album_artist_label");
	rb_builder_boldify_label (builder, "composer_label");
	rb_builder_boldify_label (builder, "genre_label");
	rb_builder_boldify_label (builder, "year_label");
	rb_builder_boldify_label (builder, "rating_label");
	rb_builder_boldify_label (builder, "track_total_label");
	rb_builder_boldify_label (builder, "discn_label");
	rb_builder_boldify_label (builder, "disc_total_label");
	rb_builder_boldify_label (builder, "artist_sortname_label");
	rb_builder_boldify_label (builder, "album_sortname_label");
	rb_builder_boldify_label (builder, "album_artist_sortname_label");
	rb_builder_boldify_label (builder, "composer_sortname_label");

	g_signal_connect_object (G_OBJECT (song_info->priv->artist),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->album),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->album_artist),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->composer),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->genre),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->year),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->track_total),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->disc_cur),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->disc_total),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->artist_sortname),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->album_sortname),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->album_artist_sortname),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->composer_sortname),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);

	/* this widget has to be customly created */
	song_info->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (song_info->priv->rating, "rated",
				 G_CALLBACK (rb_song_info_rated_cb),
				 G_OBJECT (song_info), 0);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (builder, "song_info_rating_container")),
			   song_info->priv->rating);
	g_object_set (gtk_builder_get_object (builder, "rating_label"), "mnemonic-widget", song_info->priv->rating, NULL);

	/* add relationship between the rating label and the rating widget */
	lobj = gtk_widget_get_accessible (GTK_WIDGET (gtk_builder_get_object (builder, "rating_label")));
	robj = gtk_widget_get_accessible (song_info->priv->rating);

	atk_object_add_relationship (lobj, ATK_RELATION_LABEL_FOR, robj);
	atk_object_add_relationship (robj, ATK_RELATION_LABELLED_BY, lobj);

	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->artist), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->album), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->album_artist), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->composer), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->genre), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->year), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->track_total), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->disc_cur), editable);
	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->disc_total), editable);

	/* Finish construction */
	if (song_info->priv->current_entry) {

		rb_song_info_construct_single (song_info, builder, editable);
		rb_song_info_populate_dialog (song_info);
	} else {
		rb_song_info_construct_multiple (song_info, builder, editable);
		rb_song_info_populate_dialog_multiple (song_info);
	}
	g_object_get (G_OBJECT (song_info->priv->source), "shell", &shell, NULL);
	g_signal_emit_by_name (G_OBJECT (shell), "create_song_info", song_info, (song_info->priv->current_entry == NULL));
	g_object_unref (G_OBJECT (shell));

	gtk_dialog_add_button (GTK_DIALOG (song_info),
			       _("_Close"),
			       GTK_RESPONSE_CLOSE);

	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);

	g_object_unref (builder);
}

static void
rb_song_info_dispose (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->albums != NULL) {
		g_object_unref (song_info->priv->albums);
		song_info->priv->albums = NULL;
	}
	if (song_info->priv->artists != NULL) {
		g_object_unref (song_info->priv->artists);
		song_info->priv->artists = NULL;
	}
	if (song_info->priv->genres != NULL) {
		g_object_unref (song_info->priv->genres);
		song_info->priv->genres = NULL;
	}

	if (song_info->priv->db != NULL) {
		g_object_unref (song_info->priv->db);
		song_info->priv->db = NULL;
	}
	if (song_info->priv->source != NULL) {
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      G_CALLBACK (rb_song_info_query_model_changed_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      G_CALLBACK (rb_song_info_base_query_model_changed_cb),
						      song_info);
		g_object_unref (song_info->priv->source);
		song_info->priv->source = NULL;
	}
	if (song_info->priv->query_model != NULL) {
		g_object_unref (song_info->priv->query_model);
		song_info->priv->query_model = NULL;
	}

	G_OBJECT_CLASS (rb_song_info_parent_class)->dispose (object);
}

static void
rb_song_info_finalize (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->selected_entries != NULL) {
		g_list_foreach (song_info->priv->selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (song_info->priv->selected_entries);
	}

	G_OBJECT_CLASS (rb_song_info_parent_class)->finalize (object);
}

static void
rb_song_info_set_source_internal (RBSongInfo *song_info,
				  RBSource   *source)
{
	if (song_info->priv->source != NULL) {
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      rb_song_info_query_model_changed_cb,
						      song_info);
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      rb_song_info_base_query_model_changed_cb,
						      song_info);
		g_object_unref (song_info->priv->source);
		g_object_unref (song_info->priv->query_model);
		g_object_unref (song_info->priv->db);
	}

	song_info->priv->source = source;

	g_object_ref (song_info->priv->source);

	g_object_get (G_OBJECT (song_info->priv->source), "query-model", &song_info->priv->query_model, NULL);

	g_signal_connect_object (G_OBJECT (song_info->priv->source),
				 "notify::query-model",
				 G_CALLBACK (rb_song_info_query_model_changed_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->source),
				 "notify::base-query-model",
				 G_CALLBACK (rb_song_info_base_query_model_changed_cb),
				 song_info, 0);

	g_object_get (G_OBJECT (song_info->priv->query_model), "db", &song_info->priv->db, NULL);

	rb_song_info_query_model_changed_cb (G_OBJECT (song_info->priv->source), NULL, song_info);
	rb_song_info_base_query_model_changed_cb (G_OBJECT (song_info->priv->source), NULL, song_info);
}

static void
rb_song_info_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id) {
	case PROP_SOURCE:
		rb_song_info_set_source_internal (song_info, g_value_get_object (value));
		break;
	case PROP_ENTRY_VIEW:
		song_info->priv->entry_view = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_song_info_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, song_info->priv->source);
		break;
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, song_info->priv->entry_view);
		break;
	case PROP_CURRENT_ENTRY:
		g_value_set_boxed (value, song_info->priv->current_entry);
		break;
	case PROP_SELECTED_ENTRIES:
		if (song_info->priv->selected_entries) {
			GArray *value_array;
			GValue entry_value = { 0, };
			GList *entry_list;

			value_array = g_array_sized_new (FALSE, TRUE, sizeof (GValue), 1);
			g_array_set_clear_func (value_array, (GDestroyNotify) g_value_unset);
			g_value_init (&entry_value, RHYTHMDB_TYPE_ENTRY);
			for (entry_list = song_info->priv->selected_entries; entry_list; entry_list = entry_list->next) {
				g_value_set_boxed (&entry_value, entry_list->data);
				g_array_append_val (value_array, entry_value);
			}
			g_value_unset (&entry_value);
			g_value_take_boxed (value, value_array);
		} else {
			g_value_set_boxed (value, NULL);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_song_info_new:
 * @source: #RBSource creating the song properties window
 * @entry_view: the #RBEntryView to get selection data from
 *
 * Creates a new #RBSongInfo for the selected entry or entries in
 * the specified entry view.
 *
 * Return value: the new song properties window
 */
GtkWidget *
rb_song_info_new (RBSource *source, RBEntryView *entry_view)
{
	RBSongInfo *song_info;

        g_return_val_if_fail (RB_IS_SOURCE (source), NULL);
	if (entry_view == NULL) {
		entry_view = rb_source_get_entry_view (source);
		if (entry_view == NULL) {
			return NULL;
		}
	}

	if (rb_entry_view_have_selection (entry_view) == FALSE)
		return NULL;

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO,
				  "source", source,
				  "entry-view", entry_view,
				  NULL);

	g_return_val_if_fail (song_info->priv != NULL, NULL);

	return GTK_WIDGET (song_info);
}

/**
 * rb_song_info_append_page:
 * @info: a #RBSongInfo
 * @title: the title of the new page
 * @page: the page #GtkWidget
 *
 * Adds a new page to the song properties window.  Should be called
 * in a handler connected to the #RBShell 'create-song-info' signal.
 *
 * Return value: the page number
 */
guint
rb_song_info_append_page (RBSongInfo *info, const char *title, GtkWidget *page)
{
	GtkWidget *label;
	guint page_num;

	label = gtk_label_new (title);
	page_num = gtk_notebook_append_page (GTK_NOTEBOOK (info->priv->notebook),
					     page,
					     label);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (info->priv->notebook), TRUE);

	return page_num;
}

typedef void (*RBSongInfoSelectionFunc)(RBSongInfo *info,
					RhythmDBEntry *entry,
					void *data);

static void
rb_song_info_selection_for_each (RBSongInfo *info, RBSongInfoSelectionFunc func,
				 void *data)
{
	if (info->priv->current_entry)
		func (info, info->priv->current_entry, data);
	else {
		GList *tem;
		for (tem = info->priv->selected_entries; tem ; tem = tem->next)
			func (info, tem->data, data);
	}
}

static void
rb_song_info_response_cb (GtkDialog *dialog,
			  int response_id,
			  RBSongInfo *song_info)
{
	if (response_id == GTK_RESPONSE_CLOSE) {
		rb_song_info_sync_entries (RB_SONG_INFO (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
	}
}

static void
rb_song_info_set_entry_rating (RBSongInfo *info,
			       RhythmDBEntry *entry,
			       void *data)
{
	GValue value = {0, };
	double trouble = *((double*) data);

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, trouble);
	rhythmdb_entry_set (info->priv->db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);
}

static void
rb_song_info_rated_cb (RBRating *rating,
		       double score,
		       RBSongInfo *song_info)
{
	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_SONG_INFO (song_info));
	g_return_if_fail (score >= 0 && score <= 5 );

	rb_song_info_selection_for_each (song_info,
					 rb_song_info_set_entry_rating,
					 &score);
	rhythmdb_commit (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "rating", score,
		      NULL);
}

static void
rb_song_info_mnemonic_cb (GtkWidget *target)
{
	g_return_if_fail (GTK_IS_EDITABLE (target) || GTK_IS_TEXT_VIEW (target));

	gtk_widget_grab_focus (target);

	if (GTK_IS_EDITABLE (target)) {
		gtk_editable_select_region (GTK_EDITABLE (target), 0, -1);
	} else { /* GtkTextViews need special treatment */
		g_signal_emit_by_name (G_OBJECT (target), "select-all");
	}
}

static void
rb_song_info_populate_num_field (GtkEntry *field, gulong num)
{
	char *tmp;
	if (num > 0)
		tmp = g_strdup_printf ("%.2ld", num);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_entry_set_text (field, tmp);
	g_free (tmp);
}

static void
rb_song_info_populate_dnum_field (GtkEntry *field, gdouble num)
{
	char *tmp;
	if (num > 0)
		tmp = g_strdup_printf ("%.2f", num);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_entry_set_text (field, tmp);
	g_free (tmp);
}

static void
rb_song_info_populate_dialog_multiple (RBSongInfo *song_info)
{
	gboolean mixed_artists = FALSE;
	gboolean mixed_albums = FALSE;
	gboolean mixed_album_artists = FALSE;
	gboolean mixed_composers = FALSE;
	gboolean mixed_genres = FALSE;
	gboolean mixed_years = FALSE;
	gboolean mixed_track_totals = FALSE;
	gboolean mixed_disc_numbers = FALSE;
	gboolean mixed_disc_totals = FALSE;
	gboolean mixed_ratings = FALSE;
	gboolean mixed_artist_sortnames = FALSE;
	gboolean mixed_album_sortnames = FALSE;
	gboolean mixed_album_artist_sortnames = FALSE;
	gboolean mixed_composer_sortnames = FALSE;
	const char *artist = NULL;
	const char *album = NULL;
	const char *album_artist = NULL;
	const char *composer = NULL;
	const char *genre = NULL;
	int year = 0;
	int track_total = 0;
	int disc_number = 0;
	int disc_total = 0;
	double rating = 0.0; /* Zero is used for both "unrated" and "mixed ratings" too */
	const char *artist_sortname = NULL;
	const char *album_sortname = NULL;
	const char *album_artist_sortname = NULL;
	const char *composer_sortname = NULL;
	GList *l;

	g_assert (song_info->priv->selected_entries);

	for (l = song_info->priv->selected_entries; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *entry;
		const char *entry_artist;
		const char *entry_album;
		const char *entry_album_artist;
		const char *entry_composer;
		const char *entry_genre;
		int entry_year;
		int entry_track_total;
		int entry_disc_number;
		int entry_disc_total;
		double entry_rating;
		const char *entry_artist_sortname;
		const char *entry_album_sortname;
		const char *entry_album_artist_sortname;
		const char *entry_composer_sortname;

		entry = (RhythmDBEntry*)l->data;
		entry_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
		entry_album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		entry_album_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
		entry_composer = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMPOSER);
		entry_genre = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
		entry_year = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR);
		entry_track_total = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_TOTAL);
		entry_disc_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
		entry_disc_total = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_TOTAL);
		entry_rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);
		entry_artist_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
		entry_album_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_SORTNAME);
		entry_album_artist_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
		entry_composer_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMPOSER_SORTNAME);

		/* grab first valid values */
		if (artist == NULL)
			artist = entry_artist;
		if (album == NULL)
			album = entry_album;
		if (album_artist == NULL)
			album_artist = entry_album_artist;
		if (composer == NULL)
			composer = entry_composer;
		if (genre == NULL)
			genre = entry_genre;
		if (year == 0)
			year = entry_year;
		if (track_total == 0)
			track_total = entry_track_total;
		if (disc_number == 0)
			disc_number = entry_disc_number;
		if (disc_total == 0)
			disc_total = entry_disc_total;
		if (fabs(rating) < EPSILON)
			rating = entry_rating;
		if (artist_sortname == NULL)
			artist_sortname = entry_artist_sortname;
		if (album_sortname == NULL)
			album_sortname = entry_album_sortname;
		if (album_artist_sortname == NULL)
			album_artist_sortname = entry_album_artist_sortname;
		if (composer_sortname == NULL)
			composer_sortname = entry_composer_sortname;

		/* locate mixed values */
		if (artist != entry_artist)
			mixed_artists = TRUE;
		if (album != entry_album)
			mixed_albums = TRUE;
		if (album_artist != entry_album_artist)
			mixed_album_artists = TRUE;
		if (composer != entry_composer)
			mixed_composers = TRUE;
		if (genre != entry_genre)
			mixed_genres = TRUE;
		if (year != entry_year)
			mixed_years = TRUE;
		if (track_total != entry_track_total)
			mixed_track_totals = TRUE;
		if (disc_number != entry_disc_number)
			mixed_disc_numbers = TRUE;
		if (disc_total != entry_disc_total)
			mixed_disc_totals = TRUE;
		if (fabs(rating - entry_rating) >= EPSILON)
			mixed_ratings = TRUE;
		if (artist_sortname != entry_artist_sortname)
			mixed_artist_sortnames = TRUE;
		if (album_sortname != entry_album_sortname)
			mixed_album_sortnames = TRUE;
		if (album_artist_sortname != entry_album_artist_sortname)
			mixed_album_artist_sortnames = TRUE;
		if (composer_sortname != entry_composer_sortname)
			mixed_composer_sortnames = TRUE;

		/* don't continue search if everything is mixed */
		if (mixed_artists && mixed_albums && mixed_album_artists &&
		    mixed_composers && mixed_genres && mixed_years &&
		    mixed_track_totals && mixed_disc_numbers &&
		    mixed_disc_totals && mixed_ratings &&
		    mixed_artist_sortnames && mixed_album_sortnames &&
		    mixed_album_artist_sortnames && mixed_composer_sortnames)
			break;
	}

	if (!mixed_artists && artist != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist), artist);
	if (!mixed_albums && album != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->album), album);
	if (!mixed_album_artists && album_artist != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_artist), album_artist);
	if (!mixed_composers && composer != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->composer), composer);
	if (!mixed_genres && genre != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->genre), genre);
	if (!mixed_years && year != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->year), year);
	if (!mixed_track_totals && track_total != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_total), track_total);
	if (!mixed_disc_numbers && disc_number != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_cur), disc_number);
	if (!mixed_disc_totals && disc_total != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_total), disc_total);
	if (!mixed_ratings && fabs(rating) >= EPSILON)
		g_object_set (G_OBJECT (song_info->priv->rating), "rating", rating, NULL);
	if (!mixed_artist_sortnames && artist_sortname != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist_sortname), artist_sortname);
	if (!mixed_album_sortnames && album_sortname != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_sortname), album_sortname);
	if (!mixed_album_artist_sortnames && album_artist_sortname != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_artist_sortname), album_artist_sortname);
	if (!mixed_composer_sortnames && composer_sortname != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->composer_sortname), composer_sortname);
}

static void
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	const char *text;
	char *tmp;
	gulong num;
	gdouble dnum;

	g_assert (song_info->priv->current_entry);

	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);

	tmp = g_strdup_printf (_("%s Properties"), text);
	gtk_window_set_title (GTK_WINDOW (song_info), tmp);
	g_free (tmp);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ARTIST);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_artist), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMPOSER);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->composer), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_GENRE);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->genre), text);

	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_TRACK_NUMBER);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_cur), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_TRACK_TOTAL);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_total), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DISC_NUMBER);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_cur), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DISC_TOTAL);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_total), num);
	dnum = rhythmdb_entry_get_double (song_info->priv->current_entry, RHYTHMDB_PROP_BPM);
	rb_song_info_populate_dnum_field (GTK_ENTRY (song_info->priv->bpm), dnum);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMMENT);
	gtk_text_buffer_set_text (song_info->priv->comment_buffer, text, -1);

	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
	rb_song_info_update_filesize (song_info);
	rb_song_info_update_date_added (song_info);
	rb_song_info_update_play_count (song_info);
	rb_song_info_update_last_played (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_rating (song_info);
	rb_song_info_update_year (song_info);
	rb_song_info_update_playback_error (song_info);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_SORTNAME);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album_artist_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMPOSER_SORTNAME);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->composer_sortname), text);
}

static void
rb_song_info_update_playback_error (RBSongInfo *song_info)
{
	char *message = NULL;

	if (!song_info->priv->current_entry)
		return;

	message = rhythmdb_entry_dup_string (song_info->priv->current_entry, RHYTHMDB_PROP_PLAYBACK_ERROR);

	if (message) {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    message);
		gtk_widget_show (song_info->priv->playback_error_box);
	} else {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    "No errors");
		gtk_widget_hide (song_info->priv->playback_error_box);
	}

	g_free (message);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	char *tmp;
	gulong bitrate;

	bitrate = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_BITRATE);

	if (rhythmdb_entry_is_lossless (song_info->priv->current_entry)) {
		tmp = g_strdup (_("Lossless"));
	} else if (bitrate == 0) {
		tmp = g_strdup (_("Unknown"));
	} else {
		tmp = g_strdup_printf (_("%lu kbps"), bitrate);
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate),
			    tmp);
	g_free (tmp);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	char *text;
	long duration;

	duration = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DURATION);

	text = rb_make_duration_string (duration);
	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_free (text);
}

static void
rb_song_info_update_filesize (RBSongInfo *song_info)
{
	char *text = NULL;
	guint64 filesize = 0;
	filesize = rhythmdb_entry_get_uint64 (song_info->priv->current_entry, RHYTHMDB_PROP_FILE_SIZE);
	text = g_format_size (filesize);
	gtk_label_set_text (GTK_LABEL (song_info->priv->filesize), text);
	g_free (text);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	const char *text;

	g_return_if_fail (song_info != NULL);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_LOCATION);

	if (text != NULL) {
		char *tmp;
		char *tmp_utf8;
		char *basename;

		basename = g_path_get_basename (text);
		tmp = g_uri_unescape_string (basename, NULL);
		g_free (basename);
		tmp_utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, NULL);
		g_free (tmp);
		tmp = NULL;

		if (tmp_utf8 != NULL) {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->name),
					    tmp_utf8);
		} else {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->name),
					    _("Unknown file name"));
		}

		g_free (tmp_utf8);
		tmp_utf8 = NULL;

		if (rb_uri_is_local (text)) {
			const char *desktopdir;
			char *dir;

			/* for local files, convert to path, extract dirname, and convert to utf8 */
			tmp = g_filename_from_uri (text, NULL, NULL);

			dir = g_path_get_dirname (tmp);
			g_free (tmp);
			tmp_utf8 = g_filename_to_utf8 (dir, -1, NULL, NULL, NULL);
			g_free (dir);

			/* special case for files on the desktop */
			desktopdir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
			if (g_strcmp0 (tmp_utf8, desktopdir) == 0) {
				g_free (tmp_utf8);
				tmp_utf8 = g_strdup (_("On the desktop"));
			}
		} else {
			GFile *file;
			GFile *parent;
			char *parent_uri;

			/* get parent URI and unescape it */
			file = g_file_new_for_uri (text);
			parent = g_file_get_parent (file);
			parent_uri = g_file_get_uri (parent);
			g_object_unref (file);
			g_object_unref (parent);

			tmp_utf8 = g_uri_unescape_string (parent_uri, NULL);
			g_free (parent_uri);
		}

		if (tmp_utf8 != NULL) {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->location),
					    tmp_utf8);
		} else {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->location),
					    _("Unknown location"));
		}
		g_free (tmp_utf8);
	}
}

static void
rb_song_info_backward_clicked_cb (GtkWidget *button,
				  RBSongInfo *song_info)
{
	RhythmDBEntry *new_entry;

	rb_song_info_sync_entries (RB_SONG_INFO (song_info));
	new_entry = rhythmdb_query_model_get_previous_from_entry (song_info->priv->query_model,
								  song_info->priv->current_entry);
	g_return_if_fail (new_entry != NULL);

	song_info->priv->current_entry = new_entry;
	rb_entry_view_select_entry (song_info->priv->entry_view, new_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view, new_entry);

	rb_song_info_populate_dialog (song_info);
	g_object_notify (G_OBJECT (song_info), "current-entry");
	rhythmdb_entry_unref (new_entry);
}

static void
rb_song_info_forward_clicked_cb (GtkWidget *button,
				 RBSongInfo *song_info)
{
	RhythmDBEntry *new_entry;

	rb_song_info_sync_entries (RB_SONG_INFO (song_info));
	new_entry = rhythmdb_query_model_get_next_from_entry (song_info->priv->query_model,
							      song_info->priv->current_entry);
	g_return_if_fail (new_entry != NULL);

	song_info->priv->current_entry = new_entry;
	rb_entry_view_select_entry (song_info->priv->entry_view, new_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view, new_entry);

	rb_song_info_populate_dialog (song_info);
	g_object_notify (G_OBJECT (song_info), "current-entry");

	rhythmdb_entry_unref (new_entry);
}

/*
 * rb_song_info_update_buttons: update back/forward sensitivity
 */
static void
rb_song_info_update_buttons (RBSongInfo *song_info)
{
	RhythmDBEntry *entry = NULL;

	g_return_if_fail (song_info != NULL);
	g_return_if_fail (song_info->priv->query_model != NULL);

	if (!song_info->priv->current_entry)
		return;

	/* backward */
	entry = rhythmdb_query_model_get_previous_from_entry (song_info->priv->query_model,
							      song_info->priv->current_entry);

	gtk_widget_set_sensitive (song_info->priv->backward, entry != NULL);
	if (entry != NULL)
		rhythmdb_entry_unref (entry);

	/* forward */
	entry = rhythmdb_query_model_get_next_from_entry (song_info->priv->query_model,
							  song_info->priv->current_entry);

	gtk_widget_set_sensitive (song_info->priv->forward, entry != NULL);
	if (entry != NULL)
		rhythmdb_entry_unref (entry);
}

static void
rb_song_info_query_model_inserted_cb (RhythmDBQueryModel *model,
				      GtkTreePath *path,
				      GtkTreeIter *iter,
				      RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_query_model_deleted_cb (RhythmDBQueryModel *model,
				     RhythmDBEntry*entry,
				     RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_query_model_reordered_cb (RhythmDBQueryModel *model,
				       GtkTreePath *path,
				       GtkTreeIter *iter,
				       gpointer *map,
				       RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_base_query_model_changed_cb (GObject *source,
					  GParamSpec *whatever,
					  RBSongInfo *song_info)
{
	RhythmDBQueryModel *base_query_model;

	g_object_get (source, "base-query-model", &base_query_model, NULL);

	if (song_info->priv->albums) {
		g_object_unref (song_info->priv->albums);
	}
	if (song_info->priv->artists) {
		g_object_unref (song_info->priv->artists);
	}
	if (song_info->priv->genres) {
		g_object_unref (song_info->priv->genres);
	}

	song_info->priv->albums  = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_ALBUM);
	song_info->priv->artists = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_ARTIST);
	song_info->priv->genres  = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_GENRE);

	g_object_set (song_info->priv->albums,  "query-model", base_query_model, NULL);
	g_object_set (song_info->priv->artists, "query-model", base_query_model, NULL);
	g_object_set (song_info->priv->genres,  "query-model", base_query_model, NULL);

	if (song_info->priv->album) {
		GtkEntryCompletion *comp = gtk_entry_get_completion (GTK_ENTRY (song_info->priv->album));
		gtk_entry_completion_set_model (comp, GTK_TREE_MODEL (song_info->priv->albums));
	}

	if (song_info->priv->artist) {
		GtkEntryCompletion *comp = gtk_entry_get_completion (GTK_ENTRY (song_info->priv->artist));
		gtk_entry_completion_set_model (comp, GTK_TREE_MODEL (song_info->priv->artist));
	}

	if (song_info->priv->genre) {
		GtkEntryCompletion *comp = gtk_entry_get_completion (GTK_ENTRY (song_info->priv->genre));
		gtk_entry_completion_set_model (comp, GTK_TREE_MODEL (song_info->priv->genre));
	}

	g_object_unref (base_query_model);
}

static void
rb_song_info_query_model_changed_cb (GObject *source,
				     GParamSpec *whatever,
				     RBSongInfo *song_info)
{
	if (song_info->priv->query_model) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_inserted_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_deleted_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_reordered_cb),
						      song_info);

		g_object_unref (G_OBJECT (song_info->priv->query_model));
	}

	g_object_get (source, "query-model", &song_info->priv->query_model, NULL);

	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "row-inserted", G_CALLBACK (rb_song_info_query_model_inserted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "row-changed", G_CALLBACK (rb_song_info_query_model_inserted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "post-entry-delete", G_CALLBACK (rb_song_info_query_model_deleted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "rows-reordered", G_CALLBACK (rb_song_info_query_model_reordered_cb),
				 song_info, 0);

	/* update next button sensitivity */
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_update_play_count (RBSongInfo *song_info)
{
	gulong num;
	char *text;

	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);
	text = g_strdup_printf ("%ld", num);
	gtk_label_set_text (GTK_LABEL (song_info->priv->play_count), text);
	g_free (text);
}

static void
rb_song_info_update_last_played (RBSongInfo *song_info)
{
	const char *str;
	str = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	if (!strcmp ("", str))
		str = _("Never");
	gtk_label_set_text (GTK_LABEL (song_info->priv->last_played), str);
}

static void
rb_song_info_update_rating (RBSongInfo *song_info)
{
	double rating;

	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	rating = rhythmdb_entry_get_double (song_info->priv->current_entry, RHYTHMDB_PROP_RATING);
	g_object_set (song_info->priv->rating,
		      "rating", rating,
		      NULL);
}

static void
rb_song_info_update_year (RBSongInfo *song_info)
{
	gulong year;
	char *text;

	year = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_YEAR);
	if (year > 0) {
		text = g_strdup_printf ("%lu", year);
	} else {
		text = g_strdup (_("Unknown"));
	}
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->year), text);
	g_free (text);
}

static void
rb_song_info_update_date_added (RBSongInfo *song_info)
{
	const char *str;
	str = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_FIRST_SEEN_STR);
	gtk_label_set_text (GTK_LABEL (song_info->priv->date_added), str);
}

static gboolean
sync_string_property_multiple (RBSongInfo *dialog, RhythmDBPropType property, GtkWidget *entry)
{
	const char *new_text;
	GValue val = {0,};
	GList *t;
	gboolean changed = FALSE;

	new_text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (strlen (new_text) == 0)
		return FALSE;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, new_text);
	for (t = dialog->priv->selected_entries; t != NULL; t = t->next) {
		const char *entry_value;
		RhythmDBEntry *dbentry;

		dbentry = (RhythmDBEntry *)t->data;
		entry_value = rhythmdb_entry_get_string (dbentry, property);

		if (g_strcmp0 (new_text, entry_value) == 0)
			continue;
		rhythmdb_entry_set (dialog->priv->db, dbentry, property, &val);
		changed = TRUE;
	}
	g_value_unset (&val);
	return changed;
}

static gboolean
sync_ulong_property_multiple (RBSongInfo *dialog, RhythmDBPropType property, GtkWidget *entry)
{
	const char *new_text;
	gint val_int;
	GValue val = {0,};
	GList *tem;
	gboolean changed = FALSE;
	char *endptr;

	new_text = gtk_entry_get_text (GTK_ENTRY (entry));
	val_int = g_ascii_strtoull (new_text, &endptr, 10);

	if (endptr != new_text) {

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, val_int);

		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			RhythmDBEntry *dbentry;
			gulong entry_num;

			dbentry = (RhythmDBEntry *)tem->data;
			entry_num = rhythmdb_entry_get_ulong (dbentry, property);

			if (val_int != entry_num) {
				rhythmdb_entry_set (dialog->priv->db, dbentry,
						    property, &val);
				changed = TRUE;
			}
		}
		g_value_unset (&val);
	}
	return changed;
}

static void
rb_song_info_sync_entries_multiple (RBSongInfo *dialog)
{
	const char *year_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->year));
	char *endptr;
	GValue val = {0,};
	GList *tem;
	gint year;
	gboolean changed = FALSE;
	RhythmDBEntry *entry;

	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM, dialog->priv->album);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ARTIST, dialog->priv->artist);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_ARTIST, dialog->priv->album_artist);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_COMPOSER, dialog->priv->composer);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_GENRE, dialog->priv->genre);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ARTIST_SORTNAME, dialog->priv->artist_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_SORTNAME, dialog->priv->album_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, dialog->priv->album_artist_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_COMPOSER_SORTNAME, dialog->priv->composer_sortname);

	if (strlen (year_str) > 0) {
		GDate *date = NULL;
		GType type;

		/* note: this will reset the day-of-year to Jan 1 for all entries */
		year = g_ascii_strtoull (year_str, &endptr, 10);
		if (year > 0)
			date = g_date_new_dmy (1, G_DATE_JANUARY, year);

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);

		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));

		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_DATE, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
		if (date)
			g_date_free (date);

	}

	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_TRACK_TOTAL, dialog->priv->track_total);
	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_DISC_NUMBER, dialog->priv->disc_cur);
	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_DISC_TOTAL, dialog->priv->disc_total);

	if (changed)
		rhythmdb_commit (dialog->priv->db);
}

static gboolean
sync_property_ulong_single (RBSongInfo *dialog,
			    RhythmDBEntry *entry,
			    guint property,
			    GtkWidget *w)
{
	char *endptr;

	const char *new_text = gtk_entry_get_text (GTK_ENTRY (w));
	gulong prop_val = g_ascii_strtoull (new_text, &endptr, 10);
	gulong entry_val = rhythmdb_entry_get_ulong (entry, property);

	if ((endptr != new_text) && (prop_val != entry_val)) {
		GValue val = {0,};

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, prop_val);
		rhythmdb_entry_set (dialog->priv->db, entry, property, &val);

		return TRUE;
	}
	return FALSE;
}

static gboolean
sync_property_string_single (RBSongInfo *dialog,
			     RhythmDBEntry *dbentry,
			     guint property,
			     const gchar *prop_val)
{
	const char *entry_string = rhythmdb_entry_get_string (dbentry,
							      property);
	if (g_strcmp0 (prop_val, entry_string)) {
		GValue val = {0,};

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, prop_val);
		rhythmdb_entry_set (dialog->priv->db, dbentry,
				    property, &val);
		return TRUE;
	}
	return FALSE;
}

static void
rb_song_info_sync_entry_single (RBSongInfo *dialog)
{
	const char *title;
	const char *genre;
	const char *artist;
	const char *album;
	const char *album_artist;
	const char *composer;
	const char *year_str;
	const char *artist_sortname;
	const char *album_sortname;
	const char *album_artist_sortname;
	const char *composer_sortname;
	const char *bpm_str;
	char *comment = NULL;
	char *endptr;
	GType type;
	gulong year;
	gulong entry_val;
	gdouble bpm;
	gdouble dentry_val;
	GValue val = {0,};
	gboolean changed = FALSE;
	RhythmDBEntry *entry = dialog->priv->current_entry;
	GtkTextIter start, end;

	title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist));
	album = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album));
	album_artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album_artist));
	composer = gtk_entry_get_text (GTK_ENTRY (dialog->priv->composer));
	year_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->year));
	artist_sortname = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist_sortname));
	album_sortname = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album_sortname));
	album_artist_sortname = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album_artist_sortname));
	composer_sortname = gtk_entry_get_text (GTK_ENTRY (dialog->priv->composer_sortname));

	/* Get comment text (string is allocated) */
	gtk_text_buffer_get_bounds (dialog->priv->comment_buffer, &start, &end);
	comment = gtk_text_buffer_get_text (dialog->priv->comment_buffer, &start, &end, FALSE);

	g_signal_emit (dialog, rb_song_info_signals[PRE_METADATA_CHANGE], 0,
		       entry);

	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_TRACK_NUMBER,
					       dialog->priv->track_cur);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_TRACK_TOTAL,
					       dialog->priv->track_total);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_DISC_NUMBER,
					       dialog->priv->disc_cur);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_DISC_TOTAL,
					       dialog->priv->disc_total);

	year = g_ascii_strtoull (year_str, &endptr, 10);
	entry_val = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR);
	if ((endptr != year_str) &&
	    (year != entry_val ||
	     (entry_val == 0 && year > 0))) {
		GDate *date = NULL;

		if (year > 0) {
			if (entry_val > 0) {
				gulong julian;

				julian = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);
				date = g_date_new_julian (julian);
				g_date_set_year (date, year);
			} else {
				date = g_date_new_dmy (1, G_DATE_JANUARY, year);
			}
		}

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);
		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_DATE, &val);
		changed = TRUE;

		g_value_unset (&val);
		if (date)
			g_date_free (date);
	}
	bpm_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->bpm));
	bpm = g_strtod (bpm_str, &endptr);
	dentry_val = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_BPM);
	if ((endptr != bpm_str) && (bpm != dentry_val)) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_BPM);
		g_value_init (&val, type);
		g_value_set_double (&val, bpm);
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_BPM, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_TITLE, title);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM, album);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ARTIST, artist);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_ARTIST, album_artist);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMPOSER, composer);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_GENRE, genre);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, artist_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_SORTNAME, album_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMMENT, comment);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, album_artist_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMPOSER_SORTNAME, composer_sortname);

	/* FIXME: when an entry is SYNCed, a changed signal is emitted, and
	 * this signal is also emitted, aren't they redundant?
	 */
	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[POST_METADATA_CHANGE], 0,
		       entry);

	if (changed)
		rhythmdb_commit (dialog->priv->db);

	g_free (comment);
}

static void
rb_song_info_sync_entries (RBSongInfo *dialog)
{
	if (!dialog->priv->editable)
		return;

	if (dialog->priv->current_entry)
		rb_song_info_sync_entry_single (dialog);
	else
		rb_song_info_sync_entries_multiple (dialog);
}
