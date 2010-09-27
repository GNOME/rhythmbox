/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#include "rb-sourcelist.h"
#include "rb-sourcelist-model.h"
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
 * SECTION:rb-sourcelist
 * @short_description: source list widget
 *
 * The source list widget is a GtkTreeView backed by a GtkListStore
 * containing the sources and some extra data used to structure the
 * tree view.
 *
 * The source list widget displays the available sources.  Sources are divided into
 * groups - library, stores, playlists, devices, network shares.  Groups are
 * displayed as headers, with expanders for hiding and showing the sources in 
 * the group.  Sources themselves may also have child sources, such as playlists
 * on portable audio players.
 *
 * Sources are displayed with an icon and a name.  If the source is currently
 * playing, the name is displayed in bold.
 */

struct RBSourceListPrivate
{
	GtkWidget *treeview;
	GtkCellRenderer *title_renderer;

	GtkTreeModel *real_model;
	GtkTreeModel *filter_model;
	GtkTreeSelection *selection;

	RBSource *playing_source;
	int child_source_count;
	GtkTreeViewColumn *main_column;

	RBShell *shell;
};

#define RB_SOURCELIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SOURCELIST, RBSourceListPrivate))

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
	SOURCE_ACTIVATED,
	SHOW_POPUP,
	LAST_SIGNAL
};

static void rb_sourcelist_class_init (RBSourceListClass *klass);
static void rb_sourcelist_init (RBSourceList *sourcelist);
static void rb_sourcelist_constructed (GObject *object);
static void rb_sourcelist_finalize (GObject *object);
static void rb_sourcelist_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_sourcelist_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
static gboolean rb_sourcelist_source_to_iter (RBSourceList *sourcelist,
					      RBSource *source,
					      GtkTreeIter *iter);
static gboolean rb_sourcelist_visible_source_to_iter (RBSourceList *sourcelist,
						      RBSource *source,
						      GtkTreeIter *iter);
static void rb_sourcelist_selection_changed_cb (GtkTreeSelection *selection,
						RBSourceList *sourcelist);
static void source_name_edited_cb (GtkCellRendererText *renderer, const char *pathstr,
				   const char *text, RBSourceList *sourcelist);

static guint rb_sourcelist_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSourceList, rb_sourcelist, GTK_TYPE_SCROLLED_WINDOW)

