/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 * Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <unistd.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "rb-display-page-group.h"
#include "rb-display-page-tree.h"
#include "rb-display-page-model.h"
#include "rb-debug.h"
#include "rb-cell-renderer-pixbuf.h"
#include "gossip-cell-renderer-expander.h"
#include "rb-tree-dnd.h"
#include "rb-util.h"
#include "rb-auto-playlist-source.h"
#include "rb-static-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rb-device-source.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"

/**
 * SECTION:rbdisplaypagetree
 * @short_description: widget displaying the tree of #RBDisplayPage instances
 *
 * The display page tree widget is a GtkTreeView backed by a GtkListStore
 * containing the display page instances (sources and other things).  Display
 * pages include sources, such as the library and playlists, and other things
 * like the visualization display.
 *
 * Display pages are shown in the list with an icon and the name.  The current
 * playing source is displayed in bold.
 *
 * Sources are divided into groups - library, stores, playlists, devices,
 * network shares.  Groups are displayed as headers, with expanders for hiding
 * and showing the sources in the group.  Sources themselves may also have
 * child sources, such as playlists on portable audio players.
 */

struct _RBDisplayPageTreePrivate
{
	GtkWidget *scrolled;
	GtkWidget *treeview;
	GtkCellRenderer *title_renderer;
	GtkCellRenderer *expander_renderer;

	GtkWidget *toolbar;
	GtkWidget *add_menubutton;

	RBDisplayPageModel *page_model;
	GtkTreeSelection *selection;

	int child_source_count;
	GtkTreeViewColumn *main_column;

	RBShell *shell;

	GList *expand_rows;
	GtkTreeRowReference *expand_select_row;
	guint expand_rows_id;

	GSimpleAction *remove_action;
	GSimpleAction *eject_action;

	GdkPixbuf *blank_pixbuf;
};


enum
{
	PROP_0,
	PROP_SHELL,
	PROP_MODEL
};

enum
{
	SELECTED,
	DROP_RECEIVED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBDisplayPageTree, rb_display_page_tree, GTK_TYPE_GRID)

static RBDisplayPage *
get_selected_page (RBDisplayPageTree *display_page_tree)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	RBDisplayPage *page;

	if (!gtk_tree_selection_get_selected (display_page_tree->priv->selection, &model, &iter))
		return NULL;

	gtk_tree_model_get (model,
			    &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	return page;
}

static void
heading_cell_data_func (GtkTreeViewColumn *tree_column,
			GtkCellRenderer *cell,
			GtkTreeModel *model,
			GtkTreeIter *iter,
			RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);


	if (RB_IS_DISPLAY_PAGE_GROUP (page)) {
		char *name;
		g_object_get (page, "name", &name, NULL);
		g_object_set (cell,
			      "text", name,
			      "visible", TRUE,
			      NULL);
		g_free (name);
	} else {
		g_object_set (cell,
			      "text", NULL,
			      "visible", FALSE,
			      NULL);
	}

	g_object_unref (page);
}

static void
padding_cell_data_func (GtkTreeViewColumn *tree_column,
			GtkCellRenderer *cell,
			GtkTreeModel *model,
			GtkTreeIter *iter,
			RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (RB_IS_DISPLAY_PAGE_GROUP (page)) {
		g_object_set (cell,
			      "text", NULL,
			      "visible", FALSE,
			      "xpad", 0,
			      "ypad", 0,
			      NULL);
	} else {
		g_object_set (cell,
			      "text", NULL,
			      "visible", TRUE,
			      "xpad", 3,
			      "ypad", 3,
			      NULL);
	}

	g_object_unref (page);
}

static void
padding2_cell_data_func (GtkTreeViewColumn *tree_column,
			 GtkCellRenderer *cell,
			 GtkTreeModel *model,
			 GtkTreeIter *iter,
			 RBDisplayPageTree *display_page_tree)
{
	GtkTreePath *path;

	path = gtk_tree_model_get_path (model, iter);
	if (gtk_tree_path_get_depth (path) > 2) {
		g_object_set (cell,
			      "text", NULL,
			      "visible", TRUE,
			      "xpad", 3,
			      "ypad", 0,
			      NULL);
	} else {
		g_object_set (cell,
			      "text", NULL,
			      "visible", FALSE,
			      "xpad", 0,
			      "ypad", 0,
			      NULL);
	}
	gtk_tree_path_free (path);
}

