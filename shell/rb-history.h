/* 
 *  arch-tag: Header for Song History List
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

/**
 * RBHistory is a linked list that maintains a "current" pointer and can delete
 * an arbitrary element in O(1) time. It is also responsible for ref-ing and
 * unref-ing the entries it holds. However, I'm considering moving that
 * responsibility to whoever owns the RBHistory using a deletion callback. If
 * that happens, RBHistory will be a pure data structure and can probable lose
 * the GObject-ness.
 *
 * I may also add another "enqueued" pointer to help manage a queue of next
 * songs under shuffle.
 *
 * All operations take O(1) time unless noted otherwise.
 */

#include <glib/glist.h>
#include "rhythmdb.h"
#include "rb-shell-player.h"

#ifndef __RB_HISTORY_H
#define __RB_HISTORY_H

G_BEGIN_DECLS

#define RB_TYPE_HISTORY         (rb_history_get_type ())
#define RB_HISTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_HISTORY, RBHistory))
#define RB_HISTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_HISTORY, RBHistoryClass))
#define RB_IS_HISTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_HISTORY))
#define RB_IS_HISTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_HISTORY))
#define RB_HISTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_HISTORY, RBHistoryClass))

typedef struct RBHistoryPrivate RBHistoryPrivate;

typedef struct
{
	GObject parent;

	RBHistoryPrivate *priv;
} RBHistory;

typedef struct
{
	GObjectClass parent_class;

} RBHistoryClass;

GType                   rb_history_get_type	(void);

RBHistory *		rb_history_new		(gboolean truncate_on_play);

guint			rb_history_length	(RBHistory *hist);

RhythmDBEntry *		rb_history_first	(RBHistory *hist);
RhythmDBEntry *		rb_history_previous	(RBHistory *hist);
RhythmDBEntry *		rb_history_current	(RBHistory *hist);
RhythmDBEntry *		rb_history_next		(RBHistory *hist);
RhythmDBEntry *		rb_history_last		(RBHistory *hist);

/** These move around within the history but never go beyond the head or tail */
void			rb_history_go_first	(RBHistory *hist);
void			rb_history_go_previous	(RBHistory *hist);
void			rb_history_go_next	(RBHistory *hist);
void			rb_history_go_last	(RBHistory *hist);

/** 
 * Sets the song after "current" to @entry and, depending on the value of the
 * "truncate-on-play" property, may remove the entries after this.
 */
void			rb_history_set_playing	(RBHistory *hist, RhythmDBEntry *entry);

/**
 * Adds entry onto the end of the history list
 */
void			rb_history_append	(RBHistory *hist, RhythmDBEntry *entry);

/**
 * Gets the index of the current entry.
 * Takes O(index) time.
 */
guint			rb_history_get_current_index	(RBHistory *hist);

/**
 * Inserts @entry at @index within the history list. 0<=@index<=size
 * Takes O(@index) time.
 */
void			rb_history_insert_at_index	(RBHistory *hist, RhythmDBEntry *entry, guint index);

/**
 * If the entry is in the history, removes all instances of it. Unrefs the
 * entry and decrements the list size.
 */
void			rb_history_remove_entry	(RBHistory *hist, RhythmDBEntry *entry);

/** Empties the history list */
void			rb_history_clear	(RBHistory *hist);

/** Returns a copy of the whole history in order. Caller must free the result.
 * Takes O(size) time. */
GPtrArray *		rb_history_dump		(RBHistory *hist);

G_END_DECLS

#endif /* __RB_HISTORY_H */
