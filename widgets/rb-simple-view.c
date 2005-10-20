/*
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

#include <gtk/gtktreeview.h>

#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtktooltips.h>
#include <gdk/gdkkeysyms.h>
#include <glib/ghash.h>
#include <glib/glist.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <string.h>
#include <stdlib.h>

#include "rb-simple-view.h"
#include "rb-tree-dnd.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

static const GtkTargetEntry rb_simple_view_drag_types[] = {{  "text/uri-list", 0, 0 }};

enum
{
	SHOW_POPUP,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_IS_DRAG_SOURCE,
	PROP_IS_DRAG_DEST
};

static GObjectClass *parent_class = NULL;

static guint rb_simple_view_signals[LAST_SIGNAL] = { 0 };


struct RBSimpleViewPrivate
{
	gboolean is_drag_source;
	gboolean is_drag_dest;
};


static void rb_simple_view_class_init 		(RBSimpleViewClass *klass);
static void rb_simple_view_init 		(RBSimpleView *view);
static void rb_simple_view_finalize 		(GObject *object);
static void rb_simple_view_set_property 	(GObject *object,
				       	 	 guint prop_id,
				       	 	 const GValue *value,
				       	 	 GParamSpec *pspec);
static void rb_simple_view_get_property 	(GObject *object,
				         	 guint prop_id,
				       	 	 GValue *value,
				       	 	 GParamSpec *pspec);
static GObject * rb_simple_view_constructor 	(GType type, guint n_construct_properties,
					       	 GObjectConstructParam *construct_properties);
static gboolean rb_simple_view_popup_menu_cb 	(GtkTreeView *treeview,
					     	 RBSimpleView *view);
static gboolean rb_simple_view_button_press_cb 	(GtkTreeView *treeview,
					      	 GdkEventButton *event,
					      	 RBSimpleView *view);


GType
rb_simple_view_get_type (void)
{
	static GType rb_simple_view_type = 0;

	if (rb_simple_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSimpleViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_simple_view_class_init,
			NULL,
			NULL,
			sizeof (RBSimpleView),
			0,
			(GInstanceInitFunc) rb_simple_view_init
		};
		
		rb_simple_view_type = g_type_register_static (RB_TYPE_PROPERTY_VIEW,
							      "RBSimpleView",
							       &our_info, 0);
	}

	return rb_simple_view_type;
}

static void
rb_simple_view_class_init (RBSimpleViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_simple_view_finalize;
	object_class->constructor = rb_simple_view_constructor;

	object_class->set_property = rb_simple_view_set_property;
	object_class->get_property = rb_simple_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_SOURCE,
					 g_param_spec_boolean ("is-drag-source",
							       "is drag source",
							       "whether or not this is a drag source",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	g_object_class_install_property (object_class,
					 PROP_IS_DRAG_DEST,
					 g_param_spec_boolean ("is-drag-dest",
							       "is drag dest",
							       "whether or not this is a drag dest",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	rb_simple_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSimpleViewClass, show_popup),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

}

static void
rb_simple_view_init (RBSimpleView *view)
{
	view->priv = g_new0 (RBSimpleViewPrivate, 1);
}

static void
rb_simple_view_finalize (GObject *object)
{
	RBSimpleView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SIMPLE_VIEW (object));

	view = RB_SIMPLE_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
rb_simple_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBSimpleView *view = RB_SIMPLE_VIEW (object);

	switch (prop_id)
	{
	case PROP_IS_DRAG_SOURCE:
		view->priv->is_drag_source = g_value_get_boolean (value);
		break;
	case PROP_IS_DRAG_DEST:
		view->priv->is_drag_dest = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_simple_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBSimpleView *view = RB_SIMPLE_VIEW (object);

	switch (prop_id)
	{
	case PROP_IS_DRAG_SOURCE:
		g_value_set_boolean (value, view->priv->is_drag_source);
		break;
	case PROP_IS_DRAG_DEST:
		g_value_set_boolean (value, view->priv->is_drag_dest);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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
					       "is-drag-source", FALSE,
					       "is-drag-dest", FALSE,
					       NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}


static GObject *
rb_simple_view_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBSimpleView *view;
	RBSimpleViewClass *klass;
	GObjectClass *parent_class; 
	GtkWidget *treeview;

	klass = RB_SIMPLE_VIEW_CLASS (g_type_class_peek (RB_TYPE_SIMPLE_VIEW));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	view = RB_SIMPLE_VIEW (parent_class->constructor (type, n_construct_properties,
							    construct_properties));
	
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

	if (view->priv->is_drag_source)
		rb_tree_dnd_add_drag_source_support (GTK_TREE_VIEW (treeview),
						     GDK_BUTTON1_MASK,
						     rb_simple_view_drag_types,
						     G_N_ELEMENTS (rb_simple_view_drag_types),
						     GDK_ACTION_COPY | GDK_ACTION_MOVE);
	if (view->priv->is_drag_dest)
		rb_tree_dnd_add_drag_dest_support (GTK_TREE_VIEW (treeview),
						   RB_TREE_DEST_CAN_DROP_BETWEEN | RB_TREE_DEST_EMPTY_VIEW_DROP,
						   rb_simple_view_drag_types,
						   G_N_ELEMENTS (rb_simple_view_drag_types),
						   GDK_ACTION_COPY | GDK_ACTION_MOVE);

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


void
rb_simple_view_enable_drag_source (RBSimpleView *view,
				   const GtkTargetEntry *targets,
				   int n_targets)
{
	GtkWidget *treeview;
	g_return_if_fail (view != NULL);

	treeview = rb_property_view_get_treeview (RB_PROPERTY_VIEW (view));
	rb_tree_dnd_add_drag_source_support (GTK_TREE_VIEW (treeview),
					     GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
					     targets, n_targets, GDK_ACTION_COPY);
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
			lst = g_list_append (lst, (gpointer) val);

			rb_property_view_set_selection (RB_PROPERTY_VIEW (view), lst);	
		}
		g_signal_emit (G_OBJECT (view), rb_simple_view_signals[SHOW_POPUP], 0);
		return TRUE;
	}

	return FALSE;
}


