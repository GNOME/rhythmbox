/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/encoding-profile.h>

#include "rhythmdb.h"
#include "rb-encoder.h"
#include "rb-encoder-gst.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-gst-media-types.h"

static void rb_encoder_gst_init (RBEncoderGst *encoder);
static void rb_encoder_init (RBEncoderIface *iface);

struct _RBEncoderGstPrivate {
	GstEncodingProfile *profile;

	GstElement *encodebin;
	GstElement *pipeline;
	GstElement *output;
	GstElement *sink;
	guint bus_watch_id;

	gboolean transcoding;
	gint decoded_pads;

	gboolean completion_emitted;
	gboolean cancelled;

	GstFormat position_format;
	gint64 total_length;
	guint progress_id;
	char *dest_uri;
	char *dest_media_type;
	gboolean overwrite;
	gint64 dest_size;

	GOutputStream *outstream;
	GCancellable *open_cancel;
	GTask *open_task;

	int tmpfile_fd;

	GError *error;
};

G_DEFINE_TYPE_WITH_CODE(RBEncoderGst, rb_encoder_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_ENCODER,
					      rb_encoder_init))

static void
set_error (RBEncoderGst *encoder, GError *error)
{
	if (encoder->priv->error != NULL) {
		g_warning ("got encoding error %s, but already have one: %s",
			   error->message,
			   encoder->priv->error->message);
		return;
	}

	/* translate some GStreamer errors into generic ones */
	if (g_error_matches (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NO_SPACE_LEFT)) {
		encoder->priv->error = g_error_new (RB_ENCODER_ERROR, RB_ENCODER_ERROR_OUT_OF_SPACE, "%s", error->message);
	} else if (g_error_matches (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_WRITE)) {
		encoder->priv->error = g_error_new (RB_ENCODER_ERROR, RB_ENCODER_ERROR_DEST_READ_ONLY, "%s", error->message);
	} else {
		encoder->priv->error = g_error_copy (error);
	}
}

static void
rb_encoder_gst_emit_completed (RBEncoderGst *encoder)
{
	GError *error = NULL;

	g_return_if_fail (encoder->priv->completion_emitted == FALSE);

	if (encoder->priv->progress_id != 0) {
		g_source_remove (encoder->priv->progress_id);
		encoder->priv->progress_id = 0;
	}

	/* emit an error if no audio pad has been found
	 * and it wasn't due to an error
	 */
	if (encoder->priv->error == NULL &&
	    encoder->priv->transcoding &&
	    encoder->priv->decoded_pads == 0) {
		rb_debug ("received EOS and no decoded pad");
		g_set_error (&error,
				RB_ENCODER_ERROR,
				RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
				"no decodable audio pad found");

		set_error (encoder, error);
		g_error_free (error);
		error = NULL;
	}

	encoder->priv->completion_emitted = TRUE;
	_rb_encoder_emit_completed (RB_ENCODER (encoder),
				    encoder->priv->dest_uri,
				    encoder->priv->dest_size,
				    encoder->priv->dest_media_type,
				    encoder->priv->error);
}

static void
output_close_cb (GOutputStream *stream, GAsyncResult *result, RBEncoderGst *encoder)
{
	GError *error = NULL;
	rb_debug ("finished closing output stream");
	g_output_stream_close_finish (encoder->priv->outstream, result, &error);
	if (error != NULL) {
		rb_debug ("error closing output stream: %s", error->message);
		g_error_free (error);
	}

	rb_encoder_gst_emit_completed (encoder);

	g_object_unref (encoder->priv->outstream);
	encoder->priv->outstream = NULL;

	g_object_unref (encoder);
}

