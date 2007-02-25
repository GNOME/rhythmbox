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
#ifdef HAVE_GSTREAMER_0_8
#include <gst/gconf/gconf.h>
#endif
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-debug.h"
#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-debug.h"
#include "rb-marshal.h"

static void rb_player_init (RBPlayerIface *iface);
static void rb_player_gst_finalize (GObject *object);
static void rb_player_gst_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static gboolean rb_player_gst_open (RBPlayer *player,
				    const char *uri,
				    GError **error);
static gboolean rb_player_gst_opened (RBPlayer *player);
static gboolean rb_player_gst_close (RBPlayer *player, GError **error);
static gboolean rb_player_gst_play (RBPlayer *player, GError **error);
static void rb_player_gst_pause (RBPlayer *player);
static gboolean rb_player_gst_playing (RBPlayer *player);
static void rb_player_gst_set_volume (RBPlayer *player, float volume);
static float rb_player_gst_get_volume (RBPlayer *player);
static void rb_player_gst_set_replaygain (RBPlayer *player,
				      double track_gain, double track_peak,
				      double album_gain, double album_peak);
static gboolean rb_player_gst_seekable (RBPlayer *player);
static void rb_player_gst_set_time (RBPlayer *player, long time);
static long rb_player_gst_get_time (RBPlayer *player);

G_DEFINE_TYPE_WITH_CODE(RBPlayerGst, rb_player_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_PLAYER,
					      rb_player_init))
#define RB_PLAYER_GST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYER_GST, RBPlayerGstPrivate))

#define RB_PLAYER_GST_TICK_HZ 5

enum
{
	PROP_0,
	PROP_PLAYBIN
};

struct _RBPlayerGstPrivate
{
	char *uri;

	GstElement *playbin;

	gboolean can_signal_direct_error;
	GError *error;
	gboolean emitted_error;

	gboolean playing;
	gboolean buffering;

	guint idle_error_id;
	guint idle_eos_id;
	guint idle_buffering_id;
	GHashTable *idle_info_ids;

#ifdef HAVE_GSTREAMER_0_8
	guint error_signal_id;
	guint buffering_signal_id;
#endif

	float cur_volume;

	guint tick_timeout_id;
};

typedef enum
{
	EOS,
	INFO,
	ERROR,
	TICK,
	BUFFERING,
	EVENT,
} RBPlayerGstSignalType;

typedef struct
{
	int type;
	RBPlayerGst *object;
	RBMetaDataField info_field;
	GError *error;
	GValue *info;
	guint id;
} RBPlayerGstSignal;

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
}

static gboolean
tick_timeout (RBPlayerGst *mp)
{
	if (mp->priv->playing == FALSE)
		return TRUE;

	_rb_player_emit_tick (RB_PLAYER (mp), rb_player_get_time (RB_PLAYER (mp)));

	return TRUE;
}

static void
rb_player_gst_init (RBPlayerGst *mp)
{
	mp->priv = RB_PLAYER_GST_GET_PRIVATE (mp);
	mp->priv->idle_info_ids = g_hash_table_new (NULL, NULL);

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
	g_hash_table_destroy (mp->priv->idle_info_ids);

	if (mp->priv->playbin) {
#ifdef HAVE_GSTREAMER_0_8
		g_signal_handler_disconnect (G_OBJECT (mp->priv->playbin),
					     mp->priv->error_signal_id);
		g_signal_handler_disconnect (G_OBJECT (mp->priv->playbin),
					     mp->priv->buffering_signal_id);
#endif

		gst_element_set_state (mp->priv->playbin,
				       GST_STATE_NULL);

		rb_player_gst_gst_free_playbin (mp);
	}

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
destroy_idle_signal (gpointer signal_pointer)
{
	RBPlayerGstSignal *signal = signal_pointer;

	if (signal->error)
		g_error_free (signal->error);

	if (signal->info) {
		g_value_unset (signal->info);
		g_free (signal->info);
	}

	if (signal->id != 0) {
		g_hash_table_remove (signal->object->priv->idle_info_ids,
				     GUINT_TO_POINTER (signal->id));
	}

	g_object_unref (G_OBJECT (signal->object));
	g_free (signal);

}

static gboolean
emit_signal_idle (RBPlayerGstSignal *signal)
{
	switch (signal->type) {
	case ERROR:
		_rb_player_emit_error (RB_PLAYER (signal->object), signal->error);

		/* close if not already closing */
		if (signal->object->priv->uri != NULL)
			rb_player_close (RB_PLAYER (signal->object), NULL);

		break;

	case EOS:
		_rb_player_emit_eos (RB_PLAYER (signal->object));
		signal->object->priv->idle_eos_id = 0;
		break;

	case INFO:
		_rb_player_emit_info (RB_PLAYER (signal->object), signal->info_field, signal->info);
		break;

	case BUFFERING:
		_rb_player_emit_buffering (RB_PLAYER (signal->object), g_value_get_uint (signal->info));
		signal->object->priv->idle_buffering_id = 0;
		break;
	case EVENT:
		_rb_player_emit_event (RB_PLAYER (signal->object), g_value_get_string (signal->info), NULL);
		break;
	}

	return FALSE;
}

#ifdef HAVE_GSTREAMER_0_8
static void
eos_cb (GstElement *element,
	RBPlayerGst *mp)
{
	RBPlayerGstSignal *signal;
	signal = g_new0 (RBPlayerGstSignal, 1);
	signal->type = EOS;
	signal->object = mp;
	g_object_ref (G_OBJECT (mp));
	if (mp->priv->idle_eos_id)
		g_source_remove (mp->priv->idle_eos_id);
	mp->priv->idle_eos_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
						 (GSourceFunc) emit_signal_idle,
						 signal,
						 destroy_idle_signal);
}

