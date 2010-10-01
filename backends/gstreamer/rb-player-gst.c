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
#if GST_CHECK_VERSION(0,10,25)
#include <gst/interfaces/streamvolume.h>
#endif
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

static void state_change_finished (RBPlayerGst *mp, GError *error);

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

enum StateChangeAction {
	DO_NOTHING,
	PLAYER_SHUTDOWN,
	SET_NEXT_URI,
	STOP_TICK_TIMER,
	FINISH_TRACK_CHANGE
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBPlayerGstPrivate
{
	char *prev_uri;
	char *uri;
	gpointer stream_data;
	GDestroyNotify stream_data_destroy;
	gpointer next_stream_data;
	GDestroyNotify next_stream_data_destroy;

	GstElement *playbin;
	GstElement *audio_sink;
	enum StateChangeAction state_change_action;
	guint buffer_size;

	gboolean playing;
	gboolean buffering;

	gboolean stream_change_pending;
	gboolean current_track_finishing;
	gboolean playbin_stream_changing;
	gboolean track_change;

	gboolean emitted_error;

	GList *stream_tags;

	gint volume_changed;
	gint volume_applied;
	float cur_volume;

	guint tick_timeout_id;

	GList *waiting_tees;
	GstElement *sinkbin;
	GstElement *tee;

	GList *waiting_filters; /* in reverse order */
	GstElement *filterbin;
};

static void
_destroy_stream_data (RBPlayerGst *player)
{
	if (player->priv->stream_data && player->priv->stream_data_destroy) {
		player->priv->stream_data_destroy (player->priv->stream_data);
	}
	player->priv->stream_data = NULL;
	player->priv->stream_data_destroy = NULL;
}

static void
_destroy_next_stream_data (RBPlayerGst *player)
{
	if (player->priv->next_stream_data && player->priv->next_stream_data_destroy) {
		player->priv->next_stream_data_destroy (player->priv->next_stream_data);
	}
	player->priv->next_stream_data = NULL;
	player->priv->next_stream_data_destroy = NULL;
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

	_rb_player_emit_eos (RB_PLAYER (player), player->priv->stream_data, TRUE);
}

static gboolean
emit_volume_changed_idle (RBPlayerGst *player)
{
	double vol;

#if GST_CHECK_VERSION(0,10,25)
	if (gst_element_implements_interface (player->priv->playbin, GST_TYPE_STREAM_VOLUME)) {
		vol = gst_stream_volume_get_volume (GST_STREAM_VOLUME (player->priv->playbin),
						    GST_STREAM_VOLUME_FORMAT_CUBIC);
	} else {
		vol = player->priv->cur_volume;
	}
#else
	vol = player->priv->cur_volume;
#endif

	_rb_player_emit_volume_changed (RB_PLAYER (player), vol);
	return FALSE;
}

static void
volume_notify_cb (GObject *element, GstObject *prop_object, GParamSpec *pspec, RBPlayerGst *player)
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
	if (!g_strcmp0 (tag, GST_TAG_IMAGE) || !g_strcmp0 (tag, GST_TAG_PREVIEW_IMAGE)) {
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
emit_playing_stream_and_tags (RBPlayerGst *player, gboolean track_change)
{
	GList *t;

	if (track_change) {
		/* swap stream data */
		_destroy_stream_data (player);
		player->priv->stream_data = player->priv->next_stream_data;
		player->priv->stream_data_destroy = player->priv->next_stream_data_destroy;
		player->priv->next_stream_data = NULL;
		player->priv->next_stream_data_destroy = NULL;
	}

	_rb_player_emit_playing_stream (RB_PLAYER (player), player->priv->stream_data);

	/* process any tag lists we received while starting the stream */
	for (t = player->priv->stream_tags; t != NULL; t = t->next) {
		GstTagList *tags;

		tags = (GstTagList *)t->data;
		rb_debug ("processing buffered taglist");
		gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, player);
		gst_tag_list_free (tags);
	}
	g_list_free (player->priv->stream_tags);
	player->priv->stream_tags = NULL;
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
tick_timeout (RBPlayerGst *mp)
{
	if (mp->priv->playing) {
		gint64 position;

		position = rb_player_get_time (RB_PLAYER (mp));

		/* if we don't have stream-changed messages, do the track change when
		 * the playback position is less than one second into the current track,
		 * which pretty much has to be the new one.
		 */
		if (mp->priv->playbin_stream_changing && (position < GST_SECOND)) {
			emit_playing_stream_and_tags (mp, TRUE);
			mp->priv->playbin_stream_changing = FALSE;
		}

		_rb_player_emit_tick (RB_PLAYER (mp),
				      mp->priv->stream_data,
				      position,
				      -1);
	}
	return TRUE;
}

static void
set_playbin_volume (RBPlayerGst *player, float volume)
{
	/* ignore the deep-notify we get directly from the sink, as it causes deadlock.
	 * we still get another one anyway.
	 */
	g_signal_handlers_block_by_func (player->priv->playbin, volume_notify_cb, player);
#if GST_CHECK_VERSION(0,10,25)
	if (gst_element_implements_interface (player->priv->playbin, GST_TYPE_STREAM_VOLUME))
		gst_stream_volume_set_volume (GST_STREAM_VOLUME (player->priv->playbin),
					      GST_STREAM_VOLUME_FORMAT_CUBIC, volume);
	else
		g_object_set (player->priv->playbin, "volume", volume, NULL);
#else
	g_object_set (player->priv->playbin, "volume", volume, NULL);
#endif
	g_signal_handlers_unblock_by_func (player->priv->playbin, volume_notify_cb, player);
}



static void
track_change_done (RBPlayerGst *mp, GError *error)
{
	mp->priv->stream_change_pending = FALSE;

	if (error != NULL) {
		rb_debug ("track change failed: %s", error->message);
		return;
	}
	rb_debug ("track change finished");

	mp->priv->current_track_finishing = FALSE;
	mp->priv->buffering = FALSE;
	mp->priv->playing = TRUE;

	if (mp->priv->playbin_stream_changing == FALSE) {
		emit_playing_stream_and_tags (mp, mp->priv->track_change);
	}

	if (mp->priv->tick_timeout_id == 0) {
		mp->priv->tick_timeout_id =
			g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ,
				       (GSourceFunc) tick_timeout,
				       mp);
	}

	if (mp->priv->volume_applied == 0) {
		GstElement *e;

		/* if the sink provides volume control, ignore the first
		 * volume setting, allowing the sink to restore its own
		 * volume.
		 */
		e = rb_player_gst_find_element_with_property (mp->priv->audio_sink, "volume");
		if (e != NULL) {
			mp->priv->volume_applied = 1;
			gst_object_unref (e);
		}

		if (mp->priv->volume_applied < mp->priv->volume_changed) {
			rb_debug ("applying initial volume: %f", mp->priv->cur_volume);
			set_playbin_volume (mp, mp->priv->cur_volume);
		}

		mp->priv->volume_applied = mp->priv->volume_changed;
	}
}

static void
start_state_change (RBPlayerGst *mp, GstState state, enum StateChangeAction action)
{
	GstStateChangeReturn scr;

	mp->priv->state_change_action = action;
	scr = gst_element_set_state (mp->priv->playbin, state);
	if (scr == GST_STATE_CHANGE_SUCCESS) {
		rb_debug ("state change succeeded synchronously");
		state_change_finished (mp, NULL);
	}
}

static void
state_change_finished (RBPlayerGst *mp, GError *error)
{
	enum StateChangeAction action = mp->priv->state_change_action;
	mp->priv->state_change_action = DO_NOTHING;

	switch (action) {
	case DO_NOTHING:
		break;

	case PLAYER_SHUTDOWN:
		if (error != NULL) {
			g_warning ("unable to shut down player pipeline: %s\n", error->message);
		}
		break;

	case SET_NEXT_URI:
		if (error != NULL) {
			g_warning ("unable to stop playback: %s\n", error->message);
		} else {
			rb_debug ("setting new playback URI %s", mp->priv->uri);
			g_object_set (mp->priv->playbin, "uri", mp->priv->uri, NULL);
			start_state_change (mp, GST_STATE_PLAYING, FINISH_TRACK_CHANGE);
		}
		break;

	case STOP_TICK_TIMER:
		if (error != NULL) {
			g_warning ("unable to pause playback: %s\n", error->message);
		} else {
			if (mp->priv->tick_timeout_id != 0) {
				g_source_remove (mp->priv->tick_timeout_id);
				mp->priv->tick_timeout_id = 0;
			}
		}
		break;

	case FINISH_TRACK_CHANGE:
		track_change_done (mp, error);
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
bus_cb (GstBus *bus, GstMessage *message, RBPlayerGst *mp)
{
	const GstStructure *structure;
	g_return_val_if_fail (mp != NULL, FALSE);

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR: {
		char *debug;
		GError *error = NULL;
		GError *sig_error = NULL;
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

		code = rb_gst_error_get_error_code (error);

		if (emit) {
			if (message_from_sink (mp->priv->audio_sink, message)) {
				rb_debug ("got error from sink: %s (%s)", error->message, debug);
				/* Translators: the parameter here is an error message */
				g_set_error (&sig_error,
					     RB_PLAYER_ERROR,
					     code,
					     _("Failed to open output device: %s"),
					     error->message);
			} else {
				rb_debug ("got error from stream: %s (%s)", error->message, debug);
				g_set_error (&sig_error,
					     RB_PLAYER_ERROR,
					     code,
					     "%s",
					     error->message);
			}
			state_change_finished (mp, sig_error);
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
		_rb_player_emit_eos (RB_PLAYER (mp), mp->priv->stream_data, FALSE);
		break;

	case GST_MESSAGE_STATE_CHANGED:
		{
			GstState oldstate;
			GstState newstate;
			GstState pending;
			gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
			if (GST_MESSAGE_SRC (message) == GST_OBJECT (mp->priv->playbin)) {
				rb_debug ("playbin reached state %s", gst_element_state_get_name (newstate));
				if (pending == GST_STATE_VOID_PENDING) {
					state_change_finished (mp, NULL);
				}
			}
			break;
		}

	case GST_MESSAGE_TAG: {
		GstTagList *tags;
		gst_message_parse_tag (message, &tags);

		if (mp->priv->stream_change_pending || mp->priv->playbin_stream_changing) {
			mp->priv->stream_tags = g_list_append (mp->priv->stream_tags, tags);
		} else {
			gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, mp);
			gst_tag_list_free (tags);
		}
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
		structure = gst_message_get_structure (message);
		if (gst_is_missing_plugin_message (message)) {
			handle_missing_plugin_message (mp, message);
		} else if (mp->priv->playbin_stream_changing &&
			   gst_structure_has_name (structure, "playbin2-stream-changed")) {
			rb_debug ("got playbin2-stream-changed message");
			mp->priv->playbin_stream_changing = FALSE;
			emit_playing_stream_and_tags (mp, TRUE);
		} else if (gst_structure_has_name (structure, "redirect")) {
			const char *uri = gst_structure_get_string (structure, "new-location");
			_rb_player_emit_redirect (RB_PLAYER (mp), mp->priv->stream_data, uri);
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
				 "deep-notify::volume",
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
		GstPad *ghostpad;

		/* setup filterbin */
		mp->priv->filterbin = rb_gst_create_filter_bin ();

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

	rb_debug ("pipeline construction complete");
	return TRUE;
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
	mp->priv->current_track_finishing = FALSE;

	_destroy_stream_data (mp);
	if (uri == NULL) {
		_destroy_next_stream_data (mp);
	}
	g_free (mp->priv->uri);
	g_free (mp->priv->prev_uri);
	mp->priv->uri = NULL;
	mp->priv->prev_uri = NULL;

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}

	if (mp->priv->playbin != NULL) {
		start_state_change (mp, GST_STATE_NULL, PLAYER_SHUTDOWN);
	}
	return TRUE;
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
	_destroy_next_stream_data (mp);
	g_free (mp->priv->prev_uri);
	mp->priv->prev_uri = mp->priv->uri;
	mp->priv->uri = g_strdup (uri);
	mp->priv->next_stream_data = stream_data;
	mp->priv->next_stream_data_destroy = stream_data_destroy;
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

	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	mp->priv->track_change = TRUE;

	if (mp->priv->stream_change_pending == FALSE) {
		rb_debug ("no stream change pending, just restarting playback");
		mp->priv->track_change = FALSE;
		start_state_change (mp, GST_STATE_PLAYING, FINISH_TRACK_CHANGE);
	} else if (mp->priv->current_track_finishing) {
		rb_debug ("current track finishing -> just setting URI on playbin");
		g_object_set (mp->priv->playbin, "uri", mp->priv->uri, NULL);

		mp->priv->playbin_stream_changing = TRUE;

		track_change_done (mp, NULL);
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
				track_change_done (mp, *error);
			}
		}

		/* no stream reuse, so stop, set the new URI, then start */
		if (reused == FALSE) {
			rb_debug ("not in transition, stopping current track to start the new one");
			start_state_change (mp, GST_STATE_READY, SET_NEXT_URI);
		}

	}

	return TRUE;
}

static void
impl_pause (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->playbin != NULL);

	start_state_change (mp, GST_STATE_PAUSED, STOP_TICK_TIMER);
}

static gboolean
impl_playing (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->playing;
}

static void
impl_set_volume (RBPlayer *player,
		 float volume)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	mp->priv->volume_changed++;
	if (mp->priv->volume_applied > 0) {
		set_playbin_volume (mp, volume);
		mp->priv->volume_applied = mp->priv->volume_changed;
	} else {
		/* volume will be applied in the first call to impl_play */
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

	rb_debug ("seeking to %" G_GINT64_FORMAT, time);
	gst_element_seek (mp->priv->playbin, 1.0,
			  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, time,
			  GST_SEEK_TYPE_NONE, -1);

	gst_element_get_state (mp->priv->playbin, NULL, NULL, 100 * GST_MSECOND);
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
need_pad_blocking (RBPlayerGst *mp)
{
	return (mp->priv->playing || (mp->priv->uri != NULL));
}

static gboolean
impl_add_tee (RBPlayerGstTee *player, GstElement *element)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->tee == NULL) {
		mp->priv->waiting_tees = g_list_prepend (mp->priv->waiting_tees, element);
		return TRUE;
	}

	return rb_gst_add_tee (RB_PLAYER (player), mp->priv->tee, element, need_pad_blocking (mp));
}

static gboolean
impl_remove_tee (RBPlayerGstTee *player, GstElement *element)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->tee == NULL) {
		gst_object_sink (element);
		mp->priv->waiting_tees = g_list_remove (mp->priv->waiting_tees, element);
		return TRUE;
	}

	return rb_gst_remove_tee (RB_PLAYER (mp), mp->priv->tee, element, need_pad_blocking (mp));
}

static gboolean
impl_add_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->filterbin == NULL) {
		mp->priv->waiting_filters = g_list_prepend (mp->priv->waiting_filters, element);
		return TRUE;
	}
	return rb_gst_add_filter (RB_PLAYER (mp), mp->priv->filterbin, element, need_pad_blocking (mp));
}

static gboolean
impl_remove_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->filterbin == NULL) {
		gst_object_sink (element);
		mp->priv->waiting_filters = g_list_remove (mp->priv->waiting_filters, element);
		return TRUE;
	}

	return rb_gst_remove_filter (RB_PLAYER (mp), mp->priv->filterbin, element, need_pad_blocking (mp));
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

