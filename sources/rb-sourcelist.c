/*
 * arch-tag: Implementation of main "Sources" display widget
 *
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <libgnome/gnome-i18n.h>
#include <unistd.h>
#include <string.h>

#include "rb-sourcelist.h"
#include "rb-sourcelist-model.h"
#include "rb-debug.h"
#include "rb-marshal.h"
#include "rb-tree-view-column.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-tree-dnd.h"

struct RBSourceListPriv
{
	GtkWidget *treeview;
	GtkCellRenderer *title_renderer;

	GtkTreeModel *model;
	GtkTreeSelection *selection;
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
static void rb_sourcelist_selection_changed_cb (GtkTreeSelection *selection,
						RBSourceList *sourcelist);
static void drop_received_cb (RBSourceListModel *model, RBSource *target, GtkTreeViewDropPosition pos,
			      GtkSelectionData *data, RBSourceList *sourcelist);
static void row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
			      GtkTreeViewColumn *column, RBSourceList *sourcelist);
static gboolean button_press_cb (GtkTreeView *treeview,
				 GdkEventButton *event,
				 RBSourceList *sourcelist);
static void name_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data);
static void source_name_edited_cb (GtkCellRendererText *renderer, const char *pathstr,
				   const char *text, RBSourceList *sourcelist);
static void rb_sourcelist_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
						GtkTreeModel *tree_model, GtkTreeIter *iter,
						RBSourceList *sourcelist);

static GtkVBoxClass *parent_class = NULL;

static guint rb_sourcelist_signals[LAST_SIGNAL] = { 0 };

GType
rb_sourcelist_get_type (void)
{
	static GType rb_sourcelist_type = 0;

	if (!rb_sourcelist_type)
	{
		static const GTypeInfo rb_sourcelist_info = {
			sizeof (RBSourceListClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) rb_sourcelist_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (RBSourceList),
			0,              /* n_preallocs */
			(GInstanceInitFunc) rb_sourcelist_init
		};

		rb_sourcelist_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW, "RBSourceList",
							     &rb_sourcelist_info, 0);
	}
	
	return rb_sourcelist_type;
}

static void
rb_sourcelist_class_init (RBSourceListClass *class)
{
	GObjectClass   *o_class;
	GtkObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);

	o_class = (GObjectClass *) class;
	object_class = (GtkObjectClass *) class;

	o_class->finalize = rb_sourcelist_finalize;

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
			      G_TYPE_POINTER);
}

static void
rb_sourcelist_init (RBSourceList *sourcelist)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *gcolumn;

	sourcelist->priv = g_new0 (RBSourceListPriv, 1);

	sourcelist->priv->model = rb_sourcelist_model_new ();

	g_signal_connect (G_OBJECT (sourcelist->priv->model),
			  "drop_received",
			  G_CALLBACK (drop_received_cb),
			  sourcelist);

	sourcelist->priv->treeview = gtk_tree_view_new_with_model (sourcelist->priv->model);

	g_signal_connect (G_OBJECT (sourcelist->priv->treeview),
			  "row_activated",
			  G_CALLBACK (row_activated_cb),
			  sourcelist);

	g_signal_connect (G_OBJECT (sourcelist->priv->treeview),
			  "button_press_event",
			  G_CALLBACK (button_press_cb),
			  sourcelist);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gcolumn = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (gcolumn, _("_Source"));
	gtk_tree_view_column_set_clickable (gcolumn, FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), gcolumn);

	gtk_tree_view_column_pack_start (gcolumn, renderer, FALSE);
	gtk_tree_view_column_set_attributes (gcolumn, renderer,
				             "pixbuf", RB_SOURCELIST_MODEL_COLUMN_PIXBUF,
					     NULL);

	/* Set up the name column */
	sourcelist->priv->title_renderer = renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (gcolumn, renderer,
						 (GtkTreeCellDataFunc)
						 rb_sourcelist_title_cell_data_func,
						 sourcelist, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (source_name_edited_cb), sourcelist);

	sourcelist->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sourcelist->priv->treeview));
	g_signal_connect_object (G_OBJECT (sourcelist->priv->selection),
			         "changed",
			         G_CALLBACK (rb_sourcelist_selection_changed_cb),
			         sourcelist,
				 0);
}

