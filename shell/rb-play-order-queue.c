/* 
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

#include "rb-play-order-queue.h"

#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_queue_play_order_class_init (RBQueuePlayOrderClass *klass);

static RhythmDBEntry* rb_queue_play_order_get_next (RBPlayOrder* method);
static RhythmDBEntry* rb_queue_play_order_get_previous (RBPlayOrder* method);
static void rb_queue_play_order_go_next (RBPlayOrder* method);

static void rb_queue_play_order_playing_entry_changed (RBPlayOrder *porder,
						       RhythmDBEntry *old_entry,
						       RhythmDBEntry *new_entry);
static void rb_queue_play_order_playing_entry_removed (RBPlayOrder *porder,
						       RhythmDBEntry *entry);

G_DEFINE_TYPE (RBQueuePlayOrder, rb_queue_play_order, RB_TYPE_PLAY_ORDER)
#define RB_QUEUE_PLAY_ORDER_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_QUEUE_PLAY_ORDER, RBQueuePlayOrderPrivate))

typedef struct _RBQueuePlayOrderPrivate RBQueuePlayOrderPrivate;

struct _RBQueuePlayOrderPrivate
{
	gboolean playing_entry_removed;
};

RBPlayOrder *
rb_queue_play_order_new (RBShellPlayer *player)
{
	RBQueuePlayOrder *lorder;

	lorder = g_object_new (RB_TYPE_QUEUE_PLAY_ORDER,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (lorder);
}

static void
rb_queue_play_order_class_init (RBQueuePlayOrderClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_queue_play_order_get_next;
	porder->go_next = rb_queue_play_order_go_next;
	porder->get_previous = rb_queue_play_order_get_previous;
	porder->playing_entry_changed = rb_queue_play_order_playing_entry_changed;
	porder->playing_entry_removed = rb_queue_play_order_playing_entry_removed;

	g_type_class_add_private (klass, sizeof (RBQueuePlayOrderPrivate));
}

static void
rb_queue_play_order_init (RBQueuePlayOrder *porder)
{
}

static RhythmDBEntry* 
rb_queue_play_order_get_next (RBPlayOrder* porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_QUEUE_PLAY_ORDER (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);
	if (entry == NULL) {
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
			return NULL;
		return rhythmdb_query_model_iter_to_entry (model, &iter);
	} else {
		return rhythmdb_query_model_get_next_from_entry (model, entry);
	}
}

static void
rb_queue_play_order_go_next (RBPlayOrder *porder)
{
	RhythmDBEntry *next;
	RhythmDBQueryModel *model;

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return;

	next = rb_queue_play_order_get_next (porder);
	rb_play_order_set_playing_entry (porder, next);
}

static RhythmDBEntry*
rb_queue_play_order_get_previous (RBPlayOrder* porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_QUEUE_PLAY_ORDER (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

	g_object_get (porder, "playing-entry", &entry, NULL);
	if (entry == NULL)
		return NULL;
	return rhythmdb_query_model_get_previous_from_entry (model, entry);
}

static void
rb_queue_play_order_playing_entry_changed (RBPlayOrder *porder,
					   RhythmDBEntry *old_entry,
					   RhythmDBEntry *new_entry)
{
	RhythmDBQueryModel *model = rb_play_order_get_query_model (porder);
	RBQueuePlayOrderPrivate *priv = RB_QUEUE_PLAY_ORDER_GET_PRIVATE (porder);
	if (model == NULL)
		return;

	if (old_entry && old_entry != new_entry && !priv->playing_entry_removed)
		rhythmdb_query_model_remove_entry (model, old_entry);
}

static void
rb_queue_play_order_playing_entry_removed (RBPlayOrder *porder,
					   RhythmDBEntry *entry)
{
	RBQueuePlayOrderPrivate *priv = RB_QUEUE_PLAY_ORDER_GET_PRIVATE (porder);
	GError *error = NULL;

	priv->playing_entry_removed = TRUE;
	if (!rb_shell_player_do_next (rb_play_order_get_player (porder), &error)) {
		if (error->domain != RB_SHELL_PLAYER_ERROR ||
		    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
			g_warning ("rb_queue_play_order_playing_entry_removed: Unhandled error: %s", error->message);
	}
	priv->playing_entry_removed = FALSE;
}
