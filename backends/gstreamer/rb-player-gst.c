/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <gst/gconf/gconf.h>
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

#ifdef WITH_DAAP_SUPPORT
#include "rb-daap-src.h"
#endif

G_DEFINE_TYPE(RBPlayer, rb_player, G_TYPE_OBJECT)

static void rb_player_finalize (GObject *object);

struct RBPlayerPrivate
{
	char *uri;

	GstElement *playbin;

	gboolean can_signal_direct_error;
	GError *error;

	gboolean playing;

	guint idle_error_id;
	guint idle_eos_id;
	guint idle_buffering_id;
	GHashTable *idle_info_ids;

	guint error_signal_id;
	guint buffering_signal_id;

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
	LAST_SIGNAL
} RBPlayerSignalType;

typedef struct
{
	int type;
	RBPlayer *object;
	RBMetaDataField info_field;
	GError *error;
	GValue *info;
	guint id;
} RBPlayerSignal;

static guint rb_player_signals[LAST_SIGNAL] = { 0 };

static gboolean rb_player_sync_pipeline (RBPlayer *mp);
static void rb_player_gst_free_playbin (RBPlayer *player);

static void
rb_player_class_init (RBPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

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
	rb_player_signals[BUFFERING] =
		g_signal_new ("buffering",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerClass, buffering),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_UINT);
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
	mp->priv->tick_timeout_id = g_timeout_add (ms_period, (GSourceFunc) tick_timeout, mp);
	mp->priv->idle_info_ids = g_hash_table_new (NULL, NULL);
	
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *mp;

	mp = RB_PLAYER (object);

	g_source_remove (mp->priv->tick_timeout_id);
	g_hash_table_destroy (mp->priv->idle_info_ids);

	if (mp->priv->playbin) {
		g_signal_handler_disconnect (G_OBJECT (mp->priv->playbin),
					     mp->priv->error_signal_id);
		g_signal_handler_disconnect (G_OBJECT (mp->priv->playbin),
					     mp->priv->buffering_signal_id);
		
		gst_element_set_state (mp->priv->playbin,
				       GST_STATE_NULL);
		
		rb_player_gst_free_playbin (mp);
	}

	g_free (mp->priv);

	G_OBJECT_CLASS (rb_player_parent_class)->finalize (object);
}

static void
rb_player_gst_free_playbin (RBPlayer *player)
{
	if (player->priv->playbin == NULL)
		return;
	
	gst_object_unref (GST_OBJECT (player->priv->playbin));
	player->priv->playbin = NULL;
}

