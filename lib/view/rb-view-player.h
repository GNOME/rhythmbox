/*
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
 *  $Id$
 */

#ifndef __RB_VIEW_PLAYER_H
#define __RB_VIEW_PLAYER_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <monkey-media.h>

#include "rb-view.h"

G_BEGIN_DECLS

#define RB_TYPE_VIEW_PLAYER         (rb_view_player_get_type ())
#define RB_VIEW_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VIEW_PLAYER, RBViewPlayer))
#define RB_IS_VIEW_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VIEW_PLAYER))
#define RB_VIEW_PLAYER_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_VIEW_PLAYER, RBViewPlayerIface))

typedef struct RBViewPlayer RBViewPlayer; /* Dummy typedef */

typedef enum
{
	RB_VIEW_PLAYER_TRUE          = 1,
	RB_VIEW_PLAYER_FALSE         = 0,
	RB_VIEW_PLAYER_NOT_SUPPORTED = 2
} RBViewPlayerResult;

typedef struct
{
	GTypeInterface g_iface;
	
	/* signals */
	void (*changed)       (RBViewPlayer *player);
	void (*start_playing) (RBViewPlayer *player);

	/* methods */
	void                    (*impl_set_shuffle)      (RBViewPlayer *player,
					                  gboolean shuffle);
	void                    (*impl_set_repeat)       (RBViewPlayer *player,
					                  gboolean repeat);

	RBViewPlayerResult      (*impl_have_first)       (RBViewPlayer *player);
	RBViewPlayerResult      (*impl_have_next)        (RBViewPlayer *player);
	RBViewPlayerResult      (*impl_have_previous)    (RBViewPlayer *player);
	
	void                    (*impl_next)             (RBViewPlayer *player);
	void                    (*impl_previous)         (RBViewPlayer *player);

	const char             *(*impl_get_title)        (RBViewPlayer *player);

	const char             *(*impl_get_artist)       (RBViewPlayer *player);
	const char             *(*impl_get_album)        (RBViewPlayer *player);
	const char             *(*impl_get_song)         (RBViewPlayer *player);
	long                    (*impl_get_duration)     (RBViewPlayer *player);

	GdkPixbuf              *(*impl_get_pixbuf)       (RBViewPlayer *player);

	MonkeyMediaAudioStream *(*impl_get_stream)       (RBViewPlayer *player);

	void                    (*impl_start_playing)    (RBViewPlayer *player);
	void                    (*impl_stop_playing)     (RBViewPlayer *player);
} RBViewPlayerIface;

GType                   rb_view_player_get_type         (void);

void                    rb_view_player_set_shuffle      (RBViewPlayer *player,
						         gboolean shuffle);
void                    rb_view_player_set_repeat       (RBViewPlayer *player,
						         gboolean repeat);

RBViewPlayerResult      rb_view_player_have_first       (RBViewPlayer *player);
RBViewPlayerResult      rb_view_player_have_next        (RBViewPlayer *player);
RBViewPlayerResult      rb_view_player_have_previous    (RBViewPlayer *player);

void                    rb_view_player_next             (RBViewPlayer *player);
void                    rb_view_player_previous         (RBViewPlayer *player);

const char             *rb_view_player_get_title        (RBViewPlayer *player);

const char             *rb_view_player_get_artist       (RBViewPlayer *player);
const char             *rb_view_player_get_album        (RBViewPlayer *player);
const char             *rb_view_player_get_song         (RBViewPlayer *player);
long                    rb_view_player_get_duration     (RBViewPlayer *player);

GdkPixbuf              *rb_view_player_get_pixbuf       (RBViewPlayer *player);

MonkeyMediaAudioStream *rb_view_player_get_stream       (RBViewPlayer *player);

void                    rb_view_player_start_playing    (RBViewPlayer *player);
void                    rb_view_player_stop_playing     (RBViewPlayer *player);

void                    rb_view_player_notify_changed   (RBViewPlayer *player);

void                    rb_view_player_notify_playing   (RBViewPlayer *player);

G_END_DECLS

#endif /* __RB_VIEW_PLAYER_H */
