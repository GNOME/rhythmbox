/* 
 *  arch-tag: Implementation of shuffle play order
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

#include "rb-play-order-shuffle.h"

#include "rb-history.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include <string.h>

static void rb_shuffle_play_order_class_init (RBShufflePlayOrderClass *klass);
static void rb_shuffle_play_order_init (RBShufflePlayOrder *sorder);
static void rb_shuffle_play_order_finalize (GObject *object);

static RhythmDBEntry* rb_shuffle_play_order_get_next (RBPlayOrder* method);
static void rb_shuffle_play_order_go_next (RBPlayOrder* method);
static RhythmDBEntry* rb_shuffle_play_order_get_previous (RBPlayOrder* method);
static void rb_shuffle_play_order_go_previous (RBPlayOrder* method);

static void rb_shuffle_sync_history_with_entry_view (RBShufflePlayOrder *sorder);
static GPtrArray *get_entry_view_contents (RBEntryView *entry_view);

static void rb_shuffle_db_changed (RBPlayOrder *porder, RhythmDB *db);
static void rb_shuffle_playing_entry_changed (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_shuffle_entry_added (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_shuffle_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry);
static void rb_shuffle_entries_replaced (RBPlayOrder *porder);
static void rb_shuffle_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry);
static gboolean entry_view_and_history_contents_match (RBShufflePlayOrder *sorder);

struct RBShufflePlayOrderPrivate
{
	RBHistory *history;

	/** holds a new history generated after changing the filter, but not committed until the playing song changes. */
	RBHistory *tentative_history;

	/** TRUE if all of the entries in the entry_view were replaced */
	gboolean entries_replaced;
	GHashTable *entries_removed;
	GHashTable *entries_added;
};

static RBPlayOrderClass *parent_class = NULL;

GType
rb_shuffle_play_order_get_type (void)
{
	static GType rb_shuffle_play_order_type = 0;

	if (rb_shuffle_play_order_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBShufflePlayOrderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_shuffle_play_order_class_init,
			NULL,
			NULL,
			sizeof (RBShufflePlayOrder),
			0,
			(GInstanceInitFunc) rb_shuffle_play_order_init
		};

		rb_shuffle_play_order_type = g_type_register_static (RB_TYPE_PLAY_ORDER,
				"RBShufflePlayOrder",
				&our_info, 0);
	}

	return rb_shuffle_play_order_type;
}

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

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_shuffle_play_order_finalize;


	porder = RB_PLAY_ORDER_CLASS (klass);

	porder->db_changed = rb_shuffle_db_changed;
	porder->playing_entry_changed = rb_shuffle_playing_entry_changed;
	porder->entry_added = rb_shuffle_entry_added;
	porder->entry_removed = rb_shuffle_entry_removed;
	porder->entries_replaced = rb_shuffle_entries_replaced;
	porder->db_entry_deleted = rb_shuffle_db_entry_deleted;

	porder->get_next = rb_shuffle_play_order_get_next;
	porder->go_next = rb_shuffle_play_order_go_next;
	porder->get_previous = rb_shuffle_play_order_get_previous;
	porder->go_previous = rb_shuffle_play_order_go_previous;
}

static void
rb_shuffle_play_order_init (RBShufflePlayOrder *sorder)
{
	sorder->priv = g_new0 (RBShufflePlayOrderPrivate, 1);

	sorder->priv->history = rb_history_new (FALSE,
						(GFunc) rb_play_order_unref_entry_swapped,
					       	rb_play_order_get_db (RB_PLAY_ORDER (sorder)));

	sorder->priv->entries_replaced = FALSE;
	sorder->priv->entries_added = g_hash_table_new (g_direct_hash, g_direct_equal);
	sorder->priv->entries_removed = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
rb_shuffle_play_order_finalize (GObject *object)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (object));

	sorder = RB_SHUFFLE_PLAY_ORDER (object);

	g_object_unref (sorder->priv->history);
	if (sorder->priv->tentative_history)
		g_object_unref (sorder->priv->tentative_history);
	g_hash_table_destroy (sorder->priv->entries_added);
	g_hash_table_destroy (sorder->priv->entries_removed);
	g_free (sorder->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

inline static RBHistory *
get_history (RBShufflePlayOrder *sorder)
{
	if (sorder->priv->tentative_history)
		return sorder->priv->tentative_history;
	else
		return sorder->priv->history;
}

static RhythmDBEntry* 
rb_shuffle_play_order_get_next (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder), NULL);

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_shuffle_sync_history_with_entry_view (sorder);

	if (rb_play_order_get_playing_entry (porder) == rb_history_current (get_history (sorder))) {
		if (rb_history_current (get_history (sorder)) == rb_history_last (get_history (sorder))) {
			entry = NULL;
		} else {
			rb_debug ("choosing next entry in shuffle");
			entry = rb_history_next (get_history (sorder));
		}
	} else {
		/* If the player is currently stopped, the "next" (first) song
		 * is the first in the shuffle. */
		rb_debug ("choosing current entry in shuffle");
		entry = rb_history_current (get_history (sorder));
	}

	return entry;
}

