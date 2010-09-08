/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Based on Sound-Juicer's ripping code
 *
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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
#include <profiles/gnome-media-profiles.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gst/pbutils/missing-plugins.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"
#include "rb-encoder.h"
#include "rb-encoder-gst.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

static void rb_encoder_gst_class_init (RBEncoderGstClass *klass);
static void rb_encoder_gst_init       (RBEncoderGst *encoder);
static void rb_encoder_gst_finalize   (GObject *object);
static void rb_encoder_init (RBEncoderIface *iface);

struct _RBEncoderGstPrivate {
	GstElement *enc;
	GstElement *pipeline;

	gboolean transcoding;
	gint decoded_pads;

	gboolean completion_emitted;

	GstFormat position_format;
	gint64 total_length;
	guint progress_id;
	char *dest_uri;
	char *dest_mediatype;

	GOutputStream *outstream;

	GError *error;
};

G_DEFINE_TYPE_WITH_CODE(RBEncoderGst, rb_encoder_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_ENCODER,
					      rb_encoder_init))
#define RB_ENCODER_GST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_ENCODER_GST, RBEncoderGstPrivate))

static void rb_encoder_gst_encode (RBEncoder *encoder,
				   RhythmDBEntry *entry,
				   const char *dest,
				   const char *dest_media_type);
static void rb_encoder_gst_cancel (RBEncoder *encoder);
static gboolean rb_encoder_gst_get_media_type (RBEncoder *encoder,
					       RhythmDBEntry *entry,
					       GList *dest_media_types,
					       char **media_type,
					       char **extension);
static gboolean rb_encoder_gst_get_missing_plugins (RBEncoder *encoder,
						    const char *media_type,
						    char ***details);
static void rb_encoder_gst_emit_completed (RBEncoderGst *encoder);


static const char *
get_entry_media_type (RhythmDBEntry *entry)
{
	const char *entry_media_type;

	/* hackish mapping of gstreamer container media types to actual
	 * encoding media types; this should be unnecessary when we do proper
	 * (deep) typefinding.
	 */
	entry_media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	if (rb_safe_strcmp (entry_media_type, "audio/x-wav") == 0) {
		/* if it has a bitrate, assume it's mp3-in-wav */
		if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0) {
			entry_media_type = "audio/mpeg";
		}
	} else if (rb_safe_strcmp (entry_media_type, "application/x-id3") == 0) {
		entry_media_type = "audio/mpeg";
	} else if (rb_safe_strcmp (entry_media_type, "audio/x-flac") == 0) {
		entry_media_type = "audio/flac";
	}

	return entry_media_type;
}

static void
rb_encoder_gst_class_init (RBEncoderGstClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
	GstCaps *caps;

        object_class->finalize = rb_encoder_gst_finalize;

        g_type_class_add_private (klass, sizeof (RBEncoderGstPrivate));

	/* create the media type -> GstCaps lookup table
	 *
	 * The strings are static data for now, but if we allow dynamic changing
	 * we need to change this to use g_strdup/g_free
	 */
	klass->media_caps_table = g_hash_table_new_full (g_str_hash, g_str_equal,
							 NULL,
							 (GDestroyNotify)gst_caps_unref);

	/* M4A/AAC */
	caps = gst_caps_new_simple ("audio/mpeg",
				    "mpegversion", G_TYPE_INT, 4,
				    NULL);
	g_hash_table_insert (klass->media_caps_table, "audio/aac", caps);
	g_hash_table_insert (klass->media_caps_table, "audio/x-m4a", caps);

	/* MP3 */
	caps = gst_caps_new_simple ("audio/mpeg",
				    "mpegversion", G_TYPE_INT, 1,
				    "layer", G_TYPE_INT, 3,
				    NULL);
	g_hash_table_insert (klass->media_caps_table, "audio/mpeg", caps);

	/* hack for HAL's application/ogg reporting, assume it's audio/vorbis */
	caps = gst_caps_new_simple ("audio/x-vorbis",
				    NULL);
	g_hash_table_insert (klass->media_caps_table, "application/ogg", caps);

	/* FLAC */
	caps = gst_caps_new_simple ("audio/x-flac", NULL);
	g_hash_table_insert (klass->media_caps_table, "audio/flac", caps);

	/* create the media type -> extension fallback mapping table */
	klass->media_extension_table = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (klass->media_extension_table, "audio/mpeg", "mp3");
	g_hash_table_insert (klass->media_extension_table, "audio/x-vorbis", "ogg");
	g_hash_table_insert (klass->media_extension_table, "audio/x-flac", "flac");
	g_hash_table_insert (klass->media_extension_table, "audio/x-m4a", "m4a");
}