static void
pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;
	GtkTreePath *path;
	GIcon *icon = NULL;

	path = gtk_tree_model_get_path (model, iter);
	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);

	switch (gtk_tree_path_get_depth (path)) {
	case 1:
		g_object_set (cell, "visible", FALSE, NULL);
		break;

	case 2:
	case 3:
		g_object_get (page, "icon", &icon, NULL);
		if (icon == NULL) {
			g_object_set (cell, "visible", TRUE, "pixbuf", display_page_tree->priv->blank_pixbuf, NULL);
		} else {
			g_object_set (cell, "visible", TRUE, "gicon", icon, NULL);
			g_object_unref (icon);
		}
		break;

	default:
		g_object_set (cell, "visible", TRUE, "pixbuf", display_page_tree->priv->blank_pixbuf, NULL);
		break;
	}

	gtk_tree_path_free (path);
	g_object_unref (page);
}

static void
title_cell_data_func (GtkTreeViewColumn *column,
		      GtkCellRenderer   *renderer,
		      GtkTreeModel      *tree_model,
		      GtkTreeIter       *iter,
		      RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;
	gboolean playing;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, &playing,
			    -1);

	if (RB_IS_DISPLAY_PAGE_GROUP (page)) {
		g_object_set (renderer, "visible", FALSE, "text", NULL, NULL);
	} else {
		char *name;
		g_object_get (page, "name", &name, NULL);

		g_object_set (renderer,
			      "visible", TRUE,
			      "text", name,
			      "weight", playing ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
			      NULL);
		g_free (name);
	}

	g_object_unref (page);
}

static void
expander_cell_data_func (GtkTreeViewColumn *column,
			 GtkCellRenderer   *cell,
			 GtkTreeModel      *model,
			 GtkTreeIter       *iter,
			 RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);

	if (RB_IS_DISPLAY_PAGE_GROUP (page) || gtk_tree_model_iter_has_child (model, iter) == FALSE) {
		g_object_set (cell, "visible", FALSE, NULL);
	} else if (gtk_tree_model_iter_has_child (model, iter)) {
		GtkTreePath *path;
		gboolean     row_expanded;

		path = gtk_tree_model_get_path (model, iter);
		row_expanded = gtk_tree_view_row_expanded (GTK_TREE_VIEW (display_page_tree->priv->treeview),
							   path);
		gtk_tree_path_free (path);

		g_object_set (cell,
			      "visible", TRUE,
			      "expander-style", row_expanded ? GTK_EXPANDER_EXPANDED : GTK_EXPANDER_COLLAPSED,
			      NULL);
	}

	g_object_unref (page);
}

static void
row_activated_cb (GtkTreeView       *treeview,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  RBDisplayPageTree *display_page_tree)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;
	RBDisplayPage *page;

	model = gtk_tree_view_get_model (treeview);

	g_return_if_fail (gtk_tree_model_get_iter (model, &iter, path));

	gtk_tree_model_get (model, &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);

	if (page != NULL) {
		rb_debug ("page %p activated", page);
		rb_display_page_activate (page);
		g_object_unref (page);
	}
}

static void
drop_received_cb (RBDisplayPageModel     *model,
		  RBDisplayPage          *page,
		  GtkTreeViewDropPosition pos,
		  GtkSelectionData       *data,
		  RBDisplayPageTree      *display_page_tree)
{
	rb_debug ("drop received");
	g_signal_emit (display_page_tree, signals[DROP_RECEIVED], 0, page, data);
}

static gboolean
expand_rows_cb (RBDisplayPageTree *display_page_tree)
{
	GList *l;
	rb_debug ("expanding %d rows", g_list_length (display_page_tree->priv->expand_rows));
	display_page_tree->priv->expand_rows_id = 0;

	for (l = display_page_tree->priv->expand_rows; l != NULL; l = l->next) {
		GtkTreePath *path;
		path = gtk_tree_row_reference_get_path (l->data);
		if (path != NULL) {
			gtk_tree_view_expand_to_path (GTK_TREE_VIEW (display_page_tree->priv->treeview), path);
			if (l->data == display_page_tree->priv->expand_select_row) {
				GtkTreeSelection *selection;
				GtkTreeIter iter;

				selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (display_page_tree->priv->treeview));
				if (gtk_tree_model_get_iter (GTK_TREE_MODEL (display_page_tree->priv->page_model), &iter, path)) {
					rb_debug ("selecting one of the expanded rows");
					gtk_tree_selection_select_iter (selection, &iter);
				}
			}
			gtk_tree_path_free (path);
		}
	}

	rb_list_destroy_free (display_page_tree->priv->expand_rows, (GDestroyNotify) gtk_tree_row_reference_free);
	display_page_tree->priv->expand_rows = NULL;
	return FALSE;
}

