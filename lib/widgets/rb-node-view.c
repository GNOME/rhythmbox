/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 *
 *  FIXME "Add/Remove columns" dialog
 */

#include <gtk/gtktreeview.h>
#include <gtk/gtktreemodelsort.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <libxml/tree.h>
#include <stdlib.h>

#include "gtktreemodelfilter.h"
#include "rb-tree-model-node.h"
#include "rb-node-view.h"
#include "rb-dialog.h"

static void rb_node_view_class_init (RBNodeViewClass *klass);
static void rb_node_view_init (RBNodeView *view);
static void rb_node_view_finalize (GObject *object);
static void rb_node_view_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_node_view_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_node_view_construct (RBNodeView *view);
static int rb_node_view_sort_func (GtkTreeModel *model,
			           GtkTreeIter *a,
			           GtkTreeIter *b,
			           gpointer user_data);
static void get_selection (GtkTreeModel *model,
	                   GtkTreePath *path,
	                   GtkTreeIter *iter,
	                   void **data);
static void rb_node_view_selection_changed_cb (GtkTreeSelection *selection,
				               RBNodeView *view);
static void rb_node_view_row_activated_cb (GtkTreeView *treeview,
			                   GtkTreePath *path,
			                   GtkTreeViewColumn *column,
			                   RBNodeView *view);
static void rb_tree_model_node_row_inserted_cb (RBTreeModelNode *model,
				                GtkTreePath *path,
				                GtkTreeIter *iter,
				                RBNodeView *view);
static void rb_tree_model_node_row_deleted_cb (RBTreeModelNode *model,
				               GtkTreePath *path,
			                       RBNodeView *view);
static RBNode *rb_node_view_get_node (RBNodeView *view,
		                      gboolean down);
static void gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					              RBNodeView *view);

struct RBNodeViewPrivate
{
	RBNode *root;

	RBTreeModelNode *nodemodel;
	GtkTreeModel *filtermodel;
	GtkTreeModel *sortmodel;

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	char *view_desc_file;

	gboolean have_selection;
};

enum
{
	NODE_SELECTED,
	NODE_ACTIVATED,
	HAVE_SELECTION,
	CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_ROOT,
	PROP_FILTER_ROOT,
	PROP_PLAYING_NODE,
	PROP_VIEW_DESC_FILE
};

static GObjectClass *parent_class = NULL;

static guint rb_node_view_signals[LAST_SIGNAL] = { 0 };

GType
rb_node_view_get_type (void)
{
	static GType rb_node_view_type = 0;

	if (rb_node_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNodeViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_view_class_init,
			NULL,
			NULL,
			sizeof (RBNodeView),
			0,
			(GInstanceInitFunc) rb_node_view_init
		};

		rb_node_view_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW,
							    "RBNodeView",
							    &our_info, 0);
	}

	return rb_node_view_type;
}

