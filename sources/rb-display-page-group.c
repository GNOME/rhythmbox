/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew <jonathan@d14n.org>
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

#include "rb-display-page-group.h"
#include "rb-display-page-tree.h"

/**
 * SECTION:rbdisplaypagegroup
 * @short_description: Display page grouping
 *
 * Page groups define sections of the display page tree.  A page group
 * consists of an internal name, a display name, and a category.
 * The internal name can be used to locate a registered page group.
 * The category is used to sort the page groups.
 *
 * While #RBDisplayPageGroup is a subclass of #RBDisplayPage, by default page
 * groups are never selectable so they have no content.
 */

G_LOCK_DEFINE_STATIC (display_page_groups);

enum {
	PROP_0,
	PROP_ID,
	PROP_CATEGORY,
	PROP_LOADED
};

struct _RBDisplayPageGroupPrivate
{
	char *id;
	RBDisplayPageGroupCategory category;
	gboolean loaded;
};

G_DEFINE_TYPE (RBDisplayPageGroup, rb_display_page_group, RB_TYPE_DISPLAY_PAGE)

static GHashTable *display_page_groups_map;

/**
 * rb_display_page_group_add_core_groups:
 * @shell: the #RBShell
 * @page_model: the #RBDisplayPageModel
 *
 * Registers core page groups.
 */
void
rb_display_page_group_add_core_groups (GObject *shell, RBDisplayPageModel *page_model)
{
	RBDisplayPageGroup *group;

	group = rb_display_page_group_new (shell, "library", _("Library"), RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED);
	rb_display_page_model_add_page (page_model, RB_DISPLAY_PAGE (group), NULL);

	group = rb_display_page_group_new (shell, "stores", _("Stores"), RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED);
	rb_display_page_model_add_page (page_model, RB_DISPLAY_PAGE (group), NULL);

	group = rb_display_page_group_new (shell, "playlists", _("Playlists"), RB_DISPLAY_PAGE_GROUP_CATEGORY_PERSISTENT);
	rb_display_page_model_add_page (page_model, RB_DISPLAY_PAGE (group), NULL);

	group = rb_display_page_group_new (shell, "devices", _("Devices"), RB_DISPLAY_PAGE_GROUP_CATEGORY_REMOVABLE);
	rb_display_page_model_add_page (page_model, RB_DISPLAY_PAGE (group), NULL);
	rb_display_page_group_loaded (group);

	group = rb_display_page_group_new (shell, "shared", _("Shared"), RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT);
	rb_display_page_model_add_page (page_model, RB_DISPLAY_PAGE (group), NULL);
	rb_display_page_group_loaded (group);
}

/**
 * rb_display_page_group_get_by_id:
 * @id: name of page group to find
 *
 * Locates a page group by name.  If the page group has not been registered yet,
 * returns NULL instead.
 *
 * Return value: (transfer none): existing page group, or NULL.
 */
RBDisplayPageGroup *
rb_display_page_group_get_by_id (const char *id)
{
	RBDisplayPageGroup *group;

	group = NULL;

	G_LOCK (display_page_groups);
	if (display_page_groups_map) {
		group = g_hash_table_lookup (display_page_groups_map, id);
	}
	G_UNLOCK (display_page_groups);

	return group;
}

/**
 * rb_display_page_group_loaded:
 * @group: a #RBDisplayPageGroup
 *
 * Called when the page group is fully loaded, that is, all initial pages have
 * been added.
 */
void
rb_display_page_group_loaded (RBDisplayPageGroup *group)
{
	group->priv->loaded = TRUE;
	g_object_notify (G_OBJECT (group), "loaded");
}

/**
 * rb_display_page_group_new:
 * @shell: the #RBShell
 * @id: name of the page group (untranslated, used in code)
 * @name: display name of the page group (translated)
 * @category: category for the page group
 *
 * Creates a new page group object.  The group will be registered
 * before it is returned.
 *
 * Return value: new page group
 */
RBDisplayPageGroup *
rb_display_page_group_new (GObject *shell,
			   const char *id,
			   const char *name,
			   RBDisplayPageGroupCategory category)
{
	return g_object_new (RB_TYPE_DISPLAY_PAGE_GROUP,
			     "shell", shell,
			     "id", id,
			     "name", name,
			     "category", category,
			     NULL);
}

static gboolean
impl_selectable (RBDisplayPage *page)
{
	return FALSE;
}

