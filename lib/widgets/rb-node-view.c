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
 */

#include <gtk/gtktreeview.h>
#include <gtk/gtktreemodelsort.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkiconfactory.h>
#include <gdk/gdkkeysyms.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <string.h>
#include <libxml/tree.h>
#include <stdlib.h>

#include "eggtreemultidnd.h"
#include "eggtreemodelfilter.h"
#include "rb-tree-model-node.h"
#include "rb-tree-model-sort.h"
#include "rb-node-view.h"
#include "rb-dialog.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-node-song.h"
#include "rb-string-helpers.h"
#include "rb-library-dnd-types.h"
#include "eel-gconf-extensions.h"

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
static void gtk_tree_model_sort_row_inserted_cb (GtkTreeModel *model,
				                 GtkTreePath *path,
				                 GtkTreeIter *iter,
				                 RBNodeView *view);
static void gtk_tree_model_sort_row_deleted_cb (GtkTreeModel *model,
				                GtkTreePath *path,
			                        RBNodeView *view);
static void gtk_tree_model_sort_row_changed_cb (GtkTreeModel *model,
				                GtkTreePath *path,
				                GtkTreeIter *iter,
			                        RBNodeView *view);
static void gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					              RBNodeView *view);
static gboolean rb_node_view_timeout_cb (RBNodeView *view);
static int rb_node_view_get_n_rows (RBNodeView *view);
static void root_child_removed_cb (RBNode *root,
			           RBNode *child,
			           RBNodeView *view);
static void tree_view_size_allocate_cb (GtkWidget *widget,
			                GtkAllocation *allocation);
static gboolean rb_node_view_is_empty (RBNodeView *view);
static void rb_node_view_columns_parse (RBNodeView *view,
					const char *config);
static void rb_node_view_columns_config_changed_cb (GConfClient* client,
						    guint cnxn_id,
						    GConfEntry *entry,
						    gpointer user_data);

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
	GList *nodeselection;

	gboolean keep_selection;

	RBNode *selected_node;

	gboolean changed;
	guint timeout;

	gboolean selection_lock;

	GList *current_random;
	GList *random_nodes;
	
	char *columns_key;
	guint gconf_notification_id;
	GHashTable *columns;

	RBLibrary *library;
};

