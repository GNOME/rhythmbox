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
#include "rb-stock-icons.h"
#include "rb-marshal.h"
#include "rb-cell-renderer-pixbuf.h"
#include "gossip-cell-renderer-expander.h"
#include "rb-tree-dnd.h"
#include "rb-util.h"
#include "rb-auto-playlist-source.h"
#include "rb-static-playlist-source.h"

/**
 * SECTION:rb-display-page-tree
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
	GtkWidget *treeview;
	GtkCellRenderer *title_renderer;
	GtkCellRenderer *expander_renderer;

	RBDisplayPageModel *page_model;
	GtkTreeSelection *selection;

	int child_source_count;
	GtkTreeViewColumn *main_column;

	RBShell *shell;

	GList *expand_rows;
	GtkTreeRowReference *expand_select_row;
	guint expand_rows_id;

	GSettings *settings;
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

G_DEFINE_TYPE (RBDisplayPageTree, rb_display_page_tree, GTK_TYPE_SCROLLED_WINDOW)


static gboolean
retrieve_expander_state (RBDisplayPageTree *display_page_tree, RBDisplayPageGroup *group)
{
	char **groups;
	char *id;
	gboolean collapsed;

	groups = g_settings_get_strv (display_page_tree->priv->settings, "collapsed-groups");
	g_object_get (group, "id", &id, NULL);
	collapsed = rb_str_in_strv (id, (const char **)groups);
	g_free (id);
	g_strfreev (groups);

	return (collapsed == FALSE);
}

static void
store_expander_state (RBDisplayPageTree *display_page_tree, RBDisplayPageGroup *group, gboolean expanded)
{
	char **newgroups = NULL;
	char **groups;
	char *id;
	int num;
	int i;
	int p;

	groups = g_settings_get_strv (display_page_tree->priv->settings, "collapsed-groups");
	g_object_get (group, "id", &id, NULL);

	num = g_strv_length (groups);
	p = 0;
	if (rb_str_in_strv (id, (const char **)groups) && expanded) {
		newgroups = g_new0(char *, num);
		for (i = 0; i < num; i++) {
			if (g_strcmp0 (groups[i], id) != 0) {
				newgroups[p++] = g_strdup (groups[i]);
			}
		}
	} else if (expanded == FALSE) {
		newgroups = g_new0(char *, num + 2);
		for (i = 0; i < num; i++) {
			newgroups[i] = g_strdup (groups[i]);
		}
		newgroups[i] = g_strdup (id);
	}

	if (newgroups != NULL) {
		g_settings_set_strv (display_page_tree->priv->settings, "collapsed-groups", (const char * const *)newgroups);
		g_strfreev (newgroups);
	}
	g_strfreev (groups);
	g_free (id);
}

static void
set_cell_background (RBDisplayPageTree  *display_page_tree,
		     GtkCellRenderer    *cell,
		     gboolean            is_group,
		     gboolean            is_active)
{
	GdkRGBA color;

	g_return_if_fail (display_page_tree != NULL);
	g_return_if_fail (cell != NULL);

	gtk_style_context_get_color (gtk_widget_get_style_context (GTK_WIDGET (display_page_tree)),
				     GTK_STATE_SELECTED,
				     &color);

	if (!is_group) {
		if (is_active) {
			/* Here we take the current theme colour and add it to
			 * the colour for white and average the two. This
			 * gives a colour which is inline with the theme but
			 * slightly whiter.
			 */
			color.red = (color.red + 1.0) / 2;
			color.green = (color.green + 1.0) / 2;
			color.blue = (color.blue + 1.0) / 2;

			g_object_set (cell,
				      "cell-background-rgba", &color,
				      NULL);
		} else {
			g_object_set (cell,
				      "cell-background-rgba", NULL,
				      NULL);
		}
	} else {
		/* don't set background for group heading */
	}
}

static void
indent_level1_cell_data_func (GtkTreeViewColumn *tree_column,
			      GtkCellRenderer   *cell,
			      GtkTreeModel      *model,
			      GtkTreeIter       *iter,
			      RBDisplayPageTree *display_page_tree)
{
	GtkTreePath *path;
	int          depth;

	path = gtk_tree_model_get_path (model, iter);
	depth = gtk_tree_path_get_depth (path);
	gtk_tree_path_free (path);
	g_object_set (cell,
		      "text", "    ",
		      "visible", depth > 1,
		      NULL);
}

static void
indent_level2_cell_data_func (GtkTreeViewColumn *tree_column,
			      GtkCellRenderer   *cell,
			      GtkTreeModel      *model,
			      GtkTreeIter       *iter,
			      RBDisplayPageTree *display_page_tree)
{
	GtkTreePath *path;
	int          depth;

	path = gtk_tree_model_get_path (model, iter);
	depth = gtk_tree_path_get_depth (path);
	gtk_tree_path_free (path);
	g_object_set (cell,
		      "text", "    ",
		      "visible", depth > 2,
		      NULL);
}

