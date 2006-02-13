/* 
 *  arch-tag: Implementation of random play order weighted by the time since last play and the rating
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

#include "rb-play-order-random-by-age-and-rating.h"

#include "rhythmdb.h"
#include <time.h>
#include <math.h>

static void rb_random_play_order_by_age_and_rating_class_init (RBRandomPlayOrderByAgeAndRatingClass *klass);

static double rb_random_by_age_and_rating_get_entry_weight (RBRandomPlayOrder *rorder,
						 RhythmDB *db, RhythmDBEntry *entry);

G_DEFINE_TYPE (RBRandomPlayOrderByAgeAndRating,
	       rb_random_play_order_by_age_and_rating,
	       RB_TYPE_RANDOM_PLAY_ORDER)

static void
rb_random_play_order_by_age_and_rating_class_init (RBRandomPlayOrderByAgeAndRatingClass *klass)
{
	RBRandomPlayOrderClass *rorder;

	rorder = RB_RANDOM_PLAY_ORDER_CLASS (klass);
	rorder->get_entry_weight = rb_random_by_age_and_rating_get_entry_weight;
}

RBPlayOrder *
rb_random_play_order_by_age_and_rating_new (RBShellPlayer *player)
{
	RBRandomPlayOrderByAgeAndRating *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE_AND_RATING,
			"player", player,
			NULL);

	return RB_PLAY_ORDER (rorder);
}

static void
rb_random_play_order_by_age_and_rating_init (RBRandomPlayOrderByAgeAndRating *porder)
{
}

static double
rb_random_by_age_and_rating_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db, RhythmDBEntry *entry)
{
	time_t now;
	gulong last_play;
	gulong seconds_since_last_play;
	gdouble rating;

	/* This finds the log of the number of seconds since the last play.
	 * It handles never played automatically, since now-0 is a valid
	 * argument to log(). */
	time (&now);
	last_play = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_LAST_PLAYED);
	seconds_since_last_play = now - last_play;
	/* The lowest weight should be 0. */
	if (seconds_since_last_play < 1)
		seconds_since_last_play = 1;

	rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);

	/* treat unrated as 2.5 for the purposes of probabilities */
	if (rating < 0.01)
		rating = 2.5;

	return log (seconds_since_last_play) * (rating + 1.0);
}