static void
model_row_inserted_cb (GtkTreeModel *model,
		       GtkTreePath *path,
		       GtkTreeIter *iter,
		       RBDisplayPageTree *display_page_tree)
{
	display_page_tree->priv->expand_rows = g_list_append (display_page_tree->priv->expand_rows,
							      gtk_tree_row_reference_new (model, path));
	if (display_page_tree->priv->expand_rows_id == 0) {
		display_page_tree->priv->expand_rows_id = g_idle_add ((GSourceFunc)expand_rows_cb, display_page_tree);
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (display_page_tree->priv->treeview));
}

static gboolean
key_release_cb (GtkTreeView *treeview,
		GdkEventKey *event,
		RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;
	gboolean res;

	/* F2 = rename playlist */
	if (event->keyval != GDK_KEY_F2) {
		return FALSE;
	}

	page = get_selected_page (display_page_tree);
	if (page == NULL) {
		return FALSE;
	} else if (RB_IS_SOURCE (page) == FALSE) {
		g_object_unref (page);
		return FALSE;
	}

	res = FALSE;
	if (rb_source_can_rename (RB_SOURCE (page))) {
		rb_display_page_tree_edit_source_name (display_page_tree, RB_SOURCE (page));
		res = TRUE;
	}

	g_object_unref (page);
	return res;
}

/**
 * rb_display_page_tree_edit_source_name:
 * @display_page_tree: the #RBDisplayPageTree
 * @source: the #RBSource to edit
 *
 * Initiates editing of the name of the specified source.  The row for the source
 * is selected and given input focus, allowing the user to edit the name.
 * source_name_edited_cb is called when the user finishes editing.
 */
void
rb_display_page_tree_edit_source_name (RBDisplayPageTree *display_page_tree,
				       RBSource *source)
{
	GtkTreeIter  iter;
	GtkTreePath *path;

	g_assert (rb_display_page_model_find_page (display_page_tree->priv->page_model,
						   RB_DISPLAY_PAGE (source),
						   &iter));
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (display_page_tree->priv->page_model),
					&iter);
	gtk_tree_view_expand_to_path (GTK_TREE_VIEW (display_page_tree->priv->treeview), path);

	/* Make cell editable just for the moment.
	   We'll turn it off once editing is done. */
	g_object_set (display_page_tree->priv->title_renderer, "editable", TRUE, NULL);

	gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (display_page_tree->priv->treeview),
					  path, display_page_tree->priv->main_column,
					  display_page_tree->priv->title_renderer,
					  TRUE);
	gtk_tree_path_free (path);
}

/**
 * rb_display_page_tree_select:
 * @display_page_tree: the #RBDisplayPageTree
 * @page: the #RBDisplayPage to select
 *
 * Selects the specified page in the tree.  This will result in the 'selected'
 * signal being emitted.
 */
void
rb_display_page_tree_select (RBDisplayPageTree *display_page_tree,
			     RBDisplayPage *page)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GList *l;
	gboolean defer = FALSE;

	g_assert (rb_display_page_model_find_page (display_page_tree->priv->page_model,
						   page,
						   &iter));

	/* if this is a path we're trying to expand to, wait until we've done that first */
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (display_page_tree->priv->page_model), &iter);
	for (l = display_page_tree->priv->expand_rows; l != NULL; l = l->next) {
		GtkTreePath *expand_path;

		expand_path = gtk_tree_row_reference_get_path (l->data);
		if (expand_path != NULL) {
			defer = (gtk_tree_path_compare (expand_path, path) == 0);
			gtk_tree_path_free (expand_path);
		}

		if (defer) {
			display_page_tree->priv->expand_select_row = l->data;
			break;
		}
	}

	if (defer == FALSE) {
		gtk_tree_selection_select_iter (display_page_tree->priv->selection, &iter);
	}

	gtk_tree_path_free (path);
}

