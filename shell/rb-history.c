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

#include "rb-history.h"

#include "rhythmdb.h"

/**
 * SECTION:rbhistory
 * @short_description: sequence data structure useful for implementing play orders
 *
 * RBHistory is a GSequence that maintains a "current" pointer and can delete
 * an arbitrary element in amortized O(log(N)) time. It can call a deletion
 * callback when it removes one of its entries.
 *
 * All operations take amortized O(log(N)) (worst-case O(N)) time unless noted
 * otherwise.
 */

struct RBHistoryPrivate
{
	GSequence *seq;
	/* If seq is empty, current == g_sequence_get_end_ptr (seq) */
	GSequenceIter *current;

	GHashTable *entry_to_seqptr;

	gboolean truncate_on_play;
	guint maximum_size;

	GFunc destroyer;
	gpointer destroy_userdata;
};

#define RB_HISTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_HISTORY, RBHistoryPrivate))

#define MAX_HISTORY_SIZE 50

static void rb_history_class_init (RBHistoryClass *klass);
static void rb_history_init (RBHistory *shell_player);
static void rb_history_finalize (GObject *object);

static void rb_history_set_property (GObject *object,
				     guint prop_id,
				     const GValue *value,
				     GParamSpec *pspec);
static void rb_history_get_property (GObject *object,
				     guint prop_id,
				     GValue *value,
				     GParamSpec *pspec);

static void rb_history_limit_size (RBHistory *hist, gboolean cut_from_beginning);

static void rb_history_remove_entry_internal (RBHistory *hist,
					      RhythmDBEntry *entry,
					      gboolean from_seq);

enum
{
	PROP_0,
	PROP_TRUNCATE_ON_PLAY,
	PROP_MAX_SIZE,
};

G_DEFINE_TYPE (RBHistory, rb_history, G_TYPE_OBJECT)

