/* 
 *  arch-tag: Implementation of RhythmDB property GtkTreeModel
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

#include <libgnome/gnome-i18n.h>
#include "rhythmdb-property-model.h"
#include "rb-debug.h"
#include "gsequence.h"

static void rhythmdb_property_model_class_init (RhythmDBPropertyModelClass *klass);
static void rhythmdb_property_model_tree_model_init (GtkTreeModelIface *iface);
static void rhythmdb_property_model_init (RhythmDBPropertyModel *shell_player);
static void rhythmdb_property_model_finalize (GObject *object);
static void rhythmdb_property_model_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_property_model_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static GtkTreeModelFlags rhythmdb_property_model_get_flags (GtkTreeModel *model);
static gint rhythmdb_property_model_get_n_columns (GtkTreeModel *tree_model);
static GType rhythmdb_property_model_get_column_type (GtkTreeModel *tree_model, int index);
static gboolean rhythmdb_property_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
					       GtkTreePath  *path);
static GtkTreePath * rhythmdb_property_model_get_path (GtkTreeModel *tree_model,
						    GtkTreeIter  *iter);
static void rhythmdb_property_model_get_value (GtkTreeModel *tree_model, GtkTreeIter *iter,
					    gint column, GValue *value);
static gboolean rhythmdb_property_model_iter_next (GtkTreeModel  *tree_model,
						GtkTreeIter   *iter);
static gboolean rhythmdb_property_model_iter_children (GtkTreeModel *tree_model,
						    GtkTreeIter  *iter,
						    GtkTreeIter  *parent);
static gboolean rhythmdb_property_model_iter_has_child (GtkTreeModel *tree_model,
						     GtkTreeIter  *iter);
static gint rhythmdb_property_model_iter_n_children (GtkTreeModel *tree_model,
						  GtkTreeIter  *iter);
static gboolean rhythmdb_property_model_iter_nth_child (GtkTreeModel *tree_model,
						     GtkTreeIter *iter, GtkTreeIter *parent,
						     gint n);
static gboolean rhythmdb_property_model_iter_parent (GtkTreeModel *tree_model,
						  GtkTreeIter  *iter,
						  GtkTreeIter  *child);
static void rhythmdb_property_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
						 RhythmDBPropertyModel *model);
static void rhythmdb_property_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
						   RhythmDBPropertyModel *model);

typedef struct {
	char *name;
	char *sort_key;
} RhythmDBPropertyModelEntry;

struct RhythmDBPropertyModelPrivate
{
	RhythmDB *db;

	RhythmDBPropType propid;
	RhythmDBPropType sort_propid;

	guint stamp;

	GMutex *lock;

	GPtrArray *query;

	GAsyncQueue *add_queue;
	GHashTable *pending_propset;

	GSequence *properties;
	GMemChunk *property_memchunk;
	GHashTable *propset;

	RhythmDBPropertyModelEntry *all;
	
	gboolean dirty;
	gboolean complete;
};

enum
{
	PROP_0,
	PROP_RHYTHMDB,
	PROP_QUERY,
	PROP_PROP,
};

enum
{
	DIRTY,
	LAST_SIGNAL
};

static guint rhythmdb_property_model_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
rhythmdb_property_model_get_type (void)
{
	static GType rhythmdb_property_model_type = 0;

	if (rhythmdb_property_model_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RhythmDBPropertyModelClass),
			NULL,
			NULL,
			(GClassInitFunc) rhythmdb_property_model_class_init,
			NULL,
			NULL,
			sizeof (RhythmDBPropertyModel),
			0,
			(GInstanceInitFunc) rhythmdb_property_model_init
		};

		rhythmdb_property_model_type = g_type_register_static (G_TYPE_OBJECT,
								       "RhythmDBPropertyModel",
								       &our_info, 0);

		static const GInterfaceInfo tree_model_info =
		{
			(GInterfaceInitFunc) rhythmdb_property_model_tree_model_init,
			NULL,
			NULL
		};

		g_type_add_interface_static (rhythmdb_property_model_type,
					     GTK_TYPE_TREE_MODEL,
					     &tree_model_info);
	}

	return rhythmdb_property_model_type;
}

static void
rhythmdb_property_model_class_init (RhythmDBPropertyModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rhythmdb_property_model_set_property;
	object_class->get_property = rhythmdb_property_model_get_property;

	object_class->finalize = rhythmdb_property_model_finalize;

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
					 PROP_PROP,
					 g_param_spec_int ("prop",
							   "propid",
							   "Property id",
							   0, RHYTHMDB_NUM_PROPERTIES,
							   RHYTHMDB_PROP_TYPE,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));


	rhythmdb_property_model_signals[DIRTY] =
		g_signal_new ("dirty",
			      RHYTHMDB_TYPE_PROPERTY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBPropertyModelClass, dirty),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
rhythmdb_property_model_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags = rhythmdb_property_model_get_flags;
	iface->get_n_columns = rhythmdb_property_model_get_n_columns;
	iface->get_column_type = rhythmdb_property_model_get_column_type;
	iface->get_iter = rhythmdb_property_model_get_iter;
	iface->get_path = rhythmdb_property_model_get_path;
	iface->get_value = rhythmdb_property_model_get_value;
	iface->iter_next = rhythmdb_property_model_iter_next;
	iface->iter_children = rhythmdb_property_model_iter_children;
	iface->iter_has_child = rhythmdb_property_model_iter_has_child;
	iface->iter_n_children = rhythmdb_property_model_iter_n_children;
	iface->iter_nth_child = rhythmdb_property_model_iter_nth_child;
	iface->iter_parent = rhythmdb_property_model_iter_parent;
}

static void
rhythmdb_property_model_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (object);

	switch (prop_id)
	{
	case PROP_RHYTHMDB:
	{
		model->priv->db = g_value_get_object (value);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_added",
					 G_CALLBACK (rhythmdb_property_model_entry_added_cb),
					 model, 0);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_restored",
					 G_CALLBACK (rhythmdb_property_model_entry_added_cb),
					 model, 0);
		g_signal_connect_object (G_OBJECT (model->priv->db),
					 "entry_deleted",
					 G_CALLBACK (rhythmdb_property_model_entry_deleted_cb),
					 model, 0);

		break;
	}
	case PROP_QUERY:
		if (model->priv->query)
			rhythmdb_query_free (model->priv->query);
		model->priv->query = rhythmdb_query_copy (g_value_get_pointer (value));
		break;
	case PROP_PROP:
		model->priv->propid = g_value_get_int (value);
		switch (model->priv->propid)
		{
		case RHYTHMDB_PROP_GENRE:
			model->priv->sort_propid = RHYTHMDB_PROP_GENRE_SORT_KEY;
			break;
		case RHYTHMDB_PROP_ARTIST:
			model->priv->sort_propid = RHYTHMDB_PROP_ARTIST_SORT_KEY;
			break;
		case RHYTHMDB_PROP_ALBUM:
			model->priv->sort_propid = RHYTHMDB_PROP_ALBUM_SORT_KEY;
			break;
		case RHYTHMDB_PROP_TITLE:
			model->priv->sort_propid = RHYTHMDB_PROP_TITLE_SORT_KEY;
			break;
		default:
			g_assert_not_reached ();
			break;
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rhythmdb_property_model_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (object);

	switch (prop_id)
	{
	case PROP_RHYTHMDB:
		g_value_set_object (value, model->priv->db);
		break;
	case PROP_QUERY:
		g_value_set_pointer (value, model->priv->query);
		break;
	case PROP_PROP:
		g_value_set_int (value, model->priv->propid);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_property_model_init (RhythmDBPropertyModel *model)
{
	model->priv = g_new0 (RhythmDBPropertyModelPrivate, 1);

	model->priv->property_memchunk = g_mem_chunk_new ("RhythmDBPropertyModel property memchunk",
							  sizeof (RhythmDBPropertyModelEntry),
							  1024, G_ALLOC_AND_FREE);

	model->priv->stamp = g_random_int ();

	model->priv->properties = g_sequence_new (NULL);
	model->priv->propset = g_hash_table_new (g_str_hash, g_str_equal);
	model->priv->pending_propset = g_hash_table_new (g_str_hash, g_str_equal);

	model->priv->lock = g_mutex_new ();

	model->priv->add_queue = g_async_queue_new ();

	model->priv->all = g_mem_chunk_alloc (model->priv->property_memchunk);
	model->priv->all->name = g_strdup (_("All"));
	model->priv->all->sort_key = NULL;
}

static void
rhythmdb_property_model_finalize (GObject *object)
{
	RhythmDBPropertyModel *model;
	GSequencePtr ptr;
	GSequencePtr end_ptr;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_PROPERTY_MODEL (object));

	model = RHYTHMDB_PROPERTY_MODEL (object);

	rb_debug ("finalizing property model %p", model);

	g_return_if_fail (model->priv != NULL);

	end_ptr = g_sequence_get_end_ptr (model->priv->properties);
	for (ptr = g_sequence_get_begin_ptr (model->priv->properties); ptr != end_ptr;
	     ptr = g_sequence_ptr_next (ptr)) {
		RhythmDBPropertyModelEntry *prop = g_sequence_ptr_get_data (ptr);
		g_free (prop->name);
		g_free (prop->sort_key);
	}

	g_async_queue_unref (model->priv->add_queue);

	g_mem_chunk_destroy (model->priv->property_memchunk);
	g_hash_table_destroy (model->priv->pending_propset);
	g_hash_table_destroy (model->priv->propset);
	g_sequence_free (model->priv->properties);

	g_free (model->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RhythmDBPropertyModel *
rhythmdb_property_model_new (RhythmDB *db, RhythmDBPropType propid, GPtrArray *query)
{
	RhythmDBPropertyModel *model = g_object_new (RHYTHMDB_TYPE_PROPERTY_MODEL,
						     "db", db, "prop", propid, "query",
						     query, NULL);

	g_return_val_if_fail (model->priv != NULL, NULL);

	return model;
}

RhythmDBPropertyModel *
rhythmdb_property_model_new_empty (RhythmDB *db, RhythmDBPropType propid)
{
	return g_object_new (RHYTHMDB_TYPE_PROPERTY_MODEL, "db", db, "prop", propid, NULL);
}

static gint
rhythmdb_property_model_compare (RhythmDBPropertyModelEntry *a, RhythmDBPropertyModelEntry *b, RhythmDBPropertyModel *model)
{
	return strcmp (a->sort_key, b->sort_key);
}

void
rhythmdb_property_model_append (RhythmDBPropertyModel *model, const char *title, const char *sort_key)
{
	RhythmDBPropertyModelEntry *prop;

	prop = g_mem_chunk_alloc (model->priv->property_memchunk);
	prop->name = g_strdup (title);
	prop->sort_key = g_strdup (sort_key);

	g_hash_table_insert (model->priv->pending_propset, prop->name, prop->name);
	g_async_queue_push (model->priv->add_queue, prop);
}

void
rhythmdb_property_model_append_entry (RhythmDBPropertyModel *model, RhythmDBEntry *entry)
{

	const char *title;
	const char *sort_key;
		
	rhythmdb_read_lock (model->priv->db); 
	title = rhythmdb_entry_get_string (model->priv->db, entry, model->priv->propid);
	sort_key = rhythmdb_entry_get_string (model->priv->db, entry, model->priv->sort_propid);
	rhythmdb_read_unlock (model->priv->db); 

	g_mutex_lock (model->priv->lock);
	
	if (g_hash_table_lookup (model->priv->propset, title))
		goto out;
	if (g_hash_table_lookup (model->priv->pending_propset, title))
		goto out;

	rhythmdb_property_model_append (model, title, sort_key);
out:
	g_mutex_unlock (model->priv->lock);
}

void
rhythmdb_property_model_complete (RhythmDBPropertyModel *model)
{
	g_mutex_lock (model->priv->lock);
	
	model->priv->complete = TRUE;
	
	g_mutex_unlock (model->priv->lock);
}

/* Threading: should be entered via database thread, with db lock held
 */