enum
{
	NODE_SELECTED,
	NODE_ACTIVATED,
	CHANGED,
	PLAYING_NODE_REMOVED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_ROOT,
	PROP_FILTER_PARENT,
	PROP_FILTER_ARTIST,
	PROP_PLAYING_NODE,
	PROP_VIEW_COLUMNS_KEY,
	PROP_VIEW_DESC_FILE,
	PROP_LIBRARY
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
					 PROP_FILTER_PARENT,
					 g_param_spec_object ("filter-parent",
							      "Filter parent node",
							      "Filter parent node",
							      RB_TYPE_NODE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_FILTER_ARTIST,
					 g_param_spec_object ("filter-artist",
							      "Filter artist node",
							      "Filter artist node",
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
	g_object_class_install_property (object_class,
					 PROP_VIEW_COLUMNS_KEY,
					 g_param_spec_string ("columns-key",
							       "Columns key",
							       "Columns key",
							       NULL,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library object",
							      "Library object",
							      RB_TYPE_LIBRARY,
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
	rb_node_view_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeViewClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	
	rb_node_view_signals[PLAYING_NODE_REMOVED] =
		g_signal_new ("playing_node_removed",
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

	view->priv->timeout = g_timeout_add (50, (GSourceFunc) rb_node_view_timeout_cb, view);

	view->priv->current_random = NULL;
	view->priv->random_nodes = NULL;
}

static void
rb_node_view_finalize (GObject *object)
{
	RBNodeView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_VIEW (object));

	view = RB_NODE_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	g_source_remove (view->priv->timeout);

	g_list_free (view->priv->nodeselection);

	g_free (view->priv->view_desc_file);

	g_free (view->priv->columns_key);

	if (view->priv->gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->gconf_notification_id);

	g_hash_table_destroy (view->priv->columns);

	g_object_unref (G_OBJECT (view->priv->sortmodel));
	g_object_unref (G_OBJECT (view->priv->filtermodel));
	g_object_unref (G_OBJECT (view->priv->nodemodel));

	g_list_free (view->priv->random_nodes);
	
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
		{
			view->priv->root = g_value_get_object (value);
			rb_node_view_construct (view);
		}
		break;
	case PROP_FILTER_PARENT:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_set_property (G_OBJECT (view->priv->nodemodel),
				               "filter-parent", value);
		}
		break;
	case PROP_FILTER_ARTIST:
		{
			g_assert (view->priv->nodemodel != NULL);
			
			g_object_set_property (G_OBJECT (view->priv->nodemodel),
					       "filter-artist", value);
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
	case PROP_VIEW_COLUMNS_KEY:
		g_free (view->priv->columns_key);
		view->priv->columns_key = g_strdup (g_value_get_string (value));
		break;
	case PROP_LIBRARY:
		view->priv->library = g_value_get_object (value);
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
	case PROP_FILTER_PARENT:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_get_property (G_OBJECT (view->priv->nodemodel),
				               "filter-parent", value);
		}
		break;
	case PROP_FILTER_ARTIST:
		{
			g_assert (view->priv->nodemodel != NULL);

			g_object_get_property (G_OBJECT (view->priv->nodemodel),
					       "filter-artist", value);
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
	case PROP_VIEW_COLUMNS_KEY:
		g_value_set_string (value, view->priv->columns_key);
		break;
	case PROP_LIBRARY:
		g_value_set_object (value, view->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBNodeView *
rb_node_view_new (RBNode *root,
		  const char *view_desc_file,
		  const char *columns_conf_key,
		  RBLibrary *library)
{
	RBNodeView *view;

	g_assert (view_desc_file != NULL);
	g_assert (g_file_test (view_desc_file, G_FILE_TEST_EXISTS) == TRUE);

	view = RB_NODE_VIEW (g_object_new (RB_TYPE_NODE_VIEW,
					   "library", library,
					   "hadjustment", NULL,
					   "vadjustment", NULL,
					   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					   "vscrollbar_policy", GTK_POLICY_ALWAYS,
					   "shadow_type", GTK_SHADOW_IN,
					   "view-desc-file", view_desc_file,
					   "columns-key", columns_conf_key,
					   "root", root,
					   "filter-parent", root,
					   NULL));

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

static void
node_from_sort_iter_cb (RBTreeModelSort *model,
		      GtkTreeIter *iter,
		      void **node,
		      RBNodeView *view)
{
	GtkTreeIter filter_iter, node_iter;
	
	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (model),
							&filter_iter, iter);
	egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &node_iter, &filter_iter);
	*node = rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &node_iter);
}

static gboolean
bool_to_int (const char *boolean)
{
	return (strcmp (boolean, "true") == 0);
}

static void
rb_node_view_construct (RBNodeView *view)
{
	xmlDocPtr doc;
	xmlNodePtr child;
	char *tmp;

	g_signal_connect_object (G_OBJECT (view->priv->root),
			         "pre_child_removed",
			         G_CALLBACK (root_child_removed_cb),
				 view,
				 0);

	view->priv->columns = g_hash_table_new (NULL, NULL);
	view->priv->nodemodel = rb_tree_model_node_new (view->priv->root,
							view->priv->library);
	view->priv->filtermodel = egg_tree_model_filter_new (GTK_TREE_MODEL (view->priv->nodemodel),
							     NULL);
	egg_tree_model_filter_set_visible_column (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
						  RB_TREE_MODEL_NODE_COL_VISIBLE);
	view->priv->sortmodel = rb_tree_model_sort_new (view->priv->filtermodel);
	g_signal_connect_object (G_OBJECT (view->priv->sortmodel),
				 "node_from_iter",
				 G_CALLBACK (node_from_sort_iter_cb),
				 view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->sortmodel),
			         "row_inserted",
			         G_CALLBACK (gtk_tree_model_sort_row_inserted_cb),
			         view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->sortmodel),
			         "row_deleted",
			         G_CALLBACK (gtk_tree_model_sort_row_deleted_cb),
			         view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->sortmodel),
				 "row_changed",
				 G_CALLBACK (gtk_tree_model_sort_row_changed_cb),
				 view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->sortmodel),
			         "sort_column_changed",
			         G_CALLBACK (gtk_tree_sortable_sort_column_changed_cb),
			         view,
				 0);
	