static void
rb_sourcelist_class_init (RBSourceListClass *class)
{
	GObjectClass   *o_class;

	o_class = (GObjectClass *) class;

	o_class->constructed = rb_sourcelist_constructed;
	o_class->finalize = rb_sourcelist_finalize;
	o_class->set_property = rb_sourcelist_set_property;
	o_class->get_property = rb_sourcelist_get_property;

	/**
	 * RBSourceList:shell:
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
	 * RBSourceList:model:
	 *
	 * The #GtkTreeModel for the source list
	 */
	g_object_class_install_property (o_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "GtkTreeModel",
							      "GtkTreeModel object",
							      GTK_TYPE_TREE_MODEL,
							      G_PARAM_READABLE));
	/**
	 * RBSourceList::selected:
	 * @list: the #RBSourceList
	 * @source: the newly selected #RBSource
	 *
	 * Emitted when a source is selected from the source list
	 */
	rb_sourcelist_signals[SELECTED] =
		g_signal_new ("selected",
			      G_OBJECT_CLASS_TYPE (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

	/**
	 * RBSourceList::drop-received:
	 * @list: the #RBSourceList
	 * @target: the #RBSource receiving the drop
	 * @data: the drop data
	 *
	 * Emitted when a drag and drop to the source list completes.
	 */
	rb_sourcelist_signals[DROP_RECEIVED] =
		g_signal_new ("drop_received",
			      G_OBJECT_CLASS_TYPE (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, drop_received),
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	/**
	 * RBSourceList::source-activated:
	 * @list: the #RBSourceList
	 * @target: the activated #RBSource
	 *
	 * Emitted when a source is activated (by double clicking or hitting
	 * the enter key)
	 */
	rb_sourcelist_signals[SOURCE_ACTIVATED] =
		g_signal_new ("source_activated",
			      G_OBJECT_CLASS_TYPE (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, source_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

	/**
	 * RBSourceList::show-popup:
	 * @list: the #RBSourceList
	 * @target: the #RBSource for which a popup menu should be shown
	 *
	 * Emitted when a source should display a popup menu in response to some
	 * user action, such as right clicking or pressing shift-f10.
	 */
	rb_sourcelist_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, show_popup),
			      NULL, NULL,
			      rb_marshal_BOOLEAN__POINTER,
			      G_TYPE_BOOLEAN,
			      1,
			      RB_TYPE_SOURCE);

	g_type_class_add_private (class, sizeof (RBSourceListPrivate));
}

static void
rb_sourcelist_constructed (GObject *object)
{
	RBSourceList *sourcelist;

	RB_CHAIN_GOBJECT_METHOD (rb_sourcelist_parent_class, constructed, object);
	sourcelist = RB_SOURCELIST (object);

	gtk_container_add (GTK_CONTAINER (sourcelist),
			   sourcelist->priv->treeview);
}

static void
rb_sourcelist_cell_set_background (RBSourceList       *list,
				   GtkCellRenderer    *cell,
				   gboolean            is_group,
				   gboolean            is_active)
{
	GdkColor  color;
	GtkStyle *style;

	g_return_if_fail (list != NULL);
	g_return_if_fail (cell != NULL);

	style = gtk_widget_get_style (GTK_WIDGET (list));

	if (!is_group) {
		if (is_active) {
			color = style->bg[GTK_STATE_SELECTED];

			/* Here we take the current theme colour and add it to
			 * the colour for white and average the two. This
			 * gives a colour which is inline with the theme but
			 * slightly whiter.
			 */
			color.red = (color.red + (style->white).red) / 2;
			color.green = (color.green + (style->white).green) / 2;
			color.blue = (color.blue + (style->white).blue) / 2;

			g_object_set (cell,
				      "cell-background-gdk", &color,
				      NULL);
		} else {
			g_object_set (cell,
				      "cell-background-gdk", NULL,
				      NULL);
		}
	} else {
		/* don't set background for group heading */
	}
}

static void
sourcelist_indent_level1_cell_data_func (GtkTreeViewColumn *tree_column,
					 GtkCellRenderer   *cell,
					 GtkTreeModel      *model,
					 GtkTreeIter       *iter,
					 RBSourceList      *sourcelist)
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
sourcelist_indent_level2_cell_data_func (GtkTreeViewColumn *tree_column,
					 GtkCellRenderer   *cell,
					 GtkTreeModel      *model,
					 GtkTreeIter       *iter,
					 RBSourceList      *sourcelist)
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
sourcelist_pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
				  GtkCellRenderer   *cell,
				  GtkTreeModel      *model,
				  GtkTreeIter       *iter,
				  RBSourceList      *sourcelist)
{
	GdkPixbuf *pixbuf;
	gboolean   is_group;
	gboolean   is_active;

	gtk_tree_model_get (model, iter,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &is_group,
			    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, &pixbuf,
			    -1);

	g_object_set (cell,
		      "visible", !is_group,
		      "pixbuf", pixbuf,
		      NULL);

	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}

	is_active = FALSE;
	rb_sourcelist_cell_set_background (sourcelist, cell, is_group, is_active);
}