static void
rhythmdb_property_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
					RhythmDBPropertyModel *model)
{

	g_mutex_lock (model->priv->lock);

	if (G_LIKELY (model->priv->query)) {
		const char *name = rhythmdb_entry_get_string (db, entry, model->priv->propid);
		if (g_hash_table_lookup (model->priv->propset, name))
			goto out;
		if (g_hash_table_lookup (model->priv->pending_propset, name))
			goto out;

		if (rhythmdb_evaluate_query (db, model->priv->query, entry)) {
			const char *sort_key = rhythmdb_entry_get_string (db, entry, model->priv->sort_propid);
			rhythmdb_property_model_append (model, name, sort_key);
		}
	}

out:
	g_mutex_unlock (model->priv->lock);
}

static void
rhythmdb_property_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
				       RhythmDBPropertyModel *model)
{
	g_mutex_lock (model->priv->lock);
	
	if (model->priv->dirty)
		goto out;

	if (G_LIKELY (model->priv->query)) {
		if (rhythmdb_evaluate_query (db, model->priv->query, entry)) {
			rb_debug ("model is now dirty");
			model->priv->dirty = TRUE;
		}
	}
out:
	g_mutex_unlock (model->priv->lock);
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
gboolean
rhythmdb_property_model_sync (RhythmDBPropertyModel *model, GTimeVal *timeout)
{
	RhythmDBPropertyModelEntry *prop;	
	gboolean synced = FALSE;
			      
	g_mutex_lock (model->priv->lock);

	if (model->priv->dirty) {
		model->priv->dirty = FALSE;
		g_mutex_unlock (model->priv->lock);
		rb_debug ("emitting dirty");
		g_signal_emit (G_OBJECT (model), rhythmdb_property_model_signals[DIRTY], 0);
		return TRUE;
	}

	while ((prop = g_async_queue_try_pop (model->priv->add_queue)) != NULL) {
		GtkTreeIter iter;
		GtkTreePath *path;
		GSequencePtr ptr;
		GTimeVal now;

		synced = TRUE;

		iter.stamp = model->priv->stamp;

		ptr = g_sequence_insert_sorted (model->priv->properties, prop,
						(GCompareDataFunc) rhythmdb_property_model_compare,
						model);
		g_hash_table_insert (model->priv->propset, prop->name, ptr);
		g_hash_table_remove (model->priv->pending_propset, prop->name);
		
		iter.user_data = ptr;
		path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
		rb_debug ("emitting row inserted");
		gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
		gtk_tree_path_free (path);

		g_get_current_time (&now);
		if (compare_times (timeout,&now) > 0) {
			rb_debug ("hit timeout");
			break;
		}
	}

	g_mutex_unlock (model->priv->lock);
	return synced;
}

static GtkTreeModelFlags
rhythmdb_property_model_get_flags (GtkTreeModel *model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint
rhythmdb_property_model_get_n_columns (GtkTreeModel *tree_model)
{
	return RHYTHMDB_PROPERTY_MODEL_COLUMN_LAST;
}

static GType
rhythmdb_property_model_get_column_type (GtkTreeModel *tree_model, int index)
{
	switch (index)
	{
	case 0:
		return G_TYPE_STRING;
	case 1:
		return G_TYPE_BOOLEAN;
	default:
		g_assert_not_reached ();
		return G_TYPE_INVALID;
	}
}

static gboolean
rhythmdb_property_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
			       GtkTreePath *path)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	guint index;
	GSequencePtr ptr;

	index = gtk_tree_path_get_indices (path)[0];

	if (index == 0) {
		iter->stamp = model->priv->stamp;
		iter->user_data = model->priv->all;
		return TRUE;
	}

	index--;
	if (index >= g_sequence_get_length (model->priv->properties))
		return FALSE;

	ptr = g_sequence_get_ptr_at_pos (model->priv->properties, index);

	iter->stamp = model->priv->stamp;
	iter->user_data = ptr;

	return TRUE;
}

