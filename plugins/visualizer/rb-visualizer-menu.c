/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew <jonathan@d14n.org>
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
 */

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gst/gst.h>

#include "rb-visualizer-menu.h"
#include <lib/rb-debug.h>

const VisualizerQuality rb_visualizer_quality[] = {
	{ N_("Low quality"),	"low",	320,	240,	20, 	1 },
	{ N_("Normal quality"),	"medium", 640,	480,	25,	1 },
	{ N_("High quality"),	"high",	800,	600,	30,	1 }
};

static gboolean
vis_plugin_filter (GstPluginFeature *feature, gpointer data)
{
	GstElementFactory *f;

	if  (!GST_IS_ELEMENT_FACTORY (feature))
		return FALSE;
	f = GST_ELEMENT_FACTORY (feature);

	return (g_strrstr (gst_element_factory_get_klass (f), "Visualization") != NULL);
}

GMenu *
rb_visualizer_create_popup_menu (const char *fullscreen_action)
{
	GActionMap *map;
	GSettings *settings;
	GMenu *menu;
	GMenu *section;
	GMenu *submenu;
	GMenuItem *item;
	GList *features;
	GList *t;
	int i;

	menu = g_menu_new ();

	map = G_ACTION_MAP (g_application_get_default ());
	settings = g_settings_new ("org.gnome.rhythmbox.plugins.visualizer");
	g_action_map_add_action (map, g_settings_create_action (settings, "vis-quality"));
	g_action_map_add_action (map, g_settings_create_action (settings, "vis-plugin"));

	/* fullscreen item */
	section = g_menu_new ();
	g_menu_append (section, _("Fullscreen"), fullscreen_action);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));

	/* quality submenu */
	submenu = g_menu_new ();
	for (i = 0; i < G_N_ELEMENTS (rb_visualizer_quality); i++) {
		item = g_menu_item_new (_(rb_visualizer_quality[i].name), NULL);
		g_menu_item_set_action_and_target (item, "app.vis-quality", "i", i);
		g_menu_append_item (submenu, item);
	}

	g_menu_append_submenu (menu, _("Quality"), G_MENU_MODEL (submenu));

	/* effect submenu */
	submenu = g_menu_new ();

	rb_debug ("building vis plugin list");
	features = gst_registry_feature_filter (gst_registry_get (),
						vis_plugin_filter,
						FALSE, NULL);
	for (t = features; t != NULL; t = t->next) {
		GstPluginFeature *f;
		const char *name;
		const char *element_name;

		f = GST_PLUGIN_FEATURE (t->data);
		name = gst_element_factory_get_longname (GST_ELEMENT_FACTORY (f));
		element_name = gst_plugin_feature_get_name (f);
		rb_debug ("adding visualizer element %s (%s)", element_name, name);

		item = g_menu_item_new (name, NULL);
		g_menu_item_set_action_and_target (item, "app.vis-plugin", "s", element_name);
		g_menu_append_item (submenu, item);
	}
	gst_plugin_feature_list_free (features);

	g_menu_append_submenu (menu, _("Visual Effect"), G_MENU_MODEL (submenu));
	return menu;
}

int
rb_visualizer_menu_clip_quality (int value)
{
	if (value < 0) {
		return 0;
	} else if (value >= G_N_ELEMENTS (rb_visualizer_quality)) {
		return G_N_ELEMENTS (rb_visualizer_quality) - 1;
	} else {
		return value;
	}
}
