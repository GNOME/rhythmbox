/*  monkey-sound
 *
 *  arch-tag: Implementation of song metadata loading object
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

#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"
#ifdef HAVE_MUSICBRAINZ
#include "monkey-media-musicbrainz.h"
#endif

#include "vorbis-stream-info-impl.h"
#include "mp3-stream-info-impl.h"
#include "audiocd-stream-info-impl.h"
#include "flac-stream-info-impl.h"

static void monkey_media_stream_info_class_init (MonkeyMediaStreamInfoClass *klass);
static void monkey_media_stream_info_init (MonkeyMediaStreamInfo *info);
static void monkey_media_stream_info_finalize (GObject *object);
static void monkey_media_stream_info_set_property (GObject *object,
			                           guint prop_id,
			                           const GValue *value,
			                           GParamSpec *pspec);
static void monkey_media_stream_info_get_property (GObject *object,
			                           guint prop_id,
				                   GValue *value,
			                           GParamSpec *pspec);

GType
monkey_media_stream_info_field_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			/* tags */
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,                   "MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE",                   "title" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,                  "MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST",                  "artist" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,                   "MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM",                   "album" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,                    "MONKEY_MEDIA_STREAM_INFO_FIELD_DATE",                    "date" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,                   "MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE",                   "genre" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,                 "MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT",                 "comment" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,            "MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER",            "tracknumber" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,        "MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER",        "max_tracknumber" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION,                "MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION",                "location" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION,             "MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION",             "description" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION,                 "MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION",                 "version" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC,                    "MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC",                    "isrc" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION,            "MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION",            "organization" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT,               "MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT",               "copyright" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT,                 "MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT",                 "contact" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE,                 "MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE",                 "license" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER,               "MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER",               "performer" },

			/* generic stream information */
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,               "MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE",               "filesize" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,                "MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION",                "duration" },

			/* audio bits */
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO,               "MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO",               "has_audio" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO,        "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO",        "audio_codecinfo" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE,          "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE",          "audio_bitrate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,  "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE",  "audio_average-bitrate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE, "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE", "audio_variable-bitrate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY,           "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY",           "audio_quality" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE,       "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE",       "audio_samplerate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS,          "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS",          "audio_channels" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR,            "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR",            "audio_vendor" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER,     "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER",     "audio_serialnumber" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN,        "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN",        "audio_album_gain" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN,        "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN",        "audio_track_gain" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK,        "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK",        "audio_album_peak" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK,        "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK",        "audio_track_peak" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID,            "MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID",            "audio_trm_id" },

			/* video bits */
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO,               "MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO",               "has_video" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_CODEC_INFO,        "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_CODEC_INFO",        "video_codecinfo" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_BIT_RATE,          "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_BIT_RATE",          "video_bitrate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_AVERAGE_BIT_RATE,  "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_AVERAGE_BIT_RATE",  "video_average-bitrate" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VARIABLE_BIT_RATE, "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VARIABLE_BIT_RATE", "video_variable-bitrate" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_WIDTH,             "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_WIDTH",             "video_width" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_HEIGHT,            "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_HEIGHT",            "video_height" },
			{ MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_SERIAL_NUMBER,     "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_SERIAL_NUMBER",     "video_serialnumber" },
	                { MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VENDOR,            "MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VENDOR",            "video_vendor" },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MonkeyMediaStreamInfoField", values);
	}

	return etype;
}

gboolean
monkey_media_stream_info_uri_is_supported (const char *uri)
{
	char *mimetype;

	g_return_val_if_fail (uri != NULL, FALSE);

	return (monkey_media_get_stream_info_impl_for (uri, &mimetype) != -1);
}

GQuark
monkey_media_stream_info_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("monkey_media_stream_info_error");

	return quark;
}

struct MonkeyMediaStreamInfoPrivate
{
	char *location;

	GError *error;
};

enum
{
	PROP_0,
	PROP_LOCATION,
	PROP_ERROR
};

static GObjectClass *parent_class = NULL;

GType
monkey_media_stream_info_get_type (void)
{
	static GType monkey_media_stream_info_type = 0;

	if (monkey_media_stream_info_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (MonkeyMediaStreamInfoClass),
			NULL,
			NULL,
			(GClassInitFunc) monkey_media_stream_info_class_init,
			NULL,
			NULL,
			sizeof (MonkeyMediaStreamInfo),
			0,
			(GInstanceInitFunc) monkey_media_stream_info_init
		};

		monkey_media_stream_info_type = g_type_register_static (G_TYPE_OBJECT,
									"MonkeyMediaStreamInfo",
									&our_info, 0);
	}

	return monkey_media_stream_info_type;
}

