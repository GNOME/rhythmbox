/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of metadata reading using GStreamer
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include <string.h>

#include <glib/gi18n.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include <gst/gsturi.h>
#include <gio/gio.h>
#include <gst/pbutils/pbutils.h>

#include "rb-metadata.h"
#include "rb-metadata-gst-common.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

G_DEFINE_TYPE(RBMetaData, rb_metadata, G_TYPE_OBJECT)

typedef GstElement *(*RBAddTaggerElem) (GstElement *pipeline, GstElement *source, GstTagList *tags);

static void rb_metadata_finalize (GObject *object);

struct RBMetaDataPrivate
{
	char *uri;

	GHashTable *metadata;

	GstElement *pipeline;
	GstElement *sink;
	gulong typefind_cb_id;
	GstTagList *tags;

	GHashTable *taggers;

	char *type;
	gboolean eos;
	gboolean has_audio;
	gboolean has_non_audio;
	gboolean has_video;
	GSList *missing_plugins;
	GError *error;
};

#define RB_METADATA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_METADATA, RBMetaDataPrivate))


static GstElement *
flac_tagger (GstElement *pipeline, GstElement *link_to, GstTagList *tags)
{
	GstElement *tagger = NULL;

	tagger = gst_element_factory_make ("flactag", NULL);
	if (tagger == NULL)
		return NULL;

	gst_bin_add (GST_BIN (pipeline), tagger);
	gst_element_link_many (link_to, tagger, NULL);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
	return tagger;
}

static void
id3_pad_added_cb (GstElement *demux, GstPad *pad, GstElement *mux)
{
	GstPad *mux_pad;

	mux_pad = gst_element_get_compatible_pad (mux, pad, NULL);
	if (gst_pad_link (pad, mux_pad) != GST_PAD_LINK_OK)
		rb_debug ("unable to link pad from id3demux to id3v2mux");
	else
		rb_debug ("linked pad from id3demux to id3v2mux");
}

static GstElement *
id3_tagger (GstElement *pipeline, GstElement *link_to, GstTagList *tags)
{
	GstElement *demux = NULL;
	GstElement *mux = NULL;

	/* TODO use new id3tag element here; not sure what name it'll end up with though */
	demux = gst_element_factory_make ("id3demux", NULL);
	mux = gst_element_factory_make ("id3v2mux", NULL);
	if (demux == NULL || mux == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (pipeline), demux, mux, NULL);
	if (!gst_element_link (link_to, demux))
		goto error;

	g_signal_connect (demux, "pad-added", (GCallback)id3_pad_added_cb, mux);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), tags, GST_TAG_MERGE_REPLACE_ALL);
	return mux;

error:
	g_object_unref (demux);
	g_object_unref (mux);
	return NULL;
}

static void
ogg_pad_added_cb (GstElement *demux, GstPad *pad, GstTagList *tags)
{
	GstCaps *caps;
	GstStructure *structure;
	const gchar *mimetype;
	GstPad *conn_pad = NULL;
	GstElement *mux;

	caps = gst_pad_get_caps (pad);
	structure = gst_caps_get_structure (caps, 0);
	mimetype = gst_structure_get_name (structure);

	mux = g_object_get_data (G_OBJECT (demux), "mux");

	if (strcmp (mimetype, "audio/x-vorbis") == 0) {
		GstElement *tagger, *parser;
		GstBin *bin;
		GstState state;

		rb_debug ("found vorbis stream in ogg container, using vorbistag");

		parser = gst_element_factory_make ("vorbisparse", NULL);
		if (parser == NULL) {
			rb_debug ("could not create vorbisparse element");
			goto end;
		}

		tagger = gst_element_factory_make ("vorbistag", NULL);
		if (tagger == NULL) {
			rb_debug ("could not create vorbistag element");
			gst_object_unref (parser);
			goto end;
		}

		bin = GST_BIN (gst_element_get_parent (mux));
		gst_bin_add_many (bin, tagger, parser, NULL);
		gst_object_unref (GST_OBJECT (bin));

		/* connect and bring them up to the same state */
		gst_element_link_many (tagger, parser, mux, NULL);
		gst_element_get_state (mux, &state, NULL, 0);
		gst_element_set_state (parser, state);
		gst_element_set_state (tagger, state);

		conn_pad = gst_element_get_compatible_pad (tagger, pad, NULL);
		gst_pad_link (pad, conn_pad);

		gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
	} else {
		conn_pad = gst_element_get_compatible_pad (mux, pad, NULL);
		gst_pad_link (pad, conn_pad);
		rb_debug ("found stream in ogg container with no known tagging element");
	}

end:
	gst_caps_unref (caps);
}

