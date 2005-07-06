/*
 *  arch-tag: Implementation of metadata reading using GStreamer
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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
#include <libgnome/gnome-i18n.h>
#include <gst/gst.h>
#include <gst/gsttag.h>
#include <string.h>

#include "rb-metadata.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

G_DEFINE_TYPE(RBMetaData, rb_metadata, G_TYPE_OBJECT)

static void rb_metadata_finalize (GObject *object);

typedef GstElement *(*RBAddTaggerElem) (GstBin *, GstElement *);


/*
 * The list of mine type prefixes for files that shouldn't display errors about being non-audio.
 * Useful for people who have cover art, et cetera, in their music directories
 */
const char * ignore_mime_types[] = {
	"image/",
	"text/",
};

struct RBMetadataGstType
{
	char *mimetype;
	RBAddTaggerElem tag_func;
	char *human_name;
};

struct RBMetaDataPrivate
{
	char *uri;
  
	GHashTable *metadata;

	GstElement *pipeline;
	GstElement *sink;

	/* Array of RBMetadataGstType */
	GPtrArray *supported_types;

	char *type;
	gboolean handoff;
	gboolean eos;
	gboolean non_audio;
	GError *error;
};


static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;
}


static GstElement *
rb_add_flac_tagger (GstBin *pipeline, GstElement *element)
{
	GstElement *tagger = NULL;

	if (!(tagger = gst_element_factory_make ("flactag", "flactag")))
		return NULL;

	gst_bin_add (GST_BIN (pipeline), tagger);
	gst_element_link_many (element, tagger, NULL);

	return tagger;
}

static GstElement *
rb_add_id3_tagger (GstBin *pipeline, GstElement *element)
{
	GstElement *spider;
	GstElement *tagger;
	GstCaps *filtercaps = NULL;

	if (!(spider = gst_element_factory_make ("spider", "spider")))
		return NULL;

	if (!(tagger = gst_element_factory_make ("id3mux", "id3mux")))
		/* FIXME: leaks spider */
		return NULL;

	gst_bin_add (GST_BIN (pipeline), spider);
	gst_bin_add (GST_BIN (pipeline), tagger);
	
	gst_element_link_many (element, spider, NULL);
	filtercaps = gst_caps_new_simple ("audio/mpeg", NULL);
	gst_element_link_filtered (spider, tagger, filtercaps);
	gst_caps_free (filtercaps);

	return tagger;

}

static void
add_supported_type (RBMetaData *md,
		    const char *mime,
		    RBAddTaggerElem add_tagger_func,
		    const char *human_name)
{
	struct RBMetadataGstType *type = g_new0 (struct RBMetadataGstType, 1);
	type->mimetype = g_strdup (mime);
	type->tag_func = add_tagger_func;
	type->human_name = g_strdup (human_name);
	g_ptr_array_add (md->priv->supported_types, type);
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = g_new0 (RBMetaDataPrivate, 1);

	md->priv->supported_types = g_ptr_array_new ();
	
 	/* the list of supported types serves two purposes:
 	 * - it knows how to construct elements for tag writing
 	 * - it knows human-readable names for MIME types so we can say
 	 *     "There is no plugin available to play WMV files"
 	 *     rather than " .. play video/x-ms-asf files".
 	 *
 	 * only registering types we have plugins for defeats the second 
	 * purpose.
 	 */

	add_supported_type (md, "application/x-id3", rb_add_id3_tagger, "MP3");
	add_supported_type (md, "audio/mpeg", rb_add_id3_tagger, "MP3");
	add_supported_type (md, "application/ogg", NULL, "Ogg");
	add_supported_type (md, "audio/x-flac", rb_add_flac_tagger, "FLAC");
	add_supported_type (md, "audio/x-mod", NULL, "MOD");
}

