/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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
#include "rb-player-gst-filter.h"
#include "rb-player-gst-tee.h"
/*#include "rb-player-gst-data-tee.h"*/
#include "rb-player-gst.h"
#include "rb-player-gst-helper.h"

static void rb_player_init (RBPlayerIface *iface);
static void rb_player_gst_filter_init (RBPlayerGstFilterIface *iface);
static void rb_player_gst_tee_init (RBPlayerGstTeeIface *iface);
/*tatic void rb_player_gst_data_tee_init (RBPlayerGstDataTeeIface *iface);*/
static void rb_player_gst_finalize (GObject *object);
static void rb_player_gst_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static gboolean rb_player_gst_open (RBPlayer *player,
				    const char *uri,
				    gpointer stream_data,
				    GDestroyNotify stream_data_destroy,
				    GError **error);
static gboolean rb_player_gst_opened (RBPlayer *player);
static gboolean rb_player_gst_close (RBPlayer *player,
				     const char *uri,
				     GError **error);
static gboolean rb_player_gst_play (RBPlayer *player, gint crossfade, GError **error);
static void rb_player_gst_pause (RBPlayer *player);
static gboolean rb_player_gst_playing (RBPlayer *player);
static void rb_player_gst_set_volume (RBPlayer *player, float volume);
static float rb_player_gst_get_volume (RBPlayer *player);
static void rb_player_gst_set_replaygain (RBPlayer *player,
					  const char *uri,
					  double track_gain, double track_peak,
					  double album_gain, double album_peak);
static gboolean rb_player_gst_seekable (RBPlayer *player);
static void rb_player_gst_set_time (RBPlayer *player, long time);
static long rb_player_gst_get_time (RBPlayer *player);

G_DEFINE_TYPE_WITH_CODE(RBPlayerGst, rb_player_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER, rb_player_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_FILTER, rb_player_gst_filter_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_TEE, rb_player_gst_tee_init)
			/*G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER_GST_DATA_TEE, rb_player_gst_data_tee_init)*/
			)
#define RB_PLAYER_GST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYER_GST, RBPlayerGstPrivate))

#define RB_PLAYER_GST_TICK_HZ 5

enum
{
	PROP_0,
	PROP_PLAYBIN,
	PROP_BUS
};