static void
rb_encoder_gst_init (RBEncoderGst *encoder)
{
        encoder->priv = RB_ENCODER_GST_GET_PRIVATE (encoder);
}

static void
rb_encoder_init (RBEncoderIface *iface)
{
	iface->encode = rb_encoder_gst_encode;
	iface->cancel = rb_encoder_gst_cancel;
	iface->get_media_type = rb_encoder_gst_get_media_type;
	iface->get_missing_plugins = rb_encoder_gst_get_missing_plugins;
}

static void
rb_encoder_gst_finalize (GObject *object)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (object);

	if (encoder->priv->progress_id != 0)
		g_source_remove (encoder->priv->progress_id);

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

	g_free (encoder->priv->dest_uri);
	g_free (encoder->priv->dest_mediatype);

        G_OBJECT_CLASS (rb_encoder_gst_parent_class)->finalize (object);
}

RBEncoder*
rb_encoder_gst_new (void)
{
	return RB_ENCODER (g_object_new (RB_TYPE_ENCODER_GST, NULL));
}

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
	guint64 dest_size;
	GFile *file;
	GFileInfo *file_info;

	g_return_if_fail (encoder->priv->completion_emitted == FALSE);

	if (encoder->priv->progress_id != 0) {
		g_source_remove (encoder->priv->progress_id);
		encoder->priv->progress_id = 0;
	}

	/* emit an error if no audio pad has been found and it wasn't due to an
	 * error */
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

	/* find the size of the output file, assuming we can get at it with gio */
	dest_size = 0;
	file = g_file_new_for_uri (encoder->priv->dest_uri);
	file_info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, &error);
	if (error != NULL) {
		rb_debug ("couldn't get size of destination %s: %s",
			  encoder->priv->dest_uri,
			  error->message);
		g_clear_error (&error);
	} else {
		dest_size = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
		rb_debug ("destination file size: %" G_GUINT64_FORMAT, dest_size);
		g_object_unref (file_info);
	}
	g_object_unref (file);

	encoder->priv->completion_emitted = TRUE;
	_rb_encoder_emit_completed (RB_ENCODER (encoder), dest_size, encoder->priv->dest_mediatype, encoder->priv->error);
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
	g_object_ref (G_OBJECT (encoder));

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

		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);
		if (encoder->priv->outstream != NULL) {
			rb_debug ("received EOS, closing output stream");
			g_output_stream_close_async (encoder->priv->outstream,
						     G_PRIORITY_DEFAULT,
						     NULL,
						     (GAsyncReadyCallback) output_close_cb,
						     g_object_ref (encoder));
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

	g_object_unref (G_OBJECT (encoder));
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
	if (state != GST_STATE_PLAYING)
		return FALSE;

	if (!gst_element_query_position (encoder->priv->pipeline, &format, &position)) {
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
start_pipeline (RBEncoderGst *encoder)
{
	GstStateChangeReturn result;
	GstBus *bus;

	g_assert (encoder->priv->pipeline != NULL);

	bus = gst_pipeline_get_bus (GST_PIPELINE (encoder->priv->pipeline));
	gst_bus_add_watch (bus, bus_watch_cb, encoder);

	result = gst_element_set_state (encoder->priv->pipeline, GST_STATE_PLAYING);
	if (result != GST_STATE_CHANGE_FAILURE) {
		/* start reporting progress */
		if (encoder->priv->total_length > 0) {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), 0.0);
			encoder->priv->progress_id = g_timeout_add (250, (GSourceFunc)progress_timeout_cb, encoder);
		} else {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), -1);
		}
	}
}

