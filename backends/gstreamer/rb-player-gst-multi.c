/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@debian.org>
 *  Copyright (C) 2009,2024 Jonathan Matthew  <jonathan@d14n.org>
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

/*
 * Multi-stream playback backend
 *
 * This backend supports multiple active streams as completely separate pipelines,
 * operating on the assumption that the platform sound mechanism (usually a sound
 * server such as pipewire) will mix them appropriately.
 *
 * Each stream is a uridecodebin3 connected to any per-stream filters created
 * by plugins, then a volume element (used for crossfading and fade in/out on
 * pause/unpause) then an audio sink.  The RBPlayerGstFilter and
 * RBPlayerGstTee interfaces are not supported as they require the elements
 * added to the pipeline to persist, which isn't possible here.
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/pbutils.h>

#include "rb-debug.h"
#include "rb-util.h"

#include "rb-player.h"
#include "rb-player-gst-multi.h"
#include "rb-player-gst-helper.h"

static void rb_player_init (RBPlayerIface *iface);


G_DEFINE_TYPE_WITH_CODE(RBPlayerGstMulti, rb_player_gst_multi, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER, rb_player_init)
			)

#define RB_PLAYER_GST_TICK_HZ 5
#define STATE_CHANGE_MESSAGE_TIMEOUT 5

#define PAUSE_FADE_DURATION	(0.5 * GST_SECOND)

#define EPSILON			(0.001)
#define FADE_DONE_MESSAGE	"rb-fade-done"

enum
{
	PROP_0,
	PROP_BUS
};

enum
{
	PREPARE_SOURCE,
	CAN_REUSE_STREAM,
	REUSE_STREAM,
	MISSING_PLUGINS,
	GET_STREAM_FILTERS,
	LAST_SIGNAL
};

enum StateChangeAction {
	DO_NOTHING,
	DESTROY_STREAM,
	PAUSE_SEEK_BACK,
	PAUSE_FADE_IN,
	FINISH_TRACK_CHANGE,
	START_NEXT_STREAM,
	START_CROSSFADE
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBPlayerGstMulti;

typedef struct _RBPlayerGstMultiStream
{
	RBPlayerGstMulti *player;

	char *uri;
	gpointer stream_data;
	GDestroyNotify stream_data_destroy;

	char *next_uri;
	gpointer next_stream_data;
	GDestroyNotify next_stream_data_destroy;

	GstElement *pipeline;
	GstElement *uridecodebin;
	GstElement *volume;
	GstElement *stream_sync;
	GstElement *audioconvert;
	GstElement *audio_sink;

	GMutex eos_lock;
	GCond eos_cond;

	enum StateChangeAction state_change_action;
	GstState target_state;
	gint64 crossfade;
	GstTimedValueControlSource *fader;
	gboolean fading;
	double fade_end;

	gboolean decoder_linked;
	gboolean playing;
	gboolean buffering;
	gboolean stream_changing;
	gboolean track_change;
	gboolean emitted_image;
	gboolean emitted_error;
	gboolean finishing;

	GList *tags;

	gint volume_changed;
	gint volume_applied;

} RBPlayerGstMultiStream;

struct _RBPlayerGstMultiPrivate
{
	RBPlayerGstMultiStream *current;
	RBPlayerGstMultiStream *next;
	GList *previous;

	float cur_volume;

	guint tick_timeout_id;
	guint emit_stream_idle_id;
};

static void start_state_change (RBPlayerGstMultiStream *stream, GstState state, enum StateChangeAction action);
static void state_change_finished (RBPlayerGstMultiStream *stream, GError *error);

static void
destroy_stream (RBPlayerGstMultiStream *stream)
{
	rb_debug ("destroying stream %s", stream->uri);
	stream->player->priv->previous = g_list_remove (stream->player->priv->previous, stream);

	/* pipeline should already be in NULL state */

	g_clear_object (&stream->fader);

	if (stream->pipeline != NULL) {
		GstBus *bus;

		bus = gst_element_get_bus (stream->pipeline);
		gst_bus_remove_watch (bus);
		gst_object_unref (bus);

		g_object_unref (stream->pipeline);
	}

	g_list_free_full (stream->tags, (GDestroyNotify) gst_tag_list_unref);

	if (stream->stream_data && stream->stream_data_destroy) {
		stream->stream_data_destroy (stream->stream_data);
	}
	g_free (stream->uri);

	if (stream->next_stream_data && stream->next_stream_data_destroy) {
		stream->next_stream_data_destroy (stream->next_stream_data);
	}
	g_free (stream->next_uri);

	g_free (stream);
}

static void
stop_and_destroy_stream (RBPlayerGstMultiStream *stream)
{
	if (stream->pipeline != NULL) {
		start_state_change (stream, GST_STATE_NULL, DESTROY_STREAM);
	} else {
		destroy_stream (stream);
	}
}

static void
reuse_stream (RBPlayerGstMultiStream *stream, RBPlayerGstMultiStream *next, gboolean pending)
{
	if (pending) {
		if (stream->next_stream_data && stream->next_stream_data_destroy) {
			stream->next_stream_data_destroy (stream->next_stream_data);
		}
		stream->next_stream_data = next->stream_data;
		stream->next_stream_data_destroy = next->stream_data_destroy;

		g_free (stream->next_uri);
		stream->next_uri = g_strdup (next->uri);
	} else {
		if (stream->stream_data && stream->stream_data_destroy) {
			stream->stream_data_destroy (stream->stream_data);
		}
		stream->stream_data = next->stream_data;
		stream->stream_data_destroy = next->stream_data_destroy;

		g_free (stream->uri);
		stream->uri = g_strdup (next->uri);
	}

	stream->state_change_action = FINISH_TRACK_CHANGE;
	next->stream_data = NULL;
	next->stream_data_destroy = NULL;

	destroy_stream (next);
}

