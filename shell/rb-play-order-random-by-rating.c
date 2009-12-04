/*
 *  Copyright (C) 2003 Jeffrey Yasskin <jyasskin@mail.utexas.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include "rb-play-order-random-by-rating.h"

#include "rhythmdb.h"
#include <time.h>
#include <math.h>

static void rb_random_play_order_by_rating_class_init (RBRandomPlayOrderByRatingClass *klass);

static double rb_random_by_rating_get_entry_weight (RBRandomPlayOrder *rorder,
						 RhythmDB *db, RhythmDBEntry *entry);

G_DEFINE_TYPE (RBRandomPlayOrderByRating, rb_random_play_order_by_rating, RB_TYPE_RANDOM_PLAY_ORDER)

static void
rb_random_play_order_by_rating_class_init (RBRandomPlayOrderByRatingClass *klass)
{
	RBRandomPlayOrderClass *rorder;

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

static void
rb_random_play_order_by_rating_init (RBRandomPlayOrderByRating *porder)
{
}

static double
rb_random_by_rating_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db, RhythmDBEntry *entry)
{
	double rating;

	rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);
	if (rating < 0.01)
		rating = 2.5;

	return rating;
}