static void
monkey_media_stream_info_class_init (MonkeyMediaStreamInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = monkey_media_stream_info_finalize;

	object_class->set_property = monkey_media_stream_info_set_property;
	object_class->get_property = monkey_media_stream_info_get_property;

	g_object_class_install_property (object_class,
					 PROP_LOCATION,
					 g_param_spec_string ("location",
							      "Loaded URI",
							      "Loaded URI",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ERROR,
					 g_param_spec_pointer ("error",
							       "Error",
							       "Failure information",
							       G_PARAM_READWRITE));
}

static void
monkey_media_stream_info_init (MonkeyMediaStreamInfo *info)
{
	info->priv = g_new0 (MonkeyMediaStreamInfoPrivate, 1);
}

static void
monkey_media_stream_info_finalize (GObject *object)
{
	MonkeyMediaStreamInfo *info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (object));

	info = MONKEY_MEDIA_STREAM_INFO (object);

	g_return_if_fail (info->priv != NULL);

	g_free (info->priv->location);

	g_free (info->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
monkey_media_stream_info_set_property (GObject *object,
			               guint prop_id,
			               const GValue *value,
			               GParamSpec *pspec)
{
	MonkeyMediaStreamInfo *info = MONKEY_MEDIA_STREAM_INFO (object);

	switch (prop_id)
	{
	case PROP_LOCATION:
		{
			MonkeyMediaStreamInfoClass *klass = MONKEY_MEDIA_STREAM_INFO_GET_CLASS (info);

			info->priv->location = g_strdup (g_value_get_string (value));

			klass->open_stream (info);
		}
		break;
	case PROP_ERROR:
		info->priv->error = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
monkey_media_stream_info_get_property (GObject *object,
			               guint prop_id,
				       GValue *value,
			               GParamSpec *pspec)
{
	MonkeyMediaStreamInfo *info = MONKEY_MEDIA_STREAM_INFO (object);

	switch (prop_id)
	{
	case PROP_LOCATION:
		g_value_set_string (value, info->priv->location);
		break;
	case PROP_ERROR:
		g_value_set_pointer (value, info->priv->error);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

MonkeyMediaStreamInfo *
monkey_media_stream_info_new (const char *uri, GError **error)
{
	MonkeyMediaStreamInfo *info = NULL;
	GType impl_type = -1;
	char *mimetype = NULL;
	GError *tmp;

	g_return_val_if_fail (uri != NULL, NULL);

	impl_type = monkey_media_get_stream_info_impl_for (uri, &mimetype);

	if (impl_type == -1)
	{
		if (mimetype)
			g_set_error (error,
				     MONKEY_MEDIA_STREAM_INFO_ERROR,
				     MONKEY_MEDIA_STREAM_INFO_ERROR_UNSUPPORTED_MIME_TYPE,
				     _("Unsupported MIME type %s"), mimetype);
		else
			g_set_error (error,
				     MONKEY_MEDIA_STREAM_INFO_ERROR,
				     MONKEY_MEDIA_STREAM_INFO_ERROR_UNSUPPORTED_MIME_TYPE,
				     _("Unknown file type"));
		return NULL;
	}

	info = MONKEY_MEDIA_STREAM_INFO (g_object_new (impl_type,
						       "location", uri,
						       NULL));

	g_return_val_if_fail (info->priv != NULL, NULL);

	g_object_get (G_OBJECT (info), "error", &tmp, NULL);

	if (tmp != NULL)
        {
		if (error != NULL)
			*error = tmp;
		else
			g_error_free (tmp);

		g_object_unref (G_OBJECT (info));
		info = NULL;
        }

	return info;
}

static gboolean
sanitize_values (gboolean ret,
		 MonkeyMediaStreamInfo *info,
		 MonkeyMediaStreamInfoField field,
		 GValue *value)
{
	if (ret == TRUE)
	{
		/* Convert "\0" to NULL */
		if (G_VALUE_HOLDS (value, G_TYPE_STRING))
		{
			const char *str;

			str = g_value_get_string (value);
			if (str[0] != '\0')
				return TRUE;
		} else {
			return TRUE;
		}

		g_value_unset (value);
	}

	switch (field) 
	{
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		{
			GValue tracknum_value = { 0, };
			int tracknum = 0;
			
			g_value_init (value, G_TYPE_STRING);

			if (monkey_media_stream_info_get_value (info,
							        MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
							        0,
							        &tracknum_value) == TRUE)
			{
				tracknum = g_value_get_int (&tracknum_value);
				g_value_unset (&tracknum_value);
			}

			if (tracknum > 0)
			{
				char *name;

				name = g_strdup_printf (_("Track %.2d"), tracknum);
				g_value_set_string (value, name);
				g_free (name);
			}
			else
			{
				char *basename;
				char *unescaped_basename;
				char *utf8_basename;
			
				basename = g_path_get_basename (info->priv->location);
				unescaped_basename = gnome_vfs_unescape_string_for_display (basename);			
				utf8_basename = g_filename_to_utf8 (unescaped_basename,
								    g_utf8_strlen (unescaped_basename, -1),
								    NULL, NULL, NULL);
				if (utf8_basename != NULL)
					g_value_set_string (value, utf8_basename);
				else
					g_value_set_string (value, _("Unknown"));
			
				g_free (basename);
				g_free (unescaped_basename);
				g_free (utf8_basename);
			}

			ret = TRUE;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, _("Unknown"));
		ret = TRUE;
		break;
	default:
		ret = FALSE;
		break;
	}

	return ret;
}

int
monkey_media_stream_info_get_n_values (MonkeyMediaStreamInfo *info,
				       MonkeyMediaStreamInfoField field)
{
	MonkeyMediaStreamInfoClass *klass;
	int ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info), 0);

	klass = MONKEY_MEDIA_STREAM_INFO_GET_CLASS (info);

	ret = klass->get_n_values (info, field);

	if (ret == 0 &&
	    (field == MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE ||
	     field == MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST ||
	     field == MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM ||
	     field == MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE ||
	     field == MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION ||
	     field == MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT))
	{
		ret = 1;
	}

	return ret;
}

GList *
monkey_media_stream_info_get_value_list (MonkeyMediaStreamInfo *info,
					 MonkeyMediaStreamInfoField field)
{
	MonkeyMediaStreamInfoClass *klass;
	int i, max;
	GList *ret = NULL;

	g_return_val_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info), NULL);

	klass = MONKEY_MEDIA_STREAM_INFO_GET_CLASS (info);

	max = klass->get_n_values (info, field);

	for (i = 0; i < max; i++)
	{
		GValue *value;

		value = g_new0 (GValue, 1);
		
		klass->get_value (info, field, i, value);

		ret = g_list_append (ret, value);
	}

	return ret;
}

void
monkey_media_stream_info_free_value_list (GList *list)
{
	GList *l;

	for (l = list; l != NULL; l = g_list_next (l))
	{
		GValue *value;
		
		value = (GValue *) l->data;

		g_value_unset (value);
		g_free (value);
	}

	g_list_free (list);
}

gboolean
monkey_media_stream_info_get_value (MonkeyMediaStreamInfo *info,
				    MonkeyMediaStreamInfoField field,
				    int index,
				    GValue *value)
{
	MonkeyMediaStreamInfoClass *klass;
	gboolean ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info), FALSE);

	klass = MONKEY_MEDIA_STREAM_INFO_GET_CLASS (info);

	ret = klass->get_value (info, field, index, value);

	ret = sanitize_values (ret, info, field, value);

	return ret;
}

