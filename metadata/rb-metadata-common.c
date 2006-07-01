/*
 *  arch-tag: Implementation of common metadata functions
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include "rb-metadata.h"
#include "rb-debug.h"

struct RBMetaDataFieldInfo {
	GType type;
	const char *name;
};

/*
 * Note: the field names are the same as those used by GStreamer.  We
 * could have just use the GST_TAG_X defines, but that would suck if we
 * ever got a non-GStreamer metadata backend.
 */
static struct RBMetaDataFieldInfo field_info[RB_METADATA_FIELD_LAST] = {
	/* RB_METADATA_FIELD_TITLE */ 			{ G_TYPE_STRING, "title" },
	/* RB_METADATA_FIELD_ARTIST */ 			{ G_TYPE_STRING, "artist" },
	/* RB_METADATA_FIELD_ALBUM */ 			{ G_TYPE_STRING, "album" },
	/* RB_METADATA_FIELD_DATE */ 			{ G_TYPE_ULONG, "date" },
	/* RB_METADATA_FIELD_GENRE */ 			{ G_TYPE_STRING, "genre" },
	/* RB_METADATA_FIELD_COMMENT */			{ G_TYPE_STRING, "comment" },
	/* RB_METADATA_FIELD_TRACK_NUMBER */		{ G_TYPE_ULONG, "track-number" },
	/* RB_METADATA_FIELD_MAX_TRACK_NUMBER */	{ G_TYPE_ULONG, "track-count" },
	/* RB_METADATA_FIELD_DISC_NUMBER */ 		{ G_TYPE_ULONG, "album-disc-number" },
	/* RB_METADATA_FIELD_MAX_DISC_NUMBER */ 	{ G_TYPE_ULONG, "album-disc-count" },
	/* RB_METADATA_FIELD_DESCRIPTION */ 		{ G_TYPE_STRING, "description" },
	/* RB_METADATA_FIELD_VERSION */ 		{ G_TYPE_STRING, "version" },
	/* RB_METADATA_FIELD_IRSC */	 		{ G_TYPE_STRING, "isrc" },
	/* RB_METADATA_FIELD_ORGANIZATION */ 		{ G_TYPE_STRING, "organization" },
	/* RB_METADATA_FIELD_COPYRIGHT */ 		{ G_TYPE_STRING, "copyright" },
	/* RB_METADATA_FIELD_CONTACT */ 		{ G_TYPE_STRING, "contact" },
	/* RB_METADATA_FIELD_LICENSE */ 		{ G_TYPE_STRING, "license" },
	/* RB_METADATA_FIELD_PERFORMER */ 		{ G_TYPE_STRING, "performer" },
	/* RB_METADATA_FIELD_DURATION */ 		{ G_TYPE_ULONG, "duration" },
	/* RB_METADATA_FIELD_CODEC */	 		{ G_TYPE_STRING, "codec" },
	/* RB_METADATA_FIELD_BITRATE */ 		{ G_TYPE_ULONG, "bitrate" },
	/* RB_METADATA_FIELD_TRACK_GAIN */ 		{ G_TYPE_DOUBLE, "replaygain-track-gain" },
	/* RB_METADATA_FIELD_TRACK_PEAK */ 		{ G_TYPE_DOUBLE, "replaygain-track-peak" },
	/* RB_METADATA_FIELD_ALBUM_GAIN */ 		{ G_TYPE_DOUBLE, "replaygain-album-gain" },
	/* RB_METADATA_FIELD_ALBUM_PEAK */ 		{ G_TYPE_DOUBLE, "replaygain-album-peak" },
	/* RB_METADATA_FIELD_LANGUAGE_CODE */		{ G_TYPE_STRING, "language-code" },
	/* RB_METADATA_FIELD_MUSICBRAINZ-TRACKID */	{ G_TYPE_STRING, "musicbrainz-trackid" }
};

GType
rb_metadata_get_field_type (RBMetaDataField field)
{
	g_assert (field >= 0 && field < RB_METADATA_FIELD_LAST);
	return field_info[field].type;
}

const char *
rb_metadata_get_field_name (RBMetaDataField field)
{
	g_assert (field >= 0 && field < RB_METADATA_FIELD_LAST);
	return field_info[field].name;
}

GQuark
rb_metadata_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_metadata_error");

	return quark;
}

