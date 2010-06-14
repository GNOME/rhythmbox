/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  Jonathan Matthew <jonathan@kaolin.wh9.net>
 *  Copyright (C) 2006  William Jon McCann <mccann@jhu.edu>
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

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "rb-util.h"

#include "rb-source-group.h"

/**
 * SECTION:rb-source-group
 * @short_description: Source list grouping
 *
 * Source groups define sections of the source list.  A source group
 * consists of an internal name, a display name, and a category.
 * The internal name can be used to locate a registered source group.
 * The category is used to sort the source groups.
 */

G_LOCK_DEFINE_STATIC (source_groups);

static GHashTable *source_groups_map;

static RBSourceGroup *library_group = NULL;
static RBSourceGroup *stores_group = NULL;
static RBSourceGroup *playlists_group = NULL;
static RBSourceGroup *devices_group = NULL;
static RBSourceGroup *shared_group = NULL;

static void
register_core_source_groups (void)
{
	library_group = rb_source_group_register ("library", _("Library"), RB_SOURCE_GROUP_CATEGORY_FIXED);
	stores_group = rb_source_group_register ("stores", _("Stores"), RB_SOURCE_GROUP_CATEGORY_FIXED);
	playlists_group = rb_source_group_register ("playlists", _("Playlists"), RB_SOURCE_GROUP_CATEGORY_PERSISTENT);
	devices_group = rb_source_group_register ("devices", _("Devices"), RB_SOURCE_GROUP_CATEGORY_REMOVABLE);
	shared_group = rb_source_group_register ("shared", _("Shared"), RB_SOURCE_GROUP_CATEGORY_TRANSIENT);
}

/**
 * rb_source_group_init:
 *
 * Initializes source groups.
 */
void
rb_source_group_init (void)
{
	G_LOCK (source_groups);
	if (source_groups_map == NULL) {
		source_groups_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	}
	G_UNLOCK (source_groups);

	register_core_source_groups ();
}

GType
rb_source_group_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBSourceGroup",
						     (GBoxedCopyFunc)rb_copy_function,
						     (GBoxedFreeFunc)rb_null_function);
	}

	return type;
}

/**
 * RBSourcelistGroupType:
 * @RB_SOURCE_GROUP_CATEGORY_FIXED: Fixed single instance sources (e.g., library)
 * @RB_SOURCE_GROUP_CATEGORY_PERSISTENT: Persistent multiple-instance sources (e.g. playlists)
 * @RB_SOURCE_GROUP_CATEGORY_REMOVABLE: Sources representing removable devices
 * @RB_SOURCE_GROUP_CATEGORY_TRANSIENT: Transient sources (e.g. network shares)
 *
 * Predefined categories of source group. The order they're defined here is the order they
 * appear in the source list.
 */

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
rb_source_group_category_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_FIXED, "fixed"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_PERSISTENT, "persistent"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_REMOVABLE, "removable"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_TRANSIENT, "transient"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourcelistGroupType", values);
	}

	return etype;
}

/**
 * rb_source_group_get_by_name:
 * @name: name of source group to find
 *
 * Locates a source group by name.  If the source group has not been registered yet,
 * returns NULL instead.
 *
 * Return value: existing source group, or NULL.
 */
RBSourceGroup *
rb_source_group_get_by_name (const char *name)
{
	RBSourceGroup *group;

	group = NULL;

	G_LOCK (source_groups);
	if (source_groups_map) {
		group = g_hash_table_lookup (source_groups_map, name);
	}
	G_UNLOCK (source_groups);

	return group;
}

/**
 * rb_source_group_register:
 * @name: name of the source group (untranslated, used in code)
 * @display_name: display name of the source group (translated)
 * @category: category for the source group
 *
 * Registers and returns a new source group.  Registering a source group
 * that already exists will probably do bad things.
 *
 * Return value: new source group
 */
RBSourceGroup *
rb_source_group_register (const char           *name,
			  const char           *display_name,
			  RBSourceGroupCategory category)
{
	RBSourceGroup *group;

	if (name == NULL) {
		return NULL;
	}

	group = g_new0 (RBSourceGroup, 1);
	group->name = g_strdup (name);
	group->display_name = g_strdup (display_name);
	group->category = category;
	G_LOCK (source_groups);
	g_hash_table_insert (source_groups_map, g_strdup (group->name), group);
	G_UNLOCK (source_groups);

	return group;
}

/**
 * rb_source_group_library_get_type:
 *
 * Returns the predefined library source group
 *
 * Return value: library source group
 */
RBSourceGroup *
rb_source_group_library_get_type (void)
{
	return library_group;
}

/**
 * rb_source_group_playlists_get_type:
 *
 * Returns the predefined playlists source group
 *
 * Return value: playlists source group
 */
RBSourceGroup *
rb_source_group_playlists_get_type (void)
{
	return playlists_group;
}

/**
 * rb_source_group_devices_get_type:
 *
 * Returns the predefined devices source group
 *
 * Return value: devices source group
 */
RBSourceGroup *
rb_source_group_devices_get_type (void)
{
	return devices_group;
}

/**
 * rb_source_group_shared_get_type:
 *
 * Returns the predefined shared source group
 *
 * Return value: shared source group
 */
RBSourceGroup *
rb_source_group_shared_get_type (void)
{
	return shared_group;
}

/**
 * rb_source_group_stores_get_type:
 *
 * Returns the predefined stores source group
 *
 * Return value: stores source group
 */
RBSourceGroup *
rb_source_group_stores_get_type (void)
{
	return stores_group;
}
