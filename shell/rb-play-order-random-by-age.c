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

#include "rb-debug.h"
#include "rhythmdb.h"
#include "rb-history.h"
#include <string.h>
#include <time.h>
#include <math.h>

static void rb_random_play_order_by_age_class_init (RBRandomPlayOrderByAgeClass *klass);
static void rb_random_play_order_by_age_init (RBRandomPlayOrderByAge *rorder);

static GObject *rb_random_play_order_by_age_constructor (GType type, guint n_construct_properties,
								GObjectConstructParam *construct_properties);
static void rb_random_play_order_by_age_finalize (GObject *object);

static RhythmDBEntry* rb_random_play_order_by_age_get_next (RBPlayOrder* porder);
static void rb_random_play_order_by_age_go_next (RBPlayOrder* porder);
static RhythmDBEntry* rb_random_play_order_by_age_get_previous (RBPlayOrder* porder);
static void rb_random_play_order_by_age_go_previous (RBPlayOrder* porder);

static void playing_source_changed_cb (RBRandomPlayOrderByAge *rorder);
static void entry_view_playing_entry_changed_cb (GObject *entry_view,
						 GParamSpec *pspec,
						 RBRandomPlayOrderByAge *rorder);


struct RBRandomPlayOrderByAgePrivate
{
	RBHistory *history;

	RBSource *source;
};

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
			(GInstanceInitFunc) rb_random_play_order_by_age_init
		};

		rb_random_play_order_by_age_type = g_type_register_static (RB_TYPE_PLAY_ORDER,
				"RBRandomPlayOrderByAge",
				&our_info, 0);
	}

	return rb_random_play_order_by_age_type;
}

static void
rb_random_play_order_by_age_class_init (RBRandomPlayOrderByAgeClass *klass)
{
	RBPlayOrderClass *porder;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->constructor = rb_random_play_order_by_age_constructor;
	object_class->finalize = rb_random_play_order_by_age_finalize;


	porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_random_play_order_by_age_get_next;
	porder->go_next = rb_random_play_order_by_age_go_next;
	porder->get_previous = rb_random_play_order_by_age_get_previous;
	porder->go_previous = rb_random_play_order_by_age_go_previous;
}

RBRandomPlayOrderByAge *
rb_random_play_order_by_age_new (RBShellPlayer *player)
{
	RBRandomPlayOrderByAge *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE,
			"player", player,
			NULL);

	g_return_val_if_fail (rorder->priv != NULL, NULL);
	g_return_val_if_fail (rorder->priv->history != NULL, NULL);

	return rorder;
}

static void
rb_random_play_order_by_age_init (RBRandomPlayOrderByAge *rorder)
{
	rorder->priv = g_new0 (RBRandomPlayOrderByAgePrivate, 1);
}