static void
reused_stream (RBPlayerGstMultiStream *stream)
{
	if (stream->stream_data && stream->stream_data_destroy) {
		stream->stream_data_destroy (stream->stream_data);
	}
	stream->stream_data = stream->next_stream_data;
	stream->stream_data_destroy = stream->next_stream_data_destroy;
	stream->next_stream_data = NULL;
	stream->next_stream_data_destroy = NULL;
}

static gboolean
about_to_finish_idle (RBPlayerGstMultiStream *stream)
{
	_rb_player_emit_eos (RB_PLAYER (stream->player), stream->stream_data, TRUE);

	g_mutex_lock (&stream->eos_lock);
	g_cond_signal (&stream->eos_cond);
	g_mutex_unlock (&stream->eos_lock);

	/* need ref counts on streams? */
	return FALSE;
}

static void
about_to_finish_cb (GstElement *uridecodebin, RBPlayerGstMultiStream *stream)
{
	if (stream != stream->player->priv->current) {
		rb_debug ("got about-to-finish for non-current stream %s", stream->uri);
		return;
	}

	/* don't handle about-to-finish for cdda */
	if (g_str_has_prefix (stream->uri, "cdda://")) {
		rb_debug ("ignoring about-to-finish for %s", stream->uri);
		return;
	}

	stream->finishing = TRUE;

	if (stream->next_uri == NULL) {
		if (stream->target_state != GST_STATE_VOID_PENDING) {
			rb_debug ("got about-to-finish for %s already being shut down", stream->uri);
			return;
		}
		rb_debug ("got about-to-finish for %s with no next uri", stream->uri);
		g_mutex_lock (&stream->eos_lock);
		g_idle_add_full (G_PRIORITY_HIGH, (GSourceFunc) about_to_finish_idle, stream, NULL);

		g_cond_wait (&stream->eos_cond, &stream->eos_lock);
		g_mutex_unlock (&stream->eos_lock);
	}

	if (stream->next_uri != NULL) {
		rb_debug ("switching uri from %s to %s", stream->uri, stream->next_uri);

		/* change stream->uri before setting uri on uridecodebin so prepare-source callbacks see the right uri */
		g_free (stream->uri);
		stream->uri = stream->next_uri;
		stream->next_uri = NULL;

		g_object_set (uridecodebin, "uri", stream->uri, NULL);

		reused_stream (stream);

		/* track change finishes on stream-start */
	}
}

static gboolean
emit_volume_changed_idle (RBPlayerGstMulti *player)
{
	_rb_player_emit_volume_changed (RB_PLAYER (player), player->priv->cur_volume);
	return FALSE;
}

static void
volume_notify_cb (GObject *element, GstObject *prop_object, GParamSpec *pspec, RBPlayerGstMultiStream *stream)
{
	gdouble v;
	g_object_get (prop_object, "volume", &v, NULL);
	stream->player->priv->cur_volume = v;

	g_idle_add ((GSourceFunc) emit_volume_changed_idle, stream->player);
}

static void
process_tag (const GstTagList *list, const gchar *tag, RBPlayerGstMultiStream *stream)
{
	RBMetaDataField field;
	GValue value = {0,};

	/* process embedded images */
	if (!g_strcmp0 (tag, GST_TAG_IMAGE) || !g_strcmp0 (tag, GST_TAG_PREVIEW_IMAGE)) {
		if (stream->emitted_image == FALSE) {
			GdkPixbuf *pixbuf;
			pixbuf = rb_gst_process_embedded_image (list, tag);
			if (pixbuf != NULL) {
				_rb_player_emit_image (RB_PLAYER (stream->player),
						       stream->stream_data,
						       pixbuf);
				g_object_unref (pixbuf);
				stream->emitted_image = TRUE;
			}
		}
	} else if (rb_gst_process_tag_string (list, tag, &field, &value)) {
		rb_debug ("emitting info field %d", field);
		_rb_player_emit_info (RB_PLAYER (stream->player),
				      stream->stream_data,
				      field,
				      &value);
		g_value_unset (&value);
	}
}

static gboolean
actually_emit_stream_and_tags (RBPlayerGstMultiStream *stream)
{
	GList *t;
	RBPlayerGstMulti *player = stream->player;

	_rb_player_emit_playing_stream (RB_PLAYER (player), stream->stream_data);

	/* process any tag lists we received while starting the stream */
	for (t = stream->tags; t != NULL; t = t->next) {
		GstTagList *tags;

		tags = (GstTagList *)t->data;
		rb_debug ("processing buffered taglist");
		gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, player);
		gst_tag_list_free (tags);
	}
	g_list_free (stream->tags);
	stream->tags = NULL;

	player->priv->emit_stream_idle_id = 0;
	return FALSE;
}

static void
emit_playing_stream_and_tags (RBPlayerGstMultiStream *stream)
{
	RBPlayerGstMulti *player = stream->player;

	if (rb_is_main_thread ()) {
		if (player->priv->emit_stream_idle_id != 0) {
			g_source_remove (player->priv->emit_stream_idle_id);
		}
		actually_emit_stream_and_tags (stream);
	} else if (player->priv->emit_stream_idle_id == 0) {
		player->priv->emit_stream_idle_id = g_idle_add ((GSourceFunc) actually_emit_stream_and_tags, stream);
	}
}

static void
handle_missing_plugin_message (RBPlayerGstMultiStream *stream, GstMessage *message)
{
	char **details;
	char **descriptions;
	char *detail;
	char *description;
	int count;

	rb_debug ("got missing-plugin message from %s: %s",
		  GST_OBJECT_NAME (GST_MESSAGE_SRC (message)),
		  gst_missing_plugin_message_get_installer_detail (message));

	/* probably need to wait to collect any subsequent missing-plugin
	 * messages, but I think we'd need to wait for state changes to do
	 * that.  for now, we can only handle a single message.
	 */
	count = 1;

	details = g_new0 (char *, count + 1);
	descriptions = g_new0 (char *, count + 1);

	detail = gst_missing_plugin_message_get_installer_detail (message);
	description = gst_missing_plugin_message_get_description (message);
	details[0] = g_strdup (detail);
	descriptions[0] = g_strdup (description);

	g_signal_emit (stream->player, signals[MISSING_PLUGINS], 0, stream->stream_data, details, descriptions);
	g_strfreev (details);
	g_strfreev (descriptions);
}