static void
destroy_idle_signal (gpointer signal_pointer)
{
	RBPlayerSignal *signal = signal_pointer;

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
emit_signal_idle (RBPlayerSignal *signal)
{
	switch (signal->type) {
	case ERROR:
		g_signal_emit (G_OBJECT (signal->object),
			       rb_player_signals[ERROR], 0,
			       signal->error);

		/* close if not already closing */
		if (signal->object->priv->uri != NULL)
			rb_player_close (signal->object, NULL);

		break;

	case EOS:
		g_signal_emit (G_OBJECT (signal->object), rb_player_signals[EOS], 0);
		signal->object->priv->idle_eos_id = 0;
		break;

	case INFO:
		g_signal_emit (G_OBJECT (signal->object),
			       rb_player_signals[INFO], 0,
			       signal->info_field, signal->info);
		break;

	case BUFFERING:
		g_signal_emit (G_OBJECT (signal->object),
			       rb_player_signals[BUFFERING], 0,
			       g_value_get_uint (signal->info));
		signal->object->priv->idle_buffering_id = 0;
		break;
	}

	return FALSE;
}

static void
eos_cb (GstElement *element,
	RBPlayer *mp)
{
	RBPlayerSignal *signal;
	signal = g_new0 (RBPlayerSignal, 1);
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
	  RBPlayer *mp)
{
	int code;
	RBPlayerSignal *signal;

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

	/* If we're in a synchronous op, we can signal the error directly */
	if (mp->priv->can_signal_direct_error) {
		if (mp->priv->error) {
			g_warning ("Overwriting previous error \"%s\" with new error \"%s\"",
				   mp->priv->error->message,
				   error->message);
			g_error_free (mp->priv->error);
		}
		mp->priv->error = g_error_copy (error);
		return;
	}

	signal = g_new0 (RBPlayerSignal, 1);
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
}

static void
process_tag (const GstTagList *list, const gchar *tag, RBPlayer *player)
{
	int count;
	RBMetaDataField field;
	RBPlayerSignal *signal;
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
	else
		return;

	/* of those, all are strings */
	newval = g_new0 (GValue, 1);
	g_value_init (newval, G_TYPE_STRING);
	val = gst_tag_list_get_value_index (list, tag, 0);
	if (!g_value_transform (val, newval)) {
		rb_debug ("Could not transform tag value type %s into string",
			  g_type_name (G_VALUE_TYPE (val)));
		return;
	}

	signal = g_new0 (RBPlayerSignal, 1);
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

static void
found_tag_cb (GObject *pipeline, GstElement *source, GstTagList *tags, RBPlayer *player)
{
	gst_tag_list_foreach (tags, (GstTagForeachFunc) process_tag, player);
}

static void
buffering_cb (GstElement *element, gint progress, RBPlayer *mp)
{
	RBPlayerSignal *signal;

	signal = g_new0 (RBPlayerSignal, 1);
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

static gboolean
rb_player_construct (RBPlayer *mp, GError **error)
{
	char *element_name = NULL;
	GstElement *sink, *fakesink;

	/* playbin */
	rb_debug ("constructing element \"playbin\"");
	mp->priv->playbin = gst_element_factory_make ("playbin", "playbin");
	if (mp->priv->playbin == NULL) {
		goto missing_element;
	}

	fakesink = gst_element_factory_make ("fakesink", "fakesink");
	g_object_set (G_OBJECT (mp->priv->playbin), "video-sink", fakesink, NULL);

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

	/* Output sink */
	sink = gst_gconf_get_default_audio_sink ();
	if (sink == NULL) {
		g_set_error (error,
			     RB_PLAYER_ERROR,
			     RB_PLAYER_ERROR_NO_AUDIO,
			     _("Could not create audio output element; check your settings"));
		rb_player_gst_free_playbin (mp);
		return FALSE;
	}

	g_object_set (G_OBJECT (mp->priv->playbin), "audio-sink", sink, NULL);
	g_signal_connect_object (G_OBJECT (mp->priv->playbin), "eos",
				 G_CALLBACK (eos_cb), mp, 0);

	if (mp->priv->cur_volume > 1.0)
		mp->priv->cur_volume = 1.0;
	if (mp->priv->cur_volume < 0.0)
		mp->priv->cur_volume = 0;
	rb_player_set_volume (mp, mp->priv->cur_volume);

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
		rb_player_gst_free_playbin (mp);
		return FALSE;
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

	mp = RB_PLAYER (g_object_new (RB_TYPE_PLAYER, NULL, NULL));

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
rb_player_sync_pipeline (RBPlayer *mp)
{
	rb_debug ("syncing pipeline");
	if (mp->priv->playing) {
 		rb_debug ("PLAYING pipeline");
 		if (gst_element_set_state (mp->priv->playbin,
 					   GST_STATE_PLAYING) == GST_STATE_FAILURE) {
 			return FALSE;
		}
	} else {
		rb_debug ("PAUSING pipeline");
		if (gst_element_set_state (mp->priv->playbin,
					   GST_STATE_PAUSED) == GST_STATE_FAILURE) {
			return FALSE;
		}
	}
	return TRUE;
}

/* Start a sequence of synchronous GStreamer operations in which we
 * can receive an error signal.
 */
static void
begin_gstreamer_operation (RBPlayer *mp)
{
	g_assert (mp->priv->error == NULL);
	mp->priv->can_signal_direct_error = TRUE;
}

/* End a sequence of synchronous operations and propagate any
 * error from the sequence into error.
 */
static void
end_gstreamer_operation (RBPlayer *mp, gboolean op_failed, GError **error)
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
cdda_got_source_cb (GObject *object, GParamSpec *pspec, char *uri)
{
	GstElement *source;

	gst_element_get (GST_ELEMENT (object), "source", &source, NULL);
	rb_debug ("got source %p", source);
	if (source) {
		g_signal_handlers_disconnect_by_func (object, cdda_got_source_cb, uri);

		g_object_set (G_OBJECT (source), "device", uri, NULL);
		g_free (uri);
	}
}

gboolean
rb_player_open (RBPlayer *mp,
		const char *uri,
		GError **error)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), TRUE);

	if (mp->priv->playbin == NULL) {
		if (!rb_player_construct (mp, error))
			return FALSE;
	}

	g_assert (mp->priv->playbin != NULL);

	g_free (mp->priv->uri);
	mp->priv->uri = NULL;

	if (uri == NULL) {
		mp->priv->playing = FALSE;
		return TRUE;
	}


	begin_gstreamer_operation (mp);
	mp->priv->uri = g_strdup (uri);

	if (g_str_has_prefix (uri, "cdda://")) {
		gchar *copy, *temp, *split;
		int l = strlen ("cdda://");

		copy = g_strdup (uri);
		split = g_utf8_strrchr (copy + l, -1, ':');

		if (split == NULL) {
			/* invalid URI, it doesn't contain a ':' */
			end_gstreamer_operation (mp, TRUE, error);
			return FALSE;
		}

		temp = g_strdup_printf ("cdda://%s", split + 1);
		g_object_set (G_OBJECT (mp->priv->playbin), "uri", temp, NULL);
		g_free (temp);

		*split = 0;
		temp = g_strdup (copy + l);
		g_signal_connect (G_OBJECT (mp->priv->playbin),
				  "notify::source",
				  G_CALLBACK (cdda_got_source_cb),
				  temp);
		g_free (copy);
	} else {
		g_object_set (G_OBJECT (mp->priv->playbin), "uri", uri, NULL);
	}

	if (!rb_player_sync_pipeline (mp)) {
		end_gstreamer_operation (mp, TRUE, error);
		rb_player_close (mp, NULL);
		return FALSE;
	}
	end_gstreamer_operation (mp, FALSE, error);
	return TRUE;
}

static void
remove_idle_source (gpointer key, gpointer value, gpointer user_data)
{
	guint id = GPOINTER_TO_UINT (key);

	g_source_remove (id);
}

gboolean
rb_player_close (RBPlayer *mp, GError **error)
{
	gboolean ret;
	g_return_val_if_fail (RB_IS_PLAYER (mp), TRUE);

	mp->priv->playing = FALSE;

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

	if (mp->priv->playbin == NULL)
		return TRUE;

	begin_gstreamer_operation (mp);
	ret = gst_element_set_state (mp->priv->playbin, GST_STATE_READY) == GST_STATE_SUCCESS;
	end_gstreamer_operation (mp, !ret, error);
	return ret;
}

gboolean
rb_player_opened (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), FALSE);

	return mp->priv->uri != NULL;
}

