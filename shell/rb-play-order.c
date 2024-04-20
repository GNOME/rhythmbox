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

#include <glib/gi18n.h>

#include "rb-play-order.h"

#include "rb-shell-player.h"
#include "rb-debug.h"

/**
 * SECTION:rbplayorder
 * @short_description: base class for play order implementations
 *
 * A play order defines an ordering of the entries from a #RhythmDBQueryModel that
 * the #RBShellPlayer uses to get the next or previous entry to play.
 *
 * Play order methods are invoked when changes are made to the query model, when
 * the query model is replaced, the playing source is changed, or a new playing
 * entry is selected.
 *
 * The play order must implement methods to check for, retrieve, and move to the
 * next and previous entries in the play order.  Only the go_next and go_previous
 * methods actually change anything.
 *
 * The play order should also emit the have-next-previous-changed signal to hint that
 * the availability of either a next or previous entry in the order may have changed.
 * This information is used to update the sensitivity of the next and previous buttons.
 */

static void rb_play_order_class_init (RBPlayOrderClass *klass);
static void rb_play_order_init (RBPlayOrder *porder);
static void rb_play_order_dispose (GObject *object);
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
static void rb_play_order_row_deleted_cb (GtkTreeModel *model,
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
	gulong sync_playing_entry_id;
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
	HAVE_NEXT_PREVIOUS_CHANGED,
	LAST_SIGNAL
};

static guint rb_play_order_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBPlayOrder, rb_play_order, G_TYPE_OBJECT)

static void
rb_play_order_class_init (RBPlayOrderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_play_order_dispose;
	object_class->set_property = rb_play_order_set_property;
	object_class->get_property = rb_play_order_get_property;

	klass->has_next = default_has_next;
	klass->has_previous = default_has_previous;
	klass->playing_entry_removed = default_playing_entry_removed;

	/**
	 * RBPlayOrder:player:
	 *
	 * The #RBShellPlayer instance
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
						 	      "RBShellPlayer",
						 	      "Rhythmbox Player",
						 	      RB_TYPE_SHELL_PLAYER,
						 	      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBPlayOrder:playing-entry:
	 *
	 * The current playing #RhythmDBEntry
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYING_ENTRY,
					 g_param_spec_boxed ("playing-entry",
						 	     "RhythmDBEntry",
						 	     "Playing entry",
							     RHYTHMDB_TYPE_ENTRY,
						 	     G_PARAM_READWRITE));

	/**
	 * RBPlayOrder::have-next-previous-changed:
	 * @porder: the #RBPlayOrder
	 * @have_next: if %TRUE, the play order has at least one more entry
	 * @have_previous: if %TRUE, the play order has at least one entry
	 *    before the current entry
	 *
	 * Emitted as a hint to suggest that the sensitivity of next/previous
	 * buttons may need to be updated.
	 */
	rb_play_order_signals[HAVE_NEXT_PREVIOUS_CHANGED] =
		g_signal_new ("have_next_previous_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayOrderClass, have_next_previous_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (RBPlayOrderPrivate));
}

static void
rb_play_order_init (RBPlayOrder *porder)
{
	porder->priv = RB_PLAY_ORDER_GET_PRIVATE (porder);
}

static void
rb_play_order_dispose (GObject *object)
{
	RBPlayOrder *porder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAY_ORDER (object));

	porder = RB_PLAY_ORDER (object);

	if (porder->priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      G_CALLBACK (rb_play_order_entry_added_cb),
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      G_CALLBACK (rb_play_order_row_deleted_cb),
						      porder);
		g_object_unref (porder->priv->query_model);
		porder->priv->query_model = NULL;
	}

	if (porder->priv->db != NULL) {
		g_object_unref (porder->priv->db);
		porder->priv->db = NULL;
	}

	if (porder->priv->playing_entry != NULL) {
		rhythmdb_entry_unref (porder->priv->playing_entry);
		porder->priv->playing_entry = NULL;
	}

	G_OBJECT_CLASS (rb_play_order_parent_class)->dispose (object);
}

static void
rb_play_order_set_player (RBPlayOrder   *porder,
			  RBShellPlayer *player)
{
	porder->priv->player = player;
}