static void
rb_sourcelist_title_cell_data_func (GtkTreeViewColumn *column,
				    GtkCellRenderer   *renderer,
				    GtkTreeModel      *tree_model,
				    GtkTreeIter       *iter,
				    RBSourceList      *sourcelist)
{
	char    *str;
	gboolean playing;
	gboolean is_group;
	gboolean is_active;

	gtk_tree_model_get (GTK_TREE_MODEL (sourcelist->priv->filter_model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &str,
			    RB_SOURCELIST_MODEL_COLUMN_PLAYING, &playing,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &is_group,
			    -1);

	g_object_set (renderer,
		      "text", str,
		      "weight", playing ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);

	is_active = FALSE;
	rb_sourcelist_cell_set_background (sourcelist, renderer, is_group, is_active);

	g_free (str);
}

static int
compare_rows (GtkTreeModel *model,
	      GtkTreeIter  *a,
	      GtkTreeIter  *b,
	      gpointer      user_data)
{
	char *a_name;
	char *b_name;
	gboolean a_is_group;
	gboolean b_is_group;
	RBSourceGroupCategory a_category;
	RBSourceGroupCategory b_category;
	RBSource             *a_source;
	RBSource             *b_source;
	int ret;

	gtk_tree_model_get (model, a,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &a_name,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &a_is_group,
			    RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY, &a_category,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &a_source,
			    -1);
	gtk_tree_model_get (model, b,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &b_name,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &b_is_group,
			    RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY, &b_category,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &b_source,
			    -1);

	if (a_is_group && a_is_group) {
		if (a_category < b_category) {
			ret = -1;
		} else if (a_category > b_category) {
			ret = 1;
		} else {
			ret = g_utf8_collate (a_name, b_name);
		}
	} else {
		/* sort by name */
		switch (a_category) {
		case RB_SOURCE_GROUP_CATEGORY_FIXED:
			ret = -1;
			break;
		case RB_SOURCE_GROUP_CATEGORY_PERSISTENT:
			if (RB_IS_AUTO_PLAYLIST_SOURCE (a_source)
			    && RB_IS_AUTO_PLAYLIST_SOURCE (b_source)) {
				ret = g_utf8_collate (a_name, b_name);
			} else if (RB_IS_STATIC_PLAYLIST_SOURCE (a_source)
				   && RB_IS_STATIC_PLAYLIST_SOURCE (b_source)) {
				ret = g_utf8_collate (a_name, b_name);
			} else if (RB_IS_AUTO_PLAYLIST_SOURCE (a_source)) {
				ret = -1;
			} else {
				ret = 1;
			}

			break;
		case RB_SOURCE_GROUP_CATEGORY_REMOVABLE:
		case RB_SOURCE_GROUP_CATEGORY_TRANSIENT:
			ret = g_utf8_collate (a_name, b_name);
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	}

	if (a_source != NULL) {
		g_object_unref (a_source);
	}
	if (b_source != NULL) {
		g_object_unref (b_source);
	}
	g_free (a_name);
	g_free (b_name);

	return ret;
}

static void
rb_sourcelist_expander_cell_data_func (GtkTreeViewColumn *column,
				       GtkCellRenderer   *cell,
				       GtkTreeModel      *model,
				       GtkTreeIter       *iter,
				       RBSourceList      *sourcelist)
{
	gboolean is_group;
	gboolean is_active;

	gtk_tree_model_get (model, iter,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &is_group,
			    -1);

	if (gtk_tree_model_iter_has_child (model, iter)) {
		GtkTreePath *path;
		gboolean     row_expanded;

		path = gtk_tree_model_get_path (model, iter);
		row_expanded = gtk_tree_view_row_expanded (GTK_TREE_VIEW (gtk_tree_view_column_get_tree_view (column)),
							   path);
		gtk_tree_path_free (path);

		g_object_set (cell,
			      "visible", TRUE,
			      "expander-style", row_expanded ? GTK_EXPANDER_EXPANDED : GTK_EXPANDER_COLLAPSED,
			      NULL);
	} else {
		g_object_set (cell, "visible", FALSE, NULL);
	}

	is_active = FALSE;
	rb_sourcelist_cell_set_background (sourcelist, cell, is_group, is_active);
}

static void
row_activated_cb (GtkTreeView       *treeview,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  RBSourceList      *sourcelist)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;
	RBSource     *target;
	gboolean      is_group;

	model = gtk_tree_view_get_model (treeview);

	g_return_if_fail (gtk_tree_model_get_iter (model, &iter, path));

	gtk_tree_model_get (model, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &is_group,
			    -1);

	if (target != NULL) {
		g_signal_emit (sourcelist, rb_sourcelist_signals[SOURCE_ACTIVATED], 0, target);
		g_object_unref (target);
	}
}

static void
drop_received_cb (RBSourceListModel      *model,
		  RBSource               *target,
		  GtkTreeViewDropPosition pos,
		  GtkSelectionData       *data,
		  RBSourceList           *sourcelist)
{
	rb_debug ("drop recieved");
	/* Proxy the signal. */
	g_signal_emit (sourcelist, rb_sourcelist_signals[DROP_RECEIVED], 0, target, data);
}

static gboolean
emit_show_popup (GtkTreeView  *treeview,
		 RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	RBSource   *target;
	gboolean    ret;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (treeview),
					      NULL, &iter))
		return FALSE;

	gtk_tree_model_get (sourcelist->priv->filter_model, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == NULL)
		return FALSE;

	g_return_val_if_fail (RB_IS_SOURCE (target), FALSE);

	g_signal_emit (sourcelist, rb_sourcelist_signals[SHOW_POPUP], 0, target, &ret);

	if (target != NULL) {
		g_object_unref (target);
	}

	return ret;
}