gboolean
monkey_media_stream_info_set_value (MonkeyMediaStreamInfo *info,
				    MonkeyMediaStreamInfoField field,
				    int index,
				    const GValue *value)
{
	MonkeyMediaStreamInfoClass *klass;

	g_return_val_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info), FALSE);

	klass = MONKEY_MEDIA_STREAM_INFO_GET_CLASS (info);

	return klass->set_value (info, field, index, value);
}

static const char *genres[] =
{
	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"AlternRock",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychadelic",
	"Rave",
	"ShowTunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",
	"Folk",
	"Folk-Rock",
	"National Folk",
	"Swing",
	"Fast Fusion",
	"Bebob",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Power Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Capella",
	"Euro-House",
	"Dance Hall",
	N_("Unknown")
};

GList *
monkey_media_stream_info_list_all_genres (void)
{
	static GList *list = NULL;
	GList *list2;
	int i;

	if (list != NULL)
		return list;

	for (i = 0; i < G_N_ELEMENTS (genres); i++)
		list = g_list_append (list, _(genres[i]));
	
	list2 = g_list_sort (list, (GCompareFunc) strcmp);
	list = list2;

	return list;
}

int
monkey_media_stream_info_genre_to_index (const char *genre)
{
	int i;
	
	for (i = 0; i < G_N_ELEMENTS (genres); i++)
	{
		if (strcmp (genre, _(genres[i])) == 0)
			return i;
	}

	return G_N_ELEMENTS (genres) - 1;
}

