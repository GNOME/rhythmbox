/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of main song information display widget
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
#include "rb-preferences.h"
#include "rb-shell-player.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"
#include "rhythmdb.h"
#include "rb-player.h"

/**
 * SECTION:rb-header
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
static void rb_header_finalize (GObject *object);
static void rb_header_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_header_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static void rb_header_set_show_timeline (RBHeader *header,
			                 gboolean show);
static void rb_header_update_elapsed (RBHeader *header);
static void apply_slider_position (RBHeader *header);
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBHeader *header);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static void slider_changed_callback (GtkWidget *widget, RBHeader *header);
static gboolean slider_scroll_callback (GtkWidget *widget, GdkEventScroll *event, RBHeader *header);

static void rb_header_elapsed_changed_cb (RBShellPlayer *player, gint64 elapsed, RBHeader *header);

struct RBHeaderPrivate
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	RBShellPlayer *shell_player;

	GtkWidget *image;
	GtkWidget *song;

	GtkWidget *timeline;
	GtkWidget *scaleline;
	gboolean scaleline_shown;

	GtkWidget *scale;
	GtkAdjustment *adjustment;
	gboolean slider_dragging;
	gboolean slider_locked;
	guint slider_moved_timeout;
	long latest_set_time;
	GtkWidget *elapsed;

	gint64 elapsed_time;		/* nanoseconds */
	long duration;
	gboolean seekable;
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_SHELL_PLAYER,
	PROP_ENTRY,
	PROP_SEEKABLE,
	PROP_SLIDER_DRAGGING
};

#define TITLE_MARKUP(xTITLE) g_markup_printf_escaped ("<big><b>%s</b></big>", xTITLE)
#define ALBUM_MARKUP(xALBUM) g_markup_printf_escaped (" %s <i>%s</i>", _("from"), xALBUM)
#define ARTIST_MARKUP(xARTIST) g_markup_printf_escaped (" %s <i>%s</i>", _("by"), xARTIST)
#define STREAM_MARKUP(xSTREAM) g_markup_printf_escaped (" (%s)", xSTREAM)

#define SCROLL_UP_SEEK_OFFSET	5
#define SCROLL_DOWN_SEEK_OFFSET -5

G_DEFINE_TYPE (RBHeader, rb_header, GTK_TYPE_HBOX)

static void
rb_header_class_init (RBHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_header_finalize;

	object_class->set_property = rb_header_set_property;
	object_class->get_property = rb_header_get_property;

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
	 * RBHeader:entry:
	 *
	 * The #RhythmDBEntry to display
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY,
					 g_param_spec_boxed ("entry",
							     "RhythmDBEntry",
							     "RhythmDBEntry pointer",
							     RHYTHMDB_TYPE_ENTRY,
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

	g_type_class_add_private (klass, sizeof (RBHeaderPrivate));
}

static void
rb_header_init (RBHeader *header)
{
	/*
	 * The children in this widget look like this:
	 * RBHeader
	 *   GtkHBox
	 *     GtkLabel			(priv->song)
	 *   GtkHBox			(priv->timeline)
	 *     GtkHScale		(priv->scale)
	 *     GtkAlignment
	 *       GtkLabel		(priv->elapsed)
	 */
	GtkWidget *hbox;
	GtkWidget *vbox;

	header->priv = G_TYPE_INSTANCE_GET_PRIVATE (header, RB_TYPE_HEADER, RBHeaderPrivate);

	gtk_box_set_spacing (GTK_BOX (header), 3);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_widget_show (vbox);
	gtk_box_pack_start (GTK_BOX (header), vbox, TRUE, TRUE, 0);

	/* song info */
	hbox = gtk_hbox_new (FALSE, 16);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show (hbox);

	header->priv->song = gtk_label_new ("");
 	gtk_label_set_use_markup (GTK_LABEL (header->priv->song), TRUE);
 	gtk_label_set_selectable (GTK_LABEL (header->priv->song), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (header->priv->song), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (header->priv->song), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), header->priv->song, TRUE, TRUE, 0);
	gtk_widget_show (header->priv->song);

	/* construct the time display */
	header->priv->timeline = gtk_hbox_new (FALSE, 3);
	header->priv->elapsed = gtk_label_new ("");

	gtk_misc_set_padding (GTK_MISC (header->priv->elapsed), 2, 0);
	gtk_box_pack_start (GTK_BOX (header->priv->timeline), header->priv->elapsed, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (hbox), header->priv->timeline, FALSE, FALSE, 0);
	gtk_widget_show_all (header->priv->timeline);

	/* row for the position slider */
	header->priv->scaleline = gtk_hbox_new (FALSE, 3);
	gtk_box_pack_start (GTK_BOX (vbox), header->priv->scaleline, FALSE, FALSE, 0);
	header->priv->scaleline_shown = FALSE;

	header->priv->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 10.0, 1.0, 10.0, 0.0));
	header->priv->scale = gtk_hscale_new (header->priv->adjustment);
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
	gtk_scale_set_draw_value (GTK_SCALE (header->priv->scale), FALSE);
	gtk_widget_set_size_request (header->priv->scale, 150, -1);
	gtk_box_pack_start (GTK_BOX (header->priv->scaleline), header->priv->scale, TRUE, TRUE, 0);
}

