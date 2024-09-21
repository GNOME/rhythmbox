/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
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

#include <config.h>

#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-stock-icons.h"
#include "rb-header.h"
#include "rb-debug.h"
#include "rb-shell-player.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rb-player.h"
#include "rb-text-helpers.h"
#include "rb-fading-image.h"
#include "rb-file-helpers.h"
#include "rb-ext-db.h"

#define LABEL_SELECT_PADDING	20

/**
 * SECTION:rbheader
 * @short_description: playback area widgetry
 *
 * The RBHeader widget displays information about the current playing track
 * (title, album, artist), the elapsed or remaining playback time, and a
 * position slider indicating the playback position.  It translates slider
 * move and drag events into seek requests for the player backend.
 *
 * For shoutcast-style streams, the title/artist/album display is supplemented
 * by metadata extracted from the stream.  See #RBStreamingSource for more information
 * on how the metadata is reported.
 */

static void rb_header_class_init (RBHeaderClass *klass);
static void rb_header_init (RBHeader *header);
static void rb_header_constructed (GObject *object);
static void rb_header_dispose (GObject *object);
static void rb_header_finalize (GObject *object);
static void rb_header_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_header_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static GtkSizeRequestMode rb_header_get_request_mode (GtkWidget *widget);
static void rb_header_get_preferred_width (GtkWidget *widget,
					   int *minimum_size,
					   int *natural_size);
static void rb_header_size_allocate (GtkWidget *widget, GtkAllocation *allocation);
static void rb_header_update_elapsed (RBHeader *header);
static void apply_slider_position (RBHeader *header);
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBHeader *header);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static void slider_changed_callback (GtkWidget *widget, RBHeader *header);
static gboolean slider_scroll_callback (GtkWidget *widget, GdkEventScroll *event, RBHeader *header);
static gboolean slider_focus_out_callback (GtkWidget *widget, GdkEvent *event, RBHeader *header);
static void time_button_clicked_cb (GtkWidget *button, RBHeader *header);

static void rb_header_elapsed_changed_cb (RBShellPlayer *player, gint64 elapsed, RBHeader *header);
static void rb_header_extra_metadata_cb (RhythmDB *db, RhythmDBEntry *entry, const char *property_name, const GValue *metadata, RBHeader *header);
static void rb_header_sync (RBHeader *header);
static void rb_header_sync_time (RBHeader *header);

