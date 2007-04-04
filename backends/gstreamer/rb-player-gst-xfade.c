/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006   Jonathan Matthew  <jonathan@kaolin.wh9.net>
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

/*
 * GStreamer player backend with crossfading and gaplessness and trees and
 * flowers and bunnies.
 */

/*
 * not yet implemented:
 * - replaygain (need to figure out what to do if we set_replaygain gets
 *               called while fading in)
 * - implement RBPlayerGstTee (maybe not entirely working?)
 * - implement RBPlayerGstFilter (sort of works?)
 *
 * things that need to be fixed:
 * - error reporting is abysmal
 * - shell-player still thinks it's paused when playing the first stream
 *    (playing/paused status is generally screwy) (maybe fixed?)
 *
 * crack:
 * - use more interesting transition effects - filter sweeps, reverb, etc.
 */

/*
 * basic design:
 *
 * we have a single output bin, beginning with an adder.
 * connected to this are a number of stream bins, consisting of a
 * source, decodebin, audio convert/resample, and a volume element used
 * for fading in and out.  (might be interesting to replace those with
 * high/low pass filter elements?)
 *
 * stream bins only stay connected to the adder while actually playing.
 * when not playing (prerolling or paused), the stream bin's source pad
 * is blocked so no data can flow.
 *
 * streams go through a number of states:
 *
 * when a stream is created (in rb_player_open()), it starts in PREROLLING
 * state.  from there:
 *
 * - rb_player_play():  -> PREROLL_PLAY
 * - preroll finishes:  -> WAITING
 *
 * from WAITING:
 *
 * - rb_player_play(), crossfade == 0, other stream playing:  -> WAITING_EOS
 * - rb_player_play(), crossfade > 0, other stream playing:   -> FADING IN, link to adder, unblock
 *      + fade out existing stream
 * - rb_player_play(), crossfade < 0, other stream playing:   -> PLAYING, link to adder, unblock
 *      + stop existing stream
 * - rb_player_play(), existing stream paused:  -> PLAYING, link to adder, unblock
 *      + stop existing stream
 * - rb_player_play(), nothing already playing:  -> PLAYING, link to adder, unblock
 *
 * from PREROLL_PLAY:
 *
 * - preroll finishes, crossfade == 0, other stream playing:  -> WAITING_EOS
 * - preroll finishes, crossfade > 0, other stream playing:  -> FADING_IN, link to adder, unblock
 *   	+ fade out existing stream
 * - preroll finishes, crossfade < 0, other stream playing:  -> PLAYING, link to adder, unblock
 *      + stop existing stream
 * - preroll finishes, existing stream paused:  -> PLAYING, link to adder, unblock
 *      + stop existing stream
 * - preroll finishes, nothing already playing:  -> PLAYING, link to adder, unblock
 *
 * from WAITING_EOS:
 *
 * - EOS received for another stream:  -> PLAYING, link to adder, unblock
 *
 * from FADING_IN:
 *
 * - fade in completes:  -> PLAYING
 * - another stream starts fading in:  -> FADING_OUT
 * - rb_player_pause():  -> PAUSED, block, unlink from adder
 * - stopped for another stream:  -> PENDING_REMOVE
 * - rb_player_set_time():  -> SEEKING, block, unlink
 * - reused for another stream:  -> REUSING; block, unlink
 *
 * from PLAYING:
 *
 * - rb_player_pause():  -> PAUSED, block, unlink from adder
 * - EOS:  -> PENDING_REMOVE
 * - another stream starts fading in:  -> FADING_OUT
 * - stopped for another stream:  -> PENDING_REMOVE
 * - rb_player_set_time():  -> SEEKING, block, unlink
 * - reused for another stream:  -> REUSING; block, unlink
 *
 * from SEEKING:
 * - rb_player_pause():  -> SEEKING_PAUSED
 * - blocked:  perform seek, link, unblock -> PLAYING | FADING_IN
 *
 * from SEEKING_PAUSED:
 * - blocked:  perform seek, -> PAUSED
 * - rb_player_play():   -> SEEKING
 *
 * from PAUSED:
 *
 * - rb_player_play():  -> PLAYING, link to adder, unblock
 * - stopped for another stream:  -> PENDING_REMOVE
 * - rb_player_set_time(): -> perform seek
 *
 * from FADING_OUT:
 *
 * - fade out finishes:  -> PENDING_REMOVE
 * - EOS:  -> PENDING_REMOVE
 * - reused for another stream:  -> REUSING; block, unlink
 *
 * from PENDING_REMOVE:
 *
 * - reap_streams idle handler called:  -> unlink from adder, stream destroyed
 *
 * from REUSING:
 *
 *  - blocked:  emit reuse-stream, -> PLAYING
 */

#include "config.h"
#include <math.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/base/gstbasetransform.h>

#include "rb-player.h"
#include "rb-player-gst-xfade.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-marshal.h"
#include "rb-player-gst-tee.h"
#include "rb-player-gst-filter.h"

static void rb_player_init (RBPlayerIface *iface);
static void rb_player_gst_tee_init (RBPlayerGstTeeIface *iface);
static void rb_player_gst_filter_init (RBPlayerGstFilterIface *iface);
static void rb_player_gst_xfade_dispose (GObject *object);
static void rb_player_gst_xfade_finalize (GObject *object);

static gboolean rb_player_gst_xfade_open (RBPlayer *player,
					  const char *uri,
					  gpointer stream_data,
					  GDestroyNotify stream_data_destroy,
					  GError **error);
static gboolean rb_player_gst_xfade_opened (RBPlayer *player);
static gboolean rb_player_gst_xfade_close (RBPlayer *player, const char *uri, GError **error);
static gboolean rb_player_gst_xfade_play (RBPlayer *player, gint crossfade, GError **error);
static void rb_player_gst_xfade_pause (RBPlayer *player);
static gboolean rb_player_gst_xfade_playing (RBPlayer *player);
static gboolean rb_player_gst_xfade_seekable (RBPlayer *player);
static gboolean rb_player_gst_xfade_in_transition (RBPlayerGstXFade *player);
static void rb_player_gst_xfade_set_time (RBPlayer *player, long time);
static long rb_player_gst_xfade_get_time (RBPlayer *player);
static void rb_player_gst_xfade_set_volume (RBPlayer *player, float volume);
static float rb_player_gst_xfade_get_volume (RBPlayer *player);
static void rb_player_gst_xfade_set_replaygain (RBPlayer *player,
						const char *uri,
						double track_gain, double track_peak,
						double album_gain, double album_peak);
static gboolean rb_player_gst_xfade_add_tee (RBPlayerGstTee *player, GstElement *element);
static gboolean rb_player_gst_xfade_add_filter (RBPlayerGstFilter *player, GstElement *element);
static gboolean rb_player_gst_xfade_remove_tee (RBPlayerGstTee *player, GstElement *element);
static gboolean rb_player_gst_xfade_remove_filter (RBPlayerGstFilter *player, GstElement *element);

static gboolean create_sink (RBPlayerGstXFade *player, GError **error);
static gboolean start_sink (RBPlayerGstXFade *player);
static gboolean stop_sink (RBPlayerGstXFade *player);
static gboolean pause_sink (RBPlayerGstXFade *player);

GType rb_xfade_stream_get_type (void);

G_DEFINE_TYPE_WITH_CODE(RBPlayerGstXFade, rb_player_gst_xfade, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER,
					      rb_player_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_TEE,
					      rb_player_gst_tee_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_FILTER,
					      rb_player_gst_filter_init))

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYER_GST_XFADE, RBPlayerGstXFadePrivate))

#define RB_PLAYER_GST_XFADE_TICK_HZ 5

#define EPSILON			(0.001)
#define STREAM_PLAYING_MESSAGE	"rb-stream-playing"
#define FADE_OUT_DONE_MESSAGE	"rb-fade-out-done"
#define FADE_IN_DONE_MESSAGE	"rb-fade-in-done"
#define STREAM_EOS_MESSAGE	"rb-stream-eos"

#define MAX_NETWORK_BUFFER_SIZE		(2048)

enum
{
	PROP_0,
	PROP_BUFFER_SIZE
};

enum
{
	CAN_REUSE_STREAM,
	REUSE_STREAM,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* these aren't actually used to construct bitmasks,
 * but we search the list that way.
 */
typedef enum
{
	/* stable states */
	WAITING = 1,
	PLAYING = 2,
	PAUSED = 4,

	/* transition states */
	REUSING = 8,
	PREROLLING = 16,
	PREROLL_PLAY = 32,
	FADING_IN = 64,
	SEEKING = 128,
	SEEKING_PAUSED = 256,
	WAITING_EOS = 512,
	FADING_OUT = 1024,
	PENDING_REMOVE = 2048
} StreamState;

typedef struct
{
	GObject parent;
	RBPlayerGstXFade *player;

	GStaticMutex lock;

	char *uri;
	gpointer stream_data;
	GDestroyNotify stream_data_destroy;

	/* stream reuse data */
	char *new_uri;
	gpointer new_stream_data;
	GDestroyNotify new_stream_data_destroy;

	/* probably don't need to store pointers to all of these.. */
	GstElement *bin;
	GstElement *source;
	GstElement *queue;
	GstElement *decoder;
	GstElement *volume;
	GstElement *audioconvert;
	GstElement *audioresample;
	GstElement *capsfilter;
	GstElement *preroll;
	gboolean decoder_linked;
	gboolean emitted_playing;

	GstPad *src_pad;
	GstPad *ghost_pad;
	GstPad *adder_pad;
	gboolean src_blocked;
	GstClockTime base_time;

	gint64 seek_target;

	GstController *fader;
	StreamState state;
	gint crossfade;
	gboolean fading;

	guint64 queue_threshold;
	gulong queue_threshold_id;
	gulong underrun_id;
	gulong queue_probe_id;

	float replaygain_scale;
	double fade_end;

	gulong error_idle_id;
	GError *error;
} RBXFadeStream;

#define RB_TYPE_XFADE_STREAM 	(rb_xfade_stream_get_type ())
#define RB_XFADE_STREAM(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_XFADE_STREAM, RBXFadeStream))
#define RB_IS_XFADE_STREAM(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_XFADE_STREAM))

typedef struct {
	GObjectClass obj_class;
} RBXFadeStreamClass;

static void rb_xfade_stream_class_init (RBXFadeStreamClass *klass);

G_DEFINE_TYPE(RBXFadeStream, rb_xfade_stream, G_TYPE_OBJECT)


struct _RBPlayerGstXFadePrivate
{
	/* probably don't need to store pointers to these either */
	GstElement *pipeline;
	GstElement *outputbin;
	GstElement *silencebin;
	GstElement *adder;
	GstElement *capsfilter;
	GstElement *volume;
	GstElement *sink;
	GstElement *tee;
	GstElement *filterbin;
	enum {
		SINK_NULL,
		SINK_STOPPED,
		SINK_PAUSED,
		SINK_PLAYING
	} sink_state;

	GList *waiting_tees;
	GList *waiting_filters;

	GStaticRecMutex stream_list_lock;
	GList *streams;

	gboolean can_signal_direct_error;
	GError *error;
	gboolean emitted_error;

	gboolean playing;

	float cur_volume;
	guint buffer_size;	/* kB */

	guint tick_timeout_id;

