/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * arch-tag: Implementation of GStreamer encoding backend
 *
 * Based on Sound-Juicer's ripping code
 *
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 * Copyright (C) 2006 James Livingston <jrl@ids.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <gst/gst.h>
#include <string.h>
#include <profiles/gnome-media-profiles.h>
#include <gtk/gtk.h>

#ifdef HAVE_GSTREAMER_0_10
#include <gst/tag/tag.h>
#endif

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"
#include "rb-encoder.h"
#include "rb-encoder-gst.h"
#include "rb-debug.h"
#include "rb-util.h"

#ifdef HAVE_GSTREAMER_0_8
#define GstStateChangeReturn GstElementStateReturn
#define GstState GstElementState
#define GST_STATE_CHANGE_FAILURE GST_STATE_FAILURE
#define gst_caps_unref gst_caps_free
#endif

static void rb_encoder_gst_class_init (RBEncoderGstClass *klass);
static void rb_encoder_gst_init       (RBEncoderGst *encoder);
static void rb_encoder_gst_finalize   (GObject *object);
static void rb_encoder_init (RBEncoderIface *iface);

struct _RBEncoderGstPrivate {
	GstElement *enc;
	GstElement *pipeline;

	gboolean transcoding;
	gint decoded_pads;

	gboolean error_emitted;
	gboolean completion_emitted;

	GstFormat position_format;
	gint64 total_length;
	guint progress_id;
};

G_DEFINE_TYPE_WITH_CODE(RBEncoderGst, rb_encoder_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_ENCODER,
					      rb_encoder_init))
#define RB_ENCODER_GST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_ENCODER_GST, RBEncoderGstPrivate))

static gboolean rb_encoder_gst_encode (RBEncoder *encoder,
				       RhythmDBEntry *entry,
				       const char *dest,
				       GList *mime_types);
static void rb_encoder_gst_cancel (RBEncoder *encoder);
static gboolean rb_encoder_gst_get_preferred_mimetype (RBEncoder *encoder,
						       GList *mime_types,
						       char **mime,
						       char **extension);
static void rb_encoder_gst_emit_completed (RBEncoderGst *encoder);


static void
rb_encoder_gst_class_init (RBEncoderGstClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
	GstCaps *caps;

        object_class->finalize = rb_encoder_gst_finalize;

        g_type_class_add_private (klass, sizeof (RBEncoderGstPrivate));

	/* create the mimetype -> GstCaps lookup table
	 *
	 * The strings are static data for now, but if we allow dynamic changing
	 * we need to change this to use g_strdup/g_free
	 */
	klass->mime_caps_table = g_hash_table_new_full (g_str_hash, g_str_equal,
							NULL, (GDestroyNotify)gst_caps_unref);

	/* AAC */
	caps = gst_caps_new_simple ("audio/mpeg",
				    "mpegversion", G_TYPE_INT, 4,
				    NULL);
	g_hash_table_insert (klass->mime_caps_table, "audio/aac", caps);

	/* MP3 */
	caps = gst_caps_new_simple ("audio/mpeg",
				    "mpegversion", G_TYPE_INT, 1,
				    "layer", G_TYPE_INT, 3,
				    NULL);
	g_hash_table_insert (klass->mime_caps_table, "audio/mpeg", caps);
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
	iface->get_preferred_mimetype = rb_encoder_gst_get_preferred_mimetype;
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

        G_OBJECT_CLASS (rb_encoder_gst_parent_class)->finalize (object);
}

RBEncoder*
rb_encoder_gst_new (void)
{
	return RB_ENCODER (g_object_new (RB_TYPE_ENCODER_GST, NULL));
}

static void
rb_encoder_gst_emit_error (RBEncoderGst *encoder, GError *error)
{
	encoder->priv->error_emitted = TRUE;
	_rb_encoder_emit_error (RB_ENCODER (encoder), error);
}

static void
rb_encoder_gst_emit_completed (RBEncoderGst *encoder)
{
	GError *error = NULL;

	g_return_if_fail (encoder->priv->completion_emitted == FALSE);

	if (encoder->priv->progress_id != 0)
		g_source_remove (encoder->priv->progress_id);

	/* emit an error if no audio pad has been found and it wasn't due to an
	 * error */
	if (encoder->priv->error_emitted == FALSE &&
			encoder->priv->transcoding &&
			encoder->priv->decoded_pads == 0) {
		rb_debug ("received EOS and no decoded pad");
		g_set_error (&error,
				RB_ENCODER_ERROR,
				RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
				"no decodable audio pad found");

		rb_encoder_gst_emit_error (encoder, error);
		g_error_free (error);
	}

	encoder->priv->completion_emitted = TRUE;
	_rb_encoder_emit_completed (RB_ENCODER (encoder));
}