	view->priv->treeview = gtk_tree_view_new_with_model (view->priv->sortmodel);
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
				 "size_allocate",
				 G_CALLBACK (tree_view_size_allocate_cb),
				 view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "row_activated",
			         G_CALLBACK (rb_node_view_row_activated_cb),
			         view,
				 0);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect_object (G_OBJECT (view->priv->selection),
			         "changed",
			         G_CALLBACK (rb_node_view_selection_changed_cb),
			         view,
				 0);

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
		gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view->priv->treeview), bool_to_int (tmp));
	g_free (tmp);

	tmp = xmlGetProp (doc->children, "headers-visible");
	if (tmp != NULL)
		gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), bool_to_int (tmp));
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

	tmp = xmlGetProp (doc->children, "keep-selection");
	if (tmp != NULL)
		view->priv->keep_selection = bool_to_int (tmp);
	g_free (tmp);

	for (child = doc->children->children; child != NULL; child = child->next)
	{
		char *title = NULL;
		gboolean visible = FALSE, reorderable = FALSE, resizable = FALSE, clickable = TRUE;
		gboolean default_sort_column = FALSE, expand = FALSE;
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
			visible = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "reorderable");
		if (tmp != NULL)
			reorderable = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "resizable");
		if (tmp != NULL)
			resizable = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "clickable");
		if (tmp != NULL)
			clickable = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "default-sort-column");
		if (tmp != NULL)
			default_sort_column = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "expand");
		if (tmp != NULL)
			expand = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "sort-order");
		if (tmp != NULL)
		{
			char **parts = g_strsplit (tmp, " ", 0);
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
			gtk_tree_view_column_set_resizable (gcolumn, resizable);
			gtk_tree_view_column_set_sizing (gcolumn,
							 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		}
		else
		{
			int width;
			
			renderer = rb_cell_renderer_pixbuf_new ();
			gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
			gtk_tree_view_column_set_attributes (gcolumn, renderer,
							     "pixbuf", column,
							     NULL);
			gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_FIXED);
			gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
			gtk_tree_view_column_set_fixed_width (gcolumn, width + 5);
		}

		if (title != NULL)
		{
			gtk_tree_view_column_set_title (gcolumn, _(title));
			g_free (title);
		}

		gtk_tree_view_column_set_reorderable (gcolumn, reorderable);

		if (clickable == TRUE)
		{
			if (sort_order != NULL)
				gtk_tree_view_column_set_sort_column_id (gcolumn, column);
		}
		
		gtk_tree_view_column_set_clickable (gcolumn, clickable);

		gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (view->priv->sortmodel),
						 column,
						 rb_node_view_sort_func,
						 sort_order, NULL);
		g_object_set_data_full (G_OBJECT (gcolumn),
					"sort-order", sort_order,
					(GDestroyNotify) g_list_free);

		if (default_sort_column == TRUE)
		{
			gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (view->priv->sortmodel),
							      column, GTK_SORT_ASCENDING);
		}

		g_object_set_data (G_OBJECT (gcolumn), "expand", GINT_TO_POINTER (expand));

		gtk_tree_view_column_set_visible (gcolumn, visible);

		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), 
					     gcolumn);
		
		g_hash_table_insert (view->priv->columns, 
				     GINT_TO_POINTER (column), 
				     gcolumn);
	}
		
	if (view->priv->columns_key != NULL)
	{
		char *config = eel_gconf_get_string (view->priv->columns_key);

		if (config != NULL)
		{
			rb_node_view_columns_parse (view, config);

			view->priv->gconf_notification_id = 
				eel_gconf_notification_add (view->priv->columns_key,
							    rb_node_view_columns_config_changed_cb,
							    view);

			g_free (config);
		}
	}

	xmlFreeDoc (doc);
}