	guint stream_reap_id;
};


static void
rb_xfade_stream_init (RBXFadeStream *stream)
{
	stream->replaygain_scale = 1.0;
	g_static_mutex_init (&stream->lock);
}

static void
rb_xfade_stream_dispose_stream_data (RBXFadeStream *stream)
{
	if (stream->stream_data && stream->stream_data_destroy) {
		stream->stream_data_destroy (stream->stream_data);
	}
	stream->stream_data = NULL;
	stream->stream_data_destroy = NULL;
}

static void
rb_xfade_stream_dispose (GObject *object)
{
	RBXFadeStream *sd = RB_XFADE_STREAM (object);

	if (sd->bin != NULL) {
		gst_object_unref (sd->bin);
		sd->bin = NULL;
	}

	if (sd->source != NULL) {
		gst_object_unref (sd->source);
		sd->source = NULL;
	}

	if (sd->queue != NULL) {
		if (sd->queue_threshold_id != 0)
			g_signal_handler_disconnect (sd->queue, sd->queue_threshold_id);
		if (sd->underrun_id != 0)
			g_signal_handler_disconnect (sd->queue, sd->underrun_id);
		if (sd->queue_probe_id != 0) {
			GstPad *sinkpad;

			sinkpad = gst_element_get_pad (sd->queue, "sink");
			gst_pad_remove_buffer_probe (sinkpad, sd->queue_probe_id);
			gst_object_unref (sinkpad);
		}
		gst_object_unref (sd->queue);
		sd->queue = NULL;
	}

	if (sd->decoder != NULL) {
		gst_object_unref (sd->decoder);
		sd->decoder = NULL;
	}

	if (sd->volume != NULL) {
		gst_object_unref (sd->volume);
		sd->volume = NULL;
	}

	if (sd->fader != NULL) {
		gst_object_unref (sd->fader);
		sd->fader = NULL;
	}

	if (sd->audioconvert != NULL) {
		gst_object_unref (sd->audioconvert);
		sd->audioconvert = NULL;
	}

	if (sd->audioresample != NULL) {
		gst_object_unref (sd->audioresample);
		sd->audioresample = NULL;
	}

	if (sd->player != NULL) {
		g_object_unref (sd->player);
		sd->player = NULL;
	}


	rb_xfade_stream_dispose_stream_data (sd);

	G_OBJECT_CLASS (rb_xfade_stream_parent_class)->dispose (object);
}

static void
rb_xfade_stream_finalize (GObject *object)
{
	RBXFadeStream *sd = RB_XFADE_STREAM (object);

	g_static_mutex_free (&sd->lock);
	g_free (sd->uri);

	if (sd->error != NULL) {
		g_error_free (sd->error);
	}

	G_OBJECT_CLASS (rb_xfade_stream_parent_class)->finalize (object);
}

static void
rb_xfade_stream_class_init (RBXFadeStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_xfade_stream_dispose;
	object_class->finalize = rb_xfade_stream_finalize;
}

/* caller must hold stream list lock */
static void
dump_stream_list (RBPlayerGstXFade *player)
{
	GList *l;
	if (player->priv->streams == NULL) {
		rb_debug ("stream list is empty");
	} else {
		rb_debug ("current stream list:");
		for (l = player->priv->streams; l != NULL; l = l->next) {
			RBXFadeStream *stream = (RBXFadeStream *)l->data;
			const char *statename = "<wtf>";
			switch (stream->state) {
			case WAITING:	 	statename = "waiting";		break;
			case PLAYING:	 	statename = "playing";		break;
			case PAUSED:	 	statename = "paused";		break;

			case REUSING:		statename = "reusing";		break;
			case PREROLLING: 	statename = "prerolling"; 	break;
			case PREROLL_PLAY: 	statename = "preroll->play"; 	break;
			case FADING_IN: 	statename = "fading in"; 	break;
			case SEEKING:		statename = "seeking";		break;
			case SEEKING_PAUSED:	statename = "seeking->paused";	break;
			case WAITING_EOS: 	statename = "waiting for EOS"; 	break;
			case FADING_OUT: 	statename = "fading out"; 	break;

			case PENDING_REMOVE:	statename = "pending remove";	break;
			}

			rb_debug ("[%s] %s", statename, stream->uri);
		}
	}
}

static void
dump_stream_list_lock (RBPlayerGstXFade *player)
{
	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	dump_stream_list (player);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
}

/* caller must hold stream list lock */
static RBXFadeStream *
find_stream_by_uri (RBPlayerGstXFade *player, const char *uri)
{
	GList *i;
	if (uri == NULL)
		return NULL;

	for (i = player->priv->streams; i != NULL; i = i->next) {
		RBXFadeStream *stream = (RBXFadeStream *)i->data;
		if (strcmp (uri, stream->uri) == 0)
			return g_object_ref (stream);
	}
	return NULL;
}

/* caller must hold stream list lock */
static RBXFadeStream *
find_stream_by_element (RBPlayerGstXFade *player, GstElement *element)
{
	GList *i;

	for (i = player->priv->streams; i != NULL; i = i->next) {
		RBXFadeStream *stream;
		GstElement *e;

		stream = (RBXFadeStream *)i->data;
		e = element;
		while (e != NULL) {
			if (e == stream->bin)
				return g_object_ref (stream);

			e = GST_ELEMENT_PARENT (e);
		}
	}

	return NULL;
}

/* caller must hold stream list lock */
static RBXFadeStream *
find_stream_by_state (RBPlayerGstXFade *player, gint state_mask)
{
	GList *i;

	for (i = player->priv->streams; i != NULL; i = i->next) {
		RBXFadeStream *stream;

		stream = (RBXFadeStream *)i->data;
		if ((stream->state & state_mask) != 0) {
			return g_object_ref (stream);
		}
	}

	return NULL;
}

static void
rb_player_gst_xfade_get_property (GObject *object,
				  guint prop_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (object);

	switch (prop_id) {
	case PROP_BUFFER_SIZE:
		g_value_set_uint (value, player->priv->buffer_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_player_gst_xfade_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (object);

	switch (prop_id) {
	case PROP_BUFFER_SIZE:
		player->priv->buffer_size = g_value_get_uint (value);
		/* try to adjust any playing streams? */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_player_gst_xfade_class_init (RBPlayerGstXFadeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_player_gst_xfade_dispose;
	object_class->finalize = rb_player_gst_xfade_finalize;
	object_class->set_property = rb_player_gst_xfade_set_property;
	object_class->get_property = rb_player_gst_xfade_get_property;

	g_object_class_install_property (object_class,
					 PROP_BUFFER_SIZE,
					 g_param_spec_uint ("buffer-size",
						 	    "buffer size",
							    "Buffer size for network streams, in kB",
							    64, MAX_NETWORK_BUFFER_SIZE, 128,
							    G_PARAM_READWRITE));

	signals[CAN_REUSE_STREAM] =
		g_signal_new ("can-reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstXFadeClass, can_reuse_stream),
			      NULL, NULL,
			      rb_marshal_BOOLEAN__STRING_STRING_OBJECT,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[REUSE_STREAM] =
		g_signal_new ("reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstXFadeClass, reuse_stream),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING_OBJECT,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);

	g_type_class_add_private (klass, sizeof (RBPlayerGstXFadePrivate));
}

static void
rb_player_init (RBPlayerIface *iface)
{
	iface->open = rb_player_gst_xfade_open;
	iface->opened = rb_player_gst_xfade_opened;
	iface->close = rb_player_gst_xfade_close;
	iface->play = rb_player_gst_xfade_play;
	iface->pause = rb_player_gst_xfade_pause;
	iface->playing = rb_player_gst_xfade_playing;
	iface->set_volume = rb_player_gst_xfade_set_volume;
	iface->get_volume = rb_player_gst_xfade_get_volume;
	iface->set_replaygain = rb_player_gst_xfade_set_replaygain;
	iface->seekable = rb_player_gst_xfade_seekable;
	iface->set_time = rb_player_gst_xfade_set_time;
	iface->get_time = rb_player_gst_xfade_get_time;
	iface->multiple_open = (RBPlayerFeatureFunc) rb_true_function;
}

static void
rb_player_gst_tee_init (RBPlayerGstTeeIface *iface)
{
	iface->add_tee = rb_player_gst_xfade_add_tee;
	iface->remove_tee = rb_player_gst_xfade_remove_tee;
}

static void
rb_player_gst_filter_init (RBPlayerGstFilterIface *iface)
{
	iface->add_filter = rb_player_gst_xfade_add_filter;
	iface->remove_filter = rb_player_gst_xfade_remove_filter;
}


static void
rb_player_gst_xfade_init (RBPlayerGstXFade *player)
{
	player->priv = GET_PRIVATE (player);

	g_static_rec_mutex_init (&player->priv->stream_list_lock);
	player->priv->cur_volume = 1.0f;
}

static void
rb_player_gst_xfade_dispose (GObject *object)
{
	RBPlayerGstXFade *player;
	GList *l;

	g_return_if_fail (RB_IS_PLAYER_GST_XFADE (object));
	player = RB_PLAYER_GST_XFADE (object);

	/* if the sink is paused, unpause it while we clean up streams */
	if (player->priv->sink_state == SINK_PAUSED) {
		rb_debug ("unpausing sink for shutdown");
		start_sink (player);
	}

	/* clean up streams */
	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	for (l = player->priv->streams; l != NULL; l = l->next) {
		RBXFadeStream *stream = (RBXFadeStream *)l->data;

		/* unlink instead? */
		gst_element_set_state (stream->bin, GST_STATE_NULL);

		g_object_unref (stream);
	}
	g_list_free (player->priv->streams);
	player->priv->streams = NULL;
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	stop_sink (player);

	if (player->priv->pipeline != NULL) {
		/* maybe we should keep references to the adder, sink, etc.? */
		gst_element_set_state (player->priv->pipeline, GST_STATE_NULL);
		g_object_unref (player->priv->pipeline);
		player->priv->pipeline = NULL;
	}

	G_OBJECT_CLASS (rb_player_gst_xfade_parent_class)->dispose (object);
}

static void
rb_player_gst_xfade_finalize (GObject *object)
{
	RBPlayerGstXFade *player;

	player = RB_PLAYER_GST_XFADE (object);

	if (player->priv->waiting_tees) {
		g_list_foreach (player->priv->waiting_tees, (GFunc)gst_object_sink, NULL);
	}
	g_list_free (player->priv->waiting_tees);

	G_OBJECT_CLASS (rb_player_gst_xfade_parent_class)->finalize (object);
}

RBPlayer *
rb_player_gst_xfade_new (GError **error)
{
	RBPlayer *mp;

	mp = RB_PLAYER (g_object_new (RB_TYPE_PLAYER_GST_XFADE, NULL, NULL));

	return mp;
}

static gboolean
emit_stream_error_cb (RBXFadeStream *stream)
{
	stream->error_idle_id = 0;
	_rb_player_emit_error (RB_PLAYER (stream->player),
			       stream->stream_data,
			       stream->error);
	g_error_free (stream->error);
	stream->error = NULL;

	return FALSE;
}

static void
emit_stream_error (RBXFadeStream *stream, GError *error)
{
	if (stream->error_idle_id != 0) {
		g_error_free (error);
		return;
	}

	stream->error = error;
	stream->error_idle_id = g_idle_add ((GSourceFunc) emit_stream_error_cb,
					    stream);
}

static void
post_stream_playing_message (RBXFadeStream *stream)
{
	GstMessage *msg;
	GstStructure *s;

	if (stream->emitted_playing)
		return;

	rb_debug ("posting " STREAM_PLAYING_MESSAGE " message for stream %s", stream->uri);
	s = gst_structure_new (STREAM_PLAYING_MESSAGE, NULL);
	msg = gst_message_new_application (GST_OBJECT (stream->bin), s);
	gst_element_post_message (stream->bin, msg);

	stream->emitted_playing = TRUE;
}

static void
post_buffering_message (RBXFadeStream *stream, guint64 level)
{
	GstMessage *message;

	/* somewhat hackish: pretend the stream is already playing
	 * so that everything above handles the buffering messages
	 * correctly.
	 */
	post_stream_playing_message (stream);

	message = gst_message_new_buffering (GST_OBJECT_CAST (stream->queue), level);
	gst_element_post_message (stream->queue, message);
}

/* updates a stream's base time so its position is reported correctly */
static void
adjust_stream_base_time (RBXFadeStream *stream)
{
	GstFormat format;
	gint64 output_pos = -1;
	gint64 stream_pos = -1;

	format = GST_FORMAT_TIME;
	gst_element_query_position (GST_PAD_PARENT (stream->adder_pad), &format, &output_pos);
	if (output_pos != -1) {
		stream->base_time = output_pos;
	}

	/* offset the base position to account for the current stream position */
	format = GST_FORMAT_TIME;
	gst_element_query_position (stream->volume, &format, &stream_pos);
	if (stream_pos != -1) {
		rb_debug ("adjusting base time: %" G_GINT64_FORMAT 
		    " - %" G_GINT64_FORMAT " => %" G_GINT64_FORMAT, 
		    stream->base_time, stream_pos, 
		    stream->base_time - stream_pos);
		stream->base_time -= stream_pos;
	}
}

/* called on a streaming thread when the volume level for a stream changes. */
static void
volume_changed_cb (GObject *object, GParamSpec *pspec, RBPlayerGstXFade *player)
{
	RBXFadeStream *stream;
	gdouble vol;
	char *message = NULL;

	/* post app messages on the bus when fades complete.
	 * our bus callback will handle them on the main thread.
	 */

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	stream = find_stream_by_element (player, GST_ELEMENT (object));
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	if (stream == NULL) {
		rb_debug ("got volume change for unknown stream");
		return;
	}

	/* check if the fade is complete */
	g_object_get (stream->volume, "volume", &vol, NULL);
	switch (stream->state) {
	case FADING_IN:
		if (vol > (stream->fade_end - EPSILON)) {
			rb_debug ("stream %s fully faded in -> PLAYING state", stream->uri);
			/*message = FADE_IN_DONE_MESSAGE;*/		/* not actually used */
			stream->fading = FALSE;
			stream->state = PLAYING;
		/*} else {
			rb_debug ("fading %s in: %f", stream->uri, (float)vol);*/
		}
		break;
	case FADING_OUT:
		if (vol < (stream->fade_end + EPSILON)) {
			rb_debug ("stream %s fully faded out", stream->uri);
			message = FADE_OUT_DONE_MESSAGE;
			stream->fading = FALSE;
		} else {
			/*rb_debug ("fading %s out: %f", stream->uri, (float)vol);*/
			/* force the volume element out of passthrough mode so it
			 * continues to update the controller (otherwise, if the
			 * fade out starts at 1.0, it never gets anywhere)
			 */
			gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (stream->volume), FALSE);
		}
		break;
	default:
		rb_debug ("unexpectedly got a volume change for stream %s to %f (not fading)", stream->uri, (float)vol);
		break;
	}

	if (message != NULL) {
		GstMessage *msg;
		GstStructure *s;

		rb_debug ("posting %s message for stream %s", message, stream->uri);
		s = gst_structure_new (message, NULL);
		msg = gst_message_new_application (GST_OBJECT (object), s);
		gst_element_post_message (GST_ELEMENT (object), msg);
	}

	g_object_unref (stream);
}

/* starts a volume slide on a stream.
 * volume_changed_cb watches the volume change
 * and posts a message on the bus when the slide
 * is done.
 */
static void
start_stream_fade (RBXFadeStream *stream, double start, double end, gint64 time)
{
	GValue v = {0,};
	gint64 pos = -1;
	GstFormat format = GST_FORMAT_TIME;
	RBPlayerGstXFade *player = stream->player;

	/* should this take replaygain scaling into account? */

	gst_element_query_position (stream->volume, &format, &pos);
	if (pos < 0) {
		/* probably means we haven't actually started the stream yet.
		 * we also get (weird) negative results with some decoders
		 * (mad but not flump3dec, for instance) immediately after prerolling.
		 * the controller doesn't seem to work if we give it a 0 timestamp
		 * here, but something unnoticeably later does work.
		 */
		pos = 100000;
	}
	if (format != GST_FORMAT_TIME) {
		rb_debug ("got position query results in some other format: %s", gst_format_get_name (format));
		pos = 0;
	}

	/* apply replaygain scaling */
	start *= stream->replaygain_scale;
	end *= stream->replaygain_scale;
	rb_debug ("fading stream %s: [%f, %" G_GINT64_FORMAT "] to [%f, %" G_GINT64_FORMAT "]",
		  stream->uri,
		  (float)start, pos,
		  (float)end, pos + time);

	/* drop the stream list lock before doing anything to the controller.
	 * volume changes result in volume_changed_cb being called with the
	 * controller lock held; volume_changed_cb acquires the stream list lock.
	 * therefore we can't ever touch the controller with the stream list lock
	 * held.
	 */

	g_object_ref (stream);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	/* apparently we need to set the starting volume, otherwise fading in doesn't work. */
	g_object_set (stream->volume, "volume", start, NULL);

	gst_controller_unset_all (stream->fader, "volume");

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, start);
	if (gst_controller_set (stream->fader, "volume", pos, &v) == FALSE) {
		rb_debug ("controller didn't like our start point");
	}
	g_value_unset (&v);

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, end);
	if (gst_controller_set (stream->fader, "volume", pos + time, &v) == FALSE) {
		rb_debug ("controller didn't like our end point");
	}
	g_value_unset (&v);

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	g_object_unref (stream);

	stream->fade_end = end;
	stream->fading = TRUE;

	/* tiny hack:  if the controlled element is in passthrough mode, the
	 * controller won't get updated.
	 */
	gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (stream->volume), FALSE);
}


static void
link_unblocked_cb (GstPad *pad, gboolean blocked, RBXFadeStream *stream)
{
	/* sometimes we seem to get called twice */
	if (stream->state == FADING_IN || stream->state == PLAYING)
		return;

	rb_debug ("stream %s is unblocked -> FADING_IN | PLAYING", stream->uri);
	g_static_rec_mutex_lock (&stream->player->priv->stream_list_lock);
	stream->src_blocked = FALSE;
	if (stream->fading)
		stream->state = FADING_IN;
	else
		stream->state = PLAYING;

	adjust_stream_base_time (stream);

	/* should handle state change failures here.. */
	gst_element_set_state (stream->bin, GST_STATE_PLAYING);

	post_stream_playing_message (stream);
	g_static_rec_mutex_unlock (&stream->player->priv->stream_list_lock);
	g_object_unref (stream);
}

/* links a stream bin to the adder
 * - adds the bin to the pipeline
 * - links to a new adder pad
 * - unblocks the stream if it's blocked
 *
 * how should this signal errors?  post bus messages?
 */
static gboolean
link_and_unblock_stream (RBXFadeStream *stream, GError **error)
{
	GstPadLinkReturn plr;
	GstStateChangeReturn scr;
	RBPlayerGstXFade *player = stream->player;

	if (stream->adder_pad != NULL) {
		rb_debug ("stream %s is already linked", stream->uri);
		return TRUE;
	}

	rb_debug ("linking stream %s", stream->uri);

	start_sink (player);

	if (GST_ELEMENT_PARENT (stream->bin) == NULL)
		gst_bin_add (GST_BIN (player->priv->pipeline), stream->bin);

	stream->adder_pad = gst_element_get_request_pad (player->priv->adder, "sink%d");
	if (stream->adder_pad == NULL) {
		/* this error message kind of sucks */
		rb_debug ("couldn't get adder pad to link in new stream");
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link new stream into GStreamer pipeline"));
		return FALSE;
	}

	plr = gst_pad_link (stream->ghost_pad, stream->adder_pad);
	if (GST_PAD_LINK_FAILED (plr)) {
		gst_element_release_request_pad (player->priv->adder, stream->adder_pad);
		stream->adder_pad = NULL;

		/* this error message kind of sucks */
		rb_debug ("linking stream pad to adder pad failed: %d", plr);
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link new stream into GStreamer pipeline"));
		return FALSE;
	}

	if (stream->src_blocked) {
		g_object_ref (stream);
		gst_pad_set_blocked_async (stream->src_pad,
					   FALSE,
					   (GstPadBlockCallback) link_unblocked_cb,
					   stream);
		return TRUE;
	} else {
		rb_debug ("??? stream %s is already unblocked -> PLAYING", stream->uri);
		stream->state = PLAYING;
		adjust_stream_base_time (stream);

		scr = gst_element_set_state (stream->bin, GST_STATE_PLAYING);

		post_stream_playing_message (stream);

		if (scr == GST_STATE_CHANGE_FAILURE) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_GENERAL,
				     _("Failed to start new stream"));
			return FALSE;
		}
		return TRUE;
	}
}

/*
 * reuses a stream.  the stream reuse signal is handled by some
 * external code somewhere.
 */
static void
reuse_stream (RBXFadeStream *stream)
{
	GError *error = NULL;
	g_signal_emit (stream->player,
		       signals[REUSE_STREAM], 0,
		       stream->new_uri, stream->uri, stream->bin);

	/* replace URI and stream data */
	g_free (stream->uri);
	stream->uri = stream->new_uri;

	rb_xfade_stream_dispose_stream_data (stream);
	stream->stream_data = stream->new_stream_data;
	stream->stream_data_destroy = stream->new_stream_data_destroy;

	stream->new_uri = NULL;
	stream->new_stream_data = NULL;
	stream->new_stream_data_destroy = NULL;

	if (link_and_unblock_stream (stream, &error) == FALSE) {
		emit_stream_error (stream, error);
	}
}


/*
 * performs a seek on an unlinked and blocked stream.
 * if the stream is still in FADING_IN or PLAYING state,
 * relinks and unblocks the stream.
 */
static void
perform_seek (RBXFadeStream *stream)
{
	GError *error;
	rb_debug ("sending seek event..");
	gst_element_seek (stream->volume, 1.0,
			  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, stream->seek_target,
			  GST_SEEK_TYPE_NONE, -1);

	switch (stream->state) {
	case SEEKING:
		if (link_and_unblock_stream (stream, &error) == FALSE) {
			emit_stream_error (stream, error);
		}
		/* state will be set to PLAYING or FADING_IN when the
		 * stream is relinked.
		 */
		break;
	case SEEKING_PAUSED:
		rb_debug ("leaving paused stream %s unlinked", stream->uri);
		stream->state = PAUSED;
		break;
	default:
		break;
	}
}

/* called when a stream's source pad is blocked, so it can be unlinked
 * from the pipeline.
 */
static void
unlink_blocked_cb (GstPad *pad, gboolean blocked, RBXFadeStream *stream)
{
	if (stream->adder_pad == NULL) {
		rb_debug ("stream %s is already unlinked.  huh?", stream->uri);
		return;
	}

	rb_debug ("stream %s is blocked; unlinking", stream->uri);

	if (gst_pad_unlink (stream->ghost_pad, stream->adder_pad) == FALSE) {
		g_warning ("Couldn't unlink stream %s: things will probably go quite badly from here on", stream->uri);
	}

	gst_element_release_request_pad (GST_PAD_PARENT (stream->adder_pad), stream->adder_pad);
	stream->adder_pad = NULL;

	stream->src_blocked = TRUE;
	stream->emitted_playing = FALSE;

	/* might want a stream-paused signal here? */

	/* handle unlinks for seeking and stream reuse */
	switch (stream->state) {
	case SEEKING:
		perform_seek (stream);
		break;
	case REUSING:
		reuse_stream (stream);
		break;
	default:
		break;
	}
}

/*
 * blocks and unlinks a stream.  this is the only way we can pause a stream -
 * if the stream is linked to the adder and the audio sink is in PLAYING, the
 * stream will play.
 */
static void
unlink_and_block_stream (RBXFadeStream *stream)
{
	if (stream->adder_pad == NULL) {
		rb_debug ("stream %s is not linked", stream->uri);
	} else if (stream->src_blocked) {
		/* probably shouldn't happen, but we'll handle it anyway */
		unlink_blocked_cb (stream->src_pad, TRUE, stream);
	} else {
		gst_pad_set_blocked_async (stream->src_pad,
					   TRUE,
					   (GstPadBlockCallback) unlink_blocked_cb,
					   stream);
	}
}

/*
 * sets a stream to NULL state, unlinks it from the adder,
 * removes it from the pipeline, removes it from the
 * stream list, and frees it (hopefully).
 *
 * must not be called on a streaming thread.
 */
static void
unlink_and_dispose_stream (RBPlayerGstXFade *player, RBXFadeStream *stream)
{
	GstStateChangeReturn sr;

	rb_debug ("stopping stream %s", stream->uri);
	sr = gst_element_set_state (stream->bin, GST_STATE_NULL);
	if (sr == GST_STATE_CHANGE_ASYNC) {
		/* downward state transitions aren't supposed to return ASYNC.. */
		rb_debug ("!!! stream %s isn't cooperating", stream->uri);
		gst_element_get_state (stream->bin, NULL, NULL, GST_CLOCK_TIME_NONE);
	}

	if (stream->adder_pad != NULL) {
		rb_debug ("unlinking stream %s", stream->uri);
		if (gst_pad_unlink (stream->ghost_pad, stream->adder_pad) == FALSE) {
			g_warning ("Couldn't unlink stream %s: things will probably go quite badly from here on", stream->uri);
		}

		gst_element_release_request_pad (GST_PAD_PARENT (stream->adder_pad), stream->adder_pad);
		stream->adder_pad = NULL;
	}

	gst_bin_remove (GST_BIN (player->priv->pipeline), stream->bin);

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	player->priv->streams = g_list_remove (player->priv->streams, stream);
	dump_stream_list (player);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	g_object_unref (stream);
}

/* idle handler used to clean up finished streams */
static gboolean
reap_streams (RBPlayerGstXFade *player)
{
	GList *t;
	GList *reap = NULL;

	player->priv->stream_reap_id = 0;
	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	dump_stream_list (player);
	for (t = player->priv->streams; t != NULL; t = t->next) {
		RBXFadeStream *stream = (RBXFadeStream *)t->data;

		if (stream->state == PENDING_REMOVE) {
			reap = g_list_prepend (reap, stream);
		}
	}
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	for (t = reap; t != NULL; t = t->next) {
		RBXFadeStream *stream = (RBXFadeStream *)t->data;
		rb_debug ("reaping stream %s", stream->uri);
		unlink_and_dispose_stream (player, stream);
	}
	g_list_free (reap);

	return FALSE;
}

/* schedules a call to reap_streams */
static void
schedule_stream_reap (RBPlayerGstXFade *player)
{
	if (player->priv->stream_reap_id != 0)
		return;

	dump_stream_list_lock (player);
	player->priv->stream_reap_id = g_idle_add ((GSourceFunc) reap_streams, player);
}

/* emits a tag signal from the player, maybe */
static void
process_tag (const GstTagList *list, const gchar *tag, RBXFadeStream *stream)
{
	int count;
	RBMetaDataField field;
	const GValue *val;
	GValue newval = {0,};

	count = gst_tag_list_get_tag_size (list, tag);
	if (count < 1)
		return;

	rb_debug ("got tag %s for stream %s", tag, stream->uri);
	/* only handle the subset of fields we use for iradio */
	if (!strcmp (tag, GST_TAG_TITLE))
		field = RB_METADATA_FIELD_TITLE;
	else if (!strcmp (tag, GST_TAG_GENRE))
		field = RB_METADATA_FIELD_GENRE;
	else if (!strcmp (tag, GST_TAG_COMMENT))
		field = RB_METADATA_FIELD_COMMENT;
	else if (!strcmp (tag, GST_TAG_BITRATE))
		field = RB_METADATA_FIELD_BITRATE;
#ifdef GST_TAG_MUSICBRAINZ_TRACKID
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID))
		field = RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
