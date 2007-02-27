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

G_LOCK_DEFINE_STATIC (source_groups);

static GHashTable *source_groups_map;

static RBSourceGroup *library_group = NULL;
static RBSourceGroup *playlists_group = NULL;
static RBSourceGroup *devices_group = NULL;
static RBSourceGroup *shared_group = NULL;

static void
register_core_source_groups (void)
{
	library_group = rb_source_group_register ("library", _("Library"), RB_SOURCE_GROUP_CATEGORY_FIXED);
	playlists_group = rb_source_group_register ("playlists", _("Playlists"), RB_SOURCE_GROUP_CATEGORY_PERSISTENT);
	devices_group = rb_source_group_register ("devices", _("Devices"), RB_SOURCE_GROUP_CATEGORY_REMOVABLE);
	shared_group = rb_source_group_register ("shared", _("Shared"), RB_SOURCE_GROUP_CATEGORY_TRANSIENT);
}

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

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
rb_source_group_category_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_FIXED, "Fixed single instance source"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_PERSISTENT, "Persistent multiple-instance source"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_REMOVABLE, "Source representing a removable device"),
			ENUM_ENTRY (RB_SOURCE_GROUP_CATEGORY_TRANSIENT, "Transient source (eg. network shares)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourcelistGroupType", values);
	}

	return etype;
}

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

RBSourceGroup *
rb_source_group_library_get_type (void)
{
	return library_group;
}

RBSourceGroup *
rb_source_group_playlists_get_type (void)
{
	return playlists_group;
}

RBSourceGroup *
rb_source_group_devices_get_type (void)
{
	return devices_group;
}

RBSourceGroup *
rb_source_group_shared_get_type (void)
{
	return shared_group;
}