static gboolean
tick_timeout (RBPlayerGstMulti *player)
{
	if (player->priv->current && player->priv->current->playing) {
		_rb_player_emit_tick (RB_PLAYER (player),
				      player->priv->current->stream_data,
				      rb_player_get_time (RB_PLAYER (player)),	/* stupid */
				      -1);
	}
	return TRUE;
}

static void
start_tick_timeout (RBPlayerGstMulti *player)
{
	if (player->priv->tick_timeout_id == 0) {
		player->priv->tick_timeout_id =
			g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ,
				       (GSourceFunc) tick_timeout,
				       player);
	}
}

static void
stop_tick_timeout (RBPlayerGstMulti *player)
{
	if (player->priv->tick_timeout_id != 0) {
		g_source_remove (player->priv->tick_timeout_id);
		player->priv->tick_timeout_id = 0;
	}
}


static void
set_sink_volume (RBPlayerGstMultiStream *stream, float volume)
{
	GstElement *e;
	gboolean done = FALSE;

	/* ignore the deep-notify we get directly from the sink, as it causes deadlock.
	 * we still get another one anyway.
	 */
	g_signal_handlers_block_by_func (stream->audio_sink, volume_notify_cb, stream->player);

	if (GST_IS_BIN (stream->audio_sink)) {
		e = gst_bin_get_by_interface (GST_BIN (stream->audio_sink), GST_TYPE_STREAM_VOLUME);
		if (e != NULL) {
			rb_debug ("%s stream volume %f", stream->uri, volume);
			gst_stream_volume_set_volume (GST_STREAM_VOLUME (e), GST_STREAM_VOLUME_FORMAT_LINEAR, volume);
			g_object_unref (e);
			done = TRUE;
		}
	}

	if (done == FALSE) {
		e = rb_player_gst_find_element_with_property (stream->audio_sink, "volume");
		if (e != NULL) {
			rb_debug ("%s prop volume %f", stream->uri, volume);
			g_object_set (e, "volume", volume, NULL);
			done = TRUE;
		}
	}

	if (done == FALSE) {
		g_warning ("don't know how to set output volume");
	}
	g_signal_handlers_unblock_by_func (stream->audio_sink, volume_notify_cb, stream->player);
}

static void
track_change_done (RBPlayerGstMulti *player, GError *error)
{
	RBPlayerGstMultiStream *stream;

	if (player->priv->next != NULL) {
		stream = player->priv->next;
	} else {
		stream = player->priv->current;
	}

	if (error != NULL) {
		_rb_player_emit_playing_stream (RB_PLAYER (player), stream->stream_data);
		rb_debug ("track change failed: %s", error->message);
		return;
	}
	rb_debug ("track change finished");

	if (player->priv->next != NULL && player->priv->current != NULL)
		player->priv->previous = g_list_prepend (player->priv->previous, player->priv->current);

	player->priv->current = stream;
	player->priv->next = NULL;

	emit_playing_stream_and_tags (stream);

	start_tick_timeout (player);
}

static void
start_state_change (RBPlayerGstMultiStream *stream, GstState state, enum StateChangeAction action)
{
	GstStateChangeReturn scr;

	rb_debug ("changing %s state to %s", stream->uri, gst_element_state_get_name (state));
	stream->state_change_action = action;
	stream->target_state = state;
	scr = gst_element_set_state (stream->pipeline, state);
	if (scr == GST_STATE_CHANGE_SUCCESS) {
		rb_debug ("state change succeeded synchronously");
		if (state == GST_STATE_NULL) {
			state_change_finished (stream, NULL);
		}
	}
}


static void
stream_volume_changed_cb (GObject *object, GParamSpec *pspec, RBPlayerGstMultiStream *stream)
{
	GstStructure *s;
	GstMessage *msg;
	GstEvent *event;
	gdouble vol;

	if (stream->fading == FALSE)
		return;

	g_object_get (stream->volume, "volume", &vol, NULL);

	if (fabs (vol - stream->fade_end) < EPSILON) {
		stream->fading = FALSE;

		rb_debug ("stream %s: posting fade-done message", stream->uri);
		s = gst_structure_new_empty (FADE_DONE_MESSAGE);
		msg = gst_message_new_application (NULL, s);

		event = gst_event_new_sink_message (FADE_DONE_MESSAGE, msg);
		gst_element_send_event (stream->volume, event);

		gst_timed_value_control_source_unset_all (stream->fader);

		gst_message_unref (msg);
	}
}


static void
start_stream_fade (RBPlayerGstMultiStream *stream, double start, double end, gint64 duration, enum StateChangeAction action)
{
	gint64 pos = GST_CLOCK_TIME_NONE;

	gst_element_query_position (stream->volume, GST_FORMAT_TIME, &pos);
	if (pos == GST_CLOCK_TIME_NONE)
		pos = 0;

	rb_debug ("fading stream %s: [%f %" G_GINT64_FORMAT "] to [%f %" G_GINT64_FORMAT "]",
		  stream->uri, (float)start, pos, (float)end, pos + duration);
	g_signal_handlers_block_by_func (stream->volume, stream_volume_changed_cb, stream);

	stream->fade_end = end;
	gst_timed_value_control_source_unset_all (stream->fader);
	gst_timed_value_control_source_set (stream->fader, pos, start/10.0);
	gst_timed_value_control_source_set (stream->fader, 0, start/10.0);	/* maybe don't need this? */
	gst_timed_value_control_source_set (stream->fader, pos + duration, end/10.0);

	g_signal_handlers_unblock_by_func (stream->volume, stream_volume_changed_cb, stream);

	stream->fading = TRUE;
	stream->state_change_action = action;
	/* make sure the controlled element isn't in passthrough mode so it actually updates */
	gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (stream->volume), FALSE);
}

