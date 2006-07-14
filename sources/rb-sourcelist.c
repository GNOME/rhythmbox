/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * arch-tag: Implementation of main "Sources" display widget
 *
 * Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "rb-tree-dnd.h"
#include "rb-util.h"

struct RBSourceListPrivate
{
	GtkWidget *treeview;
	GtkCellRenderer *title_renderer;

	GtkTreeModel *real_model;
	GtkTreeModel *filter_model;
	GtkTreeSelection *selection;

	RBSource *playing_source;
	int child_source_count;
	GtkTreeViewColumn *hidden_column;
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
static void drop_received_cb (RBSourceListModel *model, RBSource *target, GtkTreeViewDropPosition pos,
			      GtkSelectionData *data, RBSourceList *sourcelist);
static void row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
			      GtkTreeViewColumn *column, RBSourceList *sourcelist);
static gboolean button_press_cb (GtkTreeView *treeview,
				 GdkEventButton *event,
				 RBSourceList *sourcelist);
static gboolean key_release_cb (GtkTreeView *treeview,
				GdkEventKey *event,
				RBSourceList *sourcelist);
static gboolean popup_menu_cb (GtkTreeView *treeview, RBSourceList *sourcelist);
static void name_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data);
static void visibility_notify_cb (GObject *obj, GParamSpec *pspec,
				  gpointer data);
static void icon_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data);
static void source_name_edited_cb (GtkCellRendererText *renderer, const char *pathstr,
				   const char *text, RBSourceList *sourcelist);
static void rb_sourcelist_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						GtkTreeModel *tree_model, GtkTreeIter *iter,
						RBSourceList *sourcelist);
static void rb_sourcelist_update_expander_visibility (RBSourceList *sourcelist);

static guint rb_sourcelist_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSourceList, rb_sourcelist, GTK_TYPE_SCROLLED_WINDOW)

