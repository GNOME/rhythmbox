/*
 *  Copyright © 2002 Jorn Baayen.  All rights reserved.
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

#include <gtk/gtkhbox.h>

#include "rb.h"
#include "rb-node.h"

#ifndef __RB_PLAYER_H
#define __RB_PLAYER_H

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
	GtkHBoxClass parent_class;
} RBPlayerClass;

typedef enum
{
	RB_PLAYER_PLAYING,
	RB_PLAYER_PAUSED,
	RB_PLAYER_STOPPED
} RBPlayerState;

GType         rb_player_get_type      (void);

RBPlayer     *rb_player_new           (RB *rb);

void          rb_player_queue_song    (RBPlayer *player,
			               RBNode *song,
				       gboolean insert_start,
			               gboolean start_playing);

RBNode       *rb_player_get_song      (RBPlayer *player);

void          rb_player_set_state     (RBPlayer *player,
			               RBPlayerState state);

RBPlayerState rb_player_get_state     (RBPlayer *player);

void          rb_player_load_playlist (RBPlayer *player,
			               const char *uri,
			               GError **error);

void          rb_player_save_playlist (RBPlayer *player,
			               const char *uri,
				       const char *name,
			               GError **error);

G_END_DECLS

#endif /* __RB_PLAYER_H */
