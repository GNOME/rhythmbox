/* 
 *  arch-tag: Header for random play order weighted by the time since last play
 *
 *  Copyright (C) 2003 Jeffrey Yasskin <jyasskin@mail.utexas.edu>
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

#ifndef __RB_PLAY_ORDER_RANDOM_BY_AGE_H
#define __RB_PLAY_ORDER_RANDOM_BY_AGE_H

#include "rb-play-order.h"

#include "rb-shell-player.h"

G_BEGIN_DECLS

#define RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE         (rb_random_play_order_by_age_get_type ())
#define RB_RANDOM_PLAY_ORDER_BY_AGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE, RBRandomPlayOrderByAge))
#define RB_RANDOM_PLAY_ORDER_BY_AGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE, RBRandomPlayOrderByAgeClass))
#define RB_IS_RANDOM_PLAY_ORDER_BY_AGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE))
#define RB_IS_RANDOM_PLAY_ORDER_BY_AGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE))
#define RB_RANDOM_PLAY_ORDER_BY_AGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE, RBRandomPlayOrderByAgeClass))

typedef struct RBRandomPlayOrderByAgePrivate RBRandomPlayOrderByAgePrivate;

typedef struct
{
	RBPlayOrder parent;

	RBRandomPlayOrderByAgePrivate *priv;
} RBRandomPlayOrderByAge;

typedef struct
{
	RBPlayOrderClass parent_class;
} RBRandomPlayOrderByAgeClass;

GType					rb_random_play_order_by_age_get_type	(void);

RBRandomPlayOrderByAge *		rb_random_play_order_by_age_new		(RBShellPlayer *player);

G_END_DECLS

#endif /* __RB_PLAY_ORDER_RANDOM_BY_AGE_H */
