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
 * SECTION:rbsourcesearch
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

#include "rb-source.h"
#include "rb-source-search.h"

static void	rb_source_search_class_init (RBSourceSearchClass *klass);
static void	rb_source_search_init (RBSourceSearch *search);

G_DEFINE_TYPE (RBSourceSearch, rb_source_search, G_TYPE_OBJECT)

#define RB_SOURCE_SEARCH_DATA_ITEM	"rb-source-search"

static gboolean
default_is_subset (RBSourceSearch *search, const char *current, const char *next)
{
	/* the most common searches will return a strict subset if the
	 * next search is the current search with a suffix.
	 */
	return (current != NULL && g_str_has_prefix (next, current));
}

static char *
default_get_description (RBSourceSearch *search)
{
	return g_strdup ("");
}

static void
rb_source_search_class_init (RBSourceSearchClass *klass)
{
	klass->searches = g_hash_table_new (g_str_hash, g_str_equal);

	klass->is_subset = default_is_subset;
	klass->get_description = default_get_description;
}

static void
rb_source_search_init (RBSourceSearch *search)
{
	/* nothing */
}

/**
 * rb_source_search_get_by_name:
 * @name: name to look up
 *
 * Finds the registered search instance with the specified name
 *
 * Returns: (transfer none): search instance, or NULL if not found
 */
RBSourceSearch *
rb_source_search_get_by_name (const char *name)
{
	RBSourceSearchClass *klass;
	klass = RB_SOURCE_SEARCH_CLASS (g_type_class_peek (RB_TYPE_SOURCE_SEARCH));
	return g_hash_table_lookup (klass->searches, name);
}

/**
 * rb_source_search_register:
 * @search: search instance to register
 * @name: name to register
 *
 * Registers a named search instance that can be used in menus and
 * search action states.
 */
void
rb_source_search_register (RBSourceSearch *search, const char *name)
{
	RBSourceSearchClass *klass;
	klass = RB_SOURCE_SEARCH_CLASS (g_type_class_peek (RB_TYPE_SOURCE_SEARCH));
	g_hash_table_insert (klass->searches, g_strdup (name), search);
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
 * rb_source_search_get_description:
 * @search: a #RBSourceSearch
 *
 * Returns a description of the search suitable for displaying in a menu
 *
 * Return value: description string
 */
char *
rb_source_search_get_description (RBSourceSearch *search)
{
	RBSourceSearchClass *klass = RB_SOURCE_SEARCH_GET_CLASS (search);
	return klass->get_description (search);
}

/**
 * rb_source_search_create_query:
 * @search: a #RBSourceSearch
 * @db: the #RhythmDB
 * @search_text: the search text
 *
 * Creates a #RhythmDBQuery from the user's search text.
 *
 * Return value: (transfer full): #RhythmDBQuery for the source to use
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
 * Return value: (transfer full): the #RhythmDBQuery for the search text and property, or NULL
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
 * rb_source_search_add_to_menu:
 * @menu: #GMenu instance to populate
 * @action_namespace: muxer namespace for the action ("app" or "win")
 * @action: search action to attach the menu item to
 * @name: name of the search instance to add
 *
 * Adds a registered search instance to a search menu.
 */
void
rb_source_search_add_to_menu (GMenu *menu, const char *action_namespace, GAction *action, const char *name)
{
	GMenuItem *item;
	RBSourceSearch *search;
	char *action_name;
       
	search = rb_source_search_get_by_name (name);
	g_assert (search != NULL);

	if (action_namespace != NULL) {
		action_name = g_strdup_printf ("%s.%s", action_namespace, g_action_get_name (action));
	} else {
		action_name = g_strdup (g_action_get_name (action));
	}

	item = g_menu_item_new (rb_source_search_get_description (search), NULL);
	g_menu_item_set_action_and_target (item, action_name, "s", name);
	g_menu_append_item (menu, item);

	g_free (action_name);
}
