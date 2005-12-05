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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomeui/libgnomeui.h> /* for GnomeUrl */

#include "rb-song-display-box.h"
#include "rb-stock-icons.h"
#include "rb-header.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_header_class_init (RBHeaderClass *klass);
static void rb_header_init (RBHeader *player);
static void rb_header_finalize (GObject *object);
static void rb_header_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_header_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static long rb_header_get_duration (RBHeader *player);
static void rb_header_set_show_timeline (RBHeader *player,
			                 gboolean show);
static void rb_header_update_elapsed (RBHeader *player);
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *player);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBHeader *player);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *player);
static void slider_changed_callback (GtkWidget *widget, RBHeader *player);

static void rb_header_tick_cb (RBPlayer *mmplayer, long time, RBHeader *header);

typedef struct
{
	long elapsed;
	long duration;
} RBHeaderState;

struct RBHeaderPrivate
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	char *title;
	char *urltext, *urllink;

	RBPlayer *mmplayer;
	gulong tick_id;

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

	RBSongDisplayBox *displaybox;
	gboolean displaybox_shown;
	GtkWidget *urlline;
	gboolean urlline_shown;
	GnomeHRef *url;

	RBHeaderState *state;
};

#define RB_HEADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_HEADER, RBHeaderPrivate))

enum
{
	PROP_0,
	PROP_DB,
	PROP_PLAYER,
	PROP_ENTRY,
	PROP_TITLE,
	PROP_URLTEXT,
	PROP_URLLINK,
};

#define SONG_MARKUP(xSONG) g_strdup_printf ("<big><b>%s</b></big>", xSONG);

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
					 g_param_spec_pointer ("entry",
							       "RhythmDBEntry",
							       "RhythmDBEntry pointer",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
							      "Player",
							      "RBPlayer object",
							      RB_TYPE_PLAYER,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "title",
							      "title",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URLTEXT,
					 g_param_spec_string ("urltext",
							      "urltext",
							      "urltext",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URLLINK,
					 g_param_spec_string ("urllink",
							      "urllink",
							      "urllink",
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
	 *     RBSongDisplayBox		(priv->displaybox)
	 *       GtkLabel		("from")
	 *       GtkWidget		(priv->displaybox->album)
	 *       GtkLabel		("by")
	 *       GtkWidget		(priv->displaybox->artist)
	 *     GtkHBox			(priv->urlline)
	 *       GtkLabel		("Listening to")
	 *       GnomeHRef		(priv->url)
	 *   GtkHBox			(priv->timeline)
	 *     GtkHScale		(priv->scale)
	 *     GtkAlignment
	 *       GtkLabel		(priv->elapsed)
	 */
	GtkWidget *urlline, *label,  *hbox;
	GtkWidget *vbox;
	char *s;

	header->priv = g_new0 (RBHeaderPrivate, 1);
	header->priv->state = g_new0 (RBHeaderState, 1);

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
	gtk_box_pack_start (GTK_BOX (hbox), header->priv->song, FALSE, TRUE, 0);
	gtk_widget_show (header->priv->song);

	/* Construct the Artist/Album display */
	header->priv->displaybox = RB_SONG_DISPLAY_BOX (rb_song_display_box_new ());
	header->priv->displaybox_shown = FALSE;
	gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (header->priv->displaybox), TRUE, TRUE, 0);

	/* Construct the URL display */

	urlline = header->priv->urlline = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_start (GTK_BOX (hbox), header->priv->urlline, TRUE, TRUE, 0);

	label = gtk_label_new (_("Listening to "));
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (label), FALSE, TRUE, 0);
	header->priv->url = (GnomeHRef *) gnome_href_new ("", "");
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (header->priv->url), FALSE, TRUE, 0);
	header->priv->urlline_shown = FALSE;

	/* construct the time display */
	header->priv->timeline = gtk_hbox_new (FALSE, 3);

	s = rb_header_get_elapsed_string (header);
	header->priv->elapsed = gtk_label_new (s);
	g_free (s);

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
	RBHeader *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HEADER (object));

	player = RB_HEADER (object);
	g_return_if_fail (player->priv != NULL);

	g_free (player->priv->state);
	g_free (player->priv);

	G_OBJECT_CLASS (rb_header_parent_class)->finalize (object);
}

