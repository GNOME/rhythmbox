/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
 *  arch-tag: Implementation of base class for play order classes
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

#include <string.h>

#include <glib/gi18n.h>

#include "rb-play-order.h"

#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "rb-marshal.h"

/* Play Orders */
#include "rb-play-order-linear.h"
#include "rb-play-order-linear-loop.h"
#include "rb-play-order-shuffle.h"
#include "rb-play-order-random-equal-weights.h"
#include "rb-play-order-random-by-age.h"
#include "rb-play-order-random-by-rating.h"
#include "rb-play-order-random-by-age-and-rating.h"
#include "rb-play-order-queue.h"

static void rb_play_order_class_init (RBPlayOrderClass *klass);
static void rb_play_order_init (RBPlayOrder *porder);
static GObject *rb_play_order_constructor (GType type, guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
static void rb_play_order_finalize (GObject *object);
static void rb_play_order_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_play_order_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static gboolean default_has_next (RBPlayOrder *porder);
static gboolean default_has_previous (RBPlayOrder *porder);
static void default_playing_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry);

static void rb_play_order_entry_added_cb (GtkTreeModel *model,
					  GtkTreePath *path,
					  GtkTreeIter *iter,
					  RBPlayOrder *porder);
static void rb_play_order_entry_deleted_cb (GtkTreeModel *model,
					    GtkTreePath *path,
					    RBPlayOrder *porder);
static void rb_play_order_query_model_changed_cb (GObject *source, 
						  GParamSpec *arg,
						  RBPlayOrder *porder);
static void rb_play_order_update_have_next_previous (RBPlayOrder *porder);

struct RBPlayOrderPrivate
{
	RBShellPlayer *player;
	RBSource *source;
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	RhythmDBEntry *playing_entry;
	gulong query_model_change_id;
	gboolean have_next;
	gboolean have_previous;
};

#define RB_PLAY_ORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAY_ORDER, RBPlayOrderPrivate))

enum
{
	PROP_0,
	PROP_PLAYER,
	PROP_PLAYING_ENTRY
};

enum
{
	PLAYING_ENTRY_REMOVED,
	HAVE_NEXT_PREVIOUS_CHANGED,
	LAST_SIGNAL
};

static guint rb_play_order_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBPlayOrder, rb_play_order, G_TYPE_OBJECT)

static void
rb_play_order_class_init (RBPlayOrderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = rb_play_order_constructor;
	object_class->finalize = rb_play_order_finalize;
	object_class->set_property = rb_play_order_set_property;
	object_class->get_property = rb_play_order_get_property;

	klass->has_next = default_has_next;
	klass->has_previous = default_has_previous;
	klass->playing_entry_removed = default_playing_entry_removed;

	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
						 	      "RBShellPlayer",
						 	      "Rhythmbox Player",
						 	      RB_TYPE_SHELL_PLAYER,
						 	      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	g_object_class_install_property (object_class,
					 PROP_PLAYING_ENTRY,
					 g_param_spec_pointer ("playing-entry",
						 	       "RhythmDBEntry",
						 	       "Playing entry",
						 	       G_PARAM_READWRITE));
	
	rb_play_order_signals[PLAYING_ENTRY_REMOVED] =
		g_signal_new ("playing_entry_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayOrderClass, playing_entry_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
	rb_play_order_signals[HAVE_NEXT_PREVIOUS_CHANGED] =
		g_signal_new ("have_next_previous_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayOrderClass, have_next_previous_changed),
			      NULL, NULL,
			      rb_marshal_VOID__BOOLEAN_BOOLEAN,
			      G_TYPE_NONE,
			      2, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (RBPlayOrderPrivate));
}

static void
rb_play_order_init (RBPlayOrder *porder)
{
	porder->priv = RB_PLAY_ORDER_GET_PRIVATE (porder);
}

static GObject *
rb_play_order_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBPlayOrder *porder;

	porder = RB_PLAY_ORDER (G_OBJECT_CLASS (rb_play_order_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	return G_OBJECT (porder);
}

static void
rb_play_order_finalize (GObject *object)
{
	RBPlayOrder *porder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAY_ORDER (object));

	porder = RB_PLAY_ORDER (object);

	if (porder->priv->query_model) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      G_CALLBACK (rb_play_order_entry_added_cb),
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      G_CALLBACK (rb_play_order_entry_deleted_cb),
						      porder);
	}

	G_OBJECT_CLASS (rb_play_order_parent_class)->finalize (object);
}

