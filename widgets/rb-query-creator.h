/*
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <rhythmdb/rhythmdb-query-model.h>

#ifndef __RB_QUERY_CREATOR_H
#define __RB_QUERY_CREATOR_H

G_BEGIN_DECLS

#define RB_TYPE_QUERY_CREATOR         (rb_query_creator_get_type ())
#define RB_QUERY_CREATOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_QUERY_CREATOR, RBQueryCreator))
#define RB_QUERY_CREATOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_QUERY_CREATOR, RBQueryCreatorClass))
#define RB_IS_QUERY_CREATOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_QUERY_CREATOR))
#define RB_IS_QUERY_CREATOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_QUERY_CREATOR))
#define RB_QUERY_CREATOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_QUERY_CREATOR, RBQueryCreatorClass))

typedef struct _RBQueryCreator RBQueryCreator;
typedef struct _RBQueryCreatorClass RBQueryCreatorClass;

struct _RBQueryCreator
{
	GtkDialog parent;
};

struct _RBQueryCreatorClass
{
	GtkDialogClass parent_class;
};

GType		rb_query_creator_get_type	(void);

GtkWidget *	rb_query_creator_new		(RhythmDB *db);

GtkWidget *	rb_query_creator_new_from_query	(RhythmDB *db, GPtrArray *query,
						 RhythmDBQueryModelLimitType limit_type, GVariant *limit_value,
						 const char *sort_column, gint sort_direction);

GPtrArray *	rb_query_creator_get_query	(RBQueryCreator *creator);

void		rb_query_creator_get_limit	(RBQueryCreator *creator,
						 RhythmDBQueryModelLimitType *type,
						 GVariant **limit);

void		rb_query_creator_get_sort_order (RBQueryCreator *creator, const char **sort_key, gint *sort_direction);

G_END_DECLS

#endif /* __RB_QUERY_CREATOR_H */