void
rb_node_view_set_filter (RBNodeView *view,
			 RBNode *filter_parent,
			 RBNode *filter_artist)
{
	g_return_if_fail (RB_IS_NODE_VIEW (view));

	g_object_set (G_OBJECT (view),
		      "filter-artist", filter_artist,
		      "filter-parent", filter_parent,
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
	g_return_val_if_fail (RB_IS_NODE_VIEW (view), NULL);

	return rb_tree_model_node_get_playing_node (view->priv->nodemodel);
}

RBNode *
rb_node_view_get_node (RBNodeView *view,
		       RBNode *start,
		       RBDirection direction)
{
	GtkTreeIter iter, iter2;
	GValue val = {0, };
	gboolean visible;

	g_assert (start != NULL);

	rb_tree_model_node_iter_from_node (RB_TREE_MODEL_NODE (view->priv->nodemodel),
					   start, &iter);
	gtk_tree_model_get_value (GTK_TREE_MODEL (view->priv->nodemodel), &iter,
				  RB_TREE_MODEL_NODE_COL_VISIBLE, &val);
	visible = g_value_get_boolean (&val);
	g_value_unset (&val);

	if (visible == FALSE)
		return NULL;

	egg_tree_model_filter_convert_child_iter_to_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter2, &iter);
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter, &iter2);

	if (direction == RB_DIRECTION_DOWN)
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
	egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	return rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);
}

RBNode *
rb_node_view_get_next_node (RBNodeView *view)
{
	return rb_node_view_get_node (view, rb_node_view_get_playing_node (view), RB_DIRECTION_DOWN);
}

RBNode *
rb_node_view_get_previous_node (RBNodeView *view)
{	
	return rb_node_view_get_node (view, rb_node_view_get_playing_node (view), RB_DIRECTION_UP);
}

RBNode *
rb_node_view_get_first_node (RBNodeView *view)
{
	GtkTreeIter iter, iter2;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->sortmodel),
				           &iter) == FALSE)
		return NULL;

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);
	egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	return rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);
}

static gboolean
rb_node_view_node_is_repeat (RBNodeView *view, RBNode *node)
{
	GList *tmp;

	tmp = view->priv->random_nodes;

	while (tmp) {
		RBNode *tmp_node = tmp->data;

		if (tmp_node == node)
			return TRUE;

		tmp = tmp->next;
	}

	return FALSE;
}

static RBNode *
rb_node_view_get_random_node (RBNodeView *view)
{
	static int recur_count = 0;
	RBNode *node;
	GtkTreePath *path;
	GtkTreeIter iter, iter2;
	char *path_str;
	int index, n_rows;

	if (rb_node_view_is_empty (view) == TRUE)
		return NULL;

	n_rows = rb_node_view_get_n_rows (view);
	if ((n_rows - 1) > 0)
		index = g_random_int_range (0, n_rows - 1);
	else
		index = 0;

	path_str = g_strdup_printf ("%d", index);
	path = gtk_tree_path_new_from_string (path_str);
	g_free (path_str);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel),
				 &iter, path);

	gtk_tree_path_free (path);

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);
	egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter, &iter2);

	node = rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &iter);

	if (rb_node_view_node_is_repeat (view, node)) {
		if (recur_count > 5) {
			recur_count = 0;

			/* we give up, we are returning a repeat */
			return node;
		} else {
			recur_count++;
			return rb_node_view_get_random_node (view);
		}
	}
	
	recur_count = 0;

	return node;
}

RBNode *
rb_node_view_get_next_random_node (RBNodeView *view)
{
	RBNode *node;

	if (rb_node_view_is_empty (view) == TRUE)
		return NULL;

	if (view->priv->current_random != NULL &&
	    view->priv->current_random->next != NULL) {
		view->priv->current_random = view->priv->current_random->next;
		node = (RBNode *)view->priv->current_random->data;
	} else {
		node = rb_node_view_get_random_node (view);
		view->priv->random_nodes = g_list_append (view->priv->random_nodes, node);
		view->priv->current_random = g_list_last (view->priv->random_nodes);
	}

	return node;
}

