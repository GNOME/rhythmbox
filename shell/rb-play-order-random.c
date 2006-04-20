/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of base class for weighted random play orders
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/* This class is a base case for weighted random play orders. Subclasses only
 * need to override get_entry_weight() to return the right weight for a given
 * entry.
 *
 * This class also delays committing any changes until the user moves to the
 * next or previous song. So if the user changes the entry-view to contain
 * different songs, but changes it back before the current song finishes, they
 * will not see any changes to their history of played songs.
 */

#include <string.h>

#include "rb-play-order-random-by-age.h"

#include "rb-debug.h"
#include "rhythmdb.h"
#include "rb-history.h"

static void rb_random_play_order_class_init (RBRandomPlayOrderClass *klass);
static void rb_random_play_order_init (RBRandomPlayOrder *rorder);

static void rb_random_play_order_finalize (GObject *object);

static RhythmDBEntry* rb_random_play_order_get_next (RBPlayOrder* porder);
static void rb_random_play_order_go_next (RBPlayOrder* porder);
static RhythmDBEntry* rb_random_play_order_get_previous (RBPlayOrder* porder);
static void rb_random_play_order_go_previous (RBPlayOrder* porder);

static void rb_random_db_changed (RBPlayOrder *porder, RhythmDB *db);
static void rb_random_playing_entry_changed (RBPlayOrder *porder, 
					     RhythmDBEntry *old_entry, 
					     RhythmDBEntry *new_entry);
static void rb_random_query_model_changed (RBPlayOrder *porder);
static void rb_random_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry);

static void rb_random_handle_query_model_changed (RBRandomPlayOrder *rorder);
static void rb_random_filter_history (RBRandomPlayOrder *rorder, RhythmDBQueryModel *model);

struct RBRandomPlayOrderPrivate
{
	RBHistory *history;

	/* Updates are made to the tentative_history, which is then copied
	 * over the real one when go_{next,previous} are called. */
	RBHistory *tentative_history;

	gboolean query_model_changed;
};

G_DEFINE_TYPE (RBRandomPlayOrder, rb_random_play_order, RB_TYPE_PLAY_ORDER)
#define RB_RANDOM_PLAY_ORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_RANDOM_PLAY_ORDER, RBRandomPlayOrderPrivate))


static void
rb_random_play_order_class_init (RBRandomPlayOrderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPlayOrderClass *porder;

	object_class->finalize = rb_random_play_order_finalize;

	porder = RB_PLAY_ORDER_CLASS (klass);
	porder->db_changed = rb_random_db_changed;
	porder->playing_entry_changed = rb_random_playing_entry_changed;
	porder->entry_added = (void (*)(RBPlayOrder*,RhythmDBEntry*))rb_random_query_model_changed;
	porder->entry_removed = (void (*)(RBPlayOrder*,RhythmDBEntry*))rb_random_query_model_changed;
	porder->query_model_changed = rb_random_query_model_changed;
	porder->db_entry_deleted = rb_random_db_entry_deleted;

	porder->has_next = rb_play_order_model_not_empty;
	porder->get_next = rb_random_play_order_get_next;
	porder->go_next = rb_random_play_order_go_next;
	porder->get_previous = rb_random_play_order_get_previous;
	porder->go_previous = rb_random_play_order_go_previous;

	g_type_class_add_private (klass, sizeof (RBRandomPlayOrderPrivate));
}

static void
rb_random_play_order_init (RBRandomPlayOrder *rorder)
{
	rorder->priv = RB_RANDOM_PLAY_ORDER_GET_PRIVATE (rorder);

	rorder->priv->history = rb_history_new (TRUE,
						(GFunc) rb_play_order_unref_entry_swapped,
					       	rb_play_order_get_db (RB_PLAY_ORDER (rorder)));
	rb_history_set_maximum_size (rorder->priv->history, 50);

	rorder->priv->query_model_changed = TRUE;
}

static void
rb_random_play_order_finalize (GObject *object)
{
	RBRandomPlayOrder *rorder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (object));

	rorder = RB_RANDOM_PLAY_ORDER (object);

	g_object_unref (G_OBJECT (rorder->priv->history));
	if (rorder->priv->tentative_history)
		g_object_unref (G_OBJECT (rorder->priv->tentative_history));

	G_OBJECT_CLASS (rb_random_play_order_parent_class)->finalize (object);
}

static double
rb_random_play_order_get_entry_weight (RBRandomPlayOrder *rorder, RhythmDB *db,
				       RhythmDBEntry *entry)
{
	g_return_val_if_fail (rorder != NULL, 1.0);
	g_return_val_if_fail (RB_RANDOM_PLAY_ORDER_GET_CLASS (rorder)->get_entry_weight != NULL, 1.0);
	return RB_RANDOM_PLAY_ORDER_GET_CLASS (rorder)->get_entry_weight (rorder, db, entry);
}

