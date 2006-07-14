/*
 *  arch-tag: Implementation of linear navigation method
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

#include "rb-play-order-linear.h"

#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_linear_play_order_class_init (RBLinearPlayOrderClass *klass);

static RhythmDBEntry* rb_linear_play_order_get_next (RBPlayOrder* method);
static RhythmDBEntry* rb_linear_play_order_get_previous (RBPlayOrder* method);

G_DEFINE_TYPE (RBLinearPlayOrder, rb_linear_play_order, RB_TYPE_PLAY_ORDER)

RBPlayOrder *
rb_linear_play_order_new (RBShellPlayer *player)
{
	RBLinearPlayOrder *lorder;

	lorder = g_object_new (RB_TYPE_LINEAR_PLAY_ORDER,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (lorder);
}

static void
rb_linear_play_order_class_init (RBLinearPlayOrderClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_linear_play_order_get_next;
	porder->get_previous = rb_linear_play_order_get_previous;
}

static void
rb_linear_play_order_init (RBLinearPlayOrder *porder)
{
}

static RhythmDBEntry*
rb_linear_play_order_get_next (RBPlayOrder* porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);
	if (entry) {
		RhythmDBEntry *next;
		next = rhythmdb_query_model_get_next_from_entry (model, entry);
		rhythmdb_entry_unref (entry);
		return next;
	} else {
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
			return NULL;
		return rhythmdb_query_model_iter_to_entry (model, &iter);
	}
}

static RhythmDBEntry*
rb_linear_play_order_get_previous (RBPlayOrder* porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry, *prev;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LINEAR_PLAY_ORDER (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);
	if (entry == NULL)
		return NULL;

	prev = rhythmdb_query_model_get_previous_from_entry (model, entry);
	rhythmdb_entry_unref (entry);
	return prev;
}
