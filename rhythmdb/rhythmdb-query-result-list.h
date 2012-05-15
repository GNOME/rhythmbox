/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#include <glib-object.h>

#ifndef RHYTHMDB_QUERY_RESULT_LIST_H
#define RHYTHMDB_QUERY_RESULT_LIST_H

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_QUERY_RESULT_LIST         (rhythmdb_query_result_list_get_type ())
#define RHYTHMDB_QUERY_RESULT_LIST(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_QUERY_RESULT_LIST, RhythmDBQueryResultList))
#define RHYTHMDB_QUERY_RESULT_LIST_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE_QUERY_RESULT_LIST, RhythmDBQueryResultListClass))
#define RHYTHMDB_IS_QUERY_RESULT_LIST(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_QUERY_RESULT_LIST))
#define RHYTHMDB_IS_QUERY_RESULT_LIST_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_QUERY_RESULT_LIST))
#define RHYTHMDB_QUERY_RESULT_LIST_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_QUERY_RESULT_LIST, RhythmDBQueryResultListClass))

typedef struct _RhythmDBQueryResultList RhythmDBQueryResultList;
typedef struct _RhythmDBQueryResultListClass RhythmDBQueryResultListClass;
typedef struct _RhythmDBQueryResultListPrivate RhythmDBQueryResultListPrivate;

struct _RhythmDBQueryResultList
{
	GObject parent;
	RhythmDBQueryResultListPrivate *priv;
};

struct _RhythmDBQueryResultListClass
{
	GObjectClass parent;

	/* signals */
	void	(*complete)		(RhythmDBQueryResultList *list);
};

GType			rhythmdb_query_result_list_get_type		(void);

RhythmDBQueryResultList *rhythmdb_query_result_list_new 		(void);

GList 			*rhythmdb_query_result_list_get_results 	(RhythmDBQueryResultList *list);

G_END_DECLS

#endif /* RHYTHMDB_QUERY_RESULT_LIST_H */
