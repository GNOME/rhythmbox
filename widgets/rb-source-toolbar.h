/*
 *  Copyright (C) 2011 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_SOURCE_TOOLBAR_H
#define RB_SOURCE_TOOLBAR_H

#include <gtk/gtk.h>

#include <sources/rb-source.h>

G_BEGIN_DECLS

#define RB_TYPE_SOURCE_TOOLBAR         (rb_source_toolbar_get_type ())
#define RB_SOURCE_TOOLBAR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE_TOOLBAR, RBSourceToolbar))
#define RB_SOURCE_TOOLBAR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SOURCE_TOOLBAR, RBSourceToolbarClass))
#define RB_IS_SOURCE_TOOLBAR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE_TOOLBAR))
#define RB_IS_SOURCE_TOOLBAR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SOURCE_TOOLBAR))
#define RB_SOURCE_TOOLBAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE_TOOLBAR, RBSourceToolbarClass))

typedef struct _RBSourceToolbar RBSourceToolbar;
typedef struct _RBSourceToolbarClass RBSourceToolbarClass;
typedef struct _RBSourceToolbarPrivate RBSourceToolbarPrivate;

struct _RBSourceToolbar
{
	GtkGrid parent;

	RBSourceToolbarPrivate *priv;
};

struct _RBSourceToolbarClass
{
	GtkGridClass parent;
};

GType		rb_source_toolbar_get_type 		(void);

RBSourceToolbar *rb_source_toolbar_new 			(RBDisplayPage *page,
							 GtkAccelGroup *accel_group);

void		rb_source_toolbar_add_search_entry_menu	(RBSourceToolbar *toolbar,
							 GMenuModel *search_menu,
							 GAction *search_action);

void		rb_source_toolbar_add_search_entry	(RBSourceToolbar *toolbar,
							 const char *placeholder);

void		rb_source_toolbar_clear_search_entry	(RBSourceToolbar *toolbar);

G_END_DECLS

#endif  /* RB_SOURCE_TOOLBAR_H */