static gboolean
bus_watch_cb (GstBus *bus, GstMessage *message, gpointer data)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (data);
	char *string;
	GError *error = NULL;

	/* ref ourselves, in case one of the signal handler unrefs us */
	g_object_ref (encoder);

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR:
		gst_message_parse_error (message, &error, &string);
		set_error (encoder, error);
		rb_debug ("received error %s", string);
		g_error_free (error);
		g_free (string);

		rb_encoder_cancel (RB_ENCODER (encoder));
		break;

	case GST_MESSAGE_WARNING:
		gst_message_parse_warning (message, &error, &string);
		rb_debug ("received warning %s", string);
		g_error_free (error);
		g_free (string);
		break;

	case GST_MESSAGE_EOS:
		gst_element_query_position (encoder->priv->pipeline, GST_FORMAT_BYTES, &encoder->priv->dest_size);

		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);
		if (encoder->priv->outstream != NULL) {
			rb_debug ("received EOS, closing output stream");
			g_output_stream_close_async (encoder->priv->outstream,
						     G_PRIORITY_DEFAULT,
						     NULL,
						     (GAsyncReadyCallback) output_close_cb,
						     g_object_ref (encoder));
		} else if (encoder->priv->tmpfile_fd) {
			rb_debug ("received EOS, closing temp file");
			close (encoder->priv->tmpfile_fd);
			encoder->priv->tmpfile_fd = 0;

			rb_encoder_gst_emit_completed (encoder);
		} else {
			rb_debug ("received EOS, but there's no output stream");
			rb_encoder_gst_emit_completed (encoder);

			g_object_unref (encoder->priv->pipeline);
			encoder->priv->pipeline = NULL;
		}

		break;

	default:
		rb_debug ("message of type %s", gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
		break;
	}

	g_object_unref (encoder);
	return TRUE;
}

static gboolean
progress_timeout_cb (RBEncoderGst *encoder)
{
	gint64 position;
	static GstFormat format;
	GstState state;

	if (encoder->priv->pipeline == NULL)
		return FALSE;

	format = encoder->priv->position_format;

	gst_element_get_state (encoder->priv->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
	if (state != GST_STATE_PLAYING) {
		encoder->priv->progress_id = 0;
		return FALSE;
	}

	if (!gst_element_query_position (encoder->priv->pipeline, format, &position)) {
		g_warning ("Could not get current track position");
		return TRUE;
	}

	if (format == GST_FORMAT_TIME) {
		gint secs;

		secs = position / GST_SECOND;
		rb_debug ("encoding progress at %d out of %" G_GINT64_FORMAT,
			  secs,
			  encoder->priv->total_length);
		_rb_encoder_emit_progress (RB_ENCODER (encoder),
					   ((double)secs) / encoder->priv->total_length);
	} else {
		rb_debug ("encoding progress at %" G_GINT64_FORMAT " out of %" G_GINT64_FORMAT,
			  position,
			  encoder->priv->total_length);
		_rb_encoder_emit_progress (RB_ENCODER (encoder),
					   ((double) position) / encoder->priv->total_length);
	}

	return TRUE;
}

static void
add_string_tag (GstTagList *tags, GstTagMergeMode mode, const char *tag, RhythmDBEntry *entry, RhythmDBPropType property)
{
	const char *v;
	v = rhythmdb_entry_get_string (entry, property);
	if (v != NULL && v[0] != '\0') {
		gst_tag_list_add (tags, mode, tag, v, NULL);
	}
}

static void
add_tags_from_entry (RBEncoderGst *encoder, RhythmDBEntry *entry)
{
	GstTagList *tags;
	GValue obj = {0,};
	GstTagSetter *tag_setter;
	GstIterator *iter;
	gulong day;
	gdouble bpm;
	gboolean done;

	tags = gst_tag_list_new (GST_TAG_TRACK_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
				 GST_TAG_ALBUM_VOLUME_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER),
				 GST_TAG_ENCODER, "Rhythmbox",
				 GST_TAG_ENCODER_VERSION, VERSION,
				 NULL);

	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, entry, RHYTHMDB_PROP_TITLE);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_ARTIST, entry, RHYTHMDB_PROP_ARTIST);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_ALBUM, entry, RHYTHMDB_PROP_ALBUM);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_GENRE, entry, RHYTHMDB_PROP_GENRE);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_COMMENT, entry, RHYTHMDB_PROP_COMMENT);

	day = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);

	if (day > 0) {
		GDate *date;
		GstDateTime *datetime;

		date = g_date_new_julian (day);
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_DATE, date,
				  NULL);

		datetime = gst_date_time_new_ymd (g_date_get_year (date),
						  g_date_get_month (date),
						  g_date_get_day (date));
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_DATE_TIME, datetime,
				  NULL);

		gst_date_time_unref (datetime);
		g_date_free (date);
	}
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_MUSICBRAINZ_TRACKID, entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_MUSICBRAINZ_ARTISTID, entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_MUSICBRAINZ_ALBUMID, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_MUSICBRAINZ_ALBUMARTISTID, entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_ARTIST_SORTNAME, entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
	add_string_tag (tags, GST_TAG_MERGE_APPEND, GST_TAG_ALBUM_SORTNAME, entry, RHYTHMDB_PROP_ALBUM_SORTNAME);

	/* is zero a valid BPM? */
	bpm = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_BPM);
	if (bpm > 0.001) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_BEATS_PER_MINUTE, bpm, NULL);
	}

	/* XXX encodebin isn't a tag setter yet */
	/*
	gst_tag_setter_merge_tags (GST_TAG_SETTER (encoder->priv->encodebin), tags, GST_TAG_MERGE_REPLACE_ALL);
	*/
	iter = gst_bin_iterate_all_by_interface (GST_BIN (encoder->priv->encodebin), GST_TYPE_TAG_SETTER);
	done = FALSE;
	while (!done) {
		g_value_init (&obj, GST_TYPE_ELEMENT);
		switch (gst_iterator_next (iter, &obj)) {
		case GST_ITERATOR_OK:
			tag_setter = g_value_get_object (&obj);
			gst_tag_setter_merge_tags (tag_setter, tags, GST_TAG_MERGE_REPLACE_ALL);
			g_value_unset (&obj);
			break;
		case GST_ITERATOR_RESYNC:
			gst_iterator_resync (iter);
			break;
		case GST_ITERATOR_ERROR:
			done = TRUE;
			break;
		case GST_ITERATOR_DONE:
			done = TRUE;
			break;
		}
	}

	gst_tag_list_unref (tags);
}