RBNode *
rb_node_view_get_previous_random_node (RBNodeView *view)
{
	RBNode *node;

	if (rb_node_view_is_empty (view) == TRUE)
		return NULL;

	if (view->priv->current_random != NULL &&
	    view->priv->current_random->prev != NULL) {
		view->priv->current_random = view->priv->current_random->prev;
		node = (RBNode *)view->priv->current_random->data;
	} else {
		node = rb_node_view_get_random_node (view);
		view->priv->random_nodes = g_list_prepend (view->priv->random_nodes, node);
		view->priv->current_random = view->priv->random_nodes;
	}

	return node;
}

static void
get_selection (GtkTreeModel *model,
	       GtkTreePath *path,
	       GtkTreeIter *iter,
	       void **data)
{
	GtkTreeModelSort *sortmodel = GTK_TREE_MODEL_SORT (model);
	EggTreeModelFilter *filtermodel = EGG_TREE_MODEL_FILTER (sortmodel->child_model);
	RBTreeModelNode *nodemodel = RB_TREE_MODEL_NODE (filtermodel->child_model);
	GList **list = (GList **) data;
	GtkTreeIter *iter2 = gtk_tree_iter_copy (iter);
	GtkTreeIter iter3;
	RBNode *node;

	gtk_tree_model_sort_convert_iter_to_child_iter (sortmodel, &iter3, iter2);
	egg_tree_model_filter_convert_iter_to_child_iter (filtermodel, iter2, &iter3);
	
	node = rb_tree_model_node_node_from_iter (nodemodel, iter2);

	gtk_tree_iter_free (iter2);

	*list = g_list_prepend (*list, node);
}

GList *
rb_node_view_get_selection (RBNodeView *view)
{
	GList *list = NULL;
	
	gtk_tree_selection_selected_foreach (view->priv->selection,
					     (GtkTreeSelectionForeachFunc) get_selection,
					     (void **) &list);
	g_list_free (view->priv->nodeselection);
	view->priv->nodeselection = list;
	
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
		char *folda, *foldb;

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
			folda = g_utf8_casefold (stra, g_utf8_strlen (stra, -1));
			foldb = g_utf8_casefold (strb, g_utf8_strlen (strb, -1));
			retval = g_utf8_collate (folda, foldb);
			g_free (folda);
			g_free (foldb);
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

	if (view->priv->selection_lock == TRUE)
		return;

	sel = rb_node_view_get_selection (view);
	available = (sel != NULL);
	if (sel != NULL)
		selected_node = RB_NODE ((g_list_first (sel))->data);

	if (available != view->priv->have_selection)
	{
		view->priv->changed = TRUE;
		view->priv->have_selection = available;
	}

	if (selected_node != NULL && selected_node != view->priv->selected_node)
	{
		g_signal_emit (G_OBJECT (view), rb_node_view_signals[NODE_SELECTED], 0, selected_node);
	}

	view->priv->selected_node = selected_node;
}

gboolean
rb_node_view_have_selection (RBNodeView *view)
{
	return view->priv->have_selection;
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
	egg_tree_model_filter_convert_iter_to_child_iter
		(EGG_TREE_MODEL_FILTER (view->priv->filtermodel), &iter, &iter2);

	node = rb_tree_model_node_node_from_iter (view->priv->nodemodel, &iter);

	g_signal_emit (G_OBJECT (view), rb_node_view_signals[NODE_ACTIVATED], 0, node);
}

static void
gtk_tree_model_sort_row_inserted_cb (GtkTreeModel *model,
				     GtkTreePath *path,
				     GtkTreeIter *iter,
				     RBNodeView *view)
{
	view->priv->changed = TRUE;
}

static void
gtk_tree_model_sort_row_deleted_cb (GtkTreeModel *model,
				    GtkTreePath *path,
			            RBNodeView *view)
{
	view->priv->changed = TRUE;
}

static void
gtk_tree_model_sort_row_changed_cb (GtkTreeModel *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
			            RBNodeView *view)
{
	view->priv->changed = TRUE;
}

