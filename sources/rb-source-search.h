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

#ifndef __RB_SOURCE_SEARCH_H
#define __RB_SOURCE_SEARCH_H

#include <glib-object.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_SOURCE_SEARCH	(rb_source_search_get_type())
#define RB_SOURCE_SEARCH(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE_SEARCH, RBSourceSearch))
#define RB_SOURCE_SEARCH_CLASS(o) (G_TYPE_CHECK_CLASS_CAST ((o), RB_TYPE_SOURCE_SEARCH, RBSourceSearchClass))
#define RB_IS_SOURCE_SEARCH(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE_SEARCH))
#define RB_IS_SOURCE_SEARCH_CLASS(o)  (G_TYPE_CHECK_CLASS_TYPE ((o), RB_TYPE_SOURCE_SEARCH))
#define RB_SOURCE_SEARCH_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE_SEARCH, RBSourceSearchClass))

typedef struct _RBSourceSearch RBSourceSearch;
typedef struct _RBSourceSearchClass RBSourceSearchClass;

struct _RBSourceSearch
{
	GObject parent;
};

struct _RBSourceSearchClass
{
	GObjectClass parent_class;
	GHashTable *searches;

	/* virtual functions */
	gboolean       (*is_subset)	(RBSourceSearch *search,
					 const char *current,
					 const char *next);
	RhythmDBQuery *(*create_query)	(RBSourceSearch *search,
					 RhythmDB *db,
					 const char *search_text);
	char *         (*get_description)(RBSourceSearch *search);

};

GType		rb_source_search_get_type	(void);

RBSourceSearch *rb_source_search_get_by_name (const char *name);
void            rb_source_search_register (RBSourceSearch *search, const char *name);

gboolean 	rb_source_search_is_subset (RBSourceSearch *search,
					    const char *current,
					    const char *next);
RhythmDBQuery *	rb_source_search_create_query (RBSourceSearch *search,
					       RhythmDB *db,
					       const char *search_text);

char *		rb_source_search_get_description (RBSourceSearch *search);

void		rb_source_search_add_to_menu (GMenu *menu,
					      const char *action_namespace,
					      GAction *action,
					      const char *name);

/* for search implementations */
RhythmDBQuery *	_rb_source_search_create_simple_query (RBSourceSearch *search,
						       RhythmDB *db,
						       const char *search_text,
						       RhythmDBPropType search_prop);

G_END_DECLS

#endif	/* __RB_SOURCE_SEARCH_H */