static void
pad_added_cb (GstElement *decodebin, GstPad *new_pad, RBEncoderGst *encoder)
{
	GstPad *enc_sinkpad;
	GstCaps *caps;
	gchar *caps_string;

	/* transcode only the first audio track. multitrack audio files are not
	 * so common anyway */
	if (encoder->priv->decoded_pads > 0) {
		rb_debug ("already have an audio track to encode");
		return;
	}

	caps = gst_pad_query_caps (new_pad, NULL);
	caps_string = gst_caps_to_string (caps);
	gst_caps_unref (caps);

	/* only process audio data */
	if (strncmp (caps_string, "audio/", 6) == 0) {
		rb_debug ("linking first audio pad");
		encoder->priv->decoded_pads++;
		enc_sinkpad = gst_element_get_static_pad (encoder->priv->encodebin, "audio_0");
		if (gst_pad_link (new_pad, enc_sinkpad) != GST_PAD_LINK_OK)
			rb_debug ("error linking pads");
	} else {
		rb_debug ("ignoring non-audio pad");
	}

	g_free (caps_string);
}

static GstElement *
add_decoding_pipeline (RBEncoderGst *encoder,
		       GError **error)
{
	GstElement *decodebin;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	encoder->priv->transcoding = TRUE;
	decodebin = gst_element_factory_make ("decodebin", NULL);
	if (decodebin == NULL) {
		rb_debug ("couldn't create decodebin");
		g_set_error (error,
				RB_ENCODER_ERROR,
				RB_ENCODER_ERROR_INTERNAL,
				"Could not create decodebin");
		return NULL;
	}

	gst_bin_add (GST_BIN (encoder->priv->pipeline), decodebin);

	g_signal_connect_object (decodebin,
			"pad-added",
			G_CALLBACK (pad_added_cb),
			encoder, 0);

	return decodebin;
}