static void
rb_play_order_set_playing_entry_internal (RBPlayOrder   *porder,
					  RhythmDBEntry *entry)
{
	RhythmDBEntry *old_entry;

	old_entry = porder->priv->playing_entry;
	porder->priv->playing_entry = entry;

	if (porder->priv->playing_entry != NULL) {
		rhythmdb_entry_ref (porder->priv->playing_entry);
	}

	if (RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed)
		RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_changed (porder, old_entry, entry);

	if (old_entry != NULL) {
		rhythmdb_entry_unref (old_entry);
	}

	rb_play_order_update_have_next_previous (porder);
}

static void
rb_play_order_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBPlayOrder *porder = RB_PLAY_ORDER (object);

	switch (prop_id) {
	case PROP_PLAYER:
		rb_play_order_set_player (porder, g_value_get_object (value));
		break;
	case PROP_PLAYING_ENTRY:
		rb_play_order_set_playing_entry_internal (porder, g_value_get_boxed (value));
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
		g_value_set_boxed (value, porder->priv->playing_entry);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_play_order_get_player:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: (transfer none): #RBShellPlayer instance
 */
RBShellPlayer *
rb_play_order_get_player (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);

	return porder->priv->player;
}

/**
 * rb_play_order_get_source:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: (transfer none): the playing #RBSource instance.
 **/
RBSource *
rb_play_order_get_source (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);

	return porder->priv->source;
}

/**
 * rb_play_order_get_db:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: (transfer none): the #RhythmDB instance.
 **/
RhythmDB *
rb_play_order_get_db (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);

	return porder->priv->db;
}

/**
 * rb_play_order_get_query_model:
 * @porder: #RBPlayOrder instance
 *
 * Only for use by #RBPlayOrder subclasses.
 *
 * Returns: (transfer none): the active #RhythmDBQueryModel for the playing source.
 */
RhythmDBQueryModel *
rb_play_order_get_query_model (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);

	return porder->priv->query_model;
}

/**
 * rb_play_order_player_is_playing:
 * @porder: #RBPlayOrder instance
 *
 * Returns %TRUE if there is a current playing entry in the play order.
 *
 * Return value: %TRUE if playing
 **/
gboolean
rb_play_order_player_is_playing (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), FALSE);

	return (porder->priv->playing_entry != NULL);
}

/**
 * rb_play_order_set_playing_entry:
 * @porder: #RBPlayOrder instance
 * @entry: (transfer none) (allow-none): The new playing entry (or NULL for none)
 *
 * Sets the playing entry in the play order.
 **/
void
rb_play_order_set_playing_entry (RBPlayOrder *porder,
				 RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	rb_play_order_set_playing_entry_internal (porder, entry);
}

/**
 * rb_play_order_get_playing_entry:
 * @porder: #RBPlayOrder instance
 *
 * Returns the current playing entry in the play order.
 *
 * Returns: (transfer full): playing entry
 */
RhythmDBEntry *
rb_play_order_get_playing_entry (RBPlayOrder *porder)
{
	RhythmDBEntry *entry;

	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);

	entry = porder->priv->playing_entry;
	if (entry != NULL) {
		rhythmdb_entry_ref (entry);
	}

	return entry;
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

	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	g_object_get (porder->priv->player,
		      "db", &db,
		      NULL);

	if (db != porder->priv->db) {
		if (RB_PLAY_ORDER_GET_CLASS (porder)->db_changed)
			RB_PLAY_ORDER_GET_CLASS (porder)->db_changed (porder, db);

		if (porder->priv->db != NULL) {
			g_object_unref (porder->priv->db);
		}

		porder->priv->db = g_object_ref (db);
	}

	g_object_unref (db);

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

	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	if (porder->priv->source)
		g_object_get (porder->priv->source, "query-model", &new_model, NULL);

	if (porder->priv->query_model == new_model) {
		if (new_model != NULL)
			g_object_unref (new_model);
		return;
	}

	if (porder->priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      rb_play_order_entry_added_cb,
						      porder);
		g_signal_handlers_disconnect_by_func (G_OBJECT (porder->priv->query_model),
						      rb_play_order_row_deleted_cb,
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
					 G_CALLBACK (rb_play_order_row_deleted_cb),
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
rb_play_order_entry_added_cb (GtkTreeModel *model,
			      GtkTreePath *path,
			      GtkTreeIter *iter,
			      RBPlayOrder *porder)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model),
						    iter);
	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_added)
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_added (porder, entry);

	if (!rhythmdb_query_model_has_pending_changes (RHYTHMDB_QUERY_MODEL (model)))
		rb_play_order_update_have_next_previous (porder);

	rhythmdb_entry_unref (entry);
}

