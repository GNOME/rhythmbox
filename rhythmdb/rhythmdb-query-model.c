/* 
 *  arch-tag: Implementation of RhythmDB query result GtkTreeModel
 *
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
 */

#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "rhythmdb-model.h"
#include "rhythmdb-query-model.h"
#include "rb-debug.h"
#include "gsequence.h"

static void rhythmdb_query_model_class_init (RhythmDBQueryModelClass *klass);
static void rhythmdb_query_model_tree_model_init (GtkTreeModelIface *iface);
static void rhythmdb_query_model_rhythmdb_model_init (RhythmDBModelIface *iface);
static void rhythmdb_query_model_init (RhythmDBQueryModel *shell_player);
static void rhythmdb_query_model_finalize (GObject *object);
static void rhythmdb_query_model_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_query_model_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_query_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
						 RhythmDBQueryModel *model);
static void rhythmdb_query_model_entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
						   RhythmDBQueryModel *model);
static void rhythmdb_query_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
						   RhythmDBQueryModel *model);
static gboolean rhythmdb_query_model_entry_to_iter (RhythmDBModel *rmodel, RhythmDBEntry *entry,
						    GtkTreeIter *iter);
static void rhythmdb_query_model_cancel (RhythmDBModel *model);
static gboolean rhythmdb_query_model_poll (RhythmDBModel *model, GTimeVal *timeout);

static GtkTreeModelFlags rhythmdb_query_model_get_flags (GtkTreeModel *model);
static gint rhythmdb_query_model_get_n_columns (GtkTreeModel *tree_model);
static GType rhythmdb_query_model_get_column_type (GtkTreeModel *tree_model, int index);
static gboolean rhythmdb_query_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
					       GtkTreePath  *path);
static GtkTreePath * rhythmdb_query_model_get_path (GtkTreeModel *tree_model,
						    GtkTreeIter  *iter);
static void rhythmdb_query_model_get_value (GtkTreeModel *tree_model, GtkTreeIter *iter,
					    gint column, GValue *value);
static gboolean rhythmdb_query_model_iter_next (GtkTreeModel  *tree_model,
						GtkTreeIter   *iter);
static gboolean rhythmdb_query_model_iter_children (GtkTreeModel *tree_model,
						    GtkTreeIter  *iter,
						    GtkTreeIter  *parent);
static gboolean rhythmdb_query_model_iter_has_child (GtkTreeModel *tree_model,
						     GtkTreeIter  *iter);
static gint rhythmdb_query_model_iter_n_children (GtkTreeModel *tree_model,
						  GtkTreeIter  *iter);
static gboolean rhythmdb_query_model_iter_nth_child (GtkTreeModel *tree_model,
						     GtkTreeIter *iter, GtkTreeIter *parent,
						     gint n);
static gboolean rhythmdb_query_model_iter_parent (GtkTreeModel *tree_model,
						  GtkTreeIter  *iter,
						  GtkTreeIter  *child);
static GSequencePtr choose_sequence_element (GSequence *seq);


struct RhythmDBQueryModelUpdate
{
	enum {
		RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED,
		RHYTHMDB_QUERY_MODEL_UPDATE_ROW_CHANGED,
		RHYTHMDB_QUERY_MODEL_UPDATE_ROW_DELETED,
		RHYTHMDB_QUERY_MODEL_QUERY_COMPLETE,
	} type;
	RhythmDBEntry *entry;
};

struct RhythmDBQueryModelPrivate
{
	RhythmDB *db;

	GCompareDataFunc sort_func;
	gpointer sort_user_data;

	GPtrArray *query;

	guint stamp;

	GSequence *entries;
	GHashTable *reverse_map;
	
	GMutex *lock;
	
	/* row_inserted/row_changed/row_deleted */
	GAsyncQueue *pending_updates;
};

enum
{
	PROP_0,
	PROP_RHYTHMDB,
	PROP_QUERY,
	PROP_SORT_FUNC,
	PROP_SORT_DATA,
};

enum
{
	COMPLETE,
	LAST_SIGNAL
};