static GstElement *
create_pipeline_and_source (RBEncoderGst *encoder,
			    RhythmDBEntry *entry,
			    GError **error)
{
	char *uri;
	GstElement *src;

	uri = rhythmdb_entry_get_playback_uri (entry);
	if (uri == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Didn't get a playback URI for entry %s",
			     rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return NULL;
	}

	src = gst_element_make_from_uri (GST_URI_SRC, uri, "source", NULL);
	if (src == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Could not create source element for '%s'", uri);
		g_free (uri);
		return NULL;
	}

	encoder->priv->pipeline = gst_pipeline_new ("pipeline");
	gst_bin_add (GST_BIN (encoder->priv->pipeline), src);

	/* provide a hook for setting source properties */
	_rb_encoder_emit_prepare_source (RB_ENCODER (encoder), uri, G_OBJECT (src));

	g_free (uri);
	return src;
}

static GstElement *
transcode_track (RBEncoderGst *encoder,
	 	 RhythmDBEntry *entry,
		 GError **error)
{
	/* src ! decodebin ! encodebin ! sink */
	GstElement *src;
	GstElement *decoder;

	g_assert (encoder->priv->pipeline == NULL);
	g_assert (encoder->priv->profile != NULL);

	rb_debug ("transcoding to profile %s", gst_encoding_profile_get_name (encoder->priv->profile));

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL) {
		return NULL;
	}

	decoder = add_decoding_pipeline (encoder, error);
	if (decoder == NULL) {
		return NULL;
	}

	if (gst_element_link (src, decoder) == FALSE) {
		rb_debug ("unable to link source element to decodebin");
		g_set_error (error,
			     RB_ENCODER_ERROR,
			     RB_ENCODER_ERROR_INTERNAL,
			     "Unable to link source element to decodebin");
		return NULL;
	}

	encoder->priv->encodebin = gst_element_factory_make ("encodebin", NULL);
	if (encoder->priv->encodebin == NULL) {
		rb_debug ("unable to create encodebin");
		g_set_error (error,
				RB_ENCODER_ERROR,
				RB_ENCODER_ERROR_INTERNAL,
				"Could not create encodebin");
		return NULL;
	}
	g_object_set (encoder->priv->encodebin,
		      "profile", encoder->priv->profile,
		      "queue-bytes-max", 0,
		      "queue-buffers-max", 0,
		      "queue-time-max", 30 * GST_SECOND,
		      NULL);
	gst_bin_add (GST_BIN (encoder->priv->pipeline), encoder->priv->encodebin);

	return encoder->priv->encodebin;
}

