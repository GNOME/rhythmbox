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
#include <gst/tag/tag.h>
#include <gst/gsturi.h>

#ifdef HAVE_GSTREAMER_0_8
#include <gst/gsttag.h>
#define gst_caps_unref gst_caps_free
#define gst_tag_setter_add_tag_values gst_tag_setter_add_values
#define gst_tag_setter_set_tag_merge_mode gst_tag_setter_set_merge_mode
#define gst_tag_setter_merge_tags gst_tag_setter_merge
#endif
#ifdef HAVE_GSTREAMER_0_10
#include <gst/gsttagsetter.h>
#endif

#include "rb-metadata.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

G_DEFINE_TYPE(RBMetaData, rb_metadata, G_TYPE_OBJECT)

static void rb_metadata_finalize (GObject *object);

typedef GstElement *(*RBAddTaggerElem) (RBMetaData *, GstElement *);


/*
 * The list of mine type prefixes for files that shouldn't display errors about being non-audio.
 * Useful for people who have cover art, et cetera, in their music directories
 */
const char * ignore_mime_types[] = {
	"image/",
	"text/",
	"application/xml",
	"application/zip"
};

/*
 * File size below which we will simply ignore files that can't be identified.
 * This is mostly here so we ignore the various text files that are packaged
 * with many netlabel releases and other downloads.
 */
#define REALLY_SMALL_FILE_SIZE	(4096)

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
	gulong typefind_cb_id;
	GstTagList *tags;

	/* Array of RBMetadataGstType */
	GPtrArray *supported_types;

	char *type;
	gboolean handoff;
	gboolean eos;
	gboolean non_audio;
	gboolean has_video;
	GError *error;
};

#define RB_METADATA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_METADATA, RBMetaDataPrivate))

#ifdef HAVE_GSTREAMER_0_10
static void
gst_date_gulong_transform (const GValue *src, GValue *dest)
{
	const GDate *date = gst_value_get_date (src);

	g_value_set_ulong (dest, (date) ? g_date_get_julian (date) : 0);
}

static void
gulong_gst_date_transform (const GValue *src, GValue *dest)
{
	gulong day = g_value_get_ulong (src);
	GDate *date = g_date_new_julian (day);

	gst_value_set_date (dest, date);
	g_date_free (date);
}

#endif

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;

	g_type_class_add_private (klass, sizeof (RBMetaDataPrivate));
#ifdef HAVE_GSTREAMER_0_10
	g_value_register_transform_func (GST_TYPE_DATE, G_TYPE_ULONG, gst_date_gulong_transform);
	g_value_register_transform_func (G_TYPE_ULONG, GST_TYPE_DATE, gulong_gst_date_transform);
#endif
}


static GstElement *
rb_add_flac_tagger (RBMetaData *md, GstElement *element)
{
	GstElement *tagger = NULL;

	if (!(tagger = gst_element_factory_make ("flactag", "flactag")))
		return NULL;

	gst_bin_add (GST_BIN (md->priv->pipeline), tagger);
	gst_element_link_many (element, tagger, NULL);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), md->priv->tags, GST_TAG_MERGE_REPLACE_ALL);

	return tagger;
}


#ifdef HAVE_GSTREAMER_0_10
static void
id3_pad_added_cb (GstElement *demux, GstPad *pad, GstElement *mux)
{
	GstPad *mux_pad;

	mux_pad = gst_element_get_compatible_pad (mux, pad, NULL);
	if (gst_pad_link (pad, mux_pad) != GST_PAD_LINK_OK)
		rb_debug ("unable to link pad from id3demux to id3mux");
	else
		rb_debug ("linked pad from id3de to id3mux");
}

static gboolean
rb_gst_plugin_greater (const char *plugin, const char *element, gint major, gint minor, gint micro)
{
	const char *version;
	GstPlugin *p;
	guint i;
	guint count;

	if (gst_default_registry_check_feature_version (element, major, minor, micro + 1))
		return TRUE;

	if (!gst_default_registry_check_feature_version (element, major, minor, micro))
		return FALSE;

	p = gst_default_registry_find_plugin (plugin);
	if (p == NULL)
		return FALSE;

	version = gst_plugin_get_version (p);

	/* check if it's not a release */
	count = sscanf (version, "%u.%u.%u.%u", &i, &i, &i, &i);
	return (count > 3);
}