static GtkTreePath *
rhythmdb_property_model_get_path (GtkTreeModel *tree_model,
			       GtkTreeIter  *iter)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	GtkTreePath *path;

	g_return_val_if_fail (iter->stamp == model->priv->stamp, NULL);

	if (g_sequence_ptr_is_end (iter->user_data))
		return NULL;

	path = gtk_tree_path_new ();
	if (iter->user_data == model->priv->all)
		gtk_tree_path_append_index (path, 0);
	else
		gtk_tree_path_append_index (path, g_sequence_ptr_get_position (iter->user_data)+1);
	return path;
}

static void
rhythmdb_property_model_get_value (GtkTreeModel *tree_model, GtkTreeIter *iter,
				   gint column, GValue *value)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	RhythmDBPropertyModelEntry *prop;

	g_return_if_fail (!g_sequence_ptr_is_end (iter->user_data));
	g_return_if_fail (model->priv->stamp == iter->stamp);

	if (iter->user_data == model->priv->all) {
		switch (column)
		{
		case 0:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, model->priv->all->name);
			break;
		case 1:
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, TRUE);
			break;
		default:
			g_assert_not_reached ();
		}
	} else {
		prop = g_sequence_ptr_get_data (iter->user_data);
		
		switch (column)
		{
		case 0:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, prop->name);
			break;
		case 1:
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, prop == model->priv->all);
			break;
		default:
			g_assert_not_reached ();
		}
	}
}

