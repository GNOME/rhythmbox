/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <string.h>

#include "rb-play-order-shuffle.h"

#include "rb-history.h"
#include "rb-debug.h"
#include "rb-util.h"

static void rb_shuffle_play_order_class_init (RBShufflePlayOrderClass *klass);
static void rb_shuffle_play_order_init (RBShufflePlayOrder *sorder);
static void rb_shuffle_play_order_dispose (GObject *object);
static void rb_shuffle_play_order_finalize (GObject *object);

static RhythmDBEntry* rb_shuffle_play_order_get_next (RBPlayOrder* method);
static void rb_shuffle_play_order_go_next (RBPlayOrder* method);
static RhythmDBEntry* rb_shuffle_play_order_get_previous (RBPlayOrder* method);
static void rb_shuffle_play_order_go_previous (RBPlayOrder* method);

static void rb_shuffle_sync_history_with_query_model (RBShufflePlayOrder *sorder);
static GPtrArray *get_query_model_contents (RhythmDBQueryModel *model);

static void rb_shuffle_db_changed (RBPlayOrder *porder, RhythmDB *db);
static void rb_shuffle_playing_entry_changed (RBPlayOrder *porder,
					      RhythmDBEntry *old_entry,
					      RhythmDBEntry *new_entry);
static void rb_shuffle_entry_added (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_shuffle_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_shuffle_query_model_changed (RBPlayOrder *porder);
static void rb_shuffle_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry);
static gboolean query_model_and_history_contents_match (RBShufflePlayOrder *sorder);

struct RBShufflePlayOrderPrivate
{
	RBHistory *history;

	/* TRUE if the query model has been changed */
	gboolean query_model_changed;

	GHashTable *entries_removed;
	GHashTable *entries_added;

	/* stores the playing entry if it comes from outside the query model */
	RhythmDBEntry *external_playing_entry;
};

G_DEFINE_TYPE (RBShufflePlayOrder, rb_shuffle_play_order, RB_TYPE_PLAY_ORDER)
#define RB_SHUFFLE_PLAY_ORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHUFFLE_PLAY_ORDER, RBShufflePlayOrderPrivate))

RBPlayOrder *
rb_shuffle_play_order_new (RBShellPlayer *player)
{
	RBShufflePlayOrder *sorder;

	sorder = g_object_new (RB_TYPE_SHUFFLE_PLAY_ORDER,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (sorder);
}

static void
rb_shuffle_play_order_class_init (RBShufflePlayOrderClass *klass)
{
	RBPlayOrderClass *porder;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_shuffle_play_order_dispose;
	object_class->finalize = rb_shuffle_play_order_finalize;

	porder = RB_PLAY_ORDER_CLASS (klass);

	porder->db_changed = rb_shuffle_db_changed;
	porder->playing_entry_changed = rb_shuffle_playing_entry_changed;
	porder->entry_added = rb_shuffle_entry_added;
	porder->entry_removed = rb_shuffle_entry_removed;
	porder->query_model_changed = rb_shuffle_query_model_changed;
	porder->db_entry_deleted = rb_shuffle_db_entry_deleted;

	porder->has_next = rb_play_order_model_not_empty;
	porder->has_previous = rb_play_order_model_not_empty;
	porder->get_next = rb_shuffle_play_order_get_next;
	porder->go_next = rb_shuffle_play_order_go_next;
	porder->get_previous = rb_shuffle_play_order_get_previous;
	porder->go_previous = rb_shuffle_play_order_go_previous;

	g_type_class_add_private (klass, sizeof (RBShufflePlayOrderPrivate));
}

static void
rb_shuffle_play_order_init (RBShufflePlayOrder *sorder)
{
	sorder->priv = RB_SHUFFLE_PLAY_ORDER_GET_PRIVATE (sorder);

	sorder->priv->history = rb_history_new (FALSE,
						(GFunc) rhythmdb_entry_unref,
					       	NULL);

	sorder->priv->query_model_changed = FALSE;
	sorder->priv->entries_added = g_hash_table_new_full (g_direct_hash, g_direct_equal,
							     (GDestroyNotify)rhythmdb_entry_unref, NULL);
	sorder->priv->entries_removed = g_hash_table_new_full (g_direct_hash, g_direct_equal,
							       (GDestroyNotify)rhythmdb_entry_unref, NULL);
}

static void
rb_shuffle_play_order_dispose (GObject *object)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (object));

	sorder = RB_SHUFFLE_PLAY_ORDER (object);

	if (sorder->priv->external_playing_entry != NULL) {
		rhythmdb_entry_unref (sorder->priv->external_playing_entry);
		sorder->priv->external_playing_entry = NULL;
	}

	if (sorder->priv->history != NULL) {
		g_object_unref (sorder->priv->history);
		sorder->priv->history = NULL;
	}

	G_OBJECT_CLASS (rb_shuffle_play_order_parent_class)->dispose (object);
}