static void art_cb (RBExtDBKey *key, RBExtDBKey *store_key, const char *filename, GValue *data, RBHeader *header);
static void uri_dropped_cb (RBFadingImage *image, const char *uri, RBHeader *header);
static void pixbuf_dropped_cb (RBFadingImage *image, GdkPixbuf *pixbuf, RBHeader *header);
static void image_button_press_cb (GtkWidget *widget, GdkEvent *event, RBHeader *header);
static gboolean label_button_press_cb (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static void art_added_cb (RBExtDB *db, RBExtDBKey *key, const char *filename, GValue *data, RBHeader *header);
static void volume_widget_changed_cb (GtkScaleButton *widget, gdouble volume, RBHeader *header);
static void player_volume_changed_cb (RBShellPlayer *player, GParamSpec *pspec, RBHeader *header);

struct RBHeaderPrivate
{
	RhythmDB *db;
	RhythmDBEntry *entry;
	RBExtDB *art_store;

	RBShellPlayer *shell_player;
	RBSource *playing_source;
	gulong status_changed_id;
	gboolean showing_playback_status;

	GtkWidget *songbox;
	GtkWidget *song;
	GtkWidget *details;
	GtkWidget *not_playing;
	GtkWidget *image;
	GtkWidget *volume_button;

	GtkWidget *scale;
	GtkAdjustment *adjustment;
	gboolean slider_dragging;
	gboolean slider_locked;
	gboolean slider_drag_moved;
	guint slider_moved_timeout;
	long latest_set_time;

	GtkWidget *timebutton;
	GtkWidget *timelabel;

	gint64 elapsed_time;		/* nanoseconds */
	gboolean show_remaining;
	long duration;
	gboolean seekable;
	char *image_path;
	RBExtDBKey *art_key;
	gboolean show_album_art;
	gboolean show_slider;
	
	gboolean syncing_volume;
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_SHELL_PLAYER,
	PROP_SEEKABLE,
	PROP_SLIDER_DRAGGING,
	PROP_SHOW_REMAINING,
	PROP_SHOW_POSITION_SLIDER,
	PROP_SHOW_ALBUM_ART
};

#define TITLE_FORMAT  "<big><b>%s</b></big>"
#define ALBUM_FORMAT  "<i>%s</i>"
#define ARTIST_FORMAT "<i>%s</i>"
#define STREAM_FORMAT "%s"

/* unicode graphic characters, encoded in UTF-8 */
static const char *const UNICODE_MIDDLE_DOT = "\xC2\xB7";

#define SCROLL_UP_SEEK_OFFSET	5
#define SCROLL_DOWN_SEEK_OFFSET -5

G_DEFINE_TYPE (RBHeader, rb_header, GTK_TYPE_GRID)

static void
rb_header_class_init (RBHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->constructed = rb_header_constructed;
	object_class->dispose = rb_header_dispose;
	object_class->finalize = rb_header_finalize;

	object_class->set_property = rb_header_set_property;
	object_class->get_property = rb_header_get_property;

	widget_class->get_request_mode = rb_header_get_request_mode;
	widget_class->get_preferred_width = rb_header_get_preferred_width;
	widget_class->size_allocate = rb_header_size_allocate;
	/* GtkGrid's get_preferred_height_for_width does all we need here */

	/**
	 * RBHeader:db:
	 *
	 * #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));

	/**
	 * RBHeader:shell-player:
	 *
	 * The #RBShellPlayer instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "shell player",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBHeader:seekable:
	 *
	 * If TRUE, the header should allow seeking by dragging the playback position slider
	 */
	g_object_class_install_property (object_class,
					 PROP_SEEKABLE,
					 g_param_spec_boolean ("seekable",
						 	       "seekable",
							       "seekable",
							       TRUE,
							       G_PARAM_READWRITE));

	/**
	 * RBHeader:slider-dragging:
	 *
	 * Whether the song position slider is currently being dragged.
	 */
	g_object_class_install_property (object_class,
					 PROP_SLIDER_DRAGGING,
					 g_param_spec_boolean ("slider-dragging",
						 	       "slider dragging",
							       "slider dragging",
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * RBHeader:show-remaining:
	 *
	 * Whether to show remaining time (as opposed to elapsed time) in the numeric
	 * time display.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_REMAINING,
					 g_param_spec_boolean ("show-remaining",
							       "show remaining",
							       "whether to show remaining or elapsed time",
							       FALSE,
							       G_PARAM_READWRITE));

	/**
	 * RBHeader:show-position-slider:
	 *
	 * Whether to show the playback position slider.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_POSITION_SLIDER,
					 g_param_spec_boolean ("show-position-slider",
							       "show position slider",
							       "whether to show the playback position slider",
							       TRUE,
							       G_PARAM_READWRITE));
	/**
	 * RBHeader:show-album-art:
	 *
	 * Whether to show the album art display widget.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_ALBUM_ART,
					 g_param_spec_boolean ("show-album-art",
							       "show album art",
							       "whether to show album art",
							       TRUE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBHeaderPrivate));
}

static void
rb_header_init (RBHeader *header)
{
	header->priv = G_TYPE_INSTANCE_GET_PRIVATE (header, RB_TYPE_HEADER, RBHeaderPrivate);
}

static void
rb_header_constructed (GObject *object)
{
	RBHeader *header = RB_HEADER (object);
	char *label;

	RB_CHAIN_GOBJECT_METHOD (rb_header_parent_class, constructed, object);

	gtk_grid_set_column_spacing (GTK_GRID (header), 6);
	gtk_grid_set_column_homogeneous (GTK_GRID (header), TRUE);
	gtk_container_set_border_width (GTK_CONTAINER (header), 3);

	/* set up position slider */
	header->priv->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 10.0, 1.0, 10.0, 0.0));
	header->priv->scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, header->priv->adjustment);
	gtk_widget_set_no_show_all (header->priv->scale, TRUE);
	gtk_range_set_fill_level (GTK_RANGE (header->priv->scale), 0.0);
	gtk_range_set_show_fill_level (GTK_RANGE (header->priv->scale), FALSE);
	gtk_range_set_restrict_to_fill_level (GTK_RANGE (header->priv->scale), FALSE);
	gtk_widget_set_hexpand (header->priv->scale, TRUE);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "button_press_event",
				 G_CALLBACK (slider_press_callback),
				 header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "button_release_event",
				 G_CALLBACK (slider_release_callback),
				 header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "motion_notify_event",
				 G_CALLBACK (slider_moved_callback),
				 header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "value_changed",
				 G_CALLBACK (slider_changed_callback),
				 header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "scroll_event",
				 G_CALLBACK (slider_scroll_callback),
				 header, 0);
	g_signal_connect_object (G_OBJECT (header->priv->scale),
				 "focus-out-event",
				 G_CALLBACK (slider_focus_out_callback),
				 header, 0);
	gtk_scale_set_draw_value (GTK_SCALE (header->priv->scale), FALSE);
	gtk_widget_set_size_request (header->priv->scale, 150, -1);

	/* set up song information labels */
	header->priv->songbox = gtk_grid_new ();
	gtk_widget_set_hexpand (header->priv->songbox, TRUE);
	gtk_widget_set_valign (header->priv->songbox, GTK_ALIGN_CENTER);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (header->priv->songbox), GTK_ORIENTATION_VERTICAL);

	header->priv->song = g_object_ref (gtk_label_new (" "));
	gtk_widget_show (header->priv->song);
	gtk_label_set_use_markup (GTK_LABEL (header->priv->song), TRUE);
	gtk_label_set_selectable (GTK_LABEL (header->priv->song), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (header->priv->song), PANGO_ELLIPSIZE_END);
	gtk_widget_set_halign (header->priv->song, GTK_ALIGN_START);
	gtk_widget_set_valign (header->priv->song, GTK_ALIGN_CENTER);
	g_signal_connect_object (header->priv->song,
				 "button-press-event",
				 G_CALLBACK (label_button_press_cb),
				 header, 0);

	header->priv->details = g_object_ref (gtk_label_new (""));
	gtk_widget_show (header->priv->details);
	gtk_label_set_use_markup (GTK_LABEL (header->priv->details), TRUE);
	gtk_label_set_selectable (GTK_LABEL (header->priv->details), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (header->priv->details), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (header->priv->details, TRUE);
	gtk_widget_set_halign (header->priv->details, GTK_ALIGN_START);
	gtk_widget_set_valign (header->priv->details, GTK_ALIGN_CENTER);
	g_signal_connect_object (header->priv->details,
				 "button-press-event",
				 G_CALLBACK (label_button_press_cb),
				 header, 0);

	label = g_markup_printf_escaped (TITLE_FORMAT, _("Not Playing"));
	header->priv->not_playing = g_object_ref (gtk_label_new (label));
	g_free (label);
	gtk_widget_show (header->priv->not_playing);
	gtk_label_set_use_markup (GTK_LABEL (header->priv->not_playing), TRUE);
	gtk_label_set_selectable (GTK_LABEL (header->priv->not_playing), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (header->priv->not_playing), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (header->priv->not_playing, TRUE);
	gtk_widget_set_halign (header->priv->not_playing, GTK_ALIGN_START);
	gtk_widget_set_valign (header->priv->not_playing, GTK_ALIGN_CENTER);
	gtk_container_add (GTK_CONTAINER (header->priv->songbox), header->priv->not_playing);
	g_signal_connect_object (header->priv->not_playing,
				 "button-press-event",
				 G_CALLBACK (label_button_press_cb),
				 header, 0);

	/* elapsed time / duration display */
	header->priv->timelabel = gtk_label_new ("");
	gtk_label_set_attributes (GTK_LABEL (header->priv->timelabel),
				  rb_text_numeric_get_pango_attr_list ());
	gtk_widget_set_halign (header->priv->timelabel, GTK_ALIGN_END);
	gtk_widget_set_no_show_all (header->priv->timelabel, TRUE);

	header->priv->timebutton = gtk_button_new ();
	gtk_widget_set_valign (header->priv->timebutton, GTK_ALIGN_CENTER);
	gtk_button_set_relief (GTK_BUTTON (header->priv->timebutton), GTK_RELIEF_NONE);
	gtk_container_add (GTK_CONTAINER (header->priv->timebutton), header->priv->timelabel);
	g_signal_connect_object (header->priv->timebutton,
				 "clicked",
				 G_CALLBACK (time_button_clicked_cb),
				 header, 0);

	/* image display */
	header->priv->art_store = rb_ext_db_new ("album-art");
	g_signal_connect_object (header->priv->art_store,
				 "added",
				 G_CALLBACK (art_added_cb),
				 header, 0);
	header->priv->image = GTK_WIDGET (g_object_new (RB_TYPE_FADING_IMAGE,
							"fallback", RB_STOCK_MISSING_ARTWORK,
							NULL));
	gtk_widget_set_no_show_all (header->priv->image, TRUE);
	g_signal_connect (header->priv->image,
			  "pixbuf-dropped",
			  G_CALLBACK (pixbuf_dropped_cb),
			  header);
	g_signal_connect (header->priv->image,
			  "uri-dropped",
			  G_CALLBACK (uri_dropped_cb),
			  header);
	g_signal_connect (header->priv->image,
			  "button-press-event",
			  G_CALLBACK (image_button_press_cb),
			  header);

	/* volume button */
	header->priv->volume_button = gtk_volume_button_new ();
	gtk_widget_set_valign (header->priv->volume_button, GTK_ALIGN_CENTER);
	g_signal_connect (header->priv->volume_button, "value-changed",
			  G_CALLBACK (volume_widget_changed_cb),
			  header);
	g_signal_connect (header->priv->shell_player, "notify::volume",
			  G_CALLBACK (player_volume_changed_cb),
			  header);

	gtk_grid_attach (GTK_GRID (header), header->priv->image, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (header), header->priv->songbox, 2, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (header), header->priv->timebutton, 3, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (header), header->priv->scale, 4, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (header), header->priv->volume_button, 5, 0, 1, 1);

	/* currently, nothing sets this.  it should be set on track changes. */
	header->priv->seekable = TRUE;

	rb_header_sync (header);
}

static void
rb_header_dispose (GObject *object)
{
	RBHeader *header = RB_HEADER (object);

	if (header->priv->db != NULL) {
		g_object_unref (header->priv->db);
		header->priv->db = NULL;
	}

	if (header->priv->shell_player != NULL) {
		g_object_unref (header->priv->shell_player);
		header->priv->shell_player = NULL;
	}

	if (header->priv->art_store != NULL) {
		rb_ext_db_cancel_requests (header->priv->art_store, (RBExtDBRequestCallback) art_cb, header);
		g_object_unref (header->priv->art_store);
		header->priv->art_store = NULL;
	}

	g_clear_object (&header->priv->song);
	g_clear_object (&header->priv->details);
	g_clear_object (&header->priv->not_playing);

	G_OBJECT_CLASS (rb_header_parent_class)->dispose (object);
}

static void
rb_header_finalize (GObject *object)
{
	RBHeader *header;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HEADER (object));

	header = RB_HEADER (object);
	g_return_if_fail (header->priv != NULL);

	g_free (header->priv->image_path);
	if (header->priv->art_key)
		rb_ext_db_key_free (header->priv->art_key);

	G_OBJECT_CLASS (rb_header_parent_class)->finalize (object);
}

