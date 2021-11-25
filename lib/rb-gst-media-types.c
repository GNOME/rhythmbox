/*
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include "config.h"

#include <memory.h>

#include <gst/pbutils/encoding-target.h>

#include <lib/rb-gst-media-types.h>
#include <lib/rb-debug.h>
#include <lib/rb-file-helpers.h>

/* don't like this much, but it's all we can do for now.
 * these media types are copied from gst-plugins-base/gst-libs/gst/pbutils/descriptions.c.
 * these are only the media types that don't start with 'audio/' or 'video/', which are
 * identified fairly accurately by the filters for those prefixes.
 */
static const char *container_formats[] = {
	"application/ogg",
	"application/vnd.rn-realmedia",
	"application/x-id3",
	"application/x-ape",
	"application/x-icy"
};

#define ENCODING_TARGET_FILE "rhythmbox.gep"
static GstEncodingTarget *default_target = NULL;
static GKeyFile *target_keyfile = NULL;

#define ENCODER_STYLE_SETTINGS_PREFIX "rhythmbox-encoding-"

RBGstMediaType
rb_gst_get_missing_plugin_type (const GstStructure *structure)
{
	const char *media_type;
	const char *missing_type;
	const GstCaps *caps;
	const GValue *val;
	int i;

	if (structure == NULL) {
		rb_debug ("no missing plugin details");
		return MEDIA_TYPE_NONE;
	}

	missing_type = gst_structure_get_string (structure, "type");
	if (missing_type == NULL || strcmp (missing_type, "decoder") != 0) {
		rb_debug ("missing plugin is not a decoder");
		return MEDIA_TYPE_NONE;
	}

	val = gst_structure_get_value (structure, "detail");
	caps = gst_value_get_caps (val);

	media_type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
	for (i = 0; i < G_N_ELEMENTS (container_formats); i++) {
		if (strcmp (media_type, container_formats[i]) == 0) {
			rb_debug ("missing plugin is a container demuxer");
			return MEDIA_TYPE_CONTAINER;
		}
	}

	if (g_str_has_prefix (media_type, "audio/")) {
		rb_debug ("missing plugin is an audio decoder");
		return MEDIA_TYPE_AUDIO;
	} else if (g_str_has_prefix (media_type, "video/")) {
		rb_debug ("missing plugin is (probably) a video decoder");
		return MEDIA_TYPE_VIDEO;
	} else {
		rb_debug ("missing plugin is neither a video nor audio decoder");
		return MEDIA_TYPE_OTHER;
	}
}

char *
rb_gst_caps_to_media_type (const GstCaps *caps)
{
	GstStructure *s;
	const char *media_type;

	/* the aim here is to reduce the caps to a single mimetype-like
	 * string, describing the audio encoding (for audio files) or the
	 * file type (for everything else).  raw media types are ignored.
	 *
	 * there are only a couple of special cases.
	 */

	if (gst_caps_get_size (caps) == 0)
		return NULL;

	s = gst_caps_get_structure (caps, 0);
	media_type = gst_structure_get_name (s);
	if (media_type == NULL) {
		return NULL;
	} else if (g_str_has_prefix (media_type, "audio/x-raw") ||
	    g_str_has_prefix (media_type, "video/x-raw")) {
		/* ignore raw types */
		return NULL;
	} else if (g_str_equal (media_type, "audio/mpeg")) {
		/* need to distinguish between mpeg 1 layer 3 and
		 * mpeg 2 or 4 here.
		 */
		int mpegversion = 0;
		gst_structure_get_int (s, "mpegversion", &mpegversion);
		switch (mpegversion) {
		case 2:
		case 4:
			return g_strdup ("audio/x-aac");		/* hmm. */

		case 1:
		default:
			return g_strdup ("audio/mpeg");
		}
	} else {
		/* everything else is fine as-is */
		return g_strdup (media_type);
	}
}

GstCaps *
rb_gst_media_type_to_caps (const char *media_type)
{
	/* special cases: */
	if (strcmp (media_type, "audio/mpeg") == 0) {
		return gst_caps_from_string ("audio/mpeg, mpegversion=(int)1");
	} else if (strcmp (media_type, "audio/x-aac") == 0) {
		return gst_caps_from_string ("audio/mpeg, mpegversion=(int){ 2, 4 }");
	} else {
		/* otherwise, the media type is enough */
		return gst_caps_from_string (media_type);
	}
}