static gboolean
button_press_cb (GtkTreeView    *treeview,
		 GdkEventButton *event,
		 RBSourceList   *sourcelist)
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
		g_object_get (sourcelist->priv->shell, "ui-manager", &uimanager, NULL);
		rb_gtk_action_popup_menu (uimanager, "/SourceListPopup");
		g_object_unref (uimanager);
		return TRUE;
	}

	res = gtk_tree_model_get_iter (GTK_TREE_MODEL (sourcelist->priv->filter_model), &iter, path);
	gtk_tree_path_free (path);
	if (res) {
		gtk_tree_selection_select_iter (gtk_tree_view_get_selection (treeview), &iter);
	}

	return emit_show_popup (treeview, sourcelist);
}

static gboolean
key_release_cb (GtkTreeView  *treeview,
		GdkEventKey  *event,
		RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	RBSource   *target;
	gboolean    res;

	/* F2 = rename playlist */
	if (event->keyval != GDK_F2) {
		return FALSE;
	}

	if (!gtk_tree_selection_get_selected (sourcelist->priv->selection, NULL, &iter)) {
		return FALSE;
	}

	gtk_tree_model_get (sourcelist->priv->filter_model,
			    &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target,
			    -1);
	if (target == NULL) {
		return FALSE;
	}

	res = FALSE;
	if (rb_source_can_rename (target)) {
		rb_sourcelist_edit_source_name (sourcelist, target);
		res = TRUE;
	}

	g_object_unref (target);

	return res;
}

static gboolean
popup_menu_cb (GtkTreeView  *treeview,
	       RBSourceList *sourcelist)
{
	return emit_show_popup (treeview, sourcelist);
}

static void
rb_sourcelist_init (RBSourceList *sourcelist)
{
	GtkCellRenderer *renderer;

	sourcelist->priv = RB_SOURCELIST_GET_PRIVATE (sourcelist);

	sourcelist->priv->filter_model = rb_sourcelist_model_new ();
	sourcelist->priv->real_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (sourcelist->priv->filter_model));
	g_signal_connect_object (sourcelist->priv->filter_model,
				 "drop_received",
				 G_CALLBACK (drop_received_cb),
				 sourcelist, 0);

	sourcelist->priv->treeview = gtk_tree_view_new_with_model (sourcelist->priv->filter_model);

	g_object_set (sourcelist->priv->treeview,
		      "headers-visible", FALSE,
		      "reorderable", TRUE,
		      "enable-search", TRUE,
		      "search-column", RB_SOURCELIST_MODEL_COLUMN_NAME,
		      NULL);

	rb_sourcelist_model_set_dnd_targets (RB_SOURCELIST_MODEL (sourcelist->priv->filter_model),
					     GTK_TREE_VIEW (sourcelist->priv->treeview));

	g_signal_connect_object (sourcelist->priv->treeview,
				 "row_activated",
				 G_CALLBACK (row_activated_cb),
				 sourcelist, 0);

	g_signal_connect_object (sourcelist->priv->treeview,
				 "button_press_event",
				 G_CALLBACK (button_press_cb),
				 sourcelist, 0);
	g_signal_connect_object (sourcelist->priv->treeview,
				 "key_release_event",
				 G_CALLBACK (key_release_cb),
				 sourcelist, 0);

	g_signal_connect_object (sourcelist->priv->treeview,
				 "popup_menu",
				 G_CALLBACK (popup_menu_cb),
				 sourcelist, 0);

	sourcelist->priv->main_column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (sourcelist->priv->main_column, _("S_ource"));
	gtk_tree_view_column_set_clickable (sourcelist->priv->main_column, FALSE);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (sourcelist->priv->real_model),
                                         RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY,
					 compare_rows,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sourcelist->priv->real_model),
                                              RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY,
                                              GTK_SORT_ASCENDING);

	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), sourcelist->priv->main_column);

	/* Set up the indent level1 column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) sourcelist_indent_level1_cell_data_func,
						 sourcelist,
						 NULL);

	g_object_set (renderer,
		      "xpad", 0,
		      "visible", FALSE,
		      NULL);

	/* Set up the indent level2 column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) sourcelist_indent_level2_cell_data_func,
						 sourcelist,
						 NULL);

	g_object_set (renderer,
		      "xpad", 0,
		      "visible", FALSE,
		      NULL);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) sourcelist_pixbuf_cell_data_func,
						 sourcelist,
						 NULL);

	g_object_set (renderer,
		      "xpad", 8,
		      "ypad", 1,
		      "visible", FALSE,
		      NULL);


	/* Set up the name column */
	sourcelist->priv->title_renderer = renderer = gtk_cell_renderer_text_new ();
	g_object_set (sourcelist->priv->title_renderer,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) rb_sourcelist_title_cell_data_func,
						 sourcelist,
						 NULL);
	g_signal_connect_object (renderer, "edited", G_CALLBACK (source_name_edited_cb), sourcelist, 0);

	g_object_set (sourcelist->priv->treeview, "show-expanders", FALSE, NULL);

	/* Expander */
	renderer = gossip_cell_renderer_expander_new ();
	gtk_tree_view_column_pack_end (sourcelist->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column,
						 renderer,
						 (GtkTreeCellDataFunc) rb_sourcelist_expander_cell_data_func,
						 sourcelist,
						 NULL);

	sourcelist->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sourcelist->priv->treeview));
	g_signal_connect_object (sourcelist->priv->selection,
			         "changed",
			         G_CALLBACK (rb_sourcelist_selection_changed_cb),
			         sourcelist,
				 0);
}