static guint rhythmdb_query_model_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
rhythmdb_query_model_get_type (void)
{
	static GType rhythmdb_query_model_type = 0;

	if (rhythmdb_query_model_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBQueryModelClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_query_model_class_init,
			NULL,
			NULL,
			sizeof (RhythmDBQueryModel),
			0,
			(GInstanceInitFunc) rhythmdb_query_model_init
		};

		rhythmdb_query_model_type = g_type_register_static (G_TYPE_OBJECT,
								    "RhythmDBQueryModel",
								    &our_info, 0);

		static const GInterfaceInfo tree_model_info =
		{
			(GInterfaceInitFunc) rhythmdb_query_model_tree_model_init,
			NULL,
			NULL
		};

		static const GInterfaceInfo rhythmdb_model_info =
		{
			(GInterfaceInitFunc) rhythmdb_query_model_rhythmdb_model_init,
			NULL,
			NULL
		};

		g_type_add_interface_static (rhythmdb_query_model_type,
					     GTK_TYPE_TREE_MODEL,
					     &tree_model_info);

		g_type_add_interface_static (rhythmdb_query_model_type,
					     RHYTHMDB_TYPE_MODEL,
					     &rhythmdb_model_info);
	}

	return rhythmdb_query_model_type;
}

static void
rhythmdb_query_model_class_init (RhythmDBQueryModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rhythmdb_query_model_set_property;
	object_class->get_property = rhythmdb_query_model_get_property;

	object_class->finalize = rhythmdb_query_model_finalize;

	g_object_class_install_property (object_class,
					 PROP_RHYTHMDB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_QUERY,
					 g_param_spec_pointer ("query",
							      "Query",
							      "RhythmDBQuery",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SORT_FUNC,
					 g_param_spec_pointer ("sort-func",
							      "SortFunc",
							      "Sort function",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SORT_DATA,
					 g_param_spec_pointer ("sort-data",
							      "SortData",
							      "Sort user data",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rhythmdb_query_model_signals[COMPLETE] =
		g_signal_new ("complete",
			      RHYTHMDB_TYPE_QUERY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryModelClass, complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
rhythmdb_query_model_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags = rhythmdb_query_model_get_flags;
	iface->get_n_columns = rhythmdb_query_model_get_n_columns;
	iface->get_column_type = rhythmdb_query_model_get_column_type;
	iface->get_iter = rhythmdb_query_model_get_iter;
	iface->get_path = rhythmdb_query_model_get_path;
	iface->get_value = rhythmdb_query_model_get_value;
	iface->iter_next = rhythmdb_query_model_iter_next;
	iface->iter_children = rhythmdb_query_model_iter_children;
	iface->iter_has_child = rhythmdb_query_model_iter_has_child;
	iface->iter_n_children = rhythmdb_query_model_iter_n_children;
	iface->iter_nth_child = rhythmdb_query_model_iter_nth_child;
	iface->iter_parent = rhythmdb_query_model_iter_parent;
}

static void
rhythmdb_query_model_rhythmdb_model_init (RhythmDBModelIface *iface)
{
	iface->entry_to_iter = rhythmdb_query_model_entry_to_iter;
	iface->poll = rhythmdb_query_model_poll;
	iface->cancel = rhythmdb_query_model_cancel;
}

static void
rhythmdb_query_model_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (object);

	switch (prop_id)
	{
	case PROP_RHYTHMDB:
	{
		model->priv->db = g_value_get_object (value);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_added",
					 G_CALLBACK (rhythmdb_query_model_entry_added_cb),
					 model, 0);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_restored",
					 G_CALLBACK (rhythmdb_query_model_entry_added_cb),
					 model, 0);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_changed",
					 G_CALLBACK (rhythmdb_query_model_entry_changed_cb),
					 model, 0);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_deleted",
					 G_CALLBACK (rhythmdb_query_model_entry_deleted_cb),
					 model, 0);
		break;
	}
	case PROP_QUERY:
		model->priv->query = rhythmdb_query_copy (g_value_get_pointer (value));
		break;
	case PROP_SORT_FUNC:
		model->priv->sort_func = g_value_get_pointer (value);
		break;
	case PROP_SORT_DATA:
		model->priv->sort_user_data = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rhythmdb_query_model_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (object);

	switch (prop_id)
	{
	case PROP_RHYTHMDB:
		g_value_set_object (value, model->priv->db);
		break;
	case PROP_QUERY:
		g_value_set_pointer (value, model->priv->query);
		break;
	case PROP_SORT_FUNC:
		g_value_set_pointer (value, model->priv->sort_func);
		break;
	case PROP_SORT_DATA:
		g_value_set_pointer (value, model->priv->sort_user_data);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_query_model_init (RhythmDBQueryModel *model)
{
	model->priv = g_new0 (RhythmDBQueryModelPrivate, 1);

	model->priv->stamp = g_random_int ();

	model->priv->entries = g_sequence_new (NULL);
	model->priv->reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);

	model->priv->lock = g_mutex_new ();

	model->priv->pending_updates = g_async_queue_new ();
}

static void
rhythmdb_query_model_finalize (GObject *object)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_QUERY_MODEL (object));

	model = RHYTHMDB_QUERY_MODEL (object);

	g_hash_table_destroy (model->priv->reverse_map);
	g_sequence_free (model->priv->entries);

	if (model->priv->query)
		rhythmdb_query_free (model->priv->query);

	g_async_queue_unref (model->priv->pending_updates);

	g_mutex_free (model->priv->lock);

	g_return_if_fail (model->priv != NULL);

	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RhythmDBQueryModel *
rhythmdb_query_model_new (RhythmDB *db, GPtrArray *query,
			  GCompareDataFunc sort_func,
			  gpointer user_data)
{
	RhythmDBQueryModel *model = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
						  "db", db, "query", query,
						  "sort-func", sort_func,
						  "sort-data", user_data, NULL);

	g_return_val_if_fail (model->priv != NULL, NULL);

	return model;
}