#ifdef HAVE_GSTREAMER_0_10
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
		rb_encoder_gst_emit_error (encoder, error);
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
		rb_debug ("received EOS");

		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);

		rb_encoder_gst_emit_completed (encoder);

		g_object_unref (encoder->priv->pipeline);
		encoder->priv->pipeline = NULL;
		break;

	default:
		rb_debug ("message of type %s", gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
		break;
	}

	g_object_unref (G_OBJECT (encoder));
	return TRUE;
}
#endif

#ifdef HAVE_GSTREAMER_0_8
static void
gst_eos_cb (GstElement *element, RBEncoderGst *encoder)
{
	rb_debug ("received EOS");

	gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);

	rb_encoder_gst_emit_completed (encoder);

	g_object_unref (encoder->priv->pipeline);
	encoder->priv->pipeline = NULL;
}

static void
gst_error_cb (GstElement *element,
			  GstElement *source,
			  GError *error,
			  gchar *debug,
			  RBEncoderGst *encoder)
{
	rb_encoder_gst_emit_error (encoder, error);
	rb_debug ("received error %s", debug);

	rb_encoder_cancel (RB_ENCODER (encoder));
}

#endif

static gboolean
progress_timeout_cb (RBEncoderGst *encoder)
{
	gint64 position;
	static GstFormat format;
	GstState state;

	if (encoder->priv->pipeline == NULL)
		return FALSE;

	format = encoder->priv->position_format;

#ifdef HAVE_GSTREAMER_0_10
	gst_element_get_state (encoder->priv->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
	if (state != GST_STATE_PLAYING)
		return FALSE;

	if (!gst_element_query_position (encoder->priv->pipeline, &format, &position)) {
		g_warning ("Could not get current track position");
		return TRUE;
	}
#elif HAVE_GSTREAMER_0_8
	state = gst_element_get_state (encoder->priv->pipeline);
	if (state != GST_STATE_PLAYING)
		return FALSE;

	if (!gst_element_query (encoder->priv->pipeline, GST_QUERY_POSITION, &format, &position)) {
		g_warning ("Could not get current track position");
		return TRUE;
	}
#endif

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

static gboolean
start_pipeline (RBEncoderGst *encoder, GError **error)
{
	GstStateChangeReturn result;
#ifdef HAVE_GSTREAMER_0_10
	GstBus *bus;

	g_return_val_if_fail (encoder->priv->pipeline != NULL, FALSE);

	bus = gst_pipeline_get_bus (GST_PIPELINE (encoder->priv->pipeline));
	gst_bus_add_watch (bus, bus_watch_cb, encoder);

	result = gst_element_set_state (encoder->priv->pipeline, GST_STATE_PLAYING);
#elif HAVE_GSTREAMER_0_8

	g_return_val_if_fail (encoder->priv->pipeline != NULL, FALSE);

	g_signal_connect_object (G_OBJECT (encoder->priv->pipeline),
				 "error", G_CALLBACK (gst_error_cb),
				 encoder, 0);
	g_signal_connect_object (G_OBJECT (encoder->priv->pipeline),
				 "eos", G_CALLBACK (gst_eos_cb),
				 encoder, 0);
	result = gst_element_set_state (encoder->priv->pipeline, GST_STATE_PLAYING);
#endif

	if (result != GST_STATE_CHANGE_FAILURE) {
		/* start reporting progress */
		if (encoder->priv->total_length > 0) {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), 0.0);
			encoder->priv->progress_id = g_timeout_add (250, (GSourceFunc)progress_timeout_cb, encoder);
		} else {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), -1);
		}
	}

	return (result != GST_STATE_CHANGE_FAILURE);
}

