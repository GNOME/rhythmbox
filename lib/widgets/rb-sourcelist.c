/*
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
 * $Id$
 */

#include "config.h"

#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <unistd.h>
#include <string.h>

#include "rb-sourcelist.h"
#include "rb-tree-view-column.h"
#include "rb-cell-renderer-pixbuf.h"

#define RB_SOURCELIST_COLUMN_PIXBUF 0
#define RB_SOURCELIST_COLUMN_NAME 1
#define RB_SOURCELIST_COLUMN_SOURCE 2
#define RB_SOURCELIST_COLUMN_ATTRIBUTES 3

struct RBSourceListPriv
{
	GtkWidget *treeview;

	GtkListStore *store;
	GtkTreeSelection *selection;
};

enum
{
	SELECTED,
	LAST_SIGNAL
};

static void rb_sourcelist_class_init (RBSourceListClass *klass);
static void rb_sourcelist_init (RBSourceList *sourcelist);
static void rb_sourcelist_finalize (GObject *object);
static void rb_sourcelist_selection_changed_cb (GtkTreeSelection *selection,
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
}

static void
rb_sourcelist_init (RBSourceList *sourcelist)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *gcolumn;

	sourcelist->priv = g_new0 (RBSourceListPriv, 1);

	sourcelist->priv->store = gtk_list_store_new (4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_POINTER, PANGO_TYPE_ATTR_LIST);

	sourcelist->priv->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (sourcelist->priv->store));

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gcolumn = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (gcolumn, _("_Source"));
	gtk_tree_view_column_set_clickable (gcolumn, FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), gcolumn);

	gtk_tree_view_column_pack_start (gcolumn, renderer, FALSE);
	gtk_tree_view_column_set_attributes (gcolumn, renderer,
				             "pixbuf", RB_SOURCELIST_COLUMN_PIXBUF,
					     NULL);

	/* Set up the name column */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
	gtk_tree_view_column_set_attributes (gcolumn, renderer,
					     "text", RB_SOURCELIST_COLUMN_NAME,
					     "attributes", RB_SOURCELIST_COLUMN_ATTRIBUTES,
					     NULL);

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

	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));
	g_return_if_fail (RB_IS_SOURCE (source));

	gtk_list_store_append (sourcelist->priv->store, &iter);

	gtk_list_store_set (sourcelist->priv->store, &iter,
			    RB_SOURCELIST_COLUMN_PIXBUF, rb_source_get_pixbuf (source),
			    RB_SOURCELIST_COLUMN_NAME, rb_source_get_description (source),
			    RB_SOURCELIST_COLUMN_SOURCE, source,
			    RB_SOURCELIST_COLUMN_ATTRIBUTES, attrs,
			    -1);
}

void
rb_sourcelist_select (RBSourceList *sourcelist, RBSource *source)
{
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sourcelist->priv->store),
				       &iter);
	do {
		gpointer target = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL (sourcelist->priv->store), &iter,
				    RB_SOURCELIST_COLUMN_SOURCE, &target, -1);
		if (source == target) {
			gtk_tree_selection_select_iter (sourcelist->priv->selection, &iter);
			return;
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (sourcelist->priv->store),
					   &iter));
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

	g_assert (gtk_tree_selection_get_selected (sourcelist->priv->selection,
						   &cindy, &iter));

	gtk_tree_model_get (cindy, &iter,
			    RB_SOURCELIST_COLUMN_SOURCE, &target, -1);
	g_return_if_fail (RB_IS_SOURCE (target));
	source = target;
	g_signal_emit (G_OBJECT (sourcelist), rb_sourcelist_signals[SELECTED], 0, source);
}