static void
apply_sink_volume (RBPlayerGstMultiStream *stream, gdouble volume)
{
	GstElement *e;

	if (stream->volume_applied != 0) {
		return;
	}

	/* if the sink provides volume control, ignore the first
	 * volume setting, allowing the sink to restore its own
	 * volume.
	 */
	e = rb_player_gst_find_element_with_property (stream->audio_sink, "volume");
	if (e != NULL) {
		stream->volume_applied = 1;
		gst_object_unref (e);
	}

	if (stream->volume_applied < stream->volume_changed) {
		rb_debug ("applying initial volume: %f (%d/%d)", stream->player->priv->cur_volume, stream->volume_applied, stream->volume_changed);
		set_sink_volume (stream, stream->player->priv->cur_volume);

		stream->volume_applied = stream->volume_changed;
	}
}

static void
state_change_finished (RBPlayerGstMultiStream *stream, GError *error)
{
	RBPlayerGstMultiStream *current;
	enum StateChangeAction action = stream->state_change_action;
	stream->state_change_action = DO_NOTHING;
	stream->target_state = GST_STATE_VOID_PENDING;

	switch (action) {
	case DO_NOTHING:
		break;

	case DESTROY_STREAM:
		if (error != NULL) {
			g_warning ("unable to shut down player pipeline for stream %s: %s\n", stream->uri, error->message);
		}
		destroy_stream (stream);
		break;

	case PAUSE_SEEK_BACK:
		if (error != NULL) {
			g_warning ("unable to pause playback: %s\n", error->message);
		} else {
			gint64 pos;

			gst_element_query_position (stream->pipeline, GST_FORMAT_TIME, &pos);
			if (pos == GST_CLOCK_TIME_NONE) {
				rb_debug ("unable to seek back in %s as position query failed", stream->uri);
			} else {
				if (pos > PAUSE_FADE_DURATION)
					pos -= PAUSE_FADE_DURATION;
				else
					pos = 0;

				gst_element_seek (stream->pipeline, 1.0,
						  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
						  GST_SEEK_TYPE_SET, pos,
						  GST_SEEK_TYPE_NONE, -1);
			}
			stop_tick_timeout (stream->player);
		}
		break;

	case PAUSE_FADE_IN:
		if (error != NULL) {
			g_warning ("unable to resume playback: %s\n", error->message);
		} else {
			stream->playing = TRUE;
			emit_playing_stream_and_tags (stream);

			start_tick_timeout (stream->player);
			start_stream_fade (stream, 0.0, 1.0, PAUSE_FADE_DURATION, DO_NOTHING);
		}
		break;

	case START_NEXT_STREAM:
		if (error != NULL) {
			/* do something */
		}

		apply_sink_volume (stream, stream->player->priv->cur_volume);
		current = stream->player->priv->current;
		rb_debug ("replacing %s with %s", current->uri, stream->uri);
		start_state_change (current, GST_STATE_NULL, DESTROY_STREAM);

		gst_timed_value_control_source_unset_all (stream->fader);
		gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (stream->fader), 0, 0.1);
		start_state_change (stream, GST_STATE_PLAYING, FINISH_TRACK_CHANGE);

		break;

	case START_CROSSFADE:
		if (error != NULL) {
			/* do something */
		}

		current = stream->player->priv->current;
		rb_debug ("crossfading %s to %s", current->uri, stream->uri);
		apply_sink_volume (stream, stream->player->priv->cur_volume);
		track_change_done (stream->player, error);
		start_stream_fade (stream, 0.0, 1.0, stream->crossfade, DO_NOTHING);
		start_stream_fade (current, 1.0, 0.0, stream->crossfade, DESTROY_STREAM);
		break;

	case FINISH_TRACK_CHANGE:
		apply_sink_volume (stream, stream->player->priv->cur_volume);	/* ? */
		track_change_done (stream->player, error);
		break;
	}
}

static gboolean
message_from_sink (GstElement *sink, GstMessage *message)
{
	GstElement *src;
	GstElement *match;
	char *name;

	src = GST_ELEMENT (GST_MESSAGE_SRC (message));

	if (GST_IS_BIN (sink) == FALSE) {
		return (src == sink);
	}

	name = gst_element_get_name (src);
	match = gst_bin_get_by_name (GST_BIN (sink), name);
	g_free (name);

	if (match != NULL) {
		g_object_unref (match);
		return (match == src);
	}

	return FALSE;
}

