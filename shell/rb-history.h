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

#include <glib.h>
#include <rhythmdb/rhythmdb.h>

#ifndef __RB_HISTORY_H
#define __RB_HISTORY_H

G_BEGIN_DECLS

#define RB_TYPE_HISTORY         (rb_history_get_type ())
#define RB_HISTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_HISTORY, RBHistory))
#define RB_HISTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_HISTORY, RBHistoryClass))
#define RB_IS_HISTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_HISTORY))
#define RB_IS_HISTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_HISTORY))
#define RB_HISTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_HISTORY, RBHistoryClass))

typedef struct _RBHistory RBHistory;
typedef struct _RBHistoryClass RBHistoryClass;

typedef struct RBHistoryPrivate RBHistoryPrivate;

struct _RBHistory
{
	GObject parent;

	RBHistoryPrivate *priv;
};

struct _RBHistoryClass
{
	GObjectClass parent_class;
};

GType                   rb_history_get_type	(void);

RBHistory *		rb_history_new		(gboolean truncate_on_play,
						 GFunc destroyer,
						 gpointer destroy_userdata);

void			rb_history_set_destroy_notify	(RBHistory *hist,
							 GFunc destroyer,
							 gpointer destroy_userdata);
void			rb_history_set_truncate_on_play	(RBHistory *hist, gboolean truncate_on_play);
void			rb_history_set_maximum_size	(RBHistory *hist, guint maximum_size);

guint			rb_history_length	(RBHistory *hist);

RhythmDBEntry *		rb_history_first	(RBHistory *hist);
RhythmDBEntry *		rb_history_previous	(RBHistory *hist);
RhythmDBEntry *		rb_history_current	(RBHistory *hist);
RhythmDBEntry *		rb_history_next		(RBHistory *hist);
RhythmDBEntry *		rb_history_last		(RBHistory *hist);

void			rb_history_go_first	(RBHistory *hist);
void			rb_history_go_previous	(RBHistory *hist);
void			rb_history_go_next	(RBHistory *hist);
void			rb_history_go_last	(RBHistory *hist);

void			rb_history_set_playing	(RBHistory *hist, RhythmDBEntry *entry);

void			rb_history_append	(RBHistory *hist, RhythmDBEntry *entry);

gint			rb_history_get_current_index	(RBHistory *hist);

void			rb_history_insert_at_index	(RBHistory *hist, RhythmDBEntry *entry, guint index);

void			rb_history_remove_entry	(RBHistory *hist, RhythmDBEntry *entry);

void			rb_history_clear	(RBHistory *hist);

GPtrArray *		rb_history_dump		(RBHistory *hist);

gboolean		rb_history_contains_entry	(RBHistory *hist, RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RB_HISTORY_H */