static void
rb_history_class_init (RBHistoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_history_finalize;

	object_class->set_property = rb_history_set_property;
	object_class->get_property = rb_history_get_property;

	/**
	 * RBHistory:truncate-on-play:
	 *
	 * If set, rb_history_set_playing() truncates the rest of the history
	 */
	g_object_class_install_property (object_class,
					 PROP_TRUNCATE_ON_PLAY,
					 g_param_spec_boolean ("truncate-on-play",
							       "Truncate on Play",
							       "Whether rb_history_set_playing() truncates the rest of the list",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * RBHistory:maximum-size:
	 *
	 * Maximum number of entries to store in the history.  If 0, no limit is applied.
	 */
	g_object_class_install_property (object_class,
					 PROP_MAX_SIZE,
					 g_param_spec_uint ("maximum-size",
							    "Maximum Size",
							    "Maximum length of the history. Infinite if 0",
							    0, G_MAXUINT,
							    0,
							    G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBHistoryPrivate));
}

/**
 * rb_history_new:
 * @truncate_on_play: Whether rb_history_set_playing() should truncate the history
 * @destroyer: (scope async): function to call when removing an entry from the history
 * @destroy_userdata: data to pass to @destroyer
 *
 * Creates a new history instance.
 *
 * Return value: a new #RBHistory
 */
RBHistory *
rb_history_new (gboolean truncate_on_play, GFunc destroyer, gpointer destroy_userdata)
{
	RBHistory *history;

	history = g_object_new (RB_TYPE_HISTORY,
				"truncate-on-play", truncate_on_play,
				NULL);

	g_return_val_if_fail (history->priv != NULL, NULL);

	history->priv->destroyer = destroyer;
	history->priv->destroy_userdata = destroy_userdata;

	return history;
}

static void
rb_history_init (RBHistory *hist)
{
	hist->priv = RB_HISTORY_GET_PRIVATE (hist);

	hist->priv->entry_to_seqptr = g_hash_table_new (g_direct_hash,
							g_direct_equal);
	hist->priv->seq = g_sequence_new (NULL);
	hist->priv->current = g_sequence_get_begin_iter (hist->priv->seq);
}

static void
rb_history_finalize (GObject *object)
{
	RBHistory *hist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HISTORY (object));

	hist = RB_HISTORY (object);

	/* remove all of the stored entries */
	rb_history_clear (hist);

	g_hash_table_destroy (hist->priv->entry_to_seqptr);
	g_sequence_free (hist->priv->seq);

	G_OBJECT_CLASS (rb_history_parent_class)->finalize (object);
}

static void
rb_history_set_property (GObject *object,
			 guint prop_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	RBHistory *hist = RB_HISTORY (object);

	switch (prop_id)
	{
	case PROP_TRUNCATE_ON_PLAY: {
		hist->priv->truncate_on_play = g_value_get_boolean (value);
		} break;
	case PROP_MAX_SIZE: {
		hist->priv->maximum_size = g_value_get_uint (value);
		rb_history_limit_size (hist, TRUE);
		} break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_history_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	RBHistory *hist = RB_HISTORY (object);

	switch (prop_id)
	{
	case PROP_TRUNCATE_ON_PLAY: {
		g_value_set_boolean (value, hist->priv->truncate_on_play);
		} break;
	case PROP_MAX_SIZE: {
		g_value_set_uint (value, hist->priv->maximum_size);
		} break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}

}

/**
 * rb_history_set_destroy_notify:
 * @hist: a #RBHistory
 * @destroyer: (scope async): function to call when removing an entry from the history
 * @destroy_userdata: data to pass to @destroyer
 *
 * Sets a new function to call when removing entries from the history.
 */
void
rb_history_set_destroy_notify (RBHistory *hist, GFunc destroyer, gpointer destroy_userdata)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	hist->priv->destroyer = destroyer;
	hist->priv->destroy_userdata = destroy_userdata;
}

/**
 * rb_history_set_truncate_on_play:
 * @hist: a #RBHistory
 * @truncate_on_play: Whether rb_history_set_playing() should truncate the history
 *
 * Sets the 'truncate-on-play' property.
 */
void
rb_history_set_truncate_on_play (RBHistory *hist, gboolean truncate_on_play)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	hist->priv->truncate_on_play = truncate_on_play;
	g_object_notify (G_OBJECT (hist), "truncate-on-play");
}

/**
 * rb_history_set_maximum_size:
 * @hist: a #RBHistory
 * @maximum_size: new maximum size of the history (or 0 for no limit)
 *
 * Sets the maximum-size property
 */
void
rb_history_set_maximum_size (RBHistory *hist, guint maximum_size)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	hist->priv->maximum_size = maximum_size;
	g_object_notify (G_OBJECT (hist), "maximum-size");
}

/**
 * rb_history_length:
 * @hist: a #RBHistory
 *
 * Returns the number of entries in the history.
 *
 * Return value: number of entries
 */
guint
rb_history_length (RBHistory *hist)
{
	g_return_val_if_fail (RB_IS_HISTORY (hist), 0);

	return g_sequence_get_length (hist->priv->seq);
}

/**
 * rb_history_first:
 * @hist: a #RBHistory
 *
 * Returns the first entry in the history.
 *
 * Return value: (transfer none): first entry
 */
RhythmDBEntry *
rb_history_first (RBHistory *hist)
{
	GSequenceIter *begin;
	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	begin = g_sequence_get_begin_iter (hist->priv->seq);
	return g_sequence_iter_is_end (begin) ? NULL : g_sequence_get (begin);
}

/**
 * rb_history_previous:
 * @hist: a #RBHistory
 *
 * Returns the #RhythmDBEntry before the current position.
 *
 * Return value: (transfer none): previous entry
 */
RhythmDBEntry *
rb_history_previous (RBHistory *hist)
{
	GSequenceIter *prev;

	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	prev = g_sequence_iter_prev (hist->priv->current);
	return prev == hist->priv->current ? NULL : g_sequence_get (prev);
}

