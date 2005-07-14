/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of GStreamer backends, with workarounds for bugs
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@debian.org>
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
#include <gdk/gdk.h>
#include <gst/gst.h>
#include <gst/gconf/gconf.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-debug.h"
#include "rb-player.h"
#include "rb-debug.h"
#include "rb-marshal.h"

G_DEFINE_TYPE(RBPlayer, rb_player, G_TYPE_OBJECT)

static void rb_player_finalize (GObject *object);

struct RBPlayerPrivate
{
	char *uri;

	GstElement *playbin;

	GError *error;

	gboolean playing;

	guint error_signal_id;

	float cur_volume;

	GTimer *timer;
	long timer_add;

	guint tick_timeout_id;
};

typedef enum
{
	EOS,
	INFO,
	ERROR,
	TICK,
	LAST_SIGNAL
} RBPlayerSignalType;

typedef struct
{
	RBPlayer *object;
	RBMetaDataField info_field;
	GError *error;
	GValue *info;
} RBPlayerSignal;

static guint rb_player_signals[LAST_SIGNAL] = { 0 };

static gboolean rb_player_sync_pipeline (RBPlayer *mp, GError **error);
static void rb_player_gst_free_playbin (RBPlayer *player);

static void
rb_player_class_init (RBPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_player_finalize;

	rb_player_signals[EOS] =
		g_signal_new ("eos",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, eos),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_player_signals[INFO] =
		g_signal_new ("info",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, info),
			      NULL, NULL,
			      rb_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE,
			      2, G_TYPE_INT, G_TYPE_POINTER);
	rb_player_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_player_signals[TICK] =
		g_signal_new ("tick",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, tick),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__LONG,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_LONG);
}

static gboolean
tick_timeout (RBPlayer *mp)
{
	if (mp->priv->playing == FALSE)
		return TRUE;

	g_signal_emit (G_OBJECT (mp), rb_player_signals[TICK], 0,
		       rb_player_get_time (mp));

	return TRUE;
}

static void
rb_player_init (RBPlayer *mp)
{
	gint ms_period = 1000 / RB_PLAYER_TICK_HZ;

	mp->priv = g_new0 (RBPlayerPrivate, 1);
	mp->priv->tick_timeout_id = g_timeout_add (ms_period, (GSourceFunc) tick_timeout, mp);
	
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *mp;

	mp = RB_PLAYER (object);

	g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->playbin) {
		g_signal_handler_disconnect (G_OBJECT (mp->priv->playbin),
					     mp->priv->error_signal_id);
		
		gst_element_set_state (mp->priv->playbin,
				       GST_STATE_NULL);
		
		rb_player_gst_free_playbin (mp);
	}

	if (mp->priv->timer)
		g_timer_destroy (mp->priv->timer);
	
	g_free (mp->priv);

	G_OBJECT_CLASS (rb_player_parent_class)->finalize (object);
}

static void
rb_player_gst_free_playbin (RBPlayer *player)
{
	if (player->priv->playbin == NULL)
		return;
	
	gst_object_unref (GST_OBJECT (player->priv->playbin));
	player->priv->playbin = NULL;
}

static gboolean
eos_signal_idle (RBPlayer *mp)
{
	g_signal_emit (G_OBJECT (mp), rb_player_signals[EOS], 0);

	g_object_unref (G_OBJECT (mp));

	return FALSE;
}

static gboolean
error_signal_idle (RBPlayerSignal *signal)
{
	g_signal_emit (G_OBJECT (signal->object),
		       rb_player_signals[ERROR], 0,
		       signal->error);

	/* close if not already closing */
	if (signal->object->priv->uri != NULL)
		rb_player_close (signal->object, NULL);

	g_object_unref (G_OBJECT (signal->object));
	g_error_free (signal->error);
	g_free (signal);

	return FALSE;
}

static void
eos_cb (GstElement *element,
	RBPlayer *mp)
{
	g_object_ref (G_OBJECT (mp));
	g_idle_add ((GSourceFunc) eos_signal_idle, mp);
}

static void
rb_player_gst_signal_error (RBPlayer *mp, int code, const char *msg)
{
	RBPlayerSignal *signal;

	signal = g_new0 (RBPlayerSignal, 1);
	signal->object = mp;
	signal->error = g_error_new_literal (RB_PLAYER_ERROR,
					     code,
					     msg);

	g_object_ref (G_OBJECT (mp));

	g_idle_add ((GSourceFunc) error_signal_idle, signal);
}

