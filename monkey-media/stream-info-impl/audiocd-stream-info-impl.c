/*  monkey-sound
 *
 *  arch-tag: Implementation of AudioCD metadata loading
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#include <stdlib.h>

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"
#include "monkey-media-audio-cd-private.h"
#include "monkey-media-musicbrainz.h"

#include "audiocd-stream-info-impl.h"

static void audiocd_stream_info_impl_class_init (AudioCDStreamInfoImplClass *klass);
static void audiocd_stream_info_impl_init (AudioCDStreamInfoImpl *ma);
static void audiocd_stream_info_impl_finalize (GObject *object);
static void audiocd_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info);
static gboolean audiocd_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
						    MonkeyMediaStreamInfoField field,
						    int index,
						    GValue *value);
static gboolean audiocd_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
						    MonkeyMediaStreamInfoField field,
						    int index,
						    const GValue *value);
static int audiocd_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
						  MonkeyMediaStreamInfoField field);

struct AudioCDStreamInfoImplPrivate
{
	MonkeyMediaAudioCD *cd;

	MonkeyMediaMusicbrainz *mb;

	char *cd_id;

	int track;
};

static GObjectClass *parent_class = NULL;

GType
audiocd_stream_info_impl_get_type (void)
{
	static GType audiocd_stream_info_impl_type = 0;

	if (audiocd_stream_info_impl_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (AudioCDStreamInfoImplClass),
			NULL,
			NULL,
			(GClassInitFunc) audiocd_stream_info_impl_class_init,
			NULL,
			NULL,
			sizeof (AudioCDStreamInfoImpl),
			0,
			(GInstanceInitFunc) audiocd_stream_info_impl_init
		};

		audiocd_stream_info_impl_type = g_type_register_static (MONKEY_MEDIA_TYPE_STREAM_INFO,
								       "AudioCDStreamInfoImpl",
								       &our_info, 0);
	}

	return audiocd_stream_info_impl_type;
}

static void
audiocd_stream_info_impl_class_init (AudioCDStreamInfoImplClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MonkeyMediaStreamInfoClass *info_class = MONKEY_MEDIA_STREAM_INFO_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = audiocd_stream_info_impl_finalize;

	info_class->open_stream  = audiocd_stream_info_impl_open_stream;
	info_class->get_n_values = audiocd_stream_info_impl_get_n_values;
	info_class->get_value    = audiocd_stream_info_impl_get_value;
	info_class->set_value    = audiocd_stream_info_impl_set_value;
}

static void
audiocd_stream_info_impl_init (AudioCDStreamInfoImpl *impl)
{
	impl->priv = g_new0 (AudioCDStreamInfoImplPrivate, 1);
}

static void
audiocd_stream_info_impl_finalize (GObject *object)
{
	AudioCDStreamInfoImpl *impl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_AUDIOCD_STREAM_INFO_IMPL (object));

	impl = AUDIOCD_STREAM_INFO_IMPL (object);

	g_return_if_fail (impl->priv != NULL);

	if (impl->priv->cd != NULL)
		g_object_unref (G_OBJECT (impl->priv->cd));

	if (impl->priv->mb != NULL)
		g_object_unref (G_OBJECT (impl->priv->mb));

	g_free (impl->priv->cd_id);

	g_free (impl->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
audiocd_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info)
{
	AudioCDStreamInfoImpl *impl = AUDIOCD_STREAM_INFO_IMPL (info);
	GError *error = NULL;
	char *uri;

	g_object_get (G_OBJECT (info),
		      "error", &error,
		      "location", &uri,
		      NULL);

	impl->priv->track = atoi (uri + 10);
	g_free (uri);

	impl->priv->cd = monkey_media_audio_cd_new (&error);
	if (error != NULL)
	{
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	if (monkey_media_audio_cd_have_track (impl->priv->cd, impl->priv->track, &error) == FALSE)
	{
		if (error != NULL)
		{
			error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
                        	             MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			        	     _("Invalid track number"));
		}
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	impl->priv->mb = monkey_media_musicbrainz_new ();

	impl->priv->cd_id = monkey_media_audio_cd_get_disc_id (impl->priv->cd, &error);
	if (error != NULL)
	{
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	if (monkey_media_musicbrainz_load_info (impl->priv->mb,
					        MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD,
					        impl->priv->cd_id) == FALSE)
	{
		/* no disc found, let's try single song lookup mode */
		GValue value = { 0, };

		if (audiocd_stream_info_impl_get_value (info,
						        MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID,
						        0,
						        &value) == TRUE)
		{
			monkey_media_musicbrainz_load_info (impl->priv->mb,
							    MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG,
							    g_value_get_string (&value));
			g_value_unset (&value);
		}
	}
}