static void
rb_metadata_finalize (GObject *object)
{
	int i;
	RBMetaData *md;

	md = RB_METADATA (object);

	for (i = 0; i < md->priv->supported_types->len; i++) {
		struct RBMetadataGstType *type = g_ptr_array_index (md->priv->supported_types, i);
		g_free (type->mimetype);
		g_free (type->human_name);
		g_free (type);
	}
	g_ptr_array_free (md->priv->supported_types, TRUE);

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);

	if (md->priv->pipeline)
		gst_object_unref (GST_OBJECT (md->priv->pipeline));

	g_free (md->priv->type);
	g_free (md->priv->uri);

	g_free (md->priv);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

static void
free_gvalue (GValue *val)
{
	g_value_unset (val);
	g_free (val);
}

static int
rb_metadata_gst_tag_to_field (const char *tag)
{
	if (!strcmp (tag, GST_TAG_TITLE))
		return RB_METADATA_FIELD_TITLE;
	else if (!strcmp (tag, GST_TAG_ARTIST))
		return RB_METADATA_FIELD_ARTIST;
	else if (!strcmp (tag, GST_TAG_ALBUM))
		return RB_METADATA_FIELD_ALBUM;
	else if (!strcmp (tag, GST_TAG_DATE))
		return RB_METADATA_FIELD_DATE;
	else if (!strcmp (tag, GST_TAG_GENRE))
		return RB_METADATA_FIELD_GENRE;
	else if (!strcmp (tag, GST_TAG_COMMENT))
		return RB_METADATA_FIELD_COMMENT;
	else if (!strcmp (tag, GST_TAG_TRACK_NUMBER))
		return RB_METADATA_FIELD_TRACK_NUMBER;
	else if (!strcmp (tag, GST_TAG_TRACK_COUNT)) 
		return RB_METADATA_FIELD_MAX_TRACK_NUMBER;
	else if (!strcmp (tag, GST_TAG_ALBUM_VOLUME_NUMBER))
		return RB_METADATA_FIELD_DISC_NUMBER;
	else if (!strcmp (tag, GST_TAG_ALBUM_VOLUME_COUNT))
		return RB_METADATA_FIELD_MAX_TRACK_NUMBER;
	else if (!strcmp (tag, GST_TAG_DESCRIPTION))
		return RB_METADATA_FIELD_DESCRIPTION;
	else if (!strcmp (tag, GST_TAG_VERSION))
		return RB_METADATA_FIELD_VERSION;
	else if (!strcmp (tag, GST_TAG_ISRC))
		return RB_METADATA_FIELD_ISRC;
	else if (!strcmp (tag, GST_TAG_ORGANIZATION))
		return RB_METADATA_FIELD_ORGANIZATION;
	else if (!strcmp (tag, GST_TAG_COPYRIGHT))
		return RB_METADATA_FIELD_COPYRIGHT;
	else if (!strcmp (tag, GST_TAG_CONTACT))
		return RB_METADATA_FIELD_CONTACT;
	else if (!strcmp (tag, GST_TAG_LICENSE))
		return RB_METADATA_FIELD_LICENSE;
	else if (!strcmp (tag, GST_TAG_PERFORMER))
		return RB_METADATA_FIELD_PERFORMER;
	else if (!strcmp (tag, GST_TAG_DURATION))
		return RB_METADATA_FIELD_DURATION;
	else if (!strcmp (tag, GST_TAG_CODEC))
		return RB_METADATA_FIELD_CODEC;
	else if (!strcmp (tag, GST_TAG_BITRATE))
		return RB_METADATA_FIELD_BITRATE;
	else if (!strcmp (tag, GST_TAG_TRACK_GAIN))
		return RB_METADATA_FIELD_TRACK_GAIN;
	else if (!strcmp (tag, GST_TAG_TRACK_PEAK))
		return RB_METADATA_FIELD_TRACK_PEAK;
	else if (!strcmp (tag, GST_TAG_ALBUM_GAIN))
		return RB_METADATA_FIELD_ALBUM_GAIN;
	else if (!strcmp (tag, GST_TAG_ALBUM_PEAK))
		return RB_METADATA_FIELD_ALBUM_PEAK;
	else
		return -1;
}

