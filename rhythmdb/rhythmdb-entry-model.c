/* 
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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

#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "rhythmdb-entry-model.h"
#include "rb-debug.h"

static void rhythmdb_entry_model_class_init (RhythmDBEntryModelClass *klass);
static void rhythmdb_entry_model_tree_model_init (GtkTreeModelIface *iface);
static void rhythmdb_entry_model_init (RhythmDBEntryModel *shell_player);
static void rhythmdb_entry_model_finalize (GObject *object);
static void rhythmdb_entry_model_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_entry_model_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);

struct RhythmDBEntryModelPrivate
{
	RhythmDB *db;

	guint stamp;

	guint n_columns;
	GType *column_types;

	GPtrArray *entries;
};

enum
{
	PROP_0,
	PROP_RHYTHMDB,
}

static GObjectClass *parent_class = NULL;

GType
rhythmdb_entry_model_get_type (void)
{
	static GType rhythmdb_entry_model_type = 0;

	if (rhythmdb_entry_model_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBEntryModelClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_entry_model_class_init,
			NULL,
			NULL,
			sizeof (RhythmDBEntryModel),
			0,
			(GInstanceInitFunc) rhythmdb_entry_model_init
		};

		rhythmdb_entry_model_type = g_type_register_static (G_TYPE_OBJECT,
								    "RhythmDBEntryModel",
								    &our_info, 0);

		static const GInterfaceInfo tree_model_info =
		{
			(GInterfaceInitFunc) rhythmdb_entry_model_tree_model_init,
			NULL,
			NULL
		};
	}

	return rhythmdb_entry_model_type;
}

static void
rhythmdb_entry_model_class_init (RhythmDBEntryModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rhythmdb_entry_model_set_property;
	object_class->get_property = rhythmdb_entry_model_get_property;

	object_class->finalize = rhythmdb_entry_model_finalize;

	g_object_class_install_property (object_class,
					 PROP_RHYTHMDB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));
}

static void
rhythmdb_entry_model_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags = rhythmdb_entry_model_get_flags;
	iface->get_n_columns = rhythmdb_entry_model_get_n_columns;
	iface->get_column_type = rhythmdb_entry_model_get_column_type;
	iface->get_iter = rhythmdb_entry_model_get_iter;
	iface->get_path = rhythmdb_entry_model_get_path;
	iface->get_value = rhythmdb_entry_model_get_value;
	iface->iter_next = rhythmdb_entry_model_iter_next;
	iface->iter_children = rhythmdb_entry_model_iter_children;
	iface->iter_has_child = rhythmdb_entry_model_iter_has_child;
	iface->iter_n_children = rhythmdb_entry_model_iter_n_children;
	iface->iter_nth_child = rhythmdb_entry_model_iter_nth_child;
	iface->iter_parent = rhythmdb_entry_model_iter_parent;
}

static void
rhythmdb_entry_model_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (object);

	switch (prop_id)
	{
	case PROP_RHYTHMDB:
		source->priv->db = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rhythmdb_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_object (value, source->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GType
extract_gtype_from_enum_entry (GEnumClass *klass, guint i)
{
	GEnumValue *value;
	char *typename;
	char *typename_end;
	
	value = g_enum_get_value (klass, i);
	typename = strstr (value->value_nick, "(");
	g_return_val_if_fail (typename != NULL, G_TYPE_INVALID);

	typename_end = strstr (typename, ")");
	typename++;
	typename = g_strndup (typename, typename_end-typename);
	return g_type_from_name (typename);
}

static void
rhythmdb_entry_model_init (RhythmDBEntryModel *model)
{
	GtkWidget *align;
	GEnumClass *enum_class;
	int i;

	model->priv = g_new0 (RhythmDBEntryModelPrivate, 1);

	model->priv->stamp = g_random_int ();
  
	prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

	/* First figure out how many columns we should have */
	model->priv->n_columns = prop_class->n_values + unsaved_prop_class->n_values;

	model->priv->column_types = g_new (GType, model->priv->n_columns);

	/* Now, extract the GType of each column from the enum descriptions,
	 * and cache that for later use. */
	for (i = 0; i < prop_class->n_values; i++)
		model->priv->column_types[i] = extract_gtype_from_enum_entry (prop_class, i);
	
	for (; i < unsaved_prop_class->n_values; i++)
		model->priv->column_types[i] = extract_gtype_from_enum_entry (unsaved_prop_class, i);

	g_type_class_unref (prop_class);
	g_type_class_unref (unsaved_prop_class);

}

