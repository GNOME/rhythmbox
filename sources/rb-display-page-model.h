/*
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#ifndef RB_DISPLAY_PAGE_MODEL_H
#define RB_DISPLAY_PAGE_MODEL_H

#include <gtk/gtk.h>

#include <sources/rb-display-page.h>

G_BEGIN_DECLS

#define RB_TYPE_DISPLAY_PAGE_MODEL		(rb_display_page_model_get_type ())
#define RB_DISPLAY_PAGE_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_DISPLAY_PAGE_MODEL, RBDisplayPageModel))
#define RB_DISPLAY_PAGE_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_DISPLAY_PAGE_MODEL, RBDisplayPageModelClass))
#define RB_IS_DISPLAY_PAGE_MODEL(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_DISPLAY_PAGE_MODEL))
#define RB_IS_DISPLAY_PAGE_MODEL_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_DISPLAY_PAGE_MODEL))
#define RB_DISPLAY_PAGE_MODEL_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_DISPLAY_PAGE_MODEL, RBDisplayPageModelClass))

typedef enum {
	RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING = 0,
	RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE,
	RB_DISPLAY_PAGE_MODEL_N_COLUMNS
} RBDisplayPageModelColumn;

GType rb_display_page_model_column_get_type (void);
#define RB_TYPE_DISPLAY_PAGE_MODEL_COLUMN (rb_display_page_model_column_get_type ())

typedef struct _RBDisplayPageModel RBDisplayPageModel;
typedef struct _RBDisplayPageModelClass RBDisplayPageModelClass;

struct _RBDisplayPageModel
{
	GtkTreeModelFilter parent;
};

struct _RBDisplayPageModelClass
{
	GtkTreeModelFilterClass parent_class;

	void (*drop_received) (RBDisplayPageModel *model,
			       RBDisplayPage *target,
			       GtkTreeViewDropPosition pos,
			       GtkSelectionData *data);
	void (*page_inserted) (RBDisplayPageModel *model,
			       RBDisplayPage *page,
			       GtkTreeIter *iter);
};

GType		rb_display_page_model_get_type	(void);

RBDisplayPageModel *rb_display_page_model_new		(void);

void		rb_display_page_model_set_playing_source (RBDisplayPageModel *page_model,
							  RBDisplayPage *source);

void		rb_display_page_model_add_page (RBDisplayPageModel *page_model,
						RBDisplayPage *page,
						RBDisplayPage *parent);
void		rb_display_page_model_remove_page (RBDisplayPageModel *page_model,
						   RBDisplayPage *page);
gboolean	rb_display_page_model_find_page (RBDisplayPageModel *page_model,
						 RBDisplayPage *page,
						 GtkTreeIter *iter);
gboolean	rb_display_page_model_find_page_full (RBDisplayPageModel *page_model,
						      RBDisplayPage *page,
						      GtkTreeIter *iter);

void		rb_display_page_model_set_dnd_targets (RBDisplayPageModel *page_model,
						       GtkTreeView *treeview);

G_END_DECLS

#endif /* RB_DISPLAY_PAGE_MODEL */