static const char *
rb_metadata_gst_field_to_gst_tag (RBMetaDataField field)
{
	switch (field)
	{
	case RB_METADATA_FIELD_TITLE:
		return GST_TAG_TITLE;
	case RB_METADATA_FIELD_ARTIST:
		return GST_TAG_ARTIST;
	case RB_METADATA_FIELD_ALBUM:
		return GST_TAG_ALBUM;
	case RB_METADATA_FIELD_DATE:
		return GST_TAG_DATE;
	case RB_METADATA_FIELD_GENRE:
		return GST_TAG_GENRE;
	case RB_METADATA_FIELD_COMMENT:
		return GST_TAG_COMMENT;
	case RB_METADATA_FIELD_TRACK_NUMBER:
		return GST_TAG_TRACK_NUMBER;
	case RB_METADATA_FIELD_MAX_TRACK_NUMBER:
		return GST_TAG_TRACK_COUNT;
	case RB_METADATA_FIELD_DISC_NUMBER:
		return GST_TAG_ALBUM_VOLUME_NUMBER;
	case RB_METADATA_FIELD_MAX_DISC_NUMBER:
		return GST_TAG_ALBUM_VOLUME_COUNT;
	case RB_METADATA_FIELD_DESCRIPTION:
		return GST_TAG_DESCRIPTION;
	case RB_METADATA_FIELD_VERSION:
		return GST_TAG_VERSION;
	case RB_METADATA_FIELD_ISRC:
		return GST_TAG_ISRC;
	case RB_METADATA_FIELD_ORGANIZATION:
		return GST_TAG_ORGANIZATION;
	case RB_METADATA_FIELD_COPYRIGHT:
		return GST_TAG_COPYRIGHT;
	case RB_METADATA_FIELD_CONTACT:
		return GST_TAG_CONTACT;
	case RB_METADATA_FIELD_LICENSE:
		return GST_TAG_LICENSE;
	case RB_METADATA_FIELD_PERFORMER:
		return GST_TAG_PERFORMER;
	case RB_METADATA_FIELD_DURATION:
		return GST_TAG_DURATION;
	case RB_METADATA_FIELD_CODEC:
		return GST_TAG_CODEC;
	case RB_METADATA_FIELD_BITRATE:
		return GST_TAG_BITRATE;
	case RB_METADATA_FIELD_TRACK_GAIN:
		return GST_TAG_TRACK_GAIN;
	case RB_METADATA_FIELD_TRACK_PEAK:
		return GST_TAG_TRACK_PEAK;
	case RB_METADATA_FIELD_ALBUM_GAIN:
		return GST_TAG_ALBUM_GAIN;
	case RB_METADATA_FIELD_ALBUM_PEAK:
		return GST_TAG_ALBUM_PEAK;
	default:
		return NULL;
	}
}

static const char *
rb_metadata_gst_type_to_name (RBMetaData *md, const char *mimetype)
{
	int i;
	for (i = 0; i < md->priv->supported_types->len; i++) {
		struct RBMetadataGstType *type = g_ptr_array_index (md->priv->supported_types, i);
		if (!strcmp (type->mimetype, mimetype))
			return type->human_name;
	}
	return NULL;
}

static RBAddTaggerElem
rb_metadata_gst_type_to_tag_function (RBMetaData *md, const char *mimetype)
{
	int i;
	for (i = 0; i < md->priv->supported_types->len; i++) {
		struct RBMetadataGstType *type = g_ptr_array_index (md->priv->supported_types, i);
		if (!strcmp (type->mimetype, mimetype))
			return type->tag_func;
	}
	return NULL;
}

static void
rb_metadata_gst_eos_cb (GstElement *element, RBMetaData *md)
{
	rb_debug ("caught eos");
	if (md->priv->eos) {
		rb_debug ("RENTERED EOS!");
		return;
	}
	gst_element_set_state (md->priv->sink, GST_STATE_NULL);
	md->priv->eos = TRUE;
}