static void
rb_node_view_class_init (RBNodeViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_node_view_finalize;

	object_class->set_property = rb_node_view_set_property;
	object_class->get_property = rb_node_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_ROOT,
					 g_param_spec_object ("root",
							      "Root node",
							      "Root node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_FILTER_ROOT,
					 g_param_spec_object ("filter-root",
							      "Filter root node",
							      "Filter root node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYING_NODE,
					 g_param_spec_object ("playing-node",
							      "Playing node",
							      "Playing node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_VIEW_DESC_FILE,
					 g_param_spec_string ("view-desc-file",
							      "View description",
							      "View description",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_node_view_signals[NODE_ACTIVATED] =
		g_signal_new ("node_activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeViewClass, node_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
	rb_node_view_signals[NODE_SELECTED] =
		g_signal_new ("node_selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeViewClass, node_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
	rb_node_view_signals[HAVE_SELECTION] =
		g_signal_new ("have_selection",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeViewClass, have_selection),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
	rb_node_view_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeViewClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_node_view_init (RBNodeView *view)
{
	view->priv = g_new0 (RBNodeViewPrivate, 1);
}

static void
rb_node_view_finalize (GObject *object)
{
	RBNodeView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_VIEW (object));

	view = RB_NODE_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_free (view->priv->view_desc_file);
	
	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_node_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBNodeView *view = RB_NODE_VIEW (object);

	switch (prop_id)
	{
	case PROP_ROOT:
		view->priv->root = g_value_get_object (value);
		rb_node_view_construct (view);
		break;
	case PROP_FILTER_ROOT:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_set_property (G_OBJECT (view->priv->nodemodel),
				               "filter-root", value);
			
			g_signal_emit (G_OBJECT (view), rb_node_view_signals[CHANGED], 0);
		}
		break;
	case PROP_PLAYING_NODE:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_set_property (G_OBJECT (view->priv->nodemodel),
				               "playing-node", value);
		}
		break;
	case PROP_VIEW_DESC_FILE:
		g_free (view->priv->view_desc_file);
		view->priv->view_desc_file = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_node_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBNodeView *view = RB_NODE_VIEW (object);

	switch (prop_id)
	{
	case PROP_ROOT:
		g_value_set_object (value, view->priv->root);
		break;
	case PROP_FILTER_ROOT:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_get_property (G_OBJECT (view->priv->nodemodel),
				               "filter-root", value);
		}
		break;
	case PROP_PLAYING_NODE:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_get_property (G_OBJECT (view->priv->nodemodel),
				               "playing-node", value);
		}
		break;
	case PROP_VIEW_DESC_FILE:
		g_value_set_string (value, view->priv->view_desc_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBNodeView *
rb_node_view_new (RBNode *root,
		  const char *view_desc_file)
{
	RBNodeView *view;

	g_assert (view_desc_file != NULL);
	g_assert (g_file_test (view_desc_file, G_FILE_TEST_EXISTS) == TRUE);

	view = RB_NODE_VIEW (g_object_new (RB_TYPE_NODE_VIEW,
					   "hadjustment", NULL,
					   "vadjustment", NULL,
					   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					   "vscrollbar_policy", GTK_POLICY_ALWAYS,
					   "shadow_type", GTK_SHADOW_IN,
					   "view-desc-file", view_desc_file,
					   "root", root,
					   "filter-root", root,
					   NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

static void
rb_node_view_construct (RBNodeView *view)
{
	xmlDocPtr doc;
	xmlNodePtr child;
	char *tmp;

	view->priv->nodemodel = rb_tree_model_node_new (view->priv->root);
	g_signal_connect (G_OBJECT (view->priv->nodemodel),
			  "row_inserted",
			  G_CALLBACK (rb_tree_model_node_row_inserted_cb),
			  view);
	g_signal_connect (G_OBJECT (view->priv->nodemodel),
			  "row_deleted",
			  G_CALLBACK (rb_tree_model_node_row_deleted_cb),
			  view);
	view->priv->filtermodel = gtk_tree_model_filter_new_with_model (GTK_TREE_MODEL (view->priv->nodemodel),
							                RB_TREE_MODEL_NODE_COL_VISIBLE,
							                NULL);
	view->priv->sortmodel = gtk_tree_model_sort_new_with_model (view->priv->filtermodel);
	g_signal_connect (G_OBJECT (view->priv->sortmodel),
			  "sort_column_changed",
			  G_CALLBACK (gtk_tree_sortable_sort_column_changed_cb),
			  view);
	
	view->priv->treeview = gtk_tree_view_new_with_model (view->priv->sortmodel);
	g_signal_connect (G_OBJECT (view->priv->treeview),
			  "row_activated",
			  G_CALLBACK (rb_node_view_row_activated_cb),
			  view);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect (G_OBJECT (view->priv->selection),
			  "changed",
			  G_CALLBACK (rb_node_view_selection_changed_cb),
			  view);
	gtk_container_add (GTK_CONTAINER (view), view->priv->treeview);

	/* load layout */
	doc = xmlParseFile (view->priv->view_desc_file);

	if (doc == NULL)
	{
		rb_error_dialog (_("Failed to parse %s as NodeView layout file"),
				 view->priv->view_desc_file);
		return;
	}

	tmp = xmlGetProp (doc->children, "rules-hint");
	if (tmp != NULL)
		gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view->priv->treeview), atoi (tmp));
	g_free (tmp);

	tmp = xmlGetProp (doc->children, "headers-visible");
	if (tmp != NULL)
		gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), atoi (tmp));
	g_free (tmp);

	tmp = xmlGetProp (doc->children, "selection-mode");
	if (tmp != NULL)
	{
		GEnumClass *class = g_type_class_ref (GTK_TYPE_SELECTION_MODE);
		GEnumValue *ev = g_enum_get_value_by_name (class, tmp);
		gtk_tree_selection_set_mode (view->priv->selection, ev->value);
		g_type_class_unref (class);
	}
	g_free (tmp);

	for (child = doc->children->children; child != NULL; child = child->next)
	{
		char *title = NULL;
		gboolean visible = FALSE, reorderable = FALSE, resizable = FALSE;
		GList *sort_order = NULL;
		RBTreeModelNodeColumn column;
		GEnumClass *class = g_type_class_ref (RB_TYPE_TREE_MODEL_NODE_COLUMN);
		GtkTreeViewColumn *gcolumn;
		GtkCellRenderer *renderer;
		GEnumValue *ev;

		/* get props from the xml file */
		tmp = xmlGetProp (child, "column");
		if (tmp == NULL)
			continue;
		ev = g_enum_get_value_by_name (class, tmp);
		column = ev->value;
		g_free (tmp);

		title = xmlGetProp (child, "_title");
		
		tmp = xmlGetProp (child, "visible");
		if (tmp != NULL)
			visible = atoi (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "reorderable");
		if (tmp != NULL)
			reorderable = atoi (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "resizable");
		if (tmp != NULL)
			resizable = atoi (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "sort-order");
		if (tmp != NULL)
		{
			char **parts = g_strsplit (tmp, ",", 0);
			int i;
			
			for (i = 0; parts != NULL && parts[i] != NULL; i++)
			{
				RBTreeModelNodeColumn col;
				ev = g_enum_get_value_by_name (class, parts[i]);	
				col = ev->value;
				sort_order = g_list_append (sort_order, GINT_TO_POINTER (col));
			}
			
			g_strfreev (parts);
		}
		g_free (tmp);

		g_type_class_unref (class);

		/* so we got all info, now we can actually build the column */
		gcolumn = gtk_tree_view_column_new ();
		if (column != RB_TREE_MODEL_NODE_COL_PLAYING)
		{
			renderer = gtk_cell_renderer_text_new ();
			gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
			gtk_tree_view_column_set_attributes (gcolumn, renderer,
							     "text", column,
							     NULL);
		}
		else
		{
			renderer = gtk_cell_renderer_pixbuf_new ();
			gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
			gtk_tree_view_column_set_attributes (gcolumn, renderer,
							     "pixbuf", column,
							     NULL);
			gtk_tree_view_column_set_fixed_width (gcolumn, GTK_ICON_SIZE_MENU);
		}
		if (title != NULL)
		{
			gtk_tree_view_column_set_title (gcolumn, _(title));
			g_free (title);
		}
		gtk_tree_view_column_set_resizable (gcolumn, resizable);
		gtk_tree_view_column_set_reorderable (gcolumn, reorderable);
		if (sort_order != NULL)
			gtk_tree_view_column_set_sort_column_id (gcolumn, column);
		gtk_tree_view_column_set_visible (gcolumn, visible);
		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), gcolumn);

		gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (view->priv->sortmodel),
						 column,
						 rb_node_view_sort_func,
						 sort_order, NULL);
		g_object_set_data_full (G_OBJECT (gcolumn),
					"sort-order", sort_order,
					(GDestroyNotify) g_list_free);
	}

	xmlFreeDoc (doc);
}

void
rb_node_view_set_filter_root (RBNodeView *view,
			      RBNode *root)
{
	g_return_if_fail (RB_IS_NODE_VIEW (view));

	g_object_set (G_OBJECT (view),
		      "filter-root", root,
		      NULL);
}

void
rb_node_view_set_playing_node (RBNodeView *view,
			       RBNode *node)
{
	g_return_if_fail (RB_IS_NODE_VIEW (view));

	g_object_set (G_OBJECT (view),
		      "playing-node", node,
		      NULL);
}

RBNode *
rb_node_view_get_playing_node (RBNodeView *view)
{
	RBNode *node;
	
	g_return_val_if_fail (RB_IS_NODE_VIEW (view), NULL);

	g_object_get (G_OBJECT (view),
		      "playing-node", &node,
		      NULL);

	return node;
}

static RBNode *
rb_node_view_get_node (RBNodeView *view,
		       gboolean down)
{
	RBNode *playing_node = rb_node_view_get_playing_node (view);
	GtkTreeIter iter, iter2;
	GValue val = {0, };
	gboolean visible;

	g_assert (playing_node != NULL);

	rb_tree_model_node_iter_from_node (RB_TREE_MODEL_NODE (view->priv->nodemodel),
					   playing_node, &iter);
	gtk_tree_model_get_value (GTK_TREE_MODEL (view->priv->nodemodel), &iter,
				  RB_TREE_MODEL_NODE_COL_VISIBLE, &val);
	visible = g_value_get_boolean (&val);
	g_value_unset (&val);

	if (visible == FALSE)
		return NULL;

	gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter2, &iter);
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter, &iter2);

	if (down == TRUE)
	{
		if (gtk_tree_model_iter_next (GTK_TREE_MODEL (view->priv->sortmodel), &iter) == FALSE)
			return NULL;
	}
	else
	{
		GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->sortmodel), &iter);
		gboolean found;

		if (gtk_tree_path_prev (path) == FALSE)
		{
			gtk_tree_path_free (path);
			return NULL;
		}
		
		found = gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel), &iter, path);

		gtk_tree_path_free (path);

		if (found == FALSE)
			return NULL;
	}

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	return rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);
}