const char *
monkey_media_stream_info_index_to_genre (int index)
{
	if (index >= G_N_ELEMENTS (genres))
		return _(genres[G_N_ELEMENTS (genres) - 1]);

	return _(genres[index]);
}

#ifdef HAVE_MUSICBRAINZ
static void
clear_values (MonkeyMediaStreamInfo *info,
	      MonkeyMediaStreamInfoField field)
{
	int i;
	
	for (i = monkey_media_stream_info_get_n_values (info, field) - 1; i >= 0; i--)
	{
		GValue value = { 0, };
		g_value_init (&value, G_TYPE_NONE);
		monkey_media_stream_info_set_value (info, field, i, &value);
	}
}
#endif

gboolean
monkey_media_stream_info_get_value_net (MonkeyMediaStreamInfo *info,
					MonkeyMediaStreamInfoField field,
					GValue *value,
					GError **error)
{
#ifdef HAVE_MUSICBRAINZ
	MonkeyMediaMusicbrainz *mb;
	gboolean ret;
#endif
	GValue val = { 0, };

	g_return_val_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info), FALSE);

	if (monkey_media_stream_info_get_value (info,
					        MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID,
					        0,
					        &val) == FALSE)
	{
		if (*error != NULL)
		{
			*error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
					      MONKEY_MEDIA_STREAM_INFO_ERROR_NO_TRM_ID,
					      _("No TRM ID for this song"));
		}
		return FALSE;
	}

#ifdef HAVE_MUSICBRAINZ
	mb = monkey_media_musicbrainz_new ();

	if (monkey_media_musicbrainz_load_info (mb,
					        MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG,
					        g_value_get_string (&val)) == FALSE)
	{
		if (*error != NULL)
		{
			*error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
					      MONKEY_MEDIA_STREAM_INFO_ERROR_NO_NET_INFO,
					      _("No information for this song found"));
		}
		g_value_unset (&val);
		g_object_unref (G_OBJECT (mb));
		return FALSE;
	}

	g_value_unset (&val);

	ret = monkey_media_musicbrainz_query (mb,
					      field,
					      -1,
					      value);

	g_object_unref (G_OBJECT (mb));
	return ret;
#else
	return TRUE;
#endif
}

void
monkey_media_stream_info_sync_from_net (MonkeyMediaStreamInfo *info,
					GError **error)
{
#ifdef HAVE_MUSICBRAINZ
	MonkeyMediaMusicbrainz *mb;
	GValue value = { 0, };
	int i;

	static MonkeyMediaStreamInfoField sync_fields[] = {
		MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
		MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
		MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
		MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
		MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER
	};

	g_return_if_fail (MONKEY_MEDIA_IS_STREAM_INFO (info));

	if (monkey_media_stream_info_get_value (info,
					        MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID,
					        0,
					        &value) == FALSE)
	{
		if (*error != NULL)
		{
			*error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
					      MONKEY_MEDIA_STREAM_INFO_ERROR_NO_TRM_ID,
					      _("No TRM ID for this song"));
		}
		return;
	}

	mb = monkey_media_musicbrainz_new ();

	if (monkey_media_musicbrainz_load_info (mb,
					        MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG,
					        g_value_get_string (&value)) == FALSE)
	{
		if (*error != NULL)
		{
			*error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
					      MONKEY_MEDIA_STREAM_INFO_ERROR_NO_NET_INFO,
					      _("No information for this song found"));
		}
		g_value_unset (&value);
		g_object_unref (G_OBJECT (mb));
		return;
	}

	g_value_unset (&value);

	for (i = 0; i < G_N_ELEMENTS (sync_fields); i++)
	{
		if (monkey_media_musicbrainz_query (mb,
						    sync_fields[i],
						    -1,
						    &value) == TRUE)
		{
			clear_values (info, sync_fields[i]);
			monkey_media_stream_info_set_value (info,
							    sync_fields[i],
							    0,
							    &value);
			g_value_unset (&value);
		}
	}

	g_object_unref (G_OBJECT (mb));
#endif
}