static void
impl_cancel (RBEncoder *bencoder)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (bencoder);

	if (encoder->priv->open_cancel != NULL) {
		g_cancellable_cancel (encoder->priv->open_cancel);
	}

	if (encoder->priv->pipeline != NULL) {
		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);
		g_object_unref (encoder->priv->pipeline);
		encoder->priv->pipeline = NULL;
	}

	if (encoder->priv->outstream != NULL) {
		GError *error = NULL;
		GFile *f;
		g_output_stream_close (encoder->priv->outstream, NULL, &error);
		if (error != NULL) {
			rb_debug ("error closing output stream: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (encoder->priv->outstream);
		encoder->priv->outstream = NULL;

		/* try to delete the output file, since it's incomplete */
		error = NULL;
		f = g_file_new_for_uri (encoder->priv->dest_uri);
		if (g_file_delete (f, NULL, &error) == FALSE) {
			rb_debug ("error deleting incomplete output file: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (f);
	}

	if (encoder->priv->error == NULL) {
		/* should never be displayed to the user anyway */
		encoder->priv->error = g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED, " ");
	}

	encoder->priv->cancelled = TRUE;
	rb_encoder_gst_emit_completed (encoder);
}

static gboolean
cancel_idle (RBEncoder *encoder)
{
	impl_cancel (encoder);
	g_object_unref (encoder);
	return FALSE;
}

static void
sink_open_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (source_object);
	GstStateChangeReturn state_change;
	GstBus *bus;
	GError *error = NULL;

	if (g_task_propagate_boolean (G_TASK (result), &error) == FALSE) {
		/* this would have already been done as part of cancel action */
		if (encoder->priv->cancelled == FALSE) {
			set_error (encoder, error);
			rb_encoder_gst_emit_completed (encoder);
		}
		g_error_free (error);
	} else {
		if (encoder->priv->outstream != NULL) {
			g_object_set (encoder->priv->sink, "stream", encoder->priv->outstream, NULL);
		}

		_rb_encoder_emit_prepare_sink (RB_ENCODER (encoder), encoder->priv->dest_uri, G_OBJECT (encoder->priv->sink));

		gst_bin_add (GST_BIN (encoder->priv->pipeline), encoder->priv->sink);
		gst_element_link (encoder->priv->output, encoder->priv->sink);

		bus = gst_pipeline_get_bus (GST_PIPELINE (encoder->priv->pipeline));
		encoder->priv->bus_watch_id = gst_bus_add_watch (bus, bus_watch_cb, encoder);
		g_object_unref (bus);

		state_change = gst_element_set_state (encoder->priv->pipeline, GST_STATE_PLAYING);
		if (state_change != GST_STATE_CHANGE_FAILURE) {
			if (encoder->priv->total_length > 0) {
				_rb_encoder_emit_progress (RB_ENCODER (encoder), 0.0);
				encoder->priv->progress_id = g_timeout_add (250, (GSourceFunc)progress_timeout_cb, encoder);
			} else {
				_rb_encoder_emit_progress (RB_ENCODER (encoder), -1);
			}
		}
	}

	g_object_unref (encoder);
}

static GOutputStream *
stream_open (RBEncoderGst *encoder, GFile *file, GCancellable *cancellable, GError **error)
{
	GFileOutputStream *stream;

	if (encoder->priv->overwrite) {
		stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, cancellable, error);
	} else {
		stream = g_file_create (file, G_FILE_CREATE_NONE, cancellable, error);
	}

	if (*error != NULL && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
		char *msg = g_strdup ((*error)->message);

		g_clear_error (error);
		g_set_error_literal (error,
				     RB_ENCODER_ERROR,
				     RB_ENCODER_ERROR_DEST_EXISTS,
				     msg);
		g_free (msg);
	}

	return G_OUTPUT_STREAM (stream);
}

static void
sink_open (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (source_object);
	GError *error = NULL;

	if (g_str_equal (encoder->priv->dest_uri, RB_ENCODER_DEST_TEMPFILE)) {
		GFile *tmpfile;
		char *tmpfile_name;

		encoder->priv->tmpfile_fd = g_file_open_tmp ("rb-encoder-XXXXXX",
							     &tmpfile_name,
							     &error);
		if (error != NULL) {
			g_set_error (&error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_FILE_ACCESS,
				     _("Could not create a temporary file to write to: %s"),
				    error->message);
			g_task_return_error (task, error);
			return;
		}

		rb_debug ("opened temporary file %s", tmpfile_name);
		encoder->priv->sink = gst_element_factory_make ("fdsink", NULL);
		g_object_set (encoder->priv->sink, "fd", encoder->priv->tmpfile_fd, NULL);

		tmpfile = g_file_new_for_commandline_arg (tmpfile_name);
		g_free (encoder->priv->dest_uri);
		encoder->priv->dest_uri = g_file_get_uri (tmpfile);
		g_object_unref (tmpfile);
		g_free (tmpfile_name);

		g_task_return_boolean (task, TRUE);
		return;
	}

	encoder->priv->sink = gst_element_factory_make ("giostreamsink", NULL);
	if (encoder->priv->sink != NULL) {
		GFile *file;

		file = g_file_new_for_uri (encoder->priv->dest_uri);

		encoder->priv->outstream = stream_open (encoder, file, cancellable, &error);
		if (encoder->priv->outstream != NULL) {
			rb_debug ("opened output stream for %s", encoder->priv->dest_uri);
		} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
			rb_debug ("using default sink for %s as gio can't do it", encoder->priv->dest_uri);
			g_clear_error (&error);
			g_clear_object (&encoder->priv->sink);
		} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME)) {
			char *dest;

			g_clear_error (&error);

			dest = rb_sanitize_uri_for_filesystem (encoder->priv->dest_uri, "msdos");
			g_free (encoder->priv->dest_uri);
			encoder->priv->dest_uri = dest;
			rb_debug ("sanitized destination uri to %s", dest);

			file = g_file_new_for_uri (encoder->priv->dest_uri);
			encoder->priv->outstream = stream_open (encoder, file, cancellable, &error);
		}
	}

	if (encoder->priv->sink == NULL) {
		rb_debug ("unable to create giostreamsink, using default sink for %s", encoder->priv->dest_uri);
		encoder->priv->sink = gst_element_make_from_uri (GST_URI_SINK, encoder->priv->dest_uri, "sink", NULL);
		if (encoder->priv->sink == NULL) {
			g_set_error (&error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_FILE_ACCESS,
				     _("Could not create a GStreamer sink element to write to %s"),
				     encoder->priv->dest_uri);
			g_task_return_error (task, error);
		}
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_boolean (task, TRUE);
	}
}