static void
rb_metadata_gst_error_cb (GstElement *element,
			  GstElement *source,
			  GError *error,
			  gchar *debug,
			  RBMetaData *md)
{
	rb_debug ("caught error: %s ", error->message);
	md->priv->error = g_error_new_literal (RB_METADATA_ERROR,
					       RB_METADATA_ERROR_GENERAL,
					       error->message);
}

static void
rb_metadata_gst_load_tag (const GstTagList *list, const gchar *tag, RBMetaData *md)
{
	int count, tem, type;
	RBMetaDataField field;
	GValue *newval;
	const GValue *val;

	rb_debug ("uri: %s tag: %s ", md->priv->uri, tag);

	count = gst_tag_list_get_tag_size (list, tag);
	if (count < 1)
		return;

	tem = rb_metadata_gst_tag_to_field (tag);
	if (tem < 0)
		return;
	field = (RBMetaDataField) tem;

	type = rb_metadata_get_field_type (md, field);
	val = gst_tag_list_get_value_index (list, tag, 0);
	newval = g_new0 (GValue, 1);
	g_value_init (newval, type);
	if (!g_value_transform (val, newval)) {
		
		rb_debug ("Could not transform tag value type %s into %s",
			  g_type_name (G_VALUE_TYPE (val)), 
			  g_type_name (G_VALUE_TYPE (newval)));
		return;
	}

	switch (type) {
	case G_TYPE_STRING: {
		/* Remove leading and trailing whitespace */
		char *str;
		str = g_value_dup_string (newval);
		str = g_strstrip (str);
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
		break;
	}

	case RB_METADATA_FIELD_DURATION: {
		/* GStreamer sends us duration in ns, 
		 * but we need it in seconds
		 */
		guint64 duration;
		duration = g_value_get_uint64 (val);
		g_value_set_ulong (newval, duration/(1000*1000*1000));
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
rb_metadata_gst_found_tag (GObject *pipeline, GstElement *source, GstTagList *tags, RBMetaData *md)
{
	gst_tag_list_foreach (tags, (GstTagForeachFunc) rb_metadata_gst_load_tag, md);
}

static void
rb_metadata_gst_typefind_cb (GstElement *typefind, guint probability, GstCaps *caps, RBMetaData *md)
{
	if (gst_caps_get_size (caps) > 0) {
		md->priv->type = g_strdup (gst_structure_get_name (gst_caps_get_structure (caps, 0)));
		rb_debug ("found type %s", md->priv->type);
	}
}

static void
rb_metadata_gst_fakesink_handoff_cb (GstElement *fakesink, GstBuffer *buf, GstPad *pad, RBMetaData *md)
{
	if (md->priv->handoff) {
		/* we get lots of these with decodebin.  why? */
		/* rb_debug ("caught recursive handoff!"); */
		return;
	} else if (md->priv->eos) {
		rb_debug ("caught handoff after eos!");
		return;
	}
		
	rb_debug ("in fakesink handoff");
	md->priv->handoff = TRUE;
}

static void
rb_metadata_gst_new_decoded_pad_cb (GstElement *decodebin, GstPad *pad, gboolean last, RBMetaData *md)
{
	GstCaps *caps;
	GstStructure *structure;
	const gchar *mimetype;

	caps = gst_pad_get_caps (pad);

	/* we get "ANY" caps for text/plain files etc. */
	if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
		rb_debug ("decoded pad with no caps or any caps.  this file is boring.");
		md->priv->non_audio = TRUE;
	} else {
		/* is this pad audio? */
		structure = gst_caps_get_structure (caps, 0);
		mimetype = gst_structure_get_name (structure);
		if (g_str_has_prefix (mimetype, "audio/x-raw")) {
			/* link this to the fake sink */
			GstPad *sink_pad;
			rb_debug ("linking new decoded pad of type %s to fakesink", mimetype);
			sink_pad = gst_element_get_pad (md->priv->sink, "sink");
			gst_pad_link (pad, sink_pad);

			/* what happens if we get two audio pads from the same stream? 
			 * can this happen?
			 */
		} else {
			/* assume anything we can get a video or text stream out of is
			 * something that should be fed to totem rather than rhythmbox.
			 */
			rb_debug ("got decoded pad of non-audio type %s", mimetype);
			md->priv->non_audio = TRUE;
		}
	}

	gst_caps_free (caps);
}

static void
rb_metadata_gst_unknown_type_cb (GstElement *decodebin, GstPad *pad, GstCaps *caps, RBMetaData *md)
{
	/* try to shortcut it a bit */
	rb_debug ("decodebin emitted unknown type signal");
	md->priv->non_audio = TRUE;
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

void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *gnomevfssrc = NULL;
	GstElement *decodebin = NULL;
	GstElement *typefind = NULL;

	g_free (md->priv->uri);
	md->priv->uri = NULL;
	g_free (md->priv->type);
	md->priv->type = NULL;
	md->priv->error = NULL;
	md->priv->eos = FALSE;
	md->priv->handoff = FALSE;
	md->priv->non_audio = FALSE;

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
						    NULL, (GDestroyNotify) free_gvalue);

	/* The main tagfinding pipeline looks like this:
 	 * gnomevfssrc ! decodebin ! fakesink
 	 *
 	 * but we can only link the fakesink in when the decodebin
 	 * creates an audio source pad.  we do this in the 'new-decoded-pad'
 	 * signal handler.
 	 */
	pipeline = gst_pipeline_new ("pipeline");

 	gnomevfssrc = make_pipeline_element (pipeline, "gnomevfssrc", error);
 	decodebin = make_pipeline_element (pipeline, "decodebin", error);
 	md->priv->sink = make_pipeline_element (pipeline, "fakesink", error);
 	if (!(gnomevfssrc && decodebin && md->priv->sink)) {
 		rb_debug ("missing an element, sadly");
 		goto out;
 	}


	g_object_set (G_OBJECT (gnomevfssrc), "location", uri, NULL);
	g_object_set (G_OBJECT (md->priv->sink), "signal-handoffs", TRUE, NULL);
 
 	g_signal_connect_object (pipeline, "error", G_CALLBACK (rb_metadata_gst_error_cb), md, 0);
 	g_signal_connect_object (pipeline, "found-tag", G_CALLBACK (rb_metadata_gst_found_tag), md, 0);

	g_signal_connect_object (md->priv->sink, "handoff", G_CALLBACK (rb_metadata_gst_fakesink_handoff_cb), md, 0);
	g_signal_connect_object (md->priv->sink, "eos", G_CALLBACK (rb_metadata_gst_eos_cb), md, 0);

 	g_signal_connect_object (decodebin, "new-decoded-pad", G_CALLBACK (rb_metadata_gst_new_decoded_pad_cb), md, 0);
 	g_signal_connect_object (decodebin, "unknown-type", G_CALLBACK (rb_metadata_gst_unknown_type_cb), md, 0);
  
 	/* locate the decodebin's typefind, so we can get the have_type signal too.
 	 * this is kind of nasty, since it relies on an essentially arbitrary string 
 	 * in the decodebin code not changing.  the alternative is to have our own
 	 * typefind instance before the decodebin.  it might not like that.
 	 */
 	typefind = gst_bin_get_by_name (GST_BIN (decodebin), "typefind");
 	g_assert (typefind != NULL);
 	g_signal_connect_object (typefind, "have_type", G_CALLBACK (rb_metadata_gst_typefind_cb), md, 0);
 
 	gst_element_link (gnomevfssrc, decodebin);

	md->priv->pipeline = pipeline;
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	while (gst_bin_iterate (GST_BIN (pipeline))
	       && md->priv->error == NULL
	       && !md->priv->handoff
	       && !md->priv->eos)
		;

	if (md->priv->handoff) {
		/* We caught the first buffer, which means the decoder should have read all
		 * of the metadata, and should know the length now.
		 */
		if (g_hash_table_lookup (md->priv->metadata, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION)) == NULL) {
			GstFormat format = GST_FORMAT_TIME;
			gint64 length;
			
			if (gst_element_query (md->priv->sink, GST_QUERY_TOTAL,
					       &format, &length)) {
				GValue *newval = g_new0 (GValue, 1);
				
				rb_debug ("duration query succeeded");
				
				g_value_init (newval, G_TYPE_ULONG);
				/* FIXME - use guint64 for duration? */
				g_value_set_ulong (newval, (long) (length / (1 * 1000 * 1000 * 1000)));
				g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION),
						     newval);
			} else {
				rb_debug ("duration query failed!");
			}
		}
	} else if (md->priv->eos)
		rb_debug ("caught eos without handoff!");
	
	gst_element_set_state (pipeline, GST_STATE_NULL);

	/* report errors for various failure cases.
	 * these don't include the URI as the load failure dialog
	 * already displays it.
	 */
	if (md->priv->error) {
		g_propagate_error (error, md->priv->error);
	} else if (!md->priv->type) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNRECOGNIZED,
			     _("The MIME type of the file could not be identified"));
	} else if (md->priv->non_audio) {
		gboolean ignore = FALSE;
		int i;

		for (i = 0; i < G_N_ELEMENTS (ignore_mime_types); i++)
			if (g_str_has_prefix (md->priv->type, ignore_mime_types[i]))
				ignore = TRUE;
		if (!ignore)
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_NOT_AUDIO,
				     _("The file is not an audio stream"));
		else
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_NOT_AUDIO_IGNORE,
				     _("The file is not an audio stream, but is being ignored"));
	} else if (!md->priv->handoff) {
		const char *report_type = rb_metadata_gst_type_to_name (md, md->priv->type);
		if (report_type == NULL)
			report_type = md->priv->type;
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNSUPPORTED,
			     _("There is no plugin installed to handle a %s file."),
			     md->priv->type);
	} else {
		/* yay, it worked */
		rb_debug ("successfully read metadata for %s", uri);
	
		/* it doesn't matter if we don't recognise the format,
		 * as long as gstreamer can play it, we can put it in
		 * the library.
		 */
		if (!rb_metadata_gst_type_to_name (md, md->priv->type)) {
			rb_debug ("we don't know what type %s (from file %s) is, but we'll play it anyway",
				  md->priv->type, uri);
		}
	}

	if (*error != NULL) {
		g_free (md->priv->type);
		md->priv->type = NULL;
	}
 out:
	if (pipeline != NULL)
		gst_object_unref (GST_OBJECT (pipeline));
	md->priv->pipeline = NULL;
}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
#ifdef ENABLE_TAG_WRITING
	return rb_metadata_gst_type_to_tag_function (md, mimetype) != NULL;