static void
error_cb (GstElement *element,
	  GstElement *source,
	  const GError *error,
	  gchar *debug,
	  RBPlayerGst *mp)
{
	int code;
	RBPlayerGstSignal *signal;

	/* the handler shouldn't get called with error=NULL, but sometimes it does */
	if (error == NULL)
		return;

	if ((error->domain == GST_CORE_ERROR)
	    || (error->domain == GST_LIBRARY_ERROR)
	    || (error->code == GST_RESOURCE_ERROR_BUSY)) {
		code = RB_PLAYER_ERROR_NO_AUDIO;
	} else {
		code = RB_PLAYER_ERROR_GENERAL;
	}

	/* If we've already got an error, ignore 'internal data flow error'
	 * type messages, as they're too generic to be helpful.
	 */
	if (mp->priv->emitted_error &&
	    error->domain == GST_STREAM_ERROR &&
	    error->code == GST_STREAM_ERROR_FAILED) {
		rb_debug ("Ignoring generic error \"%s\"", error->message);
		return;
	}

	/* If we're in a synchronous op, we can signal the error directly */
	if (mp->priv->can_signal_direct_error) {
		if (mp->priv->error) {
			g_warning ("Overwriting previous error \"%s\" with new error \"%s\"",
				   mp->priv->error->message,
				   error->message);
			g_error_free (mp->priv->error);
		}
		mp->priv->emitted_error = TRUE;
		mp->priv->error = g_error_copy (error);
		return;
	}

	signal = g_new0 (RBPlayerGstSignal, 1);
	signal->type = ERROR;
	signal->object = mp;
	signal->error = g_error_new_literal (RB_PLAYER_ERROR,
					     code,
					     error->message);

	g_object_ref (G_OBJECT (mp));

	if (mp->priv->idle_error_id)
		g_source_remove (mp->priv->idle_error_id);
	mp->priv->idle_error_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
						   (GSourceFunc) emit_signal_idle,
						   signal,
						   destroy_idle_signal);
	mp->priv->emitted_error = TRUE;
}
#endif

static void
process_tag (const GstTagList *list, const gchar *tag, RBPlayerGst *player)
{
	int count;
	RBMetaDataField field;
	RBPlayerGstSignal *signal;
	const GValue *val;
	GValue *newval;

	count = gst_tag_list_get_tag_size (list, tag);
	if (count < 1)
		return;

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
	newval = g_new0 (GValue, 1);
	switch (field) {
	case RB_METADATA_FIELD_BITRATE:
		g_value_init (newval, G_TYPE_ULONG);
		break;

	case RB_METADATA_FIELD_TITLE:
	case RB_METADATA_FIELD_GENRE:
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
	default:
		g_value_init (newval, G_TYPE_STRING);
		break;
	}
	val = gst_tag_list_get_value_index (list, tag, 0);
	if (!g_value_transform (val, newval)) {
		rb_debug ("Could not transform tag value type %s into %s",
			  g_type_name (G_VALUE_TYPE (val)),
			  g_type_name (G_VALUE_TYPE (newval)));
		return;
	}

	signal = g_new0 (RBPlayerGstSignal, 1);
	signal->object = player;
	signal->info_field = field;
	signal->info = newval;
	signal->type = INFO;
	signal->id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				      (GSourceFunc) emit_signal_idle,
				      signal,
				      destroy_idle_signal);

	g_object_ref (G_OBJECT (player));
	g_hash_table_insert (player->priv->idle_info_ids, GUINT_TO_POINTER (signal->id), NULL);
}

