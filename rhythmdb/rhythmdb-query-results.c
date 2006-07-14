/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include "rhythmdb-query-results.h"

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

void
rhythmdb_query_results_set_query (RhythmDBQueryResults *results,
				  GPtrArray *query)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->set_query)
		iface->set_query (results, query);
}

void
rhythmdb_query_results_add_results (RhythmDBQueryResults *results,
				    GPtrArray *entries)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->add_results)
		iface->add_results (results, entries);
}

void
rhythmdb_query_results_query_complete (RhythmDBQueryResults *results)
{
	RhythmDBQueryResultsIface *iface = RHYTHMDB_QUERY_RESULTS_GET_IFACE (results);
	if (iface->query_complete)
		iface->query_complete (results);
}
