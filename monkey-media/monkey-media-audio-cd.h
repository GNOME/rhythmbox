/*  monkey-media
 *
 *  arch-tag: Header for MonkeyMedia AudioCD playback object
 *
 *  Copyright (C) 2001 Iain Holmes <iain@ximian.com>
 *                2002 Kenneth Christiansen <kenneth@gnu.org>
 *                     Olivier Martin <omartin@ifrance.com>
 *                     Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __MONKEY_MEDIA_AUDIO_CD_H
#define __MONKEY_MEDIA_AUDIO_CD_H

#include <glib/gerror.h>
#include <glib-object.h>

G_BEGIN_DECLS

gboolean monkey_media_audio_cd_device_available (void);

typedef enum
{
	MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_IMPLEMENTED,
	MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_OPENED,
	MONKEY_MEDIA_AUDIO_CD_ERROR_SYSTEM_ERROR,
	MONKEY_MEDIA_AUDIO_CD_ERROR_IO,
	MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_READY
} MonkeyMediaAudioCDError;

#define MONKEY_MEDIA_AUDIO_CD_ERROR monkey_media_audio_cd_error_quark ()

GQuark monkey_media_audio_cd_error_quark (void);

#define MONKEY_MEDIA_TYPE_AUDIO_CD            (monkey_media_audio_cd_get_type ())
#define MONKEY_MEDIA_AUDIO_CD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONKEY_MEDIA_TYPE_AUDIO_CD, MonkeyMediaAudioCD))
#define MONKEY_MEDIA_AUDIO_CD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MONKEY_MEDIA_TYPE_AUDIO_CD, MonkeyMediaAudioCDClass))
#define MONKEY_MEDIA_IS_AUDIO_CD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONKEY_MEDIA_TYPE_AUDIO_CD))
#define MONKEY_MEDIA_IS_AUDIO_CD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MONKEY_MEDIA_TYPE_AUDIO_CD))
#define MONKEY_MEDIA_AUDIO_CD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MONKEY_MEDIA_TYPE_AUDIO_CD, MonkeyMediaAudioCDClass))

typedef struct MonkeyMediaAudioCDPrivate MonkeyMediaAudioCDPrivate;

typedef struct
{
	GObject object;

	MonkeyMediaAudioCDPrivate *priv;
} MonkeyMediaAudioCD;

typedef struct
{
	GObjectClass klass;

	void (*cd_changed) (MonkeyMediaAudioCD *cd, gboolean available);
} MonkeyMediaAudioCDClass;

GType               monkey_media_audio_cd_get_type    (void);

MonkeyMediaAudioCD *monkey_media_audio_cd_new         (GError **error);

gboolean            monkey_media_audio_cd_available   (MonkeyMediaAudioCD *cd,
						       GError **error);

/* returns a list of uri */
GList              *monkey_media_audio_cd_list_tracks (MonkeyMediaAudioCD *cd,
						       GError **error);

void                monkey_media_audio_cd_free_tracks (GList *list);

/* returns the musicbrainz cd index id */
char               *monkey_media_audio_cd_get_disc_id (MonkeyMediaAudioCD *cd,
						       GError **error);

void                monkey_media_audio_cd_open_tray   (MonkeyMediaAudioCD *cd,
						       GError **error);

void                monkey_media_audio_cd_close_tray  (MonkeyMediaAudioCD *cd,
						       GError **error);

G_END_DECLS

#endif /* __MONKEY_MEDIA_AUDIO_CD_H */