static GstElement *
vorbis_tagger (GstElement *pipeline, GstElement *link_to, GstTagList *tags)
{
	GstElement *demux = NULL;
	GstElement *mux = NULL;

	demux = gst_element_factory_make ("oggdemux", NULL);
	mux =  gst_element_factory_make ("oggmux", NULL);

	if (demux == NULL || mux == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (pipeline), demux, mux, NULL);
	if (!gst_element_link (link_to, demux))
		goto error;

	g_object_set_data (G_OBJECT (demux), "mux", mux);
	g_signal_connect (demux, "pad-added", (GCallback)ogg_pad_added_cb, tags);
	return mux;

error:
	g_object_unref (demux);
	g_object_unref (mux);
	return NULL;
}

static void
mp4_pad_added_cb (GstElement *demux, GstPad *demuxpad, GstPad *muxpad)
{
	if (gst_pad_link (demuxpad, muxpad) != GST_PAD_LINK_OK)
		rb_debug ("unable to link pad from qtdemux to mp4mux");
	else
		rb_debug ("linked pad from qtdemux to mp4mux");
}


static GstElement *
mp4_tagger (GstElement *pipeline, GstElement *link_to, GstTagList *tags)
{
	GstElement *demux;
	GstElement *mux;
	GstPad *muxpad;

	demux = gst_element_factory_make ("qtdemux", NULL);
	mux = gst_element_factory_make ("mp4mux", NULL);
	if (demux == NULL || mux == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (pipeline), demux, mux, NULL);
	if (!gst_element_link (link_to, demux))
		goto error;

	muxpad = gst_element_get_request_pad (mux, "audio_%d");
	g_signal_connect (demux, "pad-added", G_CALLBACK (mp4_pad_added_cb), muxpad);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), tags, GST_TAG_MERGE_REPLACE_ALL);

	return mux;

error:
	if (demux != NULL)
		g_object_unref (demux);
	if (mux != NULL)
		g_object_unref (mux);
	return NULL;
}



static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;

	g_type_class_add_private (klass, sizeof (RBMetaDataPrivate));
	rb_metadata_gst_register_transforms ();
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = RB_METADATA_GET_PRIVATE (md);

	md->priv->taggers = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

	if (gst_element_factory_find ("giostreamsink") == FALSE) {
		rb_debug ("giostreamsink not found, can't tag anything");
	} else {
		if (gst_element_factory_find ("vorbistag") &&
		    gst_element_factory_find ("vorbisparse") &&
		    gst_element_factory_find ("oggdemux") &&
		    gst_element_factory_find ("oggmux")) {
			rb_debug ("ogg vorbis tagging available");
			g_hash_table_insert (md->priv->taggers, "application/ogg", vorbis_tagger);
			g_hash_table_insert (md->priv->taggers, "audio/x-vorbis", vorbis_tagger);
		}

		if (gst_element_factory_find ("flactag")) {
			rb_debug ("flac tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/x-flac", flac_tagger);
		}

		/* TODO check for new id3 tag element too */
		if (gst_element_factory_find ("id3v2mux") && gst_element_factory_find ("id3demux")) {
			rb_debug ("id3 tagging available");
			g_hash_table_insert (md->priv->taggers, "application/x-id3", id3_tagger);
			g_hash_table_insert (md->priv->taggers, "audio/mpeg", id3_tagger);
		}

		if (gst_element_factory_find ("qtdemux") && gst_element_factory_find ("mp4mux")) {
			rb_debug ("mp4 tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/x-m4a", mp4_tagger);
			g_hash_table_insert (md->priv->taggers, "video/quicktime", mp4_tagger);
		}
	}
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);

	if (md->priv->pipeline)
		gst_object_unref (GST_OBJECT (md->priv->pipeline));

	if (md->priv->taggers)
		g_hash_table_destroy (md->priv->taggers);

	g_free (md->priv->type);
	g_free (md->priv->uri);
	g_clear_error (&md->priv->error);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL, NULL));
}