static void
rb_play_order_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBPlayOrder *porder = RB_PLAY_ORDER (object);
	RhythmDBEntry *entry;
	RhythmDBEntry *old_entry;

	switch (prop_id) {
	case PROP_PLAYER:
		porder->priv->player = g_value_get_object (value);
		break;
	case PROP_PLAYING_ENTRY:
		old_entry = porder->priv->playing_entry;
		entry = g_value_get_pointer (value); 
		porder->priv->playing_entry = entry;

		if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed)
			RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed (porder, old_entry, entry);
		if (old_entry)
			rhythmdb_entry_unref (porder->priv->db, old_entry);
		if (entry)
			rhythmdb_entry_ref (porder->priv->db, entry);

		rb_play_order_update_have_next_previous (porder);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_play_order_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBPlayOrder *porder = RB_PLAY_ORDER (object);

	switch (prop_id)
	{
	case PROP_PLAYER:
		g_value_set_object (value, porder->priv->player);
		break;
	case PROP_PLAYING_ENTRY:
		g_value_set_pointer (value, porder->priv->playing_entry);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_play_order_get_orders:
 *
 * Defines the set of available play orders, their translatable descriptions, their
 * constructor functions, whether they should appear in a drop-down list of 
 * play orders, and which one is the default.
 *
 * This should be the only function with full knowledge of what play orders are
 * available.
 */
const RBPlayOrderDescription *
rb_play_order_get_orders (void)
{
	/* Exactly one entry must have is_default==TRUE. Otherwise you will
	 * cause a g_assert(). */
	static const RBPlayOrderDescription orders[] = {
		{ "linear", N_("Linear"), rb_linear_play_order_new, TRUE, TRUE },
		{ "linear-loop", N_("Linear looping"), rb_linear_play_order_loop_new, TRUE, FALSE },
		{ "shuffle", N_("Shuffle"), rb_shuffle_play_order_new, TRUE, FALSE },
		{ "random-equal-weights", N_("Random with equal weights"), rb_random_play_order_equal_weights_new, TRUE, FALSE },
		{ "random-by-age", N_("Random by time since last play"), rb_random_play_order_by_age_new, TRUE, FALSE },
		{ "random-by-rating", N_("Random by rating"), rb_random_play_order_by_rating_new, TRUE, FALSE },
		{ "random-by-age-and-rating", N_("Random by time since last play and rating"), rb_random_play_order_by_age_and_rating_new, TRUE, FALSE },
		{ "queue", N_("Linear, removing entries once played"), rb_queue_play_order_new, FALSE, FALSE },
		{ NULL, NULL, NULL },
	};
	return orders;
}

/**
 * rb_play_order_new:
 * @porder_name: Play order type name
 * @player: #RBShellPlayer instance to attach to
 *
 * Creates a new #RBPlayOrder of the specified type.
 *
 * Returns: #RBPlayOrder instance
 **/
RBPlayOrder *
rb_play_order_new (const char* porder_name, RBShellPlayer *player)
{
	int default_index = -1;
	const RBPlayOrderDescription *orders = rb_play_order_get_orders ();
	int i;

	g_return_val_if_fail (porder_name != NULL, NULL);
	g_return_val_if_fail (player != NULL, NULL);

	for (i=0; orders[i].name != NULL; ++i) {
		if (strcmp (orders[i].name, porder_name) == 0)
			return orders[i].constructor (player);
		if (orders[i].is_default) {
			/* There must not be two default play orders */
			g_assert (default_index == -1);
			default_index = i;
		}
	}
	/* There must be a default play order */
	g_assert (default_index != -1);

	g_warning ("Unknown value \"%s\" in GConf key \"" CONF_STATE_PLAY_ORDER
			"\". Using %s play order.", porder_name, orders[default_index].name);
	return orders[default_index].constructor (player);
}

/**
 * rb_play_order_get_player:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: #RBShellPlayer instance
 **/
RBShellPlayer *
rb_play_order_get_player (RBPlayOrder *porder)
{
	return porder->priv->player;
}

/**
 * rb_play_order_get_source:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: the playing #RBSource instance.
 **/
RBSource *
rb_play_order_get_source (RBPlayOrder *porder)
{
	return porder->priv->source;
}

/**
 * rb_play_order_get_db:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: the #RhythmDB instance.
 **/
RhythmDB *
rb_play_order_get_db (RBPlayOrder *porder)
{
	return porder->priv->db;
}

/**
 * rb_play_order_get_query_model:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: the active #RhythmDBQueryModel for the playing source.
 */
RhythmDBQueryModel *
rb_play_order_get_query_model (RBPlayOrder *porder)
{
	return porder->priv->query_model;
}

/**
 * rb_play_order_player_is_playing:
 * @porder: #RBPlayOrder instance
 *
 * Returns: true if there is a current playing entry in the play order.
 **/
gboolean
rb_play_order_player_is_playing (RBPlayOrder *porder)
{
	return (porder->priv->playing_entry != NULL);
}

/**
 * rb_play_order_set_playing_entry:
 * @porder: #RBPlayOrder instance
 * @entry: The new playing entry (or NULL for none)
 *
 * Sets the playing entry in the play order.
 **/
void
rb_play_order_set_playing_entry (RBPlayOrder *porder,
				 RhythmDBEntry *entry)
{
	g_object_set (G_OBJECT (porder), "playing-entry", entry, NULL);
}

/**
 * rb_play_order_get_playing_entry:
 * @porder: #RBPlayOrder instance
 *
 * Returns: the current playing entry in the play order.
 */
RhythmDBEntry *
rb_play_order_get_playing_entry (RBPlayOrder *porder)
{
	return porder->priv->playing_entry;
}

/**
 * rb_play_order_playing_source_changed:
 * @porder: #RBPlayOrder instance
 * @source: New playing #RBSource
 *
 * Sets the playing #RBSource for the play order.  Should be called
 * by #RBShellPlayer when the active source changes.  Subclasses
 * should implement playing_source_changed() to make any necessary
 * changes.
 */
void
rb_play_order_playing_source_changed (RBPlayOrder *porder,
				      RBSource *source)
{
	RhythmDB *db = NULL;

	g_return_if_fail (porder != NULL);

	g_object_get (G_OBJECT (porder->priv->player),
		      "db", &db,
		      NULL);

	if (db != porder->priv->db) {
		if (RB_PLAY_ORDER_GET_CLASS (porder)->db_changed)
			RB_PLAY_ORDER_GET_CLASS (porder)->db_changed (porder, db);
		porder->priv->db = db;
	}

	if (source != porder->priv->source) {
		if (porder->priv->source) {
			g_signal_handler_disconnect (G_OBJECT (porder->priv->source),
						     porder->priv->query_model_change_id);
		}
		
		porder->priv->source = source;
		if (porder->priv->source) {
			porder->priv->query_model_change_id =
				g_signal_connect_object (G_OBJECT (porder->priv->source),
							 "notify::query-model",
							 G_CALLBACK (rb_play_order_query_model_changed_cb),
							 porder, 0);
		}

		rb_play_order_query_model_changed (porder);

		if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_source_changed)
			RB_PLAY_ORDER_GET_CLASS (porder)->playing_source_changed (porder);
		
		rb_play_order_update_have_next_previous (porder);
	}
}

