/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2008  Jonathan Matthew  <jonathan@d14n.org>
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

/**
 * SECTION:rb-source-search
 * @short_description: Base class for source search implementations
 *
 * These translate the text in the search entry box into a
 * RhythmDBQuery.  The basic implementation will return
 * a query like RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_SEARCH_MATCH,
 * text.  Simple variants can restrict the search to single
 * properties (artist, album, genre).  More complicated searches
 * could implement something like the Xesam User Query spec.
 *
 * The source header finds the search instance to use by looking
 * for the 'rb-source-search' data item on the active search
 * action.
 */

#include "config.h"

#include "rb-source-search.h"

static void	rb_source_search_class_init (RBSourceSearchClass *klass);
static void	rb_source_search_init (RBSourceSearch *search);

G_DEFINE_TYPE (RBSourceSearch, rb_source_search, G_TYPE_OBJECT)

#define RB_SOURCE_SEARCH_DATA_ITEM	"rb-source-search"

static gboolean
default_is_subset (RBSourceSearch *source, const char *current, const char *next)
{
	/* the most common searches will return a strict subset if the
	 * next search is the current search with a suffix.
	 */
	return (current != NULL && g_str_has_prefix (next, current));
}

static void
rb_source_search_class_init (RBSourceSearchClass *klass)
{
	klass->is_subset = default_is_subset;
}

static void
rb_source_search_init (RBSourceSearch *search)
{
	/* nothing */
}

/**
 * rb_source_search_is_subset:
 * @search: a #RBSourceSearch
 * @current: the current search text (or NULL if the current search was done with a different
 *    search implementation and so cannot be considered)
 * @next: the new search text
 *
 * Determines whether the new search text will result in a
 * subset of entries matched by the previous search.  This is
 * used to optimise the search query.
 *
 * Return value: TRUE iff the new search text will match a subset of those matched by the current search.
 */
gboolean
rb_source_search_is_subset (RBSourceSearch *search, const char *current, const char *next)
{
	RBSourceSearchClass *klass = RB_SOURCE_SEARCH_GET_CLASS (search);
	return klass->is_subset (search, current, next);
}

/**
 * rb_source_search_create_query:
 * @search: a #RBSourceSearch
 * @db: the #RhythmDB
 * @search_text: the search text
 *
 * Creates a #RhythmDBQuery from the user's search text.
 *
 * Return value: #RhythmDBQuery for the source to use
 */
RhythmDBQuery *
rb_source_search_create_query (RBSourceSearch *search, RhythmDB *db, const char *search_text)
{
	RBSourceSearchClass *klass = RB_SOURCE_SEARCH_GET_CLASS (search);
	g_assert (klass->create_query);
	return klass->create_query (search, db, search_text);
}

/**
 * _rb_source_search_create_simple_query:
 * @search: the #RBSourceSearch
 * @db: the #RhythmDB
 * @search_text: the search text such as RHYTHMDB_PROP_SEARCH_MATCH
 * @search_prop: the search property
 *
 * Creates a basic search query.
 *
 * Return value: the #RhythmDBQuery for the search text and property, or NULL
 *   if no search text is specified.
 */
RhythmDBQuery *
_rb_source_search_create_simple_query (RBSourceSearch *search, RhythmDB *db, const char *search_text, RhythmDBPropType search_prop)
{
	if (search_text[0] == '\0')
		return NULL;

	return rhythmdb_query_parse (db, 
				     RHYTHMDB_QUERY_PROP_LIKE,
				     search_prop,
				     search_text,
				     RHYTHMDB_QUERY_END);
}

/**
 * rb_source_search_action_attach:
 * @search: #RBSourceSearch to associate with the action
 * @action: UI action to associate the search with
 *
 * Attaches a #RBSourceSearch to a UI action so that
 * the search implementation will be used when the action is active.
 */
void
rb_source_search_action_attach (RBSourceSearch *search, GObject *action)
{
	g_object_set_data_full (action,
				RB_SOURCE_SEARCH_DATA_ITEM,
				g_object_ref (search),
				(GDestroyNotify) g_object_unref);
}

/**
 * rb_source_search_get_from_action:
 * @action: the action to find the #RBSourceSearch for
 *
 * Returns the #RBSourceSearch associated with the
 * specified UI action.
 *
 * Return value: associated #RBSourceSearch
 */
RBSourceSearch *
rb_source_search_get_from_action (GObject *action)
{
	gpointer data;
	data = g_object_get_data (action, RB_SOURCE_SEARCH_DATA_ITEM);
	return RB_SOURCE_SEARCH (data);
}