static GObject *
rb_random_play_order_by_age_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties)
{
	RBRandomPlayOrderByAge *rorder;
	RBShellPlayer *player;

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (G_OBJECT_CLASS (parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	player = rb_play_order_get_player (RB_PLAY_ORDER (rorder));

	rorder->priv->history = rb_history_new (TRUE);
	g_object_set (G_OBJECT (rorder->priv->history),
		      "maximum-size", 50,
		      NULL);

	g_signal_connect_swapped (G_OBJECT (player),
			"notify::playing-source",
			G_CALLBACK (playing_source_changed_cb),
			rorder);
	/* Initialize source */
	playing_source_changed_cb (rorder);

	/* Tell the history about the currently-playing song */
	if (rorder->priv->source) {
		RBEntryView *entry_view = rb_source_get_entry_view (rorder->priv->source);
		if (entry_view) {
			RhythmDBEntry *current_entry;
			g_object_get (entry_view,
					"playing-entry", &current_entry,
					NULL);
			if (current_entry)
				rb_history_set_playing (rorder->priv->history, current_entry);
		}
	}

	return G_OBJECT (rorder);
}

static void
rb_random_play_order_by_age_finalize (GObject *object)
{
	RBRandomPlayOrderByAge *rorder;
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_BY_AGE (object));

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (object);

	player = rb_play_order_get_player (RB_PLAY_ORDER (rorder));

	g_signal_handlers_disconnect_by_func (G_OBJECT (player),
					      G_CALLBACK (playing_source_changed_cb),
					      rorder);


	if (rorder->priv->source) {
		RBEntryView *entry_view = rb_source_get_entry_view (rorder->priv->source);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (entry_view_playing_entry_changed_cb),
						      rorder);
	}

	g_object_unref (G_OBJECT (rorder->priv->history));
	g_free (rorder->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static double
rb_random_by_age_entry_weight (RhythmDB *db, RhythmDBEntry *entry)
{
	time_t now;
	glong last_play;
	glong seconds_since_last_play;

	/* This returns the log of the number of seconds since the last play.
	 * It handles never played automatically, since now-0 is a valid
	 * argument to log(). */
	
	time (&now);
	last_play = rhythmdb_entry_get_long (db, entry, RHYTHMDB_PROP_LAST_PLAYED);
	seconds_since_last_play = now - last_play;
	/* The lowest weight should be 0. */
	if (seconds_since_last_play < 1)
		seconds_since_last_play = 1;
	return log (seconds_since_last_play);
}

typedef struct {
	RhythmDBEntry *entry;
	double weight;
} EntryWeight;

static GArray *
get_entry_view_contents (RBEntryView *entry_view)
{
	guint num_entries;
	guint i;
	GArray *result = g_array_new (FALSE, FALSE, sizeof (EntryWeight));
	RhythmDB *db;

	if (entry_view == NULL)
		return result;

	num_entries = rb_entry_view_get_num_entries (entry_view);
	if (num_entries == 0)
		return result;

	g_array_set_size (result, num_entries);
	g_object_get (G_OBJECT (entry_view),
		      "db", &db,
		      NULL);

	rhythmdb_read_lock (db);
	g_array_index (result, EntryWeight, 0).entry = rb_entry_view_get_first_entry (entry_view);
	g_array_index (result, EntryWeight, 0).weight
		= rb_random_by_age_entry_weight (db, g_array_index (result, EntryWeight, 0).entry);
	for (i=1; i<num_entries; ++i) {
		g_array_index (result, EntryWeight, i).entry
			= rb_entry_view_get_next_from_entry (entry_view, g_array_index (result, EntryWeight, i-1).entry);
		g_array_index (result, EntryWeight, i).weight
			= rb_random_by_age_entry_weight (db, g_array_index (result, EntryWeight, i).entry);

	}
	rhythmdb_read_unlock (db);
	return result;
}


static RhythmDBEntry*
rb_random_play_order_pick_entry (RBEntryView *entry_view)
{
	/* This is O(N) because we have to scan the entry_view anyway. If we
	 * cache the entry view contents and store the "weight-so-far" instead
	 * of the individual song's weight, we can get O(lg(N)) with a binary
	 * search here. But updates will then be O(N) because they'll have to
	 * update the weights-so-far. Can the GSequence help here? */
	double total_weight;
	double rnd;
	RhythmDBEntry *result = NULL;
	GArray *entries = get_entry_view_contents (entry_view);
	int i;

	if (entries->len == 0) {
		/* entry view empty */
		return result;
	}

	/* Algorithm due to treed */

	total_weight = 0;
	for (i=0; i<entries->len; ++i) {
		total_weight += g_array_index (entries, EntryWeight, i).weight;
	}
	rnd = g_random_double_range (0, total_weight);
	for (i=0; i<entries->len; ++i) {
		if (rnd < g_array_index (entries, EntryWeight, i).weight) {
			result = g_array_index (entries, EntryWeight, i).entry;
			break;
		}
		rnd -= g_array_index (entries, EntryWeight, i).weight;
	}
	g_array_free (entries, TRUE);
	return result;
}

static RhythmDBEntry* 
rb_random_play_order_by_age_get_next (RBPlayOrder* porder)
{
	RBRandomPlayOrderByAge *rorder;
	RhythmDBEntry *entry = NULL;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER_BY_AGE (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (porder);

	if (rb_history_current (rorder->priv->history) == NULL
			|| (rb_play_order_player_is_playing (porder)
				&& rb_history_current (rorder->priv->history) == rb_history_last (rorder->priv->history))) {
		RBEntryView *entry_view;
		entry_view = rb_play_order_get_entry_view (porder);
		if (entry_view == NULL)
			return NULL;

		rb_debug ("choosing random entry");
		entry = rb_random_play_order_pick_entry (entry_view);
		if (entry) {
			/* make sure subsequent get_next() calls return the same entry. */
			rb_history_append (rorder->priv->history, entry);
		}
	} else {
		rb_debug ("choosing enqueued entry");
		if (rb_play_order_player_is_playing (porder))
			entry = rb_history_next (rorder->priv->history);
		else
			entry = rb_history_current (rorder->priv->history);
	}

	return entry;
}

static void
rb_random_play_order_by_age_go_next (RBPlayOrder* porder)
{
	RBRandomPlayOrderByAge *rorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_BY_AGE (porder));

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (porder);

	if (rb_play_order_player_is_playing (porder))
		rb_history_go_next (rorder->priv->history);
	else {
		/* Leave the current entry current */
	}
}

static RhythmDBEntry*
rb_random_play_order_by_age_get_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrderByAge *rorder;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER_BY_AGE (porder), NULL);
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_val_if_fail (rb_play_order_player_is_playing (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (porder);

	rb_debug ("choosing history entry");
	return rb_history_previous (rorder->priv->history);
}

static void
rb_random_play_order_by_age_go_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrderByAge *rorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_BY_AGE (porder));
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_if_fail (rb_play_order_player_is_playing (porder));

	rorder = RB_RANDOM_PLAY_ORDER_BY_AGE (porder);

	rb_history_go_previous (rorder->priv->history);
}