#ifdef HAVE_GSTREAMER_0_8
/* this is basically what the function in 0.10 does */
static GstPad*
rb_gst_bin_find_unconnected_pad (GstBin *bin, GstPadDirection dir)
{
	const GList *elements, *el;

	elements = gst_bin_get_list (GST_BIN (bin));
	for (el = elements; el != NULL; el = el->next) {
		GstElement *element = GST_ELEMENT (el->data);
		const GList *pads, *pl;

		pads = gst_element_get_pad_list (element);

		for (pl = pads; pl != NULL; pl = pl->next) {
			GstPad *pad = GST_PAD (pl->data);

			if (!GST_PAD_IS_LINKED (pad) && GST_PAD_DIRECTION (pad) == dir)
				return pad;
		}
	}

	return NULL;
}

#define gst_bin_find_unconnected_pad rb_gst_bin_find_unconnected_pad

const char *GST_ENCODING_PROFILE = "audioscale ! audioconvert ! %s";
#elif HAVE_GSTREAMER_0_10
const char *GST_ENCODING_PROFILE = "audioresample ! audioconvert ! %s";
#endif

static GstElement*
add_encoding_pipeline (RBEncoderGst *encoder,
		       GMAudioProfile *profile,
		       GError **error)
{
	GstElement *queue, *encoding_bin, *queue2;
	GstPad *pad;
	char *tmp;

	queue = gst_element_factory_make ("queue", NULL);
	if (queue == NULL)
		return NULL;
	gst_bin_add (GST_BIN (encoder->priv->pipeline), queue);

	queue2 = gst_element_factory_make ("queue", NULL);
	if (queue2 == NULL)
		return NULL;
	gst_bin_add (GST_BIN (encoder->priv->pipeline), queue2);

	/* Nice big buffers... */
	g_object_set (queue, "max-size-time", 120 * GST_SECOND, NULL);

	tmp = g_strdup_printf (GST_ENCODING_PROFILE, gm_audio_profile_get_pipeline (profile));
	encoding_bin = GST_ELEMENT (gst_parse_launch (tmp, error));
	g_free (tmp);

	if (encoding_bin == NULL)
		return NULL;

	/* find pads and ghost them if necessary */
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SRC)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("src", pad));
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SINK)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("sink", pad));

	gst_bin_add (GST_BIN (encoder->priv->pipeline), encoding_bin);

	if (gst_element_link_many (queue, encoding_bin, queue2, NULL) == FALSE)
		return NULL;

	/* store the first element of the encoding graph. new_decoded_pad_cb
	 * will link to this once a decoded pad is found */
	encoder->priv->enc = queue;

	return queue2;
}

static gboolean
add_tags_from_entry (RBEncoderGst *encoder,
		     RhythmDBEntry *entry,
		     GError **error)
{
	GstTagList *tags;
	gboolean result = TRUE;
	gulong day;

	tags = gst_tag_list_new ();

	gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE_ALL,
			  /* TODO: compute replay-gain */
			  GST_TAG_TITLE, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
			  GST_TAG_ARTIST, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
			  GST_TAG_TRACK_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
			  GST_TAG_ALBUM_VOLUME_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER),
			  GST_TAG_ALBUM, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
			  GST_TAG_GENRE, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE),
			  GST_TAG_ENCODER, "Rhythmbox",
			  GST_TAG_ENCODER_VERSION, VERSION,
			  NULL);

	day = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);

	if (day > 0) {
		GDate *date;

		date = g_date_new_julian (day);
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_DATE, date,
				  NULL);
		g_date_free (date);
	}
#ifdef GST_TAG_MUSICBRAINZ_TRACKID
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID)) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_MUSICBRAINZ_TRACKID, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID),
				  NULL);
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID)) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_MUSICBRAINZ_ARTISTID, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTID),
				  NULL);
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID)) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_MUSICBRAINZ_ALBUMID, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMID),
				  NULL);
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID)) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_MUSICBRAINZ_ALBUMARTISTID, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ALBUMARTISTID),
				  NULL);
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTSORTNAME)) {
		gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
				  GST_TAG_MUSICBRAINZ_SORTNAME, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MUSICBRAINZ_ARTISTSORTNAME),
				  NULL);
	}
#endif

#ifdef HAVE_GSTREAMER_0_10
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
#elif HAVE_GSTREAMER_0_8
	{
		GstElement *tagger;

		tagger = gst_bin_get_by_interface (GST_BIN (encoder->priv->pipeline), GST_TYPE_TAG_SETTER);
		if (tagger)
			gst_tag_setter_merge (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
	}
#endif

	gst_tag_list_free (tags);
	return result;
}

