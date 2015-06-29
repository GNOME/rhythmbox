/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#include <stdio.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <gst/pbutils/pbutils.h>

#include "rb-metadata.h"
#include "rb-metadata-gst-common.h"
#include "rb-gst-media-types.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

/* copied from gstplay-enum.h */
typedef enum {
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP
} GstAutoplugSelectResult;

typedef GstElement *(*RBAddTaggerElem) (GstElement *pipeline, GstPad *srcpad, GstTagList *tags);

G_DEFINE_TYPE(RBMetaData, rb_metadata, G_TYPE_OBJECT)

struct RBMetaDataPrivate
{
	GstDiscovererInfo *info;

	char *mediatype;
	gboolean has_audio;
	gboolean has_non_audio;
	gboolean has_video;
	guint audio_bitrate;
	GstCaps *jpeg_image_caps;

	/* writing */
	GstElement *pipeline;
	GstElement *sink;
	GHashTable *taggers;
	GstTagList *tags;
	gboolean sink_linked;
};

void
rb_metadata_reset (RBMetaData *md)
{
	if (md->priv->tags != NULL) {
		gst_tag_list_free (md->priv->tags);
		md->priv->tags = NULL;
	}

	if (md->priv->info != NULL) {
		gst_discoverer_info_unref (md->priv->info);
		md->priv->info = NULL;
	}
	g_free (md->priv->mediatype);
	md->priv->mediatype = NULL;

	md->priv->has_audio = FALSE;
	md->priv->has_non_audio = FALSE;
	md->priv->has_video = FALSE;
}

static void
have_type_cb (GstElement *element, guint probability, GstCaps *caps, RBMetaData *md)
{
	md->priv->mediatype = rb_gst_caps_to_media_type (caps);
	rb_debug ("got type %s", md->priv->mediatype);
}

static void
run_typefind (RBMetaData *md, const char *uri)
{
	GstElement *src;

	src = gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
	if (src != NULL) {
		GstElement *pipeline = gst_pipeline_new (NULL);
		GstElement *sink = gst_element_factory_make ("fakesink", NULL);
		GstElement *typefind = gst_element_factory_make ("typefind", NULL);

		gst_bin_add_many (GST_BIN (pipeline), src, typefind, sink, NULL);
		if (gst_element_link_many (src, typefind, sink, NULL)) {
			GstBus *bus;
			GstMessage *message;
			gboolean done;

			g_signal_connect (typefind, "have-type", G_CALLBACK (have_type_cb), md);

			bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
			gst_element_set_state (pipeline, GST_STATE_PAUSED);
			done = FALSE;

			while (done == FALSE && md->priv->mediatype == NULL) {
				message = gst_bus_timed_pop (bus, 5 * GST_SECOND);
				if (message == NULL) {
					rb_debug ("typefind pass timed out");
					break;
				}

				switch (GST_MESSAGE_TYPE (message)) {
				case GST_MESSAGE_ERROR:
					rb_debug ("typefind pass got an error");
					done = TRUE;
					break;

				case GST_MESSAGE_STATE_CHANGED:
					if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
						GstState old, new, pending;
						gst_message_parse_state_changed (message, &old, &new, &pending);
						if (new == GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING) {
							rb_debug ("typefind pipeline reached PAUSED");
							done = TRUE;
						}
					}
					break;

				default:
					break;
				}

				gst_message_unref (message);
			}

			g_object_unref (bus);
			gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
		}

		g_object_unref (pipeline);
	}
}