/**
 * rb_play_order_row_deleted_cb:
 * @model: #GtkTreeModel
 * @entry: the #RhythmDBEntry removed from the model
 * @porder: #RBPlayOrder instance
 *
 * Called when an entry is removed from the active #RhythmDBQueryModel.
 * Subclasses should implement entry_removed() to make any necessary
 * changes if they store any state based on the contents of the
 * #RhythmDBQueryModel.
 *
 * If the removed entry is the current playing entry, the playing-entry-deleted
 * signal is emitted.
 */
static void
rb_play_order_row_deleted_cb (GtkTreeModel *model,
			      GtkTreePath *row,
			      RBPlayOrder *porder)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (model), row);
	if (entry == porder->priv->playing_entry) {
		RB_PLAY_ORDER_GET_CLASS (porder)->playing_entry_removed (porder, entry);
	}

	if (RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed)
		RB_PLAY_ORDER_GET_CLASS (porder)->entry_removed (porder, entry);

	if (!rhythmdb_query_model_has_pending_changes (RHYTHMDB_QUERY_MODEL (model)))
		rb_play_order_update_have_next_previous (porder);

	rhythmdb_entry_unref (entry);
}

static gboolean
default_has_next (RBPlayOrder *porder)
{
	RhythmDBEntry *entry;
	gboolean res;

	entry = rb_play_order_get_next (porder);

	res = (entry != NULL);
	if (entry)
		rhythmdb_entry_unref (entry);

	return res;
}

static gboolean
default_has_previous (RBPlayOrder *porder)
{
	RhythmDBEntry *entry;
	gboolean res;

	entry = rb_play_order_get_previous (porder);

	res = (entry != NULL);
	if (entry)
		rhythmdb_entry_unref (entry);

	return res;
}

