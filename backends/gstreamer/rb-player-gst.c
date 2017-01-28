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
#include <gst/audio/streamvolume.h>
#include <gst/pbutils/pbutils.h>

#include "rb-debug.h"
#include "rb-util.h"

#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-player-gst-helper.h"
#include "rb-player-gst-filter.h"

static void rb_player_init (RBPlayerIface *iface);
static void rb_player_gst_filter_init (RBPlayerGstFilterIface *iface);

static void state_change_finished (RBPlayerGst *mp, GError *error);

G_DEFINE_TYPE_WITH_CODE(RBPlayerGst, rb_player_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER, rb_player_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_FILTER, rb_player_gst_filter_init)
			)

#define RB_PLAYER_GST_TICK_HZ 5
#define STATE_CHANGE_MESSAGE_TIMEOUT 5

enum
{
	PROP_0,
	PROP_PLAYBIN,
	PROP_BUS
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

	gboolean playing;
	gboolean buffering;

	gboolean stream_change_pending;
	gboolean current_track_finishing;
	gboolean playbin_stream_changing;
	gboolean track_change;
	gboolean emitted_image;

	gboolean emitted_error;

	GList *stream_tags;

	gint volume_changed;
	gint volume_applied;
	float cur_volume;

	guint tick_timeout_id;
	guint emit_stream_idle_id;

	GList *waiting_filters; /* in reverse order */
	GstElement *filterbin;

