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
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBHeader *header);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *header);
static void slider_changed_callback (GtkWidget *widget, RBHeader *header);

static void rb_header_elapsed_changed_cb (RBShellPlayer *player, guint elapsed, RBHeader *header);

struct RBHeaderPrivate
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	char *title;

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
	guint value_changed_update_handler;
	GtkWidget *elapsed;

	long elapsed_time;
	long duration;
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_SHELL_PLAYER,
	PROP_ENTRY,
	PROP_TITLE,
};

#define SONG_MARKUP(xSONG) g_markup_printf_escaped ("<big><b>%s</b></big>", xSONG);
#define SONG_MARKUP_ALBUM_ARTIST(xSONG, xALBUM, xARTIST) g_markup_printf_escaped ("<big><b>%s</b></big> %s <i>%s</i> %s <i>%s</i>", xSONG, _("by"), xARTIST, _("from"), xALBUM);

G_DEFINE_TYPE (RBHeader, rb_header, GTK_TYPE_HBOX)

static void
rb_header_class_init (RBHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_header_finalize;

	object_class->set_property = rb_header_set_property;
	object_class->get_property = rb_header_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_ENTRY,
					 g_param_spec_boxed ("entry",
							     "RhythmDBEntry",
							     "RhythmDBEntry pointer",
							     RHYTHMDB_TYPE_ENTRY,
							     G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "shell player",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "title",
							      "title",
							      NULL,
							      G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBHeaderPrivate));
}

static void
rb_header_init (RBHeader *header)
{
	/**
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
	gtk_box_pack_start (GTK_BOX (hbox), header->priv->song, TRUE, TRUE, 0);
	gtk_widget_show (header->priv->song);

	/* construct the time display */
	header->priv->timeline = gtk_hbox_new (FALSE, 3);
	header->priv->elapsed = gtk_label_new ("");

	gtk_misc_set_padding (GTK_MISC (header->priv->elapsed), 2, 0);
	gtk_box_pack_start (GTK_BOX (header->priv->timeline), header->priv->elapsed, FALSE, FALSE, 0);
	gtk_widget_set_sensitive (header->priv->timeline, FALSE);
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
		header->priv->entry = g_value_get_boxed (value);
		if (header->priv->entry) {
			header->priv->duration = rhythmdb_entry_get_ulong (header->priv->entry,
									   RHYTHMDB_PROP_DURATION);
		} else {
			header->priv->duration = -1;
		}
		break;
	case PROP_SHELL_PLAYER:
		header->priv->shell_player = g_value_get_object (value);
		g_signal_connect (G_OBJECT (header->priv->shell_player),
				  "elapsed-changed",
				  (GCallback) rb_header_elapsed_changed_cb,
				  header);
		break;
	case PROP_TITLE:
		g_free (header->priv->title);
		header->priv->title = g_value_dup_string (value);
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
	case PROP_TITLE:
		g_value_set_string (value, header->priv->title);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBHeader *
rb_header_new (RBShellPlayer *shell_player)
{
	RBHeader *header;

	header = RB_HEADER (g_object_new (RB_TYPE_HEADER,
					  "shell-player", shell_player,
					  "title", NULL,
					  "spacing", 6, NULL));

	g_return_val_if_fail (header->priv != NULL, NULL);

	return header;
}

void
rb_header_set_playing_entry (RBHeader *header, RhythmDBEntry *entry)
{
	g_object_set (header, "entry", entry, NULL);
}

void
rb_header_set_title (RBHeader *header, const char *title)
{
	g_object_set (header, "title", title, NULL);
}

void
rb_header_sync (RBHeader *header)
{
	char *label_text;

	rb_debug ("syncing with entry = %p", header->priv->entry);

	if (header->priv->entry != NULL) {
		const char *song = header->priv->title;
		const char *album;
		const char *artist;

		gboolean have_duration = (header->priv->duration > 0);

		album = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ALBUM);
		artist = rhythmdb_entry_get_string (header->priv->entry, RHYTHMDB_PROP_ARTIST);

		/* check for artist and album */
		if ((album != NULL && artist != NULL)
		    && (strlen (album) > 0 && strlen (artist) > 0)) {
			label_text = SONG_MARKUP_ALBUM_ARTIST (song, album, artist);
		} else {
			label_text = SONG_MARKUP (song);
		}

		gtk_label_set_markup (GTK_LABEL (header->priv->song), label_text);
		g_free (label_text);

		rb_header_set_show_timeline (header, have_duration);
		if (have_duration)
			rb_header_sync_time (header);
	} else {
		char *tmp;

		rb_debug ("not playing");
		label_text = SONG_MARKUP (_("Not Playing"));
		gtk_label_set_markup (GTK_LABEL (header->priv->song), label_text);
		g_free (label_text);

		rb_header_set_show_timeline (header, FALSE);

		header->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (header->priv->adjustment, 0.0);
		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, FALSE);

		tmp = rb_make_elapsed_time_string (0, 0, !eel_gconf_get_boolean (CONF_UI_TIME_DISPLAY));
		gtk_label_set_text (GTK_LABEL (header->priv->elapsed), tmp);
		g_free (tmp);
	}
}

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
	gtk_widget_set_sensitive (header->priv->timeline, show);
	gtk_widget_set_sensitive (header->priv->scaleline, show);
}