static void
pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       RBDisplayPageTree *display_page_tree)
{
	RBDisplayPage *page;
	GdkPixbuf *pixbuf;

	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	g_object_get (page, "pixbuf", &pixbuf, NULL);

	if (pixbuf == NULL) {
		g_object_set (cell,
			      "visible", FALSE,
			      "pixbuf", NULL,
			      NULL);
	} else {
		g_object_set (cell,
			      "visible", TRUE,
			      "pixbuf", pixbuf,
			      NULL);
		g_object_unref (pixbuf);
	}

	set_cell_background (display_page_tree, cell, RB_IS_DISPLAY_PAGE_GROUP (page), FALSE);
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
	char    *name;
	gboolean playing;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PLAYING, &playing,
			    -1);

	g_object_get (page, "name", &name, NULL);

	g_object_set (renderer,
		      "text", name,
		      "weight", playing ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);

	set_cell_background (display_page_tree, renderer, RB_IS_DISPLAY_PAGE_GROUP (page), FALSE);

	g_free (name);
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

	if (gtk_tree_model_iter_has_child (model, iter)) {
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
	} else {
		g_object_set (cell, "visible", FALSE, NULL);
	}

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model), iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	set_cell_background (display_page_tree, cell, RB_IS_DISPLAY_PAGE_GROUP (page), FALSE);
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
update_expanded_state (RBDisplayPageTree *display_page_tree,
		       GtkTreeIter *iter,
		       gboolean expanded)
{
	GtkTreeModel *model;
	RBDisplayPage *page;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (display_page_tree->priv->treeview));
	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (RB_IS_DISPLAY_PAGE_GROUP (page)) {
		store_expander_state (display_page_tree, RB_DISPLAY_PAGE_GROUP (page), expanded);
	}
}

static void
row_expanded_cb (GtkTreeView *treeview,
		 GtkTreeIter *iter,
		 GtkTreePath *path,
		 RBDisplayPageTree *display_page_tree)
{
	update_expanded_state (display_page_tree, iter, TRUE);
}

static void
row_collapsed_cb (GtkTreeView *treeview,
		  GtkTreeIter *iter,
		  GtkTreePath *path,
		  RBDisplayPageTree *display_page_tree)
{
	update_expanded_state (display_page_tree, iter, FALSE);
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
	gboolean expand = FALSE;
	if (gtk_tree_path_get_depth (path) == 2) {
		GtkTreeIter group_iter;
		expand = TRUE;
		if (gtk_tree_model_iter_parent (model, &group_iter, iter)) {
			gboolean loaded;
			RBDisplayPage *page;
			RBDisplayPageGroupCategory category;

			gtk_tree_model_get (model, &group_iter,
					    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
					    -1);
			g_object_get (page, "loaded", &loaded, "category", &category, NULL);
			if (category == RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT || loaded == FALSE) {
				expand = retrieve_expander_state (display_page_tree, RB_DISPLAY_PAGE_GROUP (page));
			}
			g_object_unref (page);
		}
	} else if (gtk_tree_path_get_depth (path) == 1) {
		RBDisplayPage *page;

		gtk_tree_model_get (model, iter,
				    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
				    -1);
		expand = retrieve_expander_state (display_page_tree, RB_DISPLAY_PAGE_GROUP (page));
	}

	if (expand) {
		display_page_tree->priv->expand_rows = g_list_append (display_page_tree->priv->expand_rows,
								      gtk_tree_row_reference_new (model, path));
		if (display_page_tree->priv->expand_rows_id == 0) {
			display_page_tree->priv->expand_rows_id = g_idle_add ((GSourceFunc)expand_rows_cb, display_page_tree);
		}
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (display_page_tree->priv->treeview));
}

static gboolean
emit_show_popup (GtkTreeView *treeview,
		 RBDisplayPageTree *display_page_tree)
{
	GtkTreeIter iter;
	RBDisplayPage *page;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (treeview),
					      NULL, &iter))
		return FALSE;

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model),
			    &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL)
		return FALSE;

	g_return_val_if_fail (RB_IS_DISPLAY_PAGE (page), FALSE);

	rb_display_page_show_popup (page);
	g_object_unref (page);
	return TRUE;
}

