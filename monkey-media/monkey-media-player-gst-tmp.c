/*  monkey-media
 *
 *  arch-tag: Implementation of GStreamer backends, with workarounds for bugs
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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
#include <gst/gst.h>
#include <gst/gstqueue.h>
#include <gst/gconf/gconf.h>
#include <gst/control/control.h>
#include <gst/control/dparam_smooth.h>
#include <math.h>
#include <string.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "monkey-media.h"
#include "monkey-media-marshal.h"
#include "monkey-media-private.h"
#include "monkey-media-audio-cd.h"
#include "monkey-media-audio-cd-private.h"

static void monkey_media_player_class_init (MonkeyMediaPlayerClass *klass);
static void monkey_media_player_init (MonkeyMediaPlayer *mp);
static void monkey_media_player_finalize (GObject *object);

struct MonkeyMediaPlayerPrivate
{
	char *uri;

	GstElement *pipeline;

	GstElement *srcthread;
	GstElement *queue;
	GstElement *waiting_bin;
	GstElement *src;

	GstElement *decoder;
	GstElement *volume;
	GstElement *sink;

	MonkeyMediaAudioCD *cd;

	gboolean iradio_mode;
	gboolean playing;

	guint error_signal_id;

	GstDParam *volume_dparam;
	float cur_volume;
	gboolean mute;

	GTimer *timer;
	long timer_add;

	guint tick_timeout_id;
};

typedef enum
{
	EOS,
	INFO,
	BUFFERING_BEGIN,
	BUFFERING_END,
	ERROR,
	TICK,
	LAST_SIGNAL
} MonkeyMediaPlayerSignalType;

typedef struct
{
	MonkeyMediaPlayer *object;
	MonkeyMediaStreamInfoField info_field;
	GError *error;
	GValue *info;
} MonkeyMediaPlayerSignal;

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
	if (mp->priv->playing == FALSE)
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

#ifdef HAVE_AUDIOCD
	mp->priv->cd = monkey_media_audio_cd_new (NULL);
#endif	

	mp->priv->tick_timeout_id = g_timeout_add (ms_period, (GSourceFunc) tick_timeout, mp);
}

static void
monkey_media_player_finalize (GObject *object)
{
	MonkeyMediaPlayer *mp;

	mp = MONKEY_MEDIA_PLAYER (object);

	g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->pipeline) {
		g_signal_handler_disconnect (G_OBJECT (mp->priv->pipeline),
					     mp->priv->error_signal_id);
		
		gst_element_set_state (mp->priv->pipeline,
				       GST_STATE_NULL);
		
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
	}

	if (mp->priv->timer)
		g_timer_destroy (mp->priv->timer);
	
	g_free (mp->priv->uri);

	g_free (mp->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
eos_signal_idle (MonkeyMediaPlayer *mp)
{
	g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[EOS], 0);

	g_object_unref (G_OBJECT (mp));

	return FALSE;
}

static gboolean
buffering_begin_signal_idle (MonkeyMediaPlayer *mp)
{
	g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[BUFFERING_BEGIN], 0);

	g_object_unref (G_OBJECT (mp));

	return FALSE;
}

static gboolean
buffering_end_signal_idle (MonkeyMediaPlayer *mp)
{
	g_signal_emit (G_OBJECT (mp), monkey_media_player_signals[BUFFERING_END], 0);

	g_object_unref (G_OBJECT (mp));

	return FALSE;
}


static void
eos_cb (GstElement *element,
	MonkeyMediaPlayer *mp)
{
	g_object_ref (G_OBJECT (mp));

	gst_element_set_state (mp->priv->sink, GST_STATE_NULL);

	g_idle_add ((GSourceFunc) eos_signal_idle, mp);
}

static gboolean
error_signal_idle (MonkeyMediaPlayerSignal *signal)
{
	g_signal_emit (G_OBJECT (signal->object),
		       monkey_media_player_signals[ERROR], 0,
		       signal->error);

	/* close if not already closing */
	if (signal->object->priv->uri != NULL)
		monkey_media_player_close (signal->object);

	g_object_unref (G_OBJECT (signal->object));
	g_error_free (signal->error);
	g_free (signal);

	return FALSE;
}