static gboolean
bus_cb (GstBus *bus, GstMessage *message, RBPlayerGstMultiStream *stream)
{
	const GstStructure *structure;
	const char *name;

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR: {
		char *debug = NULL;
		GError *error = NULL;
		GError *sig_error = NULL;
		int code;
		gboolean emit = TRUE;

		gst_message_parse_error (message, &error, &debug);

		/* If we've already got an error, ignore 'internal data flow error'
		 * type messages, as they're too generic to be helpful.
		 */
		if (stream->emitted_error &&
		    error->domain == GST_STREAM_ERROR &&
		    error->code == GST_STREAM_ERROR_FAILED) {
			rb_debug ("stream %s: ignoring generic error \"%s\"", stream->uri, error->message);
			emit = FALSE;
		}

		code = rb_gst_error_get_error_code (error);

		if (emit) {
			if (message_from_sink (stream->audio_sink, message)) {
				rb_debug ("stream %s: got error from sink: %s (%s)", stream->uri, error->message, debug);
				g_set_error (&sig_error,
					     RB_PLAYER_ERROR,
					     code,
					     /* Translators: the parameter here is an error message */
					     _("Failed to open output device: %s"),
					     error->message);
			} else {
				rb_debug ("stream %s: got error from stream: %s (%s)", stream->uri, error->message, debug);
				g_set_error (&sig_error,
					     RB_PLAYER_ERROR,
					     code,
					     "%s",
					     error->message);
			}
			state_change_finished (stream, sig_error);
			stream->emitted_error = TRUE;
			if (stream->stream_changing) {
				emit_playing_stream_and_tags (stream);
			}
			_rb_player_emit_error (RB_PLAYER (stream->player), stream->stream_data, sig_error);
		}

		/* close if not already closing */
		if (stream->uri != NULL)
			rb_player_close (RB_PLAYER (stream->player), NULL, NULL);

		g_error_free (error);
		g_free (debug);
		break;
	}

	case GST_MESSAGE_EOS:
		if (stream->fading && stream->state_change_action == DESTROY_STREAM) {
			rb_debug ("got eos for ending stream %s", stream->uri);
			start_state_change (stream, GST_STATE_NULL, DESTROY_STREAM);
		} else if (stream->next_uri != NULL) {
			RBPlayerGstMulti *player = stream->player;
			gboolean reused = FALSE;
			char *old_uri;

			old_uri = stream->uri;
			stream->uri = stream->next_uri;
			stream->next_uri = NULL;

			g_signal_emit (player, signals[CAN_REUSE_STREAM], 0, stream->uri, old_uri, GST_ELEMENT (stream->pipeline), &reused);
			if (reused) {
				rb_debug ("reusing stream %s for new stream %s", old_uri, stream->uri);
				g_signal_emit (player, signals[REUSE_STREAM], 0, stream->uri, old_uri, GST_ELEMENT (stream->pipeline));
			} else {
				rb_debug ("switching uri from %s to %s on actual eos", old_uri, stream->uri);
				g_object_set (stream->uridecodebin, "uri", stream->uri, NULL);
			}
			reused_stream (stream);
			emit_playing_stream_and_tags (stream);
			g_free (old_uri);
		} else {
			rb_debug ("ignoring eos for stream %s", stream->uri);
		}

		/* might need to handle other conditions here? */
		break;

	case GST_MESSAGE_STATE_CHANGED: {
		GstState oldstate;
		GstState newstate;
		GstState pending;
		gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
		if (GST_MESSAGE_SRC (message) == GST_OBJECT (stream->pipeline)) {
			if (pending == GST_STATE_VOID_PENDING) {
				rb_debug ("stream %s: pipeline reached state %s", stream->uri, gst_element_state_get_name (newstate));
				state_change_finished (stream, NULL);
			}
		}
		break;
	}

	case GST_MESSAGE_TAG: {
		GstTagList *tags;

		if (stream->stream_changing) {
			rb_debug ("stream %s: ignoring tags during stream change", stream->uri);
			break;
		}

		gst_message_parse_tag (message, &tags);

		rb_debug ("stream %s: processing tags", stream->uri);
		gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, stream);
		gst_tag_list_free (tags);
		break;
	}


	case GST_MESSAGE_BUFFERING: {
		gint progress;

		structure = gst_message_get_structure (message);
		if (!gst_structure_get_int (structure, "buffer-percent", &progress)) {
			g_warning ("Could not get value from BUFFERING message");
			break;
		}

		if (progress >= 100) {
			stream->buffering = FALSE;
			if (stream->playing) {
				if (stream->target_state != GST_STATE_PAUSED) {
					rb_debug ("stream %s: buffering done, setting pipeline back to PLAYING", stream->uri);
					gst_element_set_state (stream->pipeline, GST_STATE_PLAYING);
				} else {
					rb_debug ("stream %s: preroll buffering done", stream->uri);
				}
			} else {
				rb_debug ("stream %s: buffering done, leaving pipeline PAUSED", stream->uri);
			}
		} else if (stream->buffering == FALSE && stream->playing) {

			if (stream->target_state != GST_STATE_PAUSED) {
				rb_debug ("stream %s: buffering - temporarily pausing playback", stream->uri);
				gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);
			} else {
				rb_debug ("stream %s buffering while prerolling", stream->uri);
			}
			stream->buffering = TRUE;
		}

		_rb_player_emit_buffering (RB_PLAYER (stream->player), stream->stream_data, progress);
		break;
	}

	case GST_MESSAGE_APPLICATION:
		structure = gst_message_get_structure (message);
		name = gst_structure_get_name (structure);

		if (strcmp (name, FADE_DONE_MESSAGE) == 0) {
			switch (stream->state_change_action) {
			case DESTROY_STREAM:
				start_state_change (stream, GST_STATE_NULL, DESTROY_STREAM);
				break;
			case PAUSE_SEEK_BACK:
				start_state_change (stream, GST_STATE_PAUSED, PAUSE_SEEK_BACK);
				break;

			case DO_NOTHING:
				gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (stream->volume), TRUE);
				break;

			case FINISH_TRACK_CHANGE:
			case START_NEXT_STREAM:
			case START_CROSSFADE:
			case PAUSE_FADE_IN:
				g_assert_not_reached ();
				break;
			}
		} else {
			_rb_player_emit_event (RB_PLAYER (stream->player), stream->stream_data, gst_structure_get_name (structure), NULL);
		}
		break;

	case GST_MESSAGE_STREAM_START:
		if (stream->stream_changing) {
			rb_debug ("stream %s: got STREAM_START message", stream->uri);
			stream->stream_changing = FALSE;
			stream->state_change_action = DO_NOTHING;
			emit_playing_stream_and_tags (stream);
		}
		break;

	case GST_MESSAGE_ELEMENT:
		structure = gst_message_get_structure (message);
		if (gst_is_missing_plugin_message (message)) {
			handle_missing_plugin_message (stream, message);
		} else if (gst_structure_has_name (structure, "redirect")) {
			const char *uri = gst_structure_get_string (structure, "new-location");
			_rb_player_emit_redirect (RB_PLAYER (stream->player), stream->stream_data, uri);
		}
		break;

	default:
		break;
	}

	/* emit message signals too, so plugins can process messages */
	gst_bus_async_signal_func (bus, message, NULL);

	return TRUE;
}

