/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2013 Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <sources/rb-display-page-menu.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>

static void rb_display_page_menu_class_init (RBDisplayPageMenuClass *klass);
static void rb_display_page_menu_init (RBDisplayPageMenu *menu);

struct _RBDisplayPageMenuPrivate
{
	RBDisplayPageModel *model;
	GtkTreeModel *real_model;
	RBDisplayPage *root_page;
	GType page_type;
	char *action;

	int item_count;
};

G_DEFINE_TYPE (RBDisplayPageMenu, rb_display_page_menu, G_TYPE_MENU_MODEL);

/**
 * SECTION:rbdisplaypagemenu
 * @short_description: #GMenu populated with a portion of the display page model
 *
 */

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_ROOT_PAGE,
	PROP_PAGE_TYPE,
	PROP_ACTION
};


static gboolean
get_page_iter (RBDisplayPageMenu *menu, GtkTreeIter *iter)
{
	GtkTreeIter parent;

	if (rb_display_page_model_find_page_full (menu->priv->model, menu->priv->root_page, &parent) == FALSE)
		return FALSE;

	if (gtk_tree_model_iter_children (menu->priv->real_model, iter, &parent) == FALSE) {
		return FALSE;
	}

	return TRUE;
}

static GtkTreePath *
get_root_path (RBDisplayPageMenu *menu)
{
	GtkTreeIter iter;

	if (rb_display_page_model_find_page_full (menu->priv->model, menu->priv->root_page, &iter) == FALSE)
		return NULL;

	return gtk_tree_model_get_path (menu->priv->real_model, &iter);
}

static gboolean
consider_page (RBDisplayPageMenu *menu, RBDisplayPage *page)
{
	gboolean visible;

	if (G_TYPE_CHECK_INSTANCE_TYPE (page, menu->priv->page_type) == FALSE)
		return FALSE;

	g_object_get (page, "visibility", &visible, NULL);
	return visible;
}

static RBDisplayPage *
get_page_at_index (RBDisplayPageMenu *menu, int index, GtkTreeIter *iter)
{
	int i;

	if (get_page_iter (menu, iter) == FALSE)
		return NULL;

	i = 0;
	do {
		RBDisplayPage *page;
		gboolean counted;

		gtk_tree_model_get (menu->priv->real_model,
				    iter,
				    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
				    -1);

		counted = consider_page (menu, page);
		if (counted && index == i) {
			return page;
		} else if (counted) {
			i++;
		}

		g_object_unref (page);
	} while (gtk_tree_model_iter_next (menu->priv->real_model, iter));

	return NULL;
}

static int
count_items (RBDisplayPageMenu *menu, int upto)
{
	GtkTreeIter iter;
	int i;
	int c;

	if (get_page_iter (menu, &iter) == FALSE) {
		return 0;
	}

	i = 0;
	c = 0;
	while (c < upto) {
		RBDisplayPage *page;
		gtk_tree_model_get (menu->priv->real_model,
				    &iter,
				    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
				    -1);

		if (consider_page (menu, page)) {
			i++;
		}
		g_object_unref (page);
		c++;
		if (!gtk_tree_model_iter_next (menu->priv->real_model, &iter))
			break;
	}

	return i;
}


static gboolean
impl_is_mutable (GMenuModel *menu_model)
{
	return TRUE;
}

static int
impl_get_n_items (GMenuModel *menu_model)
{
	RBDisplayPageMenu *menu = RB_DISPLAY_PAGE_MENU (menu_model);
	return menu->priv->item_count;
}

static void
impl_get_item_attributes (GMenuModel *menu_model, int item_index, GHashTable **attrs)
{
	RBDisplayPageMenu *menu = RB_DISPLAY_PAGE_MENU (menu_model);
	RBDisplayPage *page;
	GtkTreeIter iter;

	*attrs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);

	page = get_page_at_index (menu, item_index, &iter);
	if (page != NULL) {
		char *name;
		char *ptr;
		GVariant *v;

		g_object_get (page, "name", &name, NULL);
		rb_debug ("page at %d is %s", item_index, name);
		g_hash_table_insert (*attrs, g_strdup ("label"), g_variant_new_string (name));
		g_free (name);

		g_hash_table_insert (*attrs, g_strdup ("action"), g_variant_new_string (menu->priv->action));

		ptr = g_strdup_printf ("%p", page);
		v = g_variant_new_string (ptr);
		g_hash_table_insert (*attrs, g_strdup ("target"), g_variant_ref_sink (v));
		g_free (ptr);

		g_object_unref (page);
	} else {
		rb_debug ("no page at %d", item_index);
	}
}