static gboolean
gnomevfs_allow_overwrite_cb (GstElement *element, GnomeVFSURI *uri, RBEncoderGst *encoder)
{
	GtkWidget *dialog;
	gint response;
	char *name;
	char *display_name;

	name = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_USER_NAME | GNOME_VFS_URI_HIDE_PASSWORD);
	display_name = gnome_vfs_format_uri_for_display (name);

	dialog = gtk_message_dialog_new (NULL, 0,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
					 _("Do you want to overwrite the file \"%s\"?"),
					 display_name);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	g_free (display_name);
	g_free (name);

	return (response == GTK_RESPONSE_YES);
}

static void
new_decoded_pad_cb (GstElement *decodebin,
		GstPad *new_pad, gboolean arg1,
		RBEncoderGst *encoder)
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
		enc_sinkpad = gst_element_get_pad (encoder->priv->enc,
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
	decodebin = gst_element_factory_make ("decodebin", NULL);
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
	GstElement *sink;

	sink = gst_element_make_from_uri (GST_URI_SINK, dest, "sink");

	/* handle overwriting if we are using gnomevfssink
	 * it would be nice if GST had an interface for sinks with thi, but it doesn't
	 */
	if (g_type_is_a (G_OBJECT_TYPE (sink), g_type_from_name ("GstGnomeVFSSink"))) {
		g_signal_connect_object (G_OBJECT (sink),
					 "allow-overwrite", G_CALLBACK (gnomevfs_allow_overwrite_cb),
					 encoder, 0);
	}

	gst_bin_add (GST_BIN (encoder->priv->pipeline), sink);
	gst_element_link (end, sink);

	return TRUE;
}

static gboolean
encoder_match_mime (RBEncoderGst *rbencoder, GstElement *encoder, const gchar *mime_type)
{
	GstPad *srcpad;
	GstCaps *element_caps = NULL;
	GstCaps *desired_caps = NULL;
	GstCaps *intersect_caps = NULL;
	gboolean match = FALSE;
	char *tmp;

	srcpad = gst_element_get_pad (encoder, "src");
	element_caps = gst_pad_get_caps (srcpad);

	if (element_caps == NULL) {
		g_warning ("couldn't create any element caps");
		goto end;
	}

	desired_caps = g_hash_table_lookup (RB_ENCODER_GST_GET_CLASS (rbencoder)->mime_caps_table, mime_type);
	if (desired_caps != NULL) {
		gst_caps_ref (desired_caps);
	} else {
		desired_caps = gst_caps_new_simple (mime_type, NULL);
	}

	if (desired_caps == NULL) {
		g_warning ("couldn't create any desired caps for mimetype: %s", mime_type);
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
#ifdef HAVE_GSTREAMER_0_10
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

	return encoder;
#else
	return NULL;
#endif
}

static GMAudioProfile*
get_profile_from_mime_type (RBEncoderGst *rbencoder, const char *mime_type)
{
	GList *profiles, *walk;
	gchar *pipeline_description;
	GstElement *pipeline;
	GstElement *encoder;
	GMAudioProfile *profile;
	GMAudioProfile *matching_profile = NULL;
	GError *error = NULL;

	rb_debug ("Looking up profile for mimetype '%s'", mime_type);

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

		if (encoder_match_mime (rbencoder, encoder, mime_type)) {
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

static GMAudioProfile*
get_profile_from_mime_types (RBEncoderGst *rbencoder, GList *mime_types)
{
	GList *l;

	for (l = mime_types; l != NULL; l = g_list_next (l)) {
		GMAudioProfile *profile;

		profile = get_profile_from_mime_type (rbencoder, (const char *)l->data);
		if (profile)
			return profile;
	}

	return NULL;
}

static GstElement *
create_pipeline_and_source (RBEncoderGst *encoder,
			    RhythmDBEntry *entry,
			    GError **error)
{
	char *uri;
	GstElement *src;

	uri = rhythmdb_entry_get_playback_uri (entry);
	if (uri == NULL)
		return NULL;

	src = gst_element_make_from_uri (GST_URI_SRC, uri, "source");
	if (src == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "could not create source element for '%s'", uri);
		g_free (uri);
		return NULL;
	}

	encoder->priv->pipeline = gst_pipeline_new ("pipeline");
	gst_bin_add (GST_BIN (encoder->priv->pipeline), src);

	/* TODO: add progress reporting */

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

	if (!start_pipeline (encoder, error))
		return FALSE;

	return TRUE;
}

static gboolean
extract_track (RBEncoderGst *encoder,
	       RhythmDBEntry *entry,
	       const char *dest,
	       GError **error)
{
	/* cdsrc ! encoder ! sink */
	char *uri;
	const char *device;
	const char *profile_name;
	GMAudioProfile *profile;
	GstElement *src, *end;

	g_assert (encoder->priv->pipeline == NULL);

	profile_name = eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT);
	profile = gm_audio_profile_lookup (profile_name);
	if (profile == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
			     "Could not find encoding profile '%s'", profile_name);
		return FALSE;
	}

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		return FALSE;

	/* setup cd extraction properties */
	uri = rhythmdb_entry_get_playback_uri (entry);
	if (uri == NULL)
		return FALSE;

	device = g_utf8_strrchr (uri, -1, '#');
	g_object_set (G_OBJECT (src),
		      "device", device + 1, /* skip the '#' */
		      "track", rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
		      NULL);
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (src), "paranoia-mode")) {
		int paranoia_mode;

		paranoia_mode = 255; /* TODO: make configurable */
		g_object_set (G_OBJECT (src), "paranoia-mode", paranoia_mode, NULL);
	}
	g_free (uri);

	end = add_encoding_pipeline (encoder, profile, error);
	if (end == NULL)
		return FALSE;
	if (gst_element_link (src, encoder->priv->enc) == FALSE)
		return FALSE;

	if (!attach_output_pipeline (encoder, end, dest, error))
		return FALSE;
	if (!add_tags_from_entry (encoder, entry, error))
		return FALSE;
	if (!start_pipeline (encoder, error))
		return FALSE;

	return TRUE;
}