RhythmDBQueryModel *
rhythmdb_query_model_new_empty (RhythmDB *db)
{
	return g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
			     "db", db, NULL);
}

void
rhythmdb_query_model_complete (RhythmDBQueryModel *model)
{
	struct RhythmDBQueryModelUpdate *update;
	
	rb_debug ("queuing completion signal");
	update = g_new0 (struct RhythmDBQueryModelUpdate, 1);
	update->type = RHYTHMDB_QUERY_MODEL_QUERY_COMPLETE;

	g_async_queue_push (model->priv->pending_updates, update);
}

static void
rhythmdb_query_model_cancel (RhythmDBModel *model)
{
	rb_debug ("cancelling query");
}

static inline GSequencePtr
choose_sequence_element (GSequence *seq)
{
	return g_sequence_get_ptr_at_pos (seq, 0);
}

/* Threading: should be entered via database thread, with db lock held
 */
static void
rhythmdb_query_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
				     RhythmDBQueryModel *model)
{
	if (G_LIKELY (model->priv->query)) {
		if (rhythmdb_evaluate_query (db, model->priv->query, entry)) {
			rb_debug ("queueing entry addition");
			rhythmdb_query_model_add_entry (model, entry);
		}
	}
}

static void
rhythmdb_query_model_entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
				       RhythmDBQueryModel *model)
{
	if (g_hash_table_lookup (model->priv->reverse_map, entry) != NULL) {
		struct RhythmDBQueryModelUpdate *update;

		rb_debug ("queueing entry deletion");

		update = g_new (struct RhythmDBQueryModelUpdate, 1);
		update->type = RHYTHMDB_QUERY_MODEL_UPDATE_ROW_CHANGED;
		update->entry = entry;
		
		/* Called with a locked database */
		rhythmdb_entry_ref_unlocked (model->priv->db, entry);
		
		g_async_queue_push (model->priv->pending_updates, update);
	}
}

static void
rhythmdb_query_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
				       RhythmDBQueryModel *model)
{
	if (g_hash_table_lookup (model->priv->reverse_map, entry) != NULL) {
		struct RhythmDBQueryModelUpdate *update;

		rb_debug ("queueing entry deletion");

		update = g_new (struct RhythmDBQueryModelUpdate, 1);
		update->type = RHYTHMDB_QUERY_MODEL_UPDATE_ROW_DELETED;
		update->entry = entry;
		
		/* Called with a locked database */
		rhythmdb_entry_ref_unlocked (model->priv->db, entry);
		
		g_async_queue_push (model->priv->pending_updates, update);
	}
}