static GstElement *
rb_add_id3_tagger (RBMetaData *md, GstElement *element)
{
	GstElement *demux = NULL;
	GstElement *mux = NULL;

	demux = gst_element_factory_make ("id3demux", NULL);

	mux = gst_element_factory_make ("id3v2mux", NULL);
	if (mux != NULL) {
		/* check for backwards id3v2mux merge-mode */
		if (!rb_gst_plugin_greater ("taglib", "id3v2mux", 0, 10, 3)) {
			rb_debug ("using id3v2mux with backwards merge mode");
			gst_tag_setter_set_tag_merge_mode (GST_TAG_SETTER (mux), GST_TAG_MERGE_REPLACE);
		} else {
			rb_debug ("using id3v2mux");
		}
	} else {
		mux =  gst_element_factory_make ("id3mux", NULL);

		/* check for backwards id3mux merge-mode */
		if (mux && !rb_gst_plugin_greater ("mad", "id3mux", 0, 10, 3)) {
			rb_debug ("using id3mux with backwards merge mode");
			gst_tag_setter_set_tag_merge_mode (GST_TAG_SETTER (mux), GST_TAG_MERGE_REPLACE);
		} else {
			rb_debug ("using id3mux");
		}
	}

	if (demux == NULL || mux == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (md->priv->pipeline), demux, mux, NULL);
	if (!gst_element_link (element, demux))
		goto error;

	g_signal_connect (demux, "pad-added", (GCallback)id3_pad_added_cb, mux);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), md->priv->tags, GST_TAG_MERGE_REPLACE_ALL);

	return mux;

error:
	g_object_unref (demux);
	g_object_unref (mux);
	return NULL;
}


static void
ogg_pad_added_cb (GstElement *demux, GstPad *pad, RBMetaData *md)
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
		tagger = gst_element_factory_make ("vorbistag", NULL);

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

		gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), md->priv->tags, GST_TAG_MERGE_REPLACE_ALL);
	} else {
		conn_pad = gst_element_get_compatible_pad (mux, pad, NULL);
		gst_pad_link (pad, conn_pad);
		rb_debug ("found stream in ogg container with no known tagging element");
	}

	gst_caps_unref (caps);
}

static GstElement *
rb_add_ogg_tagger (RBMetaData *md, GstElement *element)
{
	GstElement *demux = NULL;
	GstElement *mux = NULL;

	demux = gst_element_factory_make ("oggdemux", NULL);
	mux =  gst_element_factory_make ("oggmux", NULL);

	if (demux == NULL || mux == NULL)
		goto error;

	gst_bin_add_many (GST_BIN (md->priv->pipeline), demux, mux, NULL);
	if (!gst_element_link (element, demux))
		goto error;

	g_object_set_data (G_OBJECT (demux), "mux", mux);
	g_signal_connect (demux, "pad-added", (GCallback)ogg_pad_added_cb, md);

	return mux;

error:
	g_object_unref (demux);
	g_object_unref (mux);
	return NULL;
}

#elif HAVE_GSTREAMER_0_8
static GstElement *
rb_add_id3_tagger (RBMetaData *md, GstElement *element)
{
	GstElement *tagger, *identity;
	GstCaps *filtercaps = NULL;

	if (!(tagger = gst_element_factory_make ("id3tag", "tagger"))) 
		return NULL;

	if (!(identity = gst_element_factory_make ("identity", "identity"))) {
		gst_object_unref (GST_OBJECT (tagger));
		return NULL;
	}

	gst_bin_add_many (GST_BIN (md->priv->pipeline), tagger, identity, NULL);

	filtercaps = gst_caps_new_simple ("application/x-id3", NULL);
	if (!gst_element_link_filtered (tagger, identity, filtercaps)) {
		g_warning ("Linking id3tag and identity failed");
		gst_caps_free (filtercaps);
		gst_object_unref (GST_OBJECT(tagger));
		gst_object_unref (GST_OBJECT(identity));
		return NULL;
        }
	gst_caps_free (filtercaps);

	gst_element_link_many(element, tagger, NULL);

	gst_tag_setter_merge_tags (GST_TAG_SETTER (tagger), md->priv->tags, GST_TAG_MERGE_REPLACE_ALL);

	return identity;
}
#endif

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
	RBAddTaggerElem tagger;
	gboolean has_gnomevfssink = FALSE;
	gboolean has_id3 = FALSE;
        
	md->priv = RB_METADATA_GET_PRIVATE (md);

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
	has_gnomevfssink = (gst_element_factory_find ("gnomevfssrc") != NULL &&
			    gst_element_factory_find ("gnomevfssink") != NULL);