enum
{
	MISSING_PLUGINS,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBPlayerGstPrivate
{
	char *uri;
	gpointer stream_data;
	GDestroyNotify stream_data_destroy;

	GstElement *playbin;
	GstElement *volume_handler;

	gboolean can_signal_direct_error;
	GError *error;
	gboolean emitted_error;

	gboolean playing;
	gboolean buffering;

	GList *waiting_tees;
	GstElement *sinkbin;
	GstElement *tee;

	GList *waiting_filters; /* in reverse order */
	GstElement *filterbin;

	float cur_volume;

	guint tick_timeout_id;
};

static gboolean rb_player_gst_sync_pipeline (RBPlayerGst *mp);
static void rb_player_gst_gst_free_playbin (RBPlayerGst *player);

static void
rb_player_gst_class_init (RBPlayerGstClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_player_gst_finalize;
	object_class->get_property = rb_player_gst_get_property;

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

static void
rb_player_init (RBPlayerIface *iface)
{
	iface->open = rb_player_gst_open;
	iface->opened = rb_player_gst_opened;
	iface->close = rb_player_gst_close;
	iface->play = rb_player_gst_play;
	iface->pause = rb_player_gst_pause;
	iface->playing = rb_player_gst_playing;
	iface->set_volume = rb_player_gst_set_volume;
	iface->get_volume = rb_player_gst_get_volume;
	iface->set_replaygain = rb_player_gst_set_replaygain;
	iface->seekable = rb_player_gst_seekable;
	iface->set_time = rb_player_gst_set_time;
	iface->get_time = rb_player_gst_get_time;
	iface->multiple_open = (RBPlayerFeatureFunc) rb_false_function;
}

static gboolean
tick_timeout (RBPlayerGst *mp)
{
	if (mp->priv->playing == FALSE)
		return TRUE;

	_rb_player_emit_tick (RB_PLAYER (mp), mp->priv->stream_data, rb_player_get_time (RB_PLAYER (mp)), -1);

	return TRUE;
}

static void
rb_player_gst_init (RBPlayerGst *mp)
{
	mp->priv = RB_PLAYER_GST_GET_PRIVATE (mp);
}

static void
rb_player_gst_get_property (GObject *object,
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_player_gst_finalize (GObject *object)
{
	RBPlayerGst *mp;

	mp = RB_PLAYER_GST (object);

	if (mp->priv->tick_timeout_id != 0)
		g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->playbin) {
		gst_element_set_state (mp->priv->playbin,
				       GST_STATE_NULL);

		rb_player_gst_gst_free_playbin (mp);
	}
	if (mp->priv->volume_handler) {
		g_object_unref (mp->priv->volume_handler);
		mp->priv->volume_handler = NULL;
	}

	if (mp->priv->waiting_tees) {
		g_list_foreach (mp->priv->waiting_tees, (GFunc)gst_object_sink, NULL);
	}
	g_list_free (mp->priv->waiting_tees);

	G_OBJECT_CLASS (rb_player_gst_parent_class)->finalize (object);
}

static void
rb_player_gst_gst_free_playbin (RBPlayerGst *player)
{
	if (player->priv->playbin == NULL)
		return;

	gst_object_unref (GST_OBJECT (player->priv->playbin));
	player->priv->playbin = NULL;
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
rb_player_gst_handle_missing_plugin_message (RBPlayerGst *player, GstMessage *message)
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
rb_player_gst_bus_cb (GstBus * bus, GstMessage * message, RBPlayerGst *mp)
{
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
		const GstStructure *s;
		gint progress;

		s = gst_message_get_structure (message);
		if (!gst_structure_get_int (s, "buffer-percent", &progress)) {
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

		_rb_player_emit_buffering (RB_PLAYER (mp),
					   mp->priv->stream_data,
					   progress);
		break;
	}
	case GST_MESSAGE_APPLICATION: {
		const GstStructure *structure;

		structure = gst_message_get_structure (message);
		_rb_player_emit_event (RB_PLAYER (mp),
				       mp->priv->stream_data,
				       gst_structure_get_name (structure),
				       NULL);
	}
	case GST_MESSAGE_ELEMENT: {
		if (gst_is_missing_plugin_message (message)) {
			rb_player_gst_handle_missing_plugin_message (mp, message);
		}
		break;
	}
	default:
		break;
	}

	/* emit message signals too, so plugins can process messages */
	gst_bus_async_signal_func (bus, message, NULL);

	return TRUE;
}

static gboolean
rb_player_gst_construct (RBPlayerGst *mp, GError **error)
{
	char *element_name = NULL;
	GstElement *sink;
	GstElement *fakesink;

	/* playbin */
	rb_debug ("constructing element \"playbin\"");
	mp->priv->playbin = gst_element_factory_make ("playbin", "playbin");
	if (mp->priv->playbin == NULL) {
		goto missing_element;
	}

	gst_bus_add_watch (gst_element_get_bus (GST_ELEMENT (mp->priv->playbin)),
			     (GstBusFunc) rb_player_gst_bus_cb, mp);

	/* let plugins add bits to playbin */
	g_object_notify (G_OBJECT (mp), "playbin");
	g_object_notify (G_OBJECT (mp), "bus");

	/* Use gconfaudiosink for audio if there's no audio sink yet */
	g_object_get (G_OBJECT (mp->priv->playbin), "audio-sink", &sink, NULL);
	if (sink == NULL) {
		sink = rb_player_gst_try_audio_sink ("gconfaudiosink", "audiosink");
		if (sink == NULL) {
			/* fall back to autoaudiosink */
			sink = rb_player_gst_try_audio_sink ("autoaudiosink", "audiosink");
		}

		if (sink != NULL) {
			g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", sink, NULL);
		}
	} else {
		g_object_unref (sink);
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


		/* set up the sinkbin with it's tee element */
		mp->priv->sinkbin = gst_bin_new (NULL);
		mp->priv->tee = gst_element_factory_make ("tee", NULL);
		queue = gst_element_factory_make ("queue", NULL);

		/* link it all together and insert */
		gst_bin_add_many (GST_BIN (mp->priv->sinkbin), mp->priv->filterbin, mp->priv->tee, queue, sink, NULL);
		gst_element_link_many (mp->priv->filterbin, mp->priv->tee, queue, sink, NULL);

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
	g_object_get (G_OBJECT (mp->priv->playbin), "video-sink", &sink, NULL);
	if (sink == NULL) {
		fakesink = gst_element_factory_make ("fakesink", "fakesink");
		g_object_set (G_OBJECT (mp->priv->playbin), "video-sink", fakesink, NULL);
	} else {
		g_object_unref (sink);
	}

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	rb_player_set_volume (RB_PLAYER (mp), mp->priv->cur_volume);

	rb_debug ("pipeline construction complete");
	return TRUE;
missing_element:
	{
		char *err = g_strdup_printf (_("Failed to create %s element; check your installation"),
					     element_name);
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     "%s", err);
		g_free (err);
		rb_player_gst_gst_free_playbin (mp);
		return FALSE;
	}
}

RBPlayer *
rb_player_gst_new (GError **error)
{
	RBPlayerGst *mp;

	mp = RB_PLAYER_GST (g_object_new (RB_TYPE_PLAYER_GST, NULL, NULL));

	return RB_PLAYER (mp);
}

static gboolean
rb_player_gst_sync_pipeline (RBPlayerGst *mp)
{
	rb_debug ("syncing pipeline");
	if (mp->priv->playing) {
 		rb_debug ("PLAYING pipeline");
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
 			return FALSE;
		}
	} else {
		rb_debug ("PAUSING pipeline");
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
			return FALSE;
		}
	}