static void
rb_play_order_query_model_changed_cb (GObject *source, 
				      GParamSpec *arg,
				      RBPlayOrder *porder)
{
	rb_play_order_query_model_changed (porder);
}

/**
 * rb_play_order_query_model_changed:
 * @porder: RBPlayOrder instance
 *
 * Updates the #RhythmDBQueryModel instance for the play order.
 * Called from the #RBSource notify signal handler, and also from
 * #rb_play_order_source_changed.  Subclasses should implement
 * query_model_changed() to make any necessary adjustments if they
 * store any state based on the contents of the #RhythmDBQueryModel.
 */
void 
rb_play_order_query_model_changed (RBPlayOrder *porder)
{
	RhythmDBQueryModel *new_model = NULL;

	if (porder->priv->source)
		g_object_get (G_OBJECT (porder->priv->source), "query-model", &new_model, NULL);

	if (porder->priv->query_model == new_model)
		return;

	if (porder->priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      rb_play_order_entry_added_cb,
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      rb_play_order_entry_deleted_cb,
						      porder);
		g_object_unref (porder->priv->query_model);
		porder->priv->query_model = NULL;
	}

	if (new_model != NULL) {
		porder->priv->query_model = new_model;
		g_signal_connect_object (G_OBJECT (porder->priv->query_model),
					 "row-inserted",
					 G_CALLBACK (rb_play_order_entry_added_cb),
					 porder, 0);
		g_signal_connect_object (G_OBJECT (porder->priv->query_model),
					 "row-deleted",
					 G_CALLBACK (rb_play_order_entry_deleted_cb),
					 porder, 0);
	}

	if (RB_PLAY_ORDER_GET_CLASS (porder)->query_model_changed)
		RB_PLAY_ORDER_GET_CLASS (porder)->query_model_changed (porder);
		
	rb_play_order_update_have_next_previous (porder);
}

