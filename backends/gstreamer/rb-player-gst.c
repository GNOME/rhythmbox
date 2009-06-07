/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@debian.org>
 *  Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
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
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/pbutils.h>

#include "rb-debug.h"
#include "rb-marshal.h"
#include "rb-util.h"

#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-player-gst-helper.h"
#include "rb-player-gst-filter.h"
#include "rb-player-gst-tee.h"

static void rb_player_init (RBPlayerIface *iface);
static void rb_player_gst_filter_init (RBPlayerGstFilterIface *iface);
static void rb_player_gst_tee_init (RBPlayerGstTeeIface *iface);

G_DEFINE_TYPE_WITH_CODE(RBPlayerGst, rb_player_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER, rb_player_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_FILTER, rb_player_gst_filter_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_TEE, rb_player_gst_tee_init)
			)

#define MAX_NETWORK_BUFFER_SIZE		(2048)

#define RB_PLAYER_GST_TICK_HZ 5
#define STATE_CHANGE_MESSAGE_TIMEOUT 5

enum
{
	PROP_0,
	PROP_PLAYBIN,
	PROP_BUS,
	PROP_BUFFER_SIZE
};

enum
{
	PREPARE_SOURCE,
	CAN_REUSE_STREAM,
	REUSE_STREAM,
	MISSING_PLUGINS,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBPlayerGstPrivate
{
	char *prev_uri;
	char *uri;
	gpointer stream_data;
	GDestroyNotify stream_data_destroy;

	GstElement *playbin;
	GstElement *audio_sink;
	guint buffer_size;

	gboolean playing;
	gboolean buffering;

	gboolean stream_change_pending;
	gboolean current_track_finishing;

	gboolean emitted_error;

	float cur_volume;
	float replaygain_scale;

	guint tick_timeout_id;

	GList *waiting_tees;
	GstElement *sinkbin;
	GstElement *tee;

	GList *waiting_filters; /* in reverse order */
	GstElement *filterbin;
};

static gboolean
tick_timeout (RBPlayerGst *mp)
{
	if (mp->priv->playing) {
		_rb_player_emit_tick (RB_PLAYER (mp),
				      mp->priv->stream_data,
				      rb_player_get_time (RB_PLAYER (mp)),
				      -1);
	}
	return TRUE;
}


static void
about_to_finish_cb (GstElement *playbin, RBPlayerGst *player)
{
	if (player->priv->stream_change_pending == TRUE) {
		/* this probably shouldn't happen, but it's OK if it does, I think */
		rb_debug ("got about-to-finish, but we already have a stream change pending.");
		return;
	}

	/* don't handle about-to-finish for cdda */
	if (g_str_has_prefix (player->priv->uri, "cdda://")) {
		rb_debug ("ignoring about-to-finish for %s", player->priv->uri);
		return;
	}

	/* emit EOS now and hope we get something to play */
	player->priv->current_track_finishing = TRUE;

	_rb_player_emit_eos (RB_PLAYER (player), player->priv->stream_data);
}

static gboolean
emit_volume_changed_idle (RBPlayerGst *player)
{
	_rb_player_emit_volume_changed (RB_PLAYER (player), player->priv->cur_volume);
	return FALSE;
}

static void
volume_notify_cb (GObject *element, GParamSpec *pspec, RBPlayerGst *player)
{
	gdouble v;
	g_object_get (element, "volume", &v, NULL);
	player->priv->cur_volume = v;

	g_idle_add ((GSourceFunc) emit_volume_changed_idle, player);
}

static void
process_tag (const GstTagList *list, const gchar *tag, RBPlayerGst *player)
{
	RBMetaDataField field;
	GValue value = {0,};

	/* process embedded images */
	if (!strcmp (tag, GST_TAG_IMAGE) || !strcmp (tag, GST_TAG_PREVIEW_IMAGE)) {
		GdkPixbuf *pixbuf;
		pixbuf = rb_gst_process_embedded_image (list, tag);
		if (pixbuf != NULL) {
			_rb_player_emit_image (RB_PLAYER (player),
					       player->priv->stream_data,
					       pixbuf);
			g_object_unref (pixbuf);
		}
	} else if (rb_gst_process_tag_string (list, tag, &field, &value)) {
		rb_debug ("emitting info field %d", field);
		_rb_player_emit_info (RB_PLAYER (player),
				      player->priv->stream_data,
				      field,
				      &value);
		g_value_unset (&value);
	}
}

static void
handle_missing_plugin_message (RBPlayerGst *player, GstMessage *message)
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