static void
source_setup_cb (GstElement *uridecodebin, GstElement *source, RBPlayerGstMultiStream *stream)
{
	g_signal_emit (stream->player, signals[PREPARE_SOURCE], 0, stream->uri, source);
}

static void
stream_pad_added_cb (GstElement *decoder, GstPad *pad, RBPlayerGstMultiStream *stream)
{
	GstCaps *caps;
	GstStructure *structure;
	const char *mediatype;
	GstPad *sinkpad, *srcpad;
	GstIterator *it;
	GValue item = { 0, };

	caps = gst_pad_query_caps (pad, NULL);
	if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
		rb_debug ("got empty/any decoded caps for stream %s", stream->uri);
		gst_caps_unref (caps);
		return;
	}

	structure = gst_caps_get_structure (caps, 0);
	mediatype = gst_structure_get_name (structure);
	if (g_str_has_prefix (mediatype, "audio/x-raw") == FALSE) {
		rb_debug ("got non-audio decoded caps for stream %s: %s", stream->uri, mediatype);
	} else if (stream->decoder_linked) {
		rb_debug ("got multiple decoded audio pads for stream %s", stream->uri);
	} else {
		GstPadLinkReturn gplr;

		rb_debug ("got decoded audio pad for stream %s", stream->uri);
		sinkpad = gst_element_request_pad_simple (stream->stream_sync, "sink_%u");
		gplr = gst_pad_link (pad, sinkpad);
		if (gplr != GST_PAD_LINK_OK) {
			rb_debug ("stream %s: link to stream sync failed: %d", stream->uri, gplr);
		}

		it = gst_pad_iterate_internal_links (sinkpad);
		gst_iterator_next (it, &item);
		srcpad = g_value_dup_object (&item);
		g_value_unset (&item);
		gst_iterator_free (it);

		gplr = gst_pad_link (srcpad, gst_element_get_static_pad (stream->audioconvert, "sink"));
		if (gplr != GST_PAD_LINK_OK) {
			rb_debug ("stream %s: linking stream sync to audioconvert failed: %d", stream->uri, gplr);
		}
		gst_object_unref (sinkpad);
		gst_object_unref (srcpad);

		stream->decoder_linked = TRUE;
	}

	gst_caps_unref (caps);
}

static gboolean
construct_pipeline (RBPlayerGstMultiStream *stream, GError **error)
{
	GstElement *tail;
	GArray *stream_filters = NULL;
	GstBus *bus;

	rb_debug ("creating new stream for %s", stream->uri);

	stream->pipeline = gst_pipeline_new (NULL);
	if (stream->pipeline == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "pipeline");
		return FALSE;
	}

	stream->uridecodebin = gst_element_factory_make ("uridecodebin3", NULL);
	if (stream->uridecodebin == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "uridecodebin3");
		return FALSE;
	}
	g_signal_connect (G_OBJECT (stream->uridecodebin),
			  "about-to-finish",
			  G_CALLBACK (about_to_finish_cb),
			  stream);
	g_signal_connect (G_OBJECT (stream->uridecodebin),
			  "source-setup",
			  G_CALLBACK (source_setup_cb),
			  stream);
	g_signal_connect (G_OBJECT (stream->uridecodebin),
			  "pad-added",
			  G_CALLBACK (stream_pad_added_cb),
			  stream);
	/* need pad-removed too? */
	g_object_set (stream->uridecodebin,
		      "uri", stream->uri,
		      "use-buffering", TRUE,
		      NULL);

	bus = gst_element_get_bus (stream->pipeline);
	gst_bus_add_watch (bus, (GstBusFunc) bus_cb, stream);
	gst_object_unref (bus);

	stream->audio_sink = rb_player_gst_try_audio_sink ("autoaudiosink", NULL);
	if (stream->audio_sink == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "autoaudiosink");
		return FALSE;
	}
	g_signal_connect (G_OBJECT (stream->audio_sink),
			  "deep-notify::volume",
			  G_CALLBACK (volume_notify_cb),
			  stream);

	stream->stream_sync = gst_element_factory_make ("streamsynchronizer", NULL);
	if (stream->stream_sync == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "streamsynchronizer");
		return FALSE;
	}

	stream->audioconvert = gst_element_factory_make ("audioconvert", NULL);
	if (stream->audioconvert == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "audioconvert");
		return FALSE;
	}

	stream->volume = gst_element_factory_make ("volume", NULL);
	if (stream->volume == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create %s element; check your GStreamer installation"),
			     "volume");
		return FALSE;
	}
	g_signal_connect (G_OBJECT (stream->volume),
			  "notify::volume",
			  G_CALLBACK (stream_volume_changed_cb),
			  stream);
	stream->fader = GST_TIMED_VALUE_CONTROL_SOURCE (gst_interpolation_control_source_new ());
	gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (stream->fader), 0, 0.0);
	g_object_set (stream->fader, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

	gst_object_add_control_binding (GST_OBJECT (stream->volume),
					gst_direct_control_binding_new (GST_OBJECT (stream->volume), "volume", GST_CONTROL_SOURCE (stream->fader)));

	gst_bin_add_many (GST_BIN (stream->pipeline),
			  stream->uridecodebin,
			  stream->stream_sync,
			  stream->audioconvert,
			  stream->volume,
			  stream->audio_sink,
			  NULL);
	gst_element_link (stream->audioconvert, stream->volume);

	/* link in any per-stream filters */
	tail = stream->volume;
	g_signal_emit (stream->player, signals[GET_STREAM_FILTERS], 0, stream->uri, &stream_filters);
	if (stream_filters != NULL) {
		int i;
		for (i = 0; i < stream_filters->len; i++) {
			GValue *v = &g_array_index (stream_filters, GValue, i);
			GstElement *filter;
			GstElement *audioconvert;

			audioconvert = gst_element_factory_make ("audioconvert", NULL);
			filter = GST_ELEMENT (g_value_get_object (v));

			gst_bin_add_many (GST_BIN (stream->pipeline), audioconvert, filter, NULL);
			gst_element_link_many (tail, audioconvert, filter, NULL);
			tail = filter;
		}

		g_array_unref (stream_filters);
	}
	gst_element_link (tail, stream->audio_sink);

	rb_debug ("pipeline construction complete");
	return TRUE;
}

