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
#include <gst/gstqueue.h>
#include <gst/gconf/gconf.h>
#include <gst/control/control.h>
#include <gst/control/dparam_smooth.h>
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

static void rb_player_class_init (RBPlayerClass *klass);
static void rb_player_init (RBPlayer *mp);
static void rb_player_finalize (GObject *object);

struct RBPlayerPrivate
{
	char *uri;

	GstElement *pipeline;

	GstElement *srcthread;
	GstElement *queue;
	GstElement *typefind;
	GstElement *waiting_bin;
	GstElement *src;

	GstElement *decoder;
	GstElement *volume;
	GstElement *audioconvert;
	GstElement *audioscale;
	GstElement *sink;

	GError *error;

#ifdef HAVE_AUDIOCD
	MonkeyMediaAudioCD *cd;
#endif

	gboolean iradio_mode;
	gboolean playing;

	guint error_signal_id;

	GstDParamManager *volume_dpmanager;
	GstDParam *volume_dparam;
	float cur_volume;

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
	BUFFERING_PROGRESS,
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

static GObjectClass *parent_class = NULL;

static gboolean rb_player_sync_pipeline (RBPlayer *mp, gboolean iradio_mode, GError **error);
static void rb_player_gst_free_pipeline (RBPlayer *player);

GType
rb_player_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo our_info =
		{
			sizeof (RBPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_player_class_init,
			NULL,
			NULL,
			sizeof (RBPlayer),
			0,
			(GInstanceInitFunc) rb_player_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "RBPlayer",
					       &our_info, 0);
	}

	return type;
}