	g_signal_emit (player, signals[MISSING_PLUGINS], 0, player->priv->stream_data, details, descriptions);
	g_strfreev (details);
	g_strfreev (descriptions);
}

static gboolean
bus_cb (GstBus *bus, GstMessage *message, RBPlayerGst *mp)
{
	const GstStructure *structure;
	g_return_val_if_fail (mp != NULL, FALSE);

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR: {
		char *debug;
		GError *error, *sig_error;
		int code;
		gboolean emit = TRUE;

		gst_message_parse_error (message, &error, &debug);

		/* If we've already got an error, ignore 'internal data flow error'
		 * type messages, as they're too generic to be helpful.
		 */
		if (mp->priv->emitted_error &&
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
			mp->priv->emitted_error = TRUE;
			_rb_player_emit_error (RB_PLAYER (mp), mp->priv->stream_data, sig_error);
		}

		/* close if not already closing */
		if (mp->priv->uri != NULL)
			rb_player_close (RB_PLAYER (mp), NULL, NULL);

		g_error_free (error);
		g_free (debug);
		break;
	}

	case GST_MESSAGE_EOS:
		rb_debug ("got EOS.. why haven't you told me what to do yet?");
		_rb_player_emit_eos (RB_PLAYER (mp), mp->priv->stream_data);
		break;

	case GST_MESSAGE_TAG: {
		GstTagList *tags;
		gst_message_parse_tag (message, &tags);

		gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, mp);
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
			mp->priv->buffering = FALSE;
			if (mp->priv->playing) {
				rb_debug ("buffering done, setting pipeline back to PLAYING");
				gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);
			} else {
				rb_debug ("buffering done, leaving pipeline PAUSED");
			}
		} else if (mp->priv->buffering == FALSE && mp->priv->playing) {
			GstState cur_state;

			gst_element_get_state (mp->priv->playbin, &cur_state, NULL, 0);
			if (cur_state == GST_STATE_PLAYING) {
				rb_debug ("buffering - temporarily pausing playback");
				gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED);
			} else {
				rb_debug ("buffering - during preroll; doing nothing");
			}
			mp->priv->buffering = TRUE;
		}

		_rb_player_emit_buffering (RB_PLAYER (mp), mp->priv->stream_data, progress);
		break;
	}

	case GST_MESSAGE_APPLICATION:
		structure = gst_message_get_structure (message);
		_rb_player_emit_event (RB_PLAYER (mp), mp->priv->stream_data, gst_structure_get_name (structure), NULL);
		break;

	case GST_MESSAGE_ELEMENT:
		if (gst_is_missing_plugin_message (message)) {
			handle_missing_plugin_message (mp, message);
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
source_notify_cb (GObject *object, GParamSpec *pspec, RBPlayerGst *player)
{
	GstElement *source;
	g_object_get (object, "source", &source, NULL);

	g_signal_emit (player, signals[PREPARE_SOURCE], 0, player->priv->uri, source);

	g_object_unref (source);
}

static gboolean
construct_pipeline (RBPlayerGst *mp, GError **error)
{
	GstElement *sink;

	mp->priv->playbin = gst_element_factory_make ("playbin2", NULL);
	if (mp->priv->playbin == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create playbin2 element; check your GStreamer installation"));
		return FALSE;
	}
	g_signal_connect_object (G_OBJECT (mp->priv->playbin),
				 "about-to-finish",
				 G_CALLBACK (about_to_finish_cb),
				 mp, 0);
	g_signal_connect_object (G_OBJECT (mp->priv->playbin),
				 "notify::volume",
				 G_CALLBACK (volume_notify_cb),
				 mp, 0);
	g_signal_connect_object (G_OBJECT (mp->priv->playbin),
				 "notify::source",
				 G_CALLBACK (source_notify_cb),
				 mp, 0);
	if (mp->priv->buffer_size != 0) {
		g_object_set (mp->priv->playbin, "buffer-size", mp->priv->buffer_size * 1024, NULL);
	}

	gst_bus_add_watch (gst_element_get_bus (mp->priv->playbin),
			   (GstBusFunc) bus_cb,
			   mp);

	/* let plugins add bits to playbin */
	g_object_notify (G_OBJECT (mp), "playbin");
	g_object_notify (G_OBJECT (mp), "bus");

	/* Use gconfaudiosink for audio if there's no audio sink yet */
	g_object_get (mp->priv->playbin, "audio-sink", &mp->priv->audio_sink, NULL);
	if (mp->priv->audio_sink == NULL) {
		mp->priv->audio_sink = gst_element_factory_make ("gconfaudiosink", NULL);
		if (mp->priv->audio_sink == NULL) {
			/* fall back to autoaudiosink */
			rb_debug ("falling back to autoaudiosink");
			mp->priv->audio_sink = gst_element_factory_make ("autoaudiosink", NULL);
		} else {
			rb_debug ("using gconfaudiosink");
		}

		if (mp->priv->audio_sink != NULL) {
			/* set the profile property on the gconfaudiosink to "music and movies" */
			if (g_object_class_find_property (G_OBJECT_GET_CLASS (mp->priv->audio_sink), "profile")) {
				rb_debug ("setting profile property on audio sink");
				g_object_set (mp->priv->audio_sink, "profile", 1, NULL);
			}

			g_object_set (mp->priv->playbin, "audio-sink", mp->priv->audio_sink, NULL);
		}
	} else {
		rb_debug ("existing audio sink found");
		g_object_unref (mp->priv->audio_sink);
	}

	{
		GstPad *pad;
		GList *l;
		GstElement *queue;
		GstElement *audioconvert;
		GstPad *ghostpad;

		/* setup filterbin,and insert the leading audioconvert */
		mp->priv->filterbin = gst_bin_new (NULL);
		audioconvert = gst_element_factory_make ("audioconvert", NULL);
		gst_bin_add (GST_BIN (mp->priv->filterbin), audioconvert);

		/* ghost it to the bin */
		pad = gst_element_get_pad (audioconvert, "sink");
		ghostpad = gst_ghost_pad_new ("sink", pad);
		gst_element_add_pad (mp->priv->filterbin, ghostpad);
		gst_object_unref (pad);

		pad = gst_element_get_pad (audioconvert, "src");
		ghostpad = gst_ghost_pad_new ("src", pad);
		gst_element_add_pad (mp->priv->filterbin, ghostpad);
		gst_object_unref (pad);

		/* set up the sinkbin with its tee element */
		mp->priv->sinkbin = gst_bin_new (NULL);
		mp->priv->tee = gst_element_factory_make ("tee", NULL);
		queue = gst_element_factory_make ("queue", NULL);

		/* link it all together and insert */
		gst_bin_add_many (GST_BIN (mp->priv->sinkbin), mp->priv->filterbin, mp->priv->tee, queue, mp->priv->audio_sink, NULL);
		gst_element_link_many (mp->priv->filterbin, mp->priv->tee, queue, mp->priv->audio_sink, NULL);

		pad = gst_element_get_pad (mp->priv->filterbin, "sink");
		ghostpad = gst_ghost_pad_new ("sink", pad);
		gst_element_add_pad (mp->priv->sinkbin, ghostpad);
		gst_object_unref (pad);

		g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", mp->priv->sinkbin, NULL);


		/* add any tees and filters that were waiting for us */
		for (l = mp->priv->waiting_tees; l != NULL; l = g_list_next (l)) {
			rb_player_gst_tee_add_tee (RB_PLAYER_GST_TEE (mp), GST_ELEMENT (l->data));
		}
		g_list_free (mp->priv->waiting_tees);
		mp->priv->waiting_tees = NULL;

		for (l = mp->priv->waiting_filters; l != NULL; l = g_list_next (l)) {
			rb_player_gst_filter_add_filter (RB_PLAYER_GST_FILTER(mp), GST_ELEMENT (l->data));
		}
		g_list_free (mp->priv->waiting_filters);
		mp->priv->waiting_filters = NULL;
	}

	/* Use fakesink for video if there's no video sink yet */
	g_object_get (mp->priv->playbin, "video-sink", &sink, NULL);
	if (sink == NULL) {
		sink = gst_element_factory_make ("fakesink", NULL);
		g_object_set (mp->priv->playbin, "video-sink", sink, NULL);
	} else {
		g_object_unref (sink);
	}

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	mp->priv->replaygain_scale = 1.0f;

	rb_player_set_volume (RB_PLAYER (mp), mp->priv->cur_volume);

	rb_debug ("pipeline construction complete");
	return TRUE;
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
set_state_and_wait (RBPlayerGst *player, GstState target, GError **error)
{
	GstBus *bus;
	gboolean waiting;
	gboolean result;

	g_assert (player->priv->playbin != NULL);
	/* XXX probably need to remove bus watch here if we're not on the main thread */
	/* .. probably shouldn't be doing this much anyway .. */

	rb_debug ("setting playbin state to %s", gst_element_state_get_name (target));

	switch (gst_element_set_state (player->priv->playbin, target)) {
	case GST_STATE_CHANGE_SUCCESS:
		rb_debug ("state change was successful");
		return TRUE;

	case GST_STATE_CHANGE_NO_PREROLL:
		rb_debug ("state change was successful (no preroll)");
		return TRUE;

	case GST_STATE_CHANGE_ASYNC:
		rb_debug ("state is changing asynchronously");
		result = TRUE;
		break;

	case GST_STATE_CHANGE_FAILURE:
		rb_debug ("state change failed");
		result = FALSE;
		break;

	default:
		rb_debug ("unknown state change return..");
		result = TRUE;
		break;
	}

	bus = gst_element_get_bus (player->priv->playbin);
	waiting = TRUE;
	while (waiting) {
		GstMessage *message;

		message = gst_bus_timed_pop (bus, GST_SECOND * STATE_CHANGE_MESSAGE_TIMEOUT);
		if (message == NULL) {
			rb_debug ("state change is taking too long..");
			break;
		}

		switch (GST_MESSAGE_TYPE (message)) {
		case GST_MESSAGE_ERROR:
			{
				char *debug;
				GError *gst_error = NULL;

				gst_message_parse_error (message, &gst_error, &debug);

				if (message_from_sink (player->priv->audio_sink, message)) {
					rb_debug ("got error from sink: %s (%s)", gst_error->message, debug);
					/* Translators: the parameter here is an error message */
					g_set_error (error,
						     RB_PLAYER_ERROR,
						     RB_PLAYER_ERROR_INTERNAL,
						     _("Failed to open output device: %s"),
						     gst_error->message);
				} else {
					rb_debug ("got error from stream: %s (%s)", gst_error->message, debug);
					g_set_error (error,
						     RB_PLAYER_ERROR,
						     RB_PLAYER_ERROR_GENERAL,
						     "%s",
						     gst_error->message);
				}

				g_error_free (gst_error);
				g_free (debug);

				waiting = FALSE;
				result = FALSE;
				break;
			}

		case GST_MESSAGE_STATE_CHANGED:
			{
				GstState oldstate;
				GstState newstate;
				GstState pending;
				gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
				if (GST_MESSAGE_SRC (message) == GST_OBJECT (player->priv->playbin)) {
					rb_debug ("playbin reached state %s", gst_element_state_get_name (newstate));
					if (pending == GST_STATE_VOID_PENDING && newstate == target) {
						waiting = FALSE;
					}
				}
				break;
			}

		default:
			/* pass back to regular message handler */
			bus_cb (bus, message, player);
			break;
		}
	}

	if (result == FALSE && *error == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Unable to start playback pipeline"));
	}

	return result;
}

static void
_destroy_stream_data (RBPlayerGst *player)
{
	if (player->priv->stream_data && player->priv->stream_data_destroy) {
		player->priv->stream_data_destroy (player->priv->stream_data);
	}
	player->priv->stream_data = NULL;
	player->priv->stream_data_destroy = NULL;
}

static gboolean
impl_close (RBPlayer *player, const char *uri, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if ((uri != NULL) && (mp->priv->uri != NULL) && strcmp (mp->priv->uri, uri) == 0) {
		rb_debug ("URI doesn't match current playing URI; ignoring");
		return TRUE;
	}

	mp->priv->playing = FALSE;
	mp->priv->buffering = FALSE;

	_destroy_stream_data (mp);
	g_free (mp->priv->uri);
	g_free (mp->priv->prev_uri);
	mp->priv->uri = NULL;
	mp->priv->prev_uri = NULL;

	mp->priv->replaygain_scale = 1.0f;

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}

	if (mp->priv->playbin == NULL)
		return TRUE;

	return set_state_and_wait (mp, GST_STATE_READY, error);
}

static gboolean
impl_open (RBPlayer *player,
	   const char *uri,
	   gpointer stream_data,
	   GDestroyNotify stream_data_destroy,
	   GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->playbin == NULL) {
		if (!construct_pipeline (mp, error))
			return FALSE;
	}

	g_assert (mp->priv->playbin != NULL);

	if (uri == NULL) {
		return impl_close (player, NULL, error);
	}

	rb_debug ("setting new uri to %s", uri);
	_destroy_stream_data (mp);
	g_free (mp->priv->prev_uri);
	mp->priv->prev_uri = mp->priv->uri;
	mp->priv->uri = g_strdup (uri);
	mp->priv->stream_data = stream_data;
	mp->priv->stream_data_destroy = stream_data_destroy;
	mp->priv->emitted_error = FALSE;
	mp->priv->stream_change_pending = TRUE;

	return TRUE;
}