void
rb_metadata_load (RBMetaData *md, const char *uri, GError **error)
{
	GList *streams;
	GList *l;
	GstDiscoverer *discoverer;
	GstCaps *caps;
	GError *gsterror = NULL;

	rb_metadata_reset (md);

	discoverer = gst_discoverer_new (30 * GST_SECOND, error);
	if (*error != NULL)
		return;

	md->priv->info = gst_discoverer_discover_uri (discoverer, g_strdup (uri), &gsterror);
	g_object_unref (discoverer);

	/* figure out if we've got audio, non-audio, or video streams */
	streams = gst_discoverer_info_get_streams (md->priv->info, GST_TYPE_DISCOVERER_STREAM_INFO);
	for (l = streams; l != NULL; l = l->next) {
		GstDiscovererStreamInfo *s = (GstDiscovererStreamInfo *)l->data;
		const char *mediatype;
		caps = gst_discoverer_stream_info_get_caps (s);
		mediatype = gst_structure_get_name (gst_caps_get_structure (caps, 0));

		if (GST_IS_DISCOVERER_AUDIO_INFO (s)) {
			md->priv->has_audio = TRUE;

			md->priv->audio_bitrate = gst_discoverer_audio_info_get_bitrate (GST_DISCOVERER_AUDIO_INFO (s));

			g_free (md->priv->mediatype);
			md->priv->mediatype = rb_gst_caps_to_media_type (caps);
			rb_debug ("found audio stream, media type %s", md->priv->mediatype);
		} else if (GST_IS_DISCOVERER_CONTAINER_INFO (s)) {
			if (md->priv->mediatype == NULL) {
				md->priv->mediatype = g_strdup (mediatype);
				rb_debug ("found container, media type %s", md->priv->mediatype);
			} else {
				rb_debug ("found container, ignoring media type");
			}
		} else if (g_strcmp0 (mediatype, "application/x-id3") == 0 ||
			   g_strcmp0 (mediatype, "application/x-apetag") == 0) {
			rb_debug ("found tag type, ignoring");
		} else {
			if (GST_IS_DISCOVERER_VIDEO_INFO (s)) {
				/* pretend low-framerate jpeg isn't video */
				if (gst_caps_can_intersect (caps, md->priv->jpeg_image_caps)) {
					rb_debug ("found a jpeg image stream, not actual video");
				} else {
					md->priv->has_video = TRUE;
				}
			}
			md->priv->has_non_audio = TRUE;
			if (md->priv->mediatype == NULL) {
				md->priv->mediatype = g_strdup (mediatype);
				rb_debug ("found video/image/other stream, media type %s", md->priv->mediatype);
			} else {
				rb_debug ("found video/image/other stream, ignoring media type (%s)", mediatype);
			}
			rb_debug ("video caps: %s", gst_caps_to_string (caps));
		}

		gst_caps_unref (caps);
	}
	gst_discoverer_stream_info_list_free (streams);

	/* if we don't have a media type, use typefind to get one */
	if (md->priv->mediatype == NULL) {
		run_typefind (md, uri);
	}

	/* look at missing plugin information too */
	switch (rb_gst_get_missing_plugin_type (gst_discoverer_info_get_misc (md->priv->info))) {
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

	if (gsterror != NULL) {
		int code = RB_METADATA_ERROR_GENERAL;
		if (g_error_matches (gsterror, GST_STREAM_ERROR, GST_STREAM_ERROR_TYPE_NOT_FOUND)) {
			code = RB_METADATA_ERROR_UNRECOGNIZED;
		}
		*error = g_error_new_literal (RB_METADATA_ERROR, code, gsterror->message);
		g_clear_error (&gsterror);
	}
}

gboolean
rb_metadata_has_missing_plugins (RBMetaData *md)
{
	const GstStructure *s;
	if (md->priv->info == NULL) {
		return FALSE;
	}

	s = gst_discoverer_info_get_misc (md->priv->info);
	return (rb_gst_get_missing_plugin_type (s) != MEDIA_TYPE_NONE);
}

gboolean
rb_metadata_get_missing_plugins (RBMetaData *md, char ***missing_plugins, char ***plugin_descriptions)
{
	char **mp;
	char **pd;
	GstMessage *msg;
	const GstStructure *misc;

	if (rb_metadata_has_missing_plugins (md) == FALSE) {
		return FALSE;
	}

	mp = g_new0 (char *, 2);
	pd = g_new0 (char *, 2);

	/* wrap the structure in a new message so we can use the
	 * pbutils functions to look at it.
	 */
	misc = gst_discoverer_info_get_misc (md->priv->info);
	msg = gst_message_new_element (NULL, gst_structure_copy (misc));
	mp[0] = gst_missing_plugin_message_get_installer_detail (msg);
	pd[0] = gst_missing_plugin_message_get_description (msg);
	gst_message_unref (msg);

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
	return md->priv->has_non_audio;
}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mediatype)
{
	return FALSE;
}