static void
gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					  RBNodeView *view)
{
	view->priv->changed = TRUE;
}

char *
rb_node_view_get_status (RBNodeView *view)
{
	char *ret, *size;
	int hours, minutes, seconds;
	GnomeVFSFileSize n_bytes = 0;
	long n_seconds = 0;
	int n_songs = 0;
	RBNode *parent = NULL, *artist = NULL;

	rb_tree_model_node_get_filter (view->priv->nodemodel,
				       &parent, &artist);

	if (parent != NULL)
	{
		GPtrArray *kids;
		int i;

		kids = rb_node_get_children (parent);

		for (i = 0; i < kids->len; i++)
		{
			RBNode *node;

			node = g_ptr_array_index (kids, i);
			
			if (artist != NULL &&
			    rb_node_song_has_artist (RB_NODE_SONG (node), artist, view->priv->library) == FALSE)
				continue;

			n_songs++;

			n_seconds += rb_node_get_property_long (node,
								RB_NODE_SONG_PROP_REAL_DURATION);

			n_bytes += rb_node_get_property_long (node,
							      RB_NODE_SONG_PROP_FILE_SIZE);
		}
		
		rb_node_thaw (parent);
	}

	size = gnome_vfs_format_file_size_for_display (n_bytes);

	hours   = n_seconds / (60 * 60);
	minutes = n_seconds / 60 - hours * 60;
	seconds = n_seconds % 60;

	ret = g_strdup_printf (_("%d songs, %d:%02d:%02d playing time, %s"),
			       n_songs, hours, minutes, seconds, size);

	g_free (size);

	return ret;
}

void
rb_node_view_select_all (RBNodeView *view)
{
	gtk_tree_selection_select_all (view->priv->selection);
}

void
rb_node_view_select_none (RBNodeView *view)
{
	view->priv->selection_lock = TRUE;

	gtk_tree_selection_unselect_all (view->priv->selection);

	view->priv->selected_node = NULL;

	view->priv->selection_lock = FALSE;
}

void
rb_node_view_select_node (RBNodeView *view,
			  RBNode *node)
{
	GtkTreeIter iter, iter2;
	GValue val = { 0, };
	gboolean visible;

	if (node == NULL)
		return;

	view->priv->selection_lock = TRUE;

	rb_node_view_select_none (view);

	rb_tree_model_node_iter_from_node (RB_TREE_MODEL_NODE (view->priv->nodemodel),
					   node, &iter);
	gtk_tree_model_get_value (GTK_TREE_MODEL (view->priv->nodemodel), &iter,
				  RB_TREE_MODEL_NODE_COL_VISIBLE, &val);
	visible = g_value_get_boolean (&val);
	g_value_unset (&val);
	
	if (visible == FALSE)
	{
		view->priv->selection_lock = FALSE;
		return;
	}

	egg_tree_model_filter_convert_child_iter_to_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter2, &iter);
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter, &iter2);

	gtk_tree_selection_select_iter (view->priv->selection, &iter);
	
	view->priv->selection_lock = FALSE;
}

void
rb_node_view_scroll_to_node (RBNodeView *view,
			     RBNode *node)
{
	GtkTreeIter iter, iter2;
	GValue val = { 0, };
	gboolean visible;
	GtkTreePath *path;

	if (node == NULL)
		return;
	
	rb_tree_model_node_iter_from_node (RB_TREE_MODEL_NODE (view->priv->nodemodel),
					   node, &iter);
	gtk_tree_model_get_value (GTK_TREE_MODEL (view->priv->nodemodel), &iter,
				  RB_TREE_MODEL_NODE_COL_VISIBLE, &val);
	visible = g_value_get_boolean (&val);
	g_value_unset (&val);

	if (visible == FALSE)
		return;

	egg_tree_model_filter_convert_child_iter_to_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel),
							  &iter2, &iter);
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter, &iter2);

	path = gtk_tree_model_get_path (view->priv->sortmodel, &iter);

	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview), path,
				      gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				      TRUE, 0.5, 0.0);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (view->priv->treeview), path,
				  gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0), FALSE);

	gtk_tree_path_free (path);
}