static void
rb_shuffle_play_order_finalize (GObject *object)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (object));

	sorder = RB_SHUFFLE_PLAY_ORDER (object);

	g_hash_table_destroy (sorder->priv->entries_added);
	g_hash_table_destroy (sorder->priv->entries_removed);

	G_OBJECT_CLASS (rb_shuffle_play_order_parent_class)->finalize (object);
}

static RhythmDBEntry*
rb_shuffle_play_order_get_next (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;
	RhythmDBEntry *entry;
	RhythmDBEntry *current;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder), NULL);

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_shuffle_sync_history_with_query_model (sorder);

	current = rb_play_order_get_playing_entry (porder);
	entry = NULL;

	if (current != NULL &&
	    (current == sorder->priv->external_playing_entry ||
	    current == rb_history_current (sorder->priv->history))) {
		if (rb_history_current (sorder->priv->history) != rb_history_last (sorder->priv->history)) {
			rb_debug ("choosing next entry in shuffle");
			entry = rb_history_next (sorder->priv->history);
			if (entry)
				rhythmdb_entry_ref (entry);
		}
	} else {
		/* If the player is currently stopped, the "next" (first) song
		 * is the first in the shuffle. */
		rb_debug ("choosing current entry in shuffle");
		entry = rb_history_current (sorder->priv->history);

		if (entry == NULL)
			entry = rb_history_first (sorder->priv->history);

		if (entry != NULL)
			rhythmdb_entry_ref (entry);
	}

	if (current)
		rhythmdb_entry_unref (current);
	return entry;
}

static void
rb_shuffle_play_order_go_next (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;
	RhythmDBEntry *entry;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	entry = rb_play_order_get_playing_entry (porder);
	g_assert (entry == NULL ||
		  rb_history_current (sorder->priv->history) == NULL ||
		  (entry == sorder->priv->external_playing_entry ||
		  entry == rb_history_current (sorder->priv->history)));

	if (rb_history_current (sorder->priv->history) == NULL)  {
		rb_history_go_first (sorder->priv->history);
	} else if (entry == rb_history_current (sorder->priv->history) ||
		   (sorder->priv->external_playing_entry != NULL &&
		    entry == sorder->priv->external_playing_entry)) {
		if (rb_history_current (sorder->priv->history) != rb_history_last (sorder->priv->history))
			rb_history_go_next (sorder->priv->history);
	}

	rb_play_order_set_playing_entry (porder, rb_history_current (sorder->priv->history));

	if (entry)
		rhythmdb_entry_unref (entry);
}

static RhythmDBEntry*
rb_shuffle_play_order_get_previous (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder), NULL);
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_val_if_fail (rb_play_order_player_is_playing (porder), NULL);

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_shuffle_sync_history_with_query_model (sorder);

	if (sorder->priv->external_playing_entry != NULL) {
		rb_debug ("playing from outside the query model; previous is current");
		entry = rb_history_current (sorder->priv->history);
	} else {
		rb_debug ("choosing previous history entry");
		entry = rb_history_previous (sorder->priv->history);
	}

	if (entry)
		rhythmdb_entry_ref (entry);

	return entry;
}

static void
rb_shuffle_play_order_go_previous (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	/* It doesn't make sense to call go_previous when the player is stopped */
	g_return_if_fail (rb_play_order_player_is_playing (porder));

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	if (sorder->priv->external_playing_entry != NULL) {
		/* if we were playing an external entry, the current entry
		 * is the history is the one before it.
		 */
		rb_play_order_set_playing_entry (porder, rb_history_current (sorder->priv->history));

		rhythmdb_entry_unref (sorder->priv->external_playing_entry);
		sorder->priv->external_playing_entry = NULL;
	} else {
		if (rb_history_current (sorder->priv->history) != rb_history_first (sorder->priv->history)) {
			rb_history_go_previous (sorder->priv->history);
			rb_play_order_set_playing_entry (porder, rb_history_current (sorder->priv->history));
		}
	}
}