static void
impl_get_item_links (GMenuModel *menu_model, int item_index, GHashTable **links)
{
	/* we never have any links */
	*links = g_hash_table_new (g_str_hash, g_str_equal);
}

static int
path_menu_index (RBDisplayPageMenu *menu, GtkTreePath *path)
{
	GtkTreePath *root;
	GtkTreePath *compare;
	int depth;
	int *indices;
	int index;

	compare = gtk_tree_path_copy (path);
	if (gtk_tree_path_up (compare) == FALSE) {
		gtk_tree_path_free (compare);
		return -1;
	}

	if (gtk_tree_path_get_depth (compare) == 0) {
		gtk_tree_path_free (compare);
		return -1;
	}

	root = get_root_path (menu);
	if (root == NULL) {
		gtk_tree_path_free (compare);
		return -1;
	}

	if (gtk_tree_path_compare (compare, root) != 0) {
		gtk_tree_path_free (root);
		gtk_tree_path_free (compare);
		return -1;
	}

	indices = gtk_tree_path_get_indices_with_depth (path, &depth);
	index = count_items (menu, indices[depth-1]);
	gtk_tree_path_free (root);
	gtk_tree_path_free (compare);
	return index;
}

static void
rebuild_menu (RBDisplayPageMenu *menu)
{
	int oldnum;
	oldnum = menu->priv->item_count;
	menu->priv->item_count = count_items (menu, G_MAXINT);
	rb_debug ("building menu, %d => %d items", oldnum, menu->priv->item_count);
	g_menu_model_items_changed (G_MENU_MODEL (menu), 0, oldnum, menu->priv->item_count);
}

static gboolean
consider_page_iter (RBDisplayPageMenu *menu, GtkTreeIter *iter)
{
	RBDisplayPage *page;
	gboolean result;

	gtk_tree_model_get (menu->priv->real_model,
			    iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	if (page == NULL)
		return FALSE;

	result = consider_page (menu, page);
	g_object_unref (page);
	return result;
}

static void
row_changed_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBDisplayPageMenu *menu)
{
	int index;

	if (consider_page_iter (menu, iter) == FALSE) {
		return;
	}

	index = path_menu_index (menu, path);
	if (index != -1) {
		g_menu_model_items_changed (G_MENU_MODEL (menu), index, 1, 1);
	}
}

static void
row_inserted_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBDisplayPageMenu *menu)
{
	int index;

	if (consider_page_iter (menu, iter) == FALSE) {
		return;
	}
       
	index = path_menu_index (menu, path);
	if (index != -1) {
		menu->priv->item_count++;
		g_menu_model_items_changed (G_MENU_MODEL (menu), index, 0, 1);
	}
}

static void
row_deleted_cb (GtkTreeModel *model, GtkTreePath *path, RBDisplayPageMenu *menu)
{
	int index = path_menu_index (menu, path);
	if (index != -1) {
		if (count_items (menu, G_MAXINT) != menu->priv->item_count) {
			menu->priv->item_count--;
			g_menu_model_items_changed (G_MENU_MODEL (menu), index, 1, 0);
		}
	}
}

static void
rows_reordered_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer new_order, RBDisplayPageMenu *menu)
{
	GtkTreePath *root;
	root = get_root_path (menu);
	if (root != NULL) {
		if (gtk_tree_path_compare (path, root) == 0)
			rebuild_menu (menu);

		gtk_tree_path_free (root);
	}
}


static void
impl_finalize (GObject *object)
{
	RBDisplayPageMenu *menu;

	menu = RB_DISPLAY_PAGE_MENU (object);
	g_free (menu->priv->action);

	G_OBJECT_CLASS (rb_display_page_menu_parent_class)->finalize (object);
}

static void
impl_dispose (GObject *object)
{
	RBDisplayPageMenu *menu;

	menu = RB_DISPLAY_PAGE_MENU (object);

	if (menu->priv->real_model) {
		g_signal_handlers_disconnect_by_data (menu->priv->real_model, menu);
		menu->priv->real_model = NULL;
	}

	g_clear_object (&menu->priv->model);
	g_clear_object (&menu->priv->root_page);

	G_OBJECT_CLASS (rb_display_page_menu_parent_class)->dispose (object);
}

