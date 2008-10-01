 /*
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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

#ifndef RHYTHMDB_QUERY_RESULTS_H
#define RHYTHMDB_QUERY_RESULTS_H

/*
 * Interface for objects that can handle query results from RhythmDB.
 */

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_QUERY_RESULTS		(rhythmdb_query_results_get_type ())
#define RHYTHMDB_QUERY_RESULTS(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RHYTHMDB_TYPE_QUERY_RESULTS, RhythmDBQueryResults))
#define RHYTHMDB_IS_QUERY_RESULTS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_QUERY_RESULTS))
#define RHYTHMDB_QUERY_RESULTS_GET_IFACE(obj)	(G_TYPE_INSTANCE_GET_INTERFACE ((obj), RHYTHMDB_TYPE_QUERY_RESULTS, RhythmDBQueryResultsIface))

typedef struct _RhythmDBQueryResults RhythmDBQueryResults;
typedef struct _RhythmDBQueryResultsIface RhythmDBQueryResultsIface;

struct _RhythmDBQueryResultsIface
{
	GTypeInterface g_iface;

	/* vtable */
	void	(*set_query)		(RhythmDBQueryResults *results,
				 	 GPtrArray *query);

	void	(*add_results)		(RhythmDBQueryResults *results,
				 	 GPtrArray *entries);

	void 	(*query_complete)	(RhythmDBQueryResults *results);
};

GType	rhythmdb_query_results_get_type	(void);

void	rhythmdb_query_results_set_query (RhythmDBQueryResults *results,
					  GPtrArray *query);

void	rhythmdb_query_results_add_results (RhythmDBQueryResults *results,
					    GPtrArray *entries);

void	rhythmdb_query_results_query_complete (RhythmDBQueryResults *results);

G_END_DECLS

#endif /* RHYTHMDB_QUERY_RESULTS_H */