static const char *GST_ENCODING_PROFILE = "audioconvert ! audioresample ! %s";

static GstElement*
add_encoding_pipeline (RBEncoderGst *encoder,
		       GMAudioProfile *profile,
		       GError **error)
{
	GstElement *queue, *encoding_bin, *queue2;
	GstPad *pad;
	char *tmp;

	queue = gst_element_factory_make ("queue2", NULL);
	if (queue == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Could not create queue2 element");
		return NULL;
	}
	gst_bin_add (GST_BIN (encoder->priv->pipeline), queue);

	queue2 = gst_element_factory_make ("queue2", NULL);
	if (queue2 == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Could not create queue2 element");
		return NULL;
	}
	gst_bin_add (GST_BIN (encoder->priv->pipeline), queue2);

	/* Nice big buffers... */
	g_object_set (queue, "max-size-time", 30 * GST_SECOND, "max-size-buffers", 0, "max-size-bytes", 0, NULL);

	tmp = g_strdup_printf (GST_ENCODING_PROFILE, gm_audio_profile_get_pipeline (profile));
	rb_debug ("constructing encoding bin from pipeline string %s", tmp);
	encoding_bin = GST_ELEMENT (gst_parse_launch (tmp, error));
	g_free (tmp);

	if (encoding_bin == NULL) {
		rb_debug ("unable to construct encoding bin");
		return NULL;
	}

	/* find pads and ghost them if necessary */
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SRC)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("src", pad));
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SINK)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("sink", pad));

	gst_bin_add (GST_BIN (encoder->priv->pipeline), encoding_bin);

	if (gst_element_link_many (queue, encoding_bin, queue2, NULL) == FALSE) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Could not link encoding bin to queues");
		return NULL;
	}

	/* store the first element of the encoding graph. new_decoded_pad_cb
	 * will link to this once a decoded pad is found */
	encoder->priv->enc = queue;

	return queue2;
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

static gboolean
add_tags_from_entry (RBEncoderGst *encoder,
		     RhythmDBEntry *entry,
		     GError **error)
{
	GstTagList *tags;
	gboolean result = TRUE;
	gulong day;
	gdouble bpm;

	tags = gst_tag_list_new ();

	gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE_ALL,
			  GST_TAG_TRACK_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
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

		date = g_date_new_julian (day);
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_DATE, date,
				  NULL);
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

	{
		GstIterator *iter;
		gboolean done;

		iter = gst_bin_iterate_all_by_interface (GST_BIN (encoder->priv->pipeline), GST_TYPE_TAG_SETTER);
		done = FALSE;
		while (!done) {
			GstTagSetter *tagger = NULL;
			GstTagSetter **tagger_ptr = &tagger;

			switch (gst_iterator_next (iter, (gpointer*)tagger_ptr)) {
			case GST_ITERATOR_OK:
				gst_tag_setter_merge_tags (tagger, tags, GST_TAG_MERGE_REPLACE_ALL);
				break;
			case GST_ITERATOR_RESYNC:
				gst_iterator_resync (iter);
				break;
			case GST_ITERATOR_ERROR:
				g_set_error (error,
					     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
					     "Could not add tags to tag-setter");
				result = FALSE;
				done = TRUE;
				break;
			case GST_ITERATOR_DONE:
				done = TRUE;
				break;
			}

			if (tagger)
				gst_object_unref (tagger);
		}
		gst_iterator_free (iter);
	}

	gst_tag_list_free (tags);
	return result;
}