static void
handle_query_model_changed (RBShufflePlayOrder *sorder)
{
	GPtrArray *history;
	RhythmDBEntry *entry;
	RhythmDBEntry *playing_entry;
	RhythmDBQueryModel *model;
	GtkTreeIter iter;
	gboolean found_playing_entry;
	int i;

	if (!sorder->priv->query_model_changed)
		return;

	g_hash_table_foreach_remove (sorder->priv->entries_added, (GHRFunc) rb_true_function, NULL);
	g_hash_table_foreach_remove (sorder->priv->entries_removed, (GHRFunc) rb_true_function, NULL);

	playing_entry = rb_play_order_get_playing_entry (RB_PLAY_ORDER (sorder));

	/* This simulates removing every entry in the old query model
	 * and then adding every entry in the new one. */
	history = rb_history_dump (sorder->priv->history);
	found_playing_entry = FALSE;
	for (i=0; i < history->len; ++i) {
		entry = g_ptr_array_index (history, i);
		g_hash_table_insert (sorder->priv->entries_removed, rhythmdb_entry_ref (entry), entry);
		if (entry == playing_entry)
			found_playing_entry = TRUE;
	}
	g_ptr_array_free (history, TRUE);

	model = rb_play_order_get_query_model (RB_PLAY_ORDER (sorder));
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter)) {
		do {
			entry = rhythmdb_query_model_iter_to_entry (model, &iter);
			/* don't move the playing entry */
			if (found_playing_entry && (entry == playing_entry)) {
				g_hash_table_remove (sorder->priv->entries_removed, entry);
				rhythmdb_entry_unref (entry);
			} else {
				/* hash table takes the reference we got from the query model */
				g_hash_table_insert (sorder->priv->entries_added, entry, entry);
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));
	}

	if (playing_entry)
		rhythmdb_entry_unref (playing_entry);

	sorder->priv->query_model_changed = FALSE;
}

static gboolean
remove_from_history (RhythmDBEntry *entry, gpointer *unused, RBShufflePlayOrder *sorder)
{
	if (rb_history_contains_entry (sorder->priv->history, entry)) {
		rb_history_remove_entry (sorder->priv->history, entry);
	}
	return TRUE;
}

static gboolean
add_randomly_to_history (RhythmDBEntry *entry, gpointer *unused, RBShufflePlayOrder *sorder)
{
	gint history_size;
	gint current_index;

	if (rb_history_contains_entry (sorder->priv->history, entry))
		return TRUE;

	history_size = rb_history_length (sorder->priv->history);
	current_index = rb_history_get_current_index (sorder->priv->history);
	/* Insert entry into the history at a random position between
	 * just after current and the very end. */
	rb_history_insert_at_index (sorder->priv->history,
				    rhythmdb_entry_ref (entry),
				    g_random_int_range (MIN (current_index, history_size-1) + 1,
							history_size + 1));
	return TRUE;
}

static void
rb_shuffle_sync_history_with_query_model (RBShufflePlayOrder *sorder)
{
	RhythmDBEntry *current = rb_history_current (sorder->priv->history);

	handle_query_model_changed (sorder);
	g_hash_table_foreach_remove (sorder->priv->entries_removed, (GHRFunc) remove_from_history, sorder);
	g_hash_table_foreach_remove (sorder->priv->entries_added, (GHRFunc) add_randomly_to_history, sorder);

	if (sorder->priv->external_playing_entry != NULL) {
		if (rb_history_contains_entry (sorder->priv->history,
					       sorder->priv->external_playing_entry)) {
			/* history now contains the previously external entry, so
			 * use it as the playing entry.
			 */
			rb_history_set_playing (sorder->priv->history,
						sorder->priv->external_playing_entry);
			rhythmdb_entry_unref (sorder->priv->external_playing_entry);
			sorder->priv->external_playing_entry = NULL;
			current = NULL;
		}
	}

	if (current != NULL) {
		/* if the current entry no longer exists in the history, go back to the start */
		if (!rb_history_contains_entry (sorder->priv->history, current)) {
			rb_history_set_playing (sorder->priv->history, NULL);
		}
	}

	/* postconditions */
	g_assert (query_model_and_history_contents_match (sorder));
	g_assert (g_hash_table_size (sorder->priv->entries_added) == 0);
	g_assert (g_hash_table_size (sorder->priv->entries_removed) == 0);
}

