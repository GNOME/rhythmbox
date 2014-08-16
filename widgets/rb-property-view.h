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

#ifndef __RB_PROPERTY_VIEW_H
#define __RB_PROPERTY_VIEW_H

#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-property-model.h>
#include <widgets/rb-entry-view.h>

G_BEGIN_DECLS

#define RB_TYPE_PROPERTY_VIEW         (rb_property_view_get_type ())
#define RB_PROPERTY_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyView))
#define RB_PROPERTY_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PROPERTY_VIEW, RBPropertyViewClass))
#define RB_IS_PROPERTY_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PROPERTY_VIEW))
#define RB_IS_PROPERTY_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PROPERTY_VIEW))
#define RB_PROPERTY_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyViewClass))

typedef struct _RBPropertyView RBPropertyView;
typedef struct _RBPropertyViewClass RBPropertyViewClass;

typedef struct RBPropertyViewPrivate RBPropertyViewPrivate;

struct _RBPropertyView
{
	GtkScrolledWindow parent;

	RBPropertyViewPrivate *priv;
};

struct _RBPropertyViewClass
{
	GtkScrolledWindowClass parent;

	void (*property_selected)	(RBPropertyView *view, const char *name);
	void (*properties_selected)	(RBPropertyView *view, GList *properties);
	void (*property_activated)	(RBPropertyView *view, const char *name);
	void (*selection_reset)		(RBPropertyView *view);
	void (*show_popup)		(RBPropertyView *view);
};

GType		rb_property_view_get_type		(void);

RBPropertyView *rb_property_view_new			(RhythmDB *db, guint propid,
							 const char *title);
void		rb_property_view_append_column_custom	(RBPropertyView *view,
							GtkTreeViewColumn *column);
void		rb_property_view_set_column_visible	(RBPropertyView *view,
							 gboolean visible);

void		rb_property_view_set_selection_mode	(RBPropertyView *view,
							 GtkSelectionMode mode);

void		rb_property_view_reset			(RBPropertyView *view);

void		rb_property_view_set_selection		(RBPropertyView *view,
							 const GList *vals);

GList *		rb_property_view_get_selection		(RBPropertyView *view);

RhythmDBPropertyModel * rb_property_view_get_model	(RBPropertyView *view);

void		rb_property_view_set_model		(RBPropertyView *view,
							 RhythmDBPropertyModel *model);

guint		rb_property_view_get_num_properties	(RBPropertyView *view);

void		rb_property_view_set_search_func	(RBPropertyView *view,
							 GtkTreeViewSearchEqualFunc func,
							 gpointer func_data,
							 GDestroyNotify notify);

G_END_DECLS

#endif /* __RB_PROPERTY_VIEW_H */
