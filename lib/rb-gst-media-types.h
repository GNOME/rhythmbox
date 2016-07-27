/*
 *  Copyright (C) 2010  Jonathan Matthew <jonathan@d14n.org>
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

#ifndef __RB_GST_MEDIA_TYPES_H
#define __RB_GST_MEDIA_TYPES_H

#include <gst/gst.h>
#include <gst/pbutils/encoding-target.h>
#include <gst/pbutils/encoding-profile.h>

G_BEGIN_DECLS

/* some common media types */
#define RB_GST_MEDIA_TYPE_MP3		"audio/mpeg"
#define RB_GST_MEDIA_TYPE_OGG_VORBIS 	"audio/x-vorbis"
#define RB_GST_MEDIA_TYPE_FLAC 		"audio/x-flac"
#define RB_GST_MEDIA_TYPE_AAC 		"audio/x-aac"

/* media type categories */
typedef enum {
	MEDIA_TYPE_NONE = 0,
	MEDIA_TYPE_CONTAINER,
	MEDIA_TYPE_AUDIO,
	MEDIA_TYPE_VIDEO,
	MEDIA_TYPE_OTHER
} RBGstMediaType;

/* encoding styles */
#define RB_ENCODING_STYLE_CBR		"cbr"
#define RB_ENCODING_STYLE_VBR		"vbr"

char *		rb_gst_caps_to_media_type (const GstCaps *caps);
GstCaps *	rb_gst_media_type_to_caps (const char *media_type);

const char *	rb_gst_media_type_to_extension (const char *media_type);

const char *	rb_gst_mime_type_to_media_type (const char *mime_type);

const char *	rb_gst_media_type_to_mime_type (const char *media_type);

RBGstMediaType	rb_gst_get_missing_plugin_type (const GstStructure *structure);

GstEncodingTarget *rb_gst_get_default_encoding_target (void);

GstEncodingProfile *rb_gst_get_encoding_profile (const char *media_type);

gboolean	rb_gst_media_type_matches_profile (GstEncodingProfile *profile, const char *media_type);

char *		rb_gst_encoding_profile_get_media_type (GstEncodingProfile *profile);

gboolean	rb_gst_media_type_is_lossless (const char *media_type);

char **		rb_gst_encoding_profile_get_settings (GstEncodingProfile *profile, const char *style);
char **		rb_gst_encoding_profile_get_presets (GstEncodingProfile *profile);
void		rb_gst_encoding_profile_set_preset (GstEncodingProfile *profile, const char *preset);
gboolean	rb_gst_encoder_set_encoding_style (GstElement *element, const char *style);

GstElement *	rb_gst_encoding_profile_get_encoder (GstEncodingProfile *profile);
GstCaps *	rb_gst_encoding_profile_get_encoder_caps (GstEncodingProfile *profile);

G_END_DECLS

#endif /* __RB_GST_MEDIA_TYPES_H */
