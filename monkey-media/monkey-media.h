/*  monkey-sound
 *
 *  arch-tag: Header for main MonkeyMedia interface
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

#ifndef __MONKEY_MEDIA_H
#define __MONKEY_MEDIA_H

#include <glib.h>
#include <popt.h>

G_BEGIN_DECLS

void				monkey_media_init				(int *argc, char ***argv);

void				monkey_media_init_with_popt_table		(int *argc, char ***argv,
										 const struct poptOption *popt_options);

const struct poptOption *	monkey_media_get_popt_table			(void);

void				monkey_media_shutdown				(void);

void				monkey_media_main				(void);

void				monkey_media_main_quit				(void);

GList *				monkey_media_get_supported_filename_extensions	(void);

/* these wrap the GConf key, but please use these since that will allow us to change
 * the key and/or configuration backend if it were necessary */
const char *			monkey_media_get_audio_driver			(void);
void				monkey_media_set_audio_driver			(const char *audio_driver);

const char*			monkey_media_get_cd_drive			(void);
void				monkey_media_set_cd_drive			(const char *cd_drive);

typedef enum
{
	MONKEY_MEDIA_CD_PLAYBACK_NO_ERROR_CORRECTION     = 0,
	MONKEY_MEDIA_CD_PLAYBACK_MEDIUM_ERROR_CORRECTION = 4,
	MONKEY_MEDIA_CD_PLAYBACK_FULL_ERROR_CORRECTION   = 255
} MonkeyMediaCDPlaybackMode;

MonkeyMediaCDPlaybackMode       monkey_media_get_cd_playback_mode              (void);
void                            monkey_media_set_cd_playback_mode              (MonkeyMediaCDPlaybackMode playback_mode);

G_END_DECLS

#include "monkey-media-player.h"
#include "monkey-media-stream-info.h"
#include "monkey-media-audio-quality.h"
#include "monkey-media-includes.h"

#endif /* __MONKEY_MEDIA_H */
