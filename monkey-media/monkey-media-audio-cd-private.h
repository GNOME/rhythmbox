/*  monkey-sound
 *
 *  arch-tag: Various functions internal to AudioCD implementations
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

#ifndef __MONKEY_MEDIA_AUDIO_CD_PRIVATE_H
#define __MONKEY_MEDIA_AUDIO_CD_PRIVATE_H

#include "monkey-media-audio-cd.h"

G_BEGIN_DECLS

long     monkey_media_audio_cd_get_track_duration (MonkeyMediaAudioCD *cd,
					           int track,
					           GError **error);
int      monkey_media_audio_cd_get_track_offset   (MonkeyMediaAudioCD *cd,
					           int track,
					           GError **error);
gboolean monkey_media_audio_cd_have_track         (MonkeyMediaAudioCD *cd,
						   int track,
						   GError **error);
int      monkey_media_audio_cd_get_n_tracks       (MonkeyMediaAudioCD *cd,
						   GError **error);

void     monkey_media_audio_cd_unref_if_around    (void);

G_END_DECLS

#endif /* __MONKEY_MEDIA_AUDIO_CD_PRIVATE_H */