#ifdef HAVE_GSTREAMER_0_8
static void
found_tag_cb (GObject *pipeline, GstElement *source, GstTagList *tags, RBPlayerGst *player)
{
	gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, player);
}

static void
buffering_cb (GstElement *element, gint progress, RBPlayerGst *mp)
{
	RBPlayerGstSignal *signal;

	signal = g_new0 (RBPlayerGstSignal, 1);
	signal->type = BUFFERING;

	g_object_ref (G_OBJECT (mp));
	signal->object = mp;

	signal->info = g_new0 (GValue, 1);
	g_value_init (signal->info, G_TYPE_UINT);
	g_value_set_uint (signal->info, (guint)progress);
	if (mp->priv->idle_buffering_id)
		g_source_remove (mp->priv->idle_buffering_id);
	mp->priv->idle_buffering_id = g_idle_add ((GSourceFunc) emit_signal_idle, signal);
}
#elif HAVE_GSTREAMER_0_10
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
			_rb_player_emit_error (RB_PLAYER (mp), sig_error);
		}

		/* close if not already closing */
		if (mp->priv->uri != NULL)
			rb_player_close (RB_PLAYER (mp), NULL);

		g_error_free (error);
		g_free (debug);
		break;
	}
	case GST_MESSAGE_EOS:
		_rb_player_emit_eos (RB_PLAYER (mp));
		break;
	case GST_MESSAGE_TAG: {
		GstTagList *tags;
		gst_message_parse_tag (message, &tags);

		gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, mp);
		gst_tag_list_free (tags);
		break;
	}
	case GST_MESSAGE_BUFFERING: {
		RBPlayerGstSignal *signal;
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
		signal = g_new0 (RBPlayerGstSignal, 1);
		signal->type = BUFFERING;

		g_object_ref (G_OBJECT (mp));
		signal->object = mp;
		signal->info = g_new0 (GValue, 1);
		g_value_init (signal->info, G_TYPE_UINT);
		g_value_set_uint (signal->info, (guint)progress);
		g_idle_add ((GSourceFunc) emit_signal_idle, signal);
		break;
	}
	case GST_MESSAGE_APPLICATION: {
		RBPlayerGstSignal *signal;
		const GstStructure *structure;

		structure = gst_message_get_structure (message);
		signal = g_new0 (RBPlayerGstSignal, 1);
		signal->type = EVENT;

		g_object_ref (G_OBJECT (mp));
		signal->object = mp;
		signal->info = g_new0 (GValue, 1);
		g_value_init (signal->info, G_TYPE_STRING);
		g_value_set_string (signal->info, gst_structure_get_name (structure));
		g_idle_add ((GSourceFunc) emit_signal_idle, signal);
	}
	default:
		break;
	}

	return TRUE;
}

#endif

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

#ifdef HAVE_GSTREAMER_0_8
	g_signal_connect_object (G_OBJECT (mp->priv->playbin),
				 "found_tag",
				 G_CALLBACK (found_tag_cb),
				 mp, 0);

	mp->priv->buffering_signal_id =
		g_signal_connect_object (G_OBJECT (mp->priv->playbin),
					 "buffering",
					 G_CALLBACK (buffering_cb),
					 mp, 0);

	mp->priv->error_signal_id =
		g_signal_connect_object (G_OBJECT (mp->priv->playbin),
					 "error",
					 G_CALLBACK (error_cb),
					 mp, 0);

	g_signal_connect_object (G_OBJECT (mp->priv->playbin), "eos",
				 G_CALLBACK (eos_cb), mp, 0);
#endif

#ifdef HAVE_GSTREAMER_0_8
	/* Output sink */
	sink = gst_gconf_get_default_audio_sink ();
	g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", sink, NULL);
