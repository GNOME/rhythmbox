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

#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "rb-sourcelist.h"
#include "rb-tree-view-column.h"
#include "rb-cell-renderer-pixbuf.h"

#define RB_SOURCELIST_COLUMN_PIXBUF 0
#define RB_SOURCELIST_COLUMN_NAME 1
#define RB_SOURCELIST_COLUMN_SOURCE 2
#define RB_SOURCELIST_COLUMN_ATTRIBUTES 3

struct RBSourceListPriv
{
	GtkWidget *srclabel;
	
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

		rb_sourcelist_type = g_type_register_static (GTK_TYPE_VBOX, "RBSourceList",
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
	int width;
	PangoAttrList *titleattrs = pango_attr_list_new ();
	PangoAttribute *attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);

	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (titleattrs, attr);
		
	sourcelist->priv = g_new0 (RBSourceListPriv, 1);

	sourcelist->priv->srclabel = gtk_label_new (_("Sources"));
	gtk_label_set_justify (GTK_LABEL (sourcelist->priv->srclabel), GTK_JUSTIFY_CENTER);
	gtk_label_set_attributes (GTK_LABEL (sourcelist->priv->srclabel), titleattrs);

	sourcelist->priv->store = gtk_list_store_new (4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_POINTER, PANGO_TYPE_ATTR_LIST);
	
	sourcelist->priv->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (sourcelist->priv->store));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (sourcelist->priv->treeview), FALSE);

	gtk_box_pack_start (GTK_BOX (sourcelist), sourcelist->priv->srclabel, FALSE, FALSE, 0);

	/* Set up the pixbuf column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	gcolumn = (GtkTreeViewColumn *) gtk_tree_view_column_new_with_attributes ("ERROR", renderer,
										  "pixbuf", RB_SOURCELIST_COLUMN_PIXBUF,
										  NULL);
	gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &width, NULL);
	gtk_tree_view_column_set_fixed_width (gcolumn, width + 5);
	gtk_tree_view_column_set_clickable (gcolumn, FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), gcolumn);
	
	/* Set up the name column */
	renderer = gtk_cell_renderer_text_new ();
	gcolumn = (GtkTreeViewColumn *) gtk_tree_view_column_new_with_attributes ("SourceSource", renderer,
										  "text", RB_SOURCELIST_COLUMN_NAME,
										  "attributes", RB_SOURCELIST_COLUMN_ATTRIBUTES,
										  NULL);
	gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_clickable (gcolumn, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (sourcelist->priv->treeview), gcolumn);

	gtk_box_pack_start_defaults (GTK_BOX (sourcelist), sourcelist->priv->treeview);

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
	return g_object_new (RB_TYPE_SOURCELIST, NULL);
}

void
rb_sourcelist_append (RBSourceList *sourcelist,
		      RBSource *source)
{
	GtkTreeIter iter;
	PangoAttrList *largerattrs = pango_attr_list_new ();
	PangoAttribute *largerattr = pango_attr_scale_new ((PANGO_SCALE_MEDIUM + PANGO_SCALE_LARGE)/2.0);
	
	g_return_if_fail (RB_IS_SOURCELIST (sourcelist));
	g_return_if_fail (RB_IS_SOURCE (source));

	largerattr->start_index = 0;
	largerattr->end_index = G_MAXINT;
	pango_attr_list_insert (largerattrs, largerattr);

	gtk_list_store_append (sourcelist->priv->store, &iter);

	gtk_list_store_set (sourcelist->priv->store, &iter,
			    RB_SOURCELIST_COLUMN_PIXBUF, rb_source_get_pixbuf (source),
			    RB_SOURCELIST_COLUMN_NAME, rb_source_get_description (source),
			    RB_SOURCELIST_COLUMN_SOURCE, source,
			    RB_SOURCELIST_COLUMN_ATTRIBUTES, largerattrs,
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
