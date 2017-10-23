/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_ENTRY_VIEW_H
#define __RB_ENTRY_VIEW_H

#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-query-model.h>

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
	RB_ENTRY_VIEW_COL_COMMENT,
	RB_ENTRY_VIEW_COL_DURATION,
	RB_ENTRY_VIEW_COL_QUALITY,
	RB_ENTRY_VIEW_COL_RATING,
	RB_ENTRY_VIEW_COL_PLAY_COUNT,
	RB_ENTRY_VIEW_COL_YEAR,
	RB_ENTRY_VIEW_COL_LAST_PLAYED,
	RB_ENTRY_VIEW_COL_FIRST_SEEN,
	RB_ENTRY_VIEW_COL_LAST_SEEN,
	RB_ENTRY_VIEW_COL_LOCATION,
	RB_ENTRY_VIEW_COL_BPM,
	RB_ENTRY_VIEW_COL_ERROR,
	RB_ENTRY_VIEW_COL_COMPOSER
} RBEntryViewColumn;

GType rb_entry_view_column_get_type (void);
#define RB_TYPE_ENTRY_VIEW_COLUMN	(rb_entry_view_column_get_type())

typedef enum {
	RB_ENTRY_VIEW_NOT_PLAYING,
	RB_ENTRY_VIEW_PLAYING,
	RB_ENTRY_VIEW_PAUSED
} RBEntryViewState;

GType rb_entry_view_state_get_type (void);
#define RB_TYPE_ENTRY_VIEW_STATE	(rb_entry_view_state_get_type())

typedef struct _RBEntryView RBEntryView;
typedef struct _RBEntryViewClass RBEntryViewClass;

typedef struct RBEntryViewPrivate RBEntryViewPrivate;

struct _RBEntryView
{
	GtkBox parent;

	RBEntryViewPrivate *priv;
};

struct _RBEntryViewClass
{
	GtkScrolledWindowClass parent;

	void (*entry_added)		(RBEntryView *view, RhythmDBEntry *entry);
	void (*entry_deleted)		(RBEntryView *view, RhythmDBEntry *entry);
	void (*entries_replaced)	(RBEntryView *view);

	void (*entry_activated)         (RBEntryView *view, RhythmDBEntry *entry);

	void (*have_selection_changed) (RBEntryView *view, gboolean have_selection);
	void (*selection_changed)       (RBEntryView *view);

	void (*show_popup)             (RBEntryView *view, gboolean over_entry);
};

GType		rb_entry_view_get_type			(void);

RBEntryView *	rb_entry_view_new			(RhythmDB *db,
                                                         GObject *shell_player,
                                                         gboolean is_drag_source,
							 gboolean is_drag_dest);

GtkTreeViewColumn *rb_entry_view_get_column		(RBEntryView *view,
							 RBEntryViewColumn coltype);

void		rb_entry_view_append_column		(RBEntryView *view,
							 RBEntryViewColumn coltype,
                                                         gboolean always_visible);

void		rb_entry_view_append_column_custom	(RBEntryView *view,
                                                         GtkTreeViewColumn *column,
							 const char *title,
                                                         const char *key,
							 GCompareDataFunc sort_func,
							 gpointer data,
							 GDestroyNotify data_destroy);

void		rb_entry_view_insert_column_custom	(RBEntryView *view,
                                                         GtkTreeViewColumn *column,
							 const char *title,
                                                         const char *key,
							 GCompareDataFunc sort_func,
							 gpointer data,
							 GDestroyNotify data_destroy,
							 gint position);

void		rb_entry_view_set_columns_clickable	(RBEntryView *view,
                                                         gboolean clickable);

void		rb_entry_view_set_model			(RBEntryView *view,
							 RhythmDBQueryModel *model);

void		rb_entry_view_set_state			(RBEntryView *view,
							 RBEntryViewState state);

gboolean	rb_entry_view_have_selection		(RBEntryView *view);
gboolean	rb_entry_view_have_complete_selection	(RBEntryView *view);
GList *		rb_entry_view_get_selected_entries	(RBEntryView *view);

void		rb_entry_view_select_all		(RBEntryView *view);
void		rb_entry_view_select_none		(RBEntryView *view);
void		rb_entry_view_select_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

gboolean	rb_entry_view_get_entry_contained	(RBEntryView *view,
							 RhythmDBEntry *entry);

gboolean	rb_entry_view_get_entry_visible		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_scroll_to_entry		(RBEntryView *view,
							 RhythmDBEntry *entry);

void		rb_entry_view_enable_drag_source	(RBEntryView *view,
							 const GtkTargetEntry *targets,
							 int n_targets);

void		rb_entry_view_get_sorting_order		(RBEntryView *view,
                                                         char       **column_name,
                                                         gint        *sort_order);
void		rb_entry_view_set_sorting_order		(RBEntryView *view,
                                                         const char  *column_name,
                                                         gint         sort_order);
/* deal with the sorting order as a composite string */
char *		rb_entry_view_get_sorting_type		(RBEntryView *view);
void		rb_entry_view_set_sorting_type		(RBEntryView *view,
                                                         const char  *sorttype);

void		rb_entry_view_set_fixed_column_width	(RBEntryView *view,
							 GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
							 const gchar **strings);
void		rb_entry_view_set_column_editable	(RBEntryView *view,
							 RBEntryViewColumn column,
							 gboolean editable);

const char *	rb_entry_view_get_time_date_column_sample (void);

/* resort the model with the current sorting order*/
void		rb_entry_view_resort_model		(RBEntryView *view);

void		rb_entry_view_set_status		(RBEntryView *view, const char *status, gboolean busy);

G_END_DECLS

#endif /* __RB_ENTRY_VIEW_H */