static void
error_cb (GstElement *element,
	  GObject *arg1,
	  char *errmsg,
	  MonkeyMediaPlayer *mp)
{
	MonkeyMediaPlayerSignal *signal;

	signal = g_new0 (MonkeyMediaPlayerSignal, 1);
	signal->object = mp;
	signal->error = g_error_new_literal (MONKEY_MEDIA_PLAYER_ERROR,
					     MONKEY_MEDIA_PLAYER_ERROR_GENERAL,
					     errmsg);

	g_object_ref (G_OBJECT (mp));

	g_idle_add ((GSourceFunc) error_signal_idle, signal);
}

static gboolean
info_signal_idle (MonkeyMediaPlayerSignal *signal)
{
	g_signal_emit (G_OBJECT (signal->object),
		       monkey_media_player_signals[INFO], 0,
		       signal->info_field, signal->info);

	g_object_unref (G_OBJECT (signal->object));
	g_free (signal->info);
	g_free (signal);

	return FALSE;
}

#if GST_VERSION_MAJOR == 0 && GST_VERSION_MINOR == 6
static char *
unicodify (const char *str, int len, ...)
{
	char *ret = NULL, *cset;
	va_list args;
	int bytes_read, bytes_written;

	if (g_utf8_validate (str, len, NULL))
		return g_strndup (str, len >= 0 ? len : strlen (str));

	va_start (args, len);
	while ((cset = va_arg (args, char *)) != NULL)
	{
		if (!strcmp (cset, "locale"))
			ret = g_locale_to_utf8 (str, len, &bytes_read,
						&bytes_written, NULL);
		else
			ret = g_convert (str, len, "UTF-8", cset,
					 &bytes_read, &bytes_written, NULL);
		if (ret)
			break;
	}
	va_end (args);

	return ret;
}

static char *
monkey_media_unicodify (const char *str)
{
	return unicodify (str, -1, "locale", "ISO-8859-1", NULL);
}
#endif

static void
deep_notify_cb (GstElement *element, GstElement *orig,
	        GParamSpec *pspec, MonkeyMediaPlayer *player)
{
	GValue *value;
	GEnumClass *class;
	GEnumValue *ev = NULL;

	if (!(pspec->flags & G_PARAM_READABLE)) return;

	class = g_type_class_ref (MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD);

	value = g_new0 (GValue, 1);
	g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));
	g_object_get_property (G_OBJECT (orig), pspec->name, value);

	/* Other properties from the gnomevfssrc go here */
	if (strcmp (pspec->name, "iradio-title") == 0)
		ev = g_enum_get_value (class, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE);
	if (ev == NULL && strcmp (pspec->name, "iradio-url") == 0)
		ev = g_enum_get_value (class, MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION);
	if (ev == NULL)
		ev = g_enum_get_value_by_nick (class, pspec->name);

	/* FIXME begin hack */
	if (ev == NULL) {
		char *tmp = g_strconcat ("audio_", pspec->name, NULL);
		ev = g_enum_get_value_by_nick (class, tmp);

		g_free (tmp);
	}
	/* FIXME end hack */

#if GST_VERSION_MAJOR == 0 && GST_VERSION_MINOR == 6
	if (G_VALUE_TYPE (value) == G_TYPE_STRING
		&& g_value_get_string (value) != NULL)
	{
		char *u8value = monkey_media_unicodify (g_value_get_string (value));
		if (u8value)
			g_value_set_string_take_ownership (value, u8value);
		else
			ev = NULL;
	}
#endif	

	if (ev != NULL) {
		MonkeyMediaPlayerSignal *signal;

		signal = g_new0 (MonkeyMediaPlayerSignal, 1);

		signal->info_field = ev->value;
		signal->info = value;
		signal->object = player;

		g_object_ref (G_OBJECT (player));

		g_idle_add ((GSourceFunc) info_signal_idle, signal);
	}

	g_type_class_unref (class);
}

#if GST_VERSION_MAJOR == 0 && GST_VERSION_MINOR == 6
static void
queue_full_cb (GstQueue *queue,
	       int level,
	       gpointer data)