#endif
	else
		return;

	/* of those, all except bitrate are strings */
	switch (field) {
	case RB_METADATA_FIELD_BITRATE:
		g_value_init (&newval, G_TYPE_ULONG);
		break;

	case RB_METADATA_FIELD_TITLE:
	case RB_METADATA_FIELD_GENRE:
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
	default:
		g_value_init (&newval, G_TYPE_STRING);
		break;
	}
	val = gst_tag_list_get_value_index (list, tag, 0);
	if (!g_value_transform (val, &newval)) {
		rb_debug ("Could not transform tag value type %s into %s",
			  g_type_name (G_VALUE_TYPE (val)),
			  g_type_name (G_VALUE_TYPE (&newval)));
		return;
	}

	rb_debug ("emitting info field %d for uri %s", field, stream->uri);
	_rb_player_emit_info (RB_PLAYER (stream->player),
			      stream->stream_data,
			      field,
			      &newval);

	g_value_unset (&newval);
}

/* gstreamer message bus callback */
static gboolean
rb_player_gst_xfade_bus_cb (GstBus *bus, GstMessage *message, RBPlayerGstXFade *player)
{
	RBXFadeStream *stream;
	GstObject *message_src;

	g_return_val_if_fail (player != NULL, FALSE);

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	message_src = GST_MESSAGE_SRC (message);
	stream = find_stream_by_element (player, GST_ELEMENT (message_src));
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR:
	{
		char *debug;
		GError *error, *sig_error;
		int code;
		gboolean emit = TRUE;

		gst_message_parse_error (message, &error, &debug);

		if (stream == NULL) {
			rb_debug ("Couldn't find stream for error \"%s\": %s", error->message, debug);
			g_error_free (error);
			g_free (debug);
			break;
		}

		/* If we've already got an error, ignore 'internal data flow error'
		 * type messages, as they're too generic to be helpful.
		 */
		if (player->priv->emitted_error &&
		    error->domain == GST_STREAM_ERROR &&
		    error->code == GST_STREAM_ERROR_FAILED) {
			rb_debug ("Ignoring generic error \"%s\"", error->message);
			emit = FALSE;
		}

		if ((error->domain == GST_CORE_ERROR)
			|| (error->domain == GST_LIBRARY_ERROR)
			|| (error->domain == GST_RESOURCE_ERROR && error->code == GST_RESOURCE_ERROR_BUSY)) {
			code = RB_PLAYER_ERROR_NO_AUDIO;
		} else {
			code = RB_PLAYER_ERROR_GENERAL;
		}

		if (emit) {
			sig_error = g_error_new_literal (RB_PLAYER_ERROR,
							 code,
							 error->message);
			player->priv->emitted_error = TRUE;
			_rb_player_emit_error (RB_PLAYER (player), stream->stream_data, sig_error);
		}

		/* kill the stream? */
		unlink_and_dispose_stream (player, stream);

		g_error_free (error);
		g_free (debug);
		break;
	}
	case GST_MESSAGE_TAG:
		if (stream == NULL) {
			rb_debug ("got tag message for unknown stream");
		} else {
			GstTagList *tags;
			gst_message_parse_tag (message, &tags);

			gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, stream);
			gst_tag_list_free (tags);
		}
		break;

	case GST_MESSAGE_DURATION:
		if (stream == NULL) {
			rb_debug ("got duration message for unknown stream");
		} else {
			gint64 duration;
			GstFormat format;
			gst_message_parse_duration (message, &format, &duration);
			rb_debug ("got duration %" G_GINT64_FORMAT 
			    " for stream %s", duration, stream->uri);
		}
		break;

	case GST_MESSAGE_APPLICATION:
	{
		/* process fade messages and emit signals for
		 * other stuff.
		 */
		const GstStructure *structure;
		const char *name;

		structure = gst_message_get_structure (message);
		name = gst_structure_get_name (structure);
		if (stream == NULL) {
			rb_debug ("got application message %s for unknown stream", name);
		} else if (strcmp (name, STREAM_PLAYING_MESSAGE) == 0) {
			rb_debug ("got stream playing message for %s", stream->uri);
			_rb_player_emit_playing_stream (RB_PLAYER (player), stream->stream_data);
		} else if (strcmp (name, FADE_IN_DONE_MESSAGE) == 0) {
			/* do something? */
		} else if (strcmp (name, FADE_OUT_DONE_MESSAGE) == 0) {
			/* stop the stream and dispose of it */
			rb_debug ("got fade-out-done for stream %s -> PENDING_REMOVE", stream->uri);
			stream->state = PENDING_REMOVE;
			schedule_stream_reap (player);
		} else if (strcmp (name, STREAM_EOS_MESSAGE) == 0) {
			/* emit EOS, dispose of the stream, and start any
			 * streams we had waiting for an EOS.
			 */
			rb_debug ("got EOS message for stream %s -> PENDING_REMOVE", stream->uri);
			_rb_player_emit_eos (RB_PLAYER (player), stream->stream_data);
			stream->state = PENDING_REMOVE;
			schedule_stream_reap (player);
		} else {
			_rb_player_emit_event (RB_PLAYER (player), stream->stream_data, name, NULL);
		}

		break;
	}
	case GST_MESSAGE_BUFFERING:
	{
		const GstStructure *s;
		gint progress;

		s = gst_message_get_structure (message);
		if (!gst_structure_get_int (s, "buffer-percent", &progress)) {
			g_warning ("Could not get value from BUFFERING message");
			break;
		}

		if (stream == NULL) {
			rb_debug ("got buffering message for unknown stream (%d)", progress);
		} else {
			_rb_player_emit_buffering (RB_PLAYER (player), stream->stream_data, progress);
		}
		break;
	}
	default:
		break;
	}

	if (stream != NULL)
		g_object_unref (stream);

	return TRUE;
}


