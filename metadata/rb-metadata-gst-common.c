/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
#include <gst/gst.h>
#include <gst/tag/tag.h>

#include "rb-metadata-gst-common.h"
#include "rb-debug.h"

RBMetaDataField
rb_metadata_gst_tag_to_field (const char *tag)
{
	if (!strcmp (tag, GST_TAG_TITLE))
		return RB_METADATA_FIELD_TITLE;
	else if (!strcmp (tag, GST_TAG_ARTIST))
		return RB_METADATA_FIELD_ARTIST;
	else if (!strcmp (tag, GST_TAG_ALBUM))
		return RB_METADATA_FIELD_ALBUM;
	else if (!strcmp (tag, GST_TAG_DATE_TIME))
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
		return RB_METADATA_FIELD_MAX_DISC_NUMBER;
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
	else if (!strcmp (tag, GST_TAG_BEATS_PER_MINUTE))
		return RB_METADATA_FIELD_BPM;
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID))
		return RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_ARTISTID))
		return RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID;
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_ALBUMID))
		return RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID;
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_ALBUMARTISTID))
		return RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID;
	else if (!strcmp (tag, GST_TAG_ARTIST_SORTNAME))
		return RB_METADATA_FIELD_ARTIST_SORTNAME;
	else if (!strcmp (tag, GST_TAG_ALBUM_SORTNAME))
		return RB_METADATA_FIELD_ALBUM_SORTNAME;
	else if (!strcmp (tag, GST_TAG_ALBUM_ARTIST))
		return RB_METADATA_FIELD_ALBUM_ARTIST;
	else if (!strcmp (tag, GST_TAG_ALBUM_ARTIST_SORTNAME))
		return RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME;
	else if (!strcmp (tag, GST_TAG_COMPOSER))
		return RB_METADATA_FIELD_COMPOSER;
	else if (!strcmp (tag, GST_TAG_COMPOSER_SORTNAME))
		return RB_METADATA_FIELD_COMPOSER_SORTNAME;
	else
		return -1;
}

const char *
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
		return GST_TAG_DATE_TIME;
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
	case RB_METADATA_FIELD_BPM:
		return GST_TAG_BEATS_PER_MINUTE;
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
		return GST_TAG_MUSICBRAINZ_TRACKID;
	case RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID:
		return GST_TAG_MUSICBRAINZ_ARTISTID;
	case RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID:
		return GST_TAG_MUSICBRAINZ_ALBUMID;
	case RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID:
		return GST_TAG_MUSICBRAINZ_ALBUMARTISTID;
	case RB_METADATA_FIELD_ARTIST_SORTNAME:
		return GST_TAG_ARTIST_SORTNAME;
	case RB_METADATA_FIELD_ALBUM_SORTNAME:
		return GST_TAG_ALBUM_SORTNAME;
	case RB_METADATA_FIELD_ALBUM_ARTIST:
		return GST_TAG_ALBUM_ARTIST;
	case RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME:
		return GST_TAG_ALBUM_ARTIST_SORTNAME;
	case RB_METADATA_FIELD_COMPOSER:
		return GST_TAG_COMPOSER;
	case RB_METADATA_FIELD_COMPOSER_SORTNAME:
		return GST_TAG_COMPOSER_SORTNAME;
	default:
		return NULL;
	}
}