static gboolean
impl_opened (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->uri != NULL;
}

static gboolean
impl_play (RBPlayer *player, RBPlayerPlayType play_type, gint64 crossfade, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean result;

	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	if (mp->priv->stream_change_pending == FALSE) {
		rb_debug ("no stream change pending, just restarting playback");
		result = set_state_and_wait (mp, GST_STATE_PLAYING, error);

	} else if (mp->priv->current_track_finishing) {
		rb_debug ("current track finishing -> just setting URI on playbin");
		g_object_set (mp->priv->playbin, "uri", mp->priv->uri, NULL);
		result = TRUE;

	} else {
		gboolean reused = FALSE;

		/* try to reuse the stream */
		if (mp->priv->prev_uri != NULL) {
			g_signal_emit (mp,
				       signals[CAN_REUSE_STREAM], 0,
				       mp->priv->uri, mp->priv->prev_uri, mp->priv->playbin,
				       &reused);

			if (reused) {
				rb_debug ("reusing stream to switch from %s to %s", mp->priv->prev_uri, mp->priv->uri);
				g_signal_emit (player,
					       signals[REUSE_STREAM], 0,
					       mp->priv->uri, mp->priv->prev_uri, mp->priv->playbin);
				result = TRUE;
			}
		}

		/* no stream reuse, so stop, set the new URI, then start */
		if (reused == FALSE) {
			rb_debug ("not in transition, stopping current track to start the new one");
			result = set_state_and_wait (mp, GST_STATE_READY, error);
			if (result == TRUE) {
				g_object_set (mp->priv->playbin, "uri", mp->priv->uri, NULL);
				result = set_state_and_wait (mp, GST_STATE_PLAYING, error);
			}
		}

	}

	mp->priv->stream_change_pending = FALSE;

	if (result) {
		mp->priv->current_track_finishing = FALSE;
		mp->priv->buffering = FALSE;
		mp->priv->playing = TRUE;

		_rb_player_emit_playing_stream (RB_PLAYER (mp), mp->priv->stream_data);

		if (mp->priv->tick_timeout_id == 0) {
			mp->priv->tick_timeout_id =
				g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ,
					       (GSourceFunc) tick_timeout,
					       mp);
		}
	}

	return result;
}