const char *
rb_gst_media_type_to_extension (const char *media_type)
{
	if (media_type == NULL) {
		return NULL;
	} else if (!strcmp (media_type, "audio/mpeg")) {
		return "mp3";
	} else if (!strcmp (media_type, "audio/x-vorbis") || !strcmp (media_type, "application/ogg") || !strcmp (media_type, "audio/ogg")) {
		return "ogg";
	} else if (!strcmp (media_type, "audio/x-opus")) {
		return "opus";
	} else if (!strcmp (media_type, "audio/x-flac") || !strcmp (media_type, "audio/flac")) {
		return "flac";
	} else if (!strcmp (media_type, "audio/x-aac") || !strcmp (media_type, "audio/aac") || !strcmp (media_type, "audio/x-alac")) {
		return "m4a";
	} else if (!strcmp (media_type, "audio/x-wavpack")) {
		return "wv";
	} else {
		return NULL;
	}
}

const char *
rb_gst_mime_type_to_media_type (const char *mime_type)
{
	if (!strcmp (mime_type, "application/x-id3") || !strcmp (mime_type, "audio/mpeg")) {
		return "audio/mpeg";
	} else if (!strcmp (mime_type, "application/ogg") || !strcmp (mime_type, "audio/x-vorbis") || !strcmp (mime_type, "audio/ogg")) {
		return "audio/x-vorbis";
	} else if (!strcmp (mime_type, "audio/flac")) {
		return "audio/x-flac";
	} else if (!strcmp (mime_type, "audio/aac") || !strcmp (mime_type, "audio/mp4") || !strcmp (mime_type, "audio/m4a")) {
		return "audio/x-aac";
	}
	return mime_type;
}

const char *
rb_gst_media_type_to_mime_type (const char *media_type)
{
	if (!strcmp (media_type, "audio/x-vorbis")) {
		return "application/ogg";
	} else if (!strcmp (media_type, "audio/x-flac")) {
		return "audio/flac";
	} else if (!strcmp (media_type, "audio/x-aac")) {
		return "audio/mp4";	/* probably */
	} else {
		return media_type;
	}
}

gboolean
rb_gst_media_type_matches_profile (GstEncodingProfile *profile, const char *media_type)
{
	const GstCaps *pcaps;
	const GList *cl;
	GstCaps *caps;
	gboolean matches = FALSE;

	caps = rb_gst_media_type_to_caps (media_type);
	if (caps == NULL) {
		return FALSE;
	}

	pcaps = gst_encoding_profile_get_format (profile);
	if (gst_caps_can_intersect (caps, pcaps)) {
		matches = TRUE;
	}

	if (matches == FALSE && GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
		for (cl = gst_encoding_container_profile_get_profiles (GST_ENCODING_CONTAINER_PROFILE (profile)); cl != NULL; cl = cl->next) {
			GstEncodingProfile *cp = cl->data;
			pcaps = gst_encoding_profile_get_format (cp);
			if (gst_caps_can_intersect (caps, pcaps)) {
				matches = TRUE;
				break;
			}
		}
	}
	gst_caps_unref (caps);

	return matches;
}

char *
rb_gst_encoding_profile_get_media_type (GstEncodingProfile *profile)
{
	if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
		const GList *cl = gst_encoding_container_profile_get_profiles (GST_ENCODING_CONTAINER_PROFILE (profile));
		for (; cl != NULL; cl = cl->next) {
			GstEncodingProfile *p = cl->data;
			if (GST_IS_ENCODING_AUDIO_PROFILE (p)) {
				return rb_gst_caps_to_media_type (gst_encoding_profile_get_format (p));
			}

		}

		/* now what? */
		return NULL;
	} else {
		return rb_gst_caps_to_media_type (gst_encoding_profile_get_format (profile));
	}
}

static char *
get_encoding_target_file (void)
{
	char *target_file;

	target_file = rb_find_user_data_file (ENCODING_TARGET_FILE);
	if (g_file_test (target_file, G_FILE_TEST_EXISTS) == FALSE) {
		g_free (target_file);
		target_file = g_strdup (rb_file (ENCODING_TARGET_FILE));
	}

	return target_file;
}

/**
 * rb_gst_get_default_encoding_target:
 *
 * Return value: (transfer none): default encoding target
 */
