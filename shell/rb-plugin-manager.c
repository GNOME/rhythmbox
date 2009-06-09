/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002 Paolo Maggi and James Willcox
 * Copyright (C) 2003-2005 Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n.h>

#include "rb-plugin-manager.h"
#include "rb-plugins-engine.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-builder-helpers.h"

enum
{
	ACTIVE_COLUMN,
	VISIBLE_COLUMN,
	INFO_COLUMN,
	N_COLUMNS
};

#define PLUGIN_MANAGER_NAME_TITLE _("Plugin")
#define PLUGIN_MANAGER_ACTIVE_TITLE _("Enabled")

#define RB_PLUGIN_MANAGER_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_PLUGIN_MANAGER, RBPluginManagerPrivate))

struct _RBPluginManagerPrivate
{
	GList		*plugins;
	GtkWidget	*tree;
	GtkTreeModel	*plugin_model;

	GtkWidget	*configure_button;
	GtkWidget	*site_label;
	GtkWidget	*copyright_label;
	GtkWidget	*authors_label;
	GtkWidget	*description_label;
	GtkWidget	*header_hbox;
	GtkWidget	*plugin_icon;
	GtkWidget	*site_text;
	GtkWidget	*copyright_text;
	GtkWidget	*authors_text;
	GtkWidget	*description_text;
	GtkWidget	*plugin_title;
};

G_DEFINE_TYPE(RBPluginManager, rb_plugin_manager, GTK_TYPE_VBOX)

static void rb_plugin_manager_finalize (GObject *o);
static RBPluginInfo *plugin_manager_get_selected_plugin (RBPluginManager *pm);
static void plugin_manager_toggle_active (GtkTreeIter *iter, GtkTreeModel *model, RBPluginManager *pm);
static void plugin_manager_toggle_all (RBPluginManager *pm);

static void
rb_plugin_manager_class_init (RBPluginManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_plugin_manager_finalize;

	g_type_class_add_private (object_class, sizeof (RBPluginManagerPrivate));
}

static void
configure_button_cb (GtkWidget          *button,
		     RBPluginManager *pm)
{
	RBPluginInfo *info;
	GtkWindow *toplevel;

	info = plugin_manager_get_selected_plugin (pm);

	g_return_if_fail (info != NULL);

	rb_debug ("Configuring: %s", rb_plugins_engine_get_plugin_name (info));

	toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (pm)));

	rb_plugins_engine_configure_plugin (info, toplevel);

	rb_debug ("Done configuring plugin");
}

static void
plugin_manager_view_cell_cb (GtkTreeViewColumn *tree_column,
			     GtkCellRenderer   *cell,
			     GtkTreeModel      *tree_model,
			     GtkTreeIter       *iter,
			     gpointer           data)
{
	RBPluginInfo *info;

	g_return_if_fail (tree_model != NULL);
	g_return_if_fail (tree_column != NULL);

	gtk_tree_model_get (tree_model, iter, INFO_COLUMN, &info, -1);

	if (info == NULL)
		return;

	g_return_if_fail (rb_plugins_engine_get_plugin_name (info) != NULL);

	g_object_set (G_OBJECT (cell),
		      "text",
		      rb_plugins_engine_get_plugin_name (info),
		      NULL);
}

static void
active_toggled_cb (GtkCellRendererToggle *cell,
		   gchar                 *path_str,
		   RBPluginManager    *pm)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;

	path = gtk_tree_path_new_from_string (path_str);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (pm->priv->tree));
	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter (model, &iter, path))
		plugin_manager_toggle_active (&iter, model, pm);

	gtk_tree_path_free (path);
}