static void
art_cb (RBExtDBKey *key, RBExtDBKey *store_key, const char *filename, GValue *data, RBHeader *header)
{
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (header->priv->shell_player);
	if (entry == NULL) {
		return;
	}

	if (rhythmdb_entry_matches_ext_db_key (header->priv->db, entry, store_key)) {
		GdkPixbuf *pixbuf = NULL;

		if (data != NULL && G_VALUE_HOLDS (data, GDK_TYPE_PIXBUF)) {
			pixbuf = GDK_PIXBUF (g_value_get_object (data));
		}

		rb_fading_image_set_pixbuf (RB_FADING_IMAGE (header->priv->image), pixbuf);

		g_free (header->priv->image_path);
		header->priv->image_path = g_strdup (filename);

		if (header->priv->art_key)
			rb_ext_db_key_free (header->priv->art_key);
		header->priv->art_key = rb_ext_db_key_copy (store_key);
	}

	rhythmdb_entry_unref (entry);
}

static void
art_added_cb (RBExtDB *db, RBExtDBKey *key, const char *filename, GValue *data, RBHeader *header)
{
	art_cb (key, key, filename, data, header);
}

static void
playback_status_changed_cb (RBSource *source, RBHeader *header)
{
	rb_header_sync (header);
}

static void
rb_header_playing_song_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBHeader *header)
{
	if (header->priv->entry == entry)
		return;

	if (header->priv->entry != NULL) {
		g_signal_handler_disconnect (header->priv->playing_source, header->priv->status_changed_id);
	}


	header->priv->entry = entry;
	header->priv->elapsed_time = 0;
	if (header->priv->entry) {
		RBExtDBKey *key;

		header->priv->duration = rhythmdb_entry_get_ulong (header->priv->entry,
								   RHYTHMDB_PROP_DURATION);

		key = rhythmdb_entry_create_ext_db_key (entry, RHYTHMDB_PROP_ALBUM);
		if (header->priv->art_key == NULL ||
		    rhythmdb_entry_matches_ext_db_key (header->priv->db, entry, header->priv->art_key) == FALSE ||
		    rb_ext_db_key_is_null_match (key, header->priv->art_key)) {
			rb_fading_image_start (RB_FADING_IMAGE (header->priv->image), 2000);
			rb_ext_db_request (header->priv->art_store,
					   key,
					   (RBExtDBRequestCallback) art_cb,
					   g_object_ref (header),
					   g_object_unref);
		} else {
			rb_debug ("existing art matches new entry");
		}
		rb_ext_db_key_free (key);

		header->priv->playing_source = rb_shell_player_get_playing_source (player);
		header->priv->status_changed_id =
			g_signal_connect (header->priv->playing_source,
					  "playback-status-changed",
					  G_CALLBACK (playback_status_changed_cb),
					  header);
	} else {
		rb_fading_image_start (RB_FADING_IMAGE (header->priv->image), 2000);
		header->priv->duration = 0;
	}

	rb_header_sync (header);

	g_free (header->priv->image_path);
	header->priv->image_path = NULL;
}