static gboolean
rb_node_view_timeout_cb (RBNodeView *view)
{
	if (view->priv->changed == FALSE)
		return TRUE;

	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (view), rb_node_view_signals[CHANGED], 0);

	view->priv->changed = FALSE;

	GDK_THREADS_LEAVE ();

	return TRUE;
}

static gboolean
rb_node_view_is_empty (RBNodeView *view)
{
	GtkTreeIter iter;

	return (gtk_tree_model_get_iter_first (view->priv->sortmodel, &iter) == FALSE);
}

static int
rb_node_view_get_n_rows (RBNodeView *view)
{
	RBNode *parent = NULL, *artist = NULL;
	GPtrArray *kids;
	int n_rows = 0, i;

	rb_tree_model_node_get_filter (view->priv->nodemodel,
				       &parent, &artist);

	if (parent == NULL)
		return n_rows;

	kids = rb_node_get_children (parent);
	
	for (i = 0; i < kids->len; i++)
	{
		if (artist != NULL &&
		    rb_node_song_has_artist (g_ptr_array_index (kids, i), artist, view->priv->library) == FALSE)
			continue;

		n_rows++;
	}

	rb_node_thaw (parent);

	return n_rows;
}

static void
root_child_removed_cb (RBNode *root,
		       RBNode *child,
		       RBNodeView *view)
{
	RBNode *node;

	/* playing node bit */
	if (child == rb_node_view_get_playing_node (view))
	{
		g_signal_emit (G_OBJECT (view), 
			       rb_node_view_signals[PLAYING_NODE_REMOVED], 
			       0, 
			       child);
	}

	/* selection bit */
	if (view->priv->keep_selection == FALSE)
		return;
	if (g_list_find (view->priv->nodeselection, child) == NULL)
		return;

	node = rb_node_view_get_node (view, child, RB_DIRECTION_DOWN);
	if (node == NULL)
		node = rb_node_view_get_node (view, child, RB_DIRECTION_UP);
	if (node == NULL)
		return;

	rb_node_view_select_node (view, node);
}

#define TREE_VIEW_DRAG_WIDTH 6

static void
tree_view_size_allocate_cb (GtkWidget *widget,
			    GtkAllocation *allocation)
{
	GList *columns, *l;
	int n_expand_columns = 0, total_requested_width = 0, left_over_width, width = 0;

	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (widget));
	for (l = columns; l != NULL; l = g_list_next (l))
	{
		GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN (l->data);

		if (column->visible == FALSE)
			continue;

		if (column->use_resized_width == TRUE)
		{
			total_requested_width += column->resized_width;
		}
		else if (column->column_type == GTK_TREE_VIEW_COLUMN_FIXED)
		{
			total_requested_width += column->fixed_width;
		}
		else if (gtk_tree_view_get_headers_visible (GTK_TREE_VIEW (widget)) == TRUE)
		{
			total_requested_width += MAX (column->requested_width, column->button_request);
		}
		else
		{
			if (column->requested_width >= 0)
				total_requested_width += column->requested_width;
		}

		if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column), "expand")) == TRUE)
			n_expand_columns++;
	}

	left_over_width = allocation->width - total_requested_width;
	if (left_over_width < 0)
		left_over_width = 0;

	for (l = columns; l != NULL; l = g_list_next (l))
	{
		GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN (l->data);
		GtkAllocation allocation;

		if (column->visible == FALSE)
			continue;

		if (column->use_resized_width == TRUE)
		{
			column->width = column->resized_width;
		}
		else if (column->column_type == GTK_TREE_VIEW_COLUMN_FIXED)
		{
			column->width = column->fixed_width;
		}
		else if (gtk_tree_view_get_headers_visible (GTK_TREE_VIEW (widget)) == TRUE)
		{
			column->width = MAX (column->requested_width, column->button_request);
		}
		else
		{
			column->width = column->requested_width;
			if (column->width < 0)
				column->width = 0;
		}

		if (g_list_next (l) == NULL &&
		    width + column->width < widget->allocation.width)
		{
			column->width += widget->allocation.width - column->width - width;
		}
		else if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column), "expand")) == TRUE)
		{
			column->width += (left_over_width / n_expand_columns);
		}

		g_object_notify (G_OBJECT (column), "width");

		allocation.y = 0;
		allocation.height = column->button->allocation.height; 
		allocation.x = width;
		allocation.width = column->width;
		gtk_widget_size_allocate (column->button, &allocation);
		if (column->window)
			gdk_window_move_resize (column->window,
						allocation.x + allocation.width - TREE_VIEW_DRAG_WIDTH / 2,
						allocation.y,
						TREE_VIEW_DRAG_WIDTH, allocation.height);

		width += column->width;
	}

	g_list_free (columns);
}