static void
rb_sourcelist_class_init (RBSourceListClass *class)
{
	GObjectClass   *o_class;
	GtkObjectClass *object_class;

	o_class = (GObjectClass *) class;
	object_class = (GtkObjectClass *) class;

	o_class->finalize = rb_sourcelist_finalize;
	o_class->set_property = rb_sourcelist_set_property;
	o_class->get_property = rb_sourcelist_get_property;

	g_object_class_install_property (o_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (o_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "GtkTreeModel",
							      "GtkTreeModel object",
							      GTK_TYPE_TREE_MODEL,
							      G_PARAM_READABLE));
	rb_sourcelist_signals[SELECTED] =
		g_signal_new ("selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

	rb_sourcelist_signals[DROP_RECEIVED] =
		g_signal_new ("drop_received",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, drop_received),
			      NULL, NULL,
			      gtk_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	rb_sourcelist_signals[SOURCE_ACTIVATED] =
		g_signal_new ("source_activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListClass, source_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

	rb_sourcelist_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
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
rb_sourcelist_init (RBSourceList *sourcelist)
{
	GtkCellRenderer *renderer;

	sourcelist->priv = RB_SOURCELIST_GET_PRIVATE (sourcelist);

	sourcelist->priv->filter_model = rb_sourcelist_model_new ();
	sourcelist->priv->real_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (sourcelist->priv->filter_model));

	g_signal_connect_object (G_OBJECT (sourcelist->priv->filter_model),
				 "drop_received",
				 G_CALLBACK (drop_received_cb),
				 sourcelist, 0);

	sourcelist->priv->treeview = gtk_tree_view_new_with_model (sourcelist->priv->filter_model);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (sourcelist->priv->treeview), FALSE);
	rb_sourcelist_model_set_dnd_targets (RB_SOURCELIST_MODEL (sourcelist->priv->filter_model),
					     GTK_TREE_VIEW (sourcelist->priv->treeview));
	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (sourcelist->priv->treeview),
					      (GtkTreeViewRowSeparatorFunc) rb_sourcelist_model_row_is_separator,
					      sourcelist->priv->filter_model, NULL);

	g_signal_connect_object (G_OBJECT (sourcelist->priv->treeview),
				 "row_activated",
				 G_CALLBACK (row_activated_cb),
				 sourcelist, 0);

	g_signal_connect_object (G_OBJECT (sourcelist->priv->treeview),
				 "button_press_event",
				 G_CALLBACK (button_press_cb),
				 sourcelist, 0);
	g_signal_connect_object (G_OBJECT (sourcelist->priv->treeview),
				 "key_release_event",
				 G_CALLBACK (key_release_cb),
				 sourcelist, 0);

	g_signal_connect_object (G_OBJECT (sourcelist->priv->treeview),
				 "popup_menu",
				 G_CALLBACK (popup_menu_cb),
				 sourcelist, 0);

	sourcelist->priv->hidden_column = gtk_tree_view_column_new ();
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview),
				     sourcelist->priv->hidden_column);
	gtk_tree_view_column_set_visible (sourcelist->priv->hidden_column, FALSE);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (sourcelist->priv->treeview),
				     sourcelist->priv->hidden_column);

	sourcelist->priv->main_column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (sourcelist->priv->main_column, _("S_ource"));
	gtk_tree_view_column_set_clickable (sourcelist->priv->main_column, FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), sourcelist->priv->main_column);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (sourcelist->priv->main_column, renderer,
				             "pixbuf", RB_SOURCELIST_MODEL_COLUMN_PIXBUF,
					     NULL);

	/* Set up the name column */
	sourcelist->priv->title_renderer = renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (sourcelist->priv->title_renderer),
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	gtk_tree_view_column_pack_start (sourcelist->priv->main_column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (sourcelist->priv->main_column, renderer,
						 (GtkTreeCellDataFunc)
						 rb_sourcelist_title_cell_data_func,
						 sourcelist, NULL);
	g_signal_connect_object (renderer, "edited", G_CALLBACK (source_name_edited_cb), sourcelist, 0);

	sourcelist->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sourcelist->priv->treeview));
	g_signal_connect_object (G_OBJECT (sourcelist->priv->selection),
			         "changed",
			         G_CALLBACK (rb_sourcelist_selection_changed_cb),
			         sourcelist,
				 0);

	rb_sourcelist_update_expander_visibility (sourcelist);
}

static void
rb_sourcelist_finalize (GObject *object)
{
	G_OBJECT_CLASS (rb_sourcelist_parent_class)->finalize (object);
}

static void
rb_sourcelist_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
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
rb_sourcelist_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
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

	gtk_container_add (GTK_CONTAINER (sourcelist),
			   sourcelist->priv->treeview);

	return GTK_WIDGET (sourcelist);
}