	/* FIXME: Set up a timeout to watch if the pipeline doesn't
	 * go to PAUSED/PLAYING within some time (5 secs maybe?)
	 */
	return TRUE;
}

/* Start a sequence of synchronous GStreamer operations in which we
 * can receive an error signal.
 */
static void
begin_gstreamer_operation (RBPlayerGst *mp)
{
	g_assert (mp->priv->error == NULL);
	mp->priv->can_signal_direct_error = TRUE;
}

/* End a sequence of synchronous operations and propagate any
 * error from the sequence into error.
 */
static void
end_gstreamer_operation (RBPlayerGst *mp, gboolean op_failed, GError **error)
{
	mp->priv->can_signal_direct_error = FALSE;
	if (mp->priv->error) {
		g_propagate_error (error, mp->priv->error);
		mp->priv->error = NULL;
	} else if (op_failed) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_GENERAL,
			     _("Unknown playback error"));
	}

}

static void
cdda_got_source_cb (GObject *object, GParamSpec *pspec, char *device)
{
	GstElement *source;

	g_object_get (object, "source", &source, NULL);
	rb_debug ("got source %p", source);
	if (source) {
		g_signal_handlers_disconnect_by_func (object, cdda_got_source_cb, device);

		g_object_set (G_OBJECT (source), "device", device, NULL);
		g_free (device);

		if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "paranoia-mode"))
			g_object_set (G_OBJECT (source), "paranoia-mode", 0, NULL);

		if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "read-speed"))
			g_object_set (G_OBJECT (source), "read-speed", 1, NULL);
	}
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
rb_player_gst_open (RBPlayer *player,
		const char *uri,
		gpointer stream_data,
		GDestroyNotify stream_data_destroy,
		GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean cdda_seek = FALSE;

	if (mp->priv->playbin == NULL) {
		if (!rb_player_gst_construct (mp, error))
			return FALSE;
	} else {
		if (!rb_player_close (player, NULL, error))
			return FALSE;
	}

	g_assert (mp->priv->playbin != NULL);

	if (uri == NULL) {
		_destroy_stream_data (mp);
		g_free (mp->priv->uri);
		mp->priv->uri = NULL;
		mp->priv->playing = FALSE;
		mp->priv->buffering = FALSE;
		return TRUE;
	}

	/* check if we are switching tracks on a cd, so we don't have to close the device */
	if (g_str_has_prefix (uri, "cdda://")) {
		const char *old_device = NULL;
		const char *new_device;

		if (mp->priv->uri && g_str_has_prefix (mp->priv->uri, "cdda://"))
			old_device = g_utf8_strchr (mp->priv->uri, -1, '#');
		new_device = g_utf8_strchr (uri, -1, '#');

		if (old_device && strcmp (old_device, new_device) == 0) {
			/* just seek, instead of having playbin close the device */
			GstFormat track_format = gst_format_get_by_nick ("track");
			char *track_str;
			guint track;
			guint cdda_len;

			cdda_len = strlen ("cdda://");
			track_str = g_strndup (uri + cdda_len, new_device - (uri + cdda_len));
			track = atoi (track_str);
			g_free (track_str);

			rb_debug ("seeking to track %d on CD device %s", track, new_device);

			if (gst_element_seek (mp->priv->playbin, 1.0,
					      track_format, GST_SEEK_FLAG_FLUSH,
					      GST_SEEK_TYPE_SET, track,
					      GST_SEEK_TYPE_NONE, -1))
				cdda_seek = TRUE;
		} else {
			/* +1 to skip the '#' */
			char *device = g_strdup (new_device + 1);

			rb_debug ("waiting for source element for CD device %s", device);
			g_signal_connect (G_OBJECT (mp->priv->playbin),
					  "notify::source",
					  G_CALLBACK (cdda_got_source_cb),
					  device);
		}
	}

	begin_gstreamer_operation (mp);
	_destroy_stream_data (mp);
	g_free (mp->priv->uri);
	mp->priv->uri = g_strdup (uri);
	mp->priv->stream_data = stream_data;
	mp->priv->stream_data_destroy = stream_data_destroy;
	mp->priv->emitted_error = FALSE;

	if (!cdda_seek) {
		g_object_set (G_OBJECT (mp->priv->playbin), "uri", uri, NULL);
	}

	if (!rb_player_gst_sync_pipeline (mp)) {
		end_gstreamer_operation (mp, TRUE, error);
		rb_player_gst_close (player, uri, NULL);
		return FALSE;
	}

	if (mp->priv->tick_timeout_id == 0)
		mp->priv->tick_timeout_id = g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ, (GSourceFunc) tick_timeout, mp);

	end_gstreamer_operation (mp, FALSE, error);
	return TRUE;
}