/* queue buffering signal handlers */


static gboolean
stream_queue_probe_cb (GstPad *pad, GstBuffer *data, RBXFadeStream *stream)
{
	guint64 level = 0;
	guint64 progress = 0;

	g_object_get (stream->queue, "current-level-bytes", &level, NULL);
	if (stream->queue_threshold > 0) {
		progress = (level * 99) / stream->queue_threshold;
		if (progress > 99)
			progress = 99;
	} else {
		progress = 99;
	}
	rb_debug ("current network buffer level: %" G_GUINT64_FORMAT
		  "; threshold %" G_GUINT64_FORMAT
		  " - %" G_GUINT64_FORMAT "%%",
		  level, stream->queue_threshold, progress);

	post_buffering_message (stream, progress);

	return TRUE;
}

static void
stream_queue_threshold_cb (GstElement *queue, RBXFadeStream *stream)
{
	GstPad *sinkpad;

	rb_debug ("queue running - removing pad probe, running signal handler");

	/* detach pad probe */
	sinkpad = gst_element_get_pad (stream->queue, "sink");
	gst_pad_remove_buffer_probe (sinkpad, stream->queue_probe_id);
	stream->queue_probe_id = 0;
	gst_object_unref (sinkpad);

	g_object_set (stream->queue, "min-threshold-bytes", 0, NULL);

	/* detach self */
	g_signal_handler_disconnect (stream->queue,
				     stream->queue_threshold_id);
	stream->queue_threshold_id = 0;

	post_buffering_message (stream, 100);
}

static void
stream_queue_underrun_cb (GstElement *queue, RBXFadeStream *stream)
{
	rb_debug ("queue underrun - attaching pad probe, running signal handler");
	GstPad *sinkpad;

	g_object_set (stream->queue, "min-threshold-bytes", stream->queue_threshold, NULL);

	/* attach pad probe to get buffering progress while refilling */
	if (stream->queue_probe_id == 0) {
		sinkpad = gst_element_get_pad (stream->queue, "sink");
		stream->queue_probe_id = gst_pad_add_buffer_probe (sinkpad,
								   G_CALLBACK (stream_queue_probe_cb),
								   stream);
		/* need an event probe for eos/flush events too? */
		gst_object_unref (sinkpad);
	}

	if (stream->queue_threshold_id == 0) {
		stream->queue_threshold_id =
			g_signal_connect_object (stream->queue,
						 "running",
						 G_CALLBACK (stream_queue_threshold_cb),
						 stream,
						 0);

		post_buffering_message (stream, 0);
	}

}

/* links decodebin src pads to the rest of the output pipeline */
static void
stream_new_decoded_pad_cb (GstElement *decoder, GstPad *pad, gboolean last, RBXFadeStream *stream)
{
	GstCaps *caps;
	GstStructure *structure;
	const char *mediatype;
	GstPad *vpad;

	/* make sure this is an audio pad */
	caps = gst_pad_get_caps (pad);
	if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
		rb_debug ("got empty/any decoded caps.  hmm?");
		gst_caps_unref (caps);
		return;
	}

	structure = gst_caps_get_structure (caps, 0);
	mediatype = gst_structure_get_name (structure);
	if (g_str_has_prefix (mediatype, "audio/x-raw") == FALSE) {
		rb_debug ("got non-audio decoded caps: %s", mediatype);
		gst_caps_unref (caps);
		return;
	}

	/* link to the audioconvert element for the stream */
	if (stream->decoder_linked) {
		/* probably should never happen */
		rb_debug ("hmm, decoder is already linked");
	} else {
		rb_debug ("got decoded audio pad for stream %s", stream->uri);
		vpad = gst_element_get_pad (stream->audioconvert, "sink");
		gst_pad_link (pad, vpad);
		gst_object_unref (vpad);
		stream->decoder_linked = TRUE;
	}
}

/* handles EOS events on stream bins.  since the pipeline as a whole
 * never goes EOS, we don't get an EOS bus message, so we have to handle
 * it here.
 *
 * when an EOS event is received, a bus message is posted, and any streams
 * in the WAITING_EOS state are started.
 *
 * when a new segment event is received, the stream base time is updated
 * (mostly for seeking)
 */
static gboolean
stream_src_event_cb (GstPad *pad, GstEvent *event, RBXFadeStream *stream)
{
	GstMessage *msg;
	GstStructure *s;
	RBPlayerGstXFade *player;
	GList *l;

	switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_EOS:
		rb_debug ("posting EOS message for stream %s", stream->uri);
		s = gst_structure_new (STREAM_EOS_MESSAGE, NULL);
		msg = gst_message_new_application (GST_OBJECT (stream->bin), s);
		gst_element_post_message (stream->bin, msg);

		/* start playing any streams that were waiting on an EOS
		 * (are we really allowed to do this on a stream thread?)
		 */
		player = stream->player;
		g_static_rec_mutex_lock (&player->priv->stream_list_lock);
		for (l = player->priv->streams; l != NULL; l = l->next) {
			RBXFadeStream *pstream = l->data;
			if (pstream->state == WAITING_EOS) {
				GError *error = NULL;
				rb_debug ("starting stream %s on EOS from previous", pstream->uri);

				if (link_and_unblock_stream (pstream, &error) == FALSE) {
					emit_stream_error (pstream, error);
				}
			}
		}
		g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
		break;

	case GST_EVENT_NEWSEGMENT:
		rb_debug ("got new segment for stream %s", stream->uri);
		adjust_stream_base_time (stream);
		break;
	default:
		break;
	}

	return TRUE;
}

/*
 * stream playback bin:
 *
 * src [ ! queue ] ! decodebin ! audioconvert ! audioresample ! caps ! queue ! volume
 *
 * the first queue is only added for non-local streams.  the thresholds
 * and such are probably going to be configurable at some point,
 * since people seem to get all whiny if they don't have a buffer
 * size slider to play with.
 *
 * the volume element is used for crossfading and probably replaygain
 * somehow.
 */