void
rb_sourcelist_append (RBSourceList *sourcelist,
		      RBSource *source,
		      RBSource *parent)
{
	GtkTreeIter iter;
	PangoAttrList *attrs = pango_attr_list_new ();
	const char *name;
	GdkPixbuf *pixbuf;
	gboolean visible;

	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_get (G_OBJECT (source), "name", &name, NULL);
	g_object_get (G_OBJECT (source), "icon", &pixbuf, NULL);
	g_object_get (G_OBJECT (source), "visibility", &visible, NULL);

	if (parent) {
		GtkTreeIter parent_iter;

		rb_debug ("inserting source %p with parent", source);
		g_assert (rb_sourcelist_source_to_iter (sourcelist, parent, &parent_iter));
		gtk_tree_store_append (GTK_TREE_STORE (sourcelist->priv->real_model), &iter, &parent_iter);

		if (visible) {
			sourcelist->priv->child_source_count++;
			rb_sourcelist_update_expander_visibility (sourcelist);
		}
	} else {
		GtkTreePath *group_path;
		GtkTreePath *prev_group_path = NULL;
		RBSourceListGroup group;
		GtkTreeIter group_iter;

		/* get the marker rows before and after the group for this source */
		g_object_get (G_OBJECT (source), "sourcelist-group", &group, NULL);
		group_path = rb_sourcelist_model_get_group_path (RB_SOURCELIST_MODEL (sourcelist->priv->filter_model),
								 group);
		g_assert (group_path);

		if (group > 0) {
			/* there's no marker row before the fixed source group, but we
			 * don't need one anyway.
			 */
			prev_group_path = rb_sourcelist_model_get_group_path (RB_SOURCELIST_MODEL (sourcelist->priv->filter_model),
									      group-1);
			g_assert (prev_group_path);
		}

		/* find the location to insert the source */
		if (group == RB_SOURCELIST_GROUP_TRANSIENT) {
			char *check_name = NULL;

			rb_debug ("inserting source %p in group %d in sorted order", source, group);
			g_assert (prev_group_path);

			gtk_tree_model_get_iter (sourcelist->priv->real_model, &group_iter, prev_group_path);
			do {
				g_free (check_name);

				if (!gtk_tree_model_iter_next (sourcelist->priv->real_model, &group_iter))
					break;

				gtk_tree_model_get (sourcelist->priv->real_model,
						    &group_iter,
						    RB_SOURCELIST_MODEL_COLUMN_NAME, &check_name,
						    -1);

			} while (check_name && (strlen (check_name)) > 0 && (g_utf8_collate (name, check_name) > 0));
			g_free (check_name);

		} else {
			rb_debug ("inserting source %p in group %d", source, group);
			gtk_tree_model_get_iter (sourcelist->priv->real_model, &group_iter, group_path);
		}
		gtk_tree_store_insert_before (GTK_TREE_STORE (sourcelist->priv->real_model),
					      &iter, NULL, &group_iter);
	}

	gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
			    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, pixbuf,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, name,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, source,
			    RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES, attrs,
			    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, visible,
			    -1);

	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}

	g_signal_connect_object (G_OBJECT (source), "notify::name", G_CALLBACK (name_notify_cb), sourcelist, 0);
	g_signal_connect_object (G_OBJECT (source), "notify::visibility", G_CALLBACK (visibility_notify_cb), sourcelist, 0);
	g_signal_connect_object (G_OBJECT (source), "notify::icon", G_CALLBACK (icon_notify_cb), sourcelist, 0);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

typedef struct _SourcePath {
	RBSource *source;
	GtkTreePath *path;
} SourcePath;

static gboolean
match_source_to_iter (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
		      SourcePath *sp)
{
	RBSource *target = NULL;

	gtk_tree_model_get (model, iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == sp->source) {
		sp->path = gtk_tree_path_copy (path);
		return TRUE;
	}

	return FALSE;
}

static gboolean
rb_sourcelist_source_to_iter (RBSourceList *sourcelist, RBSource *source,
			      GtkTreeIter *iter)
{
	SourcePath *sp = g_new0 (SourcePath,1);
	gboolean ret = FALSE;

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
rb_sourcelist_visible_source_to_iter (RBSourceList *sourcelist, RBSource *source,
				      GtkTreeIter *iter)
{
	SourcePath *sp = g_new0 (SourcePath,1);
	gboolean ret = FALSE;

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

void
rb_sourcelist_edit_source_name (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	g_assert (rb_sourcelist_visible_source_to_iter (sourcelist, source, &iter));
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (sourcelist->priv->filter_model),
					&iter);

	/* Make cell editable just for the moment.
	   We'll turn it off once editing is done. */
	g_object_set (G_OBJECT (sourcelist->priv->title_renderer), "editable", TRUE, NULL);

	gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sourcelist->priv->treeview),
					  path, sourcelist->priv->main_column,
					  sourcelist->priv->title_renderer,
					  TRUE);

	gtk_tree_path_free (path);
}

static void set_source_playing (RBSourceList *sourcelist, RBSource *source, gboolean playing)
{
	GtkTreeIter iter;
	g_assert (rb_sourcelist_source_to_iter (sourcelist, source, &iter));
	gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
			    RB_SOURCELIST_MODEL_COLUMN_PLAYING, playing, -1);
}