static inline RBHistory *
get_history (RBRandomPlayOrder *rorder)
{
	if (rorder->priv->tentative_history)
		return rorder->priv->tentative_history;
	else
		return rorder->priv->history;
}

typedef struct {
	RhythmDBEntry *entry;
	double weight;
	double cumulative_weight;
} EntryWeight;

static GArray *
get_query_model_contents (RBRandomPlayOrder *rorder, RhythmDBQueryModel *model)
{
	guint num_entries;
	guint i;
	RhythmDB *db;
	double weight = 0.0;
	double cumulative_weight = 0.0;
	GtkTreeIter iter;
	GArray *result = g_array_new (FALSE, FALSE, sizeof (EntryWeight));

	if (model == NULL)
		return result;

	num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	if (num_entries == 0)
		return result;

	g_array_set_size (result, num_entries);
	db = rb_play_order_get_db (RB_PLAY_ORDER (rorder));

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
		return result;

	i = 0;
	do {
		RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry (model, &iter);
		if (!entry)
			continue;
		weight = rb_random_play_order_get_entry_weight (rorder, db, entry);

		g_array_index (result, EntryWeight, i).entry = entry;
		g_array_index (result, EntryWeight, i).weight = weight;
		g_array_index (result, EntryWeight, i).cumulative_weight = cumulative_weight;
		cumulative_weight += weight;
		i++;
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));

	return result;
}

static void
rb_random_handle_query_model_changed (RBRandomPlayOrder *rorder)
{
	RhythmDBQueryModel *model;

	if (!rorder->priv->query_model_changed)
		return;

	model = rb_play_order_get_query_model (RB_PLAY_ORDER (rorder));
	rb_random_filter_history (rorder, model);
	rorder->priv->query_model_changed = FALSE;
}

static void
rb_random_filter_history (RBRandomPlayOrder *rorder, RhythmDBQueryModel *model)
{
	GPtrArray *history_contents;
	int i;

	if (rorder->priv->tentative_history) {
		g_object_unref (rorder->priv->tentative_history);
		rorder->priv->tentative_history = NULL;
	}
	history_contents = rb_history_dump (rorder->priv->history);
	for (i=0; i < history_contents->len; ++i) {
		gboolean remove = TRUE;
		if (model) {
			GtkTreeIter iter;
			if (rhythmdb_query_model_entry_to_iter (model, g_ptr_array_index (history_contents, i), &iter))
				remove = FALSE;
		}

		if (remove) {
			if (!rorder->priv->tentative_history)
				rorder->priv->tentative_history
				       	= rb_history_clone (rorder->priv->history,
							    (GFunc) rb_play_order_ref_entry_swapped,
							    rb_play_order_get_db (RB_PLAY_ORDER (rorder)));
			rb_history_remove_entry (rorder->priv->tentative_history, 
						 g_ptr_array_index (history_contents, i));
		}
	}

	g_ptr_array_free (history_contents, TRUE);
}

static void
rb_random_commit_history (RBRandomPlayOrder *rorder)
{
	if (rorder->priv->tentative_history) {
		g_object_unref (rorder->priv->history);
		rorder->priv->history = rorder->priv->tentative_history;
		rorder->priv->tentative_history = NULL;
	}
}

static inline double
rb_random_get_total_weight (GArray *weights)
{
	EntryWeight *last;
       
	g_return_val_if_fail (weights, 0.0);
	if (weights->len == 0)
		return 0.0;

	last = &g_array_index (weights,
			       EntryWeight,
			       weights->len - 1);
	return last->cumulative_weight + last->weight;
}

static RhythmDBEntry*
rb_random_play_order_pick_entry (RBRandomPlayOrder *rorder)
{
	/* This is O(N) because we traverse the query model to get the entries
	 * and weights.
	 *
	 * The general idea of this algorithm is that there is a line segment
	 * whose length is the sum of all the entries' weights. Each entry gets
	 * a sub-segment whose length is equal to that entry's weight. A random
	 * point is picked in the line segment, and the entry that point
	 * belongs to is returned.
	 *
	 * The algorithm was contributed by treed.
	 */
	double total_weight, rnd;
	int high, low;
	GArray *entry_weights;
	RhythmDBEntry *entry;
	RhythmDBQueryModel *model;

	model = rb_play_order_get_query_model (RB_PLAY_ORDER (rorder));

	entry_weights = get_query_model_contents (rorder, model);

	total_weight = rb_random_get_total_weight (entry_weights);
	if (total_weight == 0.0)
		return NULL;

	rnd = g_random_double_range (0, total_weight);
	/* Binary search for the entry with cumulative weight closest to but
	 * less than rnd */
	low = -1; high = entry_weights->len;
	while (high - low > 1) {
		int mid = (high + low) / 2;
		if (g_array_index (entry_weights, EntryWeight, mid).cumulative_weight > rnd)
			high = mid;
		else
			low = mid;
	}
	entry = g_array_index (entry_weights, EntryWeight, low).entry;

	g_array_free (entry_weights, TRUE);

	return entry;
}