static gboolean
transcode_track (RBEncoderGst *encoder,
	 	 RhythmDBEntry *entry,
		 const char *dest,
		 GList *mime_types,
		 GError **error)
{
	/* src ! decodebin ! queue ! encoding_profile ! queue ! sink */
	GMAudioProfile *profile;
	GstElement *src, *decoder, *end;

	g_assert (encoder->priv->pipeline == NULL);

	profile = get_profile_from_mime_types (encoder, mime_types);
	if (profile == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR,
			     RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
			     "Unable to locate encoding profile for mime-type "
			     /*"'%s'", mime_type*/);
		goto error;
	} else {
		rb_debug ("selected profile %s",
				gm_audio_profile_get_name (profile));
	}

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		goto error;

	decoder = add_decoding_pipeline (encoder, error);
	if (decoder == NULL)
		goto error;

	if (gst_element_link (src, decoder) == FALSE)
		goto error;

	end = add_encoding_pipeline (encoder, profile, error);
	if (end == NULL)
		goto error;

	if (!attach_output_pipeline (encoder, end, dest, error))
		goto error;
	if (!add_tags_from_entry (encoder, entry, error))
		goto error;
	if (!start_pipeline (encoder, error))
		goto error;

	return TRUE;
error:
	if (profile)
		g_object_unref (profile);

	return FALSE;
}

static GnomeVFSResult
create_parent_dirs_uri (GnomeVFSURI *uri)
{
	GnomeVFSURI *parent_uri;
	GnomeVFSResult result;

	if (gnome_vfs_uri_exists (uri))
		return GNOME_VFS_OK;

	parent_uri = gnome_vfs_uri_get_parent (uri);
	result = create_parent_dirs_uri (parent_uri);
	gnome_vfs_uri_unref (parent_uri);
	if (result != GNOME_VFS_OK)
		return result;

	return gnome_vfs_make_directory_for_uri (uri, 0750);
}

static GnomeVFSResult
create_parent_dirs (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	GnomeVFSURI *parent_uri;
	GnomeVFSResult result;

	vfs_uri = gnome_vfs_uri_new (uri);
	parent_uri = gnome_vfs_uri_get_parent (vfs_uri);

	result = create_parent_dirs_uri (parent_uri);

	gnome_vfs_uri_unref (parent_uri);
	gnome_vfs_uri_unref (vfs_uri);
	return result;
}