static void
rb_metadata_gst_load_tag (const GstTagList *list, const gchar *tag, RBMetaData *md)
{
	int count, tem, type;
	RBMetaDataField field;
	GValue *newval;
	const GValue *val;

	count = gst_tag_list_get_tag_size (list, tag);
	if (count < 1)
		return;

	tem = rb_metadata_gst_tag_to_field (tag);
	if (tem < 0) {
		rb_debug ("no metadata field for tag \"%s\"", tag);
		return;
	}
	field = (RBMetaDataField) tem;

	type = rb_metadata_get_field_type (field);
	val = gst_tag_list_get_value_index (list, tag, 0);
	newval = g_new0 (GValue, 1);
	g_value_init (newval, type);
	if (!g_value_transform (val, newval)) {

		rb_debug ("Could not transform tag value type %s into %s",
			  g_type_name (G_VALUE_TYPE (val)),
			  g_type_name (G_VALUE_TYPE (newval)));
		g_value_unset (newval);
		g_free (newval);
		return;
	}

	switch (type) {
	case G_TYPE_STRING: {
		/* Reject invalid utf-8 strings, shorter duplicated tags
		 * and then remove leading and trailing whitespace.
		 */
		char *str;

		str = g_value_dup_string (newval);

		if (!g_utf8_validate (str, -1, NULL)) {
			rb_debug ("Got invalid UTF-8 tag data");
			g_free (str);
			g_value_unset (newval);
			g_free (newval);
			return;
		}
		str = g_strstrip (str);

		/* Check whether we have a shorter duplicate tag,
		 * Doesn't work with non-normalised UTF-8 strings */
		val = g_hash_table_lookup (md->priv->metadata,
					   GINT_TO_POINTER (field));
		if (val != NULL) {
			const char *old_str;
			old_str = g_value_get_string (val);
			if (old_str != NULL
			    && g_utf8_strlen (old_str, -1) > g_utf8_strlen (str, -1)) {
				if (g_str_has_prefix (old_str, str) != FALSE) {
					rb_debug ("Got shorter duplicate tag");
					g_free (str);
					g_value_unset (newval);
					g_free (newval);
					return;
				}
			}
		}

		rb_debug ("processed string tag \"%s\": \"%s\"", tag, str);

		g_value_take_string (newval, str);
		break;
	}
	default:
		break;
	}

	switch (field) {
	case RB_METADATA_FIELD_BITRATE: {
		/* GStreamer sends us bitrate in bps, but we need it in kbps*/
		gulong bitrate;
		bitrate = g_value_get_ulong (newval);
		g_value_set_ulong (newval, bitrate/1000);
		rb_debug ("processed bitrate value: %lu", g_value_get_ulong (newval));
		break;
	}

	case RB_METADATA_FIELD_DURATION: {
		/* GStreamer sends us duration in ns,
		 * but we need it in seconds
		 */
		guint64 duration;
		duration = g_value_get_uint64 (val);
		g_value_set_ulong (newval, duration/(1000*1000*1000));
		rb_debug ("processed duration value: %lu", g_value_get_ulong (newval));
		break;
	}

	default:
		break;
	}

	g_hash_table_insert (md->priv->metadata,
			     GINT_TO_POINTER (field),
			     newval);
}

static void
rb_metadata_gst_typefind_cb (GstElement *typefind, guint probability, GstCaps *caps, RBMetaData *md)
{
	if (!(gst_caps_is_empty (caps) || gst_caps_is_any (caps))) {
		g_free (md->priv->type);
		md->priv->type = g_strdup (gst_structure_get_name (gst_caps_get_structure (caps, 0)));
		rb_debug ("found type %s", md->priv->type);
	}

	g_signal_handler_disconnect (typefind, md->priv->typefind_cb_id);
}