static void
rb_shuffle_play_order_go_next (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	if (rb_play_order_get_playing_entry (porder) == rb_history_current (get_history (sorder))) {
		if (rb_history_current (get_history (sorder)) != rb_history_last (get_history (sorder))) {
			rb_history_go_next (get_history (sorder));
		}
	} else {
		/* If the player is currently stopped, the current song in the
		 * history needs to stay current */
	}
}

static RhythmDBEntry*
rb_shuffle_play_order_get_previous (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder), NULL);
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_val_if_fail (rb_play_order_player_is_playing (porder), NULL);

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_shuffle_sync_history_with_entry_view (sorder);

	rb_debug ("choosing previous history entry");
	return rb_history_previous (get_history (sorder));
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

	if (rb_history_current (get_history (sorder)) != rb_history_first (get_history (sorder)))
		rb_history_go_previous (get_history (sorder));
}

static gboolean
rb_return_true ()
{
	return TRUE;
}

static void
handle_entries_replaced (RBShufflePlayOrder *sorder)
{
	if (sorder->priv->entries_replaced) {
		GPtrArray *history;
		GPtrArray *entry_view;
		int i;

		g_hash_table_foreach_remove (sorder->priv->entries_added, rb_return_true, NULL);
		g_hash_table_foreach_remove (sorder->priv->entries_removed, rb_return_true, NULL);

		/* Only when the entries are replaced should the
		 * tentative_history be completely removed */
		if (sorder->priv->tentative_history) {
			g_object_unref (sorder->priv->tentative_history);
			sorder->priv->tentative_history = NULL;
		}

		/* This simulates removing every entry in the old entry-view,
		 * and then adding every entry in the new one. */
		history = rb_history_dump (get_history (sorder));
		for (i=0; i < history->len; ++i)
			rb_shuffle_entry_removed (RB_PLAY_ORDER (sorder), g_ptr_array_index (history, i));
		g_ptr_array_free (history, TRUE);

		entry_view = get_entry_view_contents (rb_play_order_get_entry_view (RB_PLAY_ORDER (sorder)));
		for (i=0; i < entry_view->len; ++i)
			rb_shuffle_entry_added (RB_PLAY_ORDER (sorder), g_ptr_array_index (entry_view, i));
		g_ptr_array_free (entry_view, TRUE);

		sorder->priv->entries_replaced = FALSE;
	}
}

static gboolean
remove_from_history (RhythmDBEntry *entry, gpointer *unused, RBShufflePlayOrder *sorder)
{
	if (rb_history_contains_entry (get_history (sorder), entry)) {
		if (sorder->priv->tentative_history == NULL) {
			sorder->priv->tentative_history = rb_history_clone (sorder->priv->history,
									    (GFunc) rb_play_order_ref_entry_swapped, 
									    rb_play_order_get_db (RB_PLAY_ORDER (sorder)));
		}
		rb_history_remove_entry (sorder->priv->tentative_history, entry);
	}
	return TRUE;
}

static gboolean
add_randomly_to_history (RhythmDBEntry *entry, gpointer *unused, RBShufflePlayOrder *sorder)
{
	if (!rb_history_contains_entry (get_history (sorder), entry)) {
		gint history_size;
		gint current_index;

		if (sorder->priv->tentative_history == NULL) {
			sorder->priv->tentative_history = rb_history_clone (sorder->priv->history,
									    (GFunc) rb_play_order_ref_entry_swapped, 
									    rb_play_order_get_db (RB_PLAY_ORDER (sorder)));
		}

		history_size = rb_history_length (sorder->priv->tentative_history);
		current_index = rb_history_get_current_index (sorder->priv->tentative_history);
		/* Insert entry into the history at a random position between
		 * just after current and the very end. */
		rhythmdb_entry_ref (rb_play_order_get_db (RB_PLAY_ORDER (sorder)), entry);
		rb_history_insert_at_index (sorder->priv->tentative_history,
					    entry,
					    g_random_int_range (MIN (current_index, history_size-1) + 1,
								history_size + 1));
	}
	return TRUE;
}