static void
rhythmdb_entry_model_finalize (GObject *object)
{
	RhythmDBEntryModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE_HEADER (object));

	model = RHYTHMDB_ENTRY_MODEL (object);

	g_return_if_fail (model->priv != NULL);

	g_free (model->priv->column_types);
	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RhythmDBEntryModel *
rhythmdb_entry_model_new (void)
{
	RhythmDBEntryModel *model = g_object_new (RHYTHMDB_TYPE_DB, NULL);

	g_return_val_if_fail (model->priv != NULL, NULL);

	return model;
}

static GtkTreeModelFlags
rhythmdb_entry_model_get_flags (GtkTreeModel *model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint
rhythmdb_entry_model_get_n_columns (GtkTreeModel *tree_model)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	return model->n_columns;
}

static GType
rhythmdb_entry_model_get_column_type (GtkTreeModel *tree_model,
				      gint          index)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	g_return_val_if_fail (index >= 0 && index < model->priv->n_columns,
			      G_TYPE_INVALID);

	return model->priv->column_types[index];
}

static gboolean
rhythmdb_entry_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
			       GtkTreePath  *path)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);
	guint index;
	RhythmDBEntry *ret;

	index = gtk_tree_path_get_indices (path)[0];

	if (index >= model->priv->entries->len)
		return FALSE;

	ret = g_ptr_array_index (model->priv->entries, index);
	g_assert (ret);

	iter->stamp = list_store->stamp;
	iter->user_data = GUINT_TO_POINTER (index);

	return TRUE;
}

static GtkTreePath *
rhythmdb_entry_model_get_path (GtkTreeModel *tree_model,
			       GtkTreeIter  *iter)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);
	GtkTreePath *path;

	g_return_val_if_fail (iter->stamp == model->priv->stamp, NULL);

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, GPOINTER_TO_UINT (iter->priv->user_data));
	return path;
}

static void
rhythmdb_entry_model_get_value (GtkTreeModel *tree_model, GtkTreeIter *iter,
				gint column, GValue *value)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	g_return_if_fail (column < model->priv->n_columns);
	g_return_if_fail (model->priv->stamp == iter->stamp);

	g_value_init (value, model->priv->column_types[column]);
	rhythmdb_entry_get (model->priv->db, g_ptr_array_index (model->priv->entries, column),
			    column, value);
}

static gboolean
rhythmdb_entry_model_iter_next (GtkTreeModel  *tree_model,
				GtkTreeIter   *iter)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	g_return_val_if_fail (iter->stamp == model->priv->stamp, NULL);

	iter->user_data = GUINT_TO_POINTER (GPOINTER_TO_UINT (iter->user_data)+1);

	return (GPOINTER_TO_UINT (iter->user_data) < model->priv->entries->len);
}

static gboolean
rhythmdb_entry_model_iter_children (GtkTreeModel *tree_model,
				    GtkTreeIter  *iter,
				    GtkTreeIter  *parent)
{
	return FALSE;
}

static gboolean
rhythmdb_entry_model_iter_has_child (GtkTreeModel *tree_model,
				     GtkTreeIter  *iter)
{
	return FALSE;
}

static gint
rhythmdb_entry_model_iter_n_children (GtkTreeModel *tree_model,
				      GtkTreeIter  *iter)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	if (iter == NULL)
		return model->priv->entries->len;

	g_return_val_if_fail (model->priv->stamp == iter->stamp, -1);
	return 0;
}

static gboolean
rhythmdb_entry_model_iter_nth_child (GtkTreeModel *tree_model,
				     GtkTreeIter *iter, GtkTreeIter *parent,
				     gint n)
{
	RhythmDBEntryModel *model = RHYTHMDB_ENTRY_MODEL (tree_model);

	if (parent) return FALSE;

	if (n > model->priv->entries->len)
		return FALSE;

	iter->stamp = model->priv->stamp;
	iter->user_data = GUINT_TO_POINTER (n);
	return TRUE;
}

static gboolean
rhythmdb_entry_model_iter_parent (GtkTreeModel *tree_model,
				  GtkTreeIter  *iter,
				  GtkTreeIter  *child)
{
	return FALSE;
}