static gboolean
rhythmdb_property_model_iter_next (GtkTreeModel  *tree_model,
				GtkTreeIter   *iter)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);

	g_return_val_if_fail (iter->stamp == model->priv->stamp, FALSE);

	if (iter->user_data == model->priv->all)
		iter->user_data = g_sequence_get_begin_ptr (model->priv->properties);
	else
		iter->user_data = g_sequence_ptr_next (iter->user_data);

	return !g_sequence_ptr_is_end (iter->user_data);
}

static gboolean
rhythmdb_property_model_iter_children (GtkTreeModel *tree_model,
				       GtkTreeIter  *iter,
				       GtkTreeIter  *parent)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);

	if (parent != NULL)
		return FALSE;

	iter->stamp = model->priv->stamp;
	iter->user_data = model->priv->all;

	return TRUE;
}

static gboolean
rhythmdb_property_model_iter_has_child (GtkTreeModel *tree_model,
					GtkTreeIter *iter)
{
	return FALSE;
}

static gint
rhythmdb_property_model_iter_n_children (GtkTreeModel *tree_model,
					 GtkTreeIter *iter)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);

	if (iter)
		g_return_val_if_fail (model->priv->stamp == iter->stamp, -1);

	if (iter == NULL)
		return 1 + g_sequence_get_length (model->priv->properties);

	return 0;
}

static gboolean
rhythmdb_property_model_iter_nth_child (GtkTreeModel *tree_model,
				     GtkTreeIter *iter, GtkTreeIter *parent,
				     gint n)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	GSequencePtr child;

	if (parent)
		return FALSE;

	if (n != 0) {
		child = g_sequence_get_ptr_at_pos (model->priv->properties, n);
		
		if (g_sequence_ptr_is_end (child))
			return FALSE;
		iter->user_data = child;
	} else {
		iter->user_data = model->priv->all;
	}

	iter->stamp = model->priv->stamp;

	return TRUE;
}

static gboolean
rhythmdb_property_model_iter_parent (GtkTreeModel *tree_model,
				     GtkTreeIter  *iter,
				     GtkTreeIter  *child)
{
	return FALSE;
}