/* Threading: Called from the database context, holding a db write lock
 */
void
rhythmdb_query_model_add_entry (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	struct RhythmDBQueryModelUpdate *update;
		
	update = g_new (struct RhythmDBQueryModelUpdate, 1);
	update->type = RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED;
	update->entry = entry;

	/* Called with a locked database */
	rhythmdb_entry_ref_unlocked (model->priv->db, entry);

	g_async_queue_push (model->priv->pending_updates, update);
}

static int
compare_times (GTimeVal *a, GTimeVal *b)
{
	if (a->tv_sec == b->tv_sec)
		/* It's quite unlikely that microseconds are equal,
		 * so just ignore that case, we don't need a lot
		 * of precision.
		 */
		return a->tv_usec > b->tv_usec ? 1 : -1;
	else if (a->tv_sec > b->tv_sec)
		return 1;
	else
		return -1;
}

/* Threading: main thread only, should hold GDK lock
 */
static gboolean
rhythmdb_query_model_poll (RhythmDBModel *rmodel, GTimeVal *timeout)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (rmodel);
	GList *processed = NULL, *tem;
	GTimeVal now;
	struct RhythmDBQueryModelUpdate *update;
	guint count = 0;

	while ((update = g_async_queue_try_pop (model->priv->pending_updates)) != NULL) {
		GtkTreePath *path;
		GtkTreeIter iter;
		GSequencePtr ptr;
		int index;

		if (update == NULL)
			break;

		rb_debug ("processing update");

		iter.stamp = model->priv->stamp;

		switch (update->type) {
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED:
		{
			ptr = g_sequence_insert_sorted (model->priv->entries, update->entry,
							model->priv->sort_func,
							model->priv->sort_user_data);

			iter.user_data = ptr;
			g_hash_table_insert (model->priv->reverse_map,
					     update->entry,
					     ptr);

			path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
							      &iter);
				
			rb_debug ("emitting row inserted");
			gtk_tree_model_row_inserted (GTK_TREE_MODEL (model),
						     path, &iter);
			gtk_tree_path_free (path);
			break;
		}
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_CHANGED:
		{
			ptr = g_hash_table_lookup (model->priv->reverse_map,
						   update->entry);

			if (ptr == NULL)
				break;

			iter.user_data = ptr;

			path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
							      &iter);
				
			rb_debug ("emitting row changed");
			gtk_tree_model_row_changed (GTK_TREE_MODEL (model),
						    path, &iter);
			gtk_tree_path_free (path);
			break;
		}
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_DELETED:
		{
			GtkTreePath *path;

			ptr = g_hash_table_lookup (model->priv->reverse_map,
						   update->entry);

			if (ptr == NULL)
				break;

			index = g_sequence_ptr_get_position (ptr);

			path = gtk_tree_path_new ();

			gtk_tree_path_append_index (path, index);
				
			model->priv->stamp++;
			rb_debug ("emitting row deleted");
			gtk_tree_model_row_deleted (GTK_TREE_MODEL (model),
						    path);

			gtk_tree_path_free (path);
			g_sequence_remove (ptr);
			g_hash_table_remove (model->priv->reverse_map, update->entry);
			break;
		}
		case RHYTHMDB_QUERY_MODEL_QUERY_COMPLETE:
		{
			rb_debug ("emitting query complete");
			g_signal_emit (G_OBJECT (model), rhythmdb_query_model_signals[COMPLETE], 0);
			break;
		}
		}

		processed = g_list_prepend (processed, update);

		count++;
		if (timeout && count / 4 > 0) {
			/* Do this here at the bottom, so we do at least one update. */
			g_get_current_time (&now);
			if (compare_times (timeout,&now) < 0)
				break;
		}
	}
	
	for (tem = processed; tem; tem = tem->next) {
		struct RhythmDBQueryModelUpdate *update = tem->data;
			
		switch (update->type) {
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED:
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_CHANGED:
		case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_DELETED:
			rhythmdb_entry_unref (model->priv->db, update->entry);
			break;
		case RHYTHMDB_QUERY_MODEL_QUERY_COMPLETE:
			break;
		}

		g_free (update);
	}
		
	g_list_free (processed);
	return processed != NULL;
}

