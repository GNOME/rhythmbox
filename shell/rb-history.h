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

#include <glib/glist.h>
#include "rhythmdb.h"

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

RBHistory *		rb_history_new		(RhythmDB *db);

RhythmDBEntry *		rb_history_forward	(RBHistory *hist);
RhythmDBEntry *		rb_history_back		(RBHistory *hist);

/** 
 * If entry != hist->current->data, replaces the section of the list after 
 * current with a link containing entry 
 */
void			rb_history_set_playing	(RBHistory *hist, RhythmDBEntry *entry);

/** 
 * I'm assuming this is only called when rb_history_back() failed, but the
 * code will still work if it's not.
 */
void			rb_history_prepend	(RBHistory *hist, RhythmDBEntry *entry);

/**
 * Adds entry onto the end of the history list
 */
void			rb_history_enqueue	(RBHistory *hist, RhythmDBEntry *entry);

/**
 * Makes the history empty
 */
void			rb_history_clear	(RBHistory *hist);

G_END_DECLS

#endif /* __RB_HISTORY_H */