GstEncodingTarget *
rb_gst_get_default_encoding_target (void)
{
	if (default_target == NULL) {
		char *target_file;
		GError *error = NULL;

		target_file = get_encoding_target_file ();
		default_target = gst_encoding_target_load_from_file (target_file, &error);
		if (default_target == NULL) {
			g_warning ("Unable to load encoding profiles from %s: %s", target_file, error ? error->message : "no error");
			g_clear_error (&error);
			g_free (target_file);
			return NULL;
		}
		g_free (target_file);
	}

	return default_target;
}

/**
 * rb_gst_get_encoding_profile:
 * @media_type: media type to get a profile for
 *
 * Return value: (transfer full): encoding profile
 */
GstEncodingProfile *
rb_gst_get_encoding_profile (const char *media_type)
{
	const GList *l;
	GstEncodingTarget *target;
	target = rb_gst_get_default_encoding_target ();

	for (l = gst_encoding_target_get_profiles (target); l != NULL; l = l->next) {
		GstEncodingProfile *profile = l->data;
		if (rb_gst_media_type_matches_profile (profile, media_type)) {
			gst_encoding_profile_ref (profile);
			return profile;
		}
	}

	return NULL;
}

gboolean
rb_gst_media_type_is_lossless (const char *media_type)
{
	int i;
	const char *lossless_types[] = {
		"audio/x-flac",
		"audio/x-alac",
		"audio/x-shorten",
		"audio/x-wavpack"	/* not completely sure */
	};

	for (i = 0; i < G_N_ELEMENTS (lossless_types); i++) {
		if (strcmp (media_type, lossless_types[i]) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static GstEncodingProfile *
get_audio_encoding_profile (GstEncodingProfile *profile)
{
	if (GST_IS_ENCODING_AUDIO_PROFILE (profile)) {
		return profile;
	} else if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
		const GList *l = gst_encoding_container_profile_get_profiles (GST_ENCODING_CONTAINER_PROFILE (profile));
		for (; l != NULL; l = l->next) {
			GstEncodingProfile *p = get_audio_encoding_profile (l->data);
			if (p != NULL) {
				return p;
			}
		}
	}

	g_warning ("no audio encoding profile in profile %s", gst_encoding_profile_get_name (profile));
	return NULL;
}

static GstElementFactory *
get_audio_encoder_factory (GstEncodingProfile *profile)
{
	GstEncodingProfile *p = get_audio_encoding_profile (profile);
	GstElementFactory *f;
	GList *l;
	GList *fl;

	if (p == NULL)
		return NULL;

	l = gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_ENCODER, GST_RANK_MARGINAL);
	fl = gst_element_factory_list_filter (l, gst_encoding_profile_get_format (p), GST_PAD_SRC, FALSE);

	if (fl != NULL) {
		f = gst_object_ref (fl->data);
	} else {
		f = NULL;
	}
	gst_plugin_feature_list_free (l);
	gst_plugin_feature_list_free (fl);
	return f;
}

/**
 * rb_gst_encoding_profile_set_preset:
 * @profile: a #GstEncodingProfile
 * @preset: preset to apply
 *
 * Applies the preset @preset to the audio encoding profile within @profile.
 */
void
rb_gst_encoding_profile_set_preset (GstEncodingProfile *profile, const char *preset)
{
	GstEncodingProfile *p;

	p = get_audio_encoding_profile (profile);
	if (p != NULL) {
		gst_encoding_profile_set_preset (p, preset);
	}
}

static GKeyFile *
get_target_keyfile (void)
{
	if (target_keyfile == NULL) {
		char *file = get_encoding_target_file ();
		GError *error = NULL;

		target_keyfile = g_key_file_new ();
		g_key_file_set_list_separator (target_keyfile, ',');
		g_key_file_load_from_file (target_keyfile, file, G_KEY_FILE_NONE, &error);
		if (error != NULL) {
			g_warning ("Unable to load encoding target keyfile %s: %s", file, error->message);
			g_clear_error (&error);
		}
	}

	return target_keyfile;
}

/**
 * rb_gst_encoding_profile_get_settings:
 * @profile: a #GstEncodingProfile
 * @style: encoding style (NULL or "cbr" or "vbr")
 *
 * Returns a list of settings for the profile @profile that can usefully
 * be exposed to a user.  This usually means just bitrate/quality settings.
 * This works by finding the name of the encoder element for the profile
 * and retrieving a list specific to that encoder.
 *
 * Return value: (transfer full): list of settings
 */
char **
rb_gst_encoding_profile_get_settings (GstEncodingProfile *profile, const char *style)
{
	GstElementFactory *factory;
	char **setting_names;
	char *key_name;

	factory = get_audio_encoder_factory (profile);
	if (factory == NULL) {
		return NULL;
	}

	/* look up list of settings;
	 * if we don't have one for the encoder, what do we do?  return everything?
	 */

	if (style == NULL) {
		key_name = g_strdup (gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
	} else {
		key_name = g_strdup_printf ("%s-%s",
					    gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
					    style);
	}

	setting_names = g_key_file_get_string_list (get_target_keyfile (),
						    "rhythmbox-encoder-settings",
						    key_name,
						    NULL,
						    NULL);
	g_free (key_name);
	return setting_names;
}

/**
 * rb_gst_encoding_profile_get_encoder:
 * @profile: a #GstEncodingProfile
 *
 * Return value: (transfer full): an encoder element instance
 */
GstElement *
rb_gst_encoding_profile_get_encoder (GstEncodingProfile *profile)
{
	GstElementFactory *factory;

	factory = get_audio_encoder_factory (profile);
	if (factory == NULL) {
		return NULL;
	}

	return gst_element_factory_create (factory, NULL);
}

/**
 * rb_gst_encoding_profile_get_encoder_caps:
 * @profile: a #GstEncodingProfile
 *
 * Return value: (transfer full): output caps for the encoder
 */
GstCaps *
rb_gst_encoding_profile_get_encoder_caps (GstEncodingProfile *profile)
{
	GstEncodingProfile *p = get_audio_encoding_profile (profile);
	if (p != NULL)
		return gst_encoding_profile_get_format (p);

	return NULL;
}

/**
 * rb_gst_encoding_profile_get_presets:
 * @profile: profile to return presets for
 *
 * Return value: (transfer full) (array zero-terminated=1) (element-type gchar *): preset names
 */
char **
rb_gst_encoding_profile_get_presets (GstEncodingProfile *profile)
{
	GstElement *encoder;
	char **presets = NULL;

	encoder = rb_gst_encoding_profile_get_encoder (profile);
	if (encoder != NULL && GST_IS_PRESET (encoder)) {
		presets = gst_preset_get_preset_names (GST_PRESET (encoder));
		g_object_unref (encoder);
	}
	return presets;
}

gboolean
rb_gst_encoder_set_encoding_style (GstElement *encoder, const char *style)
{
	GstElementFactory *factory;
	const char *element_name;
	char *group_name;
	char **keys;
	int i;

	factory = gst_element_get_factory (encoder);
	element_name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
	group_name = g_strdup_printf (ENCODER_STYLE_SETTINGS_PREFIX "%s-%s",
				      element_name,
				      style);
	rb_debug ("applying settings from %s", group_name);

	keys = g_key_file_get_keys (get_target_keyfile (), group_name, NULL, NULL);
	if (keys == NULL) {
		rb_debug ("nothing to apply");
		g_free (group_name);
		return FALSE;
	}

	for (i = 0; keys[i] != NULL; i++) {
		GParamSpec *pspec;
		GValue v = {0,};
		char *value;

		pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (encoder), keys[i]);
		if (pspec == NULL) {
			g_warning ("couldn't find encoder property %s:%s", element_name, keys[i]);
			continue;
		}

		value = g_key_file_get_string (get_target_keyfile (), group_name, keys[i], NULL);

		/* g_key_file_get_string () returns an empty string on empty value */
		if (value[0] == '\0') {
			g_warning ("no value specified for encoder property %s:%s", element_name, keys[i]);
			/* we continue, as we don't want implicit gvalue conversions to set property values */
			continue;
		}

		g_value_init (&v, pspec->value_type);
		if (gst_value_deserialize (&v, value)) {
			rb_debug ("applying value \"%s\" to property %s:%s", value, element_name, keys[i]);
			g_object_set_property (G_OBJECT (encoder), keys[i], &v);
		} else {
			g_warning ("couldn't deserialize value \"%s\" for encoder property %s:%s", value, element_name, keys[i]);
		}

		g_value_unset (&v);
	}

	g_strfreev (keys);
	g_free (group_name);
	return TRUE;
}
