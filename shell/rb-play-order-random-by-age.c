/* 
 *  arch-tag: Implementation of random play order weighted by the time since last play
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

#include "rb-play-order-random-by-age.h"

#include "rhythmdb.h"
#include <time.h>
#include <math.h>

static void rb_random_play_order_by_age_class_init (RBRandomPlayOrderByAgeClass *klass);

static double rb_random_by_age_get_entry_weight (RBRandomPlayOrder *rorder,
						 RhythmDB *db, RhythmDBEntry *entry);

static RBPlayOrderClass *parent_class = NULL;

GType
rb_random_play_order_by_age_get_type (void)
{
	static GType rb_random_play_order_by_age_type = 0;

	if (rb_random_play_order_by_age_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBRandomPlayOrderByAgeClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_random_play_order_by_age_class_init,
			NULL,
			NULL,
			sizeof (RBRandomPlayOrderByAge),
			0,
			NULL
		};

		rb_random_play_order_by_age_type = g_type_register_static (RB_TYPE_RANDOM_PLAY_ORDER,
				"RBRandomPlayOrderByAge",
				&our_info, 0);
	}

	return rb_random_play_order_by_age_type;
}

static void
rb_random_play_order_by_age_class_init (RBRandomPlayOrderByAgeClass *klass)
{
	RBRandomPlayOrderClass *rorder;

	parent_class = g_type_class_peek_parent (klass);

	rorder = RB_RANDOM_PLAY_ORDER_CLASS (klass);
	rorder->get_entry_weight = rb_random_by_age_get_entry_weight;
}

RBPlayOrder *
rb_random_play_order_by_age_new (RBShellPlayer *player)
{
	RBRandomPlayOrderByAge *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE,
			"player", player,
			NULL);

	return RB_PLAY_ORDER (rorder);
}

static double
rb_random_by_age_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db, RhythmDBEntry *entry)
{
	time_t now;
	glong last_play;
	glong seconds_since_last_play;

	/* This returns the log of the number of seconds since the last play.
	 * It handles never played automatically, since now-0 is a valid
	 * argument to log(). */
	
	time (&now);
	last_play = entry->last_played;
	seconds_since_last_play = now - last_play;
	/* The lowest weight should be 0. */
	if (seconds_since_last_play < 1)
		seconds_since_last_play = 1;
	return log (seconds_since_last_play);
}