static void
rb_player_class_init (RBPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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
	rb_player_signals[BUFFERING_BEGIN] =
		g_signal_new ("buffering_begin",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, buffering_begin),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_player_signals[BUFFERING_END] =
		g_signal_new ("buffering_end",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, buffering_end),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_player_signals[BUFFERING_PROGRESS] =
		g_signal_new ("buffering_progress",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, buffering_progress),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
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

#ifdef HAVE_AUDIOCD
	mp->priv->cd = monkey_media_audio_cd_new (NULL);
#endif	

	mp->priv->tick_timeout_id = g_timeout_add (ms_period, (GSourceFunc) tick_timeout, mp);
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *mp;

	mp = RB_PLAYER (object);

	g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->pipeline) {
		g_signal_handler_disconnect (G_OBJECT (mp->priv->pipeline),
					     mp->priv->error_signal_id);
		
		gst_element_set_state (mp->priv->pipeline,
				       GST_STATE_NULL);
		
		rb_player_gst_free_pipeline (mp);
	}

	if (mp->priv->timer)
		g_timer_destroy (mp->priv->timer);
	
	g_free (mp->priv->uri);

	g_free (mp->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_player_gst_free_pipeline (RBPlayer *player)
{
	if (player->priv->pipeline == NULL)
		return;
	
	if (player->priv->volume_dpmanager) {
		gst_dpman_detach_dparam (player->priv->volume_dpmanager, "volume");
		gst_object_unref (GST_OBJECT (player->priv->volume_dparam));
		player->priv->volume_dpmanager = NULL;
	}
	gst_object_unref (GST_OBJECT (player->priv->pipeline));
	player->priv->pipeline = NULL;
}

static gboolean
eos_signal_idle (RBPlayer *mp)
{
	g_signal_emit (G_OBJECT (mp), rb_player_signals[EOS], 0);

	g_object_unref (G_OBJECT (mp));

	return FALSE;
}

static gboolean
buffering_begin_signal_idle (RBPlayer *mp)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (mp), rb_player_signals[BUFFERING_BEGIN], 0);
	g_object_unref (G_OBJECT (mp));

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static gboolean
buffering_end_signal_idle (RBPlayer *mp)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (mp), rb_player_signals[BUFFERING_END], 0);
	g_object_unref (G_OBJECT (mp));

	GDK_THREADS_LEAVE ();

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
rb_player_gst_signal_error (RBPlayer *mp, const char *msg)
{
	RBPlayerSignal *signal;

	signal = g_new0 (RBPlayerSignal, 1);
	signal->object = mp;
	signal->error = g_error_new_literal (RB_PLAYER_ERROR,
					     RB_PLAYER_ERROR_GENERAL,
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
	rb_player_gst_signal_error (mp, error->message);
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
queue_full_cb (GstQueue *queue,
	       gpointer data)     
{
	RBPlayer *mp = RB_PLAYER (data);

	rb_debug ("caught queue full");
	g_signal_handlers_block_by_func (G_OBJECT (mp->priv->queue),
					 G_CALLBACK (queue_full_cb),
					 mp);
	if (gst_element_set_state (mp->priv->waiting_bin, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
		rb_player_gst_signal_error (mp, _("Could not start pipeline playing"));
	} else {
		g_object_ref (G_OBJECT (mp));
		g_idle_add ((GSourceFunc) buffering_end_signal_idle, mp);
	}
}

static void
rb_player_construct (RBPlayer *mp,
		     gboolean iradio_mode,
		     gboolean audiocd_mode,
		     const char *uri,
		     GError **error)
{
	GstDParamManager *dpman;
	char *element_name = NULL;

#define MAKE_ELEMENT_OR_LOSE(NAME, NICE) G_STMT_START {	\
	element_name = #NAME ;\
	rb_debug ("constructing element \"" #NICE "\""); \
	mp->priv->NICE = gst_element_factory_make (#NAME, #NICE); \
	if (!mp->priv->NICE) \
		goto missing_element; \
	} G_STMT_END

	/* The main playback pipeline for iradio, at the end this looks like:
	 * { { src ! queue } ! { mad ! volume ! sink } }
	 *
	 * For local files, it just looks like:
	 *  { src ! spider ! volume ! sink }
	 */
	MAKE_ELEMENT_OR_LOSE(thread, pipeline);
	g_signal_connect_object (G_OBJECT (mp->priv->pipeline),
				 "deep_notify",
				 G_CALLBACK (deep_notify_cb),
				 mp, 0);

	mp->priv->error_signal_id =
		g_signal_connect_object (G_OBJECT (mp->priv->pipeline),
					 "error",
					 G_CALLBACK (error_cb),
					 mp, 0);

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

	if (audiocd_mode)
		element_name = "cdparanoia";
	else
		element_name = "gnomevfssrc";
	mp->priv->src = gst_element_factory_make (element_name, "src");	
	if (mp->priv->src == NULL)
		goto missing_element;
	gst_bin_add (GST_BIN (mp->priv->srcthread), mp->priv->src);

#ifdef HAVE_AUDIOCD
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
#endif

	rb_debug ("constructing queue");
	/* The queue */
	if (iradio_mode) {
		element_name = "queue";
		mp->priv->queue = gst_element_factory_make (element_name, element_name);
		if (mp->priv->queue == NULL)
			goto missing_element;
		g_object_set (G_OBJECT (mp->priv->queue), "max-size-bytes", 64 * 1024, NULL);
		g_signal_connect_object (G_OBJECT (mp->priv->queue), "overrun",
					 G_CALLBACK (queue_full_cb), mp, 0);
		gst_bin_add (GST_BIN (mp->priv->srcthread), mp->priv->queue);
	}

	MAKE_ELEMENT_OR_LOSE(typefind, typefind);
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->typefind);

	MAKE_ELEMENT_OR_LOSE(spider, decoder);
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->decoder);

	MAKE_ELEMENT_OR_LOSE(volume, volume);
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->volume);
	dpman = gst_dpman_get_manager (mp->priv->volume);
	mp->priv->volume_dpmanager = dpman;
	gst_dpman_set_mode (dpman, "synchronous");
	mp->priv->volume_dparam = gst_dpsmooth_new (G_TYPE_DOUBLE);
	g_assert (mp->priv->volume_dparam != NULL);
	gst_dpman_attach_dparam (dpman, "volume", mp->priv->volume_dparam);

	MAKE_ELEMENT_OR_LOSE(audioconvert, audioconvert);
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->audioconvert);

	MAKE_ELEMENT_OR_LOSE(audioscale, audioscale);
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->audioscale);

	/* Output sink */
	mp->priv->sink = gst_gconf_get_default_audio_sink ();
	if (mp->priv->sink == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_NO_AUDIO,
			     _("Could not create audio output element; check your settings"));
		gst_object_unref (GST_OBJECT (mp->priv->pipeline));
		mp->priv->pipeline = NULL;
		return;
	}
	gst_bin_add (GST_BIN (mp->priv->waiting_bin), mp->priv->sink);

	gst_element_link_many (mp->priv->decoder, mp->priv->volume, mp->priv->audioconvert, mp->priv->audioscale, mp->priv->sink, NULL);
	if (iradio_mode)
		gst_element_link_many (mp->priv->src, mp->priv->queue, mp->priv->typefind, mp->priv->decoder, NULL);
	else
		gst_element_link_many (mp->priv->src, mp->priv->typefind, mp->priv->decoder, NULL);

	g_signal_connect_object (G_OBJECT (mp->priv->sink), "eos",
				 G_CALLBACK (eos_cb), mp, 0);

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	g_object_set (G_OBJECT (mp->priv->volume_dparam),
		      "value_double", mp->priv->cur_volume,
		      NULL);

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
		rb_player_gst_free_pipeline (mp);
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

	mp = RB_PLAYER (g_object_new (RB_TYPE_PLAYER, NULL));

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
rb_player_sync_pipeline (RBPlayer *mp, gboolean iradio_mode, GError **error)
{
	rb_debug ("syncing pipeline");
	if (mp->priv->playing) {
		if (iradio_mode) {
			rb_debug ("beginning buffering");
			g_object_ref (G_OBJECT (mp));
			g_idle_add ((GSourceFunc) buffering_begin_signal_idle, mp);
			if (gst_element_set_state (mp->priv->srcthread,
						   GST_STATE_PLAYING) == GST_STATE_FAILURE) {
				g_set_error (error,
					     RB_PLAYER_ERROR,
					     RB_PLAYER_ERROR_GENERAL,
					     _("Could not start pipeline playing"));
				return FALSE;
			}
		} else {
			rb_debug ("PLAYING pipeline");
			if (gst_element_set_state (mp->priv->pipeline,
						   GST_STATE_PLAYING) == GST_STATE_FAILURE) {
				g_set_error (error,
					     RB_PLAYER_ERROR,
					     RB_PLAYER_ERROR_GENERAL,
					     _("Could not start pipeline playing"));
				return FALSE;
			}				
		}
		g_timer_start (mp->priv->timer);
	} else {		
		rb_debug ("PAUSING pipeline");
		if (gst_element_set_state (mp->priv->pipeline,
					   GST_STATE_PAUSED) == GST_STATE_FAILURE) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_GENERAL,
				     _("Could not pause playback"));
			return FALSE;
		}
			
		rb_debug ("setting sink to NULL");
		if (gst_element_set_state (mp->priv->sink, GST_STATE_NULL) != GST_STATE_SUCCESS) {
		  g_set_error (error,
			       RB_PLAYER_ERROR,
			       RB_PLAYER_ERROR_GENERAL,
			       _("Could not close output sink"));
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
	gboolean audiocd_mode = uri && g_str_has_prefix (uri, "audiocd://");
	gboolean iradio_mode = uri && g_str_has_prefix (uri, "http://");

	g_return_if_fail (RB_IS_PLAYER (mp));

	if (mp->priv->pipeline) {
		gst_element_set_state (mp->priv->pipeline,
				       GST_STATE_NULL);
		rb_player_gst_free_pipeline (mp);
	}

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (uri == NULL) {
		mp->priv->playing = FALSE;
		return;
	}

	rb_player_construct (mp, iradio_mode, audiocd_mode, uri, error);
	if (error && *error)
		return;

#ifdef HAVE_AUDIOCD
	if (audiocd_mode)
	{
		GstEvent *event;
		int tracknum;

		if (!mp->priv->cd) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_INTERNAL,
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

	if (!rb_player_sync_pipeline (mp, iradio_mode, error)) {
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

	if (mp->priv->pipeline == NULL)
		return;

	if (gst_element_set_state (mp->priv->pipeline,
				   GST_STATE_NULL) != GST_STATE_SUCCESS) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to close audio output sink"));
	}

	gst_object_unref (GST_OBJECT (mp->priv->pipeline));
	mp->priv->pipeline = NULL;
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

	g_return_if_fail (mp->priv->pipeline != NULL);

	rb_player_sync_pipeline (mp, mp->priv->iradio_mode, error);
}

void
rb_player_pause (RBPlayer *mp)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->pipeline != NULL);

	mp->priv->timer_add += floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5);
	g_timer_stop (mp->priv->timer);
	g_timer_reset (mp->priv->timer);

	rb_player_sync_pipeline (mp, mp->priv->iradio_mode, NULL);
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

	if (mp->priv->pipeline != NULL) {
		g_object_set (G_OBJECT (mp->priv->volume_dparam),
			      "value_double",
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

	if (mp->priv->pipeline != NULL) {
		g_object_set (G_OBJECT (mp->priv->volume_dparam),
			      "value_double",
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
	g_return_val_if_fail (mp->priv->pipeline != NULL, FALSE);

	/* FIXME we're lying here, no idea how to fix this though, without trying
	 * a seek which might disrupt playback */
	return TRUE;
}

void
rb_player_set_time (RBPlayer *mp,
			      long time)
{
	GstEvent *event;

	g_return_if_fail (RB_IS_PLAYER (mp));
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
rb_player_get_time (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), -1);

	if (mp->priv->pipeline != NULL)
		return (long) floor (g_timer_elapsed (mp->priv->timer, NULL) + 0.5) + mp->priv->timer_add;
	else
		return -1;
}
