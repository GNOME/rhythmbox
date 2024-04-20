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
 * SECTION:rbsourcesearchbasic
 * @short_description: Simple implementation of #RBSourceSearch
 *
 * This implementation of #RBSourceSearch constructs queries that
 * search on a single #RhythmDBEntry property.  It's useful for
 * providing basic searches.
 */

#include "config.h"

#include "rb-shell.h"
#include "rb-source.h"
#include "rb-source-search-basic.h"
#include "rb-debug.h"

static void	rb_source_search_basic_class_init (RBSourceSearchBasicClass *klass);
static void	rb_source_search_basic_init (RBSourceSearchBasic *search);

G_DEFINE_TYPE (RBSourceSearchBasic, rb_source_search_basic, RB_TYPE_SOURCE_SEARCH)

enum
{
	PROP_0,
	PROP_SEARCH_PROP,
	PROP_DESCRIPTION
};

static RhythmDBQuery *
impl_create_query (RBSourceSearch *bsearch, RhythmDB *db, const char *search_text)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (bsearch);

	return _rb_source_search_create_simple_query (bsearch, db, search_text, search->search_prop);
}

static char *
impl_get_description (RBSourceSearch *bsearch)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (bsearch);
	return g_strdup (search->description);
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
	case PROP_DESCRIPTION:
		search->description = g_value_dup_string (value);
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
	case PROP_DESCRIPTION:
		g_value_set_string (value, search->description);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_finalize (GObject *object)
{
	RBSourceSearchBasic *search = RB_SOURCE_SEARCH_BASIC (object);

	g_free (search->description);

	G_OBJECT_CLASS (rb_source_search_basic_parent_class)->finalize (object);
}

static void
rb_source_search_basic_class_init (RBSourceSearchBasicClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceSearchClass *search_class = RB_SOURCE_SEARCH_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->finalize = impl_finalize;

	search_class->create_query = impl_create_query;
	search_class->get_description = impl_get_description;

	g_object_class_install_property (object_class,
					 PROP_SEARCH_PROP,
					 g_param_spec_int ("prop",
							   "propid",
							   "Property id",
							   0, RHYTHMDB_NUM_PROPERTIES,
							   RHYTHMDB_PROP_TYPE,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DESCRIPTION,
					 g_param_spec_string ("description",
							      "description",
							      "description",
							      NULL,
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
 * @description: description for the search
 *
 * Creates a new #RBSourceSearchBasic instance.
 * This performs simple string matching on a specified
 * property.
 *
 * Return value: newly created #RBSourceSearchBasic
 */
RBSourceSearch *
rb_source_search_basic_new (RhythmDBPropType prop, const char *description)
{
	return g_object_new (RB_TYPE_SOURCE_SEARCH_BASIC,
			     "prop", prop,
			     "description", description,
			     NULL);
}

/**
 * rb_source_search_basic_register:
 * @prop: the property to search on
 * @name: short non-translated name for the search instance
 * @description: user-visible description for the search
 *
 * Ensures that a search instance is registered with the specified name.
 */
void
rb_source_search_basic_register (RhythmDBPropType prop, const char *name, const char *description)
{
	RBSourceSearch *search;
	search = rb_source_search_get_by_name (name);
	if (search == NULL) {
		search = rb_source_search_basic_new (prop, description);
		rb_source_search_register (search, name);
	}
}

static void
action_activate_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	g_action_change_state (G_ACTION (action), parameter);
}

static void
action_change_state_cb (GSimpleAction *action, GVariant *parameter, GSettings *settings)
{
	const char *search_name;
	RBSourceSearch *search;

	search_name = g_variant_get_string (parameter, NULL);
	search = rb_source_search_get_by_name (search_name);
	if (search == NULL) {
		rb_debug ("tried to change search type to unknown value %s", search_name);
		return;
	}

	g_simple_action_set_state (action, parameter);

	if (settings != NULL) {
		g_settings_set_string (settings, "search-type", search_name);
	}
}

/**
 * rb_source_create_search_action:
 * @source: a #RBSource
 *
 * Creates a GAction representing the selected search type for @source.
 * The action is stateful. Its state is a string containing the name of
 * a registered search instance.  If the source has a settings instance,
 * it will be updated when the state changes.  Changes coming from the
 * settings instance are ignored.  If the source doesn't have a settings
 * instance, it should set a default state on the action at some point.
 *
 * Return value: (transfer full): #GAction instance
 */
GAction *
rb_source_create_search_action (RBSource *source)
{
	GAction *action;
	GSettings *settings;
	GVariant *state;
	char *action_name;

	g_object_get (source, "settings", &settings, NULL);

	action_name = g_strdup_printf ("source-search-%p", source);
	if (settings != NULL) {
		state = g_settings_get_value (settings, "search-type");
	} else {
		state = g_variant_new_string ("");
	}
	action = G_ACTION (g_simple_action_new_stateful (action_name, G_VARIANT_TYPE_STRING, state));
	g_free (action_name);

	g_signal_connect (action, "activate", G_CALLBACK (action_activate_cb), NULL);
	g_signal_connect (action, "change-state", G_CALLBACK (action_change_state_cb), settings);
	/* don't bother updating action state on settings changes */

	if (settings != NULL) {
		g_object_unref (settings);
	}
	return action;
}


/**
 * rb_source_search_basic_add_to_menu:
 * @menu: the #GMenu to populate
 * @action_namespace: action namespace to use for the action ("app" or "win")
 * @search_action: the search action to associate the search with
 * @prop: the property to search on
 * @name: short untranslated name for the search
 * @label: descriptive translatable label for the search
 *
 * Adds an item to @menu that will select a search based on the specified
 * property.  If there isn't already a registered search instance for the
 * property, one is created.
 */
void
rb_source_search_basic_add_to_menu (GMenu *menu, const char *action_namespace, GAction *search_action, RhythmDBPropType prop, const char *name, const char *label)
{
	rb_source_search_basic_register (prop, name, label);
	rb_source_search_add_to_menu (menu, action_namespace, search_action, name);
}