static void
rb_sourcelist_finalize (GObject *object)
{
	RBSourceList *sourcelist = RB_SOURCELIST (object);

	g_free (sourcelist->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

GtkWidget *
rb_sourcelist_new (void)
{
	RBSourceList *sourcelist;

	sourcelist = RB_SOURCELIST (g_object_new (RB_TYPE_SOURCELIST,
						  "hadjustment", NULL,
						  "vadjustment", NULL,
					          "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					          "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
					          "shadow_type", GTK_SHADOW_IN,
					          NULL));

	gtk_container_add (GTK_CONTAINER (sourcelist),
			   sourcelist->priv->treeview);

	return GTK_WIDGET (sourcelist);
}

void
rb_sourcelist_append (RBSourceList *sourcelist,
		      RBSource *source)
{
	GtkTreeIter iter;
	PangoAttrList *attrs = pango_attr_list_new ();
	const char *name;

	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_get (G_OBJECT (source), "name", &name, NULL);

	gtk_list_store_append (GTK_LIST_STORE (sourcelist->priv->model), &iter);

	gtk_list_store_set (GTK_LIST_STORE (sourcelist->priv->model), &iter,
			    RB_SOURCELIST_MODEL_COLUMN_PIXBUF, rb_source_get_pixbuf (source),
			    RB_SOURCELIST_MODEL_COLUMN_NAME, name,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, source,
			    RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES, attrs,
			    -1);

	g_signal_connect_object (G_OBJECT (source), "notify", G_CALLBACK (name_notify_cb), sourcelist, 0);
}

void
rb_sourcelist_edit_source_name (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first (sourcelist->priv->model, &iter);
	do {
		gpointer target = NULL;
		gtk_tree_model_get (sourcelist->priv->model, &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		if (source == target) {
			GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (sourcelist->priv->model),
								     &iter);
			GtkTreeViewColumn *col;

			col = gtk_tree_view_get_column (GTK_TREE_VIEW (sourcelist->priv->treeview), 0);
			gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sourcelist->priv->treeview),
							  path, col, sourcelist->priv->title_renderer,
							  TRUE);

			gtk_tree_path_free (path);
			return;
		}
	} while (gtk_tree_model_iter_next (sourcelist->priv->model, &iter));
	g_assert_not_reached ();
}

void
rb_sourcelist_remove (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first (sourcelist->priv->model, &iter);
	do {
		gpointer target = NULL;
		gtk_tree_model_get (sourcelist->priv->model, &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		if (source == target) {
			gtk_list_store_remove (GTK_LIST_STORE (sourcelist->priv->model), &iter);
			g_signal_handlers_disconnect_by_func (G_OBJECT (source), G_CALLBACK (name_notify_cb), sourcelist);
			return;
		}
	} while (gtk_tree_model_iter_next (sourcelist->priv->model, &iter));
	g_assert_not_reached ();
}

void
rb_sourcelist_select (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first (sourcelist->priv->model, &iter);
	do {
		gpointer target = NULL;
		gtk_tree_model_get (sourcelist->priv->model, &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		if (source == target) {
			gtk_tree_selection_select_iter (sourcelist->priv->selection, &iter);
			return;
		}
	} while (gtk_tree_model_iter_next (sourcelist->priv->model, &iter));
	g_assert_not_reached ();
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
	g_return_if_fail (RB_IS_SOURCE (target));
	source = target;
	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SELECTED], 0, source);
}

void
rb_sourcelist_set_dnd_targets (RBSourceList *sourcelist,
			       const GtkTargetEntry *targets,
			       int n_targets)
{
	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));

	rb_tree_dnd_add_drag_dest_support (GTK_TREE_VIEW (sourcelist->priv->treeview),
					   RB_TREE_DEST_EMPTY_VIEW_DROP,
					   targets, n_targets,
					   GDK_ACTION_LINK);
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
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);

	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SOURCE_ACTIVATED], 0, target);
}

static gboolean
button_press_cb (GtkTreeView *treeview,
		 GdkEventButton *event,
		 RBSourceList *sourcelist)
{
	GtkTreeIter iter;
	RBSource *target;
	gboolean ret;

	if (event->button != 3)
		return FALSE;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (treeview),
					      NULL, &iter))
		return FALSE;

	gtk_tree_model_get (sourcelist->priv->model, &iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
	g_return_val_if_fail (RB_IS_SOURCE (target), FALSE);

	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SHOW_POPUP], 0, target, &ret);

	return ret;
}

static void
name_notify_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
	RBSourceList *sourcelist = RB_SOURCELIST (data);
	RBSource *source = RB_SOURCE (obj);
	GtkTreeIter iter;

	if (strcmp (g_param_spec_get_name (pspec), "name"))
		return;

	gtk_tree_model_get_iter_first (sourcelist->priv->model, &iter);
	do {
		gpointer target = NULL;
		gtk_tree_model_get (sourcelist->priv->model, &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		if (source == target) {
			const char *name;

			g_object_get (obj, "name", &name, NULL);
			gtk_list_store_set (GTK_LIST_STORE (sourcelist->priv->model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_NAME, name, -1);
			return;
		}
	} while (gtk_tree_model_iter_next (sourcelist->priv->model, &iter));
	g_assert_not_reached ();
}

static void
rb_sourcelist_title_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				    GtkTreeModel *tree_model, GtkTreeIter *iter,
				    RBSourceList *sourcelist)
{
	RBSource *source;
	char *str;

	gtk_tree_model_get (GTK_TREE_MODEL (sourcelist->priv->model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_NAME, &str,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

	if (gtk_tree_selection_iter_is_selected (sourcelist->priv->selection, iter) != FALSE)
		g_object_set (G_OBJECT (renderer), "text", str,
			      "editable", rb_source_can_rename (source),
			      NULL);
	else
		g_object_set (G_OBJECT (renderer), "text", str, NULL);

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
	
	gtk_tree_model_get_iter (GTK_TREE_MODEL (sourcelist->priv->model), &iter, path);
	gtk_tree_model_get (sourcelist->priv->model,
			    &iter, RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);
	g_object_set (G_OBJECT (source), "name", text, NULL);

	gtk_tree_path_free (path);
}