static void
impl_encode (RBEncoder *bencoder,
	     RhythmDBEntry *entry,
	     const char *dest,
	     gboolean overwrite,
	     GstEncodingProfile *profile)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (bencoder);
	GError *error = NULL;
	GTask *task;

	g_return_if_fail (encoder->priv->pipeline == NULL);

	g_clear_object (&encoder->priv->profile);
	g_free (encoder->priv->dest_media_type);
	g_free (encoder->priv->dest_uri);
	encoder->priv->dest_uri = g_strdup (dest);
	encoder->priv->overwrite = overwrite;
	encoder->priv->dest_size = 0;

	/* keep ourselves alive in case we get cancelled by a signal handler */
	g_object_ref (encoder);

	/* if destination and source media types are the same, copy it */
	if (profile == NULL) {
		encoder->priv->total_length = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
		encoder->priv->position_format = GST_FORMAT_BYTES;
		encoder->priv->dest_media_type = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
		encoder->priv->output = create_pipeline_and_source (encoder, entry, &error);
	} else {
		gst_encoding_profile_ref (profile);
		encoder->priv->profile = profile;
		encoder->priv->total_length = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
		encoder->priv->position_format = GST_FORMAT_TIME;
		encoder->priv->dest_media_type = rb_gst_encoding_profile_get_media_type (profile);
		encoder->priv->output = transcode_track (encoder, entry, &error);

		add_tags_from_entry (encoder, entry);
	}

	if (error != NULL) {
		if (encoder->priv->cancelled == FALSE) {
			set_error (encoder, error);
			g_idle_add ((GSourceFunc) cancel_idle, encoder);
		}
		g_error_free (error);
	} else {
		encoder->priv->open_cancel = g_cancellable_new ();

		task = g_task_new (encoder, encoder->priv->open_cancel, sink_open_cb, NULL);
		g_task_run_in_thread (task, sink_open);
		g_object_unref (task);
	}
}