static void
impl_activate (RBDisplayPage *page)
{
	RBDisplayPageTree *display_page_tree;
	RBShell *shell;

	g_object_get (page, "shell", &shell, NULL);
	g_object_get (shell, "display-page-tree", &display_page_tree, NULL);
	rb_display_page_tree_toggle_expanded (display_page_tree, page);
	g_object_unref (display_page_tree);
	g_object_unref (shell);
}

/**
 * RBDisplayPageGroupType:
 * @RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED: Fixed single instance sources (e.g., library)
 * @RB_DISPLAY_PAGE_GROUP_CATEGORY_PERSISTENT: Persistent multiple-instance sources (e.g. playlists)
 * @RB_DISPLAY_PAGE_GROUP_CATEGORY_REMOVABLE: Sources representing removable devices
 * @RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT: Transient sources (e.g. network shares)
 * @RB_DISPLAY_PAGE_GROUP_CATEGORY_TOOLS: Utility (ie non-source) pages
 *
 * Predefined categories of page group. The order they're defined here is the order they
 * appear in the page tree.
 */

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
rb_display_page_group_category_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED, "fixed"),
			ENUM_ENTRY (RB_DISPLAY_PAGE_GROUP_CATEGORY_PERSISTENT, "persistent"),
			ENUM_ENTRY (RB_DISPLAY_PAGE_GROUP_CATEGORY_REMOVABLE, "removable"),
			ENUM_ENTRY (RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT, "transient"),
			ENUM_ENTRY (RB_DISPLAY_PAGE_GROUP_CATEGORY_TOOLS, "tools"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBDisplayPageGroupType", values);
	}

	return etype;
}

static void
impl_finalize (GObject *object)
{
	RBDisplayPageGroup *group = RB_DISPLAY_PAGE_GROUP (object);

	g_free (group->priv->id);
	/* remove from group map?  can this ever happen? */

	G_OBJECT_CLASS (rb_display_page_group_parent_class)->finalize (object);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBDisplayPageGroup *group = RB_DISPLAY_PAGE_GROUP (object);
	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, group->priv->id);
		break;
	case PROP_CATEGORY:
		g_value_set_enum (value, group->priv->category);
		break;
	case PROP_LOADED:
		g_value_set_boolean (value, group->priv->loaded);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBDisplayPageGroup *group = RB_DISPLAY_PAGE_GROUP (object);
	switch (prop_id) {
	case PROP_ID:
		group->priv->id = g_value_dup_string (value);
		break;
	case PROP_CATEGORY:
		group->priv->category = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_constructed (GObject *object)
{
	RBDisplayPageGroup *group;

	RB_CHAIN_GOBJECT_METHOD (rb_display_page_group_parent_class, constructed, object);

	group = RB_DISPLAY_PAGE_GROUP (object);

	/* register the new group */
	G_LOCK (display_page_groups);
	g_assert (g_hash_table_lookup (display_page_groups_map, group->priv->id) == NULL);
	g_hash_table_insert (display_page_groups_map, g_strdup (group->priv->id), group);
	G_UNLOCK (display_page_groups);
}

static void
rb_display_page_group_init (RBDisplayPageGroup *group)
{
	group->priv = G_TYPE_INSTANCE_GET_PRIVATE (group,
						   RB_TYPE_DISPLAY_PAGE_GROUP,
						   RBDisplayPageGroupPrivate);
}

static void
rb_display_page_group_class_init (RBDisplayPageGroupClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	G_LOCK (display_page_groups);
	if (display_page_groups_map == NULL) {
		display_page_groups_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	}
	G_UNLOCK (display_page_groups);

	object_class->constructed = impl_constructed;
	object_class->finalize = impl_finalize;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	page_class->selectable = impl_selectable;
	page_class->activate = impl_activate;

	/**
	 * RBDisplayPageGroup:id:
	 *
	 * Internal (untranslated) name for the page group
	 */
	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_string ("id",
							      "identifier",
							      "identifier",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBDisplayPageGroup:category:
	 *
	 * Page group category that the group falls into
	 */
	g_object_class_install_property (object_class,
					 PROP_CATEGORY,
					 g_param_spec_enum ("category",
							    "category",
							    "page group category",
							    RB_TYPE_DISPLAY_PAGE_GROUP_CATEGORY,
							    RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBDisplayPageProperty:loaded:
	 *
	 * Set to %TRUE once the initial set of pages have been added to the group
	 */
	g_object_class_install_property (object_class,
					 PROP_LOADED,
					 g_param_spec_boolean ("loaded",
							       "loaded",
							       "Whether the group is loaded",
							       FALSE,
							       G_PARAM_READABLE));

	g_type_class_add_private (klass, sizeof (RBDisplayPageGroupPrivate));
}