#endif
#ifdef HAVE_GSTREAMER_0_10
	gst_bus_add_watch (gst_element_get_bus (GST_ELEMENT (mp->priv->playbin)),
			     (GstBusFunc) rb_player_gst_bus_cb, mp);

	/* let plugins add bits to playbin */
	g_object_notify (G_OBJECT (mp), "playbin");

	/* Use gconfaudiosink for audio if there's no audio sink yet */
	g_object_get (G_OBJECT (mp->priv->playbin), "audio-sink", &sink, NULL);
	if (sink == NULL) {
		sink = gst_element_factory_make ("gconfaudiosink", "audiosink");
		if (sink == NULL) {
			/* fall back to autoaudiosink */
			sink = gst_element_factory_make ("autoaudiosink", "audiosink");
		}

		if (sink != NULL) {
			/* set the profile property on the gconfaudiosink to "music and movies" */
			if (g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "profile"))
				g_object_set (G_OBJECT (sink), "profile", 1, NULL);

			g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", sink, NULL);
		}
	} else {
		g_object_unref (sink);
	}
#endif

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
			     err);
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
#ifdef HAVE_GSTREAMER_0_8
 		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
#elif HAVE_GSTREAMER_0_10
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
#endif
 			return FALSE;
		}
	} else {
		rb_debug ("PAUSING pipeline");
#ifdef HAVE_GSTREAMER_0_8
 		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_FAILURE) {
#elif HAVE_GSTREAMER_0_10
		if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
#endif
			return FALSE;
		}
	}
#ifdef HAVE_GSTREAMER_0_10
	/* FIXME: Set up a timeout to watch if the pipeline doesn't
         * go to PAUSED/PLAYING within some time (5 secs maybe?)
	 */
#endif
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

#ifdef HAVE_GSTREAMER_0_10
	g_object_get (object, "source", &source, NULL);
#elif HAVE_GSTREAMER_0_8
	gst_element_get (GST_ELEMENT (object), "source", &source, NULL);
#endif
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

static gboolean
rb_player_gst_open (RBPlayer *player,
		const char *uri,
		GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean cdda_seek = FALSE;

	if (mp->priv->playbin == NULL) {
		if (!rb_player_gst_construct (mp, error))
			return FALSE;
	} else {
		if (!rb_player_close (player, error))
			return FALSE;
	}

	g_assert (mp->priv->playbin != NULL);

	if (uri == NULL) {
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

#ifdef HAVE_GSTREAMER_0_10
			if (gst_element_seek (mp->priv->playbin, 1.0,
					      track_format, GST_SEEK_FLAG_FLUSH,
					      GST_SEEK_TYPE_SET, track,
					      GST_SEEK_TYPE_NONE, -1))
				cdda_seek = TRUE;
#elif HAVE_GSTREAMER_0_8
			{
				GstEvent *event;

				event = gst_event_new_seek (track_format | GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH,
						    (guint64) track);
				if (gst_element_send_event (mp->priv->playbin, event))
					cdda_seek = TRUE;
			}
#endif
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
	g_free (mp->priv->uri);
	mp->priv->uri = g_strdup (uri);
	mp->priv->emitted_error = FALSE;

	if (!cdda_seek) {
		g_object_set (G_OBJECT (mp->priv->playbin), "uri", uri, NULL);
	}

	if (!rb_player_gst_sync_pipeline (mp)) {
		end_gstreamer_operation (mp, TRUE, error);
		rb_player_gst_close (player, NULL);
		return FALSE;
	}

	if (mp->priv->tick_timeout_id == 0)
		mp->priv->tick_timeout_id = g_timeout_add (1000 / RB_PLAYER_GST_TICK_HZ, (GSourceFunc) tick_timeout, mp);

	end_gstreamer_operation (mp, FALSE, error);
	return TRUE;
}

static void
remove_idle_source (gpointer key, gpointer value, gpointer user_data)
{
	guint id = GPOINTER_TO_UINT (key);

	g_source_remove (id);
}

static gboolean
rb_player_gst_close (RBPlayer *player, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean ret;

	mp->priv->playing = FALSE;
	mp->priv->buffering = FALSE;

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (mp->priv->idle_eos_id != 0) {
		g_source_remove (mp->priv->idle_eos_id);
		mp->priv->idle_eos_id = 0;
	}
	if (mp->priv->idle_error_id != 0) {
		g_source_remove (mp->priv->idle_error_id);
		mp->priv->idle_error_id = 0;
	}
	if (mp->priv->idle_buffering_id != 0) {
		g_source_remove (mp->priv->idle_buffering_id);
		mp->priv->idle_buffering_id = 0;
	}
	g_hash_table_foreach (mp->priv->idle_info_ids, remove_idle_source, NULL);

	if (mp->priv->tick_timeout_id != 0)
		g_source_remove (mp->priv->tick_timeout_id);

	if (mp->priv->playbin == NULL)
		return TRUE;

	begin_gstreamer_operation (mp);
#ifdef HAVE_GSTREAMER_0_8
	ret = gst_element_set_state (mp->priv->playbin, GST_STATE_READY) == GST_STATE_SUCCESS;
#elif HAVE_GSTREAMER_0_10
	ret = gst_element_set_state (mp->priv->playbin, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS;
#endif
	end_gstreamer_operation (mp, !ret, error);
	return ret;
}

static gboolean
rb_player_gst_opened (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->uri != NULL;
}

static gboolean
rb_player_gst_play (RBPlayer *player, GError **error)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	gboolean ret;

	mp->priv->playing = TRUE;
	mp->priv->buffering = FALSE;

	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	begin_gstreamer_operation (mp);
	ret = rb_player_gst_sync_pipeline (mp);
	end_gstreamer_operation (mp, !ret, error);

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

	if (mp->priv->tick_timeout_id != 0)
		g_source_remove (mp->priv->tick_timeout_id);
}

static gboolean
rb_player_gst_playing (RBPlayer *player)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);

	return mp->priv->playing;
}