static void
error_cb (GstElement *element,
	  GstElement *source,
	  GError *error,
	  gchar *debug,
	  RBPlayer *mp)
{
	int code;

	if ((error->domain == GST_CORE_ERROR)
	    || (error->domain == GST_LIBRARY_ERROR)
	    || (error->code == GST_RESOURCE_ERROR_BUSY)) {
		code = RB_PLAYER_ERROR_NO_AUDIO;
	} else {
		code = RB_PLAYER_ERROR_GENERAL;
	}

	rb_player_gst_signal_error (mp, code, error->message);
}

static gboolean
info_signal_idle (RBPlayerSignal *signal)
{
	g_signal_emit (G_OBJECT (signal->object),
		       rb_player_signals[INFO], 0,
		       signal->info_field, signal->info);

	g_object_unref (G_OBJECT (signal->object));
	g_free (signal->info);
	g_free (signal);

	return FALSE;
}

static void
deep_notify_cb (GstElement *element, GstElement *orig,
	        GParamSpec *pspec, RBPlayer *player)
{
	if (!(pspec->flags & G_PARAM_READABLE)) return;

	if (strcmp (pspec->name, "iradio-title") == 0) {
		RBPlayerSignal *signal;

		rb_debug ("caught deep notify for iradio-title");

		signal = g_new0 (RBPlayerSignal, 1);

		signal->info_field = RB_METADATA_FIELD_TITLE;
		signal->info = g_new0 (GValue, 1);
		g_value_init (signal->info, G_TYPE_STRING); 
		g_object_get_property (G_OBJECT (orig), pspec->name, signal->info);

		signal->object = player;

		g_object_ref (G_OBJECT (player));

		g_idle_add ((GSourceFunc) info_signal_idle, signal);
		return;
	}
}

static void
rb_player_construct (RBPlayer *mp, GError **error)
{
	char *element_name = NULL;
	GstElement *sink;

	/* playbin */
	rb_debug ("constructing element \"playbin\"");
	mp->priv->playbin = gst_element_factory_make ("playbin", "playbin");
	if (mp->priv->playbin == NULL) {
		goto missing_element;
	}
	g_signal_connect_object (G_OBJECT (mp->priv->playbin),
				 "deep_notify",
				 G_CALLBACK (deep_notify_cb),
				 mp, 0);

	mp->priv->error_signal_id =
		g_signal_connect_object (G_OBJECT (mp->priv->playbin),
					 "error",
					 G_CALLBACK (error_cb),
					 mp, 0);

	/* Output sink */
	sink = gst_gconf_get_default_audio_sink ();
	if (sink == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_NO_AUDIO,
			     _("Could not create audio output element; check your settings"));
		rb_player_gst_free_playbin (mp);
		return;
	}

	g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", sink, NULL);
	g_signal_connect_object (G_OBJECT (mp->priv->playbin), "eos",
				 G_CALLBACK (eos_cb), mp, 0);

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	rb_player_set_volume (mp, mp->priv->cur_volume);

	if (mp->priv->timer)
		g_timer_destroy (mp->priv->timer);
	mp->priv->timer = g_timer_new ();
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = 0;
	rb_debug ("pipeline construction complete");
	return;
missing_element:
	{
		char *err = g_strdup_printf (_("Failed to create %s element; check your installation"),
					     element_name);
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     err);
		g_free (err);
		rb_player_gst_free_playbin (mp);
	}
}

RBPlayer *
rb_player_new (GError **error)
{
	RBPlayer *mp;
	GstElement *dummy;

	dummy = gst_element_factory_make ("fakesink", "fakesink");
	if (!dummy
	    || !gst_scheduler_factory_make (NULL, GST_ELEMENT (dummy))) {
		g_set_error (error, RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     "%s",
			     _("Couldn't initialize scheduler.  Did you run gst-register?"));
		return NULL;
	}

	mp = RB_PLAYER (g_object_new (RB_TYPE_PLAYER, NULL, NULL));

	return mp;
}

GQuark
rb_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_player_error");

	return quark;
}