static gboolean
impl_close (RBPlayer *rbp, const char *uri, GError **error)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *stream = player->priv->current;

	if ((uri != NULL) && (stream->uri != NULL) && strcmp (stream->uri, uri) == 0) {
		rb_debug ("URI doesn't match current playing URI; ignoring");
		return TRUE;
	}

	stop_tick_timeout (player);

	if (stream != NULL) {
		stream->playing = FALSE;
		stop_and_destroy_stream (stream);
		player->priv->current = NULL;
	}
	return TRUE;
}

static gboolean
impl_open (RBPlayer *rbp,
	   const char *uri,
	   gpointer stream_data,
	   GDestroyNotify stream_data_destroy,
	   GError **error)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *stream;

	if (uri == NULL) {
		return impl_close (rbp, NULL, error);
	}

	g_clear_pointer (&player->priv->next, stop_and_destroy_stream);

	rb_debug ("setting next stream %s", uri);
	stream = g_new0 (RBPlayerGstMultiStream, 1);
	stream->player = player;
	stream->uri = g_strdup (uri);
	stream->stream_data = stream_data;
	stream->stream_data_destroy = stream_data_destroy;
	player->priv->next = stream;

	return TRUE;
}

static gboolean
impl_opened (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);

	return player->priv->current != NULL;
}

static gboolean
impl_play (RBPlayer *rbp, RBPlayerPlayType play_type, gint64 crossfade, GError **error)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *current, *next;

	current = player->priv->current;
	if (player->priv->next == NULL) {
		if (current->fading) {
			gdouble startvol;
			gint64 fadetime;

			g_object_get (current->volume, "volume", &startvol, NULL);
			fadetime = (gint64)(((double) PAUSE_FADE_DURATION) * startvol);

			rb_debug ("no stream change pending, reversing fade out and continuing");
			current->playing = TRUE;
			start_stream_fade (current, startvol, 1.0, fadetime, DO_NOTHING);
			_rb_player_emit_playing_stream (RB_PLAYER (player), current->stream_data);
		} else {
			rb_debug ("no stream change pending, just restarting playback");
			start_state_change (current, GST_STATE_PLAYING, PAUSE_FADE_IN);
		}

		return TRUE;
	}

	next = player->priv->next;
	next->playing = TRUE;

	switch (play_type) {
	case RB_PLAYER_PLAY_AFTER_EOS:
		g_assert (current != NULL);
		rb_debug ("will reuse current stream to play %s", next->uri);

		reuse_stream (current, next, TRUE);
		player->priv->next = NULL;

		current->stream_changing = TRUE;
		return TRUE;

	case RB_PLAYER_PLAY_REPLACE:
		if (current != NULL && current->finishing) {
			rb_debug ("current stream %s finishing, waiting for EOS to start next", current->uri);
			return TRUE;
		}
		break;

	case RB_PLAYER_PLAY_CROSSFADE:
		break;

	default:
		g_assert_not_reached ();
	}

	if (current != NULL) {
		/* try to reuse the current stream */
		gboolean reused = FALSE;
		g_signal_emit (player, signals[CAN_REUSE_STREAM], 0, next->uri, current->uri, GST_ELEMENT (current->pipeline), &reused);
		if (reused) {
			rb_debug ("reusing stream %s for new stream %s", current->uri, next->uri);
			g_signal_emit (player, signals[REUSE_STREAM], 0, next->uri, current->uri, GST_ELEMENT (current->pipeline));

			reuse_stream (current, next, FALSE);
			player->priv->next = NULL;

			emit_playing_stream_and_tags (current);
			return TRUE;
		}
	}

	if (construct_pipeline (next, error) == FALSE) {
		return FALSE;
	}

	if (current == NULL) {
		/* not much else we can do */
		gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (next->fader), 0, 0.1);
		start_state_change (next, GST_STATE_PLAYING, FINISH_TRACK_CHANGE);
	} else if (current->playing) {

		switch (play_type) {
		case RB_PLAYER_PLAY_REPLACE:
			start_state_change (next, GST_STATE_PAUSED, START_NEXT_STREAM);
			break;

		case RB_PLAYER_PLAY_CROSSFADE:
			current->crossfade = crossfade;
			next->crossfade = crossfade;
			start_state_change (next, GST_STATE_PLAYING, START_CROSSFADE);
			break;

		case RB_PLAYER_PLAY_AFTER_EOS:
			/* nothing */
			break;
		}
	} else {
		/* can't crossfade or wait for eos, so we just have to start the new stream */
		start_state_change (next, GST_STATE_PAUSED, START_NEXT_STREAM);
	}

	g_object_notify (G_OBJECT (player), "bus");
	return TRUE;
}