char **
rb_metadata_get_saveable_types (RBMetaData *md)
{
	char **types;
	GList *taggers;
	GList *t;
	int i;

	taggers = g_hash_table_get_keys (md->priv->taggers);
	types = g_new0 (char *, g_list_length (taggers) + 1);
	i = 0;
	for (t = taggers; t != NULL; t = t->next) {
		types[i++] = g_strdup (t->data);
	}

	g_list_free (taggers);
	return types;
}

static gboolean
link_named_pad (GstPad *srcpad, GstElement *element, const char *sinkpadname)
{
	GstPad *sinkpad;
	GstPadLinkReturn result;

	sinkpad = gst_element_get_static_pad (element, sinkpadname);
	if (sinkpad == NULL) {
		sinkpad = gst_element_get_request_pad (element, sinkpadname);
	}
	result = gst_pad_link (srcpad, sinkpad);
	gst_object_unref (sinkpad);

	if (GST_PAD_LINK_SUCCESSFUL (result)) {
		return TRUE;
	} else {
		char *srcname = gst_pad_get_name (srcpad);
		char *sinkname = gst_pad_get_name (sinkpad);
		rb_debug ("couldn't link %s to %s: %d",
			  srcname, sinkname, result);
		return FALSE;
	}
}

static GstElement *
flac_tagger (GstElement *pipeline, GstPad *srcpad, GstTagList *tags)
{
	GstElement *tagger = NULL;

	tagger = gst_element_factory_make ("flactag", NULL);
	if (tagger == NULL)
		return NULL;

	gst_bin_add (GST_BIN (pipeline), tagger);
	if (!link_named_pad (srcpad, tagger, "sink"))
		goto error;

	gst_element_set_state (tagger, GST_STATE_PAUSED);
	if (tags != NULL) {
		gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
	}
	return tagger;

error:
	gst_object_unref (tagger);
	return NULL;
}

static GstElement *
mp3_tagger (GstElement *pipeline, GstPad *srcpad, GstTagList *tags)
{
	GstElement *mux = NULL;

	/* try id3mux first, since it's more supported and writes id3v2.3 tags rather than v2.4.  */
	mux = gst_element_factory_make ("id3mux", NULL);
	if (mux == NULL)
		mux = gst_element_factory_make ("id3v2mux", NULL);

	if (mux == NULL)
		goto error;

	gst_bin_add (GST_BIN (pipeline), mux);
	if (!link_named_pad (srcpad, mux, "sink")) {
		rb_debug ("couldn't link decoded pad to id3 muxer");
		goto error;
	}

	gst_element_set_state (mux, GST_STATE_PAUSED);
	if (tags != NULL) {
		gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), tags, GST_TAG_MERGE_REPLACE_ALL);
	}
	rb_debug ("id3 tagger created");
	return mux;

error:
	if (mux != NULL) {
		g_object_unref (mux);
	}
	return NULL;
}