static void
impl_pause (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	GError *error = NULL;

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->playbin != NULL);

	if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
		g_warning ("unable to pause playback: %s\n", error->message);
		g_error_free (error);
	}

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}
}

static gboolean
impl_playing (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->playing;
}

static void
impl_set_replaygain (RBPlayer *player,
		     const char *uri,
		     double track_gain,
		     double track_peak,
		     double album_gain,
		     double album_peak)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	double scale;
	double gain = 0;
	double peak = 0;

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
	mp->priv->replaygain_scale = scale;

	if (mp->priv->playbin != NULL) {
		g_object_set (mp->priv->playbin, "volume", mp->priv->cur_volume * scale, NULL);
	}
}

static void
impl_set_volume (RBPlayer *player,
		 float volume)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	if (mp->priv->playbin != NULL) {
		g_object_set (mp->priv->playbin, "volume", volume * mp->priv->replaygain_scale, NULL);
	}

	mp->priv->cur_volume = volume;
}

static float
impl_get_volume (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->cur_volume;
}

static gboolean
impl_seekable (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean can_seek = TRUE;
	GstQuery *query;

	if (mp->priv->playbin == NULL)
		return FALSE;

	query = gst_query_new_seeking (GST_FORMAT_TIME);
	if (gst_element_query (mp->priv->playbin, query)) {
		gst_query_parse_seeking (query, NULL, &can_seek, NULL, NULL);
	} else {
		gst_query_unref (query);

		query = gst_query_new_duration (GST_FORMAT_TIME);
		can_seek = gst_element_query (mp->priv->playbin, query);
	}
	gst_query_unref (query);

	return can_seek;
}