/**
 * rb_play_order_entry_added_cb:
 * @model: #GtkTreeModel
 * @path: #GtkTreePath for added entry
 * @iter: #GtkTreeIter for added entry
 * @porder: #RBPlayOrder instance
 *
 * Called when a new entry is added to the active #RhythmDBQueryModel.
 * Subclasses should implement entry_added() to make any necessary 
 * changes if they store any state based on the contents of the 
 * #RhythmDBQueryModel.
 */
static void
rb_play_order_entry_added_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBPlayOrder *porder)
{
	RhythmDBEntry *entry = rhythmdb_query_model_iter_to_entry(RHYTHMDB_QUERY_MODEL (model),
								  iter);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_added)
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_added (porder, entry);
	
	if (!rhythmdb_query_model_has_pending_changes (RHYTHMDB_QUERY_MODEL (model)))
		rb_play_order_update_have_next_previous (porder);
}

/**
 * rb_play_order_entry_removed_cb:
 * @model: #GtkTreeModel
 * @path: #GtkTreePath for removed entry
 * @porder: #RBPlayOrder instance
 *
 * Called when a new entry is removed from the active #RhythmDBQueryModel.
 * Subclasses should implement entry_removed() to make any necessary 
 * changes if they store any state based on the contents of the 
 * #RhythmDBQueryModel.
 *
 * If the removed entry is the current playing entry, the playing-entry-deleted
 * signal is emitted.
 */
