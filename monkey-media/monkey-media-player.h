/*  monkey-media
 *
 *  arch-tag: Interface to audio backend  
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __MONKEY_MEDIA_PLAYER_H
#define __MONKEY_MEDIA_PLAYER_H

#include <glib-object.h>

#include "monkey-media-stream-info.h"

G_BEGIN_DECLS

typedef enum
{
	MONKEY_MEDIA_PLAYER_ERROR_NO_INPUT_PLUGIN,
	MONKEY_MEDIA_PLAYER_ERROR_NO_QUEUE_PLUGIN,
	MONKEY_MEDIA_PLAYER_ERROR_NO_DEMUX_PLUGIN,
	MONKEY_MEDIA_PLAYER_ERROR_NO_VOLUME_PLUGIN,
	MONKEY_MEDIA_PLAYER_ERROR_DEMUX_FAILED,
	MONKEY_MEDIA_PLAYER_ERROR_NO_AUDIO,
	MONKEY_MEDIA_PLAYER_ERROR_GENERAL,
	MONKEY_MEDIA_PLAYER_ERROR_INTERNAL
} MonkeyMediaPlayerError;

#define MONKEY_MEDIA_PLAYER_TICK_HZ 5

#define MONKEY_MEDIA_PLAYER_ERROR monkey_media_player_error_quark ()

GQuark monkey_media_player_error_quark (void);

#define MONKEY_MEDIA_TYPE_PLAYER         (monkey_media_player_get_type ())
#define MONKEY_MEDIA_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MONKEY_MEDIA_TYPE_PLAYER, MonkeyMediaPlayer))
#define MONKEY_MEDIA_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MONKEY_MEDIA_TYPE_PLAYER, MonkeyMediaPlayerClass))
#define MONKEY_MEDIA_IS_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MONKEY_MEDIA_TYPE_PLAYER))
#define MONKEY_MEDIA_IS_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MONKEY_MEDIA_TYPE_PLAYER))
#define MONKEY_MEDIA_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MONKEY_MEDIA_TYPE_PLAYER, MonkeyMediaPlayerClass))

typedef struct MonkeyMediaPlayerPrivate MonkeyMediaPlayerPrivate;

typedef struct
{
	GObject parent;

	MonkeyMediaPlayerPrivate *priv;
} MonkeyMediaPlayer;

typedef struct
{
	GObjectClass parent_class;

	void (*eos)             (MonkeyMediaPlayer *mp);
	void (*info)            (MonkeyMediaPlayer *mp, MonkeyMediaStreamInfoField field,
		                 GValue *value);
	void (*buffering_begin) (MonkeyMediaPlayer *mp);
	void (*buffering_end)   (MonkeyMediaPlayer *mp);
	void (*error)           (MonkeyMediaPlayer *mp, GError *error);
	void (*tick)            (MonkeyMediaPlayer *mp, long elapsed);
} MonkeyMediaPlayerClass;

GType              monkey_media_player_get_type   (void);

MonkeyMediaPlayer *monkey_media_player_new        (GError **error);

void               monkey_media_player_open       (MonkeyMediaPlayer *mp,
						   const char *uri,
		                                   GError **error);

const char	  *monkey_media_player_get_uri    (MonkeyMediaPlayer *mp);

void               monkey_media_player_close      (MonkeyMediaPlayer *mp);

void               monkey_media_player_play       (MonkeyMediaPlayer *mp);

void               monkey_media_player_pause      (MonkeyMediaPlayer *mp);

gboolean           monkey_media_player_playing    (MonkeyMediaPlayer *mp);

void               monkey_media_player_set_volume (MonkeyMediaPlayer *mp,
						   float volume);
float              monkey_media_player_get_volume (MonkeyMediaPlayer *mp);

void               monkey_media_player_set_mute   (MonkeyMediaPlayer *mp,
						   gboolean mute);
gboolean           monkey_media_player_get_mute   (MonkeyMediaPlayer *mp);

gboolean           monkey_media_player_seekable   (MonkeyMediaPlayer *mp);

void               monkey_media_player_set_time   (MonkeyMediaPlayer *mp,
						   long time);
long               monkey_media_player_get_time   (MonkeyMediaPlayer *mp);

G_END_DECLS

#endif /* __MONKEY_MEDIA_PLAYER_H */