static GtkSizeRequestMode
rb_header_get_request_mode (GtkWidget *widget)
{
	return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
rb_header_get_preferred_width (GtkWidget *widget,
			       int *minimum_width,
			       int *natural_width)
{
	*minimum_width = 0;
	*natural_width = 0;
}

static void
rb_header_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	int spacing;
	int scale_width;
	int info_width;
	int time_width;
	int image_width;
	int volume_width;
	GtkAllocation child_alloc;
	gboolean rtl;

	GTK_WIDGET_CLASS (rb_header_parent_class)->size_allocate (widget, allocation);

	gtk_widget_set_allocation (widget, allocation);
	spacing = gtk_grid_get_column_spacing (GTK_GRID (widget));
	rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);

	/* take some leading space for the image, which we always make square */
	if (RB_HEADER (widget)->priv->show_album_art) {
		image_width = allocation->height;
		if (rtl) {
			child_alloc.x = allocation->x + allocation->width - image_width;
		} else {
			child_alloc.x = allocation->x;
			allocation->x += image_width + spacing;
		}
		allocation->width -= image_width + spacing;
		child_alloc.y = allocation->y;
		child_alloc.width = image_width;
		child_alloc.height = allocation->height;
		gtk_widget_size_allocate (RB_HEADER (widget)->priv->image, &child_alloc);
	} else {
		image_width = 0;
	}

	/* allocate space for the volume button at the end */
	gtk_widget_get_preferred_width (RB_HEADER (widget)->priv->volume_button, &volume_width, NULL);
	if (rtl) {
		child_alloc.x = allocation->x;
		allocation->x += volume_width + spacing;
	} else {
		child_alloc.x = (allocation->x + allocation->width) - volume_width;
	}
	child_alloc.y = allocation->y;
	child_alloc.width = volume_width;
	child_alloc.height = allocation->height;
	allocation->width -= volume_width + spacing;
	gtk_widget_size_allocate (RB_HEADER (widget)->priv->volume_button, &child_alloc);

	/* figure out how much space to allocate to the scale.
	 * it gets at least its minimum size, at most 1/3 of the
	 * space we have.
	 */
	if (RB_HEADER (widget)->priv->show_slider) {
		gtk_widget_get_preferred_width (RB_HEADER (widget)->priv->scale, &scale_width, NULL);
		if (scale_width < allocation->width / 3)
			scale_width = allocation->width / 3;

		if (scale_width + image_width > allocation->width)
			scale_width = allocation->width - image_width;

		if (scale_width > 0) {
			if (rtl) {
				child_alloc.x = allocation->x;
			} else {
				child_alloc.x = allocation->x + (allocation->width - scale_width) + spacing;
			}
			child_alloc.y = allocation->y;
			child_alloc.width = scale_width - spacing;
			child_alloc.height = allocation->height;
			gtk_widget_show (RB_HEADER (widget)->priv->scale);
			gtk_widget_size_allocate (RB_HEADER (widget)->priv->scale, &child_alloc);
		} else {
			gtk_widget_hide (RB_HEADER (widget)->priv->scale);
		}
	} else {
		scale_width = 0;
	}

	/* time button gets its minimum size */
	gtk_widget_get_preferred_width (RB_HEADER (widget)->priv->songbox, NULL, &info_width);
	if (gtk_widget_get_visible (RB_HEADER (widget)->priv->timelabel)) {
		gtk_widget_get_preferred_width (RB_HEADER (widget)->priv->timebutton, &time_width, NULL);
	} else {
		time_width = 0;
	}

	info_width = allocation->width - (scale_width + time_width) - (2 * spacing);

	if (rtl) {
		child_alloc.x = allocation->x + allocation->width - info_width;
	} else {
		child_alloc.x = allocation->x;
	}

	if (info_width > 0) {
		child_alloc.y = allocation->y;
		child_alloc.width = info_width;
		child_alloc.height = allocation->height;
		gtk_widget_show (RB_HEADER (widget)->priv->songbox);
		gtk_widget_size_allocate (RB_HEADER (widget)->priv->songbox, &child_alloc);
	} else {
		gtk_widget_hide (RB_HEADER (widget)->priv->songbox);
		info_width = 0;
	}

	if (time_width == 0) {
		gtk_widget_hide (RB_HEADER (widget)->priv->timebutton);
	} else if (info_width + scale_width + (2 * spacing) + time_width > allocation->width) {
		gtk_widget_hide (RB_HEADER (widget)->priv->timebutton);
	} else {
		if (rtl) {
			child_alloc.x = allocation->x + scale_width + spacing;
		} else {
			child_alloc.x = allocation->x + info_width + spacing;
		}
		child_alloc.y = allocation->y;
		child_alloc.width = time_width;
		child_alloc.height = allocation->height;
		gtk_widget_show (RB_HEADER (widget)->priv->timebutton);
		gtk_widget_size_allocate (RB_HEADER (widget)->priv->timebutton, &child_alloc);
	}
}