static void
rb_header_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBHeader *player = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		player->priv->db = g_value_get_object (value);
		break;
	case PROP_ENTRY:
		player->priv->entry = g_value_get_pointer (value);
		break;
	case PROP_PLAYER:
		if (player->priv->mmplayer) {
			g_signal_handler_disconnect (player->priv->mmplayer,
						     player->priv->tick_id);
			player->priv->tick_id = 0;
		}
		player->priv->mmplayer = g_value_get_object (value);
		player->priv->tick_id = g_signal_connect (G_OBJECT (player->priv->mmplayer),
							  "tick", 
							  (GCallback) rb_header_tick_cb,
							  player);
		break;
	case PROP_TITLE:
		g_free (player->priv->title);
		player->priv->title = g_value_dup_string (value);
		break;
	case PROP_URLTEXT:
		g_free (player->priv->urltext);
		player->priv->urltext = g_value_dup_string (value);
		break;
	case PROP_URLLINK:
		g_free (player->priv->urllink);
		player->priv->urllink = g_value_dup_string (value);
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
	RBHeader *player = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, player->priv->db);
		break;
	case PROP_ENTRY:
		g_value_set_object (value, player->priv->entry);
		break;
	case PROP_PLAYER:
		g_value_set_object (value, player->priv->mmplayer);
		break;
	case PROP_TITLE:
		g_value_set_string (value, player->priv->title);
		break;
	case PROP_URLTEXT:
		g_value_set_string (value, player->priv->urltext);
		break;
	case PROP_URLLINK:
		g_value_set_string (value, player->priv->urllink);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBHeader *
rb_header_new (RBPlayer *mmplayer)
{
	RBHeader *player;

	player = RB_HEADER (g_object_new (RB_TYPE_HEADER, "player", mmplayer,
					  "title", NULL,
					  "spacing", 6, NULL));

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

void
rb_header_set_playing_entry (RBHeader *player, RhythmDBEntry *entry)
{
	g_object_set (G_OBJECT (player), "entry", entry, NULL);
}

void
rb_header_set_title (RBHeader *player, const char *title)
{
	g_object_set (G_OBJECT (player), "title", title, NULL);
}

static long
rb_header_get_duration (RBHeader *player)
{
	if (player->priv->entry) {
		return player->priv->entry->duration;
	}
	return -1;
}

void
rb_header_sync (RBHeader *player)
{
	char *tmp;

	rb_debug ("syncing with node = %p", player->priv->entry);
	rb_song_display_box_sync (player->priv->displaybox, player->priv->entry);
	
	if (player->priv->entry != NULL) {
		const char *song = player->priv->title;
		char *escaped;
		gboolean have_duration = rb_header_get_duration (player) > 0;
		const char *album, *artist; 

		album = rhythmdb_entry_get_string (player->priv->entry, RHYTHMDB_PROP_ALBUM);
		artist = rhythmdb_entry_get_string (player->priv->entry, RHYTHMDB_PROP_ARTIST);

		escaped = g_markup_escape_text (song, -1);
		tmp = SONG_MARKUP (escaped);
		g_free (escaped);
		gtk_label_set_markup (GTK_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_header_set_show_artist_album (player, (album != NULL && artist != NULL)
						 && (strlen (album) > 0 && strlen (artist) > 0));

		if (player->priv->urlline_shown)
		{
			g_return_if_fail (player->priv->urltext != NULL);
			rb_debug ("urlline shown, urltext: %s urllink: %s",
				  player->priv->urltext, player->priv->urllink);
			gnome_href_set_url (player->priv->url, player->priv->urllink);
			gnome_href_set_text (player->priv->url, player->priv->urltext);
		}

		rb_header_set_show_timeline (player, have_duration);
		if (have_duration)
			rb_header_sync_time (player);
	} else {
		rb_debug ("not playing");
		tmp = SONG_MARKUP (_("Not Playing"));
		gtk_label_set_markup (GTK_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_header_set_urldata (player, NULL, NULL);
		rb_header_set_show_artist_album (player, FALSE);
		rb_header_set_show_timeline (player, FALSE);

		player->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, 0.0);
		player->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, FALSE);

		tmp = rb_header_get_elapsed_string (player);
		gtk_label_set_text (GTK_LABEL (player->priv->elapsed), tmp);
		g_free (tmp);
	}
}

void
rb_header_set_urldata (RBHeader *player,
		       const char *urltext,
		       const char *urllink)
{
	gboolean show = (urltext != NULL);
	if (player->priv->urlline_shown == show)
		return;

	player->priv->urlline_shown = show;
	g_object_set (G_OBJECT (player), "urltext", urltext,
		      "urllink", urllink, NULL);

	if (show)
		gtk_widget_show_all (player->priv->urlline);
	else
		gtk_widget_hide (player->priv->urlline);
}

void
rb_header_set_show_artist_album (RBHeader *player,
				 gboolean show)
{
	if (player->priv->displaybox_shown == show)
		return;

	player->priv->displaybox_shown = show;

	if (show)
		gtk_widget_show_all (GTK_WIDGET (player->priv->displaybox));
	else
		gtk_widget_hide (GTK_WIDGET (player->priv->displaybox));
}

void
rb_header_set_show_position_slider (RBHeader *player,
				    gboolean show)
{
	if (player->priv->scaleline_shown == show)
		return;

	player->priv->scaleline_shown = show;

	if (show) {
		gtk_widget_show_all (GTK_WIDGET (player->priv->scaleline));
		rb_header_sync_time (player);
	} else {
		gtk_widget_hide (GTK_WIDGET (player->priv->scaleline));
	}
}

static void
rb_header_set_show_timeline (RBHeader *player,
			     gboolean show)
{
	gtk_widget_set_sensitive (player->priv->timeline, show);
	gtk_widget_set_sensitive (player->priv->scaleline, show);
}

gboolean
rb_header_sync_time (RBHeader *player)
{
	int seconds;
	long duration;

	if (player->priv->mmplayer == NULL)
		return TRUE;

	if (player->priv->slider_dragging == TRUE) {
		rb_debug ("slider is dragging, not syncing");
		return TRUE;
	}

	player->priv->state->duration = duration =
		rb_header_get_duration (player);
	seconds = player->priv->state->elapsed;

	if (duration > -1) {
		double progress = 0.0;

		if (seconds > 0)
			progress = (double) (long) seconds;
		else {
			player->priv->adjustment->upper = duration;
			g_signal_emit_by_name (G_OBJECT (player->priv->adjustment), "changed");
		}

		player->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, progress);
		player->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, TRUE);
	} else {
		player->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, 0.0);
		player->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, FALSE);
	}

	rb_header_update_elapsed (player);

	return TRUE;
}