/**
 * rb_history_current:
 * @hist: a #RBHistory
 *
 * Returns the current #RhythmDBEntry, or NULL if there is no current position
 *
 * Return value: (transfer none): current entry or NULL
 */
RhythmDBEntry *
rb_history_current (RBHistory *hist)
{
	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	return g_sequence_iter_is_end (hist->priv->current) ? NULL : g_sequence_get (hist->priv->current);
}

/**
 * rb_history_next:
 * @hist: a #RBHistory
 *
 * Returns the #RhythmDBEntry after the current position
 *
 * Return value: (transfer none): next entry
 */
RhythmDBEntry *
rb_history_next (RBHistory *hist)
{
	GSequenceIter *next;
	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	next = g_sequence_iter_next (hist->priv->current);
	return g_sequence_iter_is_end (next) ? NULL : g_sequence_get (next);
}

/**
 * rb_history_last:
 * @hist: a #RBHistory
 *
 * Returns the last #RhythmDBEntry in the history
 *
 * Return value: (transfer none): last entry
 */
RhythmDBEntry *
rb_history_last (RBHistory *hist)
{
	GSequenceIter *last;

	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	last = g_sequence_iter_prev (g_sequence_get_end_iter (hist->priv->seq));
	return g_sequence_iter_is_end (last) ? NULL : g_sequence_get (last);
}

/**
 * rb_history_go_first:
 * @hist: a #RBHistory
 *
 * Moves the current position to the first entry in the history
 */
void
rb_history_go_first (RBHistory *hist)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	hist->priv->current = g_sequence_get_begin_iter (hist->priv->seq);
}

/**
 * rb_history_go_previous:
 * @hist: a #RBHistory
 *
 * Moves the current position to the previous entry.  If the current position is
 * already at the start of the history, nothing happens.
 */
void
rb_history_go_previous (RBHistory *hist)
{
	GSequenceIter *prev;
	g_return_if_fail (RB_IS_HISTORY (hist));

	prev = g_sequence_iter_prev (hist->priv->current);
	if (prev)
		hist->priv->current = prev;
}

/**
 * rb_history_go_next:
 * @hist: a #RBHistory
 *
 * Moves the current position to the next entry.  If the current position is
 * already at the end of the history, nothing happens.
 */
void
rb_history_go_next (RBHistory *hist)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	hist->priv->current = g_sequence_iter_next (hist->priv->current);
}

/**
 * rb_history_go_last:
 * @hist: a #RBHistory
 *
 * Moves the current position to the last entry in the history
 */
void
rb_history_go_last (RBHistory *hist)
{
	GSequenceIter *last;
	g_return_if_fail (RB_IS_HISTORY (hist));

	last = g_sequence_iter_prev (g_sequence_get_end_iter (hist->priv->seq));
	hist->priv->current = last ? last : g_sequence_get_end_iter (hist->priv->seq);
}

static void
_history_remove_swapped (RhythmDBEntry *entry, RBHistory *hist)
{
	rb_history_remove_entry_internal (hist, entry, FALSE);
}

/**
 * rb_history_set_playing:
 * @hist: a #RBHistory
 * @entry: the new playing #RhythmDBEntry
 *
 * Updates the current position to point to the specified entry.
 * If the truncate-on-play property is set, this will remove all entries
 * after that.
 */