static GstElement *
vorbis_tagger (GstElement *pipeline, GstPad *srcpad, GstTagList *tags)
{
	GstElement *mux;
	GstElement *tagger;
	GstElement *parser;

	mux = gst_element_factory_make ("oggmux", NULL);
	parser = gst_element_factory_make ("vorbisparse", NULL);
	tagger = gst_element_factory_make ("vorbistag", NULL);
	if (mux == NULL || parser == NULL || tagger == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (pipeline), parser, tagger, mux, NULL);
	if (!link_named_pad (srcpad, parser, "sink"))
		goto error;
	if (!gst_element_link_many (parser, tagger, mux, NULL))
		goto error;

	gst_element_set_state (parser, GST_STATE_PAUSED);
	gst_element_set_state (tagger, GST_STATE_PAUSED);
	gst_element_set_state (mux, GST_STATE_PAUSED);
	if (tags != NULL) {
		gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
	}
	return mux;

error:
	if (parser != NULL)
		g_object_unref (parser);
	if (tagger != NULL)
		g_object_unref (tagger);
	if (mux != NULL)
		g_object_unref (mux);
	return NULL;
}

static GstElement *
mp4_tagger (GstElement *pipeline, GstPad *srcpad, GstTagList *tags)
{
	GstElement *mux;

	mux = gst_element_factory_make ("mp4mux", NULL);
	if (mux == NULL)
		return NULL;

	gst_bin_add (GST_BIN (pipeline), mux);
	if (!link_named_pad (srcpad, mux, "audio_%u"))
		goto error;

	gst_element_set_state (mux, GST_STATE_PAUSED);
	if (tags != NULL) {
		gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), tags, GST_TAG_MERGE_REPLACE_ALL);
	}
	return mux;

error:
	g_object_unref (mux);
	return NULL;
}

static void
metadata_save_pad_added_cb (GstElement *decodebin, GstPad *pad, RBMetaData *md)
{
	RBAddTaggerElem add_tagger_func = NULL;
	GstElement *retag_end;
	GstCaps *caps;
	char *caps_str;
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	if (md->priv->sink_linked) {
		GError *error;
		error = g_error_new (GST_STREAM_ERROR,
				     GST_STREAM_ERROR_FORMAT,
				     _("Unable to write tags to this file as it contains multiple streams"));
		gst_element_post_message (decodebin,
					  gst_message_new_error (GST_OBJECT (decodebin),
								 error,
								 NULL));
		g_error_free (error);
		return;
	}

	/* find a tagger function that accepts the caps */
	caps = gst_pad_query_caps (pad, NULL);
	caps_str = gst_caps_to_string (caps);
	rb_debug ("finding tagger for src caps %s", caps_str);
	g_free (caps_str);

	g_hash_table_iter_init (&iter, md->priv->taggers);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GstCaps *tagger_caps;
		const char *media_type = (const char *)key;

		tagger_caps = rb_gst_media_type_to_caps (media_type);
		/* not sure this is right, really */
		if (gst_caps_is_always_compatible (caps, tagger_caps)) {
			caps_str = gst_caps_to_string (tagger_caps);
			rb_debug ("matched sink caps %s", caps_str);
			g_free (caps_str);

			gst_caps_unref (tagger_caps);
			add_tagger_func = (RBAddTaggerElem)value;
			break;
		}
		gst_caps_unref (tagger_caps);
	}
	gst_caps_unref (caps);

	/* add retagging element(s) */
	if (add_tagger_func == NULL) {
		GError *error;
		error = g_error_new (GST_STREAM_ERROR,
				     GST_STREAM_ERROR_FORMAT,
				     _("Unable to write tags to this file as it is not encoded in a supported format"));
		gst_element_post_message (decodebin,
					  gst_message_new_error (GST_OBJECT (decodebin),
								 error,
								 NULL));
		g_error_free (error);
		return;
	}
	retag_end = add_tagger_func (md->priv->pipeline, pad, md->priv->tags);

	/* link to the sink */
	gst_element_link (retag_end, md->priv->sink);
	md->priv->sink_linked = TRUE;
}

