/*
 *  arch-tag: Header for main song information display widget
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <gtk/gtkhbox.h>

#include "rhythmdb.h"
#include "monkey-media-player.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYER         (rb_player_get_type ())
#define RB_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYER, RBPlayer))
#define RB_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYER, RBPlayerClass))
#define RB_IS_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYER))
#define RB_IS_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYER))
#define RB_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYER, RBPlayerClass))

typedef struct RBPlayerPrivate RBPlayerPrivate;

typedef struct
{
	GtkHBox parent;

	RBPlayerPrivate *priv;
} RBPlayer;

typedef struct
{
	GtkHBoxClass parent;
} RBPlayerClass;

GType		rb_player_get_type		(void);

RBPlayer *	rb_player_new			(MonkeyMediaPlayer *player);

void		rb_player_set_playing_entry	(RBPlayer *player,
						 RhythmDBEntry *entry);

void		rb_player_set_title		(RBPlayer *player,
						 const char *title);

void		rb_player_set_show_artist_album	(RBPlayer *player,
						 gboolean show);

void		rb_player_set_urldata		(RBPlayer *player,
						 const char *urltext,
						 const char *urllink);

void		rb_player_sync			(RBPlayer *player);

gboolean	rb_player_sync_time		(RBPlayer *player);

char *		rb_player_get_elapsed_string	(RBPlayer *player);

G_END_DECLS

#endif /* __RB_PLAYER_H */