#else 
	return FALSE;
#endif
}

static void
rb_metadata_gst_add_tag_data (gpointer key, const GValue *val, GstTagSetter *tagsetter)
{
	RBMetaDataField field = GPOINTER_TO_INT (key);
	const char *tag = rb_metadata_gst_field_to_gst_tag (field);

	if (tag) {
		GValue newval = {0,};
		g_value_init (&newval, gst_tag_get_type (tag));
		if (g_value_transform (val, &newval)) {
			gst_tag_setter_add_values (GST_TAG_SETTER (tagsetter),
						   GST_TAG_MERGE_REPLACE,
						   tag, &newval, NULL);
		}
		g_value_unset (&newval);
	}
}



void
rb_metadata_save (RBMetaData *md, GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *gnomevfssrc = NULL;
	GstElement *tagger = NULL;
	const char *plugin_name = NULL;
	char *tmpname = NULL;
	GnomeVFSHandle *handle = NULL;
	GnomeVFSResult result;
	RBAddTaggerElem add_tagger_func;

	g_return_if_fail (md->priv->uri != NULL);
	g_return_if_fail (md->priv->type != NULL);

	rb_debug ("saving metadata for uri: %s", md->priv->uri);

	if ((result = rb_uri_mkstemp (md->priv->uri, &tmpname, &handle)) != GNOME_VFS_OK)
		goto vfs_error;

	pipeline = gst_pipeline_new ("pipeline");

	g_signal_connect_object (pipeline, "error", G_CALLBACK (rb_metadata_gst_error_cb), md, 0);

	/* Source */
	plugin_name = "gnomevfssrc";
	if (!(gnomevfssrc = gst_element_factory_make (plugin_name, plugin_name)))
		goto missing_plugin;
	gst_bin_add (GST_BIN (pipeline), gnomevfssrc);		  
	g_object_set (G_OBJECT (gnomevfssrc), "location", md->priv->uri, NULL);

	/* Sink */
	plugin_name = "gnomevfssink";
	if (!(md->priv->sink = gst_element_factory_make (plugin_name, plugin_name)))
		goto missing_plugin;

	g_object_set (G_OBJECT (md->priv->sink), "handle", handle, NULL);
	g_signal_connect_object (md->priv->sink, "eos", G_CALLBACK (rb_metadata_gst_eos_cb), md, 0);
	/* FIXME: gst_bin_add (GST_BIN (pipeline, md->priv->sink)) really
	 * should be called here, but weird crashes happen when unreffing the
	 * pipeline if it's not moved after the creation of the tagging
	 * elements 
	 */

	/* Tagger element(s) */
	add_tagger_func = rb_metadata_gst_type_to_tag_function (md, md->priv->type);
	
	if (!add_tagger_func) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNSUPPORTED,
			     "Unsupported file type: %s", md->priv->type);
		goto out_error;
	}

	tagger = add_tagger_func (GST_BIN (pipeline), gnomevfssrc);
	gst_tag_setter_set_merge_mode (GST_TAG_SETTER (tagger), GST_TAG_MERGE_REPLACE);
	g_hash_table_foreach (md->priv->metadata, 
			      (GHFunc) rb_metadata_gst_add_tag_data,
			      GST_TAG_SETTER (tagger));

	gst_bin_add (GST_BIN (pipeline), md->priv->sink); 

	gst_element_link_many (tagger, md->priv->sink, NULL);

	md->priv->pipeline = pipeline;
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	while (gst_bin_iterate (GST_BIN (pipeline))
	       && md->priv->error == NULL
	       && !md->priv->eos)
		;
	gst_element_set_state (pipeline, GST_STATE_NULL);
	if (md->priv->error) {
		g_propagate_error (error, md->priv->error);
		goto out_error;
	}
	if (handle != NULL) {
		if ((result = gnome_vfs_close (handle)) != GNOME_VFS_OK)
			goto vfs_error;
		if ((result = gnome_vfs_move (tmpname, md->priv->uri, TRUE)) != GNOME_VFS_OK)
				goto vfs_error;
	}

	goto out;
 vfs_error:
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_GNOMEVFS,
		     "%s",
		     gnome_vfs_result_to_string (result)); 
	goto out_error;
 missing_plugin:
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_MISSING_PLUGIN,
		     _("Failed to create %s element; check your installation"),
		     plugin_name); 
 out_error:
	if (handle != NULL)
		gnome_vfs_close (handle);
	if (tmpname != NULL)
		gnome_vfs_unlink (tmpname);
 out:
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

	type = rb_metadata_get_field_type (md, field);
	g_return_val_if_fail (type == G_VALUE_TYPE (val), FALSE);

	newval = g_new0 (GValue, 1);
	g_value_init (newval, type);
	g_value_copy (val, newval);

	g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (field),
			     newval);
	return TRUE;
}

