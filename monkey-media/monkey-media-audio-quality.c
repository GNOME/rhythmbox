/*  monkey-media
 *
 *  arch-tag: Implementation of song quality utility functions
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

#include "monkey-media-audio-quality.h"
#include <libgnome/gnome-i18n.h>

GType
monkey_media_audio_quality_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ MONKEY_MEDIA_AUDIO_QUALITY_VERY_LOW,  "MONKEY_MEDIA_AUDIO_QUALITY_VERY_LOW",  N_("Very low") },
			{ MONKEY_MEDIA_AUDIO_QUALITY_LOW,       "MONKEY_MEDIA_AUDIO_QUALITY_VLOW",      N_("Low") },
			{ MONKEY_MEDIA_AUDIO_QUALITY_REGULAR,   "MONKEY_MEDIA_AUDIO_QUALITY_REGULAR",   N_("Regular") },
			{ MONKEY_MEDIA_AUDIO_QUALITY_HIGH,      "MONKEY_MEDIA_AUDIO_QUALITY_HIGH",      N_("High") },
			{ MONKEY_MEDIA_AUDIO_QUALITY_VERY_HIGH, "MONKEY_MEDIA_AUDIO_QUALITY_VERY_HIGH", N_("Very high") },
			{ MONKEY_MEDIA_AUDIO_QUALITY_LOSSLESS,  "MONKEY_MEDIA_AUDIO_QUALITY_LOSSLESS",  N_("Lossless") },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MonkeyMediaAudioQuality", values);
	}
	
	return etype;
}

MonkeyMediaAudioQuality
monkey_media_audio_quality_from_bit_rate (int bit_rate)
{
	if (bit_rate <= 80)
		return MONKEY_MEDIA_AUDIO_QUALITY_VERY_LOW;
	else if (bit_rate <= 112)
		return MONKEY_MEDIA_AUDIO_QUALITY_LOW;
	else if (bit_rate <= 160)
		return MONKEY_MEDIA_AUDIO_QUALITY_REGULAR;
	else if (bit_rate <= 224)
		return MONKEY_MEDIA_AUDIO_QUALITY_HIGH;
	else if (bit_rate <= 1410)
		return MONKEY_MEDIA_AUDIO_QUALITY_VERY_HIGH;
	else
		return MONKEY_MEDIA_AUDIO_QUALITY_LOSSLESS;
}

char *
monkey_media_audio_quality_to_string (MonkeyMediaAudioQuality quality)
{
	GEnumClass *class;
	GEnumValue *value;
	char *ret;

	class = g_type_class_ref (MONKEY_MEDIA_TYPE_AUDIO_QUALITY);

	value = g_enum_get_value (class, quality);

	ret = g_strdup (_(value->value_nick));

	g_type_class_unref (class);

	return ret;
}