static RBXFadeStream *
create_stream (RBPlayerGstXFade *player, const char *uri, gpointer stream_data, GDestroyNotify stream_data_destroy)
{
	RBXFadeStream *stream;
	GstCaps *caps;

	rb_debug ("creating new stream for %s (stream data %p)", uri, stream_data);
	stream = g_object_new (RB_TYPE_XFADE_STREAM, NULL, NULL);
	stream->player = g_object_ref (player);
	stream->stream_data = stream_data;
	stream->stream_data_destroy = stream_data_destroy;
	stream->uri = g_strdup (uri);
	stream->bin = gst_bin_new (NULL);
	stream->state = WAITING;
	if (stream->bin == NULL) {
		rb_debug ("unable to create new bin");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->bin);

	stream->source = gst_element_make_from_uri (GST_URI_SRC, stream->uri, NULL);
	if (stream->source == NULL) {
		rb_debug ("unable to create source for %s", uri);
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->source);

	/* if the source looks like it might support shoutcast/icecast metadata
	 * extraction, ask it to do so.
	 */
	if (g_str_has_prefix (uri, "http://") &&
	    g_object_class_find_property (G_OBJECT_GET_CLASS (stream->source),
		    			  "iradio-mode")) {
		g_object_set (stream->source, "iradio-mode", TRUE, NULL);
	}

	if (g_getenv ("USE_DECODEBIN2") != NULL) {
		stream->decoder = gst_element_factory_make ("decodebin2", NULL);
	}

	if (stream->decoder == NULL) {
		stream->decoder = gst_element_factory_make ("decodebin", NULL);
	}

	if (stream->decoder == NULL) {
		rb_debug ("unable to create decodebin");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->decoder);

	/* connect decodebin to audioconvert when it creates its output pad */
	g_signal_connect_object (stream->decoder,
				 "new-decoded-pad",
				 G_CALLBACK (stream_new_decoded_pad_cb),
				 stream,
				 0);

	stream->audioconvert = gst_element_factory_make ("audioconvert", NULL);
	if (stream->audioconvert == NULL) {
		rb_debug ("unable to create audio converter");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->audioconvert);

	stream->audioresample = gst_element_factory_make ("audioresample", NULL);
	if (stream->audioresample == NULL) {
		rb_debug ("unable to create audio resample");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->audioresample);

	stream->capsfilter = gst_element_factory_make ("capsfilter", NULL);
	if (stream->capsfilter == NULL) {
		rb_debug ("unable to create capsfilter");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->capsfilter);

	caps = gst_caps_new_simple ("audio/x-raw-int",
				    "channels", G_TYPE_INT, 2,
				    "rate",	G_TYPE_INT, 44100,
				    "width",	G_TYPE_INT, 16,
				    "depth",	G_TYPE_INT, 16,
				    NULL);
	g_object_set (stream->capsfilter, "caps", caps, NULL);
	gst_caps_unref (caps);

	stream->volume = gst_element_factory_make ("volume", NULL);
	if (stream->volume == NULL) {
		rb_debug ("unable to create volume control");
		g_object_unref (stream);
		return NULL;
	}
	gst_object_ref (stream->volume);

	g_signal_connect_object (stream->volume,
				 "notify::volume",
				 G_CALLBACK (volume_changed_cb),
				 player, 0);

	stream->fader = gst_object_control_properties (G_OBJECT (stream->volume), "volume", NULL);
	if (stream->fader == NULL) {
		rb_debug ("unable to create volume controller");
		g_object_unref (stream);
		return NULL;
	}
	gst_controller_set_interpolation_mode (stream->fader, "volume", GST_INTERPOLATE_LINEAR);

	stream->preroll = gst_element_factory_make ("queue", NULL);
	if (stream->preroll == NULL) {
		rb_debug ("unable to create preroll queue");
		g_object_unref (stream);
		return NULL;
	}
	/* decode a couple of seconds during prerolling, to hopefully avoid underruns.
	 * we clear this when prerolling is finished.
	 */
	g_object_set (stream->preroll, "min-threshold-buffers", 10, NULL);

	/* probably could stand to make this check a bit smarter..
	 */
	if (rb_uri_is_local (stream->uri) == FALSE) {

		stream->queue = gst_element_factory_make ("queue", NULL);
		if (stream->queue == NULL) {
			rb_debug ("unable to create queue");
			g_object_unref (stream);
			return NULL;
		}
		gst_object_ref (stream->queue);

		/* set buffer size */
		stream->queue_threshold = player->priv->buffer_size * 1024;
		g_object_set (stream->queue,
			      "min-threshold-bytes",
			      stream->queue_threshold,
			      "max-size-bytes",
			      MAX_NETWORK_BUFFER_SIZE * 2 * 1024,
			      "max-size-buffers", 0,
			      "max-size-time", 0,
			      NULL);

		stream->underrun_id =
			g_signal_connect_object (stream->queue,
						 "underrun",
						 G_CALLBACK (stream_queue_underrun_cb),
						 stream, 0);

		gst_bin_add_many (GST_BIN (stream->bin),
				  stream->source,
				  stream->queue,
				  stream->decoder,
				  stream->audioconvert,
				  stream->audioresample,
				  stream->capsfilter,
				  stream->preroll,
				  stream->volume,
				  NULL);
		gst_element_link_many (stream->source,
				       stream->queue,
				       stream->decoder,
				       NULL);
		gst_element_link_many (stream->audioconvert,
				       stream->audioresample,
				       stream->capsfilter,
				       stream->preroll,
				       stream->volume,
				       NULL);
	} else {
		gst_bin_add_many (GST_BIN (stream->bin),
				  stream->source,
				  stream->decoder,
				  stream->audioconvert,
				  stream->audioresample,
				  stream->capsfilter,
				  stream->preroll,
				  stream->volume,
				  NULL);
		gst_element_link_many (stream->source,
				       stream->decoder,
				       NULL);
		gst_element_link_many (stream->audioconvert,
				       stream->audioresample,
				       stream->capsfilter,
				       stream->preroll,
				       stream->volume,
				       NULL);
	}

	/* ghost the volume src pad up to the bin */
	stream->src_pad = gst_element_get_pad (stream->volume, "src");
	stream->ghost_pad = gst_ghost_pad_new ("src", stream->src_pad);
	gst_element_add_pad (stream->bin, stream->ghost_pad);

	/* watch for EOS events using a pad probe */
	gst_pad_add_event_probe (stream->src_pad, (GCallback) stream_src_event_cb, stream);

	/* use the pipeline bus even when not inside the pipeline (?) */
	gst_element_set_bus (stream->bin, gst_element_get_bus (player->priv->pipeline));

	return stream;
}

/* starts playback for a stream.
 * - links to adder and unblocks
 * - if crossfading:
 *   - starts the fade in of the new stream
 *   - starts the fade out of the old stream
 *   - sets the stream to PLAYING state
 * - if following (crossfade == 0)
 *   - if something is playing, set the stream to wait-eos state
 *   - otherwise, starts it
 * - if replacing (crossfade == -1)
 *   - stops any existing stream
 *   - starts the new stream
 */
static gboolean
actually_start_stream (RBXFadeStream *stream, GError **error)
{
	RBPlayerGstXFade *player = stream->player;
	gboolean ret = TRUE;

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	rb_debug ("going to start playback for stream %s (crossfade %d) -> FADING_IN | PLAYING", stream->uri, stream->crossfade);
	if (stream->crossfade > 0) {
		GList *l;

		for (l = player->priv->streams; l != NULL; l = l->next) {
			RBXFadeStream *pstream = (RBXFadeStream *)l->data;

			double fade_out_start = 1.0f;
			gint64 fade_out_time = stream->crossfade * GST_SECOND;

			if (pstream == stream)
				continue;

			switch (pstream->state) {
			case FADING_IN:
				/* fade out from where the fade in got up to */
				g_object_get (pstream->volume, "volume", &fade_out_start, NULL);
				fade_out_time = (gint64)(((double) stream->crossfade) * fade_out_start) * GST_SECOND;
				/* fall through */
			case PLAYING:
				rb_debug ("stream %s is playing; crossfading -> FADING_OUT", pstream->uri);
				start_stream_fade (pstream, fade_out_start, 0.0f, fade_out_time);
				pstream->state = FADING_OUT;

				start_stream_fade (stream, 0.0f, 1.0f, stream->crossfade * GST_SECOND);
				break;
			case PAUSED:
				rb_debug ("stream %s is paused; replacing it", pstream->uri);
				unlink_and_dispose_stream (player, pstream);
				break;
			default:
				break;
			}
		}

		if (stream->fading == FALSE) {
			GValue v = {0,};

			rb_debug ("stream isn't fading; setting volume to 1.0");
			g_value_init (&v, G_TYPE_DOUBLE);
			g_value_set_double (&v, 1.0);
			if (gst_controller_set (stream->fader, "volume", 0, &v) == FALSE) {
				rb_debug ("controller didn't like our start point");
			}
			g_value_unset (&v);
		}

		ret = link_and_unblock_stream (stream, error);
	} else if (stream->crossfade == 0) {
		GList *l;
		gboolean playing = FALSE;
		for (l = player->priv->streams; l != NULL; l = l->next) {
			RBXFadeStream *pstream = (RBXFadeStream *)l->data;
			if (pstream == stream)
				continue;

			switch (pstream->state) {
			case PLAYING:
			case FADING_IN:
			case FADING_OUT:
				rb_debug ("stream %s is already playing", pstream->uri);
				playing = TRUE;
				break;
			case PAUSED:
				rb_debug ("stream %s is paused; replacing it", pstream->uri);
				unlink_and_dispose_stream (player, pstream);
				break;
			default:
				break;
			}
		}

		if (playing) {
			/* wait for current stream's EOS */
			rb_debug ("existing playing stream found; waiting for its EOS -> WAITING_EOS");
			stream->state = WAITING_EOS;
		} else {
			rb_debug ("no playing stream found, so starting immediately");
			ret = link_and_unblock_stream (stream, error);
		}
	} else {
		/* replace any existing playing stream.
		 * might need to use schedule_stream_reap instead of
		 * doing it directly, since we can get in here on a streaming
		 * thread..  hmm.
		 */
		GList *l;
		for (l = player->priv->streams; l != NULL; l = l->next) {
			RBXFadeStream *pstream = (RBXFadeStream *)l->data;
			if (pstream == stream)
				continue;

			switch (pstream->state) {
			case PLAYING:
			case PAUSED:
			case FADING_IN:
				/* kill this one */
				rb_debug ("stopping stream %s (replaced by new stream)", pstream->uri);
				unlink_and_dispose_stream (player, pstream);
				break;

			default:
				/* let it go */
				break;
			}
		}

		ret = link_and_unblock_stream (stream, error);
	}

	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	return ret;
}

/* called on a streaming thread when the stream src pad is blocked
 * (that is, when prerolling is complete).  in some situations we
 * start playback immediately, otherwise we wait for something else
 * to happen.
 */
static void
stream_src_blocked_cb (GstPad *pad, gboolean blocked, RBXFadeStream *stream)
{
	GError *error = NULL;
	stream->src_blocked = TRUE;

	g_object_set (stream->preroll, "min-threshold-buffers", 0, NULL);

	/* update stream state */
	switch (stream->state) {
	case PREROLLING:
		rb_debug ("stream %s is prerolled, not starting yet -> WAITING", stream->uri);
		stream->state = WAITING;
		break;
	case PREROLL_PLAY:
		/* not sure this is actually an acceptable thing to do on a streaming thread.. */
		rb_debug ("stream %s is prerolled, need to start it", stream->uri);
		if (actually_start_stream (stream, &error) == FALSE) {
			emit_stream_error (stream, error);
		}
		break;
	default:
		rb_debug ("didn't expect to get preroll completion callback in this state (%d)", stream->state);
		break;
	}
}

/*
 * starts prerolling for a stream.
 * since the stream isn't linked to anything yet, we
 * block the src pad.  when the pad block callback
 * is called, prerolling is complete and the stream
 * can be linked and played immediately if required.
 *
 * must be called *without* the stream list lock?
 */
static gboolean
preroll_stream (RBPlayerGstXFade *player, RBXFadeStream *stream)
{
	GstStateChangeReturn state;
	gboolean ret = TRUE;
	gboolean unblock = FALSE;

	gst_pad_set_blocked_async (stream->src_pad,
				   TRUE,
				   (GstPadBlockCallback) stream_src_blocked_cb,
				   stream);

	stream->emitted_playing = FALSE;
	stream->state = PREROLLING;
	state = gst_element_set_state (stream->bin, GST_STATE_PAUSED);
	switch (state) {
	case GST_STATE_CHANGE_FAILURE:
		rb_debug ("preroll for stream %s failed (state change failed)", stream->uri);
		ret = FALSE;
		unblock = TRUE;
		break;
	case GST_STATE_CHANGE_NO_PREROLL:
		rb_debug ("no preroll for stream %s -> WAITING", stream->uri);
		unblock = TRUE;
		stream->state = WAITING;
		break;
	case GST_STATE_CHANGE_SUCCESS:
		rb_debug ("stream %s prerolled synchronously -> WAITING", stream->uri);
		stream->state = WAITING;
		/* expect pad block callback to have been called */
		g_assert (stream->src_blocked);
		break;
	case GST_STATE_CHANGE_ASYNC:
		stream->state = PREROLLING;
		break;
	default:
		g_assert_not_reached();
	}

	if (unblock) {
		rb_debug ("unblocking stream source pad");
		gst_pad_set_blocked_async (stream->src_pad,
					   FALSE,
					   NULL,
					   NULL);
	}

	return ret;
}

/*
 * returns the RBXFadeStream, playback position, and duration of the current
 * playing stream.
 */