static void
rb_shuffle_sync_history_with_entry_view (RBShufflePlayOrder *sorder)
{
	handle_entries_replaced (sorder);
	g_hash_table_foreach_remove (sorder->priv->entries_removed, (GHRFunc) remove_from_history, sorder);
	g_hash_table_foreach_remove (sorder->priv->entries_added, (GHRFunc) add_randomly_to_history, sorder);

	/* postconditions */
	g_assert (entry_view_and_history_contents_match (sorder));
	g_assert (g_hash_table_size (sorder->priv->entries_added) == 0);
	g_assert (g_hash_table_size (sorder->priv->entries_removed) == 0);
}

static GPtrArray *
get_entry_view_contents (RBEntryView *entry_view)
{
	guint num_entries;
	guint i;

	GPtrArray *result = g_ptr_array_new ();
	if (entry_view == NULL)
		return result;

	num_entries = rb_entry_view_get_num_entries (entry_view);
	if (num_entries == 0)
		return result;

	g_ptr_array_set_size (result, num_entries);
	g_ptr_array_index (result, 0) = rb_entry_view_get_first_entry (entry_view);
	for (i=1; i<num_entries; ++i) {
		g_ptr_array_index (result, i) =
			rb_entry_view_get_next_from_entry (entry_view,
							   g_ptr_array_index (result, i-1));
	}
	return result;
}

static void
rb_shuffle_db_changed (RBPlayOrder *porder, RhythmDB *db)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));

	rb_history_clear (RB_SHUFFLE_PLAY_ORDER (porder)->priv->history);

	rb_history_set_destroy_notify (RB_SHUFFLE_PLAY_ORDER (porder)->priv->history,
				       (GFunc) rb_play_order_unref_entry_swapped,
				       db);
}

static void
rb_shuffle_commit_history (RBShufflePlayOrder *sorder)
{
	if (sorder->priv->tentative_history) {
		g_object_unref (sorder->priv->history);
		sorder->priv->history = sorder->priv->tentative_history;
		sorder->priv->tentative_history = NULL;
	}
}

static void
rb_shuffle_playing_entry_changed (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	/* The history gets committed if the user stops playback too. If this
	 * were inside the if(entry), it would only commit when a real song
	 * started playing */
	rb_shuffle_commit_history (sorder);

	if (entry) {
		if (entry == rb_history_current (get_history (sorder))) {
			/* Do nothing */
		} else {
			rhythmdb_entry_ref (rb_play_order_get_db (porder), entry);
			rb_history_set_playing (get_history (sorder), entry);
		}
	}
}

static void
rb_shuffle_entry_added (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	g_hash_table_remove (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_removed, entry);
	g_hash_table_insert (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_added, entry, entry);
}

static void
rb_shuffle_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	g_hash_table_remove (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_added, entry);
	g_hash_table_insert (RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_removed, entry, entry);
}

static void
rb_shuffle_entries_replaced (RBPlayOrder *porder)
{
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	RB_SHUFFLE_PLAY_ORDER (porder)->priv->entries_replaced = TRUE;
}

static void
rb_shuffle_db_entry_deleted (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	RBShufflePlayOrder *sorder;

	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder));
	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	rb_history_remove_entry (sorder->priv->history, entry);
	if (sorder->priv->tentative_history)
		rb_history_remove_entry (sorder->priv->tentative_history, entry);
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
entry_view_and_history_contents_match (RBShufflePlayOrder *sorder)
{
	gboolean result = TRUE;
	GPtrArray *history_contents = rb_history_dump (get_history (sorder));
	GPtrArray *entry_view_contents = get_entry_view_contents (rb_play_order_get_entry_view (RB_PLAY_ORDER (sorder)));

	if (history_contents->len != entry_view_contents->len)
		result = FALSE;
	else {
		int i;
		g_ptr_array_sort (history_contents, ptr_compare);
		g_ptr_array_sort (entry_view_contents, ptr_compare);
		for (i=0; i<history_contents->len; ++i) {
			if (g_ptr_array_index (history_contents, i) != g_ptr_array_index (entry_view_contents, i)) {
				result = FALSE;
				break;
			}
		}
	}
	g_ptr_array_free (history_contents, TRUE);
	g_ptr_array_free (entry_view_contents, TRUE);
	return result;
}