static gboolean
audiocd_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
			            MonkeyMediaStreamInfoField field,
				    int index,
				    GValue *value)
{
	AudioCDStreamInfoImpl *impl;
	
	g_return_val_if_fail (IS_AUDIOCD_STREAM_INFO_IMPL (info), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	impl = AUDIOCD_STREAM_INFO_IMPL (info);

	if (audiocd_stream_info_impl_get_n_values (info, field) <= 0)
		return FALSE;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:            /* int */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, impl->priv->track);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:        /* int */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value,
				 monkey_media_audio_cd_get_n_tracks (impl->priv->cd, NULL));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:                   /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:                  /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:                   /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:                    /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:                   /* string */
		monkey_media_musicbrainz_query (impl->priv->mb,
						field,
						impl->priv->track,
						value);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:                 /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:                /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:             /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:                 /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:                    /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:            /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:               /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:                 /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:                 /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:               /* string */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;
	/* generic stream information */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:               /* long */
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value,
				  monkey_media_audio_cd_get_track_duration (impl->priv->cd, impl->priv->track, NULL) * 176400);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:                /* long */
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value,
				  monkey_media_audio_cd_get_track_duration (impl->priv->cd, impl->priv->track, NULL));
		break;
	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:               /* boolean */
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:        /* string */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, _("Audio CD track"));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:          /* int */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, 1411);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:  /* int */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, 1411);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:           /* MonkeyMediaAudioBitrate */
		g_value_init (value, MONKEY_MEDIA_TYPE_AUDIO_QUALITY);
		g_value_set_enum (value, monkey_media_audio_quality_from_bit_rate (1411));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE: /* boolean */
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:       /* long */
		g_value_init (value, G_TYPE_LONG);
		g_value_set_boolean (value, 44100);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:          /* int */
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, 2);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:            /* string */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:     /* long */
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, -1);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:            /* string */
		/* FIXME */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:               /* boolean */
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;
		/* Let the rest of the video flags fall through */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_CODEC_INFO:        /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_BIT_RATE:          /* int */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_AVERAGE_BIT_RATE:  /* int */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VARIABLE_BIT_RATE: /* boolean */
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_WIDTH:             /* int */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_HEIGHT:            /* int */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VENDOR:            /* string */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_SERIAL_NUMBER:     /* long */
	default:
		g_warning("Invalid field %d", field); 
		g_value_init (value, G_TYPE_NONE);
		break;
	}

	return TRUE;
}

static gboolean
audiocd_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
				    MonkeyMediaStreamInfoField field,
				    int index,
				    const GValue *value)
{
	/* FIXME */
	return FALSE;
}

static int
audiocd_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				       MonkeyMediaStreamInfoField field) 
{
	AudioCDStreamInfoImpl *impl;
	
	g_return_val_if_fail (IS_AUDIOCD_STREAM_INFO_IMPL (info), 0);

	impl = AUDIOCD_STREAM_INFO_IMPL (info);

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		return 1;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		{
			GValue value = { 0, };
			gboolean ret;
			
			ret = monkey_media_musicbrainz_query (impl->priv->mb,
						              field,
						              impl->priv->track,
						              &value);
			if (ret == TRUE)
				g_value_unset (&value);

			return ret;
		}
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:       
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
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
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
		return 1;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		return 0;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		return 1;

	default:
		return 0;
	}
}