static gboolean
get_times_and_stream (RBPlayerGstXFade *player, RBXFadeStream **pstream, gint64 *pos, gint64 *duration)
{
	gboolean got_time = FALSE;
	RBXFadeStream *stream;

	if (player->priv->pipeline == NULL)
		return FALSE;

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	stream = find_stream_by_state (player, FADING_IN | PLAYING | PAUSED);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	if (stream != NULL) {
		if (pstream != NULL) {
			*pstream = stream;
		}

		if (pos != NULL) {
			if (stream->state == PAUSED) {
				GstFormat format = GST_FORMAT_TIME;
				*pos = -1;

				gst_element_query_position (stream->volume, &format, pos);
				if (*pos != -1) {
					*pos /= GST_SECOND;
				}
			} else {
				/* for playing streams, we subtract the current output position
				 * (a running counter generated by the adder) from the position
				 * at which we started playback.
				 */
				GstFormat format = GST_FORMAT_TIME;
				*pos = -1;
				gst_element_query_position (player->priv->pipeline, &format, pos);
				if (*pos != -1) {
					*pos -= stream->base_time;
					*pos /= GST_SECOND;
				} else {
					rb_debug ("position query failed");
				}
			}
		}

		if (duration != NULL) {
			GstFormat format = GST_FORMAT_TIME;
			*duration = -1;
			/* queries are supposed to go to sinks, but the closest thing we
			 * have in the stream bin is the volume element, which is the last
			 * linked element.
			 */
			gst_element_query_duration (stream->volume, &format, duration);
			if (*duration != -1) {
				*duration /= GST_SECOND;
			}
		}
		got_time = TRUE;
		g_object_unref (stream);
	} else {
		rb_debug ("not playing");
	}

	return got_time;
}

static gboolean
tick_timeout (RBPlayerGstXFade *player)
{
	gint64 pos = -1;
	gint64 duration = -1;
	RBXFadeStream *stream = NULL;

	if (get_times_and_stream (player, &stream, &pos, &duration)) {
		_rb_player_emit_tick (RB_PLAYER (player), stream->stream_data, pos, duration);
	}

	return TRUE;
}

/*
 * output sink + adder pipeline:
 *
 * outputcaps = audio/x-raw-int,channels=2,rate=44100,width=16,depth=16
 * outputbin = outputcaps ! volume ! filterbin ! audioconvert ! audioresample ! tee ! queue ! gconfaudiosink
 * silencebin = audiotestsrc wave=silence ! outputcaps
 *
 * pipeline = silencebin ! adder ! outputbin
 *
 * the tee in output bin has branches attached to it using the
 * RBPlayerGstTee interface.  filterbin contains elements inserted
 * using the RBPlayerGstFilter interface.
 *
 * silencebin is there for two reasons:
 * - lets us start the sink without having any streams playing
 * - probably helps keep things from falling over between streams
 */

static gboolean
start_sink (RBPlayerGstXFade *player)
{
	GstStateChangeReturn sr;
	GstState state;
	gboolean wait = TRUE;

	switch (player->priv->sink_state) {
	case SINK_NULL:
		/* oops */
		g_assert_not_reached ();
		break;
	case SINK_STOPPED:
		rb_debug ("starting sink");
		/* first, start the output bin.
		 * this won't preroll until we start the silence bin.
		 */
		sr = gst_element_set_state (player->priv->outputbin, GST_STATE_PAUSED);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("output bin state change failed");
			return FALSE;
		}

		/* then the adder */
		sr = gst_element_set_state (player->priv->adder, GST_STATE_PAUSED);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("adder state change failed");
			return FALSE;
		}

		/* then the silence bin */
		sr = gst_element_set_state (player->priv->silencebin, GST_STATE_PAUSED);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("silence bin state change failed");
			return FALSE;
		}

		/* now wait for everything to finish */
		sr = gst_element_get_state (player->priv->silencebin, &state, NULL, GST_CLOCK_TIME_NONE);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("silence bin state change failed (async)");
			return FALSE;
		}
		rb_debug ("silence bin is now in state %s", gst_element_state_get_name (state));

		sr = gst_element_get_state (player->priv->adder, &state, NULL, GST_CLOCK_TIME_NONE);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("adder state change failed (async)");
			return FALSE;
		}
		rb_debug ("adder is now in state %s", gst_element_state_get_name (state));

		sr = gst_element_get_state (player->priv->outputbin, &state, NULL, GST_CLOCK_TIME_NONE);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("output bin state change failed (async)");
			return FALSE;
		}
		rb_debug ("output bin is now in state %s", gst_element_state_get_name (state));

		wait = FALSE;
		/* fall through */
	case SINK_PAUSED:
		rb_debug ("unpausing sink");

		sr = gst_element_set_state (player->priv->silencebin, GST_STATE_PLAYING);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("silence bin state change failed");
			return FALSE;
		}

		sr = gst_element_set_state (player->priv->adder, GST_STATE_PLAYING);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("adder state change failed");
			return FALSE;
		}

		sr = gst_element_set_state (player->priv->outputbin, GST_STATE_PLAYING);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("output bin state change failed");
			return FALSE;
		}

		if (wait && sr == GST_STATE_CHANGE_ASYNC) {
			sr = gst_element_get_state (player->priv->outputbin, &state, NULL, GST_CLOCK_TIME_NONE);
			if (sr == GST_STATE_CHANGE_FAILURE) {
				rb_debug ("output bin state change failed (async)");
				return FALSE;
			}
		}

		rb_debug ("sink playing");
		player->priv->sink_state = SINK_PLAYING;

		/* now that the sink is running, start polling for playing position.
		 * might want to replace this with a complicated set of pad probes
		 * to avoid polling, but duration queries on the sink are better
		 * as they account for internal buffering etc.  maybe there's a way
		 * to account for that in a pad probe callback on the sink's sink pad?
		 */
		if (player->priv->tick_timeout_id == 0) {
			gint ms_period = 1000 / RB_PLAYER_GST_XFADE_TICK_HZ;
			player->priv->tick_timeout_id =
				g_timeout_add (ms_period,
					      (GSourceFunc) tick_timeout,
					      player);
		}
	case SINK_PLAYING:
		break;
	}

	return TRUE;
}

static gboolean
stop_sink (RBPlayerGstXFade *player)
{
	GstStateChangeReturn sr;

	switch (player->priv->sink_state) {
	case SINK_PLAYING:
	case SINK_PAUSED:
		rb_debug ("stopping sink");

		if (player->priv->tick_timeout_id != 0) {
			g_source_remove (player->priv->tick_timeout_id);
			player->priv->tick_timeout_id = 0;
		}

		sr = gst_element_set_state (player->priv->silencebin, GST_STATE_READY);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("couldn't stop silence bin");
			return FALSE;
		}

		sr = gst_element_set_state (player->priv->adder, GST_STATE_READY);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("couldn't stop silence bin");
			return FALSE;
		}

		sr = gst_element_set_state (player->priv->outputbin, GST_STATE_READY);
		if (sr == GST_STATE_CHANGE_FAILURE) {
			rb_debug ("couldn't stop silence bin");
			return FALSE;
		}
		player->priv->sink_state = SINK_STOPPED;
		break;

	case SINK_STOPPED:
	case SINK_NULL:
		break;
	}

	return TRUE;
}

static gboolean
pause_sink (RBPlayerGstXFade *player)
{
	switch (player->priv->sink_state) {
	case SINK_PLAYING:
		rb_debug ("pausing sink");
		gst_element_set_state (player->priv->outputbin, GST_STATE_PAUSED);
		/* wait for state change? */
		player->priv->sink_state = SINK_PAUSED;
		return TRUE;

	case SINK_PAUSED:
		rb_debug ("sink already paused");
		return TRUE;

	case SINK_NULL:
	case SINK_STOPPED:
		rb_debug ("sink doesn't need pausing");
		return TRUE;
	default:
		g_assert_not_reached ();
	}

}

static gboolean
create_sink (RBPlayerGstXFade *player, GError **error)
{
	GstElement *audiotestsrc;
	GstElement *audioconvert;
	GstElement *audioconvert2;
	GstElement *audioresample;
	GstElement *capsfilter;
	GstElement *queue;
	GstCaps *caps;
	GstPad *pad;
	GstPad *filterpad;
	GstPad *outputghostpad;
	GstPad *ghostpad;
	GstPad *reqpad;
	GstPad *addersrcpad;
	GstPadLinkReturn plr;
	GList *l;

	if (player->priv->pipeline != NULL)
		return TRUE;

	/* set filter caps.
	 * 44100Hz is about the most reasonable thing to use;
	 * we have audioconvert+audioresample afterwards in
	 * case the output device doesn't actually support
	 * that rate.
	 */
	caps = gst_caps_new_simple ("audio/x-raw-int",
				    "channels", G_TYPE_INT, 2,
				    "rate",	G_TYPE_INT, 44100,
				    "width",	G_TYPE_INT, 16,
				    "depth",	G_TYPE_INT, 16,
				    NULL);

	player->priv->pipeline = gst_pipeline_new ("rbplayer");
	gst_bus_add_watch (gst_element_get_bus (GST_ELEMENT (player->priv->pipeline)),
			     (GstBusFunc) rb_player_gst_xfade_bus_cb, player);

	player->priv->outputbin = gst_bin_new ("outputbin");
	player->priv->adder = gst_element_factory_make ("adder", "outputadder");
	player->priv->capsfilter = gst_element_factory_make ("capsfilter", "outputcapsfilter");
	audioconvert = gst_element_factory_make ("audioconvert", "outputconvert");
	audioresample = gst_element_factory_make ("audioresample", "outputresample");
	player->priv->tee = gst_element_factory_make ("tee", "outputtee");
	queue = gst_element_factory_make ("queue", NULL);
	player->priv->volume = gst_element_factory_make ("volume", "outputvolume");
	player->priv->filterbin = gst_bin_new ("filterbin");
	if (player->priv->pipeline == NULL ||
	    player->priv->adder == NULL ||
	    player->priv->capsfilter == NULL ||
	    audioconvert == NULL ||
	    audioresample == NULL ||
	    player->priv->tee == NULL ||
	    queue == NULL ||
	    player->priv->volume == NULL ||
	    player->priv->filterbin == NULL) {
		/* we could include the element name in the error message,
		 * but these are all fundamental elements that are always
		 * available.
		 */
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create GStreamer element; check your installation"));
		return FALSE;
	}

	g_object_set (player->priv->volume, "volume", player->priv->cur_volume, NULL);

	player->priv->sink = gst_element_factory_make ("gconfaudiosink", NULL);
	if (player->priv->sink == NULL) {
		player->priv->sink = gst_element_factory_make ("autoaudiosink", NULL);
		if (player->priv->sink == NULL) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_GENERAL,
				     _("Failed to create audio output element; check your installation"));
			return FALSE;
		}
	} else {
		/* set the profile property on the gconfaudiosink to "music and movies" */
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (player->priv->sink), "profile"))
			g_object_set (player->priv->sink, "profile", 1, NULL);
	}

	g_object_set (player->priv->capsfilter, "caps", caps, NULL);

	/* set up initial filterbin contents */
	audioconvert2 = gst_element_factory_make ("audioconvert", NULL);
	gst_bin_add (GST_BIN (player->priv->filterbin), audioconvert2);

	pad = gst_element_get_pad (audioconvert2, "sink");
	gst_element_add_pad (player->priv->filterbin,
			     gst_ghost_pad_new ("sink", pad));
	gst_object_unref (pad);

	pad = gst_element_get_pad (audioconvert2, "src");
	gst_element_add_pad (player->priv->filterbin,
			     gst_ghost_pad_new ("src", pad));
	gst_object_unref (pad);

	g_object_set (queue, "max-size-buffers", 10, NULL);

	gst_bin_add_many (GST_BIN (player->priv->outputbin),
			  player->priv->capsfilter,
			  player->priv->volume,
			  player->priv->filterbin,
			  audioconvert,
			  audioresample,
			  player->priv->tee,
			  queue,
			  player->priv->sink,
			  NULL);
	if (gst_element_link_many (player->priv->capsfilter,
			       player->priv->volume,
			       player->priv->filterbin,
			       audioconvert,
			       audioresample,
			       player->priv->tee,
			       queue,
			       player->priv->sink,
			       NULL) == FALSE) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link GStreamer pipeline; check your installation"));
		return FALSE;
	}

	filterpad = gst_element_get_pad (player->priv->capsfilter, "sink");
	outputghostpad = gst_ghost_pad_new ("sink", filterpad);
	gst_element_add_pad (player->priv->outputbin, outputghostpad);
	gst_object_unref (filterpad);

	/* create silence bin */
	player->priv->silencebin = gst_bin_new ("silencebin");
	audiotestsrc = gst_element_factory_make ("audiotestsrc", "silence");
	g_object_set (audiotestsrc, "wave", 4, NULL);

	audioconvert = gst_element_factory_make ("audioconvert", "silenceconvert");

	capsfilter = gst_element_factory_make ("capsfilter", "silencecapsfilter");
	g_object_set (capsfilter, "caps", caps, NULL);
	gst_caps_unref (caps);

	if (audiotestsrc == NULL ||
	    audioconvert == NULL ||
	    capsfilter == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create GStreamer element; check your installation"));
		return FALSE;
	}

	gst_bin_add_many (GST_BIN (player->priv->silencebin),
			  audiotestsrc,
			  audioconvert,
			  capsfilter,
			  NULL);
	if (gst_element_link_many (audiotestsrc,
				   audioconvert,
				   capsfilter,
				   NULL) == FALSE) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link GStreamer pipeline; check your installation"));
		return FALSE;
	}

	filterpad = gst_element_get_pad (capsfilter, "src");
	ghostpad = gst_ghost_pad_new (NULL, filterpad);
	gst_element_add_pad (player->priv->silencebin, ghostpad);
	gst_object_unref (filterpad);

	/* assemble stuff:
	 * - add everything to the pipeline
	 * - link adder to output bin
	 * - link silence bin to adder
	 */
	gst_bin_add_many (GST_BIN (player->priv->pipeline),
			  player->priv->adder,
			  player->priv->outputbin,
			  player->priv->silencebin,
			  NULL);

	addersrcpad = gst_element_get_pad (player->priv->adder, "src");
	plr = gst_pad_link (addersrcpad, outputghostpad);
	if (plr != GST_PAD_LINK_OK) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link GStreamer pipeline; check your installation"));
		return FALSE;
	}

	reqpad = gst_element_get_request_pad (player->priv->adder, "sink%d");
	if (reqpad == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link GStreamer pipeline; check your installation"));
		return FALSE;
	}

	plr = gst_pad_link (ghostpad, reqpad);
	if (plr != GST_PAD_LINK_OK) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to link GStreamer pipeline; check your installation"));
		return FALSE;
	}

	/* add any tees and filters that were waiting for us */
	for (l = player->priv->waiting_tees; l != NULL; l = g_list_next (l)) {
		rb_player_gst_tee_add_tee (RB_PLAYER_GST_TEE (player), GST_ELEMENT (l->data));
	}
	g_list_free (player->priv->waiting_tees);
	player->priv->waiting_tees = NULL;

	for (l = player->priv->waiting_filters; l != NULL; l = g_list_next (l)) {
		rb_player_gst_filter_add_filter (RB_PLAYER_GST_FILTER (player), GST_ELEMENT (l->data));
	}
	g_list_free (player->priv->waiting_filters);
	player->priv->waiting_filters = NULL;

	player->priv->sink_state = SINK_STOPPED;
	return TRUE;
}