gboolean
rb_header_sync_time (RBHeader *header)
{
	int seconds;

	if (header->priv->shell_player == NULL)
		return TRUE;

	if (header->priv->slider_dragging == TRUE) {
		rb_debug ("slider is dragging, not syncing");
		return TRUE;
	}

	seconds = header->priv->elapsed_time;

	if (header->priv->duration > -1) {
		double progress = 0.0;

		if (seconds > 0) {
			progress = (double) (long) seconds;
		} else {
			header->priv->adjustment->upper = header->priv->duration;
			g_signal_emit_by_name (G_OBJECT (header->priv->adjustment), "changed");
		}

		header->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (header->priv->adjustment, progress);
		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, TRUE);
	} else {
		header->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (header->priv->adjustment, 0.0);
		header->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (header->priv->scale, FALSE);
	}

	rb_header_update_elapsed (header);

	return TRUE;
}

static gboolean
slider_press_callback (GtkWidget *widget,
		       GdkEventButton *event,
		       RBHeader *header)
{
	header->priv->slider_dragging = TRUE;
	header->priv->latest_set_time = -1;
	return FALSE;
}

static gboolean
slider_moved_timeout (RBHeader *header)
{
	double progress;
	long new;

	GDK_THREADS_ENTER ();

	progress = gtk_adjustment_get_value (gtk_range_get_adjustment (GTK_RANGE (header->priv->scale)));
	new = (long) (progress+0.5);

	rb_debug ("setting time to %ld", new);
	rb_shell_player_set_playing_time (header->priv->shell_player, new, NULL);

	header->priv->latest_set_time = new;
	header->priv->slider_moved_timeout = 0;

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static gboolean
slider_moved_callback (GtkWidget *widget,
		       GdkEventMotion *event,
		       RBHeader *header)
{
	GtkAdjustment *adjustment;
	double progress;

	if (header->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	header->priv->elapsed_time = (long) (progress+0.5);

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

static gboolean
slider_release_callback (GtkWidget *widget,
			 GdkEventButton *event,
			 RBHeader *header)
{
	double progress;
	long new;
	GtkAdjustment *adjustment;

	if (header->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	new = (long) (progress+0.5);

	if (new != header->priv->latest_set_time) {
		rb_debug ("setting time to %ld", new);
		rb_shell_player_set_playing_time (header->priv->shell_player, new, NULL);
	}

	header->priv->slider_dragging = FALSE;

	rb_header_sync_time (header);

	if (header->priv->slider_moved_timeout != 0) {
		g_source_remove (header->priv->slider_moved_timeout);
		header->priv->slider_moved_timeout = 0;
	}

	return FALSE;
}

static gboolean
changed_idle_callback (RBHeader *header)
{
	GDK_THREADS_ENTER ();

	slider_release_callback (header->priv->scale, NULL, header);

	header->priv->value_changed_update_handler = 0;
	rb_debug ("in changed_idle_callback");

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
slider_changed_callback (GtkWidget *widget,
		         RBHeader *header)
{
	if (header->priv->slider_dragging == FALSE &&
	    header->priv->slider_locked == FALSE &&
	    header->priv->value_changed_update_handler == 0) {
		header->priv->slider_dragging = TRUE;
		header->priv->value_changed_update_handler =
			g_idle_add ((GSourceFunc) changed_idle_callback, header);
	}
}

static void
rb_header_update_elapsed (RBHeader *header)
{
	char *elapsed_text;

	/* sanity check */
	if ((header->priv->elapsed_time > header->priv->duration) || (header->priv->elapsed_time < 0))
		return;

	elapsed_text = rb_make_elapsed_time_string (header->priv->elapsed_time,
						    header->priv->duration,
						    !eel_gconf_get_boolean (CONF_UI_TIME_DISPLAY));

	gtk_label_set_text (GTK_LABEL (header->priv->elapsed), elapsed_text);
	g_free (elapsed_text);
}

static void
rb_header_elapsed_changed_cb (RBShellPlayer *player,
			      guint elapsed,
			      RBHeader *header)
{
	header->priv->elapsed_time = elapsed;
	rb_header_sync_time (header);
}