void
rb_sourcelist_set_playing_source (RBSourceList *sourcelist, RBSource *source)
{
	if (sourcelist->priv->playing_source)
		set_source_playing (sourcelist, sourcelist->priv->playing_source, FALSE);

	sourcelist->priv->playing_source = source;
	if (source)
		set_source_playing (sourcelist, source, TRUE);
}

void
rb_sourcelist_remove (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	g_assert (rb_sourcelist_source_to_iter (sourcelist, source, &iter));

	if (gtk_tree_store_iter_depth (GTK_TREE_STORE (sourcelist->priv->real_model), &iter) > 0) {
		gboolean visible;

		g_object_get (G_OBJECT (source), "visibility", &visible, NULL);
		if (visible)
			sourcelist->priv->child_source_count--;
		rb_sourcelist_update_expander_visibility (sourcelist);
	}

	gtk_tree_store_remove (GTK_TREE_STORE (sourcelist->priv->real_model), &iter);
	g_signal_handlers_disconnect_by_func (G_OBJECT (source),
					      G_CALLBACK (name_notify_cb), sourcelist);
        g_signal_handlers_disconnect_by_func (G_OBJECT (source),
					      G_CALLBACK (visibility_notify_cb),
                                             sourcelist);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

void
rb_sourcelist_select (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	g_assert (rb_sourcelist_visible_source_to_iter (sourcelist, source, &iter));
	gtk_tree_selection_select_iter (sourcelist->priv->selection, &iter);
}

static void
rb_sourcelist_selection_changed_cb (GtkTreeSelection *selection,
				    RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	GtkTreeModel *cindy;
	gpointer target = NULL;
	RBSource *source;

	if (!gtk_tree_selection_get_selected (sourcelist->priv->selection,
					      &cindy, &iter))
		return;

	gtk_tree_model_get (cindy, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == NULL)
		return;
	g_return_if_fail (RB_IS_SOURCE (target));
	source = target;
	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SELECTED], 0, source);
}

static void
drop_received_cb (RBSourceListModel *model, RBSource *target, GtkTreeViewDropPosition pos,
		  GtkSelectionData *data, RBSourceList *sourcelist)
{
	rb_debug ("drop recieved");
	/* Proxy the signal. */
	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[DROP_RECEIVED], 0, target, data);
}

static void
row_activated_cb (GtkTreeView *treeview,
		  GtkTreePath *path,
		  GtkTreeViewColumn *column,
		  RBSourceList *sourcelist)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	RBSource *target;

	model = gtk_tree_view_get_model (treeview);
	g_return_if_fail (gtk_tree_model_get_iter (model, &iter, path));
	gtk_tree_model_get (model, &iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);

	if (target)
		g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SOURCE_ACTIVATED], 0, target);
}

static gboolean
emit_show_popup (GtkTreeView *treeview,
		 RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	RBSource *target;
	gboolean ret;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (treeview),
					      NULL, &iter))
		return FALSE;

	gtk_tree_model_get (sourcelist->priv->filter_model, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == NULL)
		return FALSE;
	g_return_val_if_fail (RB_IS_SOURCE (target), FALSE);

	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SHOW_POPUP], 0, target, &ret);

	return ret;
}

static gboolean
button_press_cb (GtkTreeView *treeview,
		 GdkEventButton *event,
		 RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (event->button != 3)
		return FALSE;

	if (gtk_tree_view_get_path_at_pos (treeview, event->x, event->y,
					   &path, NULL, NULL, NULL) == FALSE) {
		/* pointer is over empty space */
		GtkUIManager *uimanager;
		g_object_get (G_OBJECT (sourcelist->priv->shell), "ui-manager", &uimanager, NULL);
		rb_gtk_action_popup_menu (uimanager, "/SourceListPopup");
		g_object_unref (G_OBJECT (uimanager));
		return TRUE;
	}

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (sourcelist->priv->filter_model), &iter, path))
		gtk_tree_selection_select_iter (gtk_tree_view_get_selection (treeview), &iter);

	return emit_show_popup (treeview, sourcelist);
}