static void
rb_metadata_gst_new_decoded_pad_cb (GstElement *decodebin, GstPad *pad, gboolean last, RBMetaData *md)
{
	GstCaps *caps;
	GstStructure *structure;
	const gchar *mimetype;
	gboolean cancel = FALSE;

	caps = gst_pad_get_caps (pad);

	/* we get "ANY" caps for text/plain files etc. */
	if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
		rb_debug ("decoded pad with no caps or any caps.  this file is boring.");
		md->priv->has_non_audio = TRUE;
		cancel = TRUE;
	} else {
		GstPad *sink_pad;

		sink_pad = gst_element_get_static_pad (md->priv->sink, "sink");
		gst_pad_link (pad, sink_pad);
		gst_object_unref (sink_pad);

		/* is this pad audio? */
		structure = gst_caps_get_structure (caps, 0);
		mimetype = gst_structure_get_name (structure);

		if (g_str_has_prefix (mimetype, "audio/x-raw")) {
			rb_debug ("got decoded audio pad of type %s", mimetype);
			md->priv->has_audio = TRUE;
		} else if (g_str_has_prefix (mimetype, "video/")) {
			rb_debug ("got decoded video pad of type %s", mimetype);
			md->priv->has_video = TRUE;
		} else {
			rb_debug ("got decoded pad of non-audio type %s", mimetype);
			md->priv->has_non_audio = TRUE;
		}
	}

	gst_caps_unref (caps);

	/* If this is non-audio, cancel the operation.
	 * This seems to cause some deadlocks with video files, so only do it
	 * when we get no/any caps.
	 */
	if (cancel)
		gst_element_set_state (md->priv->pipeline, GST_STATE_NULL);
}

static GstElement *make_pipeline_element (GstElement *pipeline, const char *element, GError **error)
{
	GstElement *elem = gst_element_factory_make (element, element);
	if (elem == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_MISSING_PLUGIN,
			     _("Failed to create %s element; check your installation"),
			     element);
		return NULL;
	}

	gst_bin_add (GST_BIN (pipeline), elem);
	return elem;
}

static void
rb_metadata_handle_missing_plugin_message (RBMetaData *md, GstMessage *message)
{
	char *detail;

	detail = gst_missing_plugin_message_get_installer_detail (message);
	rb_debug ("got missing-plugin message from %s: %s",
		  GST_OBJECT_NAME (GST_MESSAGE_SRC (message)),
		  detail);
	g_free (detail);

	md->priv->missing_plugins = g_slist_prepend (md->priv->missing_plugins, gst_message_ref (message));

	/* update our information on what's in the stream based on
	 * what we're missing.
	 */
	switch (rb_metadata_gst_get_missing_plugin_type (message)) {
	case MEDIA_TYPE_NONE:
		break;
	case MEDIA_TYPE_CONTAINER:
		/* hm, maybe we need a way to say 'we don't even know what's in here'.
		 * but for now, the things we actually identify as containers are mostly
		 * used for audio, so pretending they actually are is good enough.
		 */
	case MEDIA_TYPE_AUDIO:
		md->priv->has_audio = TRUE;
		break;
	case MEDIA_TYPE_VIDEO:
		md->priv->has_video = TRUE;
		break;
	case MEDIA_TYPE_OTHER:
		md->priv->has_non_audio = TRUE;
		break;
	default:
		g_assert_not_reached ();
	}
}

