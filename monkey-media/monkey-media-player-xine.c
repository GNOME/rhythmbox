/*  monkey-media
 *
 *  arch-tag: Implementation of Xine audio backend
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
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

#include <xine.h>
#include <string.h>
#include <math.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "monkey-media.h"
#include "monkey-media-marshal.h"
#include "monkey-media-private.h"

static void monkey_media_player_class_init (MonkeyMediaPlayerClass *klass);
static void monkey_media_player_init (MonkeyMediaPlayer *mp);
static void monkey_media_player_finalize (GObject *object);

struct MonkeyMediaPlayerPrivate
{
	char *uri;

	xine_t *xine;
	xine_ao_driver_t *audio_driver;
	xine_vo_driver_t *video_driver;

	xine_stream_t *stream;
	xine_event_queue_t *event_queue;

	char *configfile;

	float volume;
	gboolean mute;

	GTimer *timer;
	long timer_add;

	guint tick_timeout_id;
	GAsyncQueue *queue;
};

typedef struct {
	int signal;
} signal_data;

enum
{
	EOS,
	INFO,
	BUFFERING_BEGIN,
	BUFFERING_END,
	ERROR,
	TICK,
	LAST_SIGNAL
};

static guint monkey_media_player_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
monkey_media_player_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo our_info =
		{
			sizeof (MonkeyMediaPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc) monkey_media_player_class_init,
			NULL,
			NULL,
			sizeof (MonkeyMediaPlayer),
			0,
			(GInstanceInitFunc) monkey_media_player_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "MonkeyMediaPlayer",
					       &our_info, 0);
	}

	return type;
}

static void
monkey_media_player_class_init (MonkeyMediaPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = monkey_media_player_finalize;

	monkey_media_player_signals[EOS] =
		g_signal_new ("eos",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MonkeyMediaPlayerClass, eos),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	monkey_media_player_signals[INFO] =
		g_signal_new ("info",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MonkeyMediaPlayerClass, info),
			      NULL, NULL,
			      monkey_media_marshal_VOID__ENUM_POINTER,
			      G_TYPE_NONE,
			      2,
			      MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD,
			      G_TYPE_POINTER);
	monkey_media_player_signals[BUFFERING_BEGIN] =
		g_signal_new ("buffering_begin",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (MonkeyMediaPlayerClass, buffering_begin),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE,
				0);
	monkey_media_player_signals[BUFFERING_END] =
		g_signal_new ("buffering_end",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (MonkeyMediaPlayerClass, buffering_end),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE,
				0);
	monkey_media_player_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MonkeyMediaPlayerClass, error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	monkey_media_player_signals[TICK] =
		g_signal_new ("tick",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MonkeyMediaPlayerClass, tick),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__LONG,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_LONG);
}

static gboolean
tick_timeout (MonkeyMediaPlayer *mp)
{
	if (monkey_media_player_playing (mp) == FALSE)
		return TRUE;

	g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[TICK], 0,
		       monkey_media_player_get_time (mp));

	return TRUE;
}

static void
monkey_media_player_init (MonkeyMediaPlayer *mp)
{
	gint ms_period = 1000 / MONKEY_MEDIA_PLAYER_TICK_HZ;

	mp->priv = g_new0 (MonkeyMediaPlayerPrivate, 1);

	mp->priv->tick_timeout_id = g_timeout_add (ms_period, (GSourceFunc) tick_timeout, mp);
}

static void
monkey_media_player_finalize (GObject *object)
{
	MonkeyMediaPlayer *mp;

	mp = MONKEY_MEDIA_PLAYER (object);

	g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->stream != NULL) {
		xine_stop (mp->priv->stream);
		xine_close (mp->priv->stream);
		xine_event_dispose_queue (mp->priv->event_queue);
		xine_dispose (mp->priv->stream);
	}

	if (mp->priv->audio_driver != NULL) {
		xine_close_audio_driver (mp->priv->xine,
					 mp->priv->audio_driver);
	}

	if (mp->priv->video_driver != NULL) {
		xine_close_video_driver (mp->priv->xine,
					 mp->priv->video_driver);
	}

	xine_config_save (mp->priv->xine, mp->priv->configfile);
	g_free (mp->priv->configfile);

	xine_exit (mp->priv->xine);

	g_timer_destroy (mp->priv->timer);

	g_free (mp->priv->uri);

	g_free (mp->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
signal_idle (MonkeyMediaPlayer *mp)
{
	int queue_length;
	signal_data *data;

	data = g_async_queue_try_pop (mp->priv->queue);
	if (data == NULL)
		return FALSE;

	switch (data->signal) {
	case EOS:
		g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[EOS], 0);
		break;
	case BUFFERING_BEGIN:
		g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[BUFFERING_BEGIN], 0);
		break;
	case BUFFERING_END:
		g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[BUFFERING_END], 0);
		break;
	}

	g_object_unref (G_OBJECT (mp));
	g_free (data);
	queue_length = g_async_queue_length (mp->priv->queue);

	return (queue_length > 0);
}

static void
xine_event (MonkeyMediaPlayer *mp,
	    const xine_event_t *event)
{
	signal_data *data;
	xine_progress_data_t* prg;

	switch (event->type) {
	case XINE_EVENT_UI_PLAYBACK_FINISHED:
		g_object_ref (G_OBJECT (mp));
		data = g_new0 (signal_data, 1);
		data->signal = EOS;
		g_async_queue_push (mp->priv->queue, data);
		g_idle_add ((GSourceFunc) signal_idle, mp);
		break;
	case XINE_EVENT_PROGRESS:
		prg = event->data;

		if (prg->percent == 0 || prg->percent == 100)
		{
			g_object_ref (G_OBJECT (mp));
			data = g_new0 (signal_data, 1);
			data->signal = prg->percent ?
				BUFFERING_END : BUFFERING_BEGIN;
			g_idle_add ((GSourceFunc) signal_idle, mp);
			break;
		}
	}
}

static void
monkey_media_player_construct (MonkeyMediaPlayer *mp,
			       GError **error)
{
	const char *audio_driver;
	xine_cfg_entry_t entry;

	mp->priv->xine = xine_new ();

	mp->priv->configfile = g_build_filename (monkey_media_get_dir (),
						 "xine-config",
						 NULL);
	xine_config_load (mp->priv->xine, mp->priv->configfile);

	xine_init (mp->priv->xine);

	audio_driver = monkey_media_get_audio_driver ();
	if (audio_driver == NULL)
		audio_driver = "auto";

	if (strcmp (audio_driver, "null") == 0) {
		mp->priv->audio_driver = NULL;
	} else {
		if (strcmp (audio_driver, "auto") != 0) {
			/* first try the requested driver */
			mp->priv->audio_driver = xine_open_audio_driver (mp->priv->xine,
									 audio_driver, NULL);
		}

		/* autoprobe */
		if (mp->priv->audio_driver == NULL)
			mp->priv->audio_driver = xine_open_audio_driver (mp->priv->xine, NULL, NULL);
	}

	if (mp->priv->audio_driver == NULL) {
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_AUDIO,
			     _("Failed to set up an audio driver; check your installation"));
	}

