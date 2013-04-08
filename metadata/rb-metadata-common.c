/*
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include "rb-metadata.h"
#include "rb-debug.h"

/**
 * RBMetaDataField:
 * @RB_METADATA_FIELD_TITLE: Title of the recording
 * @RB_METADATA_FIELD_ARTIST: Person(s) responsible for the recording
 * @RB_METADATA_FIELD_ALBUM: Album containing the recording
 * @RB_METADATA_FIELD_DATE: Release date of the album
 * @RB_METADATA_FIELD_GENRE: Genre of the recording
 * @RB_METADATA_FIELD_COMMENT: Free form comment on the recording
 * @RB_METADATA_FIELD_TRACK_NUMBER: Track number inside a collection
 * @RB_METADATA_FIELD_MAX_TRACK_NUMBER: Count of tracks inside the collection
 * @RB_METADATA_FIELD_DISC_NUMBER: Disc number inside a collection
 * @RB_METADATA_FIELD_MAX_DISC_NUMBER: Count of discs inside the collection
 * @RB_METADATA_FIELD_DESCRIPTION: Short text describing the recording
 * @RB_METADATA_FIELD_VERSION: Version of the recording
 * @RB_METADATA_FIELD_ISRC: International Standard Recording Code
 * @RB_METADATA_FIELD_ORGANIZATION: Organization responsible for the recording
 * @RB_METADATA_FIELD_COPYRIGHT: Copyright notice on the recording
 * @RB_METADATA_FIELD_CONTACT: Contact information
 * @RB_METADATA_FIELD_LICENSE: License of the recording
 * @RB_METADATA_FIELD_PERFORMER: Person(s) performing in the recording
 * @RB_METADATA_FIELD_DURATION: Duration of the recording
 * @RB_METADATA_FIELD_CODEC: Codec used to store the recording
 * @RB_METADATA_FIELD_BITRATE: Exact or average encoding bitrate in bits/s
 * @RB_METADATA_FIELD_TRACK_GAIN: Track gain in dB for replaygain
 * @RB_METADATA_FIELD_TRACK_PEAK: Track peak volume level
 * @RB_METADATA_FIELD_ALBUM_GAIN: Album gain in dB for replaygain
 * @RB_METADATA_FIELD_ALBUM_PEAK: Album peak volume level
 * @RB_METADATA_FIELD_BPM: Beats Per Minute
 * @RB_METADATA_FIELD_LANGUAGE_CODE: Language code (ISO-639-1)
 * @RB_METADATA_FIELD_MUSICBRAINZ_TRACKID: MusicBrainz track ID
 * @RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID: MusicBrainz artist ID
 * @RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID: MusicBrainz album ID
 * @RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID: MusicBrainz album artist ID
 * @RB_METADATA_FIELD_ARTIST_SORTNAME: Person(s) responsible for the recording, as used for sorting
 * @RB_METADATA_FIELD_ALBUM_SORTNAME: Album containing the recording, as used for sorting
 * @RB_METADATA_FIELD_ALBUM_ARTIST: The artist of the entire album
 * @RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME: The artist of the entire album, as it should be sorted
 * @RB_METADATA_FIELD_COMPOSER: The composer of the recording
 * @RB_METADATA_FIELD_COMPOSER_SORTNAME: The composer of the recording, as it should be sorted
 * @RB_METADATA_FIELD_LAST:  invalid field
 *
 * Metadata fields that can be read from and written to files.
 */

/**
 * rb_metadata_get_field_type:
 * @field: a #RBMetaDataField
 *
 * Returns the #GType of the value for a metadata field.
 *
 * Return value: value type
 */
GType
rb_metadata_get_field_type (RBMetaDataField field)
{
	switch (field) {
	case RB_METADATA_FIELD_TITLE:
	case RB_METADATA_FIELD_ARTIST:
	case RB_METADATA_FIELD_ALBUM:
	case RB_METADATA_FIELD_GENRE:
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_DESCRIPTION:
	case RB_METADATA_FIELD_VERSION:
	case RB_METADATA_FIELD_ISRC:
	case RB_METADATA_FIELD_ORGANIZATION:
	case RB_METADATA_FIELD_COPYRIGHT:
	case RB_METADATA_FIELD_CONTACT:
	case RB_METADATA_FIELD_LICENSE:
	case RB_METADATA_FIELD_PERFORMER:
	case RB_METADATA_FIELD_CODEC:
	case RB_METADATA_FIELD_LANGUAGE_CODE:
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
	case RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID:
	case RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID:
	case RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID:
	case RB_METADATA_FIELD_ARTIST_SORTNAME:
	case RB_METADATA_FIELD_ALBUM_SORTNAME:
	case RB_METADATA_FIELD_ALBUM_ARTIST:
	case RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME:
	case RB_METADATA_FIELD_COMPOSER:
	case RB_METADATA_FIELD_COMPOSER_SORTNAME:
		return G_TYPE_STRING;

	case RB_METADATA_FIELD_DATE:
	case RB_METADATA_FIELD_TRACK_NUMBER:
	case RB_METADATA_FIELD_MAX_TRACK_NUMBER:
	case RB_METADATA_FIELD_DISC_NUMBER:
	case RB_METADATA_FIELD_MAX_DISC_NUMBER:
	case RB_METADATA_FIELD_DURATION:
	case RB_METADATA_FIELD_BITRATE:
		return G_TYPE_ULONG;

	case RB_METADATA_FIELD_TRACK_GAIN:
	case RB_METADATA_FIELD_TRACK_PEAK:
	case RB_METADATA_FIELD_ALBUM_GAIN:
	case RB_METADATA_FIELD_ALBUM_PEAK:
	case RB_METADATA_FIELD_BPM:
		return G_TYPE_DOUBLE;

	default:
		g_assert_not_reached ();
	}
}

