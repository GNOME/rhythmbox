/*  monkey-sound
 *
 *  arch-tag: Header for song metadata loading object
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

#ifndef __MONKEY_MEDIA_STREAM_INFO_H
#define __MONKEY_MEDIA_STREAM_INFO_H

#include <glib-object.h>

#include "monkey-media-audio-quality.h"

G_BEGIN_DECLS

typedef enum
{
	/* tags */
	MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,                   /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,                  /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,                   /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,                    /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,                   /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,                 /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,            /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,        /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION,                /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION,             /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION,                 /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC,                    /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION,            /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT,               /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT,                 /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE,                 /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER,               /* string */

	/* generic stream information */
	MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,               /* long */
	MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,                /* long */

	/* audio bits */
	MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO,               /* boolean */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO,        /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE,          /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,  /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE, /* boolean */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY,           /* MonkeyMediaAudioQuality */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE,       /* long */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS,          /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR,            /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER,     /* long */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN,        /* double */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN,        /* double */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK,        /* double */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK,        /* double */
	MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID,            /* string */

	/* video bits */
	MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO,               /* boolean */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_CODEC_INFO,        /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_BIT_RATE,          /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_AVERAGE_BIT_RATE,  /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VARIABLE_BIT_RATE, /* boolean */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_WIDTH,             /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_HEIGHT,            /* int */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_VENDOR,            /* string */
	MONKEY_MEDIA_STREAM_INFO_FIELD_VIDEO_SERIAL_NUMBER      /* long */
} MonkeyMediaStreamInfoField;

#define MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD (monkey_media_stream_info_field_get_type ())

GType monkey_media_stream_info_field_get_type (void);

GList      *monkey_media_stream_info_list_all_genres (void);
int         monkey_media_stream_info_genre_to_index  (const char *genre);
const char *monkey_media_stream_info_index_to_genre  (int index);

typedef enum
{
	MONKEY_MEDIA_STREAM_INFO_ERROR_UNSUPPORTED_MIME_TYPE,
	MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
	MONKEY_MEDIA_STREAM_INFO_ERROR_SEEK_FAILED,
	MONKEY_MEDIA_STREAM_INFO_ERROR_NO_TRM_ID,
	MONKEY_MEDIA_STREAM_INFO_ERROR_NO_NET_INFO
} MonkeyMediaStreamInfoError;

#define MONKEY_MEDIA_STREAM_INFO_ERROR (monkey_media_stream_info_error_quark ())

GQuark monkey_media_stream_info_error_quark (void);

gboolean monkey_media_stream_info_uri_is_supported (const char *uri);

#define MONKEY_MEDIA_TYPE_STREAM_INFO         (monkey_media_stream_info_get_type ())
#define MONKEY_MEDIA_STREAM_INFO(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MONKEY_MEDIA_TYPE_STREAM_INFO, MonkeyMediaStreamInfo))
#define MONKEY_MEDIA_STREAM_INFO_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MONKEY_MEDIA_TYPE_STREAM_INFO, MonkeyMediaStreamInfoClass))
#define MONKEY_MEDIA_IS_STREAM_INFO(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MONKEY_MEDIA_TYPE_STREAM_INFO))
#define MONKEY_MEDIA_IS_STREAM_INFO_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MONKEY_MEDIA_TYPE_STREAM_INFO))
#define MONKEY_MEDIA_STREAM_INFO_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MONKEY_MEDIA_TYPE_STREAM_INFO, MonkeyMediaStreamInfoClass))

typedef struct MonkeyMediaStreamInfoPrivate MonkeyMediaStreamInfoPrivate;

typedef struct
{
	GObject parent;

	MonkeyMediaStreamInfoPrivate *priv;
} MonkeyMediaStreamInfo;

typedef struct
{
	GObjectClass parent_class;

	void     (*open_stream)  (MonkeyMediaStreamInfo *info);
	int      (*get_n_values) (MonkeyMediaStreamInfo *info, MonkeyMediaStreamInfoField field);
	gboolean (*get_value)    (MonkeyMediaStreamInfo *info, MonkeyMediaStreamInfoField field,
			          int index, GValue *value);
	gboolean (*set_value)    (MonkeyMediaStreamInfo *info, MonkeyMediaStreamInfoField field,
			          int index, const GValue *value);
} MonkeyMediaStreamInfoClass;

GType                  monkey_media_stream_info_get_type        (void);

MonkeyMediaStreamInfo *monkey_media_stream_info_new             (const char *uri,
							         GError **error);

int                    monkey_media_stream_info_get_n_values    (MonkeyMediaStreamInfo *info,
							         MonkeyMediaStreamInfoField field);

GList                 *monkey_media_stream_info_get_value_list  (MonkeyMediaStreamInfo *info,
							         MonkeyMediaStreamInfoField field);

void                   monkey_media_stream_info_free_value_list (GList *list);

gboolean               monkey_media_stream_info_get_value       (MonkeyMediaStreamInfo *info,
							         MonkeyMediaStreamInfoField field,
							         int index,
							         GValue *value);

gboolean               monkey_media_stream_info_set_value       (MonkeyMediaStreamInfo *info,
							         MonkeyMediaStreamInfoField field,
							         int index,
							         const GValue *value);

/* these query from musicbrainz */
gboolean               monkey_media_stream_info_get_value_net   (MonkeyMediaStreamInfo *info,
								 MonkeyMediaStreamInfoField field,
								 GValue *value,
								 GError **error);

void                   monkey_media_stream_info_sync_from_net   (MonkeyMediaStreamInfo *info,
								 GError **error);

G_END_DECLS

#endif /* __MONKEY_MEDIA_STREAM_INFO_H */