static void
rb_header_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBHeader *header = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		header->priv->db = g_value_get_object (value);
		g_signal_connect_object (header->priv->db,
					 "entry-extra-metadata-notify",
					 G_CALLBACK (rb_header_extra_metadata_cb),
					 header, 0);
		break;
	case PROP_SHELL_PLAYER:
		header->priv->shell_player = g_value_get_object (value);
		g_signal_connect_object (header->priv->shell_player,
					 "elapsed-nano-changed",
					 G_CALLBACK (rb_header_elapsed_changed_cb),
					 header, 0);
		g_signal_connect_object (header->priv->shell_player,
					 "playing-song-changed",
					 G_CALLBACK (rb_header_playing_song_changed_cb),
					 header, 0);
		break;
	case PROP_SEEKABLE:
		header->priv->seekable = g_value_get_boolean (value);
		break;
	case PROP_SHOW_REMAINING:
		header->priv->show_remaining = g_value_get_boolean (value);
		rb_header_update_elapsed (header);
		break;
	case PROP_SHOW_POSITION_SLIDER:
		header->priv->show_slider = g_value_get_boolean (value);
		gtk_widget_set_visible (header->priv->scale, header->priv->show_slider);
		break;
	case PROP_SHOW_ALBUM_ART:
		header->priv->show_album_art = g_value_get_boolean (value);
		gtk_widget_set_visible (header->priv->image, header->priv->show_album_art);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_header_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBHeader *header = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, header->priv->db);
		break;
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, header->priv->shell_player);
		break;
	case PROP_SEEKABLE:
		g_value_set_boolean (value, header->priv->seekable);
		break;
	case PROP_SLIDER_DRAGGING:
		g_value_set_boolean (value, header->priv->slider_dragging);
		break;
	case PROP_SHOW_REMAINING:
		g_value_set_boolean (value, header->priv->show_remaining);
		break;
	case PROP_SHOW_POSITION_SLIDER:
		g_value_set_boolean (value, header->priv->show_slider);
		break;
	case PROP_SHOW_ALBUM_ART:
		g_value_set_boolean (value, header->priv->show_album_art);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_header_new:
 * @shell_player: the #RBShellPlayer instance
 * @db: the #RhythmDB instance
 *
 * Creates a new header widget.
 *
 * Return value: the header widget
 */
RBHeader *
rb_header_new (RBShellPlayer *shell_player, RhythmDB *db)
{
	RBHeader *header;

	header = RB_HEADER (g_object_new (RB_TYPE_HEADER,
					  "shell-player", shell_player,
					  "db", db,
					  NULL));

	g_return_val_if_fail (header->priv != NULL, NULL);

	return header;
}

static void
get_extra_metadata (RhythmDB *db, RhythmDBEntry *entry, const char *field, char **value)
{
	GValue *v;

	v = rhythmdb_entry_request_extra_metadata (db, entry, field);
	if (v != NULL) {
		*value = g_value_dup_string (v);
		g_value_unset (v);
		g_free (v);
	} else {
		*value = NULL;
	}
}