static void
playing_source_changed_cb (RBRandomPlayOrderByAge *rorder)
{
	RBShellPlayer *player;
	RBSource *source;
	RhythmDB *db = NULL;

	player = rb_play_order_get_player (RB_PLAY_ORDER (rorder));

	source = rb_shell_player_get_playing_source (player);
	if (source) {
		g_object_get (G_OBJECT (source),
				"db", &db,
				NULL);
	}

	g_object_set (G_OBJECT (rorder->priv->history), 
		      "db", db, 
		      NULL);

	if (source != rorder->priv->source) {
		if (rorder->priv->source != NULL) {
			RBEntryView *entry_view = rb_source_get_entry_view (rorder->priv->source);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
					G_CALLBACK (entry_view_playing_entry_changed_cb),
					rorder);
		}
		rorder->priv->source = source;
		if (rorder->priv->source != NULL) {
			RBEntryView *entry_view = rb_source_get_entry_view (rorder->priv->source);
			g_signal_connect (G_OBJECT (entry_view),
					"notify::playing-entry",
					G_CALLBACK (entry_view_playing_entry_changed_cb),
					rorder);

		}
	}

}

static void
entry_view_playing_entry_changed_cb (GObject *entry_view,
				     GParamSpec *pspec,
				     RBRandomPlayOrderByAge *rorder)
{
	RhythmDBEntry *entry;

	g_return_if_fail (strcmp (pspec->name, "playing-entry") == 0);

	g_object_get (entry_view,
		      "playing-entry", &entry,
		      NULL);
	if (entry) {
		if (entry == rb_history_current (rorder->priv->history)) {
			/* Do nothing */
		} else {
			rb_history_set_playing (rorder->priv->history, entry);
		}
	}
}
