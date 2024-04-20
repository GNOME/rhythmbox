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

#include <lib/rb-list-model.h>

enum {
	ITEMS_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBListModel
{
	GObject parent;

	GType item_type;
	GArray *items;
};

struct _RBListModelClass
{
	GObjectClass parent;
};

G_DEFINE_TYPE (RBListModel, rb_list_model, G_TYPE_OBJECT);

static void rb_list_model_class_init (RBListModelClass *klass);
static void rb_list_model_init (RBListModel *model);

/**
 * SECTION:rblistmodel
 * @short_description: simple list model
 *
 * Stores a list of items and emits notification signals on changes.
 */

static void
impl_finalize (GObject *object)
{
	RBListModel *model = RB_LIST_MODEL (object);

	g_array_free (model->items, TRUE);

	G_OBJECT_CLASS (rb_list_model_parent_class)->finalize (object);
}

static void
rb_list_model_init (RBListModel *model)
{
}

static void
rb_list_model_class_init (RBListModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = impl_finalize;

	signals[ITEMS_CHANGED] =
		g_signal_new ("items-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      3, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
}

/**
 * rb_list_model_new:
 * @item_type: a #GType for items in the list
 * @destroy_item: callback for destroying list items
 *
 * Creates a new empty list model.
 *
 * Return value: (transfer full): the model
 */
RBListModel *
rb_list_model_new (GType item_type, GDestroyNotify destroy_item)
{
	RBListModel *model = RB_LIST_MODEL (g_object_new (RB_TYPE_LIST_MODEL, NULL));
	model->item_type = item_type;
	model->items = g_array_new (FALSE, FALSE, sizeof (gpointer));
	g_array_set_clear_func (model->items, destroy_item);

	return model;
}

/**
 * rb_list_model_get_item_type:
 * @model: an #RBListModel
 *
 * Returns the list entry type.
 *
 * Return value: list entry type
 */
GType
rb_list_model_get_item_type (RBListModel *model)
{
	return model->item_type;
}

/**
 * rb_list_model_n_items:
 * @model: an #RBListModel
 *
 * Returns the length of the list.
 *
 * Return value: list length
 */
int
rb_list_model_n_items (RBListModel *model)
{
	return model->items->len;
}

/**
 * rb_list_model_get:
 * @model: an #RBListModel
 * @index: item to retrieve
 *
 * Returns an item from the list.
 *
 * Return value: (transfer none): item at the specified index
 */
gpointer
rb_list_model_get (RBListModel *model, int index)
{
	g_return_val_if_fail (RB_IS_LIST_MODEL (model), NULL);
	g_return_val_if_fail (index >= 0, NULL);
	g_return_val_if_fail (index < model->items->len, NULL);

	return g_array_index (model->items, gpointer, index);
}

/**
 * rb_list_model_find:
 * @model: an #RBListModel
 * @item: item to find
 *
 * Returns the lowest index at which @item appears in the list,
 * or -1 if the item is not in the list.
 *
 * Return value: list index
 */
int
rb_list_model_find (RBListModel *model, gpointer item)
{
	int i;
	g_return_val_if_fail (RB_IS_LIST_MODEL (model), -1);
	if (model->item_type != G_TYPE_NONE) {
		g_return_val_if_fail (G_TYPE_CHECK_INSTANCE_TYPE (item, model->item_type), -1);
	}

	for (i = 0; i < model->items->len; i++) {
		if (g_array_index (model->items, gpointer, i) == item)
			return i;
	}
	return -1;
}

static void
items_changed (RBListModel *model, int position, int removed, int added)
{
	g_signal_emit (model, signals[ITEMS_CHANGED], 0, position, removed, added);
}

/**
 * rb_list_model_insert:
 * @model: an #RBListModel
 * @index: position to insert the item at
 * @item: item to insert
 *
 * Inserts at item into the list.  If @index is less than zero or
 * greater than the length of the list, the item is appended to the
 * list.
 */
void
rb_list_model_insert (RBListModel *model, int index, gpointer item)
{
	g_return_if_fail (RB_IS_LIST_MODEL (model));
	if (model->item_type != G_TYPE_NONE) {
		g_return_if_fail (G_TYPE_CHECK_INSTANCE_TYPE (item, model->item_type));
	}

	if (index < 0 || index > model->items->len)
		index = model->items->len;

	g_array_insert_val (model->items, index, item);
	items_changed (model, index, 0, 1);
}

/**
 * rb_list_model_append:
 * @model: an #RBListModel
 * @item: item to append
 *
 * Appends @item to the list.
 */
void
rb_list_model_append (RBListModel *model, gpointer item)
{
	rb_list_model_insert (model, -1, item);
}

/**
 * rb_list_model_prepend:
 * @model: an #RBListModel
 * @item: item to prepend
 *
 * Prepends @item to the list.
 */
void
rb_list_model_prepend (RBListModel *model, gpointer item)
{
	rb_list_model_insert (model, 0, item);
}

/**
 * rb_list_model_remove:
 * @model: an #RBListModel
 * @index: index of the item to remove
 *
 * Removes the item at @index from the list.
 */
void
rb_list_model_remove (RBListModel *model, int index)
{
	g_return_if_fail (RB_IS_LIST_MODEL (model));
	g_return_if_fail (index >= 0);
	g_return_if_fail (index < model->items->len);

	g_array_remove_index (model->items, index);
	items_changed (model, index, 1, 0);
}

/**
 * rb_list_model_remove_item:
 * @model: an #RBListModel
 * @item: item to remove
 *
 * Removes @item from the list.  If the item appears in the
 * list multiple times, only the first instance is removed.
 */
void
rb_list_model_remove_item (RBListModel *model, gpointer item)
{
	int index;

	index = rb_list_model_find (model, item);
	rb_list_model_remove (model, index);
}