static void
new_decoded_pad_cb (GstElement *decodebin, GstPad *new_pad, gboolean arg1, RBEncoderGst *encoder)
{
	GstPad *enc_sinkpad;
	GstCaps *caps;
	gchar *caps_string;

	rb_debug ("new decoded pad");

	/* transcode only the first audio track. multitrack audio files are not
	 * so common anyway */
	if (encoder->priv->decoded_pads > 0)
		return;

	caps = gst_pad_get_caps (new_pad);
	caps_string = gst_caps_to_string (caps);
	gst_caps_unref (caps);

	/* only process audio data */
	if (strncmp (caps_string, "audio/", 6) == 0) {
		encoder->priv->decoded_pads++;
		enc_sinkpad = gst_element_get_static_pad (encoder->priv->enc,
				"sink");
		if (gst_pad_link (new_pad, enc_sinkpad) != GST_PAD_LINK_OK)
			rb_debug ("error linking pads");
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
	decodebin = gst_element_factory_make ("decodebin2", NULL);
	if (decodebin == NULL) {
		g_set_error (error,
				RB_ENCODER_ERROR,
				RB_ENCODER_ERROR_INTERNAL,
				"Could not create decodebin");

		return NULL;
	}

	gst_bin_add (GST_BIN (encoder->priv->pipeline), decodebin);

	g_signal_connect_object (decodebin,
			"new-decoded-pad",
			G_CALLBACK (new_decoded_pad_cb),
			encoder, 0);

	return decodebin;
}

static gboolean
attach_output_pipeline (RBEncoderGst *encoder,
			GstElement *end,
			const char *dest,
			GError **error)
{
	GFile *file;
	GFileOutputStream *stream;
	GstElement *sink;
	GError *local_error = NULL;

	/* if we can get to the location with gio, open the file here
	 * (prompting for overwrite if it already exists) and use giostreamsink.
	 * otherwise, create whatever sink element we can.
	 */
	rb_debug ("attempting to open output file %s", dest);
	file = g_file_new_for_uri (dest);
	
	sink = gst_element_factory_make ("giostreamsink", NULL);
	if (sink != NULL) {
		stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &local_error);
		if (local_error != NULL) {
			if (g_error_matches (local_error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED)) {
				rb_debug ("gio can't write to %s, so using whatever sink will work", dest);
				g_object_unref (sink);
				sink = NULL;
				g_error_free (local_error);
			} else if (g_error_matches (local_error,
						    G_IO_ERROR,
						    G_IO_ERROR_EXISTS)) {
				if (_rb_encoder_emit_overwrite (RB_ENCODER (encoder), file)) {
					g_error_free (local_error);
					stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
					if (stream == NULL) {
						return FALSE;
					}
				} else {
					g_propagate_error (error, local_error);
					return FALSE;
				}
			} else {
				g_propagate_error (error, local_error);
				return FALSE;
			}
		}

		if (stream != NULL) {
			g_object_set (sink, "stream", stream, NULL);
			encoder->priv->outstream = G_OUTPUT_STREAM (stream);
		}
	} else {
		rb_debug ("unable to create giostreamsink, falling back to default sink for %s", dest);
	}

	if (sink == NULL) {
		sink = gst_element_make_from_uri (GST_URI_SINK, dest, "sink");
		if (sink == NULL) {
			g_set_error (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_FILE_ACCESS,
				     _("Could not create a GStreamer sink element to write to %s"),
				     dest);
			return FALSE;
		}
	}

	/* provide a hook for setting sink properties */
	_rb_encoder_emit_prepare_sink (RB_ENCODER (encoder), dest, G_OBJECT (sink));

	gst_bin_add (GST_BIN (encoder->priv->pipeline), sink);
	gst_element_link (end, sink);

	return TRUE;
}