static void
cursor_changed_cb (GtkTreeSelection *selection,
		   gpointer     data)
{
	RBPluginManager *pm = data;
	GtkTreeView *view;
	RBPluginInfo *info;
	char *string;
	GdkPixbuf *icon;
	const gchar **authors;

	view = gtk_tree_selection_get_tree_view (selection);
	info = plugin_manager_get_selected_plugin (pm);
	if (info == NULL) {
		return;
	}

	/* update info widgets */
	string = g_strdup_printf ("<span size=\"x-large\">%s</span>",
				  rb_plugins_engine_get_plugin_name (info));
	gtk_label_set_markup (GTK_LABEL (pm->priv->plugin_title), string);
	g_free (string);
	gtk_label_set_text (GTK_LABEL (pm->priv->description_text),
			    rb_plugins_engine_get_plugin_description (info));
	gtk_label_set_text (GTK_LABEL (pm->priv->copyright_text),
			    rb_plugins_engine_get_plugin_copyright (info));
	gtk_label_set_text (GTK_LABEL (pm->priv->site_text),
			    rb_plugins_engine_get_plugin_website (info));

	authors = rb_plugins_engine_get_plugin_authors (info);
	if (authors != NULL) {
		string = g_strjoinv ("\n", (gchar **)authors);
		gtk_label_set_text (GTK_LABEL (pm->priv->authors_text), string);
		g_free (string);
	} else {
		gtk_label_set_text (GTK_LABEL (pm->priv->authors_text), NULL);
	}

	icon = rb_plugins_engine_get_plugin_icon (info);
	if (icon != NULL) {
		/* rescale icon to fit header if needed */
		GdkPixbuf *icon_scaled;
		gint width, height, header_height;

		width = gdk_pixbuf_get_width (icon);
		height = gdk_pixbuf_get_height (icon);
		header_height = pm->priv->header_hbox->allocation.height;
		if (height > header_height) {
			icon_scaled = gdk_pixbuf_scale_simple (icon,
							       (gfloat)width/height*header_height, header_height,
							       GDK_INTERP_BILINEAR);
			gtk_image_set_from_pixbuf (GTK_IMAGE (pm->priv->plugin_icon), icon_scaled);
			g_object_unref (G_OBJECT (icon_scaled));
		} else {
			gtk_image_set_from_pixbuf (GTK_IMAGE (pm->priv->plugin_icon), icon);
		}
	} else {
		gtk_image_set_from_pixbuf (GTK_IMAGE (pm->priv->plugin_icon), NULL);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (pm->priv->configure_button),
				  (info != NULL) &&
				   rb_plugins_engine_plugin_is_configurable (info));
}

static void
row_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  gpointer           data)
{
	RBPluginManager *pm = data;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean found;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (pm->priv->tree));
	g_return_if_fail (model != NULL);

	found = gtk_tree_model_get_iter (model, &iter, path);
	g_return_if_fail (found);

	plugin_manager_toggle_active (&iter, model, pm);
}

static void
column_clicked_cb (GtkTreeViewColumn *tree_column,
		   gpointer           data)
{
	RBPluginManager *pm = RB_PLUGIN_MANAGER (data);

	plugin_manager_toggle_all (pm);
}

static void
plugin_manager_populate_lists (RBPluginManager *pm)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GList *p;

	for (p = pm->priv->plugins; p != NULL; p = g_list_next (p)) {
		RBPluginInfo *info;
		info = (RBPluginInfo *)p->data;

		gtk_list_store_append (GTK_LIST_STORE (pm->priv->plugin_model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (pm->priv->plugin_model), &iter,
				    ACTIVE_COLUMN, rb_plugins_engine_plugin_is_active (info),
				    VISIBLE_COLUMN, rb_plugins_engine_plugin_is_visible (info),
				    INFO_COLUMN, info,
				    -1);
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (pm->priv->tree));
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter)) {
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pm->priv->tree));
		g_return_if_fail (selection != NULL);

		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
plugin_manager_set_active (GtkTreeIter  *iter,
			   GtkTreeModel *model,
			   gboolean      active,
			   RBPluginManager *pm)
{
	RBPluginInfo *info;
	GtkTreeIter child_iter;

	gtk_tree_model_get (model, iter, INFO_COLUMN, &info, -1);

	g_return_if_fail (info != NULL);

	if (active) {
		/* activate the plugin */
		if (!rb_plugins_engine_activate_plugin (info)) {
			rb_debug ("Could not activate %s.", rb_plugins_engine_get_plugin_name (info));
			active ^= 1;
		}
	} else {
		/* deactivate the plugin */
		if (!rb_plugins_engine_deactivate_plugin (info)) {
			rb_debug ("Could not deactivate %s.", rb_plugins_engine_get_plugin_name (info));
			active ^= 1;
		}
	}

	/* set new value */
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							  &child_iter, iter);
	gtk_list_store_set (GTK_LIST_STORE (pm->priv->plugin_model),
			    &child_iter,
			    ACTIVE_COLUMN,
			    rb_plugins_engine_plugin_is_active (info),
			    -1);

	/* cause the configure button sensitivity to be updated */
	cursor_changed_cb (gtk_tree_view_get_selection (GTK_TREE_VIEW (pm->priv->tree)), pm);
}