static void
rb_play_order_entry_deleted_cb (GtkTreeModel *model, GtkTreePath *path, RBPlayOrder *porder)
{
	RhythmDBEntry *entry;
	GtkTreeIter iter;
	
	gtk_tree_model_get_iter (model, &iter, path);
	entry = rhythmdb_query_model_iter_to_entry(RHYTHMDB_QUERY_MODEL (model),
						   &iter);

	if (entry == porder->priv->playing_entry) {
		rb_debug ("signaling playing_entry_removed");
		g_signal_emit (G_OBJECT (porder), rb_play_order_signals[PLAYING_ENTRY_REMOVED],
			       0, entry);
	}
	
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed)
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed (porder, entry);

	if (!rhythmdb_query_model_has_pending_changes (RHYTHMDB_QUERY_MODEL (model)))
		rb_play_order_update_have_next_previous (porder);
}

static gboolean
default_has_next (RBPlayOrder *porder)
{
	return rb_play_order_get_next (porder) != NULL;
}

static gboolean
default_has_previous (RBPlayOrder *porder)
{
	return rb_play_order_get_previous (porder) != NULL;
}

typedef struct {
	RBShellPlayer *player;
	RhythmDBEntry *entry;
} DoNextIdleData;

static gboolean
do_next_idle_cb (DoNextIdleData *data)
{
	rb_shell_player_play_entry (data->player, data->entry);
	g_free (data);
	return FALSE;
}

static void
default_playing_entry_removed (RBPlayOrder *porder, RhythmDBEntry *entry)
{
	RBShellPlayer *player = rb_play_order_get_player (porder);
	RBSource *source = rb_shell_player_get_playing_source (player);

	/* only clear the playing source if the source this play order is using
	 * is currently playing.
	 */
	if (source == rb_play_order_get_source (porder)) {	
		switch (rb_source_handle_eos (source)) {
		case RB_SOURCE_EOF_ERROR:
		case RB_SOURCE_EOF_STOP:
		case RB_SOURCE_EOF_RETRY:
			/* stop playing */
			rb_shell_player_set_playing_source (player, NULL);
			break;
		case RB_SOURCE_EOF_NEXT:
			{
				RhythmDBEntry *entry;

				entry = rb_play_order_get_next (porder);
				if (entry != NULL) {
					DoNextIdleData *data = g_new0 (DoNextIdleData, 1);
				
					/* go to next song, do in an idle function so that other handler run first */
					data->player = player;
					data->entry = entry;
					g_idle_add_full (G_PRIORITY_HIGH_IDLE, (GSourceFunc)do_next_idle_cb, data, NULL);
				} else {
					rb_shell_player_set_playing_source (player, NULL);
				}
				break;
			}
		}
	} else {
		rb_play_order_set_playing_entry (porder, NULL);
	}
}

/**
 * rb_play_order_has_next:
 * @porder: RBPlayOrder instance.
 *
 * If there is no current playing entry, returns true if the play order is non-empty.
 *
 * Returns: true if there is an entry after the current playing entry in the play order.
 */
gboolean
rb_play_order_has_next (RBPlayOrder* porder)
{
	g_return_val_if_fail (porder != NULL, FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_next != NULL, FALSE);
	return RB_PLAY_ORDER_GET_CLASS (porder)->has_next (porder);
}

/**
 * rb_play_order_get_next:
 * @porder: RBPlayOrder instance
 *
 * Returns: the next entry in the play order, or the first if not currently playing.
 */
RhythmDBEntry *
rb_play_order_get_next (RBPlayOrder *porder)
{
	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->get_next != NULL, NULL);
	return RB_PLAY_ORDER_GET_CLASS (porder)->get_next (porder);
}

/**
 * rb_play_order_go_next:
 * @porder: RBPlayOrder instance
 *
 * Moves to the next entry in the play order.  If not currently playing, sets the
 * first entry in the play order as the playing entry.
 */
void
rb_play_order_go_next (RBPlayOrder *porder)
{
	g_return_if_fail (porder != NULL);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_next)
		RB_PLAY_ORDER_GET_CLASS (porder)->go_next (porder);
	else if (RB_PLAY_ORDER_GET_CLASS (porder)->get_next)
		rb_play_order_set_playing_entry (porder,
						 RB_PLAY_ORDER_GET_CLASS (porder)->get_next (porder));
}