#ifdef HAVE_GSTREAMER_0_10
	has_id3 = (gst_element_factory_find ("id3demux") != NULL) &&
			(gst_default_registry_check_feature_version ("id3mux", 0, 10, 2) ||
			 (gst_element_factory_find ("id3v2mux") != NULL));
#elif HAVE_GSTREAMER_0_8
	has_id3 = (gst_element_factory_find ("id3tag") != NULL);
#endif

	tagger = (has_gnomevfssink && has_id3) ?  rb_add_id3_tagger : NULL;
	add_supported_type (md, "application/x-id3", tagger, "MP3");
	add_supported_type (md, "audio/mpeg", tagger, "MP3");

#ifdef HAVE_GSTREAMER_0_10
	{
		gboolean has_vorbis;

		has_vorbis = ((gst_element_factory_find ("vorbistag") != NULL) &&
			      gst_default_registry_check_feature_version ("vorbisparse", 0, 10, 6) &&
			      gst_default_registry_check_feature_version ("oggmux", 0, 10, 6) &&
			      gst_default_registry_check_feature_version ("oggdemux", 0, 10, 6));
		tagger = (has_gnomevfssink && has_vorbis) ?  rb_add_ogg_tagger : NULL;
	}
#else
	tagger = NULL;
#endif
	add_supported_type (md, "application/ogg", tagger, "Ogg Vorbis");
	add_supported_type (md, "audio/x-vorbis", tagger, "Ogg Vorbis");

	add_supported_type (md, "audio/x-mod", NULL, "MOD");
	add_supported_type (md, "audio/x-wav", NULL, "WAV");
	add_supported_type (md, "video/x-ms-asf", NULL, "ASF");

	tagger = (has_gnomevfssink && gst_element_factory_find ("flactag")) ?  rb_add_flac_tagger : NULL;
	add_supported_type (md, "audio/x-flac", tagger, "FLAC");

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
	g_clear_error (&md->priv->error);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL, NULL));
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
#ifdef GST_TAG_MUSICBRAINZ_TRACKID
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID))
		return RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
#endif
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
#ifdef GST_TAG_MUSICBRAINZ_TRACKID
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
		return GST_TAG_MUSICBRAINZ_TRACKID;
#endif
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

static char *
make_undecodable_error (RBMetaData *md)
{
	const char *human_name;

	human_name= rb_metadata_gst_type_to_name (md, md->priv->type);
	if (human_name == NULL)
		human_name = rb_mime_get_friendly_name (md->priv->type);
	
	if (human_name) {
		return g_strdup_printf (_("The GStreamer plugins to decode \"%s\" files cannot be found"),
					human_name);
	} else {
		return g_strdup_printf (_("The file contains a stream of type %s, which is not decodable"),
					md->priv->type);
	}
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
		/* Reject invalid utf-8 strings, and then
		 * remove leading and trailing whitespace.
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

#ifdef HAVE_GSTREAMER_0_8
static void
rb_metadata_gst_found_tag (GObject *pipeline, GstElement *source, GstTagList *tags, RBMetaData *md)
{
	gst_tag_list_foreach (tags, (GstTagForeachFunc) rb_metadata_gst_load_tag, md);
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
rb_metadata_gst_error_cb (GstElement *element,
			  GstElement *source,
			  GError *error,
			  gchar *debug,
			  RBMetaData *md)
{
	if (error->domain == GST_STREAM_ERROR &&
	    error->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
		rb_debug ("caught type not found error");

	} else {
		rb_debug ("caught error: %s ", error->message);
		md->priv->error = g_error_new_literal (RB_METADATA_ERROR,
						       RB_METADATA_ERROR_GENERAL,
						       error->message);
	}
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

#endif

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
		md->priv->non_audio = TRUE;
		cancel = TRUE;
	} else {
		GstPad *sink_pad;

		sink_pad = gst_element_get_pad (md->priv->sink, "sink");
		gst_pad_link (pad, sink_pad);

		/* is this pad audio? */
		structure = gst_caps_get_structure (caps, 0);
		mimetype = gst_structure_get_name (structure);

		if (g_str_has_prefix (mimetype, "audio/x-raw")) {
			rb_debug ("got decoded audio pad of type %s", mimetype);
		} else if (g_str_has_prefix (mimetype, "video/")) {
			rb_debug ("got decoded video pad of type %s", mimetype);
			md->priv->non_audio = TRUE;
			md->priv->has_video = TRUE;
		} else {
			/* assume anything we can get a video or text stream out of is
			 * something that should be fed to totem rather than rhythmbox.
			 */
			rb_debug ("got decoded pad of non-audio type %s", mimetype);
			md->priv->non_audio = TRUE;
		}
	}

	gst_caps_unref (caps);