static void
rb_player_gst_set_replaygain (RBPlayer *player,
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

	if (mp->priv->playbin != NULL) {
		GParamSpec *volume_pspec;
		GValue val = {0,};

		volume_pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (mp->priv->playbin),
							     "volume");
		g_value_init (&val, G_TYPE_DOUBLE);

		g_value_set_double (&val, mp->priv->cur_volume * scale);
		if (g_param_value_validate (volume_pspec, &val))
			rb_debug ("replay gain too high, reducing value to %f", g_value_get_double (&val));

		g_object_set_property (G_OBJECT (mp->priv->playbin), "volume", &val);
		g_value_unset (&val);
	}
}

static void
rb_player_gst_set_volume (RBPlayer *player,
			  float volume)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (volume >= 0.0 && volume <= 1.0);

	if (mp->priv->playbin != NULL) {
		g_object_set (G_OBJECT (mp->priv->playbin),
			      "volume",
			      volume,
			      NULL);
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
#ifdef HAVE_GSTREAMER_0_8
	/* FIXME we're lying here,
	 * (0.8) trying a seek might disrupt playback */
	gboolean can_seek = TRUE;
#elif HAVE_GSTREAMER_0_10
	/* (0.10) Need to send a seekable query on playbin */
	gboolean can_seek = TRUE;
	GstQuery *query;
#endif

	if (mp->priv->playbin == NULL)
		return FALSE;

#ifdef HAVE_GSTREAMER_0_10
	query = gst_query_new_seeking (GST_FORMAT_TIME);
	if (gst_element_query (mp->priv->playbin, query)) {
		gst_query_parse_seeking (query, NULL, &can_seek, NULL, NULL);
	} else {
		gst_query_unref (query);

		query = gst_query_new_duration (GST_FORMAT_TIME);
		can_seek = gst_element_query (mp->priv->playbin, query);
	}
	gst_query_unref (query);
#endif

	return can_seek;
}

static void
rb_player_gst_set_time (RBPlayer *player, long time)
{
	RBPlayerGst *mp = RB_PLAYER_GST (player);
	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->playbin != NULL);

#ifdef HAVE_GSTREAMER_0_8
	gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED);
#elif HAVE_GSTREAMER_0_10
	if (gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC) {
		/* FIXME: Use a timeout on get_state. Post a GError somewhere on failed? */
		if (gst_element_get_state (mp->priv->playbin, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
			g_warning ("Failed to pause pipeline before seek");
		}
	}
#endif

#ifdef HAVE_GSTREAMER_0_8
	gst_element_seek (mp->priv->playbin,
			  GST_FORMAT_TIME
			  | GST_SEEK_METHOD_SET
			  | GST_SEEK_FLAG_FLUSH,
			  time * GST_SECOND);
#elif HAVE_GSTREAMER_0_10
	gst_element_seek (mp->priv->playbin, 1.0,
			  GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
			  GST_SEEK_TYPE_SET, time * GST_SECOND,
			  GST_SEEK_TYPE_NONE, -1);
#endif

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

#ifdef HAVE_GSTREAMER_0_8
		gst_element_query (mp->priv->playbin, GST_QUERY_POSITION, &fmt, &position);
#elif HAVE_GSTREAMER_0_10
		gst_element_query_position (mp->priv->playbin, &fmt, &position);
#endif
		if (position != -1)
			position /= GST_SECOND;

		return (long) position;
	} else
		return -1;
}