#else
static void
queue_full_cb (GstQueue *queue,
	       gpointer data)     
#endif
{
	MonkeyMediaPlayer *mp = MONKEY_MEDIA_PLAYER (data);

	g_signal_handlers_block_by_func (G_OBJECT (mp->priv->queue),
					 G_CALLBACK (queue_full_cb),
					 mp);
	gst_element_set_state (mp->priv->waiting_bin, GST_STATE_PLAYING);
	g_object_ref (G_OBJECT (mp));
	g_idle_add ((GSourceFunc) buffering_end_signal_idle, mp);
}

static void
monkey_media_player_construct (MonkeyMediaPlayer *mp,
			       gboolean iradio_mode,
			       gboolean audiocd_mode,
			       const char *uri,
			       GError **error)
{
	GstDParamManager *dpman;
	char *decoder_name = NULL;

	/* The main playback pipeline for iradio, at the end this looks like:
	 * { { src ! queue } ! { mad ! volume ! sink } }
	 *
	 * For local files, it just looks like:
	 *  { src ! spider ! volume ! sink }
	 */
	mp->priv->pipeline = gst_element_factory_make ("thread", "pipeline");
	g_signal_connect (G_OBJECT (mp->priv->pipeline),
			  "deep_notify",
			  G_CALLBACK (deep_notify_cb),
			  mp);

	mp->priv->error_signal_id =
		g_signal_connect (G_OBJECT (mp->priv->pipeline),
				  "error",
				  G_CALLBACK (error_cb),
				  mp);

	/* Construct the two threads */
	if (iradio_mode) {
		mp->priv->waiting_bin = gst_element_factory_make ("thread", "waiting_bin");
		mp->priv->srcthread = gst_element_factory_make ("thread", "srcthread");
		gst_bin_add_many (GST_BIN (mp->priv->pipeline),
				  mp->priv->srcthread, mp->priv->waiting_bin, NULL);
	} else {
		mp->priv->waiting_bin = mp->priv->pipeline;
		mp->priv->srcthread = mp->priv->pipeline;
	}

	/* Construct elements */

	/* The source */
	mp->priv->src = audiocd_mode ? gst_element_factory_make ("cdparanoia", "src")
		: gst_element_factory_make ("gnomevfssrc", "src");
	if (mp->priv->src == NULL) {
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_INPUT_PLUGIN,
			     _("Failed to create %s input element; check your installation"),
			     audiocd_mode ? "cdparanoia" : "gnomevfssrc");
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
		return;
	}
	gst_bin_add (GST_BIN (mp->priv->srcthread), mp->priv->src);

	if (audiocd_mode) {
		g_object_set (G_OBJECT (mp->priv->src),
			      "paranoia-mode",
			      monkey_media_get_cd_playback_mode (),
			      NULL);
		g_object_set (G_OBJECT (mp->priv->src),
			      "location",
			      monkey_media_get_cd_drive (),
			      NULL);
	}

	/* The queue */
	if (iradio_mode) {
		mp->priv->queue = gst_element_factory_make ("queue", "queue");
		if (mp->priv->queue == NULL) {
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_NO_QUEUE_PLUGIN,
				     _("Failed to create queue element; check your installation"));
			gst_object_unref (GST_OBJECT (mp->priv->pipeline));
			mp->priv->pipeline = NULL;
			return;
		}
#if GST_VERSION_MAJOR == 0 && GST_VERSION_MINOR == 6
		g_signal_connect (G_OBJECT (mp->priv->queue), "high_watermark",
				  G_CALLBACK (queue_full_cb), mp);
#else
		g_signal_connect (G_OBJECT (mp->priv->queue), "overrun",
				  G_CALLBACK (queue_full_cb), mp);
#endif
		gst_bin_add (GST_BIN (mp->priv->srcthread), mp->priv->queue);
	}

	/* The decoding element */
	if (iradio_mode)
#ifdef HAVE_MP3
		decoder_name = "mad";
#else
		decoder_name = "vorbisfile";