static gboolean
rb_player_gst_xfade_open (RBPlayer *iplayer,
			  const char *uri,
			  gpointer stream_data,
			  GDestroyNotify stream_data_destroy,
			  GError **error)
{
	RBXFadeStream *stream;
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	gboolean reused = FALSE;
	GList *t;

	/* create sink if we don't already have one */
	if (create_sink (player, error) == FALSE)
		return FALSE;

	/* see if anyone wants us to reuse an existing stream */
	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	for (t = player->priv->streams; t != NULL; t = t->next) {
		RBXFadeStream *stream = (RBXFadeStream *)t->data;

		switch (stream->state) {
		case WAITING:
		case PENDING_REMOVE:
		case REUSING:
		case SEEKING:
		case SEEKING_PAUSED:
		case PREROLLING:
		case PREROLL_PLAY:
			break;

		case PLAYING:
		case FADING_IN:
		case FADING_OUT:
		case WAITING_EOS:
		case PAUSED:
			g_signal_emit (player,
				       signals[CAN_REUSE_STREAM], 0,
				       uri, stream->uri, stream->bin,
				       &reused);
			break;
		}

		if (reused) {
			rb_debug ("reusing stream %s for new stream %s", stream->uri, uri);
			stream->new_uri = g_strdup (uri);
			stream->new_stream_data = stream_data;
			stream->new_stream_data_destroy = stream_data_destroy;

			if (stream->state == PAUSED) {
				reuse_stream (stream);
			} else {
				stream->state = REUSING;
				unlink_and_block_stream (stream);
			}
			break;
		}
	}
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
	if (reused) {
		return TRUE;
	}

	/* construct new stream */
	stream = create_stream (player, uri, stream_data, stream_data_destroy);
	if (stream == NULL) {
		char *err;
		err = g_strdup_printf (_("Failed to create GStreamer pipeline to play %s"),
				       uri);
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     err);
		g_free (err);
		return FALSE;
	}

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	player->priv->streams = g_list_prepend (player->priv->streams, stream);
	dump_stream_list (player);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	/* start prerolling it */
	if (preroll_stream (player, stream) == FALSE) {
		char *err;

		/* need to set errors here etc. .. */
		g_static_rec_mutex_lock (&player->priv->stream_list_lock);
		player->priv->streams = g_list_remove (player->priv->streams, stream);
		g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

		g_object_unref (stream);

		err = g_strdup_printf (_("Failed to start playback of %s"),
				       uri);
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     err);
		g_free (err);
		return FALSE;
	}

	return TRUE;
}


static gboolean
rb_player_gst_xfade_close (RBPlayer *iplayer, const char *uri, GError **error)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	gboolean ret = TRUE;

	if (uri == NULL) {
		GList *list;
		GList *l;

		if (player->priv->sink_state == SINK_PAUSED)
			start_sink (player);

		/* need to copy the list as unlink_and_dispose_stream modifies it */
		g_static_rec_mutex_lock (&player->priv->stream_list_lock);
		list = g_list_copy (player->priv->streams);
		for (l = list; l != NULL; l = l->next) {
			RBXFadeStream *stream = (RBXFadeStream *)l->data;
			g_object_ref (stream);
		}
		g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

		for (l = list; l != NULL; l = l->next) {
			RBXFadeStream *stream = (RBXFadeStream *)l->data;
			unlink_and_dispose_stream (player, stream);
			g_object_unref (stream);
		}
		g_list_free (list);
	} else {
		/* just stop and close the stream for the specified uri */
		RBXFadeStream *stream;

		g_static_rec_mutex_lock (&player->priv->stream_list_lock);
		stream = find_stream_by_uri (player, uri);
		g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

		if (stream != NULL) {
			unlink_and_dispose_stream (player, stream);
			g_object_unref (stream);
		} else {
			rb_debug ("can't find stream for %s", uri);
			/* XXX set error ?*/
			ret = FALSE;
		}
	}

	if (player->priv->streams == NULL) {
		ret = stop_sink (player);
	}

	return ret;
}


static gboolean
rb_player_gst_xfade_opened (RBPlayer *iplayer)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	RBXFadeStream *stream;
	gboolean opened = FALSE;

	/* maybe replace this with just a flag somewhere? */

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	stream = find_stream_by_state (player, PREROLLING | PREROLL_PLAY | WAITING_EOS | WAITING | FADING_IN | PLAYING | PAUSED);
	if (stream != NULL) {
		opened = TRUE;
		g_object_unref (stream);
	}

	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	return opened;
}

static gboolean
rb_player_gst_xfade_play (RBPlayer *iplayer, gint crossfade, GError **error)
{
	RBXFadeStream *stream;
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	gboolean ret = TRUE;

	/* if we were paused, always replace existing streams */
	if (player->priv->sink_state == SINK_PAUSED) {
		crossfade = -1;
	}

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	/* is there anything to play? */
	if (player->priv->streams == NULL) {
		/* XXX set error */
		g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
		return FALSE;
	}

	stream = g_list_first (player->priv->streams)->data;
	g_object_ref (stream);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	rb_debug ("playing stream %s, crossfade %d", stream->uri, crossfade);

	/* is the head stream already playing? */
	switch (stream->state) {
	case FADING_IN:
	case FADING_OUT:
	case PLAYING:
	case SEEKING:
		rb_debug ("stream %s is already playing", stream->uri);
		_rb_player_emit_playing_stream (RB_PLAYER (player), stream->stream_data);
		break;

	case PREROLLING:
	case PREROLL_PLAY:
		rb_debug ("stream %s is prerolling; will start playback once prerolling is complete -> PREROLL_PLAY", stream->uri);
		stream->crossfade = crossfade;
		stream->state = PREROLL_PLAY;
		break;

	case PAUSED:
		rb_debug ("unpausing stream %s", stream->uri);
		/* consider fading in here?  and/or seek back a bit? */
		ret = link_and_unblock_stream (stream, error);
		break;

	case SEEKING_PAUSED:
		rb_debug ("unpausing seeking stream %s", stream->uri);
		stream->state = SEEKING;
		break;

	case WAITING_EOS:
	case WAITING:
		stream->crossfade = crossfade;
		ret = actually_start_stream (stream, error);
		break;

	case REUSING:
		rb_debug ("currently reusing stream %s; will play when done", stream->uri);
		break;

	case PENDING_REMOVE:
		rb_debug ("hmm, can't play streams in PENDING_REMOVE state..");
		break;
	}

	g_object_unref (stream);

	/* make sure the sink is playing */
	if (start_sink (player) == FALSE) {
		ret = FALSE;
	}

	return ret;
}

static void
rb_player_gst_xfade_pause (RBPlayer *iplayer)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	GList *l;
	gboolean done = FALSE;

	/* if we're only playing one stream, just pause the sink */
	if (rb_player_gst_xfade_in_transition (player) == FALSE) {
		pause_sink (player);
		return;
	}

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	for (l = player->priv->streams; l != NULL; l = l->next) {
		RBXFadeStream *stream;
		stream = (RBXFadeStream *)l->data;
		switch (stream->state) {
		case WAITING:
		case WAITING_EOS:
			rb_debug ("stream %s is not yet playing, can't pause", stream->uri);
			break;

		case PREROLLING:
		case PREROLL_PLAY:
			rb_debug ("stream %s is prerolling, can't pause", stream->uri);
			break;

		case REUSING:
			rb_debug ("stream %s is being reused, can't pause", stream->uri);
			break;

		case PAUSED:
		case SEEKING_PAUSED:
			rb_debug ("stream %s is already paused", stream->uri);
			done = TRUE;
			break;

		case PLAYING:
		case FADING_IN:
			rb_debug ("pausing stream %s -> PAUSED", stream->uri);
			/* consider fading out here?  or just kill the volume? */
			unlink_and_block_stream (stream);
			stream->state = PAUSED;
			done = TRUE;
			break;

		case SEEKING:
			rb_debug ("pausing seeking stream %s -> SEEKING_PAUSED", stream->uri);
			stream->state = SEEKING_PAUSED;
			done = TRUE;
			break;

		case FADING_OUT:
			rb_debug ("stream %s is fading out, can't be bothered pausing it", stream->uri);
			break;

		case PENDING_REMOVE:
			rb_debug ("stream %s is done, can't pause", stream->uri);
			break;
		}

		if (done)
			break;
	}

	if (done == FALSE)
		rb_debug ("couldn't find a stream to pause");

	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
}

static gboolean
rb_player_gst_xfade_in_transition (RBPlayerGstXFade *player)
{
	gboolean playing = FALSE;
	gboolean transition = FALSE;
	GList *l;

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	for (l = player->priv->streams; l; l = l->next) {
		RBXFadeStream *stream;

		stream = (RBXFadeStream *)l->data;
		switch (stream->state) {
		case PLAYING:
		case FADING_IN:
		case FADING_OUT:
		case SEEKING:
		case SEEKING_PAUSED:
		case REUSING:
			if (playing)
				transition = TRUE;
			else
				playing = TRUE;
			break;

		default:
			break;
		}
	}
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
	return transition;
}

static gboolean
rb_player_gst_xfade_playing (RBPlayer *iplayer)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	gboolean playing = FALSE;
	RBXFadeStream *stream;

	if (player->priv->sink_state != SINK_PLAYING)
		return FALSE;

	/* XXX maybe replace with just a flag? */

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);

	stream = find_stream_by_state (player, PLAYING | FADING_IN);
	if (stream != NULL) {
		playing = TRUE;
		g_object_unref (stream);
	}
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);
	return playing;
}


static void
rb_player_gst_xfade_set_replaygain (RBPlayer *iplayer,
				    const char *uri,
				    double track_gain, double track_peak,
				    double album_gain, double album_peak)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	RBXFadeStream *stream;
	double scale;
	double gain = 0;
	double peak = 0;

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	stream = find_stream_by_uri (player, uri);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	if (stream == NULL) {
		rb_debug ("can't find stream for %s", uri);
		return;
	}

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

	stream->replaygain_scale = scale;

	/* update the stream volume if we can */
	switch (stream->state) {
	case PLAYING:
	case PAUSED:
	case SEEKING:
	case SEEKING_PAUSED:
	case REUSING:
	case WAITING:
	case WAITING_EOS:
	case PREROLLING:
	case PREROLL_PLAY:
		g_object_set (stream->volume, "volume", stream->replaygain_scale, NULL);
		break;

	case FADING_IN:
		/* hmm.. need to reset the fade?
		 * this probably shouldn't happen anyway..
		 */
		break;

	case FADING_OUT:
	case PENDING_REMOVE:
		/* not much point doing anything here */
		break;
	}

	g_object_unref (stream);
}


static void
rb_player_gst_xfade_set_volume (RBPlayer *iplayer, float volume)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);

	if (player->priv->volume != NULL) {
		gdouble v = (gdouble)volume;

		/* maybe use a controller here for smoother changes? */
		g_object_set (player->priv->volume, "volume", v, NULL);
	}
	player->priv->cur_volume = volume;
}