static RhythmDBEntry* 
rb_random_play_order_get_next (RBPlayOrder* porder)
{
	RBRandomPlayOrder *rorder;
	RhythmDBEntry *entry;
	RBHistory *history;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER (porder);

	rb_random_handle_query_model_changed (rorder);
	history = get_history (rorder);

	entry = rb_play_order_get_playing_entry (porder);
	if (rb_history_length (history) == 0 
	    || (entry == rb_history_current (history)
	        && rb_history_current (history) == rb_history_last (history))) {

		rb_debug ("choosing random entry");
		entry = rb_random_play_order_pick_entry (rorder);
		if (entry) {
			rhythmdb_entry_ref (rb_play_order_get_db (porder), entry);
			rb_history_append (history, entry);
		}
	} else {
		rb_debug ("choosing enqueued entry");
		if (entry == rb_history_current (history))
			entry = rb_history_next (history);
		else
			entry = rb_history_current (history);
	}

	return entry;
}

static void
rb_random_play_order_go_next (RBPlayOrder* porder)
{
	RBRandomPlayOrder *rorder;
	RhythmDBEntry *entry;
	RBHistory *history;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));

	rorder = RB_RANDOM_PLAY_ORDER (porder);
	history = get_history (rorder);

	rb_random_play_order_get_next (porder);

	g_object_get (G_OBJECT (rorder), "playing-entry", &entry, NULL);
	g_assert (entry == NULL || rb_history_current (history) == NULL || entry == rb_history_current (history));
	if (rb_history_current (history) == NULL)
		rb_history_go_first (history);
	else
		rb_history_go_next (history);
	rb_play_order_set_playing_entry (porder, rb_history_current (history));
}

static RhythmDBEntry*
rb_random_play_order_get_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrder *rorder;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER (porder);

	rb_random_handle_query_model_changed (rorder);

	rb_debug ("choosing history entry");
	return rb_history_previous (get_history (rorder));
}

static void
rb_random_play_order_go_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrder *rorder;
	RBHistory *history;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));
	/* It doesn't make sense to call go_previous when the player is stopped */
	g_return_if_fail (rb_play_order_player_is_playing (porder));

	rorder = RB_RANDOM_PLAY_ORDER (porder);
	history = get_history (rorder);

	rb_history_go_previous (history);
	rb_play_order_set_playing_entry (porder, rb_history_current (history));
}

static void
rb_random_db_changed (RBPlayOrder *porder, RhythmDB *db)
{
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));

	rb_history_clear (RB_RANDOM_PLAY_ORDER (porder)->priv->history); 

	rb_history_set_destroy_notify (RB_RANDOM_PLAY_ORDER (porder)->priv->history,
				       (GFunc) rb_play_order_unref_entry_swapped,
				       db);
}

static void
rb_random_playing_entry_changed (RBPlayOrder *porder, 
				 RhythmDBEntry *old_entry,
				 RhythmDBEntry *new_entry)
{
	RBRandomPlayOrder *rorder;

	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));
	rorder = RB_RANDOM_PLAY_ORDER (porder);

	rb_random_commit_history (rorder);

	if (new_entry) {
		if (new_entry == rb_history_current (get_history (rorder))) {
			/* Do nothing */
		} else {
			rhythmdb_entry_ref (rb_play_order_get_db (porder), new_entry);
			rb_history_set_playing (get_history (rorder), new_entry);
		}
	}
}

static void
rb_random_query_model_changed (RBPlayOrder *porder)
{
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));
	RB_RANDOM_PLAY_ORDER (porder)->priv->query_model_changed = TRUE;
}

static void
rb_random_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	/* When an entry is deleted from the database, it needs to vanish from
	 * all histories immediately. */
	RBRandomPlayOrder *rorder;
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER (porder));

	rorder = RB_RANDOM_PLAY_ORDER (porder);
	rb_history_remove_entry (rorder->priv->history, entry); 
	if (rorder->priv->tentative_history)
		rb_history_remove_entry (rorder->priv->tentative_history, entry); 
}
