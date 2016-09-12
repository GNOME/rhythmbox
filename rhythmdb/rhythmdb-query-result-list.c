/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "config.h"

#include "rhythmdb.h"
#include "rhythmdb-query-result-list.h"
#include "rb-debug.h"
#include "rb-util.h"

struct _RhythmDBQueryResultListPrivate
{
	gboolean complete;
	GList *results;
};

enum {
	COMPLETE,
	LAST_SIGNAL
};

static guint rhythmdb_query_result_list_signals[LAST_SIGNAL] = { 0 };

static void rhythmdb_query_result_list_query_results_init (RhythmDBQueryResultsIface *iface);

G_DEFINE_TYPE_WITH_CODE(RhythmDBQueryResultList, rhythmdb_query_result_list, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RHYTHMDB_TYPE_QUERY_RESULTS,
					      rhythmdb_query_result_list_query_results_init));


static void
impl_set_query (RhythmDBQueryResults *results, GPtrArray *query)
{
}

static void
impl_add_results (RhythmDBQueryResults *results, GPtrArray *entries)
{
	RhythmDBQueryResultList *list = RHYTHMDB_QUERY_RESULT_LIST (results);
	int i;

	for (i = 0; i < entries->len; i++) {
		RhythmDBEntry *entry;
		entry = g_ptr_array_index (entries, i);
		rhythmdb_entry_ref (entry);

		list->priv->results = g_list_prepend (list->priv->results, entry);
	}
}

static void
impl_query_complete (RhythmDBQueryResults *results)
{
	RhythmDBQueryResultList *list = RHYTHMDB_QUERY_RESULT_LIST (results);

	list->priv->results = g_list_reverse (list->priv->results);
	list->priv->complete = TRUE;

	g_signal_emit (G_OBJECT (results), rhythmdb_query_result_list_signals[COMPLETE], 0);
}

static void
impl_finalize (GObject *object)
{
	RhythmDBQueryResultList *list = RHYTHMDB_QUERY_RESULT_LIST (object);

	rb_list_destroy_free (list->priv->results, (GDestroyNotify)rhythmdb_entry_unref);

	G_OBJECT_CLASS (rhythmdb_query_result_list_parent_class)->finalize (object);
}

static void
rhythmdb_query_result_list_init (RhythmDBQueryResultList *list)
{
	list->priv = G_TYPE_INSTANCE_GET_PRIVATE (list,
						  RHYTHMDB_TYPE_QUERY_RESULT_LIST,
						  RhythmDBQueryResultListPrivate);
}

static void
rhythmdb_query_result_list_query_results_init (RhythmDBQueryResultsIface *iface)
{
	iface->set_query = impl_set_query;
	iface->add_results = impl_add_results;
	iface->query_complete = impl_query_complete;
}

static void
rhythmdb_query_result_list_class_init (RhythmDBQueryResultListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;

	/**
	 * RhythmDBQueryResultList::complete:
	 * @list: the #RhythmDBQueryResultList
	 *
	 * Emitted when the database query is complete.
	 */
	rhythmdb_query_result_list_signals[COMPLETE] =
		g_signal_new ("complete",
			      RHYTHMDB_TYPE_QUERY_RESULT_LIST,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryResultListClass, complete),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (RhythmDBQueryResultListPrivate));
}


/**
 * rhythmdb_query_result_list_new:
 *
 * Creates a new empty query result list.
 *
 * Return value: (transfer full): query result list
 */
RhythmDBQueryResultList *
rhythmdb_query_result_list_new (void)
{
	GObject *obj;
	obj = g_object_new (RHYTHMDB_TYPE_QUERY_RESULT_LIST, NULL);
	return RHYTHMDB_QUERY_RESULT_LIST (obj);
}

/**
 * rhythmdb_query_result_list_get_results:
 * @list: a #RhythmDBQueryResultList
 *
 * Returns the results from the query.
 *
 * Return value: (transfer none) (element-type RhythmDBEntry): list of results
 */
GList *
rhythmdb_query_result_list_get_results (RhythmDBQueryResultList *list)
{
	g_assert (list->priv->complete);
	return list->priv->results;
}
