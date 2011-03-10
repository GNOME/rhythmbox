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

#include "rb-play-order-linear-loop.h"

#include "rb-debug.h"
#include "rb-util.h"

static void rb_linear_play_order_loop_class_init (RBLinearPlayOrderLoopClass *klass);

static RhythmDBEntry* rb_linear_play_order_loop_get_next (RBPlayOrder* method);
static RhythmDBEntry* rb_linear_play_order_loop_get_previous (RBPlayOrder* method);

G_DEFINE_TYPE (RBLinearPlayOrderLoop, rb_linear_play_order_loop, RB_TYPE_PLAY_ORDER)

RBPlayOrder *
rb_linear_play_order_loop_new (RBShellPlayer *player)
{
	RBLinearPlayOrderLoop *lorder;

	lorder = g_object_new (RB_TYPE_LINEAR_PLAY_ORDER_LOOP,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (lorder);
}

static void
rb_linear_play_order_loop_class_init (RBLinearPlayOrderLoopClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->has_next = rb_play_order_model_not_empty;
	porder->has_previous = rb_play_order_model_not_empty;
	porder->get_next = rb_linear_play_order_loop_get_next;
	porder->get_previous = rb_linear_play_order_loop_get_previous;
}

static void
rb_linear_play_order_loop_init (RBLinearPlayOrderLoop *porder)
{
}

static RhythmDBEntry *
rb_linear_play_order_loop_get_next (RBPlayOrder *porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER_LOOP (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);

	if (entry != NULL) {
		RhythmDBEntry *next;

		next = rhythmdb_query_model_get_next_from_entry (model, entry);
		rhythmdb_entry_unref (entry);
		entry = next;
	}

	if (entry == NULL) {
		/* loop back to (or start from) the first entry */
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
			return NULL;
		return rhythmdb_query_model_iter_to_entry (model, &iter);
	}

	return entry;
}

static RhythmDBEntry *
rb_linear_play_order_loop_get_previous (RBPlayOrder *porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;
	RhythmDBEntry *prev = NULL;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER_LOOP (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);
	if (entry != NULL) {
		prev = rhythmdb_query_model_get_previous_from_entry (model, entry);
		rhythmdb_entry_unref (entry);
	}

	if (prev == NULL) {
		/* loop to last entry */
		GtkTreeIter iter;
		gint num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
		if (!gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (model), &iter, NULL, num_entries-1))
			return NULL;
		prev = rhythmdb_query_model_iter_to_entry (model, &iter);
	}

	return prev;
}
