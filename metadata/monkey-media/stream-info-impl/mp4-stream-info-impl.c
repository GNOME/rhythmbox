/*  monkey-sound
 *
 *  arch-tag: Implementation of MP4 metadata loading
 *
 *  Copyright (C) 2003 Bastien Nocera <hadess@hadess.net>
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

#include <libgnomevfs/gnome-vfs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <mp4.h>

#include "rb-string-helpers.h"

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"

#include "mp4-stream-info-impl.h"

/* MP4 audio type */
#define MP4_MPEG4_AUDIO_TYPE 0x40

static void MP4_stream_info_impl_class_init (MP4StreamInfoImplClass *klass);
static void MP4_stream_info_impl_init (MP4StreamInfoImpl *ma);
static void MP4_stream_info_impl_finalize (GObject *object);
static void MP4_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info);
static gboolean MP4_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        GValue *value);
static gboolean MP4_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        const GValue *value);
static int MP4_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				              MonkeyMediaStreamInfoField field);

struct MP4StreamInfoImplPrivate
{
	MP4FileHandle file;
	int track_id;
};

static GObjectClass *parent_class = NULL;

GType
MP4_stream_info_impl_get_type (void)
{
	static GType MP4_stream_info_impl_type = 0;

	if (MP4_stream_info_impl_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (MP4StreamInfoImplClass),
			NULL,
			NULL,
			(GClassInitFunc) MP4_stream_info_impl_class_init,
			NULL,
			NULL,
			sizeof (MP4StreamInfoImpl),
			0,
			(GInstanceInitFunc) MP4_stream_info_impl_init
		};

		MP4_stream_info_impl_type = g_type_register_static (MONKEY_MEDIA_TYPE_STREAM_INFO,
								       "MP4StreamInfoImpl",
								       &our_info, 0);
	}

	return MP4_stream_info_impl_type;
}

static void
MP4_stream_info_impl_class_init (MP4StreamInfoImplClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MonkeyMediaStreamInfoClass *info_class = MONKEY_MEDIA_STREAM_INFO_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = MP4_stream_info_impl_finalize;

	info_class->open_stream  = MP4_stream_info_impl_open_stream;
	info_class->get_n_values = MP4_stream_info_impl_get_n_values;
	info_class->get_value    = MP4_stream_info_impl_get_value;
	info_class->set_value    = MP4_stream_info_impl_set_value;
}

static void
MP4_stream_info_impl_init (MP4StreamInfoImpl *impl)
{
	impl->priv = g_new0 (MP4StreamInfoImplPrivate, 1);
}