static gboolean
rb_metadata_bus_handler (GstBus *bus, GstMessage *message, RBMetaData *md)
{
	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_EOS:
		rb_debug ("EOS reached");
		md->priv->eos = TRUE;
		return TRUE;

	case GST_MESSAGE_ERROR:
	{
		GError *gerror;
		gchar *debug;
		char *src;

		src = gst_element_get_name (GST_MESSAGE_SRC (message));
		rb_debug ("got error message from %s", src);
		g_free (src);

		gst_message_parse_error (message, &gerror, &debug);
		if (gerror->domain == GST_STREAM_ERROR &&
		    gerror->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
			rb_debug ("caught type not found error");
		} else if (gerror->domain == GST_STREAM_ERROR &&
			   gerror->code == GST_STREAM_ERROR_WRONG_TYPE &&
			   md->priv->type != NULL &&
			   strcmp (md->priv->type, "text/plain") == 0) {
			rb_debug ("got WRONG_TYPE error for text/plain: setting non-audio flag");
			md->priv->has_non_audio = TRUE;
		} else if (md->priv->error) {
			rb_debug ("caught error: %s, but we've already got one", gerror->message);
		} else {
			rb_debug ("caught error: %s ", gerror->message);

			g_clear_error (&md->priv->error);
			md->priv->error = g_error_new_literal (RB_METADATA_ERROR,
							       RB_METADATA_ERROR_GENERAL,
							       gerror->message);
		}

		/* treat this as equivalent to EOS */
		md->priv->eos = TRUE;

		g_error_free (gerror);
		g_free (debug);
		return TRUE;
	}
	case GST_MESSAGE_TAG:
	{
		GstTagList *tags;

		gst_message_parse_tag (message, &tags);
		if (tags) {
			gst_tag_list_foreach (tags, (GstTagForeachFunc) rb_metadata_gst_load_tag, md);
			gst_tag_list_free (tags);
		} else {
			const gchar *errmsg = "Could not retrieve tag list";

			rb_debug ("caught error: %s ", errmsg);
			md->priv->error = g_error_new_literal (RB_METADATA_ERROR,
							       RB_METADATA_ERROR_GENERAL,
							       errmsg);
		}
		break;
	}
	case GST_MESSAGE_ELEMENT:
	{
		if (gst_is_missing_plugin_message (message)) {
			rb_metadata_handle_missing_plugin_message (md, message);
		}
		break;
	}
	default:
	{
		char *src;

		src = gst_element_get_name (GST_MESSAGE_SRC (message));
		rb_debug ("message of type %s from %s",
			  GST_MESSAGE_TYPE_NAME (message), src);
		g_free (src);
		break;
	}
	}

	return FALSE;
}

static void
rb_metadata_event_loop (RBMetaData *md, GstElement *element, gboolean block)
{
	GstBus *bus;
	gboolean done = FALSE;

	bus = gst_element_get_bus (element);
	g_return_if_fail (bus != NULL);

	while (!done && !md->priv->eos) {
		GstMessage *message;

		if (block)
			message = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
		else
			message = gst_bus_pop (bus);

		if (message == NULL) {
			gst_object_unref (bus);
			return;
		}

		done = rb_metadata_bus_handler (bus, message, md);
		gst_message_unref (message);
	}
	gst_object_unref (bus);
}