/**
 * rb_display_page_tree_toggle_expanded:
 * @display_page_tree: the #RBDisplayPageTree
 * @page: the #RBDisplayPage to toggle
 *
 * If @page is expanded (children visible), collapses it, otherwise expands it.
 */
void
rb_display_page_tree_toggle_expanded (RBDisplayPageTree *display_page_tree,
				      RBDisplayPage *page)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	g_assert (rb_display_page_model_find_page (display_page_tree->priv->page_model,
						   page,
						   &iter));
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (display_page_tree->priv->page_model),
					&iter);
	if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (display_page_tree->priv->treeview), path)) {
		rb_debug ("collapsing page %p", page);
		gtk_tree_view_collapse_row (GTK_TREE_VIEW (display_page_tree->priv->treeview), path);
		g_object_set (display_page_tree->priv->expander_renderer,
			      "expander-style",
			      GTK_EXPANDER_COLLAPSED,
			      NULL);
	} else {
		rb_debug ("expanding page %p", page);
		gtk_tree_view_expand_row (GTK_TREE_VIEW (display_page_tree->priv->treeview), path, FALSE);
		g_object_set (display_page_tree->priv->expander_renderer,
			      "expander-style",
			      GTK_EXPANDER_EXPANDED,
			      NULL);
	}

	gtk_tree_path_free (path);
}

static gboolean
selection_check_cb (GtkTreeSelection *selection,
		    GtkTreeModel *model,
		    GtkTreePath *path,
		    gboolean currently_selected,
		    RBDisplayPageTree *display_page_tree)
{
	GtkTreeIter iter;
	gboolean result = TRUE;

	if (currently_selected) {
		/* do anything? */
	} else if (gtk_tree_model_get_iter (model, &iter, path)) {
		RBDisplayPage *page;
		gtk_tree_model_get (model, &iter, RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page, -1);

		/* figure out if page can be selected */
		result = rb_display_page_selectable (page);

		g_object_unref (page);
	}
	return result;
}

static void
selection_changed_cb (GtkTreeSelection *selection,
		      RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;

	page = get_selected_page (display_page_tree);
	if (page != NULL) {
		g_signal_emit (display_page_tree, signals[SELECTED], 0, page);

		if (RB_IS_DEVICE_SOURCE (page) && rb_device_source_can_eject (RB_DEVICE_SOURCE (page))) {
			g_simple_action_set_enabled (display_page_tree->priv->eject_action, TRUE);
		} else {
			g_simple_action_set_enabled (display_page_tree->priv->eject_action, FALSE);
		}

		g_simple_action_set_enabled (display_page_tree->priv->remove_action, rb_display_page_can_remove (page));
		g_object_unref (page);
	} else {
		g_simple_action_set_enabled (display_page_tree->priv->remove_action, FALSE);
		g_simple_action_set_enabled (display_page_tree->priv->eject_action, FALSE);
	}
}

static void
source_name_edited_cb (GtkCellRendererText *renderer,
		       const char          *pathstr,
		       const char          *text,
		       RBDisplayPageTree   *display_page_tree)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	RBDisplayPage *page;

	if (text[0] == '\0')
		return;

	path = gtk_tree_path_new_from_string (pathstr);
	g_return_if_fail (gtk_tree_model_get_iter (GTK_TREE_MODEL (display_page_tree->priv->page_model), &iter, path));
	gtk_tree_path_free (path);

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model),
			    &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL || RB_IS_SOURCE (page) == FALSE) {
		g_object_set (renderer, "editable", FALSE, NULL);
		return;
	}

	g_object_set (page, "name", text, NULL);
	g_object_unref (page);
}

static void
remove_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBDisplayPage *page = get_selected_page (RB_DISPLAY_PAGE_TREE (user_data));
	if (page) {
		rb_display_page_remove (page);
		g_object_unref (page);
	}
}

static void
eject_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBDisplayPage *page = get_selected_page (RB_DISPLAY_PAGE_TREE (user_data));
	if (page == NULL) {
		/* nothing */
	} else if (RB_IS_DEVICE_SOURCE (page) && rb_device_source_can_eject (RB_DEVICE_SOURCE (page))) {
		rb_device_source_eject (RB_DEVICE_SOURCE (page));
		g_object_unref (page);
	} else {
		rb_debug ("why are we here?");
		g_object_unref (page);
	}
}