	GMutex eos_lock;
	GCond eos_cond;
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

static gboolean
about_to_finish_idle (RBPlayerGst *player)
{
	_rb_player_emit_eos (RB_PLAYER (player), player->priv->stream_data, TRUE);

	g_mutex_lock (&player->priv->eos_lock);
	g_cond_signal (&player->priv->eos_cond);
	g_mutex_unlock (&player->priv->eos_lock);
	return FALSE;
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

	player->priv->current_track_finishing = TRUE;

	g_mutex_lock (&player->priv->eos_lock);
	g_idle_add_full (G_PRIORITY_HIGH, (GSourceFunc) about_to_finish_idle, player, NULL);

	g_cond_wait (&player->priv->eos_cond, &player->priv->eos_lock);
	g_mutex_unlock (&player->priv->eos_lock);
}

static gboolean
emit_volume_changed_idle (RBPlayerGst *player)
{
	double vol;

	if (GST_IS_STREAM_VOLUME (player->priv->playbin)) {
		vol = gst_stream_volume_get_volume (GST_STREAM_VOLUME (player->priv->playbin),
						    GST_STREAM_VOLUME_FORMAT_CUBIC);
	} else {
		vol = player->priv->cur_volume;
	}

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
		if (player->priv->stream_change_pending || (player->priv->emitted_image == FALSE)) {
			GdkPixbuf *pixbuf;
			pixbuf = rb_gst_process_embedded_image (list, tag);
			if (pixbuf != NULL) {
				_rb_player_emit_image (RB_PLAYER (player),
						       player->priv->stream_data,
						       pixbuf);
				g_object_unref (pixbuf);
				player->priv->emitted_image = TRUE;
			}
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

static gboolean
actually_emit_stream_and_tags (RBPlayerGst *player)
{
	GList *t;

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

	player->priv->emit_stream_idle_id = 0;
	return FALSE;
}

static void
emit_playing_stream_and_tags (RBPlayerGst *player, gboolean track_change)
{
	if (track_change) {
		/* swap stream data */
		_destroy_stream_data (player);
		player->priv->stream_data = player->priv->next_stream_data;
		player->priv->stream_data_destroy = player->priv->next_stream_data_destroy;
		player->priv->next_stream_data = NULL;
		player->priv->next_stream_data_destroy = NULL;
	}

	if (rb_is_main_thread ()) {
		if (player->priv->emit_stream_idle_id != 0) {
			g_source_remove (player->priv->emit_stream_idle_id);
		}
		actually_emit_stream_and_tags (player);
	} else if (player->priv->emit_stream_idle_id == 0) {
		player->priv->emit_stream_idle_id = g_idle_add ((GSourceFunc) actually_emit_stream_and_tags, player);
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
set_playbin_volume (RBPlayerGst *player, float volume)
{
	/* ignore the deep-notify we get directly from the sink, as it causes deadlock.
	 * we still get another one anyway.
	 */
	g_signal_handlers_block_by_func (player->priv->playbin, volume_notify_cb, player);
	if (GST_IS_STREAM_VOLUME (player->priv->playbin))
		gst_stream_volume_set_volume (GST_STREAM_VOLUME (player->priv->playbin),
					      GST_STREAM_VOLUME_FORMAT_CUBIC, volume);
	else
		g_object_set (player->priv->playbin, "volume", volume, NULL);
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

	rb_debug ("changing state to %s", gst_element_state_get_name (state));
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
			GstBus *bus;

			/* flush bus to ensure tags from the previous stream don't
			 * get applied to the new one
			 */
			bus = gst_element_get_bus (mp->priv->playbin);
			gst_bus_set_flushing (bus, TRUE);
			gst_bus_set_flushing (bus, FALSE);
			gst_object_unref (bus);

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
		char *debug = NULL;
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
				g_set_error (&sig_error,
					     RB_PLAYER_ERROR,
					     code,
					     /* Translators: the parameter here is an error message */
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
			if (mp->priv->playbin_stream_changing) {
				emit_playing_stream_and_tags (mp, TRUE);
			}
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
		if (mp->priv->stream_change_pending) {
			rb_debug ("got EOS with stream change pending");
			start_state_change (mp, GST_STATE_READY, SET_NEXT_URI);
		} else {
			_rb_player_emit_eos (RB_PLAYER (mp), mp->priv->stream_data, FALSE);
		}
		break;

	case GST_MESSAGE_STATE_CHANGED:
		{
			GstState oldstate;
			GstState newstate;
			GstState pending;
			gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
			if (GST_MESSAGE_SRC (message) == GST_OBJECT (mp->priv->playbin)) {
				if (pending == GST_STATE_VOID_PENDING) {
					rb_debug ("playbin reached state %s", gst_element_state_get_name (newstate));
					state_change_finished (mp, NULL);
				}
			}
			break;
		}

	case GST_MESSAGE_TAG: {
		GstTagList *tags;

		if (mp->priv->playbin_stream_changing) {
			rb_debug ("ignoring tags during playbin stream change");
			break;
		}

		gst_message_parse_tag (message, &tags);

		if (mp->priv->stream_change_pending) {
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

			rb_debug ("buffering - temporarily pausing playback");
			gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED);
			mp->priv->buffering = TRUE;
		}

		_rb_player_emit_buffering (RB_PLAYER (mp), mp->priv->stream_data, progress);
		break;
	}

	case GST_MESSAGE_APPLICATION:
		structure = gst_message_get_structure (message);
		_rb_player_emit_event (RB_PLAYER (mp), mp->priv->stream_data, gst_structure_get_name (structure), NULL);
		break;

	case GST_MESSAGE_STREAM_START:
		if (mp->priv->playbin_stream_changing) {
			rb_debug ("got STREAM_START message");
			mp->priv->playbin_stream_changing = FALSE;
			emit_playing_stream_and_tags (mp, TRUE);
		}
		break;

	case GST_MESSAGE_ELEMENT:
		structure = gst_message_get_structure (message);
		if (gst_is_missing_plugin_message (message)) {
			handle_missing_plugin_message (mp, message);
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
source_setup_cb (GstElement *playbin, GstElement *source, RBPlayerGst *player)
{
	g_signal_emit (player, signals[PREPARE_SOURCE], 0, player->priv->uri, source);
}

static gboolean
construct_pipeline (RBPlayerGst *mp, GError **error)
{
	GstElement *sink;
	GList *l;

	mp->priv->playbin = gst_element_factory_make ("playbin", NULL);
	if (mp->priv->playbin == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Failed to create playbin element; check your GStreamer installation"));
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
				 "source-setup",
				 G_CALLBACK (source_setup_cb),
				 mp, 0);

	gst_bus_add_watch (gst_element_get_bus (mp->priv->playbin),
			   (GstBusFunc) bus_cb,
			   mp);

	/* let plugins add bits to playbin */
	g_object_notify (G_OBJECT (mp), "playbin");
	g_object_notify (G_OBJECT (mp), "bus");

	g_object_get (mp->priv->playbin, "audio-sink", &mp->priv->audio_sink, NULL);
	if (mp->priv->audio_sink == NULL) {
		mp->priv->audio_sink = rb_player_gst_try_audio_sink ("autoaudiosink", NULL);
		if (mp->priv->audio_sink == NULL) {
			g_set_error (error,
				     RB_PLAYER_ERROR,
				     RB_PLAYER_ERROR_GENERAL,
				     _("Failed to create %s element; check your GStreamer installation"),
				     "autoaudiosink");
			return FALSE;
		}
		g_object_set (mp->priv->playbin, "audio-sink", mp->priv->audio_sink, NULL);
	} else {
		rb_debug ("existing audio sink found");
		g_object_unref (mp->priv->audio_sink);
	}
	g_object_set (mp->priv->playbin, "audio-sink", mp->priv->audio_sink, NULL);

	/* setup filterbin */
	mp->priv->filterbin = rb_gst_create_filter_bin ();
	g_object_set (mp->priv->playbin, "audio-filter", mp->priv->filterbin, NULL);

	/* add any filters that have already been added */
	for (l = mp->priv->waiting_filters; l != NULL; l = g_list_next (l)) {
		rb_player_gst_filter_add_filter (RB_PLAYER_GST_FILTER(mp), GST_ELEMENT (l->data));
	}
	g_list_free (mp->priv->waiting_filters);
	mp->priv->waiting_filters = NULL;

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
	mp->priv->emitted_image = FALSE;

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
		switch (play_type) {
		case RB_PLAYER_PLAY_AFTER_EOS:
			rb_debug ("current track finishing -> just setting URI on playbin");
			g_object_set (mp->priv->playbin, "uri", mp->priv->uri, NULL);
			mp->priv->playbin_stream_changing = TRUE;
			track_change_done (mp, NULL);
			break;

		case RB_PLAYER_PLAY_REPLACE:
		case RB_PLAYER_PLAY_CROSSFADE:
			rb_debug ("current track finishing, waiting for EOS to start next");
			break;

		default:
			g_assert_not_reached ();
		}
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

		gst_element_query_position (mp->priv->playbin, fmt, &position);
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
		gst_object_ref_sink (element);
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

	g_mutex_init (&mp->priv->eos_lock);
	g_cond_init (&mp->priv->eos_cond);
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
		g_value_set_object (value, mp->priv->playbin ? g_object_ref (mp->priv->playbin) : NULL);
		break;
	case PROP_BUS:
		if (mp->priv->playbin) {
			GstBus *bus;
			bus = gst_element_get_bus (mp->priv->playbin);
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
	/*RBPlayerGst *mp = RB_PLAYER_GST (object);*/

	switch (prop_id) {
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

	if (mp->priv->emit_stream_idle_id != 0) {
		g_source_remove (mp->priv->emit_stream_idle_id);
		mp->priv->emit_stream_idle_id = 0;
	}

	if (mp->priv->playbin != NULL) {
		gst_element_set_state (mp->priv->playbin, GST_STATE_NULL);
		g_object_unref (mp->priv->playbin);
		mp->priv->playbin = NULL;
		mp->priv->audio_sink = NULL;
	}

	if (mp->priv->waiting_filters != NULL) {
		g_list_foreach (mp->priv->waiting_filters, (GFunc)gst_object_ref_sink, NULL);
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

	signals[PREPARE_SOURCE] =
		g_signal_new ("prepare-source",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, prepare_source),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[CAN_REUSE_STREAM] =
		g_signal_new ("can-reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, can_reuse_stream),
			      NULL, NULL,
			      NULL,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRING, G_TYPE_STRING, GST_TYPE_ELEMENT);
	signals[REUSE_STREAM] =
		g_signal_new ("reuse-stream",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerGstClass, reuse_stream),
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

	g_type_class_add_private (klass, sizeof (RBPlayerGstPrivate));
}