static gboolean
key_release_cb (GtkTreeView *treeview,
		GdkEventKey *event,
		RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	RBSource *target;

	/* F2 = rename playlist */
	if (event->keyval != GDK_F2)
		return FALSE;

	if (!gtk_tree_selection_get_selected (sourcelist->priv->selection, NULL, &iter))
		return FALSE;

	gtk_tree_model_get (sourcelist->priv->filter_model, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	if (target == NULL)
		return FALSE;

	g_return_val_if_fail (RB_IS_SOURCE (target), FALSE);
	if (rb_source_can_rename (target)) {
		rb_sourcelist_edit_source_name (sourcelist, target);
		return TRUE;
	}

	return FALSE;
}

static gboolean
popup_menu_cb (GtkTreeView *treeview,
	       RBSourceList *sourcelist)
{
	return emit_show_popup (treeview, sourcelist);
}

static void
name_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource *source = RB_SOURCE (obj);
	GtkTreeIter iter;
	gchar *name;

	if (rb_sourcelist_source_to_iter (sourcelist, source, &iter)) {
		g_object_get (obj, "name", &name, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model),
				    &iter,
				    RB_SOURCELIST_MODEL_COLUMN_NAME, name, -1);
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
visibility_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource *source = RB_SOURCE (obj);
	GtkTreeIter iter;
	gboolean old_visibility;
	gboolean new_visibility;
	char *name;

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
			sourcelist->priv->child_source_count += new_visibility ? 1 : -1;
			rb_sourcelist_update_expander_visibility (sourcelist);

			gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, new_visibility,
					    -1);
		}
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
icon_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource *source = RB_SOURCE (obj);
	GtkTreeIter iter;
	GdkPixbuf *pixbuf;

	if (rb_sourcelist_source_to_iter (sourcelist, source, &iter)) {
		g_object_get (obj, "icon", &pixbuf, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (sourcelist->priv->real_model),
				    &iter,
				    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, pixbuf, -1);
		if (pixbuf != NULL) {
			g_object_unref (pixbuf);
		}
	}

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
rb_sourcelist_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				    GtkTreeModel *tree_model, GtkTreeIter *iter,
				    RBSourceList *sourcelist)
{
	char *str;
	gboolean playing;

	gtk_tree_model_get (GTK_TREE_MODEL (sourcelist->priv->filter_model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &str,
			    RB_SOURCELIST_MODEL_COLUMN_PLAYING, &playing,
			    -1);

	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", playing ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);

	g_free (str);
}

static void
source_name_edited_cb (GtkCellRendererText *renderer, const char *pathstr,
		       const char *text, RBSourceList *sourcelist)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	RBSource *source;

	if (text[0] == '\0')
		return;

	path = gtk_tree_path_new_from_string (pathstr);

	g_return_if_fail (gtk_tree_model_get_iter (GTK_TREE_MODEL (sourcelist->priv->filter_model),
						   &iter, path));
	gtk_tree_model_get (sourcelist->priv->filter_model,
			    &iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);
	if (source == NULL)
		return;

	g_object_set (G_OBJECT (source), "name", text, NULL);

	gtk_tree_path_free (path);

	g_object_set (G_OBJECT (renderer), "editable", FALSE, NULL);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (sourcelist->priv->treeview));
}

static void
rb_sourcelist_update_expander_visibility (RBSourceList *sourcelist)
{
	gboolean visible;
	GtkTreeViewColumn *column;

	g_assert (sourcelist->priv->child_source_count >= 0);

	visible = (sourcelist->priv->child_source_count > 0);
	if (visible)
		column = sourcelist->priv->main_column;
	else
		column = sourcelist->priv->hidden_column;

	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (sourcelist->priv->treeview),
					   column);
}