static gboolean
display_page_search_equal_func (GtkTreeModel *model,
				gint column,
				const char *key,
				GtkTreeIter *iter,
				RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;
	gboolean result = TRUE;
	char *folded_key;
	char *name;
	char *folded_name;

	gtk_tree_model_get (model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	g_object_get (page, "name", &name, NULL);

	folded_key = rb_search_fold (key);
	folded_name = rb_search_fold (name);

	if (folded_key != NULL && folded_name != NULL) {
		result = (strncmp (folded_key, folded_name, strlen (folded_key)) != 0);
	}

	g_free (folded_key);
	g_free (folded_name);
	g_free (name);
	g_object_unref (page);
	return result;
}

/**
 * rb_display_page_tree_new:
 * @shell: the #RBShell instance
 *
 * Creates the display page tree widget.
 *
 * Return value: the display page tree widget.
 */
RBDisplayPageTree *
rb_display_page_tree_new (RBShell *shell)
{
	return RB_DISPLAY_PAGE_TREE (g_object_new (RB_TYPE_DISPLAY_PAGE_TREE,
						   "shell", shell,
						   NULL));
}

static void
impl_set_property (GObject      *object,
		   guint         prop_id,
		   const GValue *value,
		   GParamSpec   *pspec)
{
	RBDisplayPageTree *display_page_tree = RB_DISPLAY_PAGE_TREE (object);
	switch (prop_id)
	{
	case PROP_SHELL:
		display_page_tree->priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject    *object,
		   guint       prop_id,
		   GValue     *value,
		   GParamSpec *pspec)
{
	RBDisplayPageTree *display_page_tree = RB_DISPLAY_PAGE_TREE (object);
	switch (prop_id)
	{
	case PROP_SHELL:
		g_value_set_object (value, display_page_tree->priv->shell);
		break;
	case PROP_MODEL:
		g_value_set_object (value, display_page_tree->priv->page_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBDisplayPageTree *display_page_tree = RB_DISPLAY_PAGE_TREE (object);

	g_clear_object (&display_page_tree->priv->blank_pixbuf);

	G_OBJECT_CLASS (rb_display_page_tree_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBDisplayPageTree *display_page_tree = RB_DISPLAY_PAGE_TREE (object);

	g_object_unref (display_page_tree->priv->page_model);

	if (display_page_tree->priv->expand_rows_id != 0) {
		g_source_remove (display_page_tree->priv->expand_rows_id);
		display_page_tree->priv->expand_rows_id = 0;
	}

	rb_list_destroy_free (display_page_tree->priv->expand_rows, (GDestroyNotify) gtk_tree_row_reference_free);

	G_OBJECT_CLASS (rb_display_page_tree_parent_class)->finalize (object);
}

static void
impl_constructed (GObject *object)
{
	RBDisplayPageTree *display_page_tree;
	GtkCellRenderer *renderer;
	GtkWidget *scrolled;
	GtkStyleContext *context;
	GtkWidget *box;
	GtkToolItem *tool_item;
	GtkWidget *button;
	GtkWidget *image;
	GIcon *icon;
	GMenuModel *menu;
	GtkBuilder *builder;
	GApplication *app;
	GtkAccelGroup *accel_group;
	int pixbuf_width, pixbuf_height;

	GActionEntry actions[] = {
		{ "display-page-remove", remove_action_cb },
		{ "display-page-eject", eject_action_cb }
	};

	RB_CHAIN_GOBJECT_METHOD (rb_display_page_tree_parent_class, constructed, object);
	display_page_tree = RB_DISPLAY_PAGE_TREE (object);


	scrolled = gtk_scrolled_window_new (NULL, NULL);
	context = gtk_widget_get_style_context (scrolled);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
	g_object_set (scrolled,
		      "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
		      "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
		      "hexpand", TRUE,
		      "vexpand", TRUE,
		      NULL);
	gtk_grid_attach (GTK_GRID (display_page_tree), scrolled, 0, 0, 1, 1);

	display_page_tree->priv->page_model = rb_display_page_model_new ();
	g_signal_connect_object (display_page_tree->priv->page_model,
				 "drop-received",
				 G_CALLBACK (drop_received_cb),
				 display_page_tree, 0);
	g_signal_connect_object (display_page_tree->priv->page_model,
				 "row-inserted",
				 G_CALLBACK (model_row_inserted_cb),
				 display_page_tree, 0);

	display_page_tree->priv->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (display_page_tree->priv->page_model));
	gtk_style_context_add_class (gtk_widget_get_style_context (display_page_tree->priv->treeview), GTK_STYLE_CLASS_SIDEBAR);

	g_object_set (display_page_tree->priv->treeview,
		      "headers-visible", FALSE,
		      "reorderable", TRUE,
		      "enable-search", TRUE,
		      "search-column", RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE,
		      NULL);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (display_page_tree->priv->treeview),
					     (GtkTreeViewSearchEqualFunc) display_page_search_equal_func,
					     display_page_tree,
					     NULL);

	rb_display_page_model_set_dnd_targets (display_page_tree->priv->page_model,
					       GTK_TREE_VIEW (display_page_tree->priv->treeview));

	g_signal_connect_object (display_page_tree->priv->treeview,
				 "row_activated",
				 G_CALLBACK (row_activated_cb),
				 display_page_tree, 0);
	g_signal_connect_object (display_page_tree->priv->treeview,
				 "key_release_event",
				 G_CALLBACK (key_release_cb),
				 display_page_tree, 0);

	display_page_tree->priv->main_column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_clickable (display_page_tree->priv->main_column, FALSE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (display_page_tree->priv->treeview),
				     display_page_tree->priv->main_column);

	gtk_icon_size_lookup (RB_DISPLAY_PAGE_ICON_SIZE, &pixbuf_width, &pixbuf_height);
	display_page_tree->priv->blank_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, pixbuf_width, pixbuf_height);
	gdk_pixbuf_fill (display_page_tree->priv->blank_pixbuf, 0);

	/* initial padding */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	g_object_set (renderer, "xpad", 3, NULL);

	/* headings */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) heading_cell_data_func,
						 display_page_tree,
						 NULL);
	g_object_set (renderer,
		      "weight", PANGO_WEIGHT_BOLD,
		      "weight-set", TRUE,
		      "ypad", 6,
		      "xpad", 0,
		      NULL);

	/* icon padding */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) padding_cell_data_func,
						 display_page_tree,
						 NULL);

	/* padding for second level */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) padding2_cell_data_func,
						 display_page_tree,
						 NULL);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) pixbuf_cell_data_func,
						 display_page_tree,
						 NULL);
	if (gtk_check_version (3, 16, 0) != NULL) {
		g_object_set (renderer, "follow-state", TRUE, NULL);
	}

	/* Set up the name column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) title_cell_data_func,
						 display_page_tree,
						 NULL);
	g_signal_connect_object (renderer, "edited", G_CALLBACK (source_name_edited_cb), display_page_tree, 0);

	g_object_set (display_page_tree->priv->treeview, "show-expanders", FALSE, NULL);
	display_page_tree->priv->title_renderer = renderer;

	/* Expander */
	renderer = gossip_cell_renderer_expander_new ();
	gtk_tree_view_column_pack_end (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) expander_cell_data_func,
						 display_page_tree,
						 NULL);
	display_page_tree->priv->expander_renderer = renderer;

	/* toolbar actions */
	app = g_application_get_default ();
	g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), display_page_tree);

	/* disable the remove and eject actions initially */
	display_page_tree->priv->remove_action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app), "display-page-remove"));
	display_page_tree->priv->eject_action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app), "display-page-eject"));
	g_simple_action_set_enabled (display_page_tree->priv->remove_action, FALSE);
	g_simple_action_set_enabled (display_page_tree->priv->eject_action, FALSE);

	/* toolbar */
	display_page_tree->priv->toolbar = gtk_toolbar_new ();
	gtk_toolbar_set_style (GTK_TOOLBAR (display_page_tree->priv->toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_set_icon_size (GTK_TOOLBAR (display_page_tree->priv->toolbar), GTK_ICON_SIZE_MENU);

	context = gtk_widget_get_style_context (display_page_tree->priv->toolbar);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
	gtk_style_context_add_class (context, "sidebar-toolbar");

	gtk_grid_attach (GTK_GRID (display_page_tree), display_page_tree->priv->toolbar, 0, 1, 1, 1);

	tool_item = gtk_tool_item_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (tool_item), box);
	gtk_toolbar_insert (GTK_TOOLBAR (display_page_tree->priv->toolbar), tool_item, -1);

	display_page_tree->priv->add_menubutton = gtk_menu_button_new ();
	icon = g_themed_icon_new_with_default_fallbacks ("list-add-symbolic");
	image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
	gtk_button_set_image (GTK_BUTTON (display_page_tree->priv->add_menubutton), image);
	gtk_box_pack_start (GTK_BOX (box), display_page_tree->priv->add_menubutton, FALSE, FALSE, 0);
	g_object_unref (icon);

	g_object_get (display_page_tree->priv->shell, "accel-group", &accel_group, NULL);
	gtk_widget_add_accelerator (display_page_tree->priv->add_menubutton,
				    "activate",
				    accel_group,
				    GDK_KEY_A,
				    GDK_MOD1_MASK,
				    GTK_ACCEL_VISIBLE);
	g_object_unref (accel_group);

	builder = rb_builder_load ("display-page-add-menu.ui", NULL);
	menu = G_MENU_MODEL (gtk_builder_get_object (builder, "display-page-add-menu"));
	rb_application_link_shared_menus (RB_APPLICATION (g_application_get_default ()), G_MENU (menu));
	gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (display_page_tree->priv->add_menubutton), menu);
	g_object_unref (builder);

	button = gtk_button_new ();
	icon = g_themed_icon_new_with_default_fallbacks ("list-remove-symbolic");
	image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
	gtk_button_set_image (GTK_BUTTON (button), image);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
	g_object_unref (icon);

	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "app.display-page-remove");

	/* maybe this should be a column in the tree instead.. */
	button = gtk_button_new ();
	icon = g_themed_icon_new_with_default_fallbacks ("media-eject-symbolic");
	image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
	gtk_button_set_image (GTK_BUTTON (button), image);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
	g_object_unref (icon);
	
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "app.display-page-eject");

	display_page_tree->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (display_page_tree->priv->treeview));
	g_signal_connect_object (display_page_tree->priv->selection,
			         "changed",
			         G_CALLBACK (selection_changed_cb),
			         display_page_tree,
				 0);
	gtk_tree_selection_set_select_function (display_page_tree->priv->selection,
						(GtkTreeSelectionFunc) selection_check_cb,
						display_page_tree,
						NULL);

	gtk_container_add (GTK_CONTAINER (scrolled), display_page_tree->priv->treeview);
}