/**
 * rb_metadata_get_field_name:
 * @field: a #RBMetaDataField
 *
 * Returns the name of a metadata field.
 *
 * Return value: field name
 */
const char *
rb_metadata_get_field_name (RBMetaDataField field)
{
	GEnumClass *klass;

	klass = g_type_class_ref (RB_TYPE_METADATA_FIELD);
	g_assert (field >= 0 && field < klass->n_values);
	return klass->values[field].value_nick;
}

GQuark
rb_metadata_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_metadata_error");

	return quark;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }


GType
rb_metadata_field_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			/* Note: field names are the GStreamer tag names.
			 * We could have just used the GST_TAG_X defines, but that
			 * would suck if we ever got a non-GStreamer metadata backend.
			 */
			ENUM_ENTRY (RB_METADATA_FIELD_TITLE, "title"),
			ENUM_ENTRY (RB_METADATA_FIELD_ARTIST, "artist"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM, "album"),
			ENUM_ENTRY (RB_METADATA_FIELD_DATE, "date"),
			ENUM_ENTRY (RB_METADATA_FIELD_GENRE, "genre"),
			ENUM_ENTRY (RB_METADATA_FIELD_COMMENT, "comment"),
			ENUM_ENTRY (RB_METADATA_FIELD_TRACK_NUMBER, "track-number"),
			ENUM_ENTRY (RB_METADATA_FIELD_MAX_TRACK_NUMBER, "track-count"),
			ENUM_ENTRY (RB_METADATA_FIELD_DISC_NUMBER, "album-disc-number"),
			ENUM_ENTRY (RB_METADATA_FIELD_MAX_DISC_NUMBER, "album-disc-count"),
			ENUM_ENTRY (RB_METADATA_FIELD_DESCRIPTION, "description"),
			ENUM_ENTRY (RB_METADATA_FIELD_VERSION, "version"),
			ENUM_ENTRY (RB_METADATA_FIELD_ISRC, "isrc"),
			ENUM_ENTRY (RB_METADATA_FIELD_ORGANIZATION, "organization"),
			ENUM_ENTRY (RB_METADATA_FIELD_COPYRIGHT, "copyright"),
			ENUM_ENTRY (RB_METADATA_FIELD_CONTACT, "contact"),
			ENUM_ENTRY (RB_METADATA_FIELD_LICENSE, "license"),
			ENUM_ENTRY (RB_METADATA_FIELD_PERFORMER, "performer"),
			ENUM_ENTRY (RB_METADATA_FIELD_DURATION, "duration"),
			ENUM_ENTRY (RB_METADATA_FIELD_CODEC, "codec"),
			ENUM_ENTRY (RB_METADATA_FIELD_BITRATE, "bitrate"),
			ENUM_ENTRY (RB_METADATA_FIELD_TRACK_GAIN, "replaygain-track-gain"),
			ENUM_ENTRY (RB_METADATA_FIELD_TRACK_PEAK, "replaygain-track-peak"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM_GAIN, "replaygain-album-gain"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM_PEAK, "replaygain-album-peak"),
			ENUM_ENTRY (RB_METADATA_FIELD_BPM, "beats-per-minute"),
			ENUM_ENTRY (RB_METADATA_FIELD_LANGUAGE_CODE, "language-code"),
			ENUM_ENTRY (RB_METADATA_FIELD_MUSICBRAINZ_TRACKID, "musicbrainz-trackid"),
			ENUM_ENTRY (RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID, "musicbrainz-artistid"),
			ENUM_ENTRY (RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID, "musicbrainz-albumid"),
			ENUM_ENTRY (RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID, "musicbrainz-albumartistid"),
			ENUM_ENTRY (RB_METADATA_FIELD_ARTIST_SORTNAME, "musicbrainz-sortname"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM_SORTNAME, "album-sortname"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM_ARTIST, "album-artist"),
			ENUM_ENTRY (RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME, "album-artist-sortname"),
			ENUM_ENTRY (RB_METADATA_FIELD_COMPOSER, "composer"),
			ENUM_ENTRY (RB_METADATA_FIELD_COMPOSER_SORTNAME, "composer-sortname"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("RBMetadataFieldType", values);
	}

	return etype;
}

GType
rb_metadata_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY(RB_METADATA_ERROR_IO, "io-error"),
			ENUM_ENTRY(RB_METADATA_ERROR_MISSING_PLUGIN, "missing-plugins"),
			ENUM_ENTRY(RB_METADATA_ERROR_UNRECOGNIZED, "identify-failed"),
			ENUM_ENTRY(RB_METADATA_ERROR_UNSUPPORTED, "unsupported-filetype"),
			ENUM_ENTRY(RB_METADATA_ERROR_GENERAL, "general-error"),
			ENUM_ENTRY(RB_METADATA_ERROR_INTERNAL, "internal-error"),
			ENUM_ENTRY(RB_METADATA_ERROR_EMPTY_FILE, "empty-file"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("RBMetadataErrorType", values);
	}

	return etype;
}