static gboolean
encoder_match_media_type (RBEncoderGst *rbencoder, GstElement *encoder, const gchar *media_type)
{
	GstPad *srcpad;
	GstCaps *element_caps = NULL;
	GstCaps *desired_caps = NULL;
	GstCaps *intersect_caps = NULL;
	gboolean match = FALSE;
	char *tmp;

	srcpad = gst_element_get_static_pad (encoder, "src");
	element_caps = gst_pad_get_caps (srcpad);

	if (element_caps == NULL) {
		g_warning ("couldn't create any element caps");
		goto end;
	}

	desired_caps = g_hash_table_lookup (RB_ENCODER_GST_GET_CLASS (rbencoder)->media_caps_table, media_type);
	if (desired_caps != NULL) {
		gst_caps_ref (desired_caps);
	} else {
		desired_caps = gst_caps_new_simple (media_type, NULL);
	}

	if (desired_caps == NULL) {
		g_warning ("couldn't create any desired caps for media type: %s", media_type);
		goto end;
	}

	intersect_caps = gst_caps_intersect (desired_caps, element_caps);
	match = !gst_caps_is_empty (intersect_caps);

	tmp = gst_caps_to_string (desired_caps);
	rb_debug ("desired caps are: %s", tmp);
	g_free (tmp);

	tmp = gst_caps_to_string (element_caps);
	rb_debug ("element caps are: %s", tmp);
	g_free (tmp);

	tmp = gst_caps_to_string (intersect_caps);
	rb_debug ("intersect caps are: %s", tmp);
	g_free (tmp);

end:
	if (intersect_caps != NULL)
		gst_caps_unref (intersect_caps);
	if (desired_caps != NULL)
		gst_caps_unref (desired_caps);
	if (element_caps != NULL)
		gst_caps_unref (element_caps);
	if (srcpad != NULL)
		gst_object_unref (GST_OBJECT (srcpad));

	return match;
}

static GstElement *
profile_bin_find_encoder (GstBin *profile_bin)
{
	GstElementFactory *factory;
	GstElement *encoder = NULL;
	GstIterator *iter;
	gboolean done = FALSE;

	iter = gst_bin_iterate_elements (profile_bin);
	while (!done) {
		gpointer data;

		switch (gst_iterator_next (iter, &data)) {
			case GST_ITERATOR_OK:
				factory = gst_element_get_factory (GST_ELEMENT (data));
				if (rb_safe_strcmp (factory->details.klass,
						"Codec/Encoder/Audio") == 0) {
					encoder = GST_ELEMENT (data);
					done = TRUE;
				}
				break;
			case GST_ITERATOR_RESYNC:
				gst_iterator_resync (iter);
				break;
			case GST_ITERATOR_ERROR:
				/* !?? */
				rb_debug ("iterator error");
				done = TRUE;
				break;
			case GST_ITERATOR_DONE:
				done = TRUE;
				break;
		}
	}
	gst_iterator_free (iter);

	if (encoder == NULL) {
		rb_debug ("unable to find encoder element");
	}
	return encoder;
}

static const char *
get_media_type_from_profile (RBEncoderGst *rbencoder, GMAudioProfile *profile)
{
	GHashTableIter iter;
	GstElement *pipeline;
	GstElement *encoder;
	char *pipeline_description;
	GError *error = NULL;
	gpointer key;
	gpointer value;

	pipeline_description =
		g_strdup_printf ("fakesrc ! %s ! fakesink",
			gm_audio_profile_get_pipeline (profile));
	pipeline = gst_parse_launch (pipeline_description, &error);
	g_free (pipeline_description);
	if (error) {
		g_warning ("unable to get media type for profile %s: %s",
			   gm_audio_profile_get_name (profile),
			   error->message);
		g_clear_error (&error);
		return NULL;
	}

	encoder = profile_bin_find_encoder (GST_BIN (pipeline));
	if (encoder == NULL) {
		g_object_unref (pipeline);
		g_warning ("Unable to get media type for profile %s: couldn't find encoder",
			   gm_audio_profile_get_name (profile));
		return NULL;
	}

	g_hash_table_iter_init (&iter, RB_ENCODER_GST_GET_CLASS (rbencoder)->media_caps_table);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const char *media_type = (const char *)key;
		if (encoder_match_media_type (rbencoder, encoder, media_type)) {
			return media_type;
		}
	}

	g_warning ("couldn't identify media type for profile %s", gm_audio_profile_get_name (profile));
	return NULL;
}

