/*
 *  Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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

#ifndef __RB_LIBRARY_BROWSER_H
#define __RB_LIBRARY_BROWSER_H

#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-query-model.h>
#include <widgets/rb-property-view.h>

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_BROWSER         (rb_library_browser_get_type ())
#define RB_LIBRARY_BROWSER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowser))
#define RB_LIBRARY_BROWSER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserClass))
#define RB_IS_LIBRARY_BROWSER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_BROWSER))
#define RB_IS_LIBRARY_BROWSER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_BROWSER))
#define RB_LIBRARY_BROWSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserClass))

typedef struct _RBLibraryBrowser RBLibraryBrowser;
typedef struct _RBLibraryBrowserClass RBLibraryBrowserClass;

struct _RBLibraryBrowser
{
	GtkBox parent;
};

struct _RBLibraryBrowserClass
{
	GtkBoxClass parent;
};

GType			rb_library_browser_get_type (void);
RBLibraryBrowser *	rb_library_browser_new      (RhythmDB *db,
						     RhythmDBEntryType *entry_type);

void 			rb_library_browser_set_model (RBLibraryBrowser *widget,
						      RhythmDBQueryModel *model,
						      gboolean query_pending);

gboolean 		rb_library_browser_reset (RBLibraryBrowser *widget);
RhythmDBQuery* 		rb_library_browser_construct_query (RBLibraryBrowser *widget);
GList* 			rb_library_browser_get_property_views (RBLibraryBrowser *widget);
RBPropertyView*		rb_library_browser_get_property_view (RBLibraryBrowser *widget,
							      RhythmDBPropType type);

gboolean 		rb_library_browser_has_selection (RBLibraryBrowser *widget);
void 			rb_library_browser_set_selection (RBLibraryBrowser *widget,
							  RhythmDBPropType type,
							  GList *selection);

G_END_DECLS

#endif /* __RB_LIBRARY_BROWSER_H */