static void
impl_constructed (GObject *object)
{
	RBDisplayPageMenu *menu;

	RB_CHAIN_GOBJECT_METHOD (rb_display_page_menu_parent_class, constructed, object);

	menu = RB_DISPLAY_PAGE_MENU (object);

	g_signal_connect (menu->priv->real_model, "row-inserted", G_CALLBACK (row_inserted_cb), menu);
	g_signal_connect (menu->priv->real_model, "row-deleted", G_CALLBACK (row_deleted_cb), menu);
	g_signal_connect (menu->priv->real_model, "row-changed", G_CALLBACK (row_changed_cb), menu);
	g_signal_connect (menu->priv->real_model, "rows-reordered", G_CALLBACK (rows_reordered_cb), menu);

	rebuild_menu (menu);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBDisplayPageMenu *menu = RB_DISPLAY_PAGE_MENU (object);

	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_object (value, menu->priv->model);
		break;
	case PROP_ROOT_PAGE:
		g_value_set_object (value, menu->priv->root_page);
		break;
	case PROP_PAGE_TYPE:
		g_value_set_gtype (value, menu->priv->page_type);
		break;
	case PROP_ACTION:
		g_value_set_string (value, menu->priv->action);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBDisplayPageMenu *menu = RB_DISPLAY_PAGE_MENU (object);

	switch (prop_id) {
	case PROP_MODEL:
		menu->priv->model = g_value_get_object (value);
		menu->priv->real_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (menu->priv->model));
		break;
	case PROP_ROOT_PAGE:
		menu->priv->root_page = g_value_get_object (value);
		break;
	case PROP_PAGE_TYPE:
		menu->priv->page_type = g_value_get_gtype (value);
		break;
	case PROP_ACTION:
		menu->priv->action = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_display_page_menu_init (RBDisplayPageMenu *menu)
{
	menu->priv = G_TYPE_INSTANCE_GET_PRIVATE (menu, RB_TYPE_DISPLAY_PAGE_MENU, RBDisplayPageMenuPrivate);
}

static void
rb_display_page_menu_class_init (RBDisplayPageMenuClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GMenuModelClass *menu_class = G_MENU_MODEL_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->finalize = impl_finalize;
	object_class->dispose = impl_dispose;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	menu_class->is_mutable = impl_is_mutable;
	menu_class->get_n_items = impl_get_n_items;
	menu_class->get_item_attributes = impl_get_item_attributes;
	menu_class->get_item_links = impl_get_item_links;


	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "model",
							      "display page model",
							      RB_TYPE_DISPLAY_PAGE_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ROOT_PAGE,
					 g_param_spec_object ("root-page",
							      "root page",
							      "root page",
							      RB_TYPE_DISPLAY_PAGE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_PAGE_TYPE,
					 g_param_spec_gtype ("page-type",
							     "page type",
							     "page type",
							     RB_TYPE_DISPLAY_PAGE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ACTION,
					 g_param_spec_string ("action",
							      "action",
							      "action name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBDisplayPageMenuPrivate));
}


/**
 * rb_display_page_menu_new:
 * @model: the #RBDisplayPageModel
 * @root: the page below which to search for pages to build the menu
 * @page_type: type of pages to add to the menu
 * @action: action name for the menu items
 *
 * Creates a menu from pages of type @page_type that are located 
 * below @root in the model.  The menu items are associated with
 * the given action name, and include a path string to the selected
 * page as the action target.  Use @rb_display_page_menu_get_page
 * to retrieve the page object.
 *
 * The menu is kept up to date as pages are added, removed, hidden
 * and shown in the model.
 *
 * Return value: new menu
 */
GMenuModel *
rb_display_page_menu_new (RBDisplayPageModel *model,
			  RBDisplayPage *root,
			  GType page_type,
			  const char *action)
{
	return G_MENU_MODEL (g_object_new (RB_TYPE_DISPLAY_PAGE_MENU,
					   "model", model,
					   "root-page", root,
					   "page-type", page_type,
					   "action", action,
					   NULL));
}

/**
 * rb_display_page_menu_get_page:
 * @model: the #RBDisplayPageModel
 * @parameters: action parameters
 *
 * Retrieves the page instance for an action invocation
 * given the action parameters.
 *
 * Return value: (transfer full): page instance
 */
RBDisplayPage *
rb_display_page_menu_get_page (RBDisplayPageModel *model, GVariant *parameters)
{
	GtkTreeIter iter;
	void *ptr;

	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE_STRING) == FALSE) {
		rb_debug ("can't find page, variant type is %s", g_variant_get_type_string (parameters));
		return NULL;
	}

	rb_debug ("trying to find page for %s", g_variant_get_string (parameters, NULL));

	if (sscanf (g_variant_get_string (parameters, NULL), "%p", &ptr) != 1) {
		rb_debug ("can't parse parameter");
		return NULL;
	}

	if (rb_display_page_model_find_page_full (model, ptr, &iter) == FALSE) {
		rb_debug ("can't find page matching %p", ptr);
		return NULL;
	}

	return g_object_ref (RB_DISPLAY_PAGE (ptr));
}