static void
plugin_manager_toggle_active (GtkTreeIter  *iter,
			      GtkTreeModel *model,
			      RBPluginManager *pm)
{
	gboolean active, visible;

	gtk_tree_model_get (model, iter,
			    ACTIVE_COLUMN, &active,
			    VISIBLE_COLUMN, &visible,
			    -1);

	if (visible) {
		active ^= 1;
		plugin_manager_set_active (iter, model, active, pm);
	}
}

static RBPluginInfo *
plugin_manager_get_selected_plugin (RBPluginManager *pm)
{
	RBPluginInfo *info = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (pm->priv->tree));
	g_return_val_if_fail (model != NULL, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pm->priv->tree));
	g_return_val_if_fail (selection != NULL, NULL);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_tree_model_get (model, &iter, INFO_COLUMN, &info, -1);
	}

	return info;
}

static void
plugin_manager_toggle_all (RBPluginManager *pm)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	static gboolean active;

	active ^= 1;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (pm->priv->tree));

	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			plugin_manager_set_active (&iter, model, active, pm);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
}

/* Callback used as the interactive search comparison function */
static gboolean
name_search_cb (GtkTreeModel *model,
		gint          column,
		const gchar  *key,
		GtkTreeIter  *iter,
		gpointer      data)
{
	RBPluginInfo *info;
	gchar *normalized_string;
	gchar *normalized_key;
	gchar *case_normalized_string;
	gchar *case_normalized_key;
	gint key_len;
	gboolean retval;

	gtk_tree_model_get (model, iter, INFO_COLUMN, &info, -1);
	if (!info)
		return FALSE;

	normalized_string = g_utf8_normalize (rb_plugins_engine_get_plugin_name (info), -1, G_NORMALIZE_ALL);
	normalized_key = g_utf8_normalize (key, -1, G_NORMALIZE_ALL);
	case_normalized_string = g_utf8_casefold (normalized_string, -1);
	case_normalized_key = g_utf8_casefold (normalized_key, -1);

	key_len = strlen (case_normalized_key);

	/* Oddly enough, this callback must return whether to stop the search
	 * because we found a match, not whether we actually matched.
	 */
	retval = (strncmp (case_normalized_key, case_normalized_string, key_len) != 0);

	g_free (normalized_key);
	g_free (normalized_string);
	g_free (case_normalized_key);
	g_free (case_normalized_string);

	return retval;
}

static void
plugin_manager_construct_tree (RBPluginManager *pm)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GtkTreeModel *filter;
	GtkTreeSelection *selection;

	pm->priv->plugin_model = GTK_TREE_MODEL (gtk_list_store_new (N_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_POINTER));
	filter = gtk_tree_model_filter_new (pm->priv->plugin_model, NULL);
	gtk_tree_model_filter_set_visible_column (GTK_TREE_MODEL_FILTER (filter), VISIBLE_COLUMN);
	gtk_tree_view_set_model (GTK_TREE_VIEW (pm->priv->tree), filter);
	g_object_unref (filter);

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (pm->priv->tree), TRUE);
	gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW (pm->priv->tree), TRUE);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pm->priv->tree));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	/* first column */
	cell = gtk_cell_renderer_toggle_new ();
	g_signal_connect (cell,
			  "toggled",
			  G_CALLBACK (active_toggled_cb),
			  pm);
	column = gtk_tree_view_column_new_with_attributes (PLUGIN_MANAGER_ACTIVE_TITLE,
							   cell,
							  "active",
							   ACTIVE_COLUMN,
							   NULL);
	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (column_clicked_cb), pm);
	gtk_tree_view_append_column (GTK_TREE_VIEW (pm->priv->tree), column);

	/* second column */
	cell = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (PLUGIN_MANAGER_NAME_TITLE, cell, NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, cell, plugin_manager_view_cell_cb,
						 pm, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (pm->priv->tree), column);

	/* Enable search for our non-string column */
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (pm->priv->tree), INFO_COLUMN);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (pm->priv->tree),
					     name_search_cb,
					     NULL,
					     NULL);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cursor_changed_cb),
			  pm);
	g_signal_connect (pm->priv->tree,
			  "row_activated",
			  G_CALLBACK (row_activated_cb),
			  pm);

	gtk_widget_show (pm->priv->tree);
}