static void
rb_header_finalize (GObject *object)
{
	RBHeader *header;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HEADER (object));

	header = RB_HEADER (object);
	g_return_if_fail (header->priv != NULL);

	G_OBJECT_CLASS (rb_header_parent_class)->finalize (object);
}

static void
rb_header_set_playing_entry_internal (RBHeader *header, RhythmDBEntry *entry)
{
	if (header->priv->entry == entry)
		return;

	header->priv->entry = entry;
	if (header->priv->entry) {
		header->priv->duration = rhythmdb_entry_get_ulong (header->priv->entry,
								   RHYTHMDB_PROP_DURATION);
	} else {
		header->priv->duration = 0;
	}

	header->priv->adjustment->upper = header->priv->duration;
	gtk_adjustment_changed (header->priv->adjustment);
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
		break;
	case PROP_ENTRY:
		rb_header_set_playing_entry_internal (header, g_value_get_boxed (value));
		break;
	case PROP_SHELL_PLAYER:
		header->priv->shell_player = g_value_get_object (value);
		g_signal_connect (G_OBJECT (header->priv->shell_player),
				  "elapsed-nano-changed",
				  (GCallback) rb_header_elapsed_changed_cb,
				  header);
		break;
	case PROP_SEEKABLE:
		header->priv->seekable = g_value_get_boolean (value);
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
	case PROP_ENTRY:
		g_value_set_boxed (value, header->priv->entry);
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
 * Return value: the header widget
 */
RBHeader *
rb_header_new (RBShellPlayer *shell_player, RhythmDB *db)
{
	RBHeader *header;

	header = RB_HEADER (g_object_new (RB_TYPE_HEADER,
					  "shell-player", shell_player,
					  "db", db,
					  "spacing", 6, NULL));

	g_return_val_if_fail (header->priv != NULL, NULL);

	return header;
}

/**
 * rb_header_set_playing_entry:
 * @header: the #RBHeader
 * @entry: the new playing #RhythmDBEntry
 * @seekable: whether the new playing entry is seekable
 *
 * Sets a new playing entry to be displayed in the header.
 */
void
rb_header_set_playing_entry (RBHeader *header,
			     RhythmDBEntry *entry,
			     gboolean seekable)
{
	g_object_set (header,
		      "entry", entry,
		      "seekable", seekable,
		      NULL);
}

static void
append_and_free (GString *str,
		 char *text)
{
	g_string_append (str, text);
	g_free (text);
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

/**
 * rb_header_sync:
 * @header: the #RBHeader
 *
 * Updates the header widget to be consistent with the current playing entry
 * including all streaming metadata.
 */
void
rb_header_sync (RBHeader *header)
{
	char *label_text;
	const char *location = "<null>";

	if (header->priv->entry != NULL) {
		location = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_LOCATION);
	}
	rb_debug ("syncing with entry = %s", location);

	if (header->priv->entry != NULL) {
		const char *title;
		const char *album;
		const char *artist;
		const char *stream_name = NULL;
		char *streaming_title;
		char *streaming_artist;
		char *streaming_album;
		GString *label_str;

		gboolean have_duration = (header->priv->duration > 0);

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

		label_str = g_string_sized_new (100);

		/* stick a right-to-left mark in the string for RTL display */
		if (gtk_widget_get_direction (GTK_WIDGET (header->priv->song)) == GTK_TEXT_DIR_RTL) {
			g_string_append (label_str, "\xE2\x80\x8F");
		}

		append_and_free (label_str, TITLE_MARKUP (title));

		if (artist != NULL && artist[0] != '\0')
			append_and_free (label_str, ARTIST_MARKUP (artist));

		if (album != NULL && album[0] != '\0')
			append_and_free (label_str, ALBUM_MARKUP (album));

		if (stream_name && stream_name[0] != '\0')
			append_and_free (label_str, STREAM_MARKUP (stream_name));

		label_text = g_string_free (label_str, FALSE);
		gtk_label_set_markup (GTK_LABEL (header->priv->song), label_text);
		g_free (label_text);

		rb_header_set_show_timeline (header, have_duration && header->priv->seekable);
		if (have_duration)
			rb_header_sync_time (header);

		g_free (streaming_artist);
		g_free (streaming_album);
		g_free (streaming_title);
	} else {
		rb_debug ("not playing");
		label_text = TITLE_MARKUP (_("Not Playing"));
		gtk_label_set_markup (GTK_LABEL (header->priv->song), label_text);
		g_free (label_text);

		rb_header_set_show_timeline (header, FALSE);

		header->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (header->priv->adjustment, 0.0);
		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, FALSE);

		gtk_label_set_text (GTK_LABEL (header->priv->elapsed), "");
	}
}