/**
 * rb_play_order_has_previous:
 * @porder: RBPlayOrder instance
 *
 * Returns: true if there is an entry before the current entry in the play order.
 * If not currently playing, returns false.
 */
gboolean
rb_play_order_has_previous (RBPlayOrder* porder)
{
	g_return_val_if_fail (porder != NULL, FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_previous != NULL, FALSE);
	return RB_PLAY_ORDER_GET_CLASS (porder)->has_previous (porder);
}

/**
 * rb_play_order_get_previous:
 * @porder: RBPlayOrder instance
 *
 * Returns: the previous entry in the play order, or NULL if not currently playing.
 */
RhythmDBEntry *
rb_play_order_get_previous (RBPlayOrder *porder)
{
	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->get_previous != NULL, NULL);
	return RB_PLAY_ORDER_GET_CLASS (porder)->get_previous (porder);
}

/**
 * rb_play_order_go_previous:
 * @porder: RBPlayOrder instance
 *
 * Moves to the previous entry in the play order.  If not currently playing, does nothing.
 */
void
rb_play_order_go_previous (RBPlayOrder *porder)
{
	g_return_if_fail (porder != NULL);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_previous)
		RB_PLAY_ORDER_GET_CLASS (porder)->go_previous (porder);
	else if (RB_PLAY_ORDER_GET_CLASS (porder)->get_previous)
		rb_play_order_set_playing_entry (porder,
						 RB_PLAY_ORDER_GET_CLASS (porder)->get_previous (porder));
}

/**
 * rb_play_order_model_not_empty:
 * @porder: RBPlayOrder instance
 *
 * Returns: true if the #RhythmDBQueryModel is not empty.
 * Can be used to implement has_next and has_previous for play orders
 * that have no beginning or end.
 */
gboolean
rb_play_order_model_not_empty (RBPlayOrder *porder)
{
	GtkTreeIter iter;
	if (!porder->priv->query_model)
		return FALSE;
	return gtk_tree_model_get_iter_first (GTK_TREE_MODEL (porder->priv->query_model), &iter);
}

/**
 * rb_play_order_update_have_next_previous:
 * @porder: RBPlayOrder instance
 *
 * Updates the have_next and have_previous flags, and emits a signal if they
 * change.  This is called whenever the play order changes in such a way that
 * these flags might change.
 */
void
rb_play_order_update_have_next_previous (RBPlayOrder *porder)
{
	gboolean have_next = rb_play_order_has_next (porder);
	gboolean have_previous = rb_play_order_has_previous (porder);

	if ((have_next != porder->priv->have_next) ||
	    (have_previous != porder->priv->have_previous)) {
		g_signal_emit (G_OBJECT (porder), rb_play_order_signals[HAVE_NEXT_PREVIOUS_CHANGED],
			       0, have_next, have_previous);
		porder->priv->have_next = have_next;
		porder->priv->have_previous = have_previous;
	}
}

/**
 * rb_play_order_ref_entry_swapped:
 * @entry: #RhythmDBEntry to reference
 * @db: #RhythmDB instance
 *
 * Utility function that can be used with #RBHistory to implement play orders
 * that maintain an internal list of entries.
 */
void
rb_play_order_ref_entry_swapped (RhythmDBEntry *entry, RhythmDB *db)
{
	rhythmdb_entry_ref (db, entry);
}

/**
 * rb_play_order_unref_entry_swapped:
 * @entry: #RhythmDBEntry to unreference
 * @db: #RhythmDB instance
 *
 * Utility function that can be used with #RBHistory to implement play orders
 * that maintain an internal list of entries.
 */
void
rb_play_order_unref_entry_swapped (RhythmDBEntry *entry, RhythmDB *db)
{
	rhythmdb_entry_unref (db, entry);
}