static gboolean
factory_src_caps_intersect (GstElementFactory *factory, GstCaps *caps)
{
	const GList *templates;
	const GList *l;

	templates = gst_element_factory_get_static_pad_templates (factory);
	for (l = templates; l != NULL; l = l->next) {
		GstStaticPadTemplate *t = l->data;
		GstCaps *tcaps;

		if (t->direction != GST_PAD_SRC) {
			continue;
		}

		tcaps = gst_static_pad_template_get_caps (t);
		if (gst_caps_can_intersect (tcaps, caps)) {
			gst_caps_unref (tcaps);
			return TRUE;
		}
		gst_caps_unref (tcaps);
	}
	return FALSE;
}

static GstAutoplugSelectResult
metadata_save_autoplug_select_cb (GstElement *decodebin, GstPad *pad, GstCaps *caps, GstElementFactory *factory, RBMetaData *md)
{
	GstCaps *src_caps;
	gboolean is_any;
	gboolean is_raw;
	gboolean is_demuxer;

	is_demuxer = (strstr (gst_element_factory_get_klass (factory), "Demuxer") != NULL);
	if (is_demuxer) {
		/* allow demuxers, since we're going to remux later */
		return GST_AUTOPLUG_SELECT_TRY;
	}

	src_caps = gst_caps_new_any ();
	is_any = gst_element_factory_can_src_all_caps (factory, src_caps);	/* or _any_caps? */
	gst_caps_unref (src_caps);
	if (is_any) {
		/* this is something like id3demux (though that will match the
		 * above check), allow it so we can get to the actual decoder later
		 */
		return GST_AUTOPLUG_SELECT_TRY;
	}

	src_caps = gst_caps_from_string ("audio/x-raw; video/x-raw");
	is_raw = factory_src_caps_intersect (factory, src_caps);
	gst_caps_unref (src_caps);

	if (is_raw == FALSE) {
		/* this is probably a parser or something, allow it */
		return GST_AUTOPLUG_SELECT_TRY;
	}

	/* don't allow decoders */
	return GST_AUTOPLUG_SELECT_EXPOSE;
}

static gboolean
check_file_valid (const char *original, const char *newfile)
{
	RBMetaData *md = rb_metadata_new ();
	GError *error = NULL;
	gboolean ret;

	rb_metadata_load (md, newfile, &error);
	ret = (error == NULL);

	/* TODO: check that the tags are correct? */

	if (error != NULL)
		g_error_free (error);
	g_object_unref (md);
	return ret;
}


