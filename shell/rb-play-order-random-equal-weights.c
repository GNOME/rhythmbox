/* 
 *  arch-tag: Implementation of random navigation method with equal weights for each song
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

#include "rb-play-order-random-equal-weights.h"

static void rb_random_play_order_equal_weights_class_init (RBRandomPlayOrderEqualWeightsClass *klass);

static double rb_random_equal_weights_get_entry_weight (RBRandomPlayOrder *rorder,
							RhythmDB *db, RhythmDBEntry *entry);

static RBPlayOrderClass *parent_class = NULL;

GType
rb_random_play_order_equal_weights_get_type (void)
{
	static GType rb_random_play_order_equal_weights_type = 0;

	if (rb_random_play_order_equal_weights_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBRandomPlayOrderEqualWeightsClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_random_play_order_equal_weights_class_init,
			NULL,
			NULL,
			sizeof (RBRandomPlayOrderEqualWeights),
			0,
			NULL
		};

		rb_random_play_order_equal_weights_type = g_type_register_static (RB_TYPE_RANDOM_PLAY_ORDER,
				"RBRandomPlayOrderEqualWeights",
				&our_info, 0);
	}

	return rb_random_play_order_equal_weights_type;
}

static void
rb_random_play_order_equal_weights_class_init (RBRandomPlayOrderEqualWeightsClass *klass)
{
	RBRandomPlayOrderClass *rorder;

	parent_class = g_type_class_peek_parent (klass);

	rorder = RB_RANDOM_PLAY_ORDER_CLASS (klass);
	rorder->get_entry_weight = rb_random_equal_weights_get_entry_weight;
}

RBPlayOrder *
rb_random_play_order_equal_weights_new (RBShellPlayer *player)
{
	RBRandomPlayOrderEqualWeights *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS,
			"player", player,
			NULL);

	return RB_PLAY_ORDER (rorder);
}

static double
rb_random_equal_weights_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db, RhythmDBEntry *entry)
{
	return 1.0;
}
