/* 
 *  arch-tag: Implementation of random play order weighted by rating
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

#include "rb-play-order-random-by-rating.h"

#include "rhythmdb.h"
#include <time.h>
#include <math.h>

static void rb_random_play_order_by_rating_class_init (RBRandomPlayOrderByRatingClass *klass);

static double rb_random_by_rating_get_entry_weight (RBRandomPlayOrder *rorder,
						 RhythmDB *db, RhythmDBEntry *entry);

static RBPlayOrderClass *parent_class = NULL;

GType
rb_random_play_order_by_rating_get_type (void)
{
	static GType rb_random_play_order_by_rating_type = 0;

	if (rb_random_play_order_by_rating_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBRandomPlayOrderByRatingClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_random_play_order_by_rating_class_init,
			NULL,
			NULL,
			sizeof (RBRandomPlayOrderByRating),
			0,
			NULL
		};

		rb_random_play_order_by_rating_type = g_type_register_static (RB_TYPE_RANDOM_PLAY_ORDER,
				"RBRandomPlayOrderByRating",
				&our_info, 0);
	}

	return rb_random_play_order_by_rating_type;
}

static void
rb_random_play_order_by_rating_class_init (RBRandomPlayOrderByRatingClass *klass)
{
	RBRandomPlayOrderClass *rorder;

	parent_class = g_type_class_peek_parent (klass);

	rorder = RB_RANDOM_PLAY_ORDER_CLASS (klass);
	rorder->get_entry_weight = rb_random_by_rating_get_entry_weight;
}

RBPlayOrder *
rb_random_play_order_by_rating_new (RBShellPlayer *player)
{
	RBRandomPlayOrderByRating *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_BY_RATING,
			"player", player,
			NULL);

	return RB_PLAY_ORDER (rorder);
}

static double
rb_random_by_rating_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db, RhythmDBEntry *entry)
{
	gdouble rating = rhythmdb_entry_get_double (db, entry, RHYTHMDB_PROP_RATING);
	return rating;
}
