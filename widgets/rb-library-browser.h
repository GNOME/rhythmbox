/*
 *  arch-tag: Header for library browser widget
 *
 *  Copyright (C) 2006 James Livingston <jrl@ids.org.au>
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

#ifndef __RB_LIBRARY_BROWSER_H
#define __RB_LIBRARY_BROWSER_H

#include <gtk/gtkhbox.h>

#include "rhythmdb.h"
#include "rhythmdb-query-model.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_BROWSER         (rb_library_browser_get_type ())
#define RB_LIBRARY_BROWSER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowser))
#define RB_LIBRARY_BROWSER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserClass))
#define RB_IS_LIBRARY_BROWSER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_BROWSER))
#define RB_IS_LIBRARY_BROWSER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_BROWSER))
#define RB_LIBRARY_BROWSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserClass))

typedef struct
{
	GtkHBox parent;
} RBLibraryBrowser;

typedef struct
{
	GtkHBoxClass parent;

	void (*changed) (RBLibraryBrowser *entry);
} RBLibraryBrowserClass;

GType		rb_library_browser_get_type (void);
RBLibraryBrowser *	rb_library_browser_new      (RhythmDB *db);

void rb_library_browser_set_model (RBLibraryBrowser *widget, RhythmDBQueryModel *model);

gboolean rb_library_browser_reset (RBLibraryBrowser *widget);
GPtrArray* rb_library_browser_construct_query (RBLibraryBrowser *widget);
GList* rb_library_browser_get_property_views (RBLibraryBrowser *widget);

gboolean rb_library_browser_has_selection (RBLibraryBrowser *widget);
void rb_library_browser_set_selection (RBLibraryBrowser *widget, RhythmDBPropType type, GList *list);


G_END_DECLS

#endif /* __RB_LIBRARY_BROWSER_H */