void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *urisrc = NULL;
	GstElement *decodebin = NULL;
	GstElement *typefind = NULL;
	gint64 file_size = -1;
	GstFormat file_size_format = GST_FORMAT_BYTES;
	GstStateChangeReturn state_ret;
	int change_timeout;
	GstBus *bus;

	g_free (md->priv->uri);
	md->priv->uri = NULL;
	g_free (md->priv->type);
	md->priv->type = NULL;
	md->priv->error = NULL;
	md->priv->eos = FALSE;
	md->priv->has_audio = FALSE;
	md->priv->has_non_audio = FALSE;
	md->priv->has_video = FALSE;
	md->priv->missing_plugins = NULL;

	if (md->priv->pipeline) {
		gst_object_unref (GST_OBJECT (md->priv->pipeline));
		md->priv->pipeline = NULL;
	}

	if (uri == NULL)
		return;

	rb_debug ("loading metadata for uri: %s", uri);
	md->priv->uri = g_strdup (uri);

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						    NULL, (GDestroyNotify) rb_value_free);

	/* The main tagfinding pipeline looks like this:
 	 * <src> ! decodebin ! fakesink
 	 *
 	 * but we can only link the fakesink in when the decodebin
 	 * creates an audio source pad.  we do this in the 'new-decoded-pad'
 	 * signal handler.
 	 */
	pipeline = gst_pipeline_new ("pipeline");

	urisrc = gst_element_make_from_uri (GST_URI_SRC, uri, "urisrc");
	if (urisrc == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_MISSING_PLUGIN,
			     _("Failed to create a source element; check your installation"));
		rb_debug ("missing an element to load the uri, sadly");
		goto out;
	}
	gst_bin_add (GST_BIN (pipeline), urisrc);

 	decodebin = make_pipeline_element (pipeline, "decodebin", error);
 	md->priv->sink = make_pipeline_element (pipeline, "fakesink", error);
 	if (!(urisrc && decodebin && md->priv->sink)) {
 		rb_debug ("missing an element, sadly");
 		goto out;
 	}

 	g_signal_connect_object (decodebin, "new-decoded-pad", G_CALLBACK (rb_metadata_gst_new_decoded_pad_cb), md, 0);

 	/* locate the decodebin's typefind, so we can get the have_type signal too.
 	 * this is kind of nasty, since it relies on an essentially arbitrary string
 	 * in the decodebin code not changing.  the alternative is to have our own
 	 * typefind instance before the decodebin.  it might not like that.
 	 */
 	typefind = gst_bin_get_by_name (GST_BIN (decodebin), "typefind");
 	g_assert (typefind != NULL);
	md->priv->typefind_cb_id = g_signal_connect_object (typefind,
							    "have_type",
							    G_CALLBACK (rb_metadata_gst_typefind_cb),
							    md,
							    0);
	gst_object_unref (GST_OBJECT (typefind));

 	gst_element_link (urisrc, decodebin);

	md->priv->pipeline = pipeline;
	rb_debug ("going to PAUSED for metadata, uri: %s", uri);
	state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
	bus = gst_element_get_bus (GST_ELEMENT (pipeline));
	change_timeout = 0;
	while (state_ret == GST_STATE_CHANGE_ASYNC &&
	       !md->priv->eos &&
	       change_timeout < 5) {
		GstMessage *msg;

		msg = gst_bus_timed_pop (bus, 1 * GST_SECOND);
		if (msg) {
			rb_metadata_bus_handler (bus, msg, md);
			gst_message_unref (msg);
			change_timeout = 0;
		} else {
			change_timeout++;
		}

		state_ret = gst_element_get_state (pipeline, NULL, NULL, 0);
	}
	gst_object_unref (GST_OBJECT (bus));

	rb_metadata_event_loop (md, GST_ELEMENT (pipeline), FALSE);

	if (state_ret != GST_STATE_CHANGE_SUCCESS) {
		rb_debug ("failed to go to PAUSED for %s", uri);
		if (!md->priv->has_non_audio && md->priv->error == NULL)
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("GStreamer error: failed to change state"));
	} else
		rb_debug ("gone to PAUSED for %s", uri);

	if (state_ret == GST_STATE_CHANGE_SUCCESS) {

		/* The pipeline went to PAUSED,
		 * which means the decoder should have read all
		 * of the metadata, and should know the length now.
		 */
		if (g_hash_table_lookup (md->priv->metadata, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION)) == NULL) {
			GstFormat format = GST_FORMAT_TIME;
			gint64 length;
			GValue *newval;

			if (gst_element_query_duration (md->priv->sink, &format, &length)) {
				g_assert (format == GST_FORMAT_TIME);
				newval = g_new0 (GValue, 1);

				rb_debug ("duration query succeeded");

				g_value_init (newval, G_TYPE_ULONG);
				/* FIXME - use guint64 for duration? */
				g_value_set_ulong (newval, (long) (length / GST_SECOND));
				g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION),
						     newval);
			} else {
				rb_debug ("duration query failed!");
			}
		}
	}

	/* Get the file size, since it might be interesting, and return
	 * the pipeline to NULL state.
	 */
	if (gst_element_query_duration (urisrc, &file_size_format, &file_size))
		g_assert (file_size_format == GST_FORMAT_BYTES);

	state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
	if (state_ret == GST_STATE_CHANGE_ASYNC) {
		g_warning ("Failed to return metadata reader to NULL state");
	}

	if (md->priv->error != NULL) {
		g_propagate_error (error, md->priv->error);
		md->priv->error = NULL;
	} else if (!md->priv->type) {
		g_clear_error (error);
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNRECOGNIZED,
			     _("The MIME type of the file could not be identified"));
	} else {
		/* yay, it worked */
		rb_debug ("successfully read metadata for %s", uri);
	}

 out:
	if (pipeline != NULL)
		gst_object_unref (GST_OBJECT (pipeline));
	md->priv->pipeline = NULL;
}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
	return g_hash_table_lookup (md->priv->taggers, mimetype) != NULL;
}