static int
plugin_name_cmp (gconstpointer a, gconstpointer b)
{
	RBPluginInfo *lhs = (RBPluginInfo*)a;
	RBPluginInfo *rhs = (RBPluginInfo*)b;
	return strcmp (rb_plugins_engine_get_plugin_name (lhs),
		       rb_plugins_engine_get_plugin_name (rhs));
}

static void
rb_plugin_manager_init (RBPluginManager *pm)
{
	GtkBuilder *builder;
	GtkWidget *plugins_window;

	pm->priv = RB_PLUGIN_MANAGER_GET_PRIVATE (pm);

	builder = rb_builder_load ("plugins.ui", pm);
	gtk_container_add (GTK_CONTAINER (pm), GTK_WIDGET (gtk_builder_get_object (builder, "edit_plugins")));

	gtk_box_set_spacing (GTK_BOX (pm), 6);

	pm->priv->tree = gtk_tree_view_new ();
        plugins_window = GTK_WIDGET (gtk_builder_get_object (builder, "plugins_list_window"));
	gtk_container_add (GTK_CONTAINER (plugins_window), pm->priv->tree);

	pm->priv->configure_button = GTK_WIDGET (gtk_builder_get_object (builder, "configure_button"));
	g_signal_connect (pm->priv->configure_button,
			  "clicked",
			  G_CALLBACK (configure_button_cb),
			  pm);

        pm->priv->header_hbox = GTK_WIDGET (gtk_builder_get_object (builder, "header_hbox"));

	pm->priv->plugin_title = GTK_WIDGET (gtk_builder_get_object (builder, "plugin_title"));

	pm->priv->site_label = GTK_WIDGET (gtk_builder_get_object (builder, "site_label"));
	rb_builder_boldify_label (builder, "site_label");
	pm->priv->copyright_label = GTK_WIDGET (gtk_builder_get_object (builder, "copyright_label"));
	rb_builder_boldify_label (builder, "copyright_label");
	pm->priv->authors_label = GTK_WIDGET (gtk_builder_get_object (builder, "authors_label"));
	rb_builder_boldify_label (builder, "authors_label");
	pm->priv->description_label = GTK_WIDGET (gtk_builder_get_object (builder, "description_label"));
	rb_builder_boldify_label (builder, "description_label");

	pm->priv->plugin_icon = GTK_WIDGET (gtk_builder_get_object (builder, "plugin_icon"));
	pm->priv->site_text = GTK_WIDGET (gtk_builder_get_object (builder, "site_text"));
	pm->priv->copyright_text = GTK_WIDGET (gtk_builder_get_object (builder, "copyright_text"));
	pm->priv->authors_text = GTK_WIDGET (gtk_builder_get_object (builder, "authors_text"));
	pm->priv->description_text = GTK_WIDGET (gtk_builder_get_object (builder, "description_text"));

	plugin_manager_construct_tree (pm);

	/* get the list of available plugins (or installed) */
	pm->priv->plugins = rb_plugins_engine_get_plugins_list ();
	pm->priv->plugins = g_list_sort (pm->priv->plugins, plugin_name_cmp);
	plugin_manager_populate_lists (pm);
	g_object_unref (builder);
}

GtkWidget *
rb_plugin_manager_new (void)
{
	return g_object_new (RB_TYPE_PLUGIN_MANAGER, 0);
}

static void
rb_plugin_manager_finalize (GObject *o)
{
	RBPluginManager *pm = RB_PLUGIN_MANAGER (o);

	g_list_free (pm->priv->plugins);

	G_OBJECT_CLASS(rb_plugin_manager_parent_class)->finalize (o);
}