gboolean
rb_node_view_get_node_visible (RBNodeView *view,
			       RBNode *node)
{
	gboolean visible;
	GtkTreeIter iter;
	GValue val = { 0, };

	rb_tree_model_node_iter_from_node (RB_TREE_MODEL_NODE (view->priv->nodemodel),
					   node, &iter);
	gtk_tree_model_get_value (GTK_TREE_MODEL (view->priv->nodemodel), &iter,
				  RB_TREE_MODEL_NODE_COL_VISIBLE, &val);
	visible = g_value_get_boolean (&val);
	g_value_unset (&val);

	return visible;
}

void
rb_node_view_enable_drag_source (RBNodeView *view,
				 const GtkTargetEntry *targets,
				 int n_targets)
{
	g_return_if_fail (view != NULL);

	egg_tree_multi_drag_add_drag_support (GTK_TREE_VIEW (view->priv->treeview));

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (view->priv->treeview),
						GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
						targets, n_targets, GDK_ACTION_COPY);
}

static void
rb_node_view_columns_config_changed_cb (GConfClient* client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer user_data)
{
	RBNodeView *view = user_data;
	GConfValue *value;
	const char *str;

	g_return_if_fail (RB_IS_NODE_VIEW (view));

	value = gconf_entry_get_value (entry);
	str = gconf_value_get_string (value);

	rb_node_view_columns_parse (view, str);
}

static void
rb_node_view_columns_parse (RBNodeView *view,
			    const char *config)
{
	int i;
	char **items;
	GEnumValue *ev;
	GList *visible_columns = NULL;
	GtkTreeViewColumn *column = NULL;
	GEnumClass *class = g_type_class_ref (RB_TYPE_TREE_MODEL_NODE_COLUMN);
	
	g_return_if_fail (view != NULL);
	g_return_if_fail (config != NULL);

	/* the list of visible columns */
	items = g_strsplit (config, ",", 0);
	if (items != NULL) 
	{
		for (i = 0; items[i] != NULL; i++)
		{
			ev = g_enum_get_value_by_name (class, items[i]);

			if ((ev != NULL) 
			    && (ev->value >= 0) 
			    && (ev->value < RB_TREE_MODEL_NODE_NUM_COLUMNS))
			{
				visible_columns = g_list_append (visible_columns, 
								 GINT_TO_POINTER (ev->value));
			}
		}

		g_strfreev (items);
	}

	g_type_class_unref (class);

	/* set the visibility for all columns */
	for (i = 0; i < RB_TREE_MODEL_NODE_NUM_COLUMNS; i++)
	{
		switch (i)
		{
		case RB_TREE_MODEL_NODE_COL_PLAYING:
		case RB_TREE_MODEL_NODE_COL_TITLE:
		case RB_TREE_MODEL_NODE_COL_VISIBLE:
		case RB_TREE_MODEL_NODE_COL_PRIORITY:
		case RB_TREE_MODEL_NODE_NUM_COLUMNS:
			/* nothing to do for these */
			break;
			
		default:
			column = g_hash_table_lookup (view->priv->columns, 
						      GINT_TO_POINTER (i));
			if (column != NULL) 
			{
				gboolean visible = g_list_find (visible_columns, GINT_TO_POINTER (i)) != NULL;
				gtk_tree_view_column_set_visible (column, visible);
			}
			break;

		}
	}

	if (visible_columns != NULL)
		g_list_free (visible_columns);
}
