/* 
 *  arch-tag: Header for base class for play order classes
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

/**
 * RBPlayOrder defines the interface for classes which can control the order
 * songs play.
 *
 * See rb-play-order-*.{h,c} for examples.
 *
 * Only rb-play-order.c should include the subclasses' headers (and it should
 * only use them to instantiate the right subclass). Anyone else who wants to
 * use the heirarchy should include rb-play-order.h and call the functions
 * defined here.
 *
 * When you add a new play order, remember to update the long description of
 * the state/play_order key in data/rhythmbox.schemas and to add the
 * appropriate code to rb_play_order_new().
 */

#ifndef __RB_PLAY_ORDER_H
#define __RB_PLAY_ORDER_H

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-query-model.h>

#include <rb-shell-player.h>

G_BEGIN_DECLS

#define RB_TYPE_PLAY_ORDER         (rb_play_order_get_type ())
#define RB_PLAY_ORDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAY_ORDER, RBPlayOrder))
#define RB_PLAY_ORDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_PLAY_ORDER, RBPlayOrderClass))
#define RB_IS_PLAY_ORDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAY_ORDER))
#define RB_IS_PLAY_ORDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAY_ORDER))
#define RB_PLAY_ORDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAY_ORDER, RBPlayOrderClass))

typedef struct RBPlayOrderPrivate RBPlayOrderPrivate;

typedef struct
{
	GObject parent;

	RBPlayOrderPrivate *priv;
} RBPlayOrder;

typedef gboolean (*RBPlayOrderQueryFunc) (RBPlayOrder *porder);

typedef struct
{
	GObjectClass parent_class;

	/* EVENTS */
	void (*playing_source_changed) (RBPlayOrder *porder);
	void (*db_changed) (RBPlayOrder *porder, RhythmDB *new_db);
	void (*playing_entry_changed) (RBPlayOrder *porder, RhythmDBEntry *old_entry, RhythmDBEntry *new_entry);
	void (*entry_added) (RBPlayOrder *porder, RhythmDBEntry *entry);
	void (*entry_removed) (RBPlayOrder *porder, RhythmDBEntry *entry);
	void (*query_model_changed) (RBPlayOrder *porder);
	void (*db_entry_deleted) (RBPlayOrder *porder, RhythmDBEntry *entry);
	void (*playing_entry_removed) (RBPlayOrder *porder, RhythmDBEntry *entry);

	/* QUERIES */
	/**
	 * Returns whether there is a next song. This controls the next
	 * button's sensitivity. If not implemented, defaults to
	 * get_next()!=NULL.
	 *
	 * Must not change any visible state.
	 */
	gboolean (*has_next) (RBPlayOrder* porder);
	/**
	 * get_next() must return the next song to play. It's called when a
	 * song finishes, when the user clicks the next button, and when the
	 * user clicks play after playback is stopped.
	 *
	 * get_next() is also used to find the first song. You can figure out
	 * whether the player is currently playing by calling
	 * rb_play_order_player_is_playing(porder).
	 *
	 * Must not change any visible state.
	 */
	RhythmDBEntry* (*get_next) (RBPlayOrder* porder);
	/**
	 * Tells the play order that the user has moved to the next song.
	 * Should be called before the EntryView::playing-entry property is
	 * changed.
	 */
	void (*go_next) (RBPlayOrder* porder);
	/**
	 * Returns whether there is a previous song. This controls the previous
	 * button's sensitivity. If not implemented, defaults to
	 * get_previous()!=NULL.
	 *
	 * Must not change any visible state.
	 */
	gboolean (*has_previous) (RBPlayOrder* porder);
	/**
	 * get_previous() must return the previous song to play. It's called
	 * when the user clicks the previous button within 2 seconds of the
	 * beginning of a song.
	 *
	 * Must not change any visible state.
	 */
	RhythmDBEntry* (*get_previous) (RBPlayOrder* porder);
	/**
	 * Tells the play order that the user has moved to the previous song.
	 * Should be called before the EntryView::playing-entry property is
	 * changed.
	 */
	void (*go_previous) (RBPlayOrder* porder);

	/* SIGNALS */
	void (*have_next_previous_changed) (RBPlayOrder *porder, gboolean have_next, gboolean have_previous);
} RBPlayOrderClass;

GType			rb_play_order_get_type		(void);

RBPlayOrder *		rb_play_order_new		(const char* play_order_name, RBShellPlayer *player);

typedef struct {
	/** Value of the state/play-order gconf key */
	char *name;
	/** Contents of the play order dropdown; should be gettext()ed before use. */
	char *description;
	/** the play order's _new function */
	RBPlayOrder *(*constructor)(RBShellPlayer *player);
	/** TRUE if the play order should appear in the dropdown */
	gboolean is_in_dropdown;
	/** If the value of the state/play-order gconf key isn't found, the one
	 * with is_default==TRUE will be used. */
	gboolean is_default;
} RBPlayOrderDescription;
const RBPlayOrderDescription *	rb_play_order_get_orders	(void);

void			rb_play_order_playing_source_changed	(RBPlayOrder *porder,
								 RBSource *source);
void			rb_play_order_query_model_changed	(RBPlayOrder *porder);

gboolean		rb_play_order_has_next		(RBPlayOrder* porder);
RhythmDBEntry *		rb_play_order_get_next		(RBPlayOrder *porder);
void 			rb_play_order_go_next		(RBPlayOrder *porder);
gboolean		rb_play_order_has_previous	(RBPlayOrder* porder);
RhythmDBEntry *		rb_play_order_get_previous	(RBPlayOrder *porder);
void 			rb_play_order_go_previous	(RBPlayOrder *porder);

void			rb_play_order_set_playing_entry (RBPlayOrder *porder,
							 RhythmDBEntry *entry);
RhythmDBEntry *		rb_play_order_get_playing_entry (RBPlayOrder *porder);

/* Private utility functions used by play order implementations */

RBShellPlayer *		rb_play_order_get_player	(RBPlayOrder *porder);
RBSource *		rb_play_order_get_source	(RBPlayOrder *porder);
RhythmDB *		rb_play_order_get_db		(RBPlayOrder *porder);
RhythmDBQueryModel *    rb_play_order_get_query_model   (RBPlayOrder *porder);
gboolean		rb_play_order_model_not_empty	(RBPlayOrder *porder);

void			rb_play_order_have_next_changed (RBPlayOrder *porder, gboolean have_next);
void			rb_play_order_have_previous_changed (RBPlayOrder *porder, gboolean have_previous);

gboolean		rb_play_order_player_is_playing	(RBPlayOrder *porder);

void			rb_play_order_check_if_empty (RBPlayOrder *porder, RhythmDBEntry *entry);

void			rb_play_order_ref_entry_swapped		(RhythmDBEntry *entry, RhythmDB *db);
void			rb_play_order_unref_entry_swapped	(RhythmDBEntry *entry, RhythmDB *db);

G_END_DECLS

#endif /* __RB_PLAY_ORDER_H */
