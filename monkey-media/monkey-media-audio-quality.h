/*  monkey-media
 *
 *  arch-tag: Header for song quality utility functions
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __MONKEY_MEDIA_AUDIO_QUALITY_H
#define __MONKEY_MEDIA_AUDIO_QUALITY_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
	MONKEY_MEDIA_AUDIO_QUALITY_VERY_LOW = 1,  /* 64 kbps Ogg Vorbis */
	MONKEY_MEDIA_AUDIO_QUALITY_LOW,       /* 96 kbps Ogg Vorbis */
	MONKEY_MEDIA_AUDIO_QUALITY_REGULAR,   /* 128 kbps Ogg Vorbis */
	MONKEY_MEDIA_AUDIO_QUALITY_HIGH,      /* 192 kbps Ogg Vorbis */
	MONKEY_MEDIA_AUDIO_QUALITY_VERY_HIGH, /* 256 kbps Ogg Vorbis */
	MONKEY_MEDIA_AUDIO_QUALITY_LOSSLESS   /* Flac */
} MonkeyMediaAudioQuality;

#define MONKEY_MEDIA_TYPE_AUDIO_QUALITY (monkey_media_audio_quality_get_type ())

GType                   monkey_media_audio_quality_get_type      (void);

MonkeyMediaAudioQuality monkey_media_audio_quality_from_bit_rate (int bit_rate);

char                   *monkey_media_audio_quality_to_string     (MonkeyMediaAudioQuality quality);

G_END_DECLS

#endif /* __MONKEY_MEDIA_AUDIO_QUALITY_H */
