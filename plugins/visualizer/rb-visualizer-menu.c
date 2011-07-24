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

static void
set_check_item_foreach (GtkWidget *widget, GtkCheckMenuItem *item)
{
	GtkCheckMenuItem *check = GTK_CHECK_MENU_ITEM (widget);
	gtk_check_menu_item_set_active (check, check == item);
}

static void
quality_item_toggled_cb (GtkMenuItem *item, gpointer data)
{
	int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "quality"));
	GSettings *settings = g_object_get_data (G_OBJECT (item), "settings");

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)) == FALSE) {
		return;
	}

	rb_debug ("vis quality %d (%s) activated", index, rb_visualizer_quality[index].setting);
	g_settings_set_string (settings, "quality", rb_visualizer_quality[index].setting);

	g_signal_handlers_block_by_func (item, quality_item_toggled_cb, data);
	gtk_container_foreach (GTK_CONTAINER (data),
			       (GtkCallback) set_check_item_foreach,
			       GTK_CHECK_MENU_ITEM (item));
	g_signal_handlers_unblock_by_func (item, quality_item_toggled_cb, data);
}

static void
vis_plugin_item_activate_cb (GtkMenuItem *item, gpointer data)
{
	const char *name = g_object_get_data (G_OBJECT (item), "element-name");
	GSettings *settings = g_object_get_data (G_OBJECT (item), "settings");

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)) == FALSE) {
		return;
	}

	rb_debug ("vis element %s activated", name);
	g_settings_set_string (settings, "vis-plugin", name);

	g_signal_handlers_block_by_func (item, vis_plugin_item_activate_cb, data);
	gtk_container_foreach (GTK_CONTAINER (data),
			       (GtkCallback) set_check_item_foreach,
			       GTK_CHECK_MENU_ITEM (item));
	g_signal_handlers_unblock_by_func (item, vis_plugin_item_activate_cb, data);
}

static gboolean
vis_plugin_filter (GstPluginFeature *feature, gpointer data)
{
	GstElementFactory *f;

	if  (!GST_IS_ELEMENT_FACTORY (feature))
		return FALSE;
	f = GST_ELEMENT_FACTORY (feature);

	return (g_strrstr (gst_element_factory_get_klass (f), "Visualization") != NULL);
}

GtkWidget *
rb_visualizer_create_popup_menu (GtkToggleAction *fullscreen_action)
{
	GSettings *settings;
	GtkWidget *menu;
	GtkWidget *submenu;
	GtkWidget *item;
	GList *features;
	GList *t;
	char *active_element;
	int quality;
	int i;

	menu = gtk_menu_new ();

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.visualizer");

	/* fullscreen item */
	item = gtk_action_create_menu_item (GTK_ACTION (fullscreen_action));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* quality submenu */
	quality = g_settings_get_enum (settings, "quality");
	submenu = gtk_menu_new ();
	for (i = 0; i < G_N_ELEMENTS (rb_visualizer_quality); i++) {
		item = gtk_check_menu_item_new_with_label (rb_visualizer_quality[i].name);

		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), (i == quality));

		g_object_set_data (G_OBJECT (item), "quality", GINT_TO_POINTER (i));
		g_object_set_data (G_OBJECT (item), "settings", settings);
		g_signal_connect (item, "toggled", G_CALLBACK (quality_item_toggled_cb), submenu);
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);
	}

	item = gtk_menu_item_new_with_mnemonic (_("_Quality"));
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* effect submenu */
	submenu = gtk_menu_new ();

	rb_debug ("building vis plugin list");
	active_element = g_settings_get_string (settings, "vis-plugin");
	features = gst_registry_feature_filter (gst_registry_get_default (),
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

		item = gtk_check_menu_item_new_with_label (name);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
						g_strcmp0 (element_name, active_element) == 0);
		g_object_set_data (G_OBJECT (item), "element-name", g_strdup (element_name));
		g_object_set_data (G_OBJECT (item), "settings", settings);
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);
		g_signal_connect (item,
				  "activate",
				  G_CALLBACK (vis_plugin_item_activate_cb),
				  submenu);
	}
	gst_plugin_feature_list_free (features);

	item = gtk_menu_item_new_with_mnemonic (_("_Visual Effect"));
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	gtk_widget_show_all (menu);
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
