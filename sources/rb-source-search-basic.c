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
 * SECTION:rb-source-search-basic
 * @short_description: Simple implementation of #RBSourceSearch
 *
 * This implementation of #RBSourceSearch constructs queries that
 * search on a single #RhythmDBEntry property.  It's useful for
 * providing basic searches.
 */

#include "config.h"

#include "rb-source-search-basic.h"

static void	rb_source_search_basic_class_init (RBSourceSearchBasicClass *klass);
static void	rb_source_search_basic_init (RBSourceSearchBasic *search);

G_DEFINE_TYPE (RBSourceSearchBasic, rb_source_search_basic, RB_TYPE_SOURCE_SEARCH)

enum
{
	PROP_0,
	PROP_SEARCH_PROP,
};

static RhythmDBQuery *
impl_create_query (RBSourceSearch *bsearch, RhythmDB *db, const char *search_text)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (bsearch);

	return _rb_source_search_create_simple_query (bsearch, db, search_text, search->search_prop);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (object);

	switch (prop_id) {
	case PROP_SEARCH_PROP:
		search->search_prop = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (object);

	switch (prop_id) {
	case PROP_SEARCH_PROP:
		g_value_set_int (value, search->search_prop);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_source_search_basic_class_init (RBSourceSearchBasicClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceSearchClass *search_class = RB_SOURCE_SEARCH_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	search_class->create_query = impl_create_query;

	g_object_class_install_property (object_class,
					 PROP_SEARCH_PROP,
					 g_param_spec_int ("prop",
							   "propid",
							   "Property id",
							   0, RHYTHMDB_NUM_PROPERTIES,
							   RHYTHMDB_PROP_TYPE,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_source_search_basic_init (RBSourceSearchBasic *search)
{
	/* nothing */
}


/**
 * rb_source_search_basic_new:
 * @prop:	the #RhythmDBPropType to search
 *
 * Creates a new #RBSourceSearchBasic instance.
 * This performs simple string matching on a specified
 * property.
 *
 * Return value: newly created #RBSourceSearchBasic
 */
RBSourceSearch *
rb_source_search_basic_new (RhythmDBPropType prop)
{
	return g_object_new (RB_TYPE_SOURCE_SEARCH_BASIC, "prop", prop, NULL);
}

/**
 * rb_source_search_basic_create_for_actions:
 * @action_group:	the GtkActionGroup containing the actions
 * @actions:		the GtkRadioActionEntries for the actions
 * @n_actions:		the number of actions
 *
 * Creates #RBSourceSearchBasic instances for a set of
 * search actions and associates them with the actions.
 * The property to match on is taken from the action
 * value in the GtkRadioActionEntry structure.
 */
void
rb_source_search_basic_create_for_actions (GtkActionGroup *action_group,
					   GtkRadioActionEntry *actions,
					   int n_actions)
{
	int i;
	for (i = 0; i < n_actions; i++) {
		GtkAction *action;
		RBSourceSearch *search;

		if (actions[i].value != RHYTHMDB_NUM_PROPERTIES) {
			action = gtk_action_group_get_action (action_group, actions[i].name);
			g_assert (action != NULL);

			search = rb_source_search_basic_new (actions[i].value);
			rb_source_search_action_attach (search, G_OBJECT (action));
			g_object_unref (search);
		}
	}
}

