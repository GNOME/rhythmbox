/* 
 *  arch-tag: Implementation of random navigation method with equal weights for each song
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

#include "rb-play-order-random-equal-weights.h"

#include "rb-debug.h"
#include "rhythmdb.h"
#include "rb-history.h"
#include <string.h>

static void rb_random_play_order_equal_weights_class_init (RBRandomPlayOrderEqualWeightsClass *klass);
static void rb_random_play_order_equal_weights_init (RBRandomPlayOrderEqualWeights *rorder);

static GObject *rb_random_play_order_equal_weights_constructor (GType type, guint n_construct_properties,
								GObjectConstructParam *construct_properties);
static void rb_random_play_order_equal_weights_finalize (GObject *object);

static RhythmDBEntry* rb_random_play_order_equal_weights_get_next (RBPlayOrder* porder);
static void rb_random_play_order_equal_weights_go_next (RBPlayOrder* porder);
static RhythmDBEntry* rb_random_play_order_equal_weights_get_previous (RBPlayOrder* porder);
static void rb_random_play_order_equal_weights_go_previous (RBPlayOrder* porder);

static void playing_source_changed_cb (RBRandomPlayOrderEqualWeights *rorder);
static void entry_view_playing_entry_changed_cb (GObject *entry_view,
						 GParamSpec *pspec,
						 RBRandomPlayOrderEqualWeights *rorder);


struct RBRandomPlayOrderEqualWeightsPrivate
{
	RBHistory *history;

	RBSource *source;
};

static RBPlayOrderClass *parent_class = NULL;

GType
rb_random_play_order_equal_weights_get_type (void)
{
	static GType rb_random_play_order_equal_weights_type = 0;

	if (rb_random_play_order_equal_weights_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBRandomPlayOrderEqualWeightsClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_random_play_order_equal_weights_class_init,
			NULL,
			NULL,
			sizeof (RBRandomPlayOrderEqualWeights),
			0,
			(GInstanceInitFunc) rb_random_play_order_equal_weights_init
		};

		rb_random_play_order_equal_weights_type = g_type_register_static (RB_TYPE_PLAY_ORDER,
				"RBRandomPlayOrderEqualWeights",
				&our_info, 0);
	}

	return rb_random_play_order_equal_weights_type;
}

static void
rb_random_play_order_equal_weights_class_init (RBRandomPlayOrderEqualWeightsClass *klass)
{
	RBPlayOrderClass *porder;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->constructor = rb_random_play_order_equal_weights_constructor;
	object_class->finalize = rb_random_play_order_equal_weights_finalize;


	porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_random_play_order_equal_weights_get_next;
	porder->go_next = rb_random_play_order_equal_weights_go_next;
	porder->get_previous = rb_random_play_order_equal_weights_get_previous;
	porder->go_previous = rb_random_play_order_equal_weights_go_previous;
}

RBRandomPlayOrderEqualWeights *
rb_random_play_order_equal_weights_new (RBShellPlayer *player)
{
	RBRandomPlayOrderEqualWeights *rorder;

	rorder = g_object_new (RB_TYPE_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS,
			"player", player,
			NULL);

	g_return_val_if_fail (rorder->priv != NULL, NULL);
	g_return_val_if_fail (rorder->priv->history != NULL, NULL);

	return rorder;
}

static void
rb_random_play_order_equal_weights_init (RBRandomPlayOrderEqualWeights *rorder)
{
	rorder->priv = g_new0 (RBRandomPlayOrderEqualWeightsPrivate, 1);
}

static GObject *
rb_random_play_order_equal_weights_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties)
{
	RBRandomPlayOrderEqualWeights *rorder;
	RBShellPlayer *player;

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (G_OBJECT_CLASS (parent_class)
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
rb_random_play_order_equal_weights_finalize (GObject *object)
{
	RBRandomPlayOrderEqualWeights *rorder;
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (object));

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (object);

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

static RhythmDBEntry* 
rb_random_play_order_equal_weights_get_next (RBPlayOrder* porder)
{
	RBRandomPlayOrderEqualWeights *rorder;
	RhythmDBEntry *entry = NULL;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder);

	if (rb_history_current (rorder->priv->history) == NULL
			|| (rb_play_order_player_is_playing (porder)
				&& rb_history_current (rorder->priv->history) == rb_history_last (rorder->priv->history))) {
		RBEntryView *entry_view;
		entry_view = rb_play_order_get_entry_view (porder);
		if (entry_view == NULL)
			return NULL;

		rb_debug ("choosing random entry");
		entry = rb_entry_view_get_random_entry (entry_view);
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
rb_random_play_order_equal_weights_go_next (RBPlayOrder* porder)
{
	RBRandomPlayOrderEqualWeights *rorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder));

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder);

	if (rb_play_order_player_is_playing (porder))
		rb_history_go_next (rorder->priv->history);
	else {
		/* Leave the current entry current */
	}
}

static RhythmDBEntry*
rb_random_play_order_equal_weights_get_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrderEqualWeights *rorder;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder), NULL);
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_val_if_fail (rb_play_order_player_is_playing (porder), NULL);

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder);

	rb_debug ("choosing history entry");
	return rb_history_previous (rorder->priv->history);
}

static void
rb_random_play_order_equal_weights_go_previous (RBPlayOrder* porder)
{
	RBRandomPlayOrderEqualWeights *rorder;

	g_return_if_fail (porder != NULL);
	g_return_if_fail (RB_IS_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder));
	/* It doesn't make sense to call get_previous when the player is stopped */
	g_return_if_fail (rb_play_order_player_is_playing (porder));

	rorder = RB_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS (porder);

	rb_history_go_previous (rorder->priv->history);
}

static void
playing_source_changed_cb (RBRandomPlayOrderEqualWeights *rorder)
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
				     RBRandomPlayOrderEqualWeights *rorder)
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