static void
rb_sourcelist_finalize (GObject *object)
{
	RBSourceList *sourcelist = RB_SOURCELIST (object);

	if (sourcelist->priv->filter_model != NULL) {
		g_object_unref (sourcelist->priv->filter_model);
	}

	G_OBJECT_CLASS (rb_sourcelist_parent_class)->finalize (object);
}

static void
rb_sourcelist_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	RBSourceList *sourcelist = RB_SOURCELIST (object);
	switch (prop_id)
	{
	case PROP_SHELL:
		sourcelist->priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_sourcelist_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
	RBSourceList *sourcelist = RB_SOURCELIST (object);
	switch (prop_id)
	{
	case PROP_SHELL:
		g_value_set_object (value, sourcelist->priv->shell);
		break;
	case PROP_MODEL:
		g_value_set_object (value, sourcelist->priv->filter_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_sourcelist_new:
 * @shell: the #RBShell instance
 *
 * Creates the source list widget.
 *
 * Return value: the source list widget.
 */
GtkWidget *
rb_sourcelist_new (RBShell *shell)
{
	RBSourceList *sourcelist;

	sourcelist = RB_SOURCELIST (g_object_new (RB_TYPE_SOURCELIST,
						  "hadjustment", NULL,
						  "vadjustment", NULL,
					          "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					          "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
					          "shadow_type", GTK_SHADOW_IN,
						  "shell", shell,
					          NULL));

	return GTK_WIDGET (sourcelist);
}

static void
icon_notify_cb (RBSource     *source,
		GParamSpec   *pspec,
		RBSourceList *sourcelist)
{
	GtkTreeIter iter;

	if (rb_sourcelist_source_to_iter (sourcelist, source, &iter)) {
		GdkPixbuf *pixbuf;

		g_object_get (source, "icon", &pixbuf, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model),
				    &iter,
				    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, pixbuf, -1);
		if (pixbuf != NULL) {
			g_object_unref (pixbuf);
		}
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

typedef struct {
	const char *name;
	gboolean    found;
	GtkTreeIter iter;
} FindGroup;

/* adapted from gossip */
static gboolean
sourcelist_get_group_foreach (GtkTreeModel *model,
			      GtkTreePath  *path,
			      GtkTreeIter  *iter,
			      FindGroup    *fg)
{
	char     *str;
	gboolean  is_group;

	/* Groups are only at the top level. */
	if (gtk_tree_path_get_depth (path) != 1) {
		return FALSE;
	}

	gtk_tree_model_get (model, iter,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &str,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, &is_group,
			    -1);
	if (is_group && strcmp (str, fg->name) == 0) {
		fg->found = TRUE;
		fg->iter = *iter;
	}

	g_free (str);

	return fg->found;
}

static gboolean
rb_sourcelist_find_group_iter (RBSourceList  *sourcelist,
			       RBSourceGroup *group,
			       GtkTreeIter   *iter)
{
	FindGroup fg;

	memset (&fg, 0, sizeof (fg));

	fg.name = group->display_name;

	gtk_tree_model_foreach (sourcelist->priv->real_model,
				(GtkTreeModelForeachFunc) sourcelist_get_group_foreach,
				&fg);

	if (iter != NULL) {
		*iter = fg.iter;
	}

	return fg.found;
}

static void
sourcelist_get_group (RBSourceList  *sourcelist,
		      RBSourceGroup *group,
		      GtkTreeIter   *iter_group,
		      gboolean      *created)
{
	gboolean found;

	found = rb_sourcelist_find_group_iter (sourcelist,
					       group,
					       iter_group);
	if (! found) {
		if (created != NULL) {
			*created = TRUE;
		}

		gtk_tree_store_append (GTK_TREE_STORE (sourcelist->priv->real_model), iter_group, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), iter_group,
				    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, NULL,
				    RB_SOURCELIST_MODEL_COLUMN_NAME, group->display_name,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, NULL,
				    RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES, NULL,
				    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, TRUE,
				    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, TRUE,
				    RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY, group->category,
				    -1);
	} else {
		if (created != NULL) {
			*created = FALSE;
		}
	}
}

static void
name_notify_cb (GObject    *obj,
		GParamSpec *pspec,
		gpointer    data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource *source = RB_SOURCE (obj);
	GtkTreeIter iter;
	char *name;

	if (rb_sourcelist_source_to_iter (sourcelist, source, &iter)) {
		g_object_get (obj, "name", &name, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model),
				    &iter,
				    RB_SOURCELIST_MODEL_COLUMN_NAME, name, -1);
		g_free (name);
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
visibility_notify_cb (GObject    *obj,
		      GParamSpec *pspec,
		      gpointer    data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource     *source = RB_SOURCE (obj);
	GtkTreeIter   iter;
	gboolean      old_visibility;
	gboolean      new_visibility;
	char         *name;

	g_object_get (obj,
		      "visibility", &new_visibility,
		      "name", &name,
		      NULL);
	rb_debug ("Source visibility changed: %s", name);
	g_free (name);

	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sourcelist->priv->filter_model));

	g_assert (rb_sourcelist_source_to_iter (sourcelist, source, &iter));
	if (gtk_tree_store_iter_depth (GTK_TREE_STORE (sourcelist->priv->real_model), &iter) > 0) {

		gtk_tree_model_get (GTK_TREE_MODEL (sourcelist->priv->real_model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, &old_visibility,
				    -1);

		if (old_visibility != new_visibility) {
			gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, new_visibility,
					    -1);
		}
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
rb_sourcelist_group_update_visibility (RBSourceList  *sourcelist,
				       RBSourceGroup *group)
{
	gboolean    found;
	GtkTreeIter iter;

	found = rb_sourcelist_find_group_iter (sourcelist, group, &iter);
	if (found) {
		gboolean has_child;

		has_child = gtk_tree_model_iter_has_child (sourcelist->priv->real_model, &iter);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, has_child,
				    -1);
		gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sourcelist->priv->filter_model));
	}
}

/**
 * rb_sourcelist_append:
 * @sourcelist: the #RBSourceList
 * @source: the #RBSource to add
 * @parent: the #RBSource below which to add the new source, or NULL
 *
 * Adds a new source to the source list.  If @parent is not NULL, the new
 * source is added beneath it.  Otherwise, it is added to the end of the
 * source group specified by the source's source-group property.
 */
void
rb_sourcelist_append (RBSourceList *sourcelist,
		      RBSource     *source,
		      RBSource     *parent)
{
	GtkTreeIter    iter;
	PangoAttrList *attrs;
	char          *name;
	GdkPixbuf     *pixbuf;
	gboolean       visible;
	RBSourceGroup *group;
	RBSourceGroupCategory group_category;
	GtkTreePath   *expand_path;

	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));
	g_return_if_fail (RB_IS_SOURCE (source));

	expand_path = NULL;
	group_category = RB_SOURCE_GROUP_CATEGORY_FIXED;

	g_object_get (source,
		      "name", &name,
		      "icon", &pixbuf,
		      "visibility", &visible,
		      "source-group", &group,
		      NULL);

	if (parent != NULL) {
		GtkTreeIter parent_iter;

		rb_debug ("inserting source %p with parent %p", source, parent);
		g_assert (rb_sourcelist_source_to_iter (sourcelist, parent, &parent_iter));
		gtk_tree_store_append (GTK_TREE_STORE (sourcelist->priv->real_model), &iter, &parent_iter);
	} else {
		GtkTreeIter    group_iter;
		gboolean       created;
		GtkTreePath   *path;

		if (group == NULL) {
			g_warning ("source %p has no group", source);
			group = RB_SOURCE_GROUP_LIBRARY;
		}

		rb_debug ("inserting source %p to group %s", source, group->name);

		sourcelist_get_group (sourcelist, group, &group_iter, &created);

		/* always set group visible when adding a source */
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &group_iter,
				    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, TRUE,
				    -1);

		group_category = group->category;

		path = gtk_tree_model_get_path (sourcelist->priv->real_model, &group_iter);
		expand_path = gtk_tree_model_filter_convert_child_path_to_path (GTK_TREE_MODEL_FILTER (sourcelist->priv->filter_model), path);
		gtk_tree_path_free (path);

		gtk_tree_store_append (GTK_TREE_STORE (sourcelist->priv->real_model), &iter, &group_iter);
	}

	attrs = pango_attr_list_new ();

	gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
			    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, pixbuf,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, name,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, source,
			    RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES, attrs,
			    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, visible,
			    RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, FALSE,
			    RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY, group_category,
			    -1);

	pango_attr_list_unref (attrs);

	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
	g_free (name);

	g_signal_connect_object (source, "notify::name", G_CALLBACK (name_notify_cb), sourcelist, 0);
	g_signal_connect_object (source, "notify::visibility", G_CALLBACK (visibility_notify_cb), sourcelist, 0);
	g_signal_connect_object (source, "notify::icon", G_CALLBACK (icon_notify_cb), sourcelist, 0);

	if (expand_path != NULL) {
		gtk_tree_view_expand_row (GTK_TREE_VIEW (sourcelist->priv->treeview), expand_path, TRUE);
		gtk_tree_path_free (expand_path);
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

typedef struct _SourcePath {
	RBSource    *source;
	GtkTreePath *path;
} SourcePath;

static gboolean
match_source_to_iter (GtkTreeModel *model,
		      GtkTreePath  *path,
		      GtkTreeIter  *iter,
		      SourcePath   *sp)
{
	RBSource *target = NULL;
	gboolean  res;

	gtk_tree_model_get (model, iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);

	res = FALSE;
	if (target == sp->source) {
		sp->path = gtk_tree_path_copy (path);
		res = TRUE;
	}

	if (target != NULL) {
		g_object_unref (target);
	}

	return res;
}

static gboolean
rb_sourcelist_source_to_iter (RBSourceList *sourcelist,
			      RBSource     *source,
			      GtkTreeIter  *iter)
{
	SourcePath *sp;
	gboolean    ret;

	ret = FALSE;
	sp = g_new0 (SourcePath, 1);
	sp->source = source;

	gtk_tree_model_foreach (sourcelist->priv->real_model, (GtkTreeModelForeachFunc) match_source_to_iter, sp);

	if (sp->path) {
		ret = gtk_tree_model_get_iter (sourcelist->priv->real_model, iter, sp->path);
	}

	gtk_tree_path_free (sp->path);
	g_free (sp);
	sp = NULL;

	return ret;
}

static gboolean
rb_sourcelist_visible_source_to_iter (RBSourceList *sourcelist,
				      RBSource     *source,
				      GtkTreeIter  *iter)
{
	SourcePath *sp;
	gboolean    ret;

	ret = FALSE;
	sp = g_new0 (SourcePath, 1);
	sp->source = source;

	gtk_tree_model_foreach (sourcelist->priv->filter_model, (GtkTreeModelForeachFunc) match_source_to_iter, sp);

	if (sp->path) {
		ret = gtk_tree_model_get_iter (sourcelist->priv->filter_model, iter, sp->path);
	}

	gtk_tree_path_free (sp->path);
	g_free (sp);
	sp = NULL;

	return ret;
}

/**
 * rb_sourcelist_edit_source_name:
 * @sourcelist: the #RBSourceList
 * @source: the #RBSource to edit
 *
 * Initiates editing of the name of the specified source.  The row for the source 
 * is selected and given input focus, allowing the user to edit the name.
 * source_name_edited_cb is called when the user finishes editing.
 */
void
rb_sourcelist_edit_source_name (RBSourceList *sourcelist,
				RBSource     *source)
{
	GtkTreeIter  iter;
	GtkTreePath *path;

	g_assert (rb_sourcelist_visible_source_to_iter (sourcelist, source, &iter));
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (sourcelist->priv->filter_model),
					&iter);
	gtk_tree_view_expand_to_path (GTK_TREE_VIEW (sourcelist->priv->treeview), path);

	/* Make cell editable just for the moment.
	   We'll turn it off once editing is done. */
	g_object_set (sourcelist->priv->title_renderer, "editable", TRUE, NULL);

	gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sourcelist->priv->treeview),
					  path, sourcelist->priv->main_column,
					  sourcelist->priv->title_renderer,
					  TRUE);

	gtk_tree_path_free (path);
}