char **
rb_metadata_get_saveable_types (RBMetaData *md)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	char **types;
	int i;

	types = g_new0 (char *, g_hash_table_size (md->priv->taggers) + 1);
	i = 0;
	g_hash_table_iter_init (&iter, md->priv->taggers);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		types[i++] = g_strdup ((const char *) key);
	}

	return types;
}

static void
rb_metadata_gst_add_tag_data (gpointer key, const GValue *val, RBMetaData *md)
{
	RBMetaDataField field = GPOINTER_TO_INT (key);
	const char *tag = rb_metadata_gst_field_to_gst_tag (field);

	/* don't write this out */
	if (field == RB_METADATA_FIELD_DURATION)
		return;

	if (tag) {
		if (field == RB_METADATA_FIELD_DATE && g_value_get_ulong (val) == 0) {
			/* we should ask gstreamer to remove the tag,
			 * but there is no easy way of doing so
			 */
		} else {
			GValue newval = {0,};

			g_value_init (&newval, gst_tag_get_type (tag));
			if (g_value_transform (val, &newval)) {
				rb_debug("Setting %s",tag);

				gst_tag_list_add_values (md->priv->tags,
							 GST_TAG_MERGE_APPEND,
							 tag, &newval,
							 NULL);
			}
			g_value_unset (&newval);
		}
	}
}

static gboolean
rb_metadata_file_valid (char *original, char *newfile)
{
	RBMetaData *md = rb_metadata_new ();
	GError *error = NULL;
	gboolean ret;

	rb_metadata_load (md, newfile, &error);
	ret = (error == NULL);

	/* TODO: check that the tags are correct? */

	if (error != NULL)
		g_error_free (error);
	g_object_unref (G_OBJECT (md));
	return ret;
}

void
rb_metadata_save (RBMetaData *md, GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *source = NULL;
        GstElement *retag_end = NULL; /* the last element after retagging subpipeline */
	const char *plugin_name = NULL;
	char *tmpname_prefix = NULL;
	char *tmpname = NULL;
	GOutputStream *stream = NULL;
	GError *io_error = NULL;
	RBAddTaggerElem add_tagger_func;

	g_return_if_fail (md->priv->uri != NULL);
	g_return_if_fail (md->priv->type != NULL);

	rb_debug ("saving metadata for uri: %s", md->priv->uri);

	tmpname_prefix = rb_uri_make_hidden (md->priv->uri);
	rb_debug ("temporary file name prefix: %s", tmpname_prefix);

	rb_uri_mkstemp (tmpname_prefix, &tmpname, &stream, &io_error);
	g_free (tmpname_prefix);
	if (io_error != NULL) {
		goto gio_error;
	}

	pipeline = gst_pipeline_new ("pipeline");
	md->priv->pipeline = pipeline;

	/* Source */
	source = gst_element_make_from_uri (GST_URI_SRC, md->priv->uri, "urisrc");
	if (source == NULL) {
		plugin_name = "urisrc";
		goto missing_plugin;	
	}
	gst_bin_add (GST_BIN (pipeline), source);

	/* Sink */
	plugin_name = "giostreamsink";
	if (!(md->priv->sink = gst_element_factory_make (plugin_name, plugin_name)))
		goto missing_plugin;

	g_object_set (G_OBJECT (md->priv->sink), "stream", stream, NULL);

	md->priv->tags = gst_tag_list_new ();
	g_hash_table_foreach (md->priv->metadata,
			      (GHFunc) rb_metadata_gst_add_tag_data,
			      md);

	/* Tagger element(s) */
	add_tagger_func = g_hash_table_lookup (md->priv->taggers, md->priv->type);
	if (!add_tagger_func) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNSUPPORTED,
			     _("Unsupported file type: %s"), md->priv->type);
		goto out_error;
	}

	retag_end = add_tagger_func (md->priv->pipeline, source, md->priv->tags);
	if (!retag_end) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNSUPPORTED,
			     _("Unable to create tag-writing elements"));
		goto out_error;
	}

	gst_bin_add (GST_BIN (pipeline), md->priv->sink);
	gst_element_link_many (retag_end, md->priv->sink, NULL);

	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	rb_metadata_event_loop (md, GST_ELEMENT (pipeline), TRUE);
	if (gst_element_set_state (pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_ASYNC) {
		if (gst_element_get_state (pipeline, NULL, NULL, 3 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("Timeout while setting pipeline to NULL"));
			goto out_error;
		}
	}

	if (md->priv->error) {
		g_propagate_error (error, md->priv->error);
		goto out_error;
	}
	if (stream != NULL) {
		GFile *src;
		GFile *dest;

		if (g_output_stream_close (stream, NULL, &io_error) == FALSE) {
			goto gio_error;
		}
		g_object_unref (stream);
		stream = NULL;

		/* check to ensure the file isn't corrupt */
		if (!rb_metadata_file_valid (md->priv->uri, tmpname)) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("File corrupted during write"));
			goto out_error;
		}

		src = g_file_new_for_uri (tmpname);
		dest = g_file_new_for_uri (md->priv->uri);
		g_file_move (src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &io_error);
		if (io_error != NULL) {
			goto gio_error;
		}
	}

	goto out;