static float
rb_player_gst_xfade_get_volume (RBPlayer *iplayer)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);

	return player->priv->cur_volume;
}

static gboolean
rb_player_gst_xfade_seekable (RBPlayer *iplayer)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	gboolean can_seek = TRUE;
	RBXFadeStream *stream;

	/* is this supposed to query the most recently opened stream,
	 * or the current playing stream?  I really don't know.
	 */
	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	stream = find_stream_by_state (player, FADING_IN | PAUSED | PLAYING);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	if (stream) {
		GstQuery *query = NULL;
		query = gst_query_new_seeking (GST_FORMAT_TIME);
		if (gst_element_query (stream->volume, query)) {
			gst_query_parse_seeking (query, NULL, &can_seek, NULL, NULL);
		} else {
			gst_query_unref (query);

			query = gst_query_new_duration (GST_FORMAT_TIME);
			can_seek = gst_element_query (stream->volume, query);
		}
		gst_query_unref (query);
		g_object_unref (stream);
	}

	return can_seek;
}

static void
rb_player_gst_xfade_set_time (RBPlayer *iplayer, long time)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	RBXFadeStream *stream;

	g_static_rec_mutex_lock (&player->priv->stream_list_lock);
	stream = find_stream_by_state (player, FADING_IN | PLAYING | PAUSED);
	g_static_rec_mutex_unlock (&player->priv->stream_list_lock);

	if (stream == NULL) {
		rb_debug ("got seek while no playing streams exist");
		return;
	}

	stream->seek_target = time * GST_SECOND;
	if (stream->state == PAUSED) {
		rb_debug ("seeking in paused stream %s; target %" 
		    G_GINT64_FORMAT, stream->uri, stream->seek_target);
		perform_seek (stream);
	} else {
		rb_debug ("unlinking playing stream %s to seek to %"
		    G_GINT64_FORMAT, stream->uri, stream->seek_target);
		stream->state = SEEKING;
		unlink_and_block_stream (stream);
	}

	g_object_unref (stream);
}

static long
rb_player_gst_xfade_get_time (RBPlayer *iplayer)
{
	gint64 pos = -1;
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);

	get_times_and_stream (player, NULL, &pos, NULL);
	return pos;
}

/* RBPlayerGstTee implementation */

typedef struct {
	RBPlayerGstXFade *player;
	GstElement *element;
} RBPlayerGstXFadePipelineOp;


static RBPlayerGstXFadePipelineOp *
new_pipeline_op (RBPlayerGstXFade *player, GstElement *element)
{
	RBPlayerGstXFadePipelineOp *op;
	op = g_new0 (RBPlayerGstXFadePipelineOp, 1);
	op->player = g_object_ref (player);
	op->element = g_object_ref (element);
	return op;
}

static void
free_pipeline_op (RBPlayerGstXFadePipelineOp *op)
{
	g_object_unref (op->player);
	g_object_unref (op->element);
	g_free (op);
}

static void
pipeline_op_done (GstPad *pad, gboolean blocked, gpointer nothing)
{
	rb_debug ("pipeline unblocked after op.  yay.");
}

static void
really_add_tee (GstPad *pad, gboolean blocked, RBPlayerGstXFadePipelineOp *op)
{
	GstElement *queue;
	GstElement *audioconvert;
	GstElement *bin;
	GstPad *sinkpad;
	GstPad *ghostpad;

	rb_debug ("really adding tee %p", op->element);

	/* set up containing bin */
	bin = gst_bin_new (NULL);
	queue = gst_element_factory_make ("queue", NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);

	g_object_set (queue, "max-size-buffers", 3, NULL);

	gst_bin_add_many (GST_BIN (bin), queue, audioconvert, op->element, NULL);
	gst_element_link_many (queue, audioconvert, op->element, NULL);

	/* add ghost pad */
	sinkpad = gst_element_get_pad (queue, "sink");
	ghostpad = gst_ghost_pad_new ("sink", sinkpad);
	gst_element_add_pad (bin, ghostpad);
	gst_object_unref (sinkpad);

	/* add it into the pipeline */
	gst_bin_add (GST_BIN (op->player->priv->outputbin), bin);
	gst_element_link (op->player->priv->tee, bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after adding tee");
		gst_element_set_state (bin, GST_STATE_PLAYING);
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	} else {
		gst_element_set_state (bin, GST_STATE_PAUSED);
	}

	free_pipeline_op (op);
}

static void
really_remove_tee (GstPad *pad, gboolean blocked, RBPlayerGstXFadePipelineOp *op)
{
	GstElement *bin;

	rb_debug ("really removing tee %p", op->element);

	/* find bin, remove everything */
	bin = GST_ELEMENT_PARENT (op->element);
	g_object_ref (bin);
	gst_bin_remove (GST_BIN (op->player->priv->outputbin), bin);

	gst_element_set_state (bin, GST_STATE_NULL);
	gst_bin_remove (GST_BIN (bin), op->element);
	g_object_unref (bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after removing tee");
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	}

	free_pipeline_op (op);
}

static gboolean
tee_op (RBPlayerGstXFade *player,
	GstElement *element,
	GstPadBlockCallback callback)
{
	RBPlayerGstXFadePipelineOp *op;
	GstPad *block_pad;

	op = new_pipeline_op (player, element);

	block_pad = gst_element_get_pad (player->priv->volume, "src");
	if (player->priv->sink_state == SINK_PLAYING) {
		rb_debug ("blocking the volume src pad to perform a tee operation");
		gst_pad_set_blocked_async (block_pad,
					   TRUE,
					   callback,
					   op);
	} else {
		rb_debug ("sink not playing; calling tee op directly");
		(*callback) (block_pad, FALSE, op);
	}

	gst_object_unref (block_pad);
	return TRUE;
}

static gboolean
rb_player_gst_xfade_add_tee (RBPlayerGstTee *iplayer, GstElement *element)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	if (player->priv->tee == NULL) {
		player->priv->waiting_tees = g_list_prepend (player->priv->waiting_tees, element);
		return TRUE;
	}

	return tee_op (player,
		       element,
		       (GstPadBlockCallback) really_add_tee);
}

static gboolean
rb_player_gst_xfade_remove_tee (RBPlayerGstTee *iplayer, GstElement *element)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	if (player->priv->tee == NULL) {
		gst_object_sink (element);
		player->priv->waiting_tees = g_list_remove (player->priv->waiting_tees, element);
		return TRUE;
	}

	return tee_op (RB_PLAYER_GST_XFADE (iplayer),
		       element,
		       (GstPadBlockCallback) really_remove_tee);
}


/* RBPlayerGstFilter implementation */


static void
really_add_filter (GstPad *pad,
		   gboolean blocked,
		   RBPlayerGstXFadePipelineOp *op)
{
	GstPad *binsinkpad;
	GstPad *binsrcpad;
	GstPad *realpad;
	GstPad *ghostpad;
	GstElement *bin;
	GstElement *audioconvert;
	GstIterator *sink_pads;
	gboolean sink_pad_found;
	gboolean stop_scan;
	gpointer element_sink_pad;
	GstPadLinkReturn link;

	rb_debug ("adding filter %p", op->element);

	/* find the element's first unlinked source pad */
	sink_pad_found = FALSE;
	stop_scan = FALSE;
	sink_pads = gst_element_iterate_sink_pads (op->element);
	while (!sink_pad_found && !stop_scan) {
		gpointer *esp_pointer = &element_sink_pad; /* stop type-punning warnings */
		switch (gst_iterator_next (sink_pads, esp_pointer)) {
			case GST_ITERATOR_OK:
				sink_pad_found = !gst_pad_is_linked (GST_PAD (element_sink_pad));
				break;
			case GST_ITERATOR_RESYNC:
				gst_iterator_resync (sink_pads);
				break;
			case GST_ITERATOR_ERROR:
			case GST_ITERATOR_DONE:
				stop_scan = TRUE;
				break;
		}
	}
	gst_iterator_free (sink_pads);

	if (!sink_pad_found) {
		g_warning ("Could not find a free sink pad on filter");
		return;
	}

	/* create containing bin */
	bin = gst_bin_new (NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);
	gst_bin_add_many (GST_BIN (bin), op->element, audioconvert, NULL);
	gst_element_link (op->element, audioconvert);

	/* create ghost pads */
	binsinkpad = gst_ghost_pad_new ("sink", GST_PAD (element_sink_pad));
	gst_element_add_pad (bin, binsinkpad);

	realpad = gst_element_get_pad (audioconvert, "src");
	binsrcpad = gst_ghost_pad_new ("src", realpad);
	gst_element_add_pad (bin, binsrcpad);
	gst_object_unref (realpad);

	/* chuck it into the filter bin */
	gst_bin_add (GST_BIN (op->player->priv->filterbin), bin);

	ghostpad = gst_element_get_pad (op->player->priv->filterbin, "src");
	realpad = gst_ghost_pad_get_target (GST_GHOST_PAD (ghostpad));
	gst_ghost_pad_set_target (GST_GHOST_PAD (ghostpad), binsrcpad);
	gst_object_unref (ghostpad);

	link = gst_pad_link (realpad, binsinkpad);
	gst_object_unref (realpad);
	if (link != GST_PAD_LINK_OK) {
		g_warning ("could not link new filter into pipeline");
		/* can't really do much else at this point.. */
	}

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after adding tee");
		gst_element_set_state (bin, GST_STATE_PLAYING);
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	} else {
		gst_element_set_state (bin, GST_STATE_PAUSED);
	}

	free_pipeline_op (op);
}

static void
really_remove_filter (GstPad *pad,
		      gboolean blocked,
		      RBPlayerGstXFadePipelineOp *op)
{
	GstPad *mypad;
	GstPad *prevpad, *nextpad;
	GstPad *ghostpad;
	GstPad *targetpad;
	GstElement *bin;

	/* get the containing bin and remove it */
	bin = GST_ELEMENT (gst_element_get_parent (op->element));
	if (bin == NULL) {
		return;
	}

	rb_debug ("removing filter %p", op->element);

	/* probably check return? */
	gst_element_set_state (bin, GST_STATE_NULL);

	mypad = gst_element_get_pad (bin, "sink");
	prevpad = gst_pad_get_peer (mypad);
	gst_pad_unlink (prevpad, mypad);
	gst_object_unref (mypad);

	ghostpad = gst_element_get_pad (bin, "src");
	nextpad = gst_element_get_pad (op->player->priv->filterbin, "src");

	targetpad = gst_ghost_pad_get_target (GST_GHOST_PAD (nextpad));
	if (targetpad == ghostpad) {
		/* we're at the end of the filter chain, so redirect
		 * the ghostpad to the previous element.
		 */
		gst_ghost_pad_set_target (GST_GHOST_PAD (nextpad), prevpad);
		gst_object_unref (nextpad);
	} else {
		/* we are in the middle, so link the previous and next elements */
		gst_object_unref (nextpad);
		nextpad = gst_pad_get_peer (ghostpad);
		gst_pad_unlink (ghostpad, nextpad);

		if (gst_pad_link (prevpad, nextpad) != GST_PAD_LINK_OK) {
			/* crap */
		}
	}

	gst_object_unref (prevpad);
	gst_object_unref (ghostpad);
	gst_object_unref (targetpad);
	gst_object_unref (nextpad);

	gst_bin_remove (GST_BIN (op->player->priv->filterbin), bin);
	gst_object_unref (bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after adding tee");
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	}

	free_pipeline_op (op);
}

static gboolean
filter_op (RBPlayerGstXFade *player,
	   GstElement *element,
	   GstPadBlockCallback callback)
{
	RBPlayerGstXFadePipelineOp *op;
	GstPad *block_pad;

	op = new_pipeline_op (player, element);

	block_pad = gst_element_get_pad (player->priv->filterbin, "sink");
	if (player->priv->sink_state == SINK_PLAYING) {
		rb_debug ("blocking the filterbin sink pad to perform a filter operation");
		gst_pad_set_blocked_async (block_pad,
					   TRUE,
					   callback,
					   op);
	} else {
		rb_debug ("sink not playing; calling filter op directly");
		(*callback) (block_pad, FALSE, op);
	}

	gst_object_unref (block_pad);
	return TRUE;
}

static gboolean
rb_player_gst_xfade_add_filter (RBPlayerGstFilter *iplayer, GstElement *element)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	if (player->priv->filterbin == NULL) {
		player->priv->waiting_filters = g_list_prepend (player->priv->waiting_filters, element);
		return TRUE;
	}

	return filter_op (player,
			  element,
			  (GstPadBlockCallback) really_add_filter);
}


static gboolean
rb_player_gst_xfade_remove_filter (RBPlayerGstFilter *iplayer, GstElement *element)
{
	RBPlayerGstXFade *player = RB_PLAYER_GST_XFADE (iplayer);
	if (player->priv->filterbin == NULL) {
		gst_object_sink (element);
		player->priv->waiting_filters = g_list_remove (player->priv->waiting_filters, element);
		return TRUE;
	}
	return filter_op (player,
			  element,
			  (GstPadBlockCallback) really_remove_filter);
}