void
rb_history_set_playing (RBHistory *hist, RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	if (entry == NULL) {
		hist->priv->current = g_sequence_get_end_iter (hist->priv->seq);
		return;
	}

	rb_history_remove_entry (hist, entry);

	g_sequence_insert_before (g_sequence_iter_next (hist->priv->current), entry);
	/* make hist->priv->current point to the new entry */
	if (g_sequence_iter_is_end (hist->priv->current))
		hist->priv->current = g_sequence_iter_prev (hist->priv->current);
	else
		hist->priv->current = g_sequence_iter_next (hist->priv->current);
	g_hash_table_insert (hist->priv->entry_to_seqptr, entry, hist->priv->current);

	if (hist->priv->truncate_on_play) {
		g_sequence_foreach_range (g_sequence_iter_next (hist->priv->current),
					    g_sequence_get_end_iter (hist->priv->seq),
					    (GFunc)_history_remove_swapped, hist);
		g_sequence_remove_range (g_sequence_iter_next (hist->priv->current),
					   g_sequence_get_end_iter (hist->priv->seq));
	}

	rb_history_limit_size (hist, TRUE);
}

/**
 * rb_history_append:
 * @hist: a #RBHistory
 * @entry: a #RhythmDBEntry to append
 *
 * Adds a new entry to the end of the history list.
 * If a size limit is set, an entry may be removed from the start to
 * keep the history list within the limit.
 */
void
rb_history_append (RBHistory *hist, RhythmDBEntry *entry)
{
	GSequenceIter *new_node;
	GSequenceIter *last;

	g_return_if_fail (RB_IS_HISTORY (hist));
	g_return_if_fail (entry != NULL);

	if (g_sequence_iter_is_end (hist->priv->current) == FALSE &&
	    entry == g_sequence_get (hist->priv->current)) {
		rb_history_remove_entry (hist, entry);
		last = g_sequence_iter_prev (g_sequence_get_end_iter (hist->priv->seq));
		hist->priv->current = last ? last : g_sequence_get_end_iter (hist->priv->seq);
	} else {
		rb_history_remove_entry (hist, entry);
	}

	g_sequence_append (hist->priv->seq, entry);
	new_node = g_sequence_iter_prev (g_sequence_get_end_iter (hist->priv->seq));
	g_hash_table_insert (hist->priv->entry_to_seqptr, entry, new_node);

	rb_history_limit_size (hist, TRUE);
}

/**
 * rb_history_get_current_index:
 * @hist: a #RBHistory
 *
 * Gets the index of the current entry. This is guaranteed to be < the
 * history's size, so if the history is empty, it returns -1.
 *
 * Return value: index of the current entry
 */
gint
rb_history_get_current_index (RBHistory *hist)
{
	g_return_val_if_fail (RB_IS_HISTORY (hist), -1);

	return g_sequence_iter_get_position (hist->priv->current);
}

/**
 * rb_history_insert_at_index:
 * @hist: a #RBHistory
 * @entry: a #RhythmDBEntry to insert
 * @index: position at which to insert @entry
 *
 * Inserts @entry at @index within the history list. 0<=@index<=size
 */
void
rb_history_insert_at_index (RBHistory *hist, RhythmDBEntry *entry, guint index)
{
	GSequenceIter *old_node;
	GSequenceIter *new_node;

	g_return_if_fail (RB_IS_HISTORY (hist));
	g_return_if_fail (entry != NULL);
	g_return_if_fail (index <= g_sequence_get_length (hist->priv->seq));

	/* Deal with case where the entry is moving forward */
	old_node = g_hash_table_lookup (hist->priv->entry_to_seqptr, entry);
	if (old_node && g_sequence_iter_get_position (old_node) < index)
		index--;

	rb_history_remove_entry (hist, entry);

	new_node = g_sequence_get_iter_at_pos (hist->priv->seq, index);
	g_sequence_insert_before (new_node, entry);
	new_node = g_sequence_iter_prev (new_node);
	g_hash_table_insert (hist->priv->entry_to_seqptr, entry, new_node);

	if (g_sequence_iter_is_end (hist->priv->current) && index == g_sequence_get_length (hist->priv->seq)-1 /*length just increased*/)
		hist->priv->current = new_node;

	rb_history_limit_size (hist, TRUE);
}

/*
 * Cuts nodes off of the history from the desired end until it is smaller than max_size.
 * Never cuts off the current node.
 */