static void
set_source_playing (RBSourceList *sourcelist,
		    RBSource     *source,
		    gboolean      playing)
{
	GtkTreeIter iter;
	g_assert (rb_sourcelist_source_to_iter (sourcelist, source, &iter));
	gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
			    RB_SOURCELIST_MODEL_COLUMN_PLAYING, playing, -1);
}

/**
 * rb_sourcelist_set_playing_source:
 * @sourcelist: the #RBSourceList
 * @source: the new playing #RBSource
 *
 * Updates the source list with the new playing source.
 * The source list tracks which source is playing in order to display
 * the name of the playing source in bold type.
 */
void
rb_sourcelist_set_playing_source (RBSourceList *sourcelist,
				  RBSource     *source)
{
	if (sourcelist->priv->playing_source)
		set_source_playing (sourcelist, sourcelist->priv->playing_source, FALSE);

	sourcelist->priv->playing_source = source;
	if (source)
		set_source_playing (sourcelist, source, TRUE);
}

/**
 * rb_sourcelist_remove:
 * @sourcelist: the #RBSourceList
 * @source: the #RBSource being removed
 *
 * Removes a source from the source list.  Disconnects signal handlers,
 * removes the source from the underlying model, and updates the visibility
 * of the group containing the source.
 */
void
rb_sourcelist_remove (RBSourceList *sourcelist,
		      RBSource     *source)
{
	GtkTreeIter    iter;
	RBSourceGroup *group;

	g_assert (rb_sourcelist_source_to_iter (sourcelist, source, &iter));

	if (source == sourcelist->priv->playing_source) {
		rb_sourcelist_set_playing_source (sourcelist, NULL);
	}

	g_object_get (source,
		      "source-group", &group,
		      NULL);

	gtk_tree_store_remove (GTK_TREE_STORE (sourcelist->priv->real_model), &iter);
	g_signal_handlers_disconnect_by_func (source,
					      G_CALLBACK (name_notify_cb), sourcelist);
        g_signal_handlers_disconnect_by_func (source,
					      G_CALLBACK (visibility_notify_cb),
                                             sourcelist);

	if (group != NULL) {
		rb_sourcelist_group_update_visibility (sourcelist, group);
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

/**
 * rb_sourcelist_select:
 * @sourcelist: the #RBSourceList
 * @source: the #RBSource to select
 *
 * Selects the specified source in the source list.  This will result in the 'selected'
 * signal being emitted.
 */
void
rb_sourcelist_select (RBSourceList *sourcelist,
		      RBSource     *source)
{
	GtkTreeIter iter;

	g_assert (rb_sourcelist_visible_source_to_iter (sourcelist, source, &iter));
	gtk_tree_selection_select_iter (sourcelist->priv->selection, &iter);
}

static void
rb_sourcelist_selection_changed_cb (GtkTreeSelection *selection,
				    RBSourceList     *sourcelist)
{
	GtkTreeIter   iter;
	GtkTreeModel *cindy;
	gpointer      target = NULL;
	RBSource     *source;

	if (!gtk_tree_selection_get_selected (sourcelist->priv->selection,
					      &cindy, &iter))
		return;

	gtk_tree_model_get (cindy, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == NULL)
		return;

	g_return_if_fail (RB_IS_SOURCE (target));
	source = target;
	g_signal_emit (sourcelist, rb_sourcelist_signals[SELECTED], 0, source);

	g_object_unref (target);
}

static void
source_name_edited_cb (GtkCellRendererText *renderer,
		       const char          *pathstr,
		       const char          *text,
		       RBSourceList        *sourcelist)
{
	GtkTreePath *path;
	GtkTreeIter  iter;
	RBSource    *source;

	if (text[0] == '\0')
		return;

	path = gtk_tree_path_new_from_string (pathstr);

	g_return_if_fail (gtk_tree_model_get_iter (GTK_TREE_MODEL (sourcelist->priv->filter_model),
						   &iter, path));
	gtk_tree_model_get (sourcelist->priv->filter_model,
			    &iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);
	if (source == NULL)
		return;

	g_object_set (source, "name", text, NULL);

	gtk_tree_path_free (path);

	g_object_set (renderer, "editable", FALSE, NULL);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));

	g_object_unref (source);
}