static gboolean
rhythmdb_query_model_entry_to_iter (RhythmDBModel *rmodel, RhythmDBEntry *entry,
				    GtkTreeIter *iter)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (rmodel);
	GSequencePtr ptr;

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);

	if (ptr == NULL)
		return FALSE;

	iter->stamp = model->priv->stamp;
	iter->user_data = ptr;
	return TRUE;
}

static GtkTreeModelFlags
rhythmdb_query_model_get_flags (GtkTreeModel *model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint
rhythmdb_query_model_get_n_columns (GtkTreeModel *tree_model)
{
/* 	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model); */

	return RHYTHMDB_NUM_PROPERTIES;
}

static GType
rhythmdb_query_model_get_column_type (GtkTreeModel *tree_model, int index)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);

	return rhythmdb_get_property_type (model->priv->db, index);
}

static gboolean
rhythmdb_query_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
			       GtkTreePath *path)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);
	guint index;
	RhythmDBEntry *ret;

	index = gtk_tree_path_get_indices (path)[0];

	if (index >= g_sequence_get_length (model->priv->entries))
		return FALSE;

	ret = g_sequence_get_ptr_at_pos (model->priv->entries, index);
	g_assert (ret);

	iter->stamp = model->priv->stamp;
	iter->user_data = ret;

	return TRUE;
}

static GtkTreePath *
rhythmdb_query_model_get_path (GtkTreeModel *tree_model,
			       GtkTreeIter  *iter)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);
	GtkTreePath *path;

	g_return_val_if_fail (iter->stamp == model->priv->stamp, NULL);

	if (g_sequence_ptr_is_end (iter->user_data))
		return NULL;

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, g_sequence_ptr_get_position (iter->user_data));
	return path;
}

static void
rhythmdb_query_model_get_value (GtkTreeModel *tree_model, GtkTreeIter *iter,
				gint column, GValue *value)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);
	RhythmDBEntry *entry;

	g_return_if_fail (!g_sequence_ptr_is_end (iter->user_data));
	g_return_if_fail (model->priv->stamp == iter->stamp);

	entry = g_sequence_ptr_get_data (iter->user_data);

	g_value_init (value, G_TYPE_POINTER);
	g_value_set_pointer (value, entry);
}

static gboolean
rhythmdb_query_model_iter_next (GtkTreeModel  *tree_model,
				GtkTreeIter   *iter)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);

	g_return_val_if_fail (iter->stamp == model->priv->stamp, FALSE);

	iter->user_data = g_sequence_ptr_next (iter->user_data);

	return !g_sequence_ptr_is_end (iter->user_data);
}

static gboolean
rhythmdb_query_model_iter_children (GtkTreeModel *tree_model,
				    GtkTreeIter  *iter,
				    GtkTreeIter  *parent)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);

	if (parent != NULL)
		return FALSE;

	if (g_sequence_get_length (model->priv->entries) == 0)
		return FALSE;

	iter->stamp = model->priv->stamp;
	iter->user_data = g_sequence_get_begin_ptr (model->priv->entries);

	return TRUE;
}

static gboolean
rhythmdb_query_model_iter_has_child (GtkTreeModel *tree_model,
				     GtkTreeIter  *iter)
{
	return FALSE;
}

static gint
rhythmdb_query_model_iter_n_children (GtkTreeModel *tree_model,
				      GtkTreeIter  *iter)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);

	if (iter == NULL)
		return g_sequence_get_length (model->priv->entries);

	g_return_val_if_fail (model->priv->stamp == iter->stamp, -1);

	return 0;
}

static gboolean
rhythmdb_query_model_iter_nth_child (GtkTreeModel *tree_model,
				     GtkTreeIter *iter, GtkTreeIter *parent,
				     gint n)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);
	GSequencePtr child;

	if (parent)
		return FALSE;

	child = g_sequence_get_ptr_at_pos (model->priv->entries, n);

	if (g_sequence_ptr_is_end (child))
		return FALSE;

	iter->stamp = model->priv->stamp;
	iter->user_data = child;

	return TRUE;
}

static gboolean
rhythmdb_query_model_iter_parent (GtkTreeModel *tree_model,
				  GtkTreeIter  *iter,
				  GtkTreeIter  *child)
{
	return FALSE;
}
