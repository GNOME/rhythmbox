/*
 *  arch-tag: Interface to audio backend  
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_PLAYER_H
#define __RB_PLAYER_H

#include <glib-object.h>
#include "rb-metadata.h"

G_BEGIN_DECLS

typedef enum
{
	RB_PLAYER_ERROR_NO_INPUT_PLUGIN,
	RB_PLAYER_ERROR_NO_QUEUE_PLUGIN,
	RB_PLAYER_ERROR_NO_DEMUX_PLUGIN,
	RB_PLAYER_ERROR_NO_VOLUME_PLUGIN,
	RB_PLAYER_ERROR_DEMUX_FAILED,
	RB_PLAYER_ERROR_NO_AUDIO,
	RB_PLAYER_ERROR_GENERAL,
	RB_PLAYER_ERROR_INTERNAL
} RBPlayerError;

#define RB_PLAYER_TICK_HZ 5

#define RB_PLAYER_ERROR rb_player_error_quark ()

GQuark rb_player_error_quark (void);

#define RB_TYPE_PLAYER         (rb_player_get_type ())
#define RB_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYER, RBPlayer))
#define RB_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYER, RBPlayerClass))
#define RB_IS_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYER))
#define RB_IS_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYER))
#define RB_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYER, RBPlayerClass))

typedef struct RBPlayerPrivate RBPlayerPrivate;

typedef struct
{
	GObject parent;

	RBPlayerPrivate *priv;
} RBPlayer;

typedef struct
{
	GObjectClass parent_class;

	void (*eos)             (RBPlayer *mp);
	void (*info)            (RBPlayer *mp, RBMetaDataField field, GValue *value);
	void (*buffering_begin) (RBPlayer *mp);
	void (*buffering_end)   (RBPlayer *mp);
	void (*error)           (RBPlayer *mp, GError *error);
	void (*tick)            (RBPlayer *mp, long elapsed);
} RBPlayerClass;

GType		rb_player_get_type   (void);

RBPlayer *	rb_player_new        (GError **error);

void		rb_player_open       (RBPlayer *mp,
				      const char *uri,
				      GError **error);

const char *	rb_player_get_uri    (RBPlayer *mp);

void		rb_player_close      (RBPlayer *mp, GError **error);

void		rb_player_play       (RBPlayer *mp, GError **error);

void		rb_player_pause      (RBPlayer *mp);

gboolean	rb_player_playing    (RBPlayer *mp);

void		rb_player_set_volume (RBPlayer *mp,
				      float volume);
float		rb_player_get_volume (RBPlayer *mp);

void		rb_player_set_replaygain (RBPlayer *mp,
					  double track_gain, double track_peak, double album_gain, double album_peak);

void		rb_player_set_mute   (RBPlayer *mp,
				      gboolean mute);

gboolean	rb_player_get_mute   (RBPlayer *mp);

gboolean	rb_player_seekable   (RBPlayer *mp);

void		rb_player_set_time   (RBPlayer *mp,
				      long time);
long		rb_player_get_time   (RBPlayer *mp);

G_END_DECLS

#endif /* __RB_PLAYER_H */
