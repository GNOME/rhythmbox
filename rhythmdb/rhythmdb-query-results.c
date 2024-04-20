/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <config.h>

#include "rhythmdb-query-results.h"

/**
 * SECTION:rhythmdbqueryresults
 * @short_description: interface for receiving query results from RhythmDB
 *
 * This is the interface that #RhythmDB uses to report results of database
 * queries.  When running a query, it first calls rhythmdb_query_results_set_query,
 * then passes entries matching the query to rhythmdb_query_results_add_results
 * in batches, and finally calls rhythmdb_query_results_query_complete.
 * There are no guarantees as to which threads the calls are made from.
 */

GType
rhythmdb_query_results_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RhythmDBQueryResultsIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			NULL,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RhythmDBQueryResults", &our_info, 0);
	}

	return our_type;
}

/**
 * rhythmdb_query_results_set_query: (skip)
 * @results: the #RhythmDBQueryResults implementation
 * @query: the new query
 *
 * When a new query is run, this method is invoked to give the
 * object implementing this interface a chance to take a copy of the
 * query criteria, so that it can evaluate the query for newly added
 * or changed entries once the query is complete.
 */
void
rhythmdb_query_results_set_query (RhythmDBQueryResults *results,
				  GPtrArray *query)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->set_query)
		iface->set_query (results, query);
}

/**
 * rhythmdb_query_results_add_results:
 * @results: the #RhythmDBQueryResults implementation
 * @entries: (element-type RB.RhythmDBEntry): #GPtrArray containing #RhythmDBEntry results
 *
 * Provides a new set of query results.  References must be taken on the
 * entries.
 */
void
rhythmdb_query_results_add_results (RhythmDBQueryResults *results,
				    GPtrArray *entries)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->add_results)
		iface->add_results (results, entries);
}

/**
 * rhythmdb_query_results_query_complete:
 * @results: the #RhythmDBQueryResults
 *
 * Called when the query is complete and all entries that match the query
 * have been supplied to rhythmdb_query_results_add_results.  If the object
 * implementing this interface needs to identify newly added or changed entries
 * that match the query, it needs to use the entry-added, entry-deleted and
 * entry-changed signals from #RhythmDB.
 */
void
rhythmdb_query_results_query_complete (RhythmDBQueryResults *results)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->query_complete)
		iface->query_complete (results);
}