#endif
#if GST_VERSION_MAJOR == 0 && GST_VERSION_MINOR == 6
	else if (g_str_has_suffix (uri, ".mp3"))
		decoder_name = "mad";
	else if (g_str_has_suffix (uri, ".ogg"))
		decoder_name = "vorbisfile";
	else if (g_str_has_suffix (uri, ".flac"))
		decoder_name = "flacdec";
#endif
	else
		decoder_name = "spider";

	mp->priv->decoder = gst_element_factory_make (decoder_name, "autoplugger");
	if (mp->priv->decoder == NULL) {
		char *err = g_strdup_printf (_("Failed to create %s element; check your installation"),
					     decoder_name);
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_DEMUX_PLUGIN,
			     err);
		g_free (err);
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
		return;
	}
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->decoder);

	/* Volume */
	mp->priv->volume = gst_element_factory_make ("volume", "volume");
	if (mp->priv->volume == NULL) {
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_VOLUME_PLUGIN,
			     _("Failed to create volume element; check your installation"));
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
		return;
	}
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->volume);

	dpman = gst_dpman_get_manager (mp->priv->volume);
	gst_dpman_set_mode (dpman, "synchronous");
	mp->priv->volume_dparam = gst_dpsmooth_new (G_TYPE_FLOAT);
	g_assert (mp->priv->volume_dparam != NULL);
	gst_dpman_attach_dparam (dpman, "volume", mp->priv->volume_dparam);

	/* Output sink */
	mp->priv->sink = gst_gconf_get_default_audio_sink ();
	if (mp->priv->sink == NULL) {
		g_set_error (error,
			     MONKEY_MEDIA_PLAYER_ERROR,
			     MONKEY_MEDIA_PLAYER_ERROR_NO_AUDIO,
			     _("Could not create audio output element; check your settings"));
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
		return;
	}
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->sink);

	gst_element_link_many (mp->priv->decoder, mp->priv->volume, mp->priv->sink, NULL);
	if (iradio_mode)
		gst_element_link_many (mp->priv->src, mp->priv->queue, mp->priv->decoder, NULL);
	else
		gst_element_link_many (mp->priv->src, mp->priv->decoder, NULL);

	g_signal_connect (G_OBJECT (mp->priv->sink), "eos",
			  G_CALLBACK (eos_cb), mp);

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	g_object_set (G_OBJECT (mp->priv->volume_dparam),
		      "value_float", mp->priv->cur_volume,
		      NULL);
	g_object_set (G_OBJECT (mp->priv->volume),
		      "mute", mp->priv->mute,
		      NULL);

	if (mp->priv->timer)
		g_timer_destroy (mp->priv->timer);
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
monkey_media_player_sync_pipeline (MonkeyMediaPlayer *mp, gboolean iradio_mode)
{
	if (mp->priv->playing) {
		
		if (iradio_mode) {
			g_object_ref (G_OBJECT (mp));
			g_idle_add ((GSourceFunc) buffering_begin_signal_idle, mp);
			gst_element_set_state (mp->priv->srcthread,
					       GST_STATE_PLAYING);
		} else {
			gst_element_set_state (mp->priv->pipeline,
					       GST_STATE_PLAYING);
		}
		g_timer_start (mp->priv->timer);
	} else {
		gst_element_set_state (mp->priv->pipeline,
				       GST_STATE_PAUSED);
		gst_element_set_state (mp->priv->sink,
				       GST_STATE_NULL);
	}
}

void
monkey_media_player_open (MonkeyMediaPlayer *mp,
			  const char *uri,
			  GError **error)
{
	gboolean audiocd_mode = uri && g_str_has_prefix (uri, "audiocd://");
	gboolean iradio_mode = uri && g_str_has_prefix (uri, "http://");

	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->pipeline) {
		gst_element_set_state (mp->priv->pipeline,
				       GST_STATE_NULL);
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
	}

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (uri == NULL) {
		mp->priv->playing = FALSE;
		return;
	}

	monkey_media_player_construct (mp, iradio_mode, audiocd_mode, uri, error);
	if (error && *error)
		return;