static void
rb_header_sync (RBHeader *header)
{
	if (header->priv->entry != NULL) {
		const char *title;
		const char *album;
		const char *artist;
		const char *stream_name = NULL;
		char *streaming_title;
		char *streaming_artist;
		char *streaming_album;
		char *t;
		PangoDirection widget_dir;

		rb_debug ("syncing with %s",
			  rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_LOCATION));
		gboolean have_duration = (header->priv->duration > 0);

		if (gtk_widget_get_parent (header->priv->song) == NULL) {
			gtk_container_remove (GTK_CONTAINER (header->priv->songbox), header->priv->not_playing);
			gtk_container_add (GTK_CONTAINER (header->priv->songbox), header->priv->song);
			gtk_container_add (GTK_CONTAINER (header->priv->songbox), header->priv->details);
		}

		title = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_TITLE);
		album = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM);
		artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ARTIST);

		get_extra_metadata (header->priv->db,
				    header->priv->entry,
				    RHYTHMDB_PROP_STREAM_SONG_TITLE,
				    &streaming_title);
		if (streaming_title) {
			/* use entry title as stream name */
			stream_name = title;
			title = streaming_title;
		}

		get_extra_metadata (header->priv->db,
				    header->priv->entry,
				    RHYTHMDB_PROP_STREAM_SONG_ARTIST,
				    &streaming_artist);
		if (streaming_artist) {
			/* override artist from entry */
			artist = streaming_artist;
		}

		get_extra_metadata (header->priv->db,
				    header->priv->entry,
				    RHYTHMDB_PROP_STREAM_SONG_ALBUM,
				    &streaming_album);
		if (streaming_album) {
			/* override album from entry */
			album = streaming_album;
		}

		widget_dir = (gtk_widget_get_direction (GTK_WIDGET (header->priv->song)) == GTK_TEXT_DIR_LTR) ?
			     PANGO_DIRECTION_LTR : PANGO_DIRECTION_RTL;

		t = rb_text_cat (widget_dir, title, TITLE_FORMAT, NULL);
		gtk_label_set_markup (GTK_LABEL (header->priv->song), t);
		g_free (t);

		if (artist == NULL || artist[0] == '\0') {	/* this is crap; should be up to the entry type */
			if (stream_name != NULL) {
				t = rb_text_cat (widget_dir, stream_name, STREAM_FORMAT, NULL);
				gtk_label_set_markup (GTK_LABEL (header->priv->details), t);
				g_free (t);
			} else {
				gtk_label_set_markup (GTK_LABEL (header->priv->details), "");
			}
		} else {
			const char *by;
			const char *from;
			PangoDirection dir;
			PangoDirection native;

			native = PANGO_DIRECTION_LTR;
			if (gtk_widget_get_direction (GTK_WIDGET (header->priv->details)) != GTK_TEXT_DIR_LTR) {
				native = PANGO_DIRECTION_RTL;
			}

			dir = rb_text_common_direction (artist, album, NULL);
			if (!rb_text_direction_conflict (dir, native)) {
				dir = native;
				by = _("by");
				from = _("from");
			} else {
				by = UNICODE_MIDDLE_DOT;
				from = UNICODE_MIDDLE_DOT;
			}

			if (album != NULL && album[0] != '\0') {
				t = rb_text_cat (dir,
						 by, "%s",
						 artist, ARTIST_FORMAT,
						 from, "%s",
						 album, ALBUM_FORMAT,
						 NULL);
			} else {
				t = rb_text_cat (dir,
						 by, "%s",
						 artist, ARTIST_FORMAT,
						 NULL);
			}
			gtk_label_set_markup (GTK_LABEL (header->priv->details), t);
			g_free (t);
		}

		if (header->priv->playing_source) {
			char *text = NULL;
			float progress = 0.0;

			rb_source_get_playback_status (header->priv->playing_source, &text, &progress);
			if (text) {
				header->priv->showing_playback_status = TRUE;
				gtk_widget_show (header->priv->timelabel);
				gtk_widget_show (header->priv->timebutton);
				gtk_label_set_text (GTK_LABEL (header->priv->timelabel), text);
				g_free (text);
			} else {
				header->priv->showing_playback_status = FALSE;
			}
			gtk_range_set_show_fill_level (GTK_RANGE (header->priv->scale), (text != NULL));

			progress = progress * gtk_adjustment_get_upper (header->priv->adjustment);
			gtk_range_set_fill_level (GTK_RANGE (header->priv->scale), progress);
		}


		gtk_widget_set_sensitive (header->priv->scale, have_duration && header->priv->seekable);
		rb_header_sync_time (header);

		g_free (streaming_artist);
		g_free (streaming_album);
		g_free (streaming_title);
	} else {
		rb_debug ("not playing");
		if (gtk_widget_get_parent (header->priv->not_playing) == NULL) {
			gtk_container_remove (GTK_CONTAINER (header->priv->songbox), header->priv->song);
			gtk_container_remove (GTK_CONTAINER (header->priv->songbox), header->priv->details);
			gtk_container_add (GTK_CONTAINER (header->priv->songbox), header->priv->not_playing);
		}

		rb_header_sync_time (header);
		gtk_widget_set_sensitive (header->priv->scale, FALSE);
	}
}

static void
rb_header_sync_time (RBHeader *header)
{
	if (header->priv->shell_player == NULL)
		return;

	if (header->priv->slider_dragging == TRUE) {
		rb_debug ("slider is dragging, not syncing");
		return;
	}

	if (header->priv->duration > 0) {
		double progress = ((double) header->priv->elapsed_time) / RB_PLAYER_SECOND;

		header->priv->slider_locked = TRUE;

		g_object_freeze_notify (G_OBJECT (header->priv->adjustment));
		gtk_adjustment_set_value (header->priv->adjustment, progress);
		gtk_adjustment_set_upper (header->priv->adjustment, header->priv->duration);
		g_object_thaw_notify (G_OBJECT (header->priv->adjustment));

		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, header->priv->seekable);
	} else {
		header->priv->slider_locked = TRUE;

		g_object_freeze_notify (G_OBJECT (header->priv->adjustment));
		gtk_adjustment_set_value (header->priv->adjustment, 0.0);
		gtk_adjustment_set_upper (header->priv->adjustment, 1.0);
		g_object_thaw_notify (G_OBJECT (header->priv->adjustment));

		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, FALSE);
	}

	rb_header_update_elapsed (header);
}

static gboolean
slider_press_callback (GtkWidget *widget,
		       GdkEventButton *event,
		       RBHeader *header)
{
	int height;

	header->priv->slider_dragging = TRUE;
	header->priv->slider_drag_moved = FALSE;
	header->priv->latest_set_time = -1;
	g_object_notify (G_OBJECT (header), "slider-dragging");

	/* hack: pretend the trough is at least 20 pixels high */
	height = gtk_widget_get_allocated_height (widget);
	if (fabs (event->y - (height / 2)) < 10)
		event->y = height / 2;

	return FALSE;
}