static GMAudioProfile*
get_profile_from_media_type (RBEncoderGst *rbencoder, const char *media_type)
{
	GList *profiles, *walk;
	gchar *pipeline_description;
	GstElement *pipeline;
	GstElement *encoder;
	GMAudioProfile *profile;
	GMAudioProfile *matching_profile = NULL;
	GError *error = NULL;

	rb_debug ("Looking up profile for media type '%s'", media_type);

	profiles = gm_audio_profile_get_active_list ();
	for (walk = profiles; walk; walk = g_list_next (walk)) {
		profile = (GMAudioProfile *) walk->data;
		pipeline_description =
			g_strdup_printf ("fakesrc ! %s ! fakesink",
				gm_audio_profile_get_pipeline (profile));
		pipeline = gst_parse_launch (pipeline_description, &error);
		g_free (pipeline_description);
		if (error) {
			g_error_free (error);
			error = NULL;
			continue;
		}

		encoder = profile_bin_find_encoder (GST_BIN (pipeline));
		if (encoder == NULL) {
			g_object_unref (pipeline);
			continue;
		}

		if (encoder_match_media_type (rbencoder, encoder, media_type)) {
			matching_profile = profile;
			gst_object_unref (GST_OBJECT (encoder));
			gst_object_unref (GST_OBJECT (pipeline));
			break;
		}

		gst_object_unref (GST_OBJECT (encoder));
		gst_object_unref (GST_OBJECT (pipeline));
	}

	if (matching_profile)
		g_object_ref (matching_profile);
	g_list_free (profiles);

	return matching_profile;
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

	src = gst_element_make_from_uri (GST_URI_SRC, uri, "source");
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

static gboolean
copy_track (RBEncoderGst *encoder,
	    RhythmDBEntry *entry,
	    const char *dest,
	    GError **error)
{
	/* source ! sink */
	GstElement *src;

	g_assert (encoder->priv->pipeline == NULL);

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		return FALSE;

	if (!attach_output_pipeline (encoder, src, dest, error))
		return FALSE;

	start_pipeline (encoder);
	return TRUE;
}

static gboolean
transcode_track (RBEncoderGst *encoder,
	 	 RhythmDBEntry *entry,
		 const char *dest,
		 GError **error)
{
	/* src ! decodebin ! queue ! encoding_profile ! queue ! sink */
	GMAudioProfile *profile;
	GstElement *src, *decoder, *end;

	g_assert (encoder->priv->pipeline == NULL);
	g_assert (encoder->priv->dest_mediatype != NULL);

	rb_debug ("transcoding to %s, media type %s", dest, encoder->priv->dest_mediatype);
	profile = get_profile_from_media_type (encoder, encoder->priv->dest_mediatype);
	if (profile == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR,
			     RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
			     "Unable to locate encoding profile for media-type %s",
			     encoder->priv->dest_mediatype);
		goto error;
	}

	rb_debug ("selected profile %s", gm_audio_profile_get_name (profile));

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		goto error;

	decoder = add_decoding_pipeline (encoder, error);
	if (decoder == NULL)
		goto error;

	if (gst_element_link (src, decoder) == FALSE) {
		rb_debug ("unable to link source element to decodebin");
		g_set_error (error,
			     RB_ENCODER_ERROR,
			     RB_ENCODER_ERROR_INTERNAL,
			     "Unable to link source element to decodebin");
		goto error;
	}

	end = add_encoding_pipeline (encoder, profile, error);
	if (end == NULL)
		goto error;

	if (!attach_output_pipeline (encoder, end, dest, error))
		goto error;
	if (!add_tags_from_entry (encoder, entry, error))
		goto error;

	start_pipeline (encoder);
	return TRUE;
error:
	if (profile)
		g_object_unref (profile);

	return FALSE;
}