#ifdef HAVE_AUDIOCD
	if (audiocd_mode)
	{
		GstEvent *event;
		int tracknum;

		if (!mp->priv->cd) {
			g_set_error (error,
				     MONKEY_MEDIA_PLAYER_ERROR,
				     MONKEY_MEDIA_PLAYER_ERROR_INTERNAL,
				     _("No AudioCD support; check your settings"));
			return;
		}

		tracknum = atoi (uri + 10);

		if (!monkey_media_audio_cd_have_track (mp->priv->cd, tracknum, error)) {
			return;
		}

		gst_element_set_state (mp->priv->pipeline, GST_STATE_PAUSED);

		/* 7 is track format */
		event = gst_event_new_seek (7 |
					    GST_SEEK_METHOD_SET |
					    GST_SEEK_FLAG_FLUSH, tracknum);
		gst_element_send_event (mp->priv->sink, event);
	} else
#endif	  
	{

		/* Internet radio support */
		g_object_set (G_OBJECT (mp->priv->src),
			      "iradio-mode", iradio_mode, NULL);
		
		g_object_set (G_OBJECT (mp->priv->src),
			      "location", uri, NULL);
	}
	mp->priv->iradio_mode = iradio_mode;
	
	mp->priv->uri = g_strdup (uri);

	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = 0;

	monkey_media_player_sync_pipeline (mp, iradio_mode);
}

void
monkey_media_player_close (MonkeyMediaPlayer *mp)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	mp->priv->playing = FALSE;

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (mp->priv->pipeline == NULL)
		return;

	gst_element_set_state (mp->priv->pipeline,
			       GST_STATE_NULL);

	gst_object_unref (GST_OBJECT (mp->priv->pipeline));
	mp->priv->pipeline = NULL;
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
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	mp->priv->playing = TRUE;

	g_return_if_fail (mp->priv->pipeline != NULL);

	monkey_media_player_sync_pipeline (mp, mp->priv->iradio_mode);
}

void
monkey_media_player_pause (MonkeyMediaPlayer *mp)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->pipeline != NULL);

	mp->priv->timer_add += floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5);
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);

	gst_element_set_state (mp->priv->pipeline,
			       GST_STATE_PAUSED);
	gst_element_set_state (mp->priv->sink,
			       GST_STATE_NULL);
}

gboolean
monkey_media_player_playing (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), FALSE);

	return mp->priv->playing;
}

void
monkey_media_player_set_volume (MonkeyMediaPlayer *mp,
				float volume)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	if (mp->priv->pipeline != NULL) {
		g_object_set (G_OBJECT (mp->priv->volume_dparam),
			      "value_float",
			      volume,
			      NULL);
	}

	mp->priv->cur_volume = volume;
}

float
monkey_media_player_get_volume (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), 0.0);

	return mp->priv->cur_volume;
}

void
monkey_media_player_set_mute (MonkeyMediaPlayer *mp,
			      gboolean mute)
{
	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));

	if (mp->priv->pipeline != NULL)
		g_object_set (G_OBJECT (mp->priv->volume),
			      "mute",
			      mute,
			      NULL);
	else

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
	g_return_val_if_fail (mp->priv->pipeline != NULL, FALSE);

	/* FIXME we're lying here, no idea how to fix this though, without trying
	 * a seek which might disrupt playback */
	return TRUE;
}

void
monkey_media_player_set_time (MonkeyMediaPlayer *mp,
			      long time)
{
	GstEvent *event;

	g_return_if_fail (MONKEY_MEDIA_IS_PLAYER (mp));
	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->pipeline != NULL);

	gst_element_set_state (mp->priv->pipeline, GST_STATE_PAUSED);

	event = gst_event_new_seek (GST_FORMAT_TIME |
				    GST_SEEK_METHOD_SET |
				    GST_SEEK_FLAG_FLUSH, time * GST_SECOND);
	gst_element_send_event (mp->priv->sink, event);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->pipeline, GST_STATE_PLAYING);

	g_timer_reset (mp->priv->timer);
	mp->priv->timer_add = time;
}

long
monkey_media_player_get_time (MonkeyMediaPlayer *mp)
{
	g_return_val_if_fail (MONKEY_MEDIA_IS_PLAYER (mp), -1);

	if (mp->priv->pipeline != NULL)
		return (long) floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5) + mp->priv->timer_add;
	else
		return -1;
}