static void
impl_pause (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *stream;

	stream = player->priv->current;
	if (stream == NULL || stream->playing == FALSE)
		return;

	stream->playing = FALSE;
	if (stream->fading) {
		gdouble startvol;
		gint64 fadetime;

		g_object_get (stream->volume, "volume", &startvol, NULL);
		fadetime = (gint64)(((double) PAUSE_FADE_DURATION) * startvol);

		start_stream_fade (stream, startvol, 0.0, fadetime, PAUSE_SEEK_BACK);
	} else {
		start_stream_fade (stream, 1.0, 0.0, PAUSE_FADE_DURATION, PAUSE_SEEK_BACK);
	}
}

static gboolean
impl_playing (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);

	if (player->priv->current == NULL)
		return FALSE;

	return player->priv->current->playing;
}

static void
impl_set_volume (RBPlayer *rbp,
		 float volume)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *stream;
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	stream = player->priv->current;
	if (stream != NULL) {
		stream->volume_changed++;
		if (stream->volume_applied > 0) {
			set_sink_volume (stream, volume);
			stream->volume_applied = stream->volume_changed;
		} else {
			/* volume will be applied in the first call to impl_play */
		}
	}

	player->priv->cur_volume = volume;
}

static float
impl_get_volume (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);

	return player->priv->cur_volume;
}

static gboolean
impl_seekable (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	gboolean can_seek = TRUE;
	GstQuery *query;

	if (player->priv->current == NULL)
		return FALSE;

	query = gst_query_new_seeking (GST_FORMAT_TIME);
	if (gst_element_query (player->priv->current->pipeline, query)) {
		gst_query_parse_seeking (query, NULL, &can_seek, NULL, NULL);
	} else {
		gst_query_unref (query);

		query = gst_query_new_duration (GST_FORMAT_TIME);
		can_seek = gst_element_query (player->priv->current->pipeline, query);
	}
	gst_query_unref (query);

	return can_seek;
}

static void
impl_set_time (RBPlayer *rbp, gint64 time)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);
	RBPlayerGstMultiStream *stream;

	stream = player->priv->current;
	if (stream) {
		rb_debug ("seeking to %" G_GINT64_FORMAT, time);
		gst_element_seek (stream->pipeline, 1.0,
				  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
				  GST_SEEK_TYPE_SET, time,
				  GST_SEEK_TYPE_NONE, -1);

		gst_element_get_state (stream->pipeline, NULL, NULL, 100 * GST_MSECOND);
	}
}

static gint64
impl_get_time (RBPlayer *rbp)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (rbp);

	if (player->priv->current != NULL) {
		gint64 position = -1;
		GstFormat fmt = GST_FORMAT_TIME;

		gst_element_query_position (player->priv->current->pipeline, fmt, &position);
		return position;
	} else {
		return -1;
	}
}


RBPlayer *
rb_player_gst_multi_new (GError **error)
{
	return RB_PLAYER (g_object_new (RB_TYPE_PLAYER_GST_MULTI, NULL, NULL));
}


static void
rb_player_gst_multi_init (RBPlayerGstMulti *player)
{
	player->priv = (G_TYPE_INSTANCE_GET_PRIVATE ((player),
			RB_TYPE_PLAYER_GST_MULTI,
			RBPlayerGstMultiPrivate));
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBPlayerGstMulti *player = RB_PLAYER_GST_MULTI (object);

	switch (prop_id) {
	case PROP_BUS:
		if (player->priv->current) {
			GstBus *bus;
			bus = gst_element_get_bus (player->priv->current->pipeline);
			g_value_set_object (value, bus);
			gst_object_unref (bus);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	/*RBPlayerGst *player = RB_PLAYER_GST_MULTI (object);*/

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBPlayerGstMulti *player;

	player = RB_PLAYER_GST_MULTI (object);

	stop_tick_timeout (player);

	if (player->priv->emit_stream_idle_id != 0) {
		g_source_remove (player->priv->emit_stream_idle_id);
		player->priv->emit_stream_idle_id = 0;
	}

	g_list_free_full (player->priv->previous, (GDestroyNotify) destroy_stream);
	if (player->priv->current != NULL) {
		/* make sure we're in NULL state? */
		destroy_stream (player->priv->current);
	}

	G_OBJECT_CLASS (rb_player_gst_multi_parent_class)->dispose (object);
}

static void
rb_player_init (RBPlayerIface *iface)
{
	iface->open = impl_open;
	iface->opened = impl_opened;
	iface->close = impl_close;
	iface->play = impl_play;
	iface->pause = impl_pause;
	iface->playing = impl_playing;
	iface->set_volume = impl_set_volume;
	iface->get_volume = impl_get_volume;
	iface->seekable = impl_seekable;
	iface->set_time = impl_set_time;
	iface->get_time = impl_get_time;
	iface->multiple_open = (RBPlayerFeatureFunc) rb_true_function;
}

static void
rb_player_gst_multi_class_init (RBPlayerGstMultiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;

	g_object_class_install_property (object_class,
					 PROP_BUS,
					 g_param_spec_object ("bus",
							      "bus",
							      "GStreamer message bus",
							      GST_TYPE_BUS,
							      G_PARAM_READABLE));

	signals[PREPARE_SOURCE] =
		g_signal_new ("prepare-source",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstMultiClass, prepare_source),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[CAN_REUSE_STREAM] =
		g_signal_new ("can-reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstMultiClass, can_reuse_stream),
			      NULL, NULL,
			      NULL,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[REUSE_STREAM] =
		g_signal_new ("reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstMultiClass, reuse_stream),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[MISSING_PLUGINS] =
		g_signal_new ("missing-plugins",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,	/* no point handling this internally */
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_POINTER, G_TYPE_STRV, G_TYPE_STRV);
	signals[GET_STREAM_FILTERS] =
		g_signal_new ("get-stream-filters",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      rb_signal_accumulator_value_array, NULL,
			      NULL,
			      G_TYPE_ARRAY,
			      1,
			      G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBPlayerGstMultiPrivate));
}