static gboolean
rb_player_sync_pipeline (RBPlayer *mp, GError **error)
{
	rb_debug ("syncing pipeline");
	if (mp->priv->playing) {
 		rb_debug ("PLAYING pipeline");
 		if (gst_element_set_state (mp->priv->playbin,
 					   GST_STATE_PLAYING) == GST_STATE_FAILURE) {
 			g_set_error (error,
 				     RB_PLAYER_ERROR,
 				     RB_PLAYER_ERROR_GENERAL,
 				     _("Could not start pipeline playing"));
 			return FALSE;
		}
		g_timer_start (mp->priv->timer);
	} else {
		rb_debug ("PAUSING pipeline");
		if (gst_element_set_state (mp->priv->playbin,
					   GST_STATE_PAUSED) == GST_STATE_FAILURE) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_GENERAL,
				     _("Could not pause playback"));
			return FALSE;
		}
	}
	return TRUE;
}

void
rb_player_open (RBPlayer *mp,
		const char *uri,
		GError **error)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	if (mp->priv->playbin == NULL) {
		rb_player_construct (mp, error);
		if (error && *error)
			return;
	}

	g_assert (mp->priv->playbin != NULL);

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (uri == NULL) {
		mp->priv->playing = FALSE;
		return;
	}
	g_object_set (G_OBJECT (mp->priv->playbin), "uri", uri, NULL);	
	mp->priv->uri = g_strdup (uri);

	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = 0;

	if (!rb_player_sync_pipeline (mp, error)) {
		rb_player_close (mp, NULL);
	}
}

void
rb_player_close (RBPlayer *mp, GError **error)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	mp->priv->playing = FALSE;

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (mp->priv->playbin == NULL)
		return;

	if (gst_element_set_state (mp->priv->playbin,
				   GST_STATE_READY) != GST_STATE_SUCCESS) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to close audio output sink"));
	}
}

gboolean
rb_player_opened (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), FALSE);

	return mp->priv->uri != NULL;
}

void
rb_player_play (RBPlayer *mp, GError **error)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	mp->priv->playing = TRUE;

	g_return_if_fail (mp->priv->playbin != NULL);

	rb_player_sync_pipeline (mp, error);
}

void
rb_player_pause (RBPlayer *mp)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->playbin != NULL);

	mp->priv->timer_add += floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5);
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);

	rb_player_sync_pipeline (mp, NULL);
}

gboolean
rb_player_playing (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), FALSE);

	return mp->priv->playing;
}

void
rb_player_set_replaygain (RBPlayer *mp,
				    double track_gain, double track_peak, double album_gain, double album_peak)
{
	double scale;
	double gain = 0;
	double peak = 0;

	g_return_if_fail (RB_IS_PLAYER (mp));

	if (album_gain != 0)
	  gain = album_gain;
	else
	  gain = track_gain;

	if (gain == 0)
		return;
	  
	scale = pow (10., gain / 20);

	/* anti clip */
	if (album_peak != 0)
	  peak = album_peak;
	else
	  peak = track_peak;

	if (peak != 0 && (scale * peak) > 1)
		scale = 1.0 / peak;
		
	/* For security */
	if (scale > 15)
		scale = 15;
	
        rb_debug ("Scale : %f New volume : %f", scale, mp->priv->cur_volume * scale);

	if (mp->priv->playbin != NULL) {
		g_object_set (G_OBJECT (mp->priv->playbin),
			      "volume",
			      mp->priv->cur_volume * scale,
			      NULL);
	}
}

void
rb_player_set_volume (RBPlayer *mp,
		      float volume)
{
	g_return_if_fail (RB_IS_PLAYER (mp));
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	if (mp->priv->playbin != NULL) {
		g_object_set (G_OBJECT (mp->priv->playbin),
			      "volume",
			      volume,
			      NULL);
	}

	mp->priv->cur_volume = volume;
}

float
rb_player_get_volume (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), 0.0);

	return mp->priv->cur_volume;
}

gboolean
rb_player_seekable (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), FALSE);
	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	/* FIXME we're lying here, no idea how to fix this though, without trying
	 * a seek which might disrupt playback */
	return TRUE;
}

void
rb_player_set_time (RBPlayer *mp, long time)
{
	g_return_if_fail (RB_IS_PLAYER (mp));
	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->playbin != NULL);

	gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED);

	gst_element_seek (mp->priv->playbin, 
			  GST_FORMAT_TIME 
			  | GST_SEEK_METHOD_SET 
			  | GST_SEEK_FLAG_FLUSH, 
			  time * GST_SECOND);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = time;
}

long
rb_player_get_time (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), -1);

	if (mp->priv->playbin != NULL)
		return (long) floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5) + mp->priv->timer_add;
	else
		return -1;
}