RBNode *
rb_node_view_get_next_node (RBNodeView *view)
{
	return rb_node_view_get_node (view, TRUE);
}

RBNode *
rb_node_view_get_previous_node (RBNodeView *view)
{	
	return rb_node_view_get_node (view, FALSE);
}

RBNode *
rb_node_view_get_first_node (RBNodeView *view)
{
	GtkTreeIter iter, iter2;

	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->sortmodel),
				       &iter);

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	return rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);
}

RBNode *
rb_node_view_get_random_node (RBNodeView *view)
{
	GtkTreePath *path;
	GtkTreeIter iter, iter2;
	char *path_str;
	int index;

	index = random () % gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->sortmodel), NULL);

	path_str = g_strdup_printf ("%d", index);
	path = gtk_tree_path_new_from_string (path_str);
	g_free (path_str);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel),
				 &iter, path);

	gtk_tree_path_free (path);

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	return rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);
}

static void
get_selection (GtkTreeModel *model,
	       GtkTreePath *path,
	       GtkTreeIter *iter,
	       void **data)
{
	GtkTreeModelSort *sortmodel = GTK_TREE_MODEL_SORT (model);
	GtkTreeModelFilter *filtermodel = GTK_TREE_MODEL_FILTER (sortmodel->child_model);
	RBTreeModelNode *nodemodel = RB_TREE_MODEL_NODE (filtermodel->child_model);
	GList **list = (GList **) data;
	GtkTreeIter *iter2 = gtk_tree_iter_copy (iter);
	GtkTreeIter iter3;
	RBNode *node;

	gtk_tree_model_sort_convert_iter_to_child_iter (sortmodel, &iter3, iter2);
	gtk_tree_model_filter_convert_iter_to_child_iter (filtermodel, iter2, &iter3);
	
	node = rb_tree_model_node_node_from_iter (nodemodel, iter2);

	gtk_tree_iter_free (iter2);

	*list = g_list_append (*list, node);
}