static gboolean
sync_playing_entry_cb (RBPlayOrder *porder)
{
	RBShellPlayer *player;

	player = rb_play_order_get_player (porder);

	if (porder->priv->playing_entry) {
		rb_shell_player_play_entry (player,
					    porder->priv->playing_entry,
					    rb_play_order_get_source (porder));
	} else {
		/* Just try to play something.  This is mostly here to make
		 * the play queue work correctly, but it might be helpful otherwise.
		 */
		GError *error = NULL;
		if (!rb_shell_player_do_next (player, &error)) {
			if (error->domain != RB_SHELL_PLAYER_ERROR ||
			    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
				g_warning ("sync_playing_entry_cb: Unhandled error: %s", error->message);
		}
	}
	porder->priv->sync_playing_entry_id = 0;
	return FALSE;
}

static void
default_playing_entry_removed (RBPlayOrder *porder,
			       RhythmDBEntry *entry)
{
	RBShellPlayer *player = rb_play_order_get_player (porder);
	RBSource *source = rb_shell_player_get_playing_source (player);

	rb_debug ("playing entry removed");

	/* only clear the playing source if the source this play order is using
	 * is currently playing.
	 */
	if (source == rb_play_order_get_source (porder)) {
		switch (rb_source_handle_eos (source)) {
		case RB_SOURCE_EOF_ERROR:
		case RB_SOURCE_EOF_STOP:
		case RB_SOURCE_EOF_RETRY:
			/* stop playing */
			rb_shell_player_stop (player);
			break;
		case RB_SOURCE_EOF_NEXT:
			{
				RhythmDBEntry *next_entry;

				/* go to next song, in an idle function so that other handlers run first */
				next_entry = rb_play_order_get_next (porder);

				if (next_entry == entry) {
					rhythmdb_entry_unref (next_entry);
					next_entry = NULL;
				}

				rb_play_order_set_playing_entry_internal (porder, next_entry);
				if (porder->priv->sync_playing_entry_id == 0) {
					porder->priv->sync_playing_entry_id =
						g_idle_add_full (G_PRIORITY_HIGH_IDLE,
								 (GSourceFunc) sync_playing_entry_cb,
								 porder,
								 NULL);
				}

				if (next_entry != NULL) {
					rhythmdb_entry_unref (next_entry);
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
rb_play_order_has_next (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_next != NULL, FALSE);

	return RB_PLAY_ORDER_GET_CLASS (porder)->has_next (porder);
}

/**
 * rb_play_order_get_next:
 * @porder: RBPlayOrder instance
 *
 * Returns the next entry in the play order, or the first if not currently playing.
 *
 * Returns: (transfer full): next entry to play
 */
RhythmDBEntry *
rb_play_order_get_next (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);
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
	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_next) {
		RB_PLAY_ORDER_GET_CLASS (porder)->go_next (porder);
	} else if (RB_PLAY_ORDER_GET_CLASS (porder)->get_next) {
		RhythmDBEntry *entry;

		entry = RB_PLAY_ORDER_GET_CLASS (porder)->get_next (porder);
		rb_play_order_set_playing_entry (porder, entry);
		if (entry != NULL) {
			rhythmdb_entry_unref (entry);
		}
	}
}

/**
 * rb_play_order_has_previous:
 * @porder: RBPlayOrder instance
 *
 * Returns %TRUE if there is an entry before the current entry in the play order.
 * If not currently playing, returns %FALSE.
 *
 * Return value: %TRUE if previous entry exists
 */
gboolean
rb_play_order_has_previous (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), FALSE);
	g_return_val_if_fail (RB_PLAY_ORDER_GET_CLASS (porder)->has_previous != NULL, FALSE);

	return RB_PLAY_ORDER_GET_CLASS (porder)->has_previous (porder);
}

/**
 * rb_play_order_get_previous:
 * @porder: RBPlayOrder instance
 *
 * Returns the previous entry in the play order, or NULL if not currently playing.
 *
 * Return value: (transfer full): previous entry
 */
RhythmDBEntry *
rb_play_order_get_previous (RBPlayOrder *porder)
{
	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), NULL);
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
	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	if (RB_PLAY_ORDER_GET_CLASS (porder)->go_previous) {
		RB_PLAY_ORDER_GET_CLASS (porder)->go_previous (porder);
	} else if (RB_PLAY_ORDER_GET_CLASS (porder)->get_previous) {
		RhythmDBEntry *entry;

		entry = RB_PLAY_ORDER_GET_CLASS (porder)->get_previous (porder);
		rb_play_order_set_playing_entry (porder, entry);
		if (entry != NULL) {
			rhythmdb_entry_unref (entry);
		}
	}
}

/**
 * rb_play_order_model_not_empty:
 * @porder: RBPlayOrder instance
 *
 * Returns %TRUE if the #RhythmDBQueryModel is not empty.
 * Can be used to implement has_next and has_previous for play orders
 * that have no beginning or end.
 *
 * Return value: %TRUE if not empty
 */
gboolean
rb_play_order_model_not_empty (RBPlayOrder *porder)
{
	GtkTreeIter iter;

	g_return_val_if_fail (RB_IS_PLAY_ORDER (porder), FALSE);

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
	gboolean have_next;
	gboolean have_previous;

	g_return_if_fail (RB_IS_PLAY_ORDER (porder));

	have_next = rb_play_order_has_next (porder);
	have_previous = rb_play_order_has_previous (porder);

	if ((have_next != porder->priv->have_next) ||
	    (have_previous != porder->priv->have_previous)) {
		g_signal_emit (G_OBJECT (porder), rb_play_order_signals[HAVE_NEXT_PREVIOUS_CHANGED],
			       0, have_next, have_previous);
		porder->priv->have_next = have_next;
		porder->priv->have_previous = have_previous;
	}
}