void
rb_metadata_save (RBMetaData *md, const char *uri, GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *urisrc = NULL;
	GstElement *decodebin = NULL;
	char *tmpname_prefix = NULL;
	char *tmpname = NULL;
	GOutputStream *stream = NULL;
	GError *io_error = NULL;
	GstBus *bus;
	gboolean done;
	GFile *src;
	GFile *dest;

	rb_debug ("saving metadata for uri: %s", uri);

	tmpname_prefix = rb_uri_make_hidden (uri);
	rb_debug ("temporary file name prefix: %s", tmpname_prefix);

	rb_uri_mkstemp (tmpname_prefix, &tmpname, &stream, &io_error);
	g_free (tmpname_prefix);
	if (io_error != NULL) {
		goto gio_error;
	}

	/* set up pipeline */
	pipeline = gst_pipeline_new ("pipeline");
	md->priv->pipeline = pipeline;
	md->priv->sink_linked = FALSE;

	urisrc = gst_element_make_from_uri (GST_URI_SRC, uri, "urisrc", NULL);
	if (urisrc == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_MISSING_PLUGIN,
			     _("Failed to create a source element; check your installation"));
		rb_debug ("missing an element to load the uri, sadly");
		goto out;
	}

	decodebin = gst_element_factory_make ("decodebin", "decoder");
	if (decodebin == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_MISSING_PLUGIN,
			     _("Failed to create the 'decodebin' element; check your GStreamer installation"));
		goto out;
	}

	md->priv->sink = gst_element_factory_make ("giostreamsink", "sink");
	if (md->priv->sink == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_MISSING_PLUGIN,
			     _("Failed to create the 'giostreamsink' element; check your GStreamer installation"));
		goto out;
	}
	g_object_set (md->priv->sink, "stream", stream, NULL);

	gst_bin_add_many (GST_BIN (pipeline), urisrc, decodebin, md->priv->sink, NULL);
	gst_element_link (urisrc, decodebin);

	g_signal_connect_object (decodebin,
				 "pad-added",
				 G_CALLBACK (metadata_save_pad_added_cb),
				 md, 0);
	g_signal_connect_object (decodebin,
				 "autoplug-select",
				 G_CALLBACK (metadata_save_autoplug_select_cb),
				 md, 0);

	/* run pipeline .. */
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	bus = gst_element_get_bus (pipeline);
	done = FALSE;
	while (done == FALSE) {
		GstMessage *message;

		message = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
		if (message == NULL) {
			/* can this even happen? */
			rb_debug ("breaking out of bus polling loop");
			break;
		}

		switch (GST_MESSAGE_TYPE (message)) {
		case GST_MESSAGE_ERROR:
			{
				GError *gerror;
				char *debug;

				gst_message_parse_error (message, &gerror, &debug);
				if (*error != NULL) {
					rb_debug ("caught error: %s (%s), but we've already got one", gerror->message, debug);
				} else {
					rb_debug ("caught error: %s (%s)", gerror->message, debug);

					g_clear_error (error);
					*error = g_error_new_literal (RB_METADATA_ERROR,
								      RB_METADATA_ERROR_GENERAL,
								      gerror->message);
				}
				done = TRUE;

				g_error_free (gerror);
				g_free (debug);
			}
			break;

		case GST_MESSAGE_EOS:
			rb_debug ("got eos message");
			done = TRUE;
			break;

		default:
			break;
		}

		gst_message_unref (message);
	}
	gst_object_unref (bus);
	gst_element_set_state (pipeline, GST_STATE_NULL);

	if (g_output_stream_close (stream, NULL, &io_error) == FALSE) {
		goto gio_error;
	}
	g_object_unref (stream);
	stream = NULL;

	if (*error == NULL) {
		GFileInfo *originfo;

		/* check to ensure the file isn't corrupt */
		if (!check_file_valid (uri, tmpname)) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("File corrupted during write"));
			goto out_error;
		}

		src = g_file_new_for_uri (tmpname);
		dest = g_file_new_for_uri (uri);

		/* try to copy access and ownership attributes over, not likely to help much though */
		originfo = g_file_query_info (dest, "access::*,owner::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (originfo) {
			g_file_set_attributes_from_info (src, originfo, G_FILE_QUERY_INFO_NONE, NULL, NULL);
			g_object_unref (originfo);
		}

		g_file_move (src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &io_error);
		if (io_error != NULL) {
			goto gio_error;
		}
		goto out;
	}

gio_error:
	if (*error == NULL) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_IO,
			     "%s",
			     io_error->message);
	}
out_error:
	if (tmpname != NULL) {
		/* clean up temporary file */
		rb_debug ("removing temporary file %s", tmpname);
		dest = g_file_new_for_uri (tmpname);
		if (g_file_delete (dest, NULL, NULL) == FALSE) {
			rb_debug ("unable to remove temporary file?");
		}
		g_object_unref (dest);
	}
out:
	g_free (tmpname);
	if (pipeline != NULL)
		gst_object_unref (GST_OBJECT (pipeline));
}

gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field, GValue *ret)
{
	const GstTagList *tags;
	const char *tag;
	GValue gstvalue = {0, };
	GstClockTime duration;
	GstDateTime *datetime;
	GDate *dateptr = NULL;
	char *str = NULL;
	const char *v;
	int i;

	if (md->priv->info == NULL)
		return FALSE;

	/* special cases: mostly duration */
	switch (field) {
	case RB_METADATA_FIELD_DURATION:
		duration = gst_discoverer_info_get_duration (md->priv->info);
		if (duration != 0) {
			g_value_init (ret, G_TYPE_ULONG);
			g_value_set_ulong (ret, duration / (1000 * 1000 * 1000));
			return TRUE;
		} else {
			rb_debug ("no duration available");
			return FALSE;
		}
		break;
	case RB_METADATA_FIELD_BITRATE:
		if (md->priv->audio_bitrate != 0) {
			g_value_init (ret, G_TYPE_ULONG);
			g_value_set_ulong (ret, md->priv->audio_bitrate / 1000);
			return TRUE;
		} else {
			return FALSE;
		}

		break;

	case RB_METADATA_FIELD_DATE:
		tags = gst_discoverer_info_get_tags (md->priv->info);
		if (tags == NULL)
			return FALSE;

		/* mp4 generally gives us useful things in GST_TAG_DATE and garbage in GST_TAG_DATE_TIME,
		 * and everything else just gives us GST_TAG_DATE_TIME.  preferring GST_TAG_DATE should
		 * make mp4 work better without breaking anything else.
		 */
		if (gst_tag_list_get_date (tags, GST_TAG_DATE, &dateptr)) {
			g_value_init (ret, G_TYPE_ULONG);
			g_value_set_ulong (ret, g_date_get_julian (dateptr));
			g_date_free (dateptr);
			return TRUE;
		} else if (gst_tag_list_get_date_time (tags, GST_TAG_DATE_TIME, &datetime)) {
			GDate date;
			g_date_set_dmy (&date,
					gst_date_time_has_day (datetime) ? gst_date_time_get_day (datetime) : 1,
					gst_date_time_has_month (datetime) ? gst_date_time_get_month (datetime) : 1,
					gst_date_time_get_year (datetime));

			g_value_init (ret, G_TYPE_ULONG);
			g_value_set_ulong (ret, g_date_get_julian (&date));

			gst_date_time_unref (datetime);
			return TRUE;
		} else {
			return FALSE;
		}
	case RB_METADATA_FIELD_COMMENT:
		tags = gst_discoverer_info_get_tags (md->priv->info);
		if (tags == NULL)
			return FALSE;

		/* pick the first valid utf8 string that looks like a comment */
		i = 0;
		while (gst_tag_list_peek_string_index (tags, GST_TAG_EXTENDED_COMMENT, i, &v)) {
			if (g_utf8_validate (v, -1, NULL)) {
				char **bits;

				/* field must look like key=value,
				 * key must be 'comment' or 'comment[lang]'
				 */
				bits = g_strsplit (v, "=", 2);
				if (bits[1] != NULL &&
				    (g_ascii_strcasecmp (bits[0], "comment") == 0 ||
				    g_ascii_strncasecmp (bits[0], "comment[", 8) == 0)) {
					g_value_init (ret, G_TYPE_STRING);
					g_value_set_string (ret, bits[1]);
					g_strfreev (bits);
					return TRUE;
				}

				g_strfreev (bits);
			} else {
				rb_debug ("ignoring %s", v);
			}
			i++;
		}
		break;
	default:
		break;
	}

	tags = gst_discoverer_info_get_tags (md->priv->info);
	if (tags == NULL) {
		return FALSE;
	}

	tag = rb_metadata_gst_field_to_gst_tag (field);
	if (tag == NULL) {
		return FALSE;
	}

	if (rb_metadata_get_field_type (field) == G_TYPE_STRING) {

		/* pick the first valid utf8 string, or if we find a later
		 * string of which the first is a prefix, pick that.
		 */
		i = 0;
		while (gst_tag_list_peek_string_index (tags, tag, i, &v)) {
			if (g_utf8_validate (v, -1, NULL) && (str == NULL || g_str_has_prefix (v, str))) {
				g_free (str);
				str = g_strdup (v);
			} else {
				rb_debug ("ignoring %s", v);
			}
			i++;
		}

		if (str != NULL) {
			str = g_strstrip (str);
			g_value_init (ret, G_TYPE_STRING);
			g_value_take_string (ret, str);
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		if (gst_tag_list_copy_value (&gstvalue, tags, tag) == FALSE) {
			return FALSE;
		}
		g_value_init (ret, rb_metadata_get_field_type (field));
		g_value_transform (&gstvalue, ret);
		g_value_unset (&gstvalue);
		return TRUE;
	}
}

const char *
rb_metadata_get_media_type (RBMetaData *md)
{
	return md->priv->mediatype;
}

gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field, const GValue *val)
{
	const char *tag;

	/* don't write this out */
	if (field == RB_METADATA_FIELD_DURATION)
		return TRUE;

	tag = rb_metadata_gst_field_to_gst_tag (field);
	if (field == RB_METADATA_FIELD_DATE) {
	       	if (g_value_get_ulong (val) == 0) {
			/* we should ask gstreamer to remove the tag,
			 * but there is no easy way of doing so
			 */
		} else {
			GstDateTime *datetime;
			GDate date;
			GValue newval = {0,};
			g_value_init (&newval, GST_TYPE_DATE_TIME);

			g_date_set_julian (&date, g_value_get_ulong (val));
			datetime = gst_date_time_new (0.0,
				       		      g_date_get_year (&date),
						      g_date_get_month (&date),
						      g_date_get_day (&date),
					      	      0, 0, 0);
			g_value_take_boxed (&newval, datetime);

			if (md->priv->tags == NULL) {
				md->priv->tags = gst_tag_list_new_empty ();
			}

			gst_tag_list_add_values (md->priv->tags,
						 GST_TAG_MERGE_APPEND,
						 tag, &newval,
						 NULL);
			g_value_unset (&newval);
		}
	} else {
		GValue newval = {0,};

		g_value_init (&newval, gst_tag_get_type (tag));
		if (g_value_transform (val, &newval)) {
			rb_debug ("Setting %s",tag);

			if (md->priv->tags == NULL) {
				md->priv->tags = gst_tag_list_new_empty ();
			}

			gst_tag_list_add_values (md->priv->tags,
						 GST_TAG_MERGE_APPEND,
						 tag, &newval,
						 NULL);
		}
		g_value_unset (&newval);
	}

	return TRUE;
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;
	md = RB_METADATA (object);
	rb_metadata_reset (md);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = (G_TYPE_INSTANCE_GET_PRIVATE ((md), RB_TYPE_METADATA, RBMetaDataPrivate));

	md->priv->taggers = g_hash_table_new (g_str_hash, g_str_equal);

	if (gst_element_factory_find ("giostreamsink") == FALSE) {
		rb_debug ("giostreamsink not found, can't tag anything");
	} else {
		if (gst_element_factory_find ("vorbistag") &&
		    gst_element_factory_find ("vorbisparse") &&
		    gst_element_factory_find ("oggmux")) {
			rb_debug ("ogg vorbis tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/x-vorbis", (gpointer)vorbis_tagger);
		}

		if (gst_element_factory_find ("flactag")) {
			rb_debug ("flac tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/x-flac", flac_tagger);
		}

		if (gst_element_factory_find ("id3v2mux") || gst_element_factory_find ("id3mux")) {
			rb_debug ("id3 tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/mpeg", mp3_tagger);
		}

		if (gst_element_factory_find ("mp4mux")) {
			rb_debug ("mp4 tagging available");
			g_hash_table_insert (md->priv->taggers, "audio/x-aac", mp4_tagger);
		}
	}

	md->priv->jpeg_image_caps = gst_caps_from_string ("image/jpeg, framerate = (fraction) [ 0, 1/1 ]");
}

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;

	g_type_class_add_private (klass, sizeof (RBMetaDataPrivate));
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL, NULL));
}