GList *
rb_node_view_get_selection (RBNodeView *view)
{
	GList *list = NULL;
	
	gtk_tree_selection_selected_foreach (view->priv->selection,
					     (GtkTreeSelectionForeachFunc) get_selection,
					     (void **) &list);
	
	return list;
}

static int
rb_node_view_sort_func (GtkTreeModel *model,
			GtkTreeIter *a,
			GtkTreeIter *b,
			gpointer user_data)
{
	GList *order = (GList *) user_data;
	GList *l;
	int retval = 0;

	for (l = order; l != NULL && retval == 0; l = g_list_next (l))
	{
		RBTreeModelNodeColumn column = GPOINTER_TO_INT (l->data);
		GType type = gtk_tree_model_get_column_type (model, column);
		GValue a_value = {0, };
		GValue b_value = {0, };
		const char *stra, *strb;

		gtk_tree_model_get_value (model, a, column, &a_value);
		gtk_tree_model_get_value (model, b, column, &b_value);
		
		switch (G_TYPE_FUNDAMENTAL (type))
		{
		case G_TYPE_BOOLEAN:
			if (g_value_get_boolean (&a_value) < g_value_get_boolean (&b_value))
				retval = -1;
			else if (g_value_get_boolean (&a_value) == g_value_get_boolean (&b_value))
				retval = 0;
			else
				retval = 1;
			break;
		case G_TYPE_STRING:
			stra = g_value_get_string (&a_value);
			strb = g_value_get_string (&b_value);
			if (stra == NULL) stra = "";
			if (strb == NULL) strb = "";
			retval = g_utf8_collate (stra, strb);
			break;
		case G_TYPE_OBJECT:
			if ((g_value_get_object (&a_value) == NULL) && (g_value_get_object (&b_value) != NULL))
				retval = -1;
			else if ((g_value_get_object (&a_value) != NULL) && (g_value_get_object (&b_value) == NULL))
				retval = 1;
			else
				retval = 0;
			break;
		default:
			g_warning ("Attempting to sort on invalid type %s\n", g_type_name (type));
			break;
		}

		g_value_unset (&a_value);
		g_value_unset (&b_value);
	}

	return retval;
}