static gboolean
rb_player_gst_close (RBPlayer *player, const char *uri, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean ret;

	mp->priv->playing = FALSE;
	mp->priv->buffering = FALSE;

	if ((uri != NULL) && (mp->priv->uri != NULL) && strcmp (mp->priv->uri, uri) == 0) {
		rb_debug ("URI doesn't match current playing URI; ignoring");
		return TRUE;
	}

	_destroy_stream_data (mp);
	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}

	if (mp->priv->playbin == NULL)
		return TRUE;

	begin_gstreamer_operation (mp);
	ret = gst_element_set_state (mp->priv->playbin, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS;
	end_gstreamer_operation (mp, !ret, error);

	if (mp->priv->volume_handler) {
		g_object_unref (mp->priv->volume_handler);
		mp->priv->volume_handler = NULL;
	}

	return ret;
}

static gboolean
rb_player_gst_opened (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->uri != NULL;
}

static gboolean
rb_player_gst_play (RBPlayer *player, gint crossfade, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean ret;

	mp->priv->playing = TRUE;
	mp->priv->buffering = FALSE;

	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	begin_gstreamer_operation (mp);
	ret = rb_player_gst_sync_pipeline (mp);
	end_gstreamer_operation (mp, !ret, error);

	_rb_player_emit_playing_stream (RB_PLAYER (mp), mp->priv->stream_data);

	if (mp->priv->tick_timeout_id == 0)
		mp->priv->tick_timeout_id = g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ, (GSourceFunc) tick_timeout, mp);

	return ret;
}

static void
rb_player_gst_pause (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->playbin != NULL);

	rb_player_gst_sync_pipeline (mp);

	if (mp->priv->tick_timeout_id != 0) {
		g_source_remove (mp->priv->tick_timeout_id);
		mp->priv->tick_timeout_id = 0;
	}
}

static gboolean
rb_player_gst_playing (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->playing;
}

static gboolean
emit_volume_changed_idle (RBPlayerGst *mp)
{
	_rb_player_emit_volume_changed (RB_PLAYER (mp), mp->priv->cur_volume);
	return FALSE;
}

static void
stream_volume_changed (GObject *element, GParamSpec *pspec, RBPlayerGst *mp)
{
	gdouble v;
	g_object_get (element, "volume", &v, NULL);
	mp->priv->cur_volume = v;

	g_idle_add ((GSourceFunc) emit_volume_changed_idle, mp);
}

static void
find_volume_handler (RBPlayerGst *mp)
{
	/* look for a 'volume' property provided by the sink */
	if (mp->priv->volume_handler == NULL && mp->priv->playbin != NULL) {
		GstElement *sink;

		g_object_get (mp->priv->playbin, "audio-sink", &sink, NULL);
		if (sink != NULL) {
			mp->priv->volume_handler = rb_player_gst_find_element_with_property (sink, "volume");
			g_object_unref (sink);
		}

		if (mp->priv->volume_handler == NULL) {
			mp->priv->volume_handler = g_object_ref (mp->priv->playbin);
		}

		g_signal_connect_object (mp->priv->volume_handler,
					 "notify::volume",
					 G_CALLBACK (stream_volume_changed),
					 mp, 0);
	}
}