gio_error:
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_IO,
		     "%s",
		     io_error->message);
	goto out_error;
missing_plugin:
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_MISSING_PLUGIN,
		     _("Failed to create %s element; check your installation"),
		     plugin_name);
out_error:
	if (stream != NULL) {
		g_output_stream_close (stream, NULL, NULL);
		g_object_unref (stream);
	}

	if (tmpname != NULL) {
		GFile *del;
		del = g_file_new_for_uri (tmpname);
		g_file_delete (del, NULL, NULL);
		g_object_unref (del);
	}

out:
	if (md->priv->tags)
		gst_tag_list_free (md->priv->tags);
	md->priv->tags = NULL;

	if (pipeline != NULL)
		gst_object_unref (GST_OBJECT (pipeline));
	md->priv->pipeline = NULL;
}

gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field,
		 GValue *ret)
{
	GValue *val;
	if ((val = g_hash_table_lookup (md->priv->metadata,
					GINT_TO_POINTER (field)))) {
		g_value_init (ret, G_VALUE_TYPE (val));
		g_value_copy (val, ret);
		return TRUE;
	}
	return FALSE;
}

const char *
rb_metadata_get_mime (RBMetaData *md)
{
	return md->priv->type;
}

gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field,
		 const GValue *val)
{
	GValue *newval;
	GType type;

	if (rb_metadata_gst_field_to_gst_tag (field) == NULL) {
		return FALSE;
	}

	type = rb_metadata_get_field_type (field);
	g_return_val_if_fail (type == G_VALUE_TYPE (val), FALSE);

	newval = g_new0 (GValue, 1);
	g_value_init (newval, type);
	g_value_copy (val, newval);

	g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (field),
			     newval);
	return TRUE;
}

gboolean
rb_metadata_has_missing_plugins (RBMetaData *md)
{
	return (g_slist_length (md->priv->missing_plugins) > 0);
}

gboolean
rb_metadata_get_missing_plugins (RBMetaData *md,
				 char ***missing_plugins,
				 char ***plugin_descriptions)
{
	char **mp;
	char **pd;
	int count;
	int i;
	GSList *t;

	count = g_slist_length (md->priv->missing_plugins);
	if (count == 0) {
		return FALSE;
	}

	mp = g_new0 (char *, count + 1);
	pd = g_new0 (char *, count + 1);
	i = 0;
	for (t = md->priv->missing_plugins; t != NULL; t = t->next) {
		GstMessage *msg = GST_MESSAGE (t->data);
		char *detail;
		char *description;

		detail = gst_missing_plugin_message_get_installer_detail (msg);
		description = gst_missing_plugin_message_get_description (msg);
		rb_debug ("adding [%s,%s] to return data", detail, description);
		mp[i] = g_strdup (detail);
		pd[i] = g_strdup (description);
		i++;

		gst_message_unref (msg);
	}
	g_slist_free (md->priv->missing_plugins);
	md->priv->missing_plugins = NULL;

	*missing_plugins = mp;
	*plugin_descriptions = pd;
	return TRUE;
}

gboolean
rb_metadata_has_audio (RBMetaData *md)
{
	return md->priv->has_audio;
}

gboolean
rb_metadata_has_video (RBMetaData *md)
{
	return md->priv->has_video;
}

gboolean
rb_metadata_has_other_data (RBMetaData *md)
{
	return md->priv->has_non_audio;		/* kinda */
}