static void
impl_set_time (RBPlayer *player, gint64 time)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	GError *error = NULL;

	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->playbin != NULL);

	if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
		g_warning ("got error while pausing the pipelink for seeking: %s\n", error->message);
		g_clear_error (&error);

		/* keep going anyway? */
	}

	gst_element_seek (mp->priv->playbin, 1.0,
			  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, time,
			  GST_SEEK_TYPE_NONE, -1);

	if (mp->priv->playing) {
		if (set_state_and_wait (mp, GST_STATE_PLAYING, &error) == FALSE) {
			g_warning ("unable to resume playback after seeking: %s\n", error->message);
			g_clear_error (&error);
		}
	}
}

static gint64
impl_get_time (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->playbin != NULL) {
		gint64 position = -1;
		GstFormat fmt = GST_FORMAT_TIME;

		gst_element_query_position (mp->priv->playbin, &fmt, &position);
		return position;
	} else {
		return -1;
	}
}

static gboolean
impl_add_tee (RBPlayerGstTee *player, GstElement *element)
{
	RBPlayerGst *mp;
	GstElement *queue, *audioconvert, *bin;
	GstPad *pad, *ghostpad;

	mp = RB_PLAYER_GST (player);

	if (mp->priv->tee == NULL) {
		mp->priv->waiting_tees = g_list_prepend (mp->priv->waiting_tees, element);
		return TRUE;
	}

	if (mp->priv->playing) {
		GError *error = NULL;
		if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
			g_warning ("Failed to pause pipeline before tee insertion: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
	}

	bin = gst_bin_new (NULL);
	queue = gst_element_factory_make ("queue", NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);

	/* set up the element's containing bin */
	gst_bin_add_many (GST_BIN (bin), queue, audioconvert, element, NULL);
	gst_bin_add (GST_BIN (mp->priv->sinkbin), bin);
	gst_element_link_many (queue, audioconvert, element, NULL);

	/* link it to the tee */
	pad = gst_element_get_pad (queue, "sink");
	ghostpad = gst_ghost_pad_new ("sink", pad);
	gst_element_add_pad (bin, ghostpad);
	gst_object_unref (pad);

	gst_element_link (mp->priv->tee, bin);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	_rb_player_gst_tee_emit_tee_inserted (player, element);

	return TRUE;
}

static gboolean
impl_remove_tee (RBPlayerGstTee *player, GstElement *element)
{
	RBPlayerGst *mp;
	GstElement *bin;

	mp = RB_PLAYER_GST (player);

	if (mp->priv->tee == NULL) {
		gst_object_sink (element);
		mp->priv->waiting_tees = g_list_remove (mp->priv->waiting_tees, element);
		return TRUE;
	}

	_rb_player_gst_tee_emit_tee_pre_remove (player, element);

	if (mp->priv->playing) {
		GError *error = NULL;
		if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
			g_warning ("Failed to pause pipeline before tee removal: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
	}

	/* get the containing bin and unlink it */
	bin = GST_ELEMENT (gst_element_get_parent (element));

	gst_element_set_state (bin, GST_STATE_NULL);

	gst_bin_remove (GST_BIN (mp->priv->sinkbin), bin);
	gst_object_unref (bin);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	return TRUE;
}

static gboolean
impl_add_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGst *mp;
	GstElement *audioconvert, *bin;
	GstPad *ghostpad, *realpad;
	GstPad *binsinkpad, *binsrcpad;
	gpointer element_sink_pad;
	GstIterator *sink_pads;
	gboolean sink_pad_found, stop_scan;
	GstPadLinkReturn link;

	mp = RB_PLAYER_GST (player);

	if (mp->priv->filterbin == NULL) {
		mp->priv->waiting_filters = g_list_prepend (mp->priv->waiting_filters, element);
		return TRUE;
	}

	if (mp->priv->playing) {
		GError *error = NULL;
		if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
			g_warning ("Failed to pause pipeline before filter insertion: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
	}

	bin = gst_bin_new (NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);

	/* set up the element's containing bin */
	rb_debug ("adding element %p and audioconvert to bin", element);
	gst_bin_add_many (GST_BIN (bin), element, audioconvert, NULL);
	gst_element_link_many (element, audioconvert, NULL);

	/* ghost to the bin */
	/* retrieve the first unliked source pad */
	sink_pad_found = FALSE;
	stop_scan = FALSE;
	sink_pads = gst_element_iterate_sink_pads (element);
	while (!sink_pad_found && !stop_scan) {
		gpointer *esp_pointer = &element_sink_pad; /* stop type-punning warnings */
		switch (gst_iterator_next (sink_pads, esp_pointer)) {
			case GST_ITERATOR_OK:
				sink_pad_found = !gst_pad_is_linked (GST_PAD(element_sink_pad));
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
		return FALSE;
	}

	binsinkpad = gst_ghost_pad_new ("sink", GST_PAD (element_sink_pad));
	gst_element_add_pad (bin, binsinkpad);

	realpad = gst_element_get_pad (audioconvert, "src");
	binsrcpad = gst_ghost_pad_new ("src", realpad);
	gst_element_add_pad (bin, binsrcpad);
	gst_object_unref (realpad);

	/* replace the filter chain ghost with the new bin */
	gst_bin_add (GST_BIN (mp->priv->filterbin), bin);

	ghostpad = gst_element_get_pad (mp->priv->filterbin, "src");
	realpad = gst_ghost_pad_get_target (GST_GHOST_PAD (ghostpad));
	gst_ghost_pad_set_target (GST_GHOST_PAD (ghostpad), binsrcpad);
	gst_object_unref (ghostpad);

	link = gst_pad_link (realpad, binsinkpad);
	gst_object_unref (realpad);
	if (link != GST_PAD_LINK_OK) {
		g_warning ("could not link new filter into pipeline");
		return FALSE;
	}

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	_rb_player_gst_filter_emit_filter_inserted (player, element);

	return TRUE;
}

static gboolean
impl_remove_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGst *mp;
	GstPad *mypad;
	GstPad *prevpad, *nextpad;
	GstPad *ghostpad;
	GstPad *targetpad;
	GstElement *bin;
	gboolean result = TRUE;

	mp = RB_PLAYER_GST (player);

	if (mp->priv->filterbin == NULL) {
		gst_object_sink (element);
		mp->priv->waiting_filters = g_list_remove (mp->priv->waiting_filters, element);
		return TRUE;
	}

	_rb_player_gst_filter_emit_filter_pre_remove (player, element);

	if (mp->priv->playing) {
		GError *error = NULL;
		if (set_state_and_wait (mp, GST_STATE_PAUSED, &error) == FALSE) {
			g_warning ("Failed to pause pipeline before filter removal: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
	}

	/* get the containing bin and unlink it */
	bin = GST_ELEMENT (gst_element_get_parent (element));

	gst_element_set_state (bin, GST_STATE_NULL);

	mypad = gst_element_get_pad (bin, "sink");
	prevpad = gst_pad_get_peer (mypad);
	gst_pad_unlink (prevpad, mypad);
	gst_object_unref (mypad);

	ghostpad = gst_element_get_pad (bin, "src");
	nextpad = gst_element_get_pad (mp->priv->filterbin, "src");

	targetpad = gst_ghost_pad_get_target (GST_GHOST_PAD (nextpad));
	if (targetpad == ghostpad) {
		/* we are at the end of the filter chain, so redirect the ghostpad to the previous element */
		gst_ghost_pad_set_target (GST_GHOST_PAD (nextpad), prevpad);
	} else {
		/* we are in the middle, so link the previous and next elements */
		mypad = gst_element_get_pad (bin, "src");
		gst_object_unref (nextpad);
		nextpad = gst_pad_get_peer (mypad);
		gst_pad_unlink (mypad, nextpad);
		gst_object_unref (mypad);

		if (gst_pad_link (prevpad, nextpad) != GST_PAD_LINK_OK)
			result = FALSE;
	}

	gst_object_unref (nextpad);
	gst_object_unref (prevpad);
	gst_object_unref (ghostpad);
	gst_object_unref (targetpad);

	gst_bin_remove (GST_BIN (mp->priv->filterbin), bin);
	gst_object_unref (bin);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	return result;
}

static void
rb_player_gst_filter_init (RBPlayerGstFilterIface *iface)
{
	iface->add_filter = impl_add_filter;
	iface->remove_filter = impl_remove_filter;
}

static void
rb_player_gst_tee_init (RBPlayerGstTeeIface *iface)
{
	iface->add_tee = impl_add_tee;
	iface->remove_tee = impl_remove_tee;
}



RBPlayer *
rb_player_gst_new (GError **error)
{
	return RB_PLAYER (g_object_new (RB_TYPE_PLAYER_GST, NULL, NULL));
}


static void
rb_player_gst_init (RBPlayerGst *mp)
{
	mp->priv = (G_TYPE_INSTANCE_GET_PRIVATE ((mp),
		    RB_TYPE_PLAYER_GST,
		    RBPlayerGstPrivate));
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBPlayerGst *mp = RB_PLAYER_GST (object);

	switch (prop_id) {
	case PROP_PLAYBIN:
		g_value_set_object (value, mp->priv->playbin);
		break;
	case PROP_BUS:
		if (mp->priv->playbin) {
			GstBus *bus;
			bus = gst_element_get_bus (mp->priv->playbin);
			g_value_set_object (value, bus);
			gst_object_unref (bus);
		}
		break;
	case PROP_BUFFER_SIZE:
		g_value_set_uint (value, mp->priv->buffer_size);
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
	RBPlayerGst *mp = RB_PLAYER_GST (object);

	switch (prop_id) {
	case PROP_BUFFER_SIZE:
		mp->priv->buffer_size = g_value_get_uint (value);
		if (mp->priv->playbin != NULL) {
			rb_debug ("setting buffer size on playbin: %d", mp->priv->buffer_size * 1024);
			g_object_set (mp->priv->playbin, "buffer-size", mp->priv->buffer_size * 1024, NULL);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBPlayerGst *mp;

	mp = RB_PLAYER_GST (object);

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}

	if (mp->priv->playbin != NULL) {
		gst_element_set_state (mp->priv->playbin, GST_STATE_NULL);
		g_object_unref (mp->priv->playbin);
		mp->priv->playbin = NULL;
		mp->priv->audio_sink = NULL;
	}

	if (mp->priv->waiting_tees != NULL) {
		g_list_foreach (mp->priv->waiting_tees, (GFunc)gst_object_sink, NULL);
		g_list_free (mp->priv->waiting_tees);
		mp->priv->waiting_tees = NULL;
	}

	if (mp->priv->waiting_filters != NULL) {
		g_list_foreach (mp->priv->waiting_filters, (GFunc)gst_object_sink, NULL);
		g_list_free (mp->priv->waiting_filters);
		mp->priv->waiting_filters = NULL;
	}

	G_OBJECT_CLASS (rb_player_gst_parent_class)->dispose (object);
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
	iface->set_replaygain = impl_set_replaygain;
	iface->seekable = impl_seekable;
	iface->set_time = impl_set_time;
	iface->get_time = impl_get_time;
	iface->multiple_open = (RBPlayerFeatureFunc) rb_false_function;
}

static void
rb_player_gst_class_init (RBPlayerGstClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;

	g_object_class_install_property (object_class,
					 PROP_PLAYBIN,
					 g_param_spec_object ("playbin",
							      "playbin",
							      "playbin element",
							      GST_TYPE_ELEMENT,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_BUS,
					 g_param_spec_object ("bus",
							      "bus",
							      "GStreamer message bus",
							      GST_TYPE_BUS,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_BUFFER_SIZE,
					 g_param_spec_uint ("buffer-size",
							    "buffer size",
							    "Buffer size for network streams, in kB",
							    64, MAX_NETWORK_BUFFER_SIZE, 128,
							    G_PARAM_READWRITE));

	signals[PREPARE_SOURCE] =
		g_signal_new ("prepare-source",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, prepare_source),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_OBJECT,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[CAN_REUSE_STREAM] =
		g_signal_new ("can-reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, can_reuse_stream),
			      NULL, NULL,
			      rb_marshal_BOOLEAN__STRING_STRING_OBJECT,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[REUSE_STREAM] =
		g_signal_new ("reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, reuse_stream),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING_OBJECT,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[MISSING_PLUGINS] =
		g_signal_new ("missing-plugins",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,	/* no point handling this internally */
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_POINTER_POINTER,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_POINTER, G_TYPE_STRV, G_TYPE_STRV);

	g_type_class_add_private (klass, sizeof (RBPlayerGstPrivate));
}