static void
rb_player_gst_set_replaygain (RBPlayer *player,
			      const char *uri,
			      double track_gain, double track_peak,
			      double album_gain, double album_peak)
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

	find_volume_handler (mp);
	if (mp->priv->volume_handler != NULL) {
		GParamSpec *volume_pspec;
		GValue val = {0,};

		volume_pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (mp->priv->volume_handler),
							     "volume");
		g_value_init (&val, G_TYPE_DOUBLE);

		g_value_set_double (&val, mp->priv->cur_volume * scale);
		if (g_param_value_validate (volume_pspec, &val))
			rb_debug ("replay gain too high, reducing value to %f", g_value_get_double (&val));

		g_object_set_property (G_OBJECT (mp->priv->volume_handler), "volume", &val);
		g_value_unset (&val);
	}
}

static void
rb_player_gst_set_volume (RBPlayer *player,
			  float volume)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	find_volume_handler (mp);
	if (mp->priv->volume_handler != NULL) {
		g_object_set (mp->priv->volume_handler, "volume", volume, NULL);
	}

	mp->priv->cur_volume = volume;
}

static float
rb_player_gst_get_volume (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->cur_volume;
}

static gboolean
rb_player_gst_seekable (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	/* Need to send a seekable query on playbin */
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
rb_player_gst_set_time (RBPlayer *player, long time)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->playbin != NULL);

	if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
		/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
		if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
			g_warning ("Failed to pause pipeline before seek");
		}
	}

	gst_element_seek (mp->priv->playbin, 1.0,
			  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, time * GST_SECOND,
			  GST_SEEK_TYPE_NONE, -1);

	if (mp->priv->playing) {
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);
	}
}

static long
rb_player_gst_get_time (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	if (mp->priv->playbin != NULL) {
		gint64 position = -1;
		GstFormat fmt = GST_FORMAT_TIME;

		gst_element_query_position (mp->priv->playbin, &fmt, &position);
		if (position != -1)
			position /= GST_SECOND;

		return (long) position;
	} else
		return -1;
}


static gboolean
rb_player_gst_add_tee (RBPlayerGstTee *player, GstElement *element)
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
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
			/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
			if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
				g_warning ("Failed to pause pipeline before tee insertion");
				return FALSE;
			}
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
rb_player_gst_remove_tee (RBPlayerGstTee *player, GstElement *element)
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
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
			/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
			if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
				g_warning ("Failed to pause pipeline before eee insertion");
				return FALSE;
			}
		}
	}

	/* get the containing bin and unlink it */
	bin = GST_ELEMENT (gst_element_get_parent (element));

	if (gst_element_set_state (bin, GST_STATE_NULL) == GST_STATE_CHANGE_ASYNC) {
		/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
		if (gst_element_get_state (bin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
			g_warning ("Failed to pause pipeline before tee insertion");
			return FALSE;
		}
	}

	gst_bin_remove (GST_BIN (mp->priv->sinkbin), bin);
	gst_object_unref (bin);

	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);

	return TRUE;
}

static gboolean
rb_player_gst_add_filter (RBPlayerGstFilter *player, GstElement *element)
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
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
			/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
			if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
				g_warning ("Failed to pause pipeline before filter insertion");
				return FALSE;
			}
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
rb_player_gst_remove_filter (RBPlayerGstFilter *player, GstElement *element)
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
		/* it'd be more fun to do this by blocking a pad.. */

		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
			/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
			if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
				g_warning ("Failed to pause pipeline before filter insertion");
				return FALSE;
			}
		}
	}

	/* get the containing bin and unlink it */
	bin = GST_ELEMENT (gst_element_get_parent (element));

	if (gst_element_set_state (bin, GST_STATE_NULL) == GST_STATE_CHANGE_ASYNC) {
		/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
		if (gst_element_get_state (bin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
			g_warning ("Failed to pause pipeline before filter insertion");
			return FALSE;
		}
	}

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

/*static gboolean
rb_player_gst_add_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{

}

static gboolean
rb_player_gst_remove_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{

}*/

static void
rb_player_gst_filter_init (RBPlayerGstFilterIface *iface)
{
	iface->add_filter = rb_player_gst_add_filter;
	iface->remove_filter = rb_player_gst_remove_filter;
}

static void
rb_player_gst_tee_init (RBPlayerGstTeeIface *iface)
{
	iface->add_tee = rb_player_gst_add_tee;
	iface->remove_tee = rb_player_gst_remove_tee;
}

/*static void
rb_player_gst_data_tee_init (RBPlayerGstDataTeeIface *iface)
{
	iface->add_data_tee = rb_player_gst_add_data_tee;
	iface->remove_data_tee = rb_player_gst_remove_data_tee;
}*/