#ifndef HAVE_NULL_VIDEO
	mp->priv->video_driver = xine_open_video_driver (mp->priv->xine, "none",
							 XINE_VISUAL_TYPE_NONE, NULL);

	/* Reduce the number of buffers to lower the memory usage */
	memset (&entry, 0, sizeof (entry));
	if (!xine_config_lookup_entry (mp->priv->xine,
				"video.num_buffers", &entry))
	{
		xine_config_register_num (mp->priv->xine,
				"video.num_buffers", 5, 0, NULL, 10,
				NULL, NULL);
		xine_config_lookup_entry (mp->priv->xine,
				"video.num_buffers", &entry);
	}
	entry.num_value = 5;
	xine_config_update_entry (mp->priv->xine, &entry);
#else
	mp->priv->video_driver = NULL;
#endif

	mp->priv->stream = xine_stream_new (mp->priv->xine,
				            mp->priv->audio_driver,
				            mp->priv->video_driver);
	mp->priv->event_queue = xine_event_new_queue (mp->priv->stream);
	mp->priv->queue = g_async_queue_new ();

	xine_event_create_listener_thread (mp->priv->event_queue,
					   (xine_event_listener_cb_t) xine_event, mp);

	xine_config_register_range (mp->priv->xine,
				    "misc.amp_level",
				    50, 0, 100, "amp volume level",
				    NULL, 10, NULL, NULL);
	mp->priv->volume = -1;

	mp->priv->timer = g_timer_new ();
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = 0;
}

MonkeyMediaPlayer *
monkey_media_player_new (GError **error)
{
	MonkeyMediaPlayer *mp;

	mp = MONKEY_MEDIA_PLAYER (g_object_new (MONKEY_MEDIA_TYPE_PLAYER, NULL));

	monkey_media_player_construct (mp, error);

	if (*error != NULL) {
		g_object_unref (G_OBJECT (mp));
		mp = NULL;
	}

	return mp;
}

GQuark
monkey_media_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("monkey_media_player_error");

	return quark;
}