static gboolean
impl_get_missing_plugins (RBEncoder *encoder,
			  GstEncodingProfile *profile,
			  char ***details,
			  char ***descriptions)
{
	GList *messages = NULL;
	GstElement *encodebin;
	GstElement *enc;
	GstMessage *message;
	GstBus *bus;
	GstPad *pad;
	gboolean ret;

	ret = FALSE;
	rb_debug ("trying to check profile %s for missing plugins", gst_encoding_profile_get_name (profile));

	encodebin = gst_element_factory_make ("encodebin", NULL);
	if (encodebin == NULL) {
		g_warning ("Unable to create encodebin");
		return FALSE;
	}

	bus = gst_bus_new ();
	gst_element_set_bus (encodebin, bus);
	gst_bus_set_flushing (bus, FALSE);		/* necessary? */

	/* encodebin only checks the muxer and other required elements */
	g_object_set (encodebin, "profile", profile, NULL);
	pad = gst_element_get_static_pad (encodebin, "audio_0");
	if (pad == NULL) {
		rb_debug ("didn't get request pad, profile %s doesn't work", gst_encoding_profile_get_name (profile));
		message = gst_bus_pop (bus);
		while (message != NULL) {
			if (gst_is_missing_plugin_message (message)) {
				messages = g_list_append (messages, message);
			} else {
				gst_message_unref (message);
			}
			message = gst_bus_pop (bus);
		}
	} else {
		rb_debug ("got request pad, profile %s works", gst_encoding_profile_get_name (profile));
		gst_element_release_request_pad (encodebin, pad);
		gst_object_unref (pad);
	}

	/* make sure there's an encoder too */
	enc = rb_gst_encoding_profile_get_encoder (profile);
	if (enc == NULL) {
		GstCaps *caps;
		rb_debug ("couldn't find an encoder, profile %s doesn't work", gst_encoding_profile_get_name (profile));

		caps = rb_gst_encoding_profile_get_encoder_caps (profile);
		messages = g_list_append (messages, gst_missing_encoder_message_new (encodebin, caps));
	} else {
		rb_debug ("encoder found, profile %s works", gst_encoding_profile_get_name (profile));
		gst_object_unref (enc);
	}

	if (messages != NULL) {
		GList *m;
		int i;

		if (details != NULL) {
			*details = g_new0(char *, g_list_length (messages)+1);
		}
		if (descriptions != NULL) {
			*descriptions = g_new0(char *, g_list_length (messages)+1);
		}
		i = 0;
		for (m = messages; m != NULL; m = m->next) {
			char *str;
			if (details != NULL) {
				str = gst_missing_plugin_message_get_installer_detail (m->data);
				rb_debug ("missing plugin for profile %s: %s",
					  gst_encoding_profile_get_name (profile),
					  str);
				(*details)[i] = str;
			}
			if (descriptions != NULL) {
				str = gst_missing_plugin_message_get_description (m->data);
				(*descriptions)[i] = str;
			}
			i++;
		}

		ret = TRUE;
		rb_list_destroy_free (messages, (GDestroyNotify)gst_message_unref);
	}

	gst_object_unref (encodebin);
	gst_object_unref (bus);
	return ret;
}

static void
impl_finalize (GObject *object)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (object);

	if (encoder->priv->progress_id != 0)
		g_source_remove (encoder->priv->progress_id);

	if (encoder->priv->bus_watch_id != 0) {
		g_source_remove (encoder->priv->bus_watch_id);
		encoder->priv->bus_watch_id = 0;
	}

	if (encoder->priv->pipeline) {
		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);
		g_object_unref (encoder->priv->pipeline);
		encoder->priv->pipeline = NULL;
	}

	if (encoder->priv->outstream) {
		g_output_stream_close (encoder->priv->outstream, NULL, NULL);
		g_object_unref (encoder->priv->outstream);
		encoder->priv->outstream = NULL;
	}

	if (encoder->priv->profile) {
		gst_encoding_profile_unref (encoder->priv->profile);
		encoder->priv->profile = NULL;
	}

	g_free (encoder->priv->dest_uri);
	g_free (encoder->priv->dest_media_type);

        G_OBJECT_CLASS (rb_encoder_gst_parent_class)->finalize (object);
}

static void
rb_encoder_gst_init (RBEncoderGst *encoder)
{
        encoder->priv = (G_TYPE_INSTANCE_GET_PRIVATE ((encoder), RB_TYPE_ENCODER_GST, RBEncoderGstPrivate));
}

static void
rb_encoder_init (RBEncoderIface *iface)
{
	iface->encode = impl_encode;
	iface->cancel = impl_cancel;
	iface->get_missing_plugins = impl_get_missing_plugins;
}

static void
rb_encoder_gst_class_init (RBEncoderGstClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

        object_class->finalize = impl_finalize;

        g_type_class_add_private (klass, sizeof (RBEncoderGstPrivate));
}

RBEncoder*
rb_encoder_gst_new (void)
{
	return RB_ENCODER (g_object_new (RB_TYPE_ENCODER_GST, NULL));
}