static gboolean
slider_moved_timeout (RBHeader *header)
{
	apply_slider_position (header);
	header->priv->slider_moved_timeout = 0;
	header->priv->slider_drag_moved = FALSE;
	return FALSE;
}

static gboolean
slider_moved_callback (GtkWidget *widget,
		       GdkEventMotion *event,
		       RBHeader *header)
{
	double progress;

	if (header->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}
	header->priv->slider_drag_moved = TRUE;

	progress = gtk_adjustment_get_value (header->priv->adjustment);
	header->priv->elapsed_time = (gint64) ((progress+0.5) * RB_PLAYER_SECOND);

	rb_header_update_elapsed (header);

	if (header->priv->slider_moved_timeout != 0) {
		rb_debug ("removing old timer");
		g_source_remove (header->priv->slider_moved_timeout);
		header->priv->slider_moved_timeout = 0;
	}
	header->priv->slider_moved_timeout =
		g_timeout_add (40, (GSourceFunc) slider_moved_timeout, header);

	return FALSE;
}

static void
apply_slider_position (RBHeader *header)
{
	double progress;
	long new;

	progress = gtk_adjustment_get_value (header->priv->adjustment);
	new = (long) (progress+0.5);

	if (new != header->priv->latest_set_time) {
		rb_debug ("setting time to %ld", new);
		rb_shell_player_set_playing_time (header->priv->shell_player, new, NULL);
		header->priv->latest_set_time = new;
	}
}

static gboolean
slider_release_callback (GtkWidget *widget,
			 GdkEventButton *event,
			 RBHeader *header)
{
	if (header->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	if (header->priv->slider_moved_timeout != 0) {
		g_source_remove (header->priv->slider_moved_timeout);
		header->priv->slider_moved_timeout = 0;
	}

	if (header->priv->slider_drag_moved)
		apply_slider_position (header);

	header->priv->slider_dragging = FALSE;
	header->priv->slider_drag_moved = FALSE;
	g_object_notify (G_OBJECT (header), "slider-dragging");
	return FALSE;
}

static void
slider_changed_callback (GtkWidget *widget,
		         RBHeader *header)
{
	/* if the slider isn't being dragged, and nothing else is happening,
	 * this indicates the position was adjusted with a keypress (page up/page down etc.),
	 * so we should directly apply the change.
	 */
	if (header->priv->slider_dragging == FALSE &&
	    header->priv->slider_locked == FALSE) {
		apply_slider_position (header);
	} else if (header->priv->slider_dragging) {
		header->priv->slider_drag_moved = TRUE;
	}
}

static gboolean
slider_scroll_callback (GtkWidget *widget, GdkEventScroll *event, RBHeader *header)
{
	gboolean retval = TRUE;
	gdouble adj = gtk_adjustment_get_value (header->priv->adjustment);

	switch (event->direction) {
	case GDK_SCROLL_UP:
		rb_debug ("slider scrolling up");
		gtk_adjustment_set_value (header->priv->adjustment, adj + SCROLL_UP_SEEK_OFFSET);
		break;

	case GDK_SCROLL_DOWN:
		rb_debug ("slider scrolling down");
		gtk_adjustment_set_value (header->priv->adjustment, adj + SCROLL_DOWN_SEEK_OFFSET);
		break;

	default:
		retval = FALSE;
		break;
	}

	return retval;
}

static gboolean
slider_focus_out_callback (GtkWidget *widget, GdkEvent *event, RBHeader *header)
{
	if (header->priv->slider_dragging) {
		if (header->priv->slider_drag_moved)
			apply_slider_position (header);

		header->priv->slider_dragging = FALSE;
		header->priv->slider_drag_moved = FALSE;
		g_object_notify (G_OBJECT (header), "slider-dragging");
	}
	return FALSE;
}

static void
rb_header_update_elapsed (RBHeader *header)
{
	long seconds;
	char *elapsed;
	char *duration;
	char *label;

	if (header->priv->showing_playback_status)
		return;

	if (header->priv->entry == NULL) {
		gtk_label_set_text (GTK_LABEL (header->priv->timelabel), "");
		gtk_widget_hide (header->priv->timelabel);
		return;
	}
	gtk_widget_show (header->priv->timelabel);
	gtk_widget_show (header->priv->timebutton);

	seconds = header->priv->elapsed_time / RB_PLAYER_SECOND;
	if (header->priv->duration == 0) {
		label = rb_make_time_string (seconds);
		gtk_label_set_text (GTK_LABEL (header->priv->timelabel), label);
		g_free (label);
	} else if (header->priv->show_remaining) {

		duration = rb_make_time_string (header->priv->duration);

		if (seconds > header->priv->duration) {
			elapsed = rb_make_time_string (0);
		} else {
			elapsed = rb_make_time_string (header->priv->duration - seconds);
		}

		/* Translators: remaining time / total time */
		label = g_strdup_printf (_("-%s / %s"), elapsed, duration);
		gtk_label_set_text (GTK_LABEL (header->priv->timelabel), label);

		g_free (elapsed);
		g_free (duration);
		g_free (label);
	} else {
		elapsed = rb_make_time_string (seconds);
		duration = rb_make_time_string (header->priv->duration);

		/* Translators: elapsed time / total time */
		label = g_strdup_printf (_("%s / %s"), elapsed, duration);
		gtk_label_set_text (GTK_LABEL (header->priv->timelabel), label);

		g_free (elapsed);
		g_free (duration);
		g_free (label);
	}
}

static void
rb_header_elapsed_changed_cb (RBShellPlayer *player,
			      gint64 elapsed,
			      RBHeader *header)
{
	header->priv->elapsed_time = elapsed;
	rb_header_sync_time (header);
}

static void
rb_header_extra_metadata_cb (RhythmDB *db,
			     RhythmDBEntry *entry,
			     const char *property_name,
			     const GValue *metadata,
			     RBHeader *header)
{
	if (entry != header->priv->entry)
		return;

	if (g_str_equal (property_name, RHYTHMDB_PROP_STREAM_SONG_TITLE) ||
	    g_str_equal (property_name, RHYTHMDB_PROP_STREAM_SONG_ARTIST) ||
	    g_str_equal (property_name, RHYTHMDB_PROP_STREAM_SONG_ALBUM)) {
		rb_header_sync (header);
	}
}

static void
pixbuf_dropped_cb (RBFadingImage *image, GdkPixbuf *pixbuf, RBHeader *header)
{
	RBExtDBKey *key;
	const char *artist;
	GValue v = G_VALUE_INIT;

	if (header->priv->entry == NULL || pixbuf == NULL)
		return;

	/* maybe ignore tiny pixbufs? */

	key = rb_ext_db_key_create_storage ("album", rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM));
	artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (artist == NULL || artist[0] == '\0' || strcmp (artist, _("Unknown")) == 0) {
		artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ARTIST);
	}
	rb_ext_db_key_add_field (key, "artist", artist);

	g_value_init (&v, GDK_TYPE_PIXBUF);
	g_value_set_object (&v, pixbuf);
	rb_ext_db_store (header->priv->art_store, key, RB_EXT_DB_SOURCE_USER_EXPLICIT, &v);
	g_value_unset (&v);

	rb_ext_db_key_free (key);
}