void
monkey_media_player_open (MonkeyMediaPlayer *mp,
			  const char *uri,
			  GError **error)
{
	int xine_error;
	char *unesc;

	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	monkey_media_player_close (mp);

	if (uri == NULL)
		return;

	if (!xine_open (mp->priv->stream, uri))
		xine_error = xine_get_error (mp->priv->stream);
	else
		xine_error = XINE_ERROR_NONE;

	if (xine_error != XINE_ERROR_NONE) {
		switch (xine_error)
		{
		case XINE_ERROR_NO_INPUT_PLUGIN:
			unesc = gnome_vfs_unescape_string_for_display (uri);
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_NO_INPUT_PLUGIN,
				     _("No input plugin available for %s; check your installation."),
				     unesc);
			g_free (unesc);
			break;
		case XINE_ERROR_NO_DEMUX_PLUGIN:
			unesc = gnome_vfs_unescape_string_for_display (uri);
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_NO_DEMUX_PLUGIN,
				     _("No demux plugin available for %s; check your installation."),
				     unesc);
			g_free (unesc);
			break;
		case XINE_ERROR_DEMUX_FAILED:
			unesc = gnome_vfs_unescape_string_for_display (uri);
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_DEMUX_FAILED,
				     _("Demuxing for %s failed; check your installation."),
				     unesc);
			g_free (unesc);
			break;
		default:
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_INTERNAL,
				     _("Internal error; check your installation."));
			break;
		}
	} else if (xine_get_stream_info (mp->priv->stream, XINE_STREAM_INFO_AUDIO_HANDLED) == FALSE) {
		unesc = gnome_vfs_unescape_string_for_display (uri);
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_AUDIO,
			     _("Audio of %s not handled; check your installation."),
			     unesc);
		g_free (unesc);
	} else {
		g_timer_stop (mp->priv->timer);
		g_timer_reset (mp->priv->timer);
		mp->priv->timer_add = 0;
	}

	mp->priv->uri = g_strdup (uri);
}

void
monkey_media_player_close (MonkeyMediaPlayer *mp)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->stream != NULL) {
		xine_stop (mp->priv->stream);
		xine_close (mp->priv->stream);
	}

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;
}

const char *
monkey_media_player_get_uri (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), NULL);

	return mp->priv->uri;
}

void
monkey_media_player_play (MonkeyMediaPlayer *mp)
{
	int speed, status;

	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->stream == NULL)
		return;

	speed = xine_get_param (mp->priv->stream, XINE_PARAM_SPEED);
	status = xine_get_status (mp->priv->stream);

	if (speed != XINE_SPEED_NORMAL && status == XINE_STATUS_PLAY)
		xine_set_param (mp->priv->stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);
	else
		xine_play (mp->priv->stream, 0, 0);

	g_timer_start (mp->priv->timer);
}

void
monkey_media_player_pause (MonkeyMediaPlayer *mp)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->stream != NULL) {
		xine_set_param (mp->priv->stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);

#ifdef HAVE_XINE_CLOSE
		/* Close the audio device when on pause */
		xine_set_param (mp->priv->stream, XINE_PARAM_AUDIO_CLOSE_DEVICE, 1);
#endif
	}

	mp->priv->timer_add += floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5);
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);
}

gboolean
monkey_media_player_playing (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), FALSE);

	if (mp->priv->stream == NULL)
		return FALSE;

	return (xine_get_status (mp->priv->stream) == XINE_STATUS_PLAY && xine_get_param (mp->priv->stream, XINE_PARAM_SPEED) == XINE_SPEED_NORMAL);
}

static gboolean
can_set_volume (MonkeyMediaPlayer *mp)
{
	if (mp->priv->audio_driver == NULL)
		return FALSE;
	if (xine_get_param (mp->priv->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL) == -2)
		return FALSE;

	return TRUE;
}

void
monkey_media_player_set_volume (MonkeyMediaPlayer *mp,
				float volume)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	if (mp->priv->stream != NULL) {
		if (can_set_volume (mp) == FALSE)
			return;

		if (!mp->priv->mute) {
			xine_set_param (mp->priv->stream, XINE_PARAM_AUDIO_AMP_LEVEL,
					CLAMP (volume * 100, 0, 100));
		}
	}

	mp->priv->volume = volume;
}

float
monkey_media_player_get_volume (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), 0.0);

	return mp->priv->volume;
}

void
monkey_media_player_set_mute (MonkeyMediaPlayer *mp,
			      gboolean mute)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->stream != NULL) {
		if (can_set_volume (mp) == FALSE)
			return;

		if (mute)
			xine_set_param (mp->priv->stream, XINE_PARAM_AUDIO_VOLUME, 0);
		else
			xine_set_param (mp->priv->stream, XINE_PARAM_AUDIO_VOLUME,
					CLAMP (mp->priv->volume * 100, 0, 100));
	}

	mp->priv->mute = mute;
}

gboolean
monkey_media_player_get_mute (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), FALSE);

	return mp->priv->mute;
}

gboolean
monkey_media_player_seekable (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), FALSE);

	if (mp->priv->stream != NULL) {
		return xine_get_stream_info (mp->priv->stream, XINE_STREAM_INFO_SEEKABLE);
	} else {
		return FALSE;
	}
}

void
monkey_media_player_set_time (MonkeyMediaPlayer *mp,
			      long time)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));
	g_return_if_fail (time >= 0);

	if (mp->priv->stream != NULL) {
		xine_play (mp->priv->stream, 0, time * 1000);

		if (monkey_media_player_playing (mp))
			xine_set_param (mp->priv->stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);
		else
			xine_set_param (mp->priv->stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);

		g_timer_reset (mp->priv->timer);
		mp->priv->timer_add = time;
	}
}

long
monkey_media_player_get_time (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), -1);

	if (mp->priv->stream != NULL)
		return (long) floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5) + mp->priv->timer_add;
	else
		return -1;
}
