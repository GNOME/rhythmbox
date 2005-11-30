/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of widget to display RhythmDB properties
 *
 *  Copyright (C) 2005 Renato Filho <renato.filho@indt.org.br>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-simple-view.h"
#include "rb-tree-dnd.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

enum
{
	SHOW_POPUP,
	LAST_SIGNAL
};

static guint rb_simple_view_signals[LAST_SIGNAL] = { 0 };


struct RBSimpleViewPrivate
{
	int reserved;
};

#define RB_SIMPLE_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SIMPLE_VIEW, RBSimpleViewPrivate))


static void rb_simple_view_class_init 		(RBSimpleViewClass *klass);
static void rb_simple_view_init 		(RBSimpleView *view);
static void rb_simple_view_finalize 		(GObject *object);
static GObject * rb_simple_view_constructor 	(GType type, guint n_construct_properties,
					       	 GObjectConstructParam *construct_properties);
static gboolean rb_simple_view_popup_menu_cb 	(GtkTreeView *treeview,
					     	 RBSimpleView *view);
static gboolean rb_simple_view_button_press_cb 	(GtkTreeView *treeview,
					      	 GdkEventButton *event,
					      	 RBSimpleView *view);

G_DEFINE_TYPE (RBSimpleView, rb_simple_view, RB_TYPE_PROPERTY_VIEW)


static void
rb_simple_view_class_init (RBSimpleViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_simple_view_finalize;
	object_class->constructor = rb_simple_view_constructor;
	
	rb_simple_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSimpleViewClass, show_popup),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBSimpleViewPrivate));
}

static void
rb_simple_view_init (RBSimpleView *view)
{
	view->priv = RB_SIMPLE_VIEW_GET_PRIVATE (view);
}

static void
rb_simple_view_finalize (GObject *object)
{
	RBSimpleView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SIMPLE_VIEW (object));

	view = RB_SIMPLE_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	G_OBJECT_CLASS (rb_simple_view_parent_class)->finalize (object);
}

RBSimpleView *
rb_simple_view_new (RhythmDB *db, guint propid, const char *title)
{
	RBSimpleView *view;

	view = RB_SIMPLE_VIEW (g_object_new (RB_TYPE_SIMPLE_VIEW,
					       "hadjustment", NULL,
					       "vadjustment", NULL,
					       "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					       "vscrollbar_policy", GTK_POLICY_ALWAYS,
					       "shadow_type", GTK_SHADOW_IN,
					       "db", db,
					       "prop", propid,
					       "title", title,
					       "draggable", FALSE,
					       NULL));

	return view;
}


static GObject *
rb_simple_view_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBSimpleView *view;
	RBSimpleViewClass *klass;
	GtkWidget *treeview;

	klass = RB_SIMPLE_VIEW_CLASS (g_type_class_peek (RB_TYPE_SIMPLE_VIEW));

	view = RB_SIMPLE_VIEW (G_OBJECT_CLASS (rb_simple_view_parent_class)->
			constructor (type, n_construct_properties, construct_properties));
	
	treeview = rb_property_view_get_treeview ( RB_PROPERTY_VIEW (view));
	g_signal_connect_object (G_OBJECT (treeview),
				 "popup_menu",
				 G_CALLBACK (rb_simple_view_popup_menu_cb),
				 view,
				 0);
	
	g_signal_connect_object (G_OBJECT (treeview),
			         "button_press_event",
			         G_CALLBACK (rb_simple_view_button_press_cb),
			         view,
				 0);

	return G_OBJECT (view);
}

void		
rb_simple_view_append_column_custom 	(RBSimpleView *view,
		    			 GtkTreeViewColumn *column,
		    			 const char *title,
		    			 gpointer user_data)
{
	GtkWidget *treeview;
	gtk_tree_view_column_set_title (column, title);
	gtk_tree_view_column_set_reorderable (column, FALSE);

	rb_debug ("appending column: (%s)", title);

	treeview = rb_property_view_get_treeview (RB_PROPERTY_VIEW (view));
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_visible (column, TRUE);
}


static gboolean 
rb_simple_view_popup_menu_cb 	(GtkTreeView *treeview,
			     	 RBSimpleView *view)
{
	g_signal_emit (G_OBJECT (view), rb_simple_view_signals[SHOW_POPUP], 0);
	return TRUE;
}

static gboolean
rb_simple_view_button_press_cb (GtkTreeView *tree,
		 	        GdkEventButton *event,
			        RBSimpleView *view)
{

	if (event->button == 3) {
		GtkTreePath *path;
		GtkWidget *treeview;
		GtkTreeSelection *selection;

		treeview = rb_property_view_get_treeview (RB_PROPERTY_VIEW (view));
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

		gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview), event->x, event->y, &path, NULL, NULL, NULL);
		if (path == NULL) {
			gtk_tree_selection_unselect_all (selection);
		} else {
			GtkTreeModel *model;
			GtkTreeIter iter;
			const char *val;
			GList *lst = NULL;
			
			model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
			gtk_tree_model_get_iter (model, &iter, path);

			gtk_tree_model_get (model, &iter, 0, &val, -1);
			lst = g_list_prepend (lst, (gpointer) val);

			rb_property_view_set_selection (RB_PROPERTY_VIEW (view), lst);	
		}
		g_signal_emit (G_OBJECT (view), rb_simple_view_signals[SHOW_POPUP], 0);
		return TRUE;
	}

	return FALSE;
}


