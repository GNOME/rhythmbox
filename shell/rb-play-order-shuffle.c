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
static GObject *rb_shuffle_play_order_constructor (GType type, guint n_construct_properties,
						   GObjectConstructParam *construct_properties);
static void rb_shuffle_play_order_finalize (GObject *object);

static RhythmDBEntry* rb_shuffle_play_order_get_next (RBPlayOrder* method);
static void rb_shuffle_play_order_go_next (RBPlayOrder* method);
static RhythmDBEntry* rb_shuffle_play_order_get_previous (RBPlayOrder* method);
static void rb_shuffle_play_order_go_previous (RBPlayOrder* method);

static void rb_shuffle_fill_history_from_entry_view (RBShufflePlayOrder *sorder);
static GPtrArray *get_entry_view_contents (RBEntryView *entry_view);

static void playing_source_changed_cb (RBShufflePlayOrder *sorder);
static void entry_view_playing_entry_changed_cb (GObject *entry_view,
						 GParamSpec *pspec,
						 RBShufflePlayOrder *sorder);
static void entry_view_contents_changed_cb (RBShufflePlayOrder *sorder);
static void handle_entry_view_changed (RBShufflePlayOrder *sorder);
static gboolean entry_view_contents_changed (RBShufflePlayOrder *sorder);

struct RBShufflePlayOrderPrivate
{
	RBHistory *history;

	RBSource *source;

	/** TRUE if the entry_view might have changed */
	gboolean entry_view_changed;

