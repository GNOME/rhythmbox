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
#include <gtk/gtktreeviewcolumn.h>
#include <gtk/gtkdnd.h>
#include <libgnomevfs/gnome-vfs-file-info.h>

#include "rhythmdb.h"
#include "rhythmdb-query-model.h"

G_BEGIN_DECLS

#define RB_TYPE_ENTRY_VIEW         (rb_entry_view_get_type ())
#define RB_ENTRY_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_ENTRY_VIEW, RBEntryView))
#define RB_ENTRY_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_ENTRY_VIEW, RBEntryViewClass))
#define RB_IS_ENTRY_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_ENTRY_VIEW))
#define RB_IS_ENTRY_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_ENTRY_VIEW))
#define RB_ENTRY_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_ENTRY_VIEW, RBEntryViewClass))

typedef enum {
	RB_ENTRY_VIEW_COL_TRACK_NUMBER,
	RB_ENTRY_VIEW_COL_TITLE,
	RB_ENTRY_VIEW_COL_ARTIST,
	RB_ENTRY_VIEW_COL_ALBUM,
	RB_ENTRY_VIEW_COL_GENRE,
	RB_ENTRY_VIEW_COL_DURATION,
	RB_ENTRY_VIEW_COL_QUALITY,
	RB_ENTRY_VIEW_COL_RATING,
	RB_ENTRY_VIEW_COL_PLAY_COUNT,
	RB_ENTRY_VIEW_COL_LAST_PLAYED,
} RBEntryViewColumn;

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
	void (*entry_deleted)		(RBEntryView *view, RhythmDBEntry *entry);

	void (*entry_selected)          (RBEntryView *view, RhythmDBEntry *entry);
	void (*entry_activated)         (RBEntryView *view, RhythmDBEntry *entry);
	void (*playing_entry_removed)   (RBEntryView *view);

	void (*changed)                (RBEntryView *view);
	void (*have_selection_changed) (RBEntryView *view, gboolean have_selection);
	void (*sort_order_changed)     (RBEntryView *view);

	void (*show_popup)             (RBEntryView *view);
} RBEntryViewClass;

GType		rb_entry_view_get_type			(void);

RBEntryView *	rb_entry_view_new			(RhythmDB *db, const char *sort_key,
							 gboolean drag_source, gboolean drag_dest);

void		rb_entry_view_append_column		(RBEntryView *view, RBEntryViewColumn coltype);

void		rb_entry_view_append_column_custom	(RBEntryView *view, GtkTreeViewColumn *column,
							 const char *title, const char *key,
							 GCompareDataFunc sort_func,
							 gpointer user_data);

void		rb_entry_view_set_columns_clickable	(RBEntryView *view, gboolean clickable);

void		rb_entry_view_set_model			(RBEntryView *view,
							 RhythmDBQueryModel *model);

gboolean	rb_entry_view_busy			(RBEntryView *view);

GnomeVFSFileSize rb_entry_view_get_total_size		(RBEntryView *view);
glong		rb_entry_view_get_duration		(RBEntryView *view);

void		rb_entry_view_set_playing		(RBEntryView *view,
							 gboolean playing);

void		rb_entry_view_set_playing_entry         (RBEntryView *view,
							 RhythmDBEntry *entry);
RhythmDBEntry *	rb_entry_view_get_playing_entry         (RBEntryView *view);

RhythmDBEntry *	rb_entry_view_get_first_entry		(RBEntryView *view);
RhythmDBEntry *	rb_entry_view_get_next_entry		(RBEntryView *view);
RhythmDBEntry *	rb_entry_view_get_previous_entry	(RBEntryView *view);

RhythmDBEntry *	rb_entry_view_get_next_from_entry	(RBEntryView *view,
							 RhythmDBEntry *entry);
RhythmDBEntry *	rb_entry_view_get_previous_from_entry	(RBEntryView *view,
							 RhythmDBEntry *entry);

gboolean	rb_entry_view_have_selection		(RBEntryView *view);
GList *		rb_entry_view_get_selected_entries	(RBEntryView *view);

void		rb_entry_view_select_all		(RBEntryView *view);
void		rb_entry_view_select_none		(RBEntryView *view);
void		rb_entry_view_select_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

guint		rb_entry_view_get_num_entries		(RBEntryView *view);

gboolean	rb_entry_view_get_entry_contained	(RBEntryView *view,
							 RhythmDBEntry *entry);

gboolean	rb_entry_view_get_entry_visible		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_scroll_to_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_enable_drag_source	(RBEntryView *view,
							 const GtkTargetEntry *targets,
							 int n_targets);
RhythmDBEntry *	rb_entry_view_get_random_entry		(RBEntryView *view);

const char *	rb_entry_view_get_sorting_type		(RBEntryView *view);

gboolean	rb_entry_view_poll_model		(RBEntryView *view);

void		rb_entry_view_freeze			(RBEntryView *view);
void		rb_entry_view_thaw			(RBEntryView *view);
G_END_DECLS

#endif /* __RB_ENTRY_VIEW_H */