static void
rb_node_view_selection_changed_cb (GtkTreeSelection *selection,
				   RBNodeView *view)
{
	gboolean available;
	RBNode *selected_node = NULL;
	GList *sel;

	sel = rb_node_view_get_selection (view);
	available = (sel != NULL);
	if (sel != NULL)
		selected_node = RB_NODE ((g_list_first (sel))->data);
	g_list_free (sel);

	if (available != view->priv->have_selection)
	{
		g_signal_emit (G_OBJECT (view), rb_node_view_signals[HAVE_SELECTION], 0, available);
		view->priv->have_selection = available;
	}

	if (selected_node != NULL)
	{
		g_signal_emit (G_OBJECT (view), rb_node_view_signals[NODE_SELECTED], 0, selected_node);
	}
}

static void
rb_node_view_row_activated_cb (GtkTreeView *treeview,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       RBNodeView *view)
{
	GtkTreeIter iter, iter2;
	RBNode *node;
	
	gtk_tree_model_get_iter (view->priv->sortmodel, &iter, path);
	gtk_tree_model_sort_convert_iter_to_child_iter
		(GTK_TREE_MODEL_SORT (view->priv->sortmodel), &iter2, &iter);
	gtk_tree_model_filter_convert_iter_to_child_iter
		(GTK_TREE_MODEL_FILTER (view->priv->filtermodel), &iter, &iter2);

	node = rb_tree_model_node_node_from_iter (view->priv->nodemodel, &iter);

	g_signal_emit (G_OBJECT (view), rb_node_view_signals[NODE_ACTIVATED], 0, node);
}

static void
rb_tree_model_node_row_inserted_cb (RBTreeModelNode *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
				    RBNodeView *view)
{
	g_signal_emit (G_OBJECT (view), rb_node_view_signals[CHANGED], 0);
}

static void
rb_tree_model_node_row_deleted_cb (RBTreeModelNode *model,
				   GtkTreePath *path,
			           RBNodeView *view)
{
	g_signal_emit (G_OBJECT (view), rb_node_view_signals[CHANGED], 0);
}

static void
gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					  RBNodeView *view)
{
	g_signal_emit (G_OBJECT (view), rb_node_view_signals[CHANGED], 0);
}