static void
rb_encoder_gst_cancel (RBEncoder *encoder)
{
	RBEncoderGstPrivate *priv = RB_ENCODER_GST (encoder)->priv;

	if (priv->pipeline != NULL) {
		gst_element_set_state (priv->pipeline, GST_STATE_NULL);
		g_object_unref (priv->pipeline);
		priv->pipeline = NULL;
	}

	if (priv->outstream != NULL) {
		GError *error = NULL;
		GFile *f;
		g_output_stream_close (priv->outstream, NULL, &error);
		if (error != NULL) {
			rb_debug ("error closing output stream: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (priv->outstream);
		priv->outstream = NULL;

		/* try to delete the output file, since it's incomplete */
		error = NULL;
		f = g_file_new_for_uri (priv->dest_uri);
		if (g_file_delete (f, NULL, &error) == FALSE) {
			rb_debug ("error deleting incomplete output file: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (f);
	}

	rb_encoder_gst_emit_completed (RB_ENCODER_GST (encoder));
}

static gboolean
cancel_idle (RBEncoder *encoder)
{
	rb_encoder_gst_cancel (encoder);
	g_object_unref (encoder);
	return FALSE;
}

static void
rb_encoder_gst_encode (RBEncoder *bencoder,
		       RhythmDBEntry *entry,
		       const char *dest,
		       const char *dest_media_type)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (bencoder);
	const char *entry_media_type;
	GError *error = NULL;
	gboolean result;

	g_return_if_fail (encoder->priv->pipeline == NULL);
	g_return_if_fail (dest_media_type != NULL);

	entry_media_type = get_entry_media_type (entry);

	if (rb_uri_create_parent_dirs (dest, &error) == FALSE) {
		error = g_error_new_literal (RB_ENCODER_ERROR,
					     RB_ENCODER_ERROR_FILE_ACCESS,
					     error->message);		/* I guess */

		set_error (encoder, error);
		g_error_free (error);
		g_idle_add ((GSourceFunc) cancel_idle, g_object_ref (encoder));
		return;
	}

	g_free (encoder->priv->dest_mediatype);
	g_free (encoder->priv->dest_uri);
	encoder->priv->dest_uri = g_strdup (dest);

	/* if destination and source media types are the same, copy it */
	if (g_strcmp0 (entry_media_type, dest_media_type) == 0) {
		rb_debug ("source file already has required media type %s, copying rather than transcoding", dest_media_type);
		encoder->priv->total_length = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
		encoder->priv->position_format = GST_FORMAT_BYTES;

		result = copy_track (RB_ENCODER_GST (encoder), entry, dest, &error);
		encoder->priv->dest_mediatype = g_strdup (entry_media_type);
	} else {
		encoder->priv->total_length = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
		encoder->priv->position_format = GST_FORMAT_TIME;
		encoder->priv->dest_mediatype = g_strdup (dest_media_type);

		result = transcode_track (RB_ENCODER_GST (encoder), entry, dest, &error);
	}

	if (result == FALSE) {
		set_error (encoder, error);
		g_error_free (error);
		g_idle_add ((GSourceFunc) cancel_idle, g_object_ref (encoder));
	}
}

static char *
get_extension_for_media_type (RBEncoder *encoder, const char *media_type)
{
	char *ext;
	GMAudioProfile *profile;

	profile = get_profile_from_media_type (RB_ENCODER_GST (encoder), media_type);
	if (profile) {
		ext = g_strdup (gm_audio_profile_get_extension (profile));
		rb_debug ("got extension %s from profile", ext);
		g_object_unref (profile);
	} else {
		GHashTable *map = RB_ENCODER_GST_GET_CLASS (encoder)->media_extension_table;
		ext = g_strdup (g_hash_table_lookup (map, media_type));
		rb_debug ("got extension %s from fallback extension map", ext);
	}

	return ext;
}

static gboolean
rb_encoder_gst_get_media_type (RBEncoder *encoder,
			       RhythmDBEntry *entry,
			       GList *dest_media_types,
			       char **media_type,
			       char **extension)
{
	GList *l;
	GMAudioProfile *profile;
	const char *src_media_type;

	src_media_type = get_entry_media_type (entry);
	g_return_val_if_fail (src_media_type != NULL, FALSE);

	if (media_type != NULL)
		*media_type = NULL;
	if (extension != NULL)
		*extension = NULL;


	/* if we don't have any destination format requirements,
	 * use preferred encoding for raw files, otherwise accept as is
	 */
	if (dest_media_types == NULL) {
		if (g_str_has_prefix (src_media_type, "audio/x-raw")) {
			const char *profile_name = eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT);
			const char *mt;
			profile = gm_audio_profile_lookup (profile_name);

			mt = get_media_type_from_profile (RB_ENCODER_GST (encoder), profile);
			if (mt == NULL) {
				/* ugh */
				return FALSE;
			}
			rb_debug ("selected preferred media type %s (extension %s)",
				  mt,
				  gm_audio_profile_get_extension (profile));
			if (media_type != NULL)
				*media_type = g_strdup (mt);
			if (extension != NULL)
				*extension = g_strdup (gm_audio_profile_get_extension (profile));
		} else {
			if (media_type != NULL)
				*media_type = g_strdup (src_media_type);

			if (extension != NULL)
				*extension = get_extension_for_media_type (encoder, src_media_type);
		}
		return TRUE;
	}

	/* check if the source media type is in the destination list */
	if (rb_string_list_contains (dest_media_types, src_media_type)) {
		rb_debug ("found source media type %s in destination type list", src_media_type);
		if (media_type != NULL)
			*media_type = g_strdup (src_media_type);
		if (extension != NULL)
			*extension = get_extension_for_media_type (encoder, src_media_type);
		return TRUE;
	}

	/* now find the type in the destination media type list that we have a
	 * profile for.
	 */
	for (l = dest_media_types; l != NULL; l = g_list_next (l)) {
		GMAudioProfile *profile;
		const char *mt;

		mt = (const char *)l->data;
		profile = get_profile_from_media_type (RB_ENCODER_GST (encoder), mt);
		if (profile) {
			rb_debug ("selected destination media type %s (extension %s)",
				  mt,
				  gm_audio_profile_get_extension (profile));
			if (extension != NULL)
				*extension = g_strdup (gm_audio_profile_get_extension (profile));

			if (media_type != NULL)
				*media_type = g_strdup (mt);
			g_object_unref (profile);
			return TRUE;
		}
	}

	return FALSE;
}

static int
add_element_if_missing (char ***details, int index, const char *element_name)
{
	GstElementFactory *factory;
	factory = gst_element_factory_find (element_name);
	if (factory != NULL) {
		rb_debug ("element factory %s is available", element_name);
		gst_object_unref (factory);
	} else {
		rb_debug ("element factory %s not available, adding detail string", element_name);
		(*details)[index++] = gst_missing_element_installer_detail_new (element_name);
	}
	return index;
}

static gboolean
rb_encoder_gst_get_missing_plugins (RBEncoder *encoder,
				    const char *media_type,
				    char ***details)
{
	/*
	 * since encoding profiles use explicit element names, we need to
	 * check for and request installation of exactly the element names
	 * used in the profile, rather than requesting an encoder for a media
	 * type.  parsing the profile pipeline description is too much work,
	 * so we'll just use the element names from the default profiles.
	 *
	 * mp3: 	lame, id3v2mux
	 * aac:		faac, ffmux_mp4
	 * ogg vorbis:	vorbisenc, oggmux
	 * flac:	flacenc
	 * mp2:		twolame, id3v2mux   (we don't have a media type for this)
	 */

	int i = 0;
	*details = g_new0(char *, 3);

	if (g_strcmp0 (media_type, "audio/mpeg") == 0) {
		i = add_element_if_missing (details, i, "lame");
		i = add_element_if_missing (details, i, "id3v2mux");
	} else if (g_strcmp0 (media_type, "audio/x-aac") == 0) {
		i = add_element_if_missing (details, i, "faac");
		i = add_element_if_missing (details, i, "ffmux_mp4");
	} else if (g_strcmp0 (media_type, "application/ogg") == 0) {
		i = add_element_if_missing (details, i, "vorbisenc");
		i = add_element_if_missing (details, i, "oggmux");
	} else if (g_strcmp0 (media_type, "audio/x-flac") == 0) {
		i = add_element_if_missing (details, i, "flacenc");
	} else {
		rb_debug ("unable to provide missing plugin details for unknown media type %s",
			  media_type);
		g_strfreev (*details);
		*details = NULL;
		return FALSE;
	}
	rb_debug ("have %d missing plugin detail strings", i);
	return TRUE;
}