#ifdef HAVE_GSTREAMER_0_10
	/* if this is non-audio, cancel the operation
	 * under 0.8 this causes assertion faliures when a pad with no caps in found
	 * it isn't /needed/ under 0.8, so we don't do it.
	 *
	 * This seems to cause some deadlocks with video files, so only do it 
	 * when we get no/any caps.
	 */
	if (cancel)
		gst_element_set_state (md->priv->pipeline, GST_STATE_NULL);
#endif
}

static void
rb_metadata_gst_unknown_type_cb (GstElement *decodebin, GstPad *pad, GstCaps *caps, RBMetaData *md)
{
	/* try to shortcut it a bit */
	md->priv->non_audio = TRUE;

	if (!gst_caps_is_empty (caps) && !gst_caps_is_any (caps)) {
		GstStructure *structure;
		const gchar *mimetype;

		structure = gst_caps_get_structure (caps, 0);
		mimetype = gst_structure_get_name (structure);

		g_free (md->priv->type);
		md->priv->type = g_strdup (mimetype);
		
		rb_debug ("decodebin emitted unknown type signal for %s", mimetype);
	} else {
		rb_debug ("decodebin emitted unknown type signal");
	}

#ifdef HAVE_GSTREAMER_0_10
	{
		char *msg;

		msg = make_undecodable_error (md);
		GST_ELEMENT_ERROR (md->priv->pipeline, STREAM, CODEC_NOT_FOUND, ("%s", msg), (NULL));
		g_free (msg);
	}
#endif
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

#ifdef HAVE_GSTREAMER_0_10
static gboolean
rb_metadata_bus_handler (GstBus *bus, GstMessage *message, RBMetaData *md)
{
	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_EOS:
		rb_debug ("EOS reached");
		md->priv->eos = TRUE;
		break;
	case GST_MESSAGE_ERROR:
	{
		GError *gerror;
		gchar *debug;

		gst_message_parse_error (message, &gerror, &debug);
		if (gerror->domain == GST_STREAM_ERROR &&
		    gerror->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
			rb_debug ("caught type not found error");
		} else if (md->priv->error) {
			rb_debug ("caught error: %s, but we've already got one", gerror->message);
		} else {
			rb_debug ("caught error: %s ", gerror->message);
			
			g_clear_error (&md->priv->error);
			md->priv->error = g_error_new_literal (RB_METADATA_ERROR,
							       RB_METADATA_ERROR_GENERAL,
							       gerror->message);
		}

		g_error_free (gerror);
		g_free (debug);
		break;
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
	default:
		rb_debug ("message of type %d", GST_MESSAGE_TYPE (message));
		break;
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
			message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);
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
#endif

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
#ifdef HAVE_GSTREAMER_0_10
	GstStateChangeReturn state_ret;
	int change_timeout;
#endif

	g_free (md->priv->uri);
	md->priv->uri = NULL;
	g_free (md->priv->type);
	md->priv->type = NULL;
	md->priv->error = NULL;
	md->priv->eos = FALSE;
	md->priv->handoff = FALSE;
	md->priv->non_audio = FALSE;
	md->priv->has_video = FALSE;

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
	 * or
	 * filesrc ! decodebin ! fakesink
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

#ifdef HAVE_GSTREAMER_0_8
	g_object_set (G_OBJECT (md->priv->sink), "signal-handoffs", TRUE, NULL);
 
 	g_signal_connect_object (pipeline, "error", G_CALLBACK (rb_metadata_gst_error_cb), md, 0);
 	g_signal_connect_object (pipeline, "found-tag", G_CALLBACK (rb_metadata_gst_found_tag), md, 0);

	g_signal_connect_object (md->priv->sink, "handoff", G_CALLBACK (rb_metadata_gst_fakesink_handoff_cb), md, 0);
	g_signal_connect_object (md->priv->sink, "eos", G_CALLBACK (rb_metadata_gst_eos_cb), md, 0);
#endif
 	g_signal_connect_object (decodebin, "new-decoded-pad", G_CALLBACK (rb_metadata_gst_new_decoded_pad_cb), md, 0);
 	g_signal_connect_object (decodebin, "unknown-type", G_CALLBACK (rb_metadata_gst_unknown_type_cb), md, 0);
  
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
#ifdef HAVE_GSTREAMER_0_8
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	while (gst_bin_iterate (GST_BIN (pipeline))
	       && md->priv->error == NULL
	       && !md->priv->handoff
	       && !md->priv->eos)
		;

	if (md->priv->handoff) {
#endif
#ifdef HAVE_GSTREAMER_0_10
	rb_debug ("going to PAUSED for metadata, uri: %s", uri);
	state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
	change_timeout = 5;
        while (state_ret == GST_STATE_CHANGE_ASYNC &&
	       !md->priv->eos &&
	       !md->priv->non_audio &&
	       change_timeout > 0) {
	    GstState state;
	    rb_debug ("element state changing asynchronously: %d, %d", state_ret, state);
	    state_ret = gst_element_get_state (GST_ELEMENT (pipeline),
			    &state, NULL, 1 * GST_SECOND);
	    change_timeout--;
	}
	if (state_ret != GST_STATE_CHANGE_SUCCESS) {
		rb_debug ("failed to go to PAUSED for %s", uri);
		rb_metadata_event_loop (md, GST_ELEMENT (pipeline), FALSE);
		if (!md->priv->non_audio && md->priv->error == NULL)
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("GStreamer error: failed to change state"));
	} else
		rb_debug ("gone to PAUSED for %s", uri);

	if (state_ret == GST_STATE_CHANGE_SUCCESS) {
		/* Post application specific message so we'll know when to stop
		 * the message loop */
		GstBus *bus;
		bus = gst_element_get_bus (GST_ELEMENT (pipeline));
		if (bus) {
			gst_bus_post (bus, 
			gst_message_new_application (GST_OBJECT (pipeline), NULL));
			gst_object_unref (bus);
		}

		/* Poll the bus for messages */
		rb_metadata_event_loop (md, GST_ELEMENT (pipeline), FALSE);
#endif
		/* We caught the first buffer(0.8) or went to PAUSED (0.10),
		 * which means the decoder should have read all
		 * of the metadata, and should know the length now.
		 */
		if (g_hash_table_lookup (md->priv->metadata, GINT_TO_POINTER (RB_METADATA_FIELD_DURATION)) == NULL) {
			GstFormat format = GST_FORMAT_TIME;
			gint64 length;
			GValue *newval;
			
#ifdef HAVE_GSTREAMER_0_8
			if (gst_element_query (md->priv->sink, GST_QUERY_TOTAL, &format, &length)) {
#endif
#ifdef HAVE_GSTREAMER_0_10
			if (gst_element_query_duration (md->priv->sink, &format, &length)) {
				g_assert (format == GST_FORMAT_TIME);
#endif
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
#ifdef HAVE_GSTREAMER_0_8
	else if (md->priv->eos) {
		g_warning ("caught eos without handoff!");
	}
#endif

	/* Get the file size, since it might be interesting, and return
	 * the pipeline to NULL state.
	 */
#ifdef HAVE_GSTREAMER_0_8
	gst_element_query (urisrc, GST_QUERY_TOTAL, &file_size_format, &file_size);
	gst_element_set_state (pipeline, GST_STATE_NULL);
#endif
#ifdef HAVE_GSTREAMER_0_10
	if (gst_element_query_duration (urisrc, &file_size_format, &file_size))
		g_assert (file_size_format == GST_FORMAT_BYTES);
	
	state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
	if (state_ret == GST_STATE_CHANGE_ASYNC) {
		g_warning ("Failed to return metadata reader to NULL state");
	}
	md->priv->handoff = (state_ret == GST_STATE_CHANGE_SUCCESS);
#endif

	/* report errors for various failure cases.
	 * these don't include the URI as the import errors source
	 * already displays it.
	 */
	if (md->priv->non_audio || !md->priv->handoff) {
		gboolean ignore = FALSE;
		int i;

		if (md->priv->has_video) {
			ignore = TRUE;
		} else {
			for (i = 0; i < G_N_ELEMENTS (ignore_mime_types); i++) {
				if (g_str_has_prefix (md->priv->type, ignore_mime_types[i])) {
					ignore = TRUE;
					break;
				}
			}
		}

		if (!ignore) {
			char *msg = make_undecodable_error (md);
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_NOT_AUDIO,
				     "%s", msg);
			g_free (msg);
		} else {
			/* we don't need an error message here (it'll never be
			 * displayed).  using NULL causes crashes with some C
			 * libraries, and gcc doesn't like zero-length format
			 * strings, so we use a single space instead.
			 */
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_NOT_AUDIO_IGNORE,
				     " ");
		}
	} else if (md->priv->error) {
		g_propagate_error (error, md->priv->error);
	} else if (!md->priv->type) {
		/* ignore really small files that can't be identified */
		gint error_code = RB_METADATA_ERROR_UNRECOGNIZED;
		if (file_size > 0 && file_size < REALLY_SMALL_FILE_SIZE) {
			rb_debug ("ignoring %s because it's too small to care about", md->priv->uri);
			error_code = RB_METADATA_ERROR_NOT_AUDIO_IGNORE;
		}
		
		g_clear_error (error);
		g_set_error (error,
			     RB_METADATA_ERROR,
			     error_code,
			     _("The MIME type of the file could not be identified"));
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
	GstElement *gnomevfssrc = NULL;
        GstElement *retag_end = NULL; /* the last elemet after retagging subpipeline */
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
	md->priv->pipeline = pipeline;

#ifdef HAVE_GSTREAMER_0_8
	g_signal_connect_object (pipeline, "error", G_CALLBACK (rb_metadata_gst_error_cb), md, 0);
#endif

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
#if HAVE_GSTREAMER_0_8
	g_signal_connect_object (md->priv->sink, "eos", G_CALLBACK (rb_metadata_gst_eos_cb), md, 0);
#endif

	md->priv->tags = gst_tag_list_new ();
	g_hash_table_foreach (md->priv->metadata, 
			      (GHFunc) rb_metadata_gst_add_tag_data,
			      md);

	/* Tagger element(s) */
	add_tagger_func = rb_metadata_gst_type_to_tag_function (md, md->priv->type);
	
	if (!add_tagger_func) {
		g_set_error (error,
			     RB_METADATA_ERROR,
			     RB_METADATA_ERROR_UNSUPPORTED,
			     _("Unsupported file type: %s"), md->priv->type);
		goto out_error;
	}

	retag_end = add_tagger_func (md, gnomevfssrc);
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

#ifdef HAVE_GSTREAMER_0_8
	while (gst_bin_iterate (GST_BIN (pipeline))
	       && md->priv->error == NULL
	       && !md->priv->eos)
		;
	gst_element_set_state (pipeline, GST_STATE_NULL);
#endif
#ifdef HAVE_GSTREAMER_0_10
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
#endif

	if (md->priv->error) {
		g_propagate_error (error, md->priv->error);
		goto out_error;
	}
	if (handle != NULL) {
		if ((result = gnome_vfs_close (handle)) != GNOME_VFS_OK)
			goto vfs_error;
		handle = NULL;

		/* check to ensure the file isn't corrupt */
		if (!rb_metadata_file_valid (md->priv->uri, tmpname)) {
			g_set_error (error,
				     RB_METADATA_ERROR,
				     RB_METADATA_ERROR_INTERNAL,
				     _("File corrupted during write"));
			goto out_error;
		}
		
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