static gboolean
slider_press_callback (GtkWidget *widget,
		       GdkEventButton *event,
		       RBHeader *player)
{
	player->priv->slider_dragging = TRUE;
	player->priv->latest_set_time = -1;
	return FALSE;
}

static gboolean
slider_moved_timeout (RBHeader *player)
{
	double progress;
	long new;

	GDK_THREADS_ENTER ();
	
	progress = gtk_adjustment_get_value (gtk_range_get_adjustment (GTK_RANGE (player->priv->scale)));
	new = (long) (progress+0.5);
	
	rb_debug ("setting time to %ld", new);
	rb_player_set_time (player->priv->mmplayer, new);

	player->priv->latest_set_time = new;
	player->priv->slider_moved_timeout = 0;
	
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static gboolean
slider_moved_callback (GtkWidget *widget,
		       GdkEventMotion *event,
		       RBHeader *player)
{
	GtkAdjustment *adjustment;
	double progress;

	if (player->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	player->priv->state->elapsed = (long) (progress+0.5);

	rb_header_update_elapsed (player);

	if (player->priv->slider_moved_timeout != 0) {
		rb_debug ("removing old timer");
		g_source_remove (player->priv->slider_moved_timeout);
		player->priv->slider_moved_timeout = 0;
	}
	player->priv->slider_moved_timeout =
		g_timeout_add (40, (GSourceFunc) slider_moved_timeout, player);
	
	return FALSE;
}

static gboolean
slider_release_callback (GtkWidget *widget,
			 GdkEventButton *event,
			 RBHeader *player)
{
	double progress;
	long new;
	GtkAdjustment *adjustment;

	if (player->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	new = (long) (progress+0.5);

	if (new != player->priv->latest_set_time) {
		rb_debug ("setting time to %ld", new);
		rb_player_set_time (player->priv->mmplayer, new);
	}

	player->priv->slider_dragging = FALSE;

	rb_header_sync_time (player);

	if (player->priv->slider_moved_timeout != 0) {
		g_source_remove (player->priv->slider_moved_timeout);
		player->priv->slider_moved_timeout = 0;
	}

	return FALSE;
}

static gboolean
changed_idle_callback (RBHeader *player)
{

	GDK_THREADS_ENTER ();

	slider_release_callback (player->priv->scale, NULL, player);

	player->priv->value_changed_update_handler = 0;
	rb_debug ("in changed_idle_callback"); 

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
slider_changed_callback (GtkWidget *widget,
		         RBHeader *player)
{
	if (player->priv->slider_dragging == FALSE &&
	    player->priv->slider_locked == FALSE &&
	    player->priv->value_changed_update_handler == 0) {
		player->priv->slider_dragging = TRUE;
		player->priv->value_changed_update_handler =
			g_idle_add ((GSourceFunc) changed_idle_callback, player);
	}
}

char *
rb_header_get_elapsed_string (RBHeader *player)
{
	int seconds = 0, minutes = 0, hours = 0, seconds2 = -1, minutes2 = -1, hours2 = -1;

	if (player->priv->state->elapsed > 0) {
		hours = player->priv->state->elapsed / (60 * 60);
		minutes = (player->priv->state->elapsed - (hours * 60 * 60)) / 60;
		seconds = player->priv->state->elapsed % 60;
	}

	/* use this if entry==NULL, so that it doesn't shift when you first start playback */
	if (player->priv->entry == NULL || player->priv->state->duration > 0) {
		if (player->priv->state->duration > 0) {
			hours2 = player->priv->state->duration / (60 * 60);
			minutes2 = (player->priv->state->duration - (hours2 * 60 * 60)) / 60;
			seconds2 = player->priv->state->duration % 60;
		} else {
			hours2 = minutes2 = seconds2 = 0;
		}

		if (eel_gconf_get_boolean (CONF_UI_TIME_DISPLAY)) {
			if (hours == 0 && hours2 == 0)
				return g_strdup_printf (_("%d:%02d of %d:%02d"),
							minutes, seconds,
							minutes2, seconds2);
			else
				return g_strdup_printf (_("%d:%02d:%02d of %d:%02d:%02d"),
							hours, minutes, seconds,
							hours2, minutes2, seconds2);
		} else {
			int remaining = player->priv->state->duration - player->priv->state->elapsed;
			int remaining_hours = remaining / (60 * 60);
			int remaining_minutes = (remaining - (remaining_hours * 60 * 60)) / 60;
			/* remaining could conceivably be negative. This would
			 * be a bug, but the elapsed time will display right
			 * with the abs(). */
			int remaining_seconds = abs (remaining % 60);
			if (hours2 == 0)
				return g_strdup_printf (_("%d:%02d of %d:%02d remaining"),
							remaining_minutes, remaining_seconds,
							minutes2, seconds2);
			else
				return g_strdup_printf (_("%d:%02d:%02d of %d:%02d:%02d remaining"),
							remaining_hours, remaining_minutes, remaining_seconds,
							hours2, minutes2, seconds2);
		}
	} else {
		if (hours == 0)
			return g_strdup_printf (_("%d:%02d"), minutes, seconds);
		else
			return g_strdup_printf (_("%d:%02d:%02d"), hours, minutes, seconds);
	}
}

static void
rb_header_update_elapsed (RBHeader *player)
{
	char *elapsed_text;

	/* sanity check */
	if ((player->priv->state->elapsed > player->priv->state->duration) || (player->priv->state->elapsed < 0))
		return;

	elapsed_text = rb_header_get_elapsed_string (player);

	gtk_label_set_text (GTK_LABEL (player->priv->elapsed), elapsed_text);
	g_free (elapsed_text);
}

static void rb_header_tick_cb (RBPlayer *mmplayer, long elapsed, RBHeader *header)
{
	gboolean sync = (elapsed != header->priv->state->elapsed);
	header->priv->state->elapsed = elapsed;
	
	if (sync) {
		GDK_THREADS_ENTER ();

		rb_header_sync_time (header);

		GDK_THREADS_LEAVE ();
	}
}