static void
MP4_stream_info_impl_finalize (GObject *object)
{
	MP4StreamInfoImpl *impl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MP4_STREAM_INFO_IMPL (object));

	impl = MP4_STREAM_INFO_IMPL (object);

	g_return_if_fail (impl->priv != NULL);

	if (impl->priv->file != 0)
		MP4Close (impl->priv->file);

	g_free (impl->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
MP4_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info)
{
	MP4StreamInfoImpl *impl = MP4_STREAM_INFO_IMPL (info);
	char *uri, *path;
	GError *error;
	int num_tracks, i;
	gboolean found;

	g_object_get (G_OBJECT (info),
		      "error", &error,
		      "location", &uri,
		      NULL);

	path = g_filename_from_uri (uri, NULL, NULL);

	if (path == NULL)
	{
		impl->priv->file = MP4Read (uri, 0);
	} else {
		impl->priv->file = MP4Read (path, 0);
	}

	g_free (path);
	g_free (uri);

	if (impl->priv->file == 0)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
				     MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
				     _("Failed to recognise filetype"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	/* Find a sound track */
	num_tracks = MP4GetNumberOfTracks (impl->priv->file, NULL, 0);
	found = FALSE;
	for (i = 0; i < num_tracks; i++)
	{
		MP4TrackId track_id;
		const char *track_type;

		track_id = MP4FindTrackId (impl->priv->file, i, NULL, 0);
		track_type = MP4GetTrackType (impl->priv->file, track_id);

		/* Did we find an audio track ? */
		if (strcmp (track_type, MP4_AUDIO_TRACK_TYPE) == 0)
		{
			int j;
			guint8 audio_type;

			j = 0;
			audio_type = MP4GetTrackAudioType (impl->priv->file,
					track_id);
			if (audio_type == MP4_MPEG4_AUDIO_TYPE)
			{
				impl->priv->track_id = track_id;
				found = TRUE;
				break;
			}
		}
	}

	if (found == FALSE)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
				     MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
				     _("Failed to find an audio track"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}
}

static int
MP4_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				   MonkeyMediaStreamInfoField field)
{
	MP4StreamInfoImpl *impl;
	char *tmp;
	gboolean ret = FALSE;
	
	g_return_val_if_fail (IS_MP4_STREAM_INFO_IMPL (info), 0);

	impl = MP4_STREAM_INFO_IMPL (info);

	tmp = NULL;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		MP4GetMetadataName (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		MP4GetMetadataArtist (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		MP4GetMetadataAlbum (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		MP4GetMetadataYear (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		MP4GetMetadataGenre (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		MP4GetMetadataComment (impl->priv->file, &tmp);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			guint16 track, total_tracks;

			return MP4GetMetadataTrack (impl->priv->file, &track,
						    &total_tracks);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		return 0;

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:
		return 1;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		return 1;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		return 0;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		return 1;

	/* default */
	default:
		return 0;
	}
}

static gboolean
MP4_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
			        MonkeyMediaStreamInfoField field,
				int index,
				GValue *value)
{
	MP4StreamInfoImpl *impl;
	char *tmp;
	
	g_return_val_if_fail (IS_MP4_STREAM_INFO_IMPL (info), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	impl = MP4_STREAM_INFO_IMPL (info);
	
	if (MP4_stream_info_impl_get_n_values (info, field) <= 0)
		return FALSE;

	tmp = NULL;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataName (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataArtist (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataAlbum (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataYear (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataGenre (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		g_value_init (value, G_TYPE_STRING);
		MP4GetMetadataComment (impl->priv->file, &tmp);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		{
			guint16 track, total_tracks;

			g_value_init (value, G_TYPE_INT);

			MP4GetMetadataTrack (impl->priv->file, &track,
					     &total_tracks);
			g_value_set_int (value, track);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			guint16 track, total_tracks;

			g_value_init (value, G_TYPE_INT);

			MP4GetMetadataTrack (impl->priv->file, &track,
					     &total_tracks);
			g_value_set_int (value, total_tracks);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
		break;

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
		{
			GnomeVFSFileInfo *i;
			GnomeVFSResult res;
			char *uri;

			g_object_get (G_OBJECT (info), "location", &uri, NULL);

			g_value_init (value, G_TYPE_LONG);
			
			i = gnome_vfs_file_info_new ();
			res = gnome_vfs_get_file_info (uri, i,
						       GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
			if (res == GNOME_VFS_OK)
				g_value_set_long (value, i->size);
			else
				g_value_set_long (value, 0);

			gnome_vfs_file_info_unref (i);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, MP4ConvertFromTrackDuration
				  (impl->priv->file, impl->priv->track_id,
				   MP4GetTrackDuration (impl->priv->file,
						        impl->priv->track_id),
				   MP4_MSECS_TIME_SCALE) / 1000);
		break;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, _("MPEG-4 audio (MPEG-4 AAC)"));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, MP4GetTrackBitRate (impl->priv->file,
							    impl->priv->track_id) / 1000);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
		g_value_init (value, MONKEY_MEDIA_TYPE_AUDIO_QUALITY);
		g_value_set_enum (value, monkey_media_audio_quality_from_bit_rate (MP4GetTrackBitRate (impl->priv->file, impl->priv->track_id)));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, (MP4GetTrackFixedSampleDuration
				(impl->priv->file, impl->priv->track_id) != MP4_INVALID_DURATION));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
		/* FIXME we're lying, we can't possibly use the decoder here */
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, 44100);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		/* Lying again */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, 2);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, 0);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, 0.0);
		break;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;

	/* default */
	default:
		g_warning ("Invalid field!");
		g_value_init (value, G_TYPE_NONE);
		break;
	}

	return TRUE;
}

static gboolean
MP4_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
				MonkeyMediaStreamInfoField field,
				int index,
				const GValue *value)
{
	/* FIXME */
	return FALSE;
}