static gboolean
button_press_cb (GtkTreeView *treeview,
		 GdkEventButton *event,
		 RBDisplayPageTree *display_page_tree)
{
	GtkTreeIter  iter;
	GtkTreePath *path;
	gboolean     res;

	if (event->button != 3) {
		return FALSE;
	}

	res = gtk_tree_view_get_path_at_pos (treeview,
					     event->x,
					     event->y,
					     &path,
					     NULL,
					     NULL,
					     NULL);
	if (! res) {
		/* pointer is over empty space */
		GtkUIManager *uimanager;
		g_object_get (display_page_tree->priv->shell, "ui-manager", &uimanager, NULL);
		rb_gtk_action_popup_menu (uimanager, "/DisplayPageTreePopup");
		g_object_unref (uimanager);
		return TRUE;
	}

	res = gtk_tree_model_get_iter (GTK_TREE_MODEL (display_page_tree->priv->page_model),
				       &iter,
				       path);
	gtk_tree_path_free (path);
	if (res) {
		gtk_tree_selection_select_iter (gtk_tree_view_get_selection (treeview), &iter);
	}

	return emit_show_popup (treeview, display_page_tree);
}

static gboolean
key_release_cb (GtkTreeView *treeview,
		GdkEventKey *event,
		RBDisplayPageTree *display_page_tree)
{
	GtkTreeIter iter;
	RBDisplayPage *page;
	gboolean res;

	/* F2 = rename playlist */
	if (event->keyval != GDK_KEY_F2) {
		return FALSE;
	}

	if (!gtk_tree_selection_get_selected (display_page_tree->priv->selection, NULL, &iter)) {
		return FALSE;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (display_page_tree->priv->page_model),
			    &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL || RB_IS_SOURCE (page) == FALSE) {
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

static gboolean
popup_menu_cb (GtkTreeView *treeview,
	       RBDisplayPageTree *display_page_tree)
{
	return emit_show_popup (treeview, display_page_tree);
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
	GtkTreeIter iter;
	GtkTreeModel *model;
	RBDisplayPage *page;

	if (!gtk_tree_selection_get_selected (display_page_tree->priv->selection, &model, &iter))
		return;

	gtk_tree_model_get (model,
			    &iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL)
		return;
	g_signal_emit (display_page_tree, signals[SELECTED], 0, page);
	g_object_unref (page);
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
						   "hadjustment", NULL,
						   "vadjustment", NULL,
						   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
						   "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
						   "shadow_type", GTK_SHADOW_IN,
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

	RB_CHAIN_GOBJECT_METHOD (rb_display_page_tree_parent_class, constructed, object);
	display_page_tree = RB_DISPLAY_PAGE_TREE (object);

	gtk_container_add (GTK_CONTAINER (display_page_tree), display_page_tree->priv->treeview);

	display_page_tree->priv->settings = g_settings_new ("org.gnome.rhythmbox.display-page-tree");
}

static void
rb_display_page_tree_init (RBDisplayPageTree *display_page_tree)
{
	GtkCellRenderer *renderer;

	display_page_tree->priv =
		G_TYPE_INSTANCE_GET_PRIVATE (display_page_tree,
					     RB_TYPE_DISPLAY_PAGE_TREE,
					     RBDisplayPageTreePrivate);

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
				 "row-collapsed",
				 G_CALLBACK (row_collapsed_cb),
				 display_page_tree, 0);
	g_signal_connect_object (display_page_tree->priv->treeview,
				 "row-expanded",
				 G_CALLBACK (row_expanded_cb),
				 display_page_tree, 0);
	g_signal_connect_object (display_page_tree->priv->treeview,
				 "button_press_event",
				 G_CALLBACK (button_press_cb),
				 display_page_tree, 0);
	g_signal_connect_object (display_page_tree->priv->treeview,
				 "key_release_event",
				 G_CALLBACK (key_release_cb),
				 display_page_tree, 0);

	g_signal_connect_object (display_page_tree->priv->treeview,
				 "popup_menu",
				 G_CALLBACK (popup_menu_cb),
				 display_page_tree, 0);

	display_page_tree->priv->main_column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_clickable (display_page_tree->priv->main_column, FALSE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (display_page_tree->priv->treeview),
				     display_page_tree->priv->main_column);

	/* Set up the indent level1 column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) indent_level1_cell_data_func,
						 display_page_tree,
						 NULL);
	g_object_set (renderer,
		      "xpad", 0,
		      "visible", FALSE,
		      NULL);

	/* Set up the indent level2 column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) indent_level2_cell_data_func,
						 display_page_tree,
						 NULL);
	g_object_set (renderer,
		      "xpad", 0,
		      "visible", FALSE,
		      NULL);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (display_page_tree->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (display_page_tree->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) pixbuf_cell_data_func,
						 display_page_tree,
						 NULL);

	g_object_set (renderer,
		      "xpad", 8,
		      "ypad", 1,
		      "visible", FALSE,
		      NULL);


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
}

static void
rb_display_page_tree_class_init (RBDisplayPageTreeClass *class)
{
	GObjectClass   *o_class;

	o_class = (GObjectClass *) class;

	o_class->constructed = impl_constructed;
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
			      g_cclosure_marshal_VOID__OBJECT,
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
			      rb_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	g_type_class_add_private (class, sizeof (RBDisplayPageTreePrivate));
}