static void
rb_display_page_tree_init (RBDisplayPageTree *display_page_tree)
{
	display_page_tree->priv =
		G_TYPE_INSTANCE_GET_PRIVATE (display_page_tree,
					     RB_TYPE_DISPLAY_PAGE_TREE,
					     RBDisplayPageTreePrivate);
}

static void
rb_display_page_tree_class_init (RBDisplayPageTreeClass *class)
{
	GObjectClass   *o_class;

	o_class = (GObjectClass *) class;

	o_class->constructed = impl_constructed;
	o_class->dispose = impl_dispose;
	o_class->finalize = impl_finalize;
	o_class->set_property = impl_set_property;
	o_class->get_property = impl_get_property;

	/**
	 * RBDisplayPageTree:shell:
	 *
	 * The #RBShell instance
	 */
	g_object_class_install_property (o_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBDisplayPageTree:model:
	 *
	 * The #GtkTreeModel for the display page tree
	 */
	g_object_class_install_property (o_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "GtkTreeModel",
							      "GtkTreeModel object",
							      GTK_TYPE_TREE_MODEL,
							      G_PARAM_READABLE));
	/**
	 * RBDisplayPageTree::selected:
	 * @tree: the #RBDisplayPageTree
	 * @page: the newly selected #RBDisplayPage
	 *
	 * Emitted when a page is selected from the tree
	 */
	signals[SELECTED] =
		g_signal_new ("selected",
			      G_OBJECT_CLASS_TYPE (o_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDisplayPageTreeClass, selected),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

	/**
	 * RBDisplayPageTree::drop-received:
	 * @tree: the #RBDisplayPageTree
	 * @page: the #RBDisplagePage receiving the drop
	 * @data: the drop data
	 *
	 * Emitted when a drag and drop to the tree completes.
	 */
	signals[DROP_RECEIVED] =
		g_signal_new ("drop-received",
			      G_OBJECT_CLASS_TYPE (o_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDisplayPageTreeClass, drop_received),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	g_type_class_add_private (class, sizeof (RBDisplayPageTreePrivate));
}