static void
uri_dropped_cb (RBFadingImage *image, const char *uri, RBHeader *header)
{
	RBExtDBKey *key;
	const char *artist;

	if (header->priv->entry == NULL || uri == NULL)
		return;

	/* maybe ignore tiny pixbufs? */

	key = rb_ext_db_key_create_storage ("album", rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM));
	artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (artist == NULL || artist[0] == '\0' || strcmp (artist, _("Unknown")) == 0) {
		artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ARTIST);
	}
	rb_ext_db_key_add_field (key, "artist", artist);

	rb_ext_db_store_uri (header->priv->art_store, key, RB_EXT_DB_SOURCE_USER_EXPLICIT, uri);

	rb_ext_db_key_free (key);
}

static void
image_button_press_cb (GtkWidget *widget, GdkEvent *event, RBHeader *header)
{
	if (event->button.type != GDK_2BUTTON_PRESS ||
	    event->button.button != 1)
		return;

	if (header->priv->image_path != NULL) {
		GAppInfo *app;
		GAppLaunchContext *context;
		GList *files = NULL;

		app = g_app_info_get_default_for_type ("image/jpeg", FALSE);
		if (app == NULL) {
			return;
		}

		files = g_list_append (NULL, g_file_new_for_path (header->priv->image_path));

		context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (gtk_widget_get_display (widget)));
		g_app_info_launch (app, files, context, NULL);
		g_object_unref (context);
		g_object_unref (app);
		g_list_free_full (files, g_object_unref);
	}
}

static void
time_button_clicked_cb (GtkWidget *widget, RBHeader *header)
{
	g_object_set (header, "show-remaining", !header->priv->show_remaining, NULL);
}

static void
volume_widget_changed_cb (GtkScaleButton *vol, gdouble value, RBHeader *header)
{
	if (!header->priv->syncing_volume) {
		g_object_set (header->priv->shell_player, "volume", value, NULL);
	}
}

static void
player_volume_changed_cb (RBShellPlayer *player, GParamSpec *pspec, RBHeader *header)
{
	float volume;

	g_object_get (player, "volume", &volume, NULL);
	header->priv->syncing_volume = TRUE;
	gtk_scale_button_set_value (GTK_SCALE_BUTTON (header->priv->volume_button), volume);
	header->priv->syncing_volume = FALSE;
}

static gboolean
do_window_drag (RBHeader *header)
{
	GtkWidget *widget;
	gboolean window_dragging;

	widget = GTK_WIDGET (header);
	while (widget != NULL) {
		if (GTK_IS_TOOLBAR (widget)) {
			gtk_widget_style_get (widget, "window-dragging", &window_dragging, NULL);
			return window_dragging;
		}

		widget = gtk_widget_get_parent (widget);
	}

	return FALSE;
}

static gboolean
label_button_press_cb (GtkWidget *label, GdkEventButton *event, RBHeader *header)
{
	GtkWidget *window;
	int min, nat;

	if (do_window_drag (header) == FALSE) {
		return FALSE;
	}

	if (gdk_event_triggers_context_menu ((GdkEvent *)event)) {
		return FALSE;
	}

	if (event->type != GDK_BUTTON_PRESS) {
		return FALSE;
	}

	/* if we're over or near the text, allow the normal selection thing to happen,
	 * otherwise act like a bit of toolbar
	 */
	gtk_widget_get_preferred_width (label, &min, &nat);
	if (gtk_widget_get_direction (label) == GTK_TEXT_DIR_RTL) {
		if (event->x > (gtk_widget_get_allocated_width (label) - (nat + LABEL_SELECT_PADDING))) {
			return FALSE;
		}
	} else if (event->x < (nat + LABEL_SELECT_PADDING)) {
		return FALSE;
	}

	window = gtk_widget_get_toplevel (label);
	gtk_window_begin_move_drag (GTK_WINDOW (window),
				    event->button,
				    event->x_root,
				    event->y_root,
				    event->time);
	return TRUE;
}