/**
 * rb_header_set_show_position_slider:
 * @header: the #RBHeader
 * @show: whether the position slider should be shown
 *
 * Sets the visibility of the position slider.  This is not currently
 * used properly.
 */
void
rb_header_set_show_position_slider (RBHeader *header,
				    gboolean show)
{
	if (header->priv->scaleline_shown == show)
		return;

	header->priv->scaleline_shown = show;

	if (show) {
		gtk_widget_show_all (GTK_WIDGET (header->priv->scaleline));
		rb_header_sync_time (header);
	} else {
		gtk_widget_hide (GTK_WIDGET (header->priv->scaleline));
	}
}

static void
rb_header_set_show_timeline (RBHeader *header,
			     gboolean show)
{
	gtk_widget_set_sensitive (header->priv->scaleline, show);
}

/**
 * rb_header_sync_time:
 * @header: the #RBHeader
 *
 * Updates the time display components of the header.
 * If the position slider is being dragged, the display is not updated.
 * If the duration of the playing entry is known, the position slider is
 * updated along with the elapsed/remaining time display.  Otherwise,
 * the slider is made insensitive.
 */
void
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
		gtk_adjustment_set_value (header->priv->adjustment, progress);
		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, header->priv->seekable);
	} else {
		header->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (header->priv->adjustment, 0.0);
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
	header->priv->slider_dragging = TRUE;
	header->priv->latest_set_time = -1;
	g_object_notify (G_OBJECT (header), "slider-dragging");

	/* HACK: we want the behaviour you get with the middle button, so we
	 * mangle the event.  clicking with other buttons moves the slider in
	 * step increments, clicking with the middle button moves the slider to
	 * the location of the click.
	 */
	event->button = 2;


	return FALSE;
}

static gboolean
slider_moved_timeout (RBHeader *header)
{
	GDK_THREADS_ENTER ();

	apply_slider_position (header);
	header->priv->slider_moved_timeout = 0;

	GDK_THREADS_LEAVE ();

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

	progress = gtk_adjustment_get_value (header->priv->adjustment);
	header->priv->elapsed_time = (guint) ((progress+0.5) * RB_PLAYER_SECOND);

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
	/* HACK: see slider_press_callback */
	event->button = 2;

	if (header->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	if (header->priv->slider_moved_timeout != 0) {
		g_source_remove (header->priv->slider_moved_timeout);
		header->priv->slider_moved_timeout = 0;
	}

	apply_slider_position (header);
	header->priv->slider_dragging = FALSE;
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

static void
rb_header_update_elapsed (RBHeader *header)
{
	long seconds;

	/* sanity check */
	seconds = header->priv->elapsed_time / RB_PLAYER_SECOND;
	if (header->priv->duration > 0 && seconds > header->priv->duration)
		return;

	if (header->priv->entry != NULL) {
		char *elapsed_text;

		elapsed_text = rb_make_elapsed_time_string (seconds,
							    header->priv->duration,
							    !eel_gconf_get_boolean (CONF_UI_TIME_DISPLAY));
		gtk_label_set_text (GTK_LABEL (header->priv->elapsed), elapsed_text);
		g_free (elapsed_text);
	} else {
		gtk_label_set_text (GTK_LABEL (header->priv->elapsed), "");
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