gboolean
rb_player_play (RBPlayer *mp, GError **error)
{
	gboolean ret;
	g_return_val_if_fail (RB_IS_PLAYER (mp), TRUE);

	mp->priv->playing = TRUE;

	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	begin_gstreamer_operation (mp);
	ret = rb_player_sync_pipeline (mp);
	end_gstreamer_operation (mp, !ret, error);
	return ret;
}

void
rb_player_pause (RBPlayer *mp)
{
	g_return_if_fail (RB_IS_PLAYER (mp));

	if (!mp->priv->playing)
		return;

	mp->priv->playing = FALSE;

	g_return_if_fail (mp->priv->playbin != NULL);

	rb_player_sync_pipeline (mp);
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

	if (mp->priv->playbin != NULL) {
		g_object_set (G_OBJECT (mp->priv->playbin),
			      "volume",
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

	if (mp->priv->playbin != NULL) {
		g_object_set (G_OBJECT (mp->priv->playbin),
			      "volume",
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
	g_return_val_if_fail (mp->priv->playbin != NULL, FALSE);

	/* FIXME we're lying here, no idea how to fix this though, without trying
	 * a seek which might disrupt playback */
	return TRUE;
}

void
rb_player_set_time (RBPlayer *mp, long time)
{
	g_return_if_fail (RB_IS_PLAYER (mp));
	g_return_if_fail (time >= 0);

	g_return_if_fail (mp->priv->playbin != NULL);

	gst_element_set_state (mp->priv->playbin, GST_STATE_PAUSED);

#ifdef WITH_DAAP_SUPPORT
	/* FIXME?
	 * This is sorta hack/sorta best way to do it.
	 * If we set up the daapsrc to do regular GStreamer seeking,
	 * GStreamer goes ape-shit and tries to seek all over the 
	 * place (typefinding), which can cause iTunes to return errors
	 * (probably cause we're requesting the same file too often)
 	 *
	 * So, we do it this way.  Doesn't that suck?
	 */
	if (mp->priv->uri && g_strncasecmp (mp->priv->uri, "daap://", 7) == 0) {
		rb_daap_src_set_time (time);
	} else {
#endif
	gst_element_seek (mp->priv->playbin, 
			  GST_FORMAT_TIME 
			  | GST_SEEK_METHOD_SET 
			  | GST_SEEK_FLAG_FLUSH, 
			  time * GST_SECOND);
#ifdef WITH_DAAP_SUPPORT
	}
#endif
	if (mp->priv->playing)
		gst_element_set_state (mp->priv->playbin, GST_STATE_PLAYING);
}

long
rb_player_get_time (RBPlayer *mp)
{
	g_return_val_if_fail (RB_IS_PLAYER (mp), -1);

	if (mp->priv->playbin != NULL) {
		gint64 gst_position;
		glong ret = 0;

		GstFormat fmt = GST_FORMAT_TIME;
		gst_element_query (mp->priv->playbin, GST_QUERY_POSITION, &fmt, &gst_position);
		ret = (glong) (gst_position / (1000*1000*1000));

#ifdef WITH_DAAP_SUPPORT
		if (mp->priv->uri && g_strncasecmp (mp->priv->uri, "daap://", 7) == 0) {
			ret += rb_daap_src_get_time ();
		}
#endif

		return ret;
	} else
		return -1;
}