static void
rb_history_limit_size (RBHistory *hist, gboolean cut_from_beginning)
{
	if (hist->priv->maximum_size != 0) {
		while (g_sequence_get_length (hist->priv->seq) > hist->priv->maximum_size) {
			if (cut_from_beginning
					|| hist->priv->current == g_sequence_iter_prev (g_sequence_get_end_iter (hist->priv->seq))) {
				rb_history_remove_entry (hist, rb_history_first (hist));
			} else {
				rb_history_remove_entry (hist, rb_history_last (hist));
			}
		}
	}
}

/**
 * rb_history_remove_entry:
 * @hist: a #RBHistory
 * @entry: the #RhythmDBEntry to remove
 *
 * Removes the specified entry from the history list.
 */
void
rb_history_remove_entry (RBHistory *hist, RhythmDBEntry *entry)
{
	rb_history_remove_entry_internal (hist, entry, TRUE);
}

static void
rb_history_remove_entry_internal (RBHistory *hist, RhythmDBEntry *entry, gboolean from_seq)
{
	GSequenceIter *to_delete;
	g_return_if_fail (RB_IS_HISTORY (hist));

	to_delete = g_hash_table_lookup (hist->priv->entry_to_seqptr, entry);
	if (to_delete) {
		g_hash_table_remove (hist->priv->entry_to_seqptr, entry);
		if (hist->priv->destroyer)
			hist->priv->destroyer (entry, hist->priv->destroy_userdata);

		if (to_delete == hist->priv->current) {
			hist->priv->current = g_sequence_get_end_iter (hist->priv->seq);
		}
		g_assert (to_delete != hist->priv->current);
		if (from_seq) {
			g_sequence_remove (to_delete);
		}
	}
}

/**
 * rb_history_clear:
 * @hist: a #RBHistory
 *
 * Empties the history list.
 */
void
rb_history_clear (RBHistory *hist)
{
	g_return_if_fail (RB_IS_HISTORY (hist));

	g_sequence_foreach (hist->priv->seq, (GFunc)_history_remove_swapped, hist);
	g_sequence_remove_range (g_sequence_get_begin_iter (hist->priv->seq),
				   g_sequence_get_end_iter (hist->priv->seq));

	/* When the sequence is empty, the hash table should also be empty. */
	g_assert (g_hash_table_size (hist->priv->entry_to_seqptr) == 0);
}

/**
 * rb_history_dump:
 * @hist: a #RBHistory
 *
 * Constructs a copy of the whole history in order. Caller must free the result.
 * The caller does not own any references on the entries in the returned array.
 * Takes O(Nlog(N)) time.
 *
 * Return value: (element-type RB.RhythmDBEntry) (transfer container): a copy of the history list
 */
GPtrArray *
rb_history_dump (RBHistory *hist)
{
	GSequenceIter *cur;
	GPtrArray *result;

	g_return_val_if_fail (RB_IS_HISTORY (hist), NULL);

	result = g_ptr_array_sized_new (g_sequence_get_length (hist->priv->seq));
	for (cur = g_sequence_get_begin_iter (hist->priv->seq);
	     !g_sequence_iter_is_end (cur);
	     cur = g_sequence_iter_next (cur)) {
		g_ptr_array_add (result, g_sequence_get (cur));
	}
	return result;
}

/**
 * rb_history_contains_entry:
 * @hist: a #RBHistory
 * @entry: a #RhythmDBEntry to check for
 *
 * Returns %TRUE if the entry is present in the history list.
 *
 * Return value: %TRUE if found
 */
gboolean
rb_history_contains_entry (RBHistory *hist, RhythmDBEntry *entry)
{
	g_return_val_if_fail (RB_IS_HISTORY (hist), FALSE);

	return g_hash_table_lookup (hist->priv->entry_to_seqptr, entry) != NULL;
}