	/** If a song is playing while the shuffle is regenerated, and it is
	 * not in the new shuffle, a reference to it is placed here so it can
	 * be removed when it finishes playing. This is NULL when there is no
	 * temporary entry. */
	RhythmDBEntry *temporary_entry;
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

RBShufflePlayOrder *
rb_shuffle_play_order_new (RBShellPlayer *player)
{
	RBShufflePlayOrder *sorder;

	sorder = g_object_new (RB_TYPE_SHUFFLE_PLAY_ORDER,
			       "player", player,
			       NULL);

	return sorder;
}

static void
rb_shuffle_play_order_class_init (RBShufflePlayOrderClass *klass)
{
	RBPlayOrderClass *porder;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->constructor = rb_shuffle_play_order_constructor;
	object_class->finalize = rb_shuffle_play_order_finalize;

	porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_shuffle_play_order_get_next;
	porder->go_next = rb_shuffle_play_order_go_next;
	porder->get_previous = rb_shuffle_play_order_get_previous;
	porder->go_previous = rb_shuffle_play_order_go_previous;
}

static void
rb_shuffle_play_order_init (RBShufflePlayOrder *sorder)
{
	sorder->priv = g_new0 (RBShufflePlayOrderPrivate, 1);

	sorder->priv->history = rb_history_new (FALSE);

	sorder->priv->entry_view_changed = FALSE;
}

static GObject *
rb_shuffle_play_order_constructor (GType type, guint n_construct_properties,
				   GObjectConstructParam *construct_properties)
{
	RBShufflePlayOrder *sorder;
	RBShellPlayer *player;

	sorder = RB_SHUFFLE_PLAY_ORDER (G_OBJECT_CLASS (parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	player = rb_play_order_get_player (RB_PLAY_ORDER (sorder));

	/* Init stuff */
	g_signal_connect_swapped (G_OBJECT (player),
				  "notify::playing-source",
				  G_CALLBACK (playing_source_changed_cb),
				  sorder);
	/* Initialize source */
	playing_source_changed_cb (sorder);

	return G_OBJECT (sorder);
}

static void
rb_shuffle_play_order_finalize (GObject *object)
{
	RBShufflePlayOrder *sorder;
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (object));

	sorder = RB_SHUFFLE_PLAY_ORDER (object);

	player = rb_play_order_get_player (RB_PLAY_ORDER (sorder));

	g_signal_handlers_disconnect_by_func (G_OBJECT (player),
					      G_CALLBACK (playing_source_changed_cb),
					      sorder);


	if (sorder->priv->source) {
		RBEntryView *entry_view = rb_source_get_entry_view (sorder->priv->source);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (entry_view_playing_entry_changed_cb),
						      sorder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (sorder->priv->source),
						      G_CALLBACK (entry_view_contents_changed_cb),
						      sorder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
						      G_CALLBACK (entry_view_contents_changed_cb),
						      sorder);
	}

	g_object_unref (sorder->priv->history);
	g_free (sorder->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shuffle_remove_temporary_entry (RBShufflePlayOrder *sorder)
{
	if (sorder->priv->temporary_entry) {
		g_return_if_fail (sorder->priv->temporary_entry != rb_history_current (sorder->priv->history));
		rb_history_remove_entry (sorder->priv->history, sorder->priv->temporary_entry);
	}
}

static RhythmDBEntry* 
rb_shuffle_play_order_get_next (RBPlayOrder* porder)
{
	RBShufflePlayOrder *sorder;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_SHUFFLE_PLAY_ORDER (porder), NULL);

	sorder = RB_SHUFFLE_PLAY_ORDER (porder);

	handle_entry_view_changed (sorder);

	if (rb_play_order_player_is_playing (porder)) {
		if (rb_history_current (sorder->priv->history) == rb_history_last (sorder->priv->history)) {
			if (eel_gconf_get_boolean (CONF_STATE_REPEAT)
					&& !(rb_history_length (sorder->priv->history) == 1
						&& sorder->priv->temporary_entry)) {
				rb_debug ("No next entry, but repeat is enabled");
				entry = rb_history_first (sorder->priv->history);
			} else {
				entry = NULL;
			}
		} else {
			rb_debug ("choosing next entry in shuffle");
			entry = rb_history_next (sorder->priv->history);
		}
	} else {
		/* If the player is currently stopped, the "next" (first) song
		 * is the first in the shuffle. */
		rb_debug ("choosing current entry in shuffle");
		entry = rb_history_current (sorder->priv->history);
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

	if (rb_play_order_player_is_playing (porder)) {
		if (rb_history_current (sorder->priv->history) == rb_history_last (sorder->priv->history)) {
			if (eel_gconf_get_boolean (CONF_STATE_REPEAT))
				rb_history_go_first (sorder->priv->history);
		} else
			rb_history_go_next (sorder->priv->history);

		rb_shuffle_remove_temporary_entry (sorder);
	} else {
		/* If the player is currently stopped, the current song in the
		 * history needs to stay current */
	}
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

	handle_entry_view_changed (sorder);

	rb_debug ("choosing previous history entry");
	entry = rb_history_previous (sorder->priv->history);

	if (entry == NULL) {
		if (eel_gconf_get_boolean (CONF_STATE_REPEAT)
				&& !(rb_history_length (sorder->priv->history) == 1
					&& sorder->priv->temporary_entry)) {
			rb_debug ("No previous entry, but repeat is enabled");
			entry = rb_history_last (sorder->priv->history);
		}
	}

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

	if (rb_history_current (sorder->priv->history) == rb_history_first (sorder->priv->history)) {
		if (eel_gconf_get_boolean (CONF_STATE_REPEAT))
			rb_history_go_last (sorder->priv->history);
	} else
		rb_history_go_previous (sorder->priv->history);

	rb_shuffle_remove_temporary_entry (sorder);
}

static void
rb_shuffle_fill_history_from_entry_view (RBShufflePlayOrder *sorder)
{
	RBEntryView *entry_view;
	GPtrArray *entries;
	guint i;

	rb_history_clear (sorder->priv->history);

	/* Collect all the entries in the entry view */
	g_return_if_fail (sorder->priv->source != NULL);
	entry_view = rb_source_get_entry_view (sorder->priv->source);
	if (entry_view == NULL)
		return;

	entries = get_entry_view_contents (entry_view);

	/* Randomize list */
	for (i=0; i<entries->len; ++i) {
		gint j = g_random_int_range (0, entries->len);
		RhythmDBEntry *tmp;
		tmp = g_ptr_array_index (entries, i);
		g_ptr_array_index (entries, i) = g_ptr_array_index (entries, j);
		g_ptr_array_index (entries, j) = tmp;
	}

	/* Fill History */
	for (i=0; i<entries->len; ++i) {
		rb_history_append (sorder->priv->history, g_ptr_array_index (entries, i));
	}

	{
		/* Put the currently-playing song first */
		RhythmDBEntry *current_entry;
		g_object_get (entry_view,
			      "playing-entry", &current_entry,
			      NULL);
		if (current_entry) {
			rb_history_insert_at_index (sorder->priv->history, current_entry, 0);

			sorder->priv->temporary_entry = current_entry;
			for (i=0; i<entries->len; ++i) {
				if (g_ptr_array_index (entries, i) == current_entry) {
					sorder->priv->temporary_entry = NULL;
					break;
				}
			}
		} else {
			sorder->priv->temporary_entry = NULL;
		}
	}

	g_ptr_array_free (entries, TRUE);

	/* Make sure current pointer is at the beginning */
	rb_history_go_first (sorder->priv->history);
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
playing_source_changed_cb (RBShufflePlayOrder *sorder)
{
	RBShellPlayer *player;
	RBSource *source;
	RhythmDB *db = NULL;

	player = rb_play_order_get_player (RB_PLAY_ORDER (sorder));

	source = rb_shell_player_get_playing_source (player);
	if (source) {
		g_object_get (G_OBJECT (source),
			      "db", &db,
			      NULL);
	}

	g_object_set (G_OBJECT (sorder->priv->history),
		      "db", db,
		      NULL);

	if (source != sorder->priv->source) {
		if (sorder->priv->source != NULL) {
			RBEntryView *entry_view = rb_source_get_entry_view (sorder->priv->source);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (entry_view_playing_entry_changed_cb),
							      sorder);
			g_signal_handlers_disconnect_by_func (G_OBJECT (sorder->priv->source),
							      G_CALLBACK (entry_view_contents_changed_cb),
							      sorder);
			g_signal_handlers_disconnect_by_func (G_OBJECT (entry_view),
							      G_CALLBACK (entry_view_contents_changed_cb),
							      sorder);
		}
		sorder->priv->source = source;
		if (sorder->priv->source != NULL) {
			RBEntryView *entry_view = rb_source_get_entry_view (sorder->priv->source);
			g_signal_connect (G_OBJECT (entry_view),
					  "notify::playing-entry",
					  G_CALLBACK (entry_view_playing_entry_changed_cb),
					  sorder);
			g_signal_connect_swapped (G_OBJECT (sorder->priv->source),
						  "filter_changed",
						  G_CALLBACK (entry_view_contents_changed_cb),
						  sorder);
			g_signal_connect_swapped (G_OBJECT (entry_view),
						  "entry-added",
						  G_CALLBACK (entry_view_contents_changed_cb),
						  sorder);
			g_signal_connect_swapped (G_OBJECT (entry_view),
						  "entry-deleted",
						  G_CALLBACK (entry_view_contents_changed_cb),
						  sorder);

			sorder->priv->entry_view_changed = TRUE;
		}
	}
}

static void
entry_view_playing_entry_changed_cb (GObject *entry_view,
				     GParamSpec *pspec,
				     RBShufflePlayOrder *sorder)
{
	RhythmDBEntry *entry;

	g_return_if_fail (strcmp (pspec->name, "playing-entry") == 0);

	g_object_get (entry_view,
		      "playing-entry", &entry,
		      NULL);
	if (entry) {
		if (entry == rb_history_current (sorder->priv->history)) {
			/* Do nothing */
		} else {
			rb_history_set_playing (sorder->priv->history, entry);
		}
	}
}

static void
entry_view_contents_changed_cb (RBShufflePlayOrder *sorder)
{
	sorder->priv->entry_view_changed = TRUE;
}

static void
handle_entry_view_changed (RBShufflePlayOrder *sorder)
{
	if (sorder->priv->entry_view_changed) {
		if (entry_view_contents_changed (sorder)) {
			rb_shuffle_fill_history_from_entry_view (sorder);
		} else {
			rb_debug ("Spurious call of entry_view_contents_changed()");
		}
		sorder->priv->entry_view_changed = FALSE;
	}
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
entry_view_contents_changed (RBShufflePlayOrder *sorder)
{
	gboolean result = FALSE;
	GPtrArray *history_contents = rb_history_dump (sorder->priv->history);
	GPtrArray *entry_view_contents = get_entry_view_contents (rb_source_get_entry_view (sorder->priv->source));

	if (history_contents->len != entry_view_contents->len)
		result = TRUE;
	else {
		int i;
		g_ptr_array_sort (history_contents, ptr_compare);
		g_ptr_array_sort (entry_view_contents, ptr_compare);
		for (i=0; i<history_contents->len; ++i) {
			if (g_ptr_array_index (history_contents, i) != g_ptr_array_index (entry_view_contents, i)) {
				result = TRUE;
				break;
			}
		}
	}
	g_ptr_array_free (history_contents, TRUE);
	g_ptr_array_free (entry_view_contents, TRUE);
	return result;
}
