/*
 *  arch-tag: Header for widget to display RhythmDB entries
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_ENTRY_VIEW_H
#define __RB_ENTRY_VIEW_H

#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkdnd.h>

#include "rhythmdb.h"
#include "rhythmdb-query-model.h"

G_BEGIN_DECLS

#define RB_TYPE_ENTRY_VIEW         (rb_entry_view_get_type ())
#define RB_ENTRY_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_ENTRY_VIEW, RBEntryView))
#define RB_ENTRY_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_ENTRY_VIEW, RBEntryViewClass))
#define RB_IS_ENTRY_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_ENTRY_VIEW))
#define RB_IS_ENTRY_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_ENTRY_VIEW))
#define RB_ENTRY_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_ENTRY_VIEW, RBEntryViewClass))

typedef struct RBEntryViewPrivate RBEntryViewPrivate;

typedef struct
{
	GtkScrolledWindow parent;

	RBEntryViewPrivate *priv;
} RBEntryView;

typedef struct
{
	GtkScrolledWindowClass parent;

	void (*entry_added)		(RBEntryView *view, RhythmDBEntry *entry);

	void (*entry_selected)          (RBEntryView *view, RhythmDBEntry *entry);
	void (*entry_activated)         (RBEntryView *view, RhythmDBEntry *entry);
	void (*playing_entry_removed)   (RBEntryView *view);

	void (*changed)                (RBEntryView *view);
	void (*have_selection_changed) (RBEntryView *view, gboolean have_selection);

	void (*show_popup)             (RBEntryView *view);
} RBEntryViewClass;

GType		rb_entry_view_get_type			(void);

RBEntryView *	rb_entry_view_new			(RhythmDB *db,
							 const char *view_desc_file);

void		rb_entry_view_set_query_model		(RBEntryView *view,
							 RhythmDBQueryModel *model);

void		rb_entry_view_set_playing		(RBEntryView *view,
							 gboolean playing);

void		rb_entry_view_set_playing_entry         (RBEntryView *view,
							 RhythmDBEntry *entry);
RhythmDBEntry *	rb_entry_view_get_playing_entry         (RBEntryView *view);

RhythmDBEntry *	rb_entry_view_get_first_entry		(RBEntryView *view);
RhythmDBEntry *	rb_entry_view_get_next_entry		(RBEntryView *view);
RhythmDBEntry *	rb_entry_view_get_previous_entry	(RBEntryView *view);

gboolean	rb_entry_view_have_selection		(RBEntryView *view);
GList *		rb_entry_view_get_selected_entries	(RBEntryView *view);

void		rb_entry_view_select_all		(RBEntryView *view);
void		rb_entry_view_select_none		(RBEntryView *view);
void		rb_entry_view_select_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

gboolean	rb_entry_view_get_entry_visible		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_scroll_to_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_enable_drag_source	(RBEntryView *view,
							 const GtkTargetEntry *targets,
							 int n_targets);

void		rb_entry_view_freeze			(RBEntryView *view);
void		rb_entry_view_thaw			(RBEntryView *view);
G_END_DECLS

#endif /* __RB_ENTRY_VIEW_H */
