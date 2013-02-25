/*
 *  Copyright (C) 2008 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RB_SOURCE_SEARCH_BASIC_H
#define __RB_SOURCE_SEARCH_BASIC_H

/*
 * Basic source search implementation.
 */

#include <gtk/gtk.h>

#include <sources/rb-source-search.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_SOURCE_SEARCH_BASIC	(rb_source_search_basic_get_type())
#define RB_SOURCE_SEARCH_BASIC(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE_SEARCH_BASIC, RBSourceSearchBasic))
#define RB_SOURCE_SEARCH_BASIC_CLASS(o) (G_TYPE_CHECK_CLASS_CAST ((o), RB_TYPE_SOURCE_SEARCH_BASIC, RBSourceSearchBasicClass))
#define RB_IS_SOURCE_SEARCH_BASIC(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE_SEARCH_BASIC))
#define RB_IS_SOURCE_SEARCH_BASIC_CLASS(o)  (G_TYPE_CHECK_CLASS_TYPE ((o), RB_TYPE_SOURCE_SEARCH_BASIC))
#define RB_SOURCE_SEARCH_BASIC_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE_SEARCH_BASIC, RBSourceSearchBasicClass))

typedef struct _RBSourceSearchBasic RBSourceSearchBasic;
typedef struct _RBSourceSearchBasicClass RBSourceSearchBasicClass;

struct _RBSourceSearchBasic
{
	RBSourceSearch parent;
	RhythmDBPropType search_prop;
	char *description;
};

struct _RBSourceSearchBasicClass
{
	RBSourceSearchClass parent_class;
};

GType		rb_source_search_basic_get_type	(void);

RBSourceSearch *rb_source_search_basic_new (RhythmDBPropType prop, const char *description);
void		rb_source_search_basic_register (RhythmDBPropType prop, const char *name, const char *description);

GAction *	rb_source_create_search_action (RBSource *source);

void		rb_source_search_basic_add_to_menu (GMenu *menu,
						    const char *action_namespace,
						    GAction *search_action,
						    RhythmDBPropType prop,
						    const char *name,
						    const char *label);

G_END_DECLS

#endif	/* __RB_SOURCE_SEARCH_BASIC_H */