/* NOTE: returned GPtrArray does not hold references to the entries */
static GPtrArray *
get_query_model_contents (RhythmDBQueryModel *model)
{
	guint num_entries;
	guint i = 0;
	GtkTreeIter iter;

	GPtrArray *result = g_ptr_array_new ();
	if (model == NULL)
		return result;

	num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	if (num_entries == 0)
		return result;

	g_ptr_array_set_size (result, num_entries);

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
		return result;
	do {
		RhythmDBEntry *entry;
		entry = rhythmdb_query_model_iter_to_entry (model, &iter);
		g_ptr_array_index (result, i++) = entry;
		rhythmdb_entry_unref (entry);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));

	return result;
}

static void
rb_shuffle_db_changed (RBPlayOrder *porder, RhythmDB *db)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));

	rb_history_clear (RB_SHUFFLE_PLAY_ORDER (porder)->priv->history);
}

static void
rb_shuffle_playing_entry_changed (RBPlayOrder *porder,
				  RhythmDBEntry *old_entry,
				  RhythmDBEntry *new_entry)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	if (sorder->priv->external_playing_entry != NULL) {
		rhythmdb_entry_unref (sorder->priv->external_playing_entry);
		sorder->priv->external_playing_entry = NULL;
	}

	if (new_entry) {
		if (new_entry == rb_history_current (sorder->priv->history)) {
			/* Do nothing */
		} else if (rb_history_contains_entry (sorder->priv->history, new_entry)) {
			rhythmdb_entry_ref (new_entry);
			rb_history_set_playing (sorder->priv->history, new_entry);
		} else {
			/* playing an entry outside the query model;
			 * track the entry separately as if it was between
			 * the current entry in the history and the next.
			 */
			rhythmdb_entry_ref (new_entry);
			sorder->priv->external_playing_entry = new_entry;
		}
	} else {
		/* go back to the start if we just finished the play order */
		if (old_entry == rb_history_last (sorder->priv->history))
			rb_history_go_first (sorder->priv->history);
	}
}

static void
rb_shuffle_entry_added (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	g_hash_table_remove (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_removed, entry);
	g_hash_table_insert (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_added, rhythmdb_entry_ref (entry), entry);
}

static void
rb_shuffle_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	g_hash_table_remove (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_added, entry);
	g_hash_table_insert (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_removed, rhythmdb_entry_ref (entry), entry);
}

static void
rb_shuffle_query_model_changed (RBPlayOrder *porder)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	RB_SHUFFLE_PLAY_ORDER (porder)->priv->query_model_changed = TRUE;
}

static void
rb_shuffle_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_history_remove_entry (sorder->priv->history, entry);
}

/* For some reason g_ptr_array_sort() passes pointers to the array elements
 * rather than the elements themselves */
static gint
ptr_compare (gconstpointer a, gconstpointer b)
{
	if (*(gconstpointer*)a < *(gconstpointer*)b)
		return -1;
	if (*(gconstpointer*)b < *(gconstpointer*)a)
		return 1;
	return 0;
}

static gboolean
query_model_and_history_contents_match (RBShufflePlayOrder *sorder)
{
	gboolean result = TRUE;
	GPtrArray *history_contents;
	GPtrArray *query_model_contents;

	history_contents = rb_history_dump (sorder->priv->history);
	query_model_contents = get_query_model_contents (rb_play_order_get_query_model (RB_PLAY_ORDER (sorder)));

	if (history_contents->len != query_model_contents->len)
		result = FALSE;
	else {
		int i;
		g_ptr_array_sort (history_contents, ptr_compare);
		g_ptr_array_sort (query_model_contents, ptr_compare);
		for (i=0; i<history_contents->len; ++i) {
			if (g_ptr_array_index (history_contents, i) != g_ptr_array_index (query_model_contents, i)) {
				result = FALSE;
				break;
			}
		}
	}
	g_ptr_array_free (history_contents, TRUE);
	g_ptr_array_free (query_model_contents, TRUE);
	return result;
}