static void
rb_encoder_gst_cancel (RBEncoder *encoder)
{
	RBEncoderGstPrivate *priv = RB_ENCODER_GST (encoder)->priv;

	if (priv->pipeline == NULL)
		return;

	gst_element_set_state (priv->pipeline, GST_STATE_NULL);
	g_object_unref (priv->pipeline);
	priv->pipeline = NULL;

	rb_encoder_gst_emit_completed (RB_ENCODER_GST (encoder));
}

static gboolean
rb_encoder_gst_encode (RBEncoder *encoder,
		       RhythmDBEntry *entry,
		       const char *dest,
		       GList *mime_types)
{
	RBEncoderGstPrivate *priv = RB_ENCODER_GST (encoder)->priv;
	const char *entry_mime_type;
	gboolean copy;
	gboolean was_raw;
	gboolean result;
	GError *error = NULL;
	GnomeVFSResult vfsresult;

	g_return_val_if_fail (priv->pipeline == NULL, FALSE);

	entry_mime_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	was_raw = g_str_has_prefix (entry_mime_type, "audio/x-raw");

	/* hackish mapping of gstreamer media types to mime types; this
	 * should be easier when we do proper (deep) typefinding.
	 */
	if (rb_safe_strcmp (entry_mime_type, "audio/x-wav") == 0) {
		/* if it has a bitrate, assume it's mp3-in-wav */
		if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
			entry_mime_type = "audio/mpeg";
	} else if (rb_safe_strcmp (entry_mime_type, "application/x-id3") == 0) {
		entry_mime_type = "audio/mpeg";
	}

	vfsresult = create_parent_dirs (dest);
	if (vfsresult != GNOME_VFS_OK) {
		error = g_error_new_literal (RB_ENCODER_ERROR,
					     RB_ENCODER_ERROR_FILE_ACCESS,
					     gnome_vfs_result_to_string (vfsresult));

		_rb_encoder_emit_error (encoder, error);
		_rb_encoder_emit_completed (encoder);
		g_error_free (error);
		return FALSE;
	}

	if (mime_types == NULL) {
		/* don't copy raw audio */
		copy = !was_raw;
	} else {
		GList *l;

		/* see if it's already in any of the destination formats */
		copy = FALSE;
		for (l = mime_types; l != NULL; l = g_list_next (l)) {
			rb_debug ("Comparing mimetypes '%s' '%s'", entry_mime_type, (char *)l->data);
			if (rb_safe_strcmp (entry_mime_type, l->data) == 0) {
				rb_debug ("Matched mimetypes '%s' '%s'", entry_mime_type, (char *)l->data);

				copy = TRUE;
				break;
			}
		}
	}

	if (copy) {
		priv->total_length = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
		priv->position_format = GST_FORMAT_BYTES;

		result = copy_track (RB_ENCODER_GST (encoder), entry, dest, &error);
	} else {
		priv->total_length = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
		priv->position_format = GST_FORMAT_TIME;

		if (mime_types == NULL) {
			result = extract_track (RB_ENCODER_GST (encoder), entry, dest, &error);
		} else {
			result = transcode_track (RB_ENCODER_GST (encoder), entry, dest, mime_types, &error);
		}
	}

	if (error) {
		RBEncoderGst *enc = RB_ENCODER_GST (encoder);

		rb_encoder_gst_emit_error (enc, error);
		g_error_free (error);
		if (enc->priv->pipeline == NULL)
			rb_encoder_gst_emit_completed (enc);
		else
			/* this will unref the pipeline and call emit_completed
			 */
			rb_encoder_gst_cancel (encoder);
	}

	return result;
}

static gboolean
rb_encoder_gst_get_preferred_mimetype (RBEncoder *encoder,
				       GList *mime_types,
				       char **mime,
				       char **extension)
{
	GList *l;

	g_return_val_if_fail (mime_types != NULL, FALSE);
	g_return_val_if_fail (mime != NULL, FALSE);
	g_return_val_if_fail (extension != NULL, FALSE);

	for (l = mime_types; l != NULL; l = g_list_next (l)) {
		GMAudioProfile *profile;
		const char *mimetype;

		mimetype = (const char *)l->data;
		profile = get_profile_from_mime_type (RB_ENCODER_GST (encoder), mimetype);
		if (profile) {
			*extension = g_strdup (gm_audio_profile_get_extension (profile));
			*mime = g_strdup (mimetype);
			g_object_unref (profile);
			return TRUE;
		}
	}

	return FALSE;
}
