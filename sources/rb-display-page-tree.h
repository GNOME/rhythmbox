/*
 * Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_DISPLAY_PAGE_TREE_H
#define RB_DISPLAY_PAGE_TREE_H

#include <gtk/gtk.h>

#include <sources/rb-display-page.h>
#include <shell/rb-shell.h>

G_BEGIN_DECLS

#define RB_TYPE_DISPLAY_PAGE_TREE	      (rb_display_page_tree_get_type ())
#define RB_DISPLAY_PAGE_TREE(obj)	      (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_DISPLAY_PAGE_TREE, RBDisplayPageTree))
#define RB_DISPLAY_PAGE_TREE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_DISPLAY_PAGE_TREE, RBDisplayPageTreeClass))
#define RB_IS_DISPLAY_PAGE_TREE(obj)	      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_DISPLAY_PAGE_TREE))
#define RB_IS_DISPLAY_PAGE_TREE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_DISPLAY_PAGE_TREE))
#define RB_DISPLAY_PAGE_TREE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_DISPLAY_PAGE_TREE, RBDisplayPageTreeClass))

typedef struct _RBDisplayPageTree RBDisplayPageTree;
typedef struct _RBDisplayPageTreeClass RBDisplayPageTreeClass;
typedef struct _RBDisplayPageTreePrivate RBDisplayPageTreePrivate;

struct _RBDisplayPageTree
{
	GtkGrid    parent;

	RBDisplayPageTreePrivate *priv;
};

struct _RBDisplayPageTreeClass
{
	GtkGridClass parent_class;

	/* signals */
	void (*selected) (RBDisplayPageTree *tree, RBDisplayPage *page);
	void (*drop_received) (RBDisplayPageTree *tree, RBDisplayPage *page, GtkSelectionData *data);
};

GType		rb_display_page_tree_get_type		(void);

RBDisplayPageTree *rb_display_page_tree_new		(RBShell *shell);

void		rb_display_page_tree_edit_source_name	(RBDisplayPageTree *display_page_tree,
							 RBSource *source);

void		rb_display_page_tree_select		(RBDisplayPageTree *display_page_tree,
							 RBDisplayPage *page);

void		rb_display_page_tree_toggle_expanded	(RBDisplayPageTree *display_page_tree,
							 RBDisplayPage *page);

G_END_DECLS

#endif /* RB_DISPLAY_PAGE_TREE_H */
