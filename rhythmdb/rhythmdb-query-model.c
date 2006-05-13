/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of RhythmDB query result GtkTreeModel
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <gtk/gtktreednd.h>

#include "rhythmdb-query-model.h"
#include "rb-debug.h"
#include "gsequence.h"
#include "rb-tree-dnd.h"
#include "rb-marshal.h"
#include "rb-util.h"

struct ReverseSortData
{
	GCompareDataFunc	func;
	gpointer		data;
};

static void rhythmdb_query_model_query_results_init (RhythmDBQueryResultsIface *iface);
static void rhythmdb_query_model_tree_model_init (GtkTreeModelIface *iface);
static void rhythmdb_query_model_drag_source_init (RbTreeDragSourceIface *iface);
static void rhythmdb_query_model_drag_dest_init (RbTreeDragDestIface *iface);

G_DEFINE_TYPE_WITH_CODE(RhythmDBQueryModel, rhythmdb_query_model, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RHYTHMDB_TYPE_QUERY_RESULTS,
					      rhythmdb_query_model_query_results_init)
			G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL,
					      rhythmdb_query_model_tree_model_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_TREE_DRAG_SOURCE,
					      rhythmdb_query_model_drag_source_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_TREE_DRAG_DEST,
					      rhythmdb_query_model_drag_dest_init))

static void rhythmdb_query_model_init (RhythmDBQueryModel *shell_player);
static GObject *rhythmdb_query_model_constructor (GType type, guint n_construct_properties,
						  GObjectConstructParam *construct_properties);
static void rhythmdb_query_model_dispose (GObject *object);
static void rhythmdb_query_model_finalize (GObject *object);
static void rhythmdb_query_model_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_query_model_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_query_model_do_insert (RhythmDBQueryModel *model,
					    RhythmDBEntry *entry,
					    gint index);
static void rhythmdb_query_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
						 RhythmDBQueryModel *model);
static void rhythmdb_query_model_entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
						   GSList *changes, RhythmDBQueryModel *model);
static void rhythmdb_query_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
						   RhythmDBQueryModel *model);

static void rhythmdb_query_model_filter_out_entry (RhythmDBQueryModel *model,
						   RhythmDBEntry *entry);
static gboolean rhythmdb_query_model_do_reorder (RhythmDBQueryModel *model, RhythmDBEntry *entry);
static gboolean rhythmdb_query_model_emit_reorder (RhythmDBQueryModel *model, gint old_pos, gint new_pos);
static gboolean rhythmdb_query_model_drag_data_get (RbTreeDragSource *dragsource,
							  GList *paths,
							  GtkSelectionData *selection_data);
static gboolean rhythmdb_query_model_drag_data_delete (RbTreeDragSource *dragsource,
							     GList *paths);
static gboolean rhythmdb_query_model_row_draggable (RbTreeDragSource *dragsource,
							  GList *paths);
static gboolean rhythmdb_query_model_drag_data_received (RbTreeDragDest *drag_dest,
							 GtkTreePath *dest,
							 GtkTreeViewDropPosition pos,
							 GtkSelectionData  *selection_data);
static gboolean rhythmdb_query_model_row_drop_possible (RbTreeDragDest *drag_dest,
							GtkTreePath *dest,
							GtkTreeViewDropPosition pos,
							GtkSelectionData  *selection_data);
static gboolean rhythmdb_query_model_row_drop_position (RbTreeDragDest   *drag_dest,
							GtkTreePath       *dest_path,
							GList *targets,
							GtkTreeViewDropPosition *pos);

static void rhythmdb_query_model_set_query (RhythmDBQueryResults *results, GPtrArray *query);
static void rhythmdb_query_model_add_results (RhythmDBQueryResults *results, GPtrArray *entries);
static void rhythmdb_query_model_query_complete (RhythmDBQueryResults *results);

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


static void rhythmdb_query_model_base_row_inserted (GtkTreeModel *base_model,
						    GtkTreePath *path,
						    GtkTreeIter *iter,
						    RhythmDBQueryModel *model);
static void rhythmdb_query_model_base_row_deleted (GtkTreeModel *base_model,
						   GtkTreePath *path,
						   RhythmDBQueryModel *model);
static void rhythmdb_query_model_base_non_entry_dropped (GtkTreeModel *base_model,
							 const char *location,
							 int position,
							 RhythmDBQueryModel *model);
static void rhythmdb_query_model_base_complete (GtkTreeModel *base_model,
						RhythmDBQueryModel *model);
static void rhythmdb_query_model_base_rows_reordered (GtkTreeModel *base_model,
						      GtkTreePath *arg1,
						      GtkTreeIter *arg2,
						      gint *order_map,
						      RhythmDBQueryModel *model);
static void rhythmdb_query_model_base_entry_removed (RhythmDBQueryModel *base_model,
						     RhythmDBEntry *entry,
						     RhythmDBQueryModel *model);
static int rhythmdb_query_model_child_index_to_base_index (RhythmDBQueryModel *model, int index);

static gint _reverse_sorting_func (gpointer a, gpointer b, struct ReverseSortData *model);
static gboolean rhythmdb_query_model_within_limit (RhythmDBQueryModel *model,
						   RhythmDBEntry *entry);

struct RhythmDBQueryModelUpdate
{
	RhythmDBQueryModel *model;
	enum {
		RHYTHMDB_QUERY_MODEL_UPDATE_ROWS_INSERTED,
		RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED_INDEX,
		RHYTHMDB_QUERY_MODEL_UPDATE_QUERY_COMPLETE,
	} type;

	union {
		struct {
			RhythmDBEntry *entry;
			gint index;
		} data;
		GPtrArray *entries;
	} entrydata;
};

static void rhythmdb_query_model_process_update (struct RhythmDBQueryModelUpdate *update);

static gboolean idle_process_update (struct RhythmDBQueryModelUpdate *update);

static const GtkTargetEntry rhythmdb_query_model_drag_types[] = { { "text/uri-list", 0, 0 },};

static GtkTargetList *rhythmdb_query_model_drag_target_list = NULL;


struct RhythmDBQueryModelPrivate
{
	RhythmDB *db;

	RhythmDBQueryModel *base_model;

	GCompareDataFunc sort_func;
	gpointer sort_data;
	GDestroyNotify sort_data_destroy;
	gboolean sort_reverse;

	GPtrArray *query, *original_query;

	guint stamp;

	RhythmDBQueryModelLimitType limit_type;
	GValueArray *limit_value;

	glong total_duration;
	guint64 total_size;

	GSequence *entries;
	GHashTable *reverse_map;
	GSequence *limited_entries;
	GHashTable *limited_reverse_map;

	gint pending_update_count;

	gboolean reorder_drag_and_drop;
	gboolean show_hidden;
};

#define RHYTHMDB_QUERY_MODEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RHYTHMDB_TYPE_QUERY_MODEL, RhythmDBQueryModelPrivate))

enum
{
	PROP_0,
	PROP_RHYTHMDB,
	PROP_QUERY,
	PROP_SORT_FUNC,
	PROP_SORT_DATA,
	PROP_SORT_DATA_DESTROY,
	PROP_SORT_REVERSE,
	PROP_LIMIT_TYPE,
	PROP_LIMIT_VALUE,
	PROP_SHOW_HIDDEN,
	PROP_BASE_MODEL,
};

enum
{
	COMPLETE,
	ENTRY_PROP_CHANGED,
	ENTRY_REMOVED,
	NON_ENTRY_DROPPED,
	LAST_SIGNAL
};

static guint rhythmdb_query_model_signals[LAST_SIGNAL] = { 0 };

static void
rhythmdb_query_model_class_init (RhythmDBQueryModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	if (!rhythmdb_query_model_drag_target_list)
		rhythmdb_query_model_drag_target_list
			= gtk_target_list_new (rhythmdb_query_model_drag_types,
					       G_N_ELEMENTS (rhythmdb_query_model_drag_types));

	object_class->set_property = rhythmdb_query_model_set_property;
	object_class->get_property = rhythmdb_query_model_get_property;

	object_class->dispose = rhythmdb_query_model_dispose;
	object_class->finalize = rhythmdb_query_model_finalize;
	object_class->constructor = rhythmdb_query_model_constructor;

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
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORT_FUNC,
					 g_param_spec_pointer ("sort-func",
							      "SortFunc",
							      "Sort function",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORT_DATA,
					 g_param_spec_pointer ("sort-data",
							       "Sort data",
							       "Sort data",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORT_DATA_DESTROY,
					 g_param_spec_pointer ("sort-data-destroy",
							       "Sort data destroy",
							       "Sort data destroy function",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SORT_REVERSE,
					 g_param_spec_boolean ("sort-reverse",
							      "sort-reverse",
							      "Reverse sort order flag",
							      FALSE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_LIMIT_TYPE,
					 g_param_spec_enum ("limit-type",
							    "limit-type",
							    "type of limit",
							    RHYTHMDB_TYPE_QUERY_MODEL_LIMIT_TYPE,
							    RHYTHMDB_QUERY_MODEL_LIMIT_NONE,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_LIMIT_VALUE,
					 g_param_spec_boxed ("limit-value",
							     "limit-value",
							     "value of limit",
							     G_TYPE_VALUE_ARRAY,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SHOW_HIDDEN,
					 g_param_spec_boolean ("show-hidden",
							       "show hidden",
							       "maximum time (seconds)",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_BASE_MODEL,
					 g_param_spec_object ("base-model",
						 	      "base-model",
							      "base RhythmDBQueryModel",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
							      

	rhythmdb_query_model_signals[ENTRY_PROP_CHANGED] =
		g_signal_new ("entry-prop-changed",
			      RHYTHMDB_TYPE_QUERY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryModelClass, entry_prop_changed),
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_INT_POINTER_POINTER,
			      G_TYPE_NONE,
			      4, RHYTHMDB_TYPE_ENTRY, G_TYPE_INT, G_TYPE_POINTER, G_TYPE_POINTER);
	rhythmdb_query_model_signals[ENTRY_REMOVED] =
		g_signal_new ("entry-removed",
			      RHYTHMDB_TYPE_QUERY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryModelClass, entry_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, RHYTHMDB_TYPE_ENTRY);
	rhythmdb_query_model_signals[NON_ENTRY_DROPPED] =
		g_signal_new ("non-entry-dropped",
			      RHYTHMDB_TYPE_QUERY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryModelClass, non_entry_dropped),
			      NULL, NULL,
			      rb_marshal_VOID__POINTER_INT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);
	rhythmdb_query_model_signals[COMPLETE] =
		g_signal_new ("complete",
			      RHYTHMDB_TYPE_QUERY_MODEL,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBQueryModelClass, complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (RhythmDBQueryModelPrivate));
}

static void
rhythmdb_query_model_query_results_init (RhythmDBQueryResultsIface *iface)
{
	iface->set_query = rhythmdb_query_model_set_query;
	iface->add_results = rhythmdb_query_model_add_results;
	iface->query_complete = rhythmdb_query_model_query_complete;
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
rhythmdb_query_model_drag_source_init (RbTreeDragSourceIface *iface)
{
	iface->row_draggable = rhythmdb_query_model_row_draggable;
	iface->drag_data_delete = rhythmdb_query_model_drag_data_delete;
	iface->drag_data_get = rhythmdb_query_model_drag_data_get;
}

static void
rhythmdb_query_model_drag_dest_init (RbTreeDragDestIface *iface)
{
	iface->drag_data_received = rhythmdb_query_model_drag_data_received;
	iface->row_drop_possible = rhythmdb_query_model_row_drop_possible;
	iface->row_drop_position = rhythmdb_query_model_row_drop_position;
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
		model->priv->db = g_value_get_object (value);
		break;
	case PROP_QUERY:
		model->priv->query = rhythmdb_query_copy (g_value_get_pointer (value));
		model->priv->original_query = rhythmdb_query_copy (model->priv->query);
		rhythmdb_query_preprocess (model->priv->db, model->priv->query);
		break;
	case PROP_SORT_FUNC:
		model->priv->sort_func = g_value_get_pointer (value);
		break;
	case PROP_SORT_DATA:
		if (model->priv->sort_data_destroy && model->priv->sort_data)
			model->priv->sort_data_destroy (model->priv->sort_data);
		model->priv->sort_data = g_value_get_pointer (value);
		break;
	case PROP_SORT_DATA_DESTROY:
		model->priv->sort_data_destroy = g_value_get_pointer (value);
		break;
	case PROP_SORT_REVERSE:
		model->priv->sort_reverse  = g_value_get_boolean (value);
		break;
	case PROP_LIMIT_TYPE:
		model->priv->limit_type = g_value_get_enum (value);
		break;
	case PROP_LIMIT_VALUE:
		if (model->priv->limit_value)
			g_value_array_free (model->priv->limit_value);
		model->priv->limit_value = (GValueArray*)g_value_dup_boxed (value);
		break;
	case PROP_SHOW_HIDDEN:
		model->priv->show_hidden = g_value_get_boolean (value);
		/* FIXME: this will have funky issues if this is changed after entries are added */
		break;
	case PROP_BASE_MODEL:
		if (model->priv->base_model) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_row_inserted),
							      model);
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_row_deleted),
							      model);
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_non_entry_dropped),
							      model);
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_complete),
							      model);
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_rows_reordered),
							      model);
			g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
							      G_CALLBACK (rhythmdb_query_model_base_entry_removed),
							      model);
			g_object_unref (model->priv->base_model);
		}

		model->priv->base_model = g_value_get_object (value);

		if (model->priv->base_model) {
			g_object_ref (model->priv->base_model);
			g_assert (model->priv->base_model->priv->db == model->priv->db);

			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "row-inserted",
						 G_CALLBACK (rhythmdb_query_model_base_row_inserted),
						 model, 0);
			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "row-deleted",
						 G_CALLBACK (rhythmdb_query_model_base_row_deleted),
						 model, 0);
			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "non-entry-dropped",
						 G_CALLBACK (rhythmdb_query_model_base_non_entry_dropped),
						 model, 0);
			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "complete",
						 G_CALLBACK (rhythmdb_query_model_base_complete),
						 model, 0);
			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "rows-reordered",
						 G_CALLBACK (rhythmdb_query_model_base_rows_reordered),
						 model, 0);
			g_signal_connect_object (G_OBJECT (model->priv->base_model),
						 "entry-removed",
						 G_CALLBACK (rhythmdb_query_model_base_entry_removed),
						 model, 0);

			if (model->priv->base_model->priv->entries) {
				GSequencePtr ptr, next;
				RhythmDBEntry *entry;

				ptr = g_sequence_get_begin_ptr (model->priv->base_model->priv->entries);
				while (!g_sequence_ptr_is_end (ptr)) {
					next = g_sequence_ptr_next (ptr);
					entry = (RhythmDBEntry *)g_sequence_ptr_get_data (ptr);
					if (model->priv->query == NULL ||
					    rhythmdb_evaluate_query (model->priv->db, model->priv->query, entry)) {
						if (model->priv->show_hidden || (rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN) == FALSE))
							rhythmdb_query_model_do_insert (model, entry, -1);
					}

					ptr = next;
				}
			}
		}
		
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
		g_value_set_pointer (value, model->priv->original_query);
		break;
	case PROP_SORT_FUNC:
		g_value_set_pointer (value, model->priv->sort_func);
		break;
	case PROP_SORT_DATA:
		g_value_set_pointer (value, model->priv->sort_data);
		break;
	case PROP_SORT_DATA_DESTROY:
		g_value_set_pointer (value, model->priv->sort_data_destroy);
		break;
	case PROP_SORT_REVERSE:
		g_value_set_boolean (value, model->priv->sort_reverse);
		break;
	case PROP_LIMIT_TYPE:
		g_value_set_enum (value, model->priv->limit_type);
		break;
	case PROP_LIMIT_VALUE:
		g_value_set_boxed (value, model->priv->limit_value);
		break;
	case PROP_SHOW_HIDDEN:
		g_value_set_boolean (value, model->priv->show_hidden);
		break;
	case PROP_BASE_MODEL:
		g_value_set_object (value, model->priv->base_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_query_model_init (RhythmDBQueryModel *model)
{
	model->priv = RHYTHMDB_QUERY_MODEL_GET_PRIVATE (model);

	model->priv->stamp = g_random_int ();

	model->priv->entries = g_sequence_new (NULL);
	model->priv->reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	model->priv->limited_entries = g_sequence_new (NULL);
	model->priv->limited_reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);

	model->priv->reorder_drag_and_drop = FALSE;
}

static GObject *
rhythmdb_query_model_constructor (GType type, guint n_construct_properties,
				  GObjectConstructParam *construct_properties)
{
	RhythmDBQueryModel *model;

	model = RHYTHMDB_QUERY_MODEL (G_OBJECT_CLASS (rhythmdb_query_model_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_signal_connect_object (G_OBJECT (model->priv->db),
				 "entry_added",
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

	return G_OBJECT (model);
}

static void
_unref_entry (RhythmDBEntry *entry, gpointer stuff, RhythmDB *db)
{
	rhythmdb_entry_unref (db, entry);
}

static void
rhythmdb_query_model_dispose (GObject *object)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_QUERY_MODEL (object));

	model = RHYTHMDB_QUERY_MODEL (object);

	g_return_if_fail (model->priv != NULL);

	rb_debug ("disposing query model %p", object);
		
	if (model->priv->base_model) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_row_inserted),
						      model);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_row_deleted),
						      model);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_non_entry_dropped),
						      model);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_complete),
						      model);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_rows_reordered),
						      model);
		g_signal_handlers_disconnect_by_func (G_OBJECT (model->priv->base_model),
						      G_CALLBACK (rhythmdb_query_model_base_entry_removed),
						      model);
		g_object_unref (G_OBJECT (model->priv->base_model));
		model->priv->base_model = NULL;
	}
	
	G_OBJECT_CLASS (rhythmdb_query_model_parent_class)->dispose (object);
}

static void
rhythmdb_query_model_finalize (GObject *object)
{
	RhythmDBQueryModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_QUERY_MODEL (object));

	model = RHYTHMDB_QUERY_MODEL (object);

	g_return_if_fail (model->priv != NULL);

	rb_debug ("finalizing query model %p", object);

	g_hash_table_foreach (model->priv->reverse_map, (GHFunc) _unref_entry, model->priv->db);
	g_hash_table_destroy (model->priv->reverse_map);
	g_sequence_free (model->priv->entries);
	g_hash_table_foreach (model->priv->limited_reverse_map, (GHFunc) _unref_entry, model->priv->db);
	g_hash_table_destroy (model->priv->limited_reverse_map);
	g_sequence_free (model->priv->limited_entries);

	if (model->priv->query)
		rhythmdb_query_free (model->priv->query);
	if (model->priv->original_query)
		rhythmdb_query_free (model->priv->original_query);

	if (model->priv->sort_data_destroy && model->priv->sort_data)
		model->priv->sort_data_destroy (model->priv->sort_data);

	if (model->priv->limit_value)
		g_value_array_free (model->priv->limit_value);

	G_OBJECT_CLASS (rhythmdb_query_model_parent_class)->finalize (object);
}

RhythmDBQueryModel *
rhythmdb_query_model_new (RhythmDB *db, GPtrArray *query,
			  GCompareDataFunc sort_func,
			  gpointer sort_data,
			  GDestroyNotify sort_data_destroy,
			  gboolean sort_reverse)
{
	RhythmDBQueryModel *model = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
						  "db", db, "query", query,
						  "sort-func", sort_func,
						  "sort-data", sort_data,
						  "sort-data-destroy", sort_data_destroy,
						  "sort-reverse", sort_reverse,
						  NULL);

	g_return_val_if_fail (model->priv != NULL, NULL);

	return model;
}

RhythmDBQueryModel *
rhythmdb_query_model_new_empty (RhythmDB *db)
{
	return g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
			     "db", db, NULL);
}

gboolean
rhythmdb_query_model_has_pending_changes (RhythmDBQueryModel *model)
{
	gboolean result;
       
	result = g_atomic_int_get (&model->priv->pending_update_count) > 0;
	if (model->priv->base_model)
		result |= rhythmdb_query_model_has_pending_changes (model->priv->base_model);

	return result;
}

static void
rhythmdb_query_model_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry,
				     RhythmDBQueryModel *model)
{
	if (model->priv->query) {
		if (!model->priv->show_hidden && rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN))
			return;

		/* check if it's in the base model */
		if (model->priv->base_model)
		       if (g_hash_table_lookup (model->priv->base_model->priv->reverse_map, entry) == NULL)
			       return;

		if (rhythmdb_evaluate_query (db, model->priv->query, entry)) {
			rhythmdb_query_model_do_insert (model, entry, -1);
		}
	}
}

static void
rhythmdb_query_model_entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
				       GSList *changes, RhythmDBQueryModel *model)
{
	gboolean hidden = FALSE;
	GSList *t;

	hidden = (!model->priv->show_hidden && rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN));

	if (g_hash_table_lookup (model->priv->reverse_map, entry) == NULL) {
		if (hidden == FALSE) {
			/* the changed entry may now satisfy the query 
			 * so we test it */
			rhythmdb_query_model_entry_added_cb (db, entry, model);
		}
		return;
	}

	if (hidden) {
		/* emit this, so things know why it was removed.
		 * this is needed to update propery models correctly */
		GValue true_val = { 0, };
		GValue false_val = { 0, };
		
		g_value_init (&true_val, G_TYPE_BOOLEAN);
		g_value_set_boolean (&true_val, TRUE);
		g_value_init (&false_val, G_TYPE_BOOLEAN);
		g_value_set_boolean (&false_val, FALSE);
		
		rb_debug ("emitting hidden-removal notification for %s",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		g_signal_emit (G_OBJECT (model),
			       rhythmdb_query_model_signals[ENTRY_PROP_CHANGED], 0,
			       entry, RHYTHMDB_PROP_HIDDEN, &false_val, &true_val);
		g_value_unset (&true_val);
		g_value_unset (&false_val);
		
		rhythmdb_query_model_remove_entry (model, entry);
		return;
	}

	/* emit separate change signals for each property */
	for (t = changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;
		g_signal_emit (G_OBJECT (model),
			       rhythmdb_query_model_signals[ENTRY_PROP_CHANGED], 0,
			       entry, change->prop, &change->old, &change->new);

		if (change->prop == RHYTHMDB_PROP_DURATION) {
			model->priv->total_duration -= g_value_get_ulong (&change->old);
			model->priv->total_duration += g_value_get_ulong (&change->new);
		} else if (change->prop == RHYTHMDB_PROP_FILE_SIZE) {
			model->priv->total_size -= g_value_get_uint64 (&change->old);
			model->priv->total_size += g_value_get_uint64 (&change->new);
		}
	}

	if (model->priv->query &&
	    !rhythmdb_evaluate_query (db, model->priv->query, entry)) {
		rhythmdb_query_model_filter_out_entry (model, entry);
		return;
	}

	/* it may have moved, so we can't just emit a changed entry */
	if (!rhythmdb_query_model_do_reorder (model, entry)) {
		/* but if it didn't, we can */
		GtkTreeIter iter;
		GtkTreePath *path;

		if (rhythmdb_query_model_entry_to_iter (model, entry, &iter)) {
			path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
							      &iter);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (model),
						     path, &iter);
			gtk_tree_path_free (path);
		}
	}
}

static void
rhythmdb_query_model_entry_deleted_cb (RhythmDB *db, RhythmDBEntry *entry,
				       RhythmDBQueryModel *model)
{
	
	if (g_hash_table_lookup (model->priv->reverse_map, entry) ||
	    g_hash_table_lookup (model->priv->limited_reverse_map, entry))
		rhythmdb_query_model_remove_entry (model, entry);
}

static void 
rhythmdb_query_model_process_update (struct RhythmDBQueryModelUpdate *update)
{
	g_atomic_int_inc (&update->model->priv->pending_update_count);
	if (rb_is_main_thread ())
		idle_process_update (update);
	else
		g_idle_add ((GSourceFunc) idle_process_update, update);
}

gboolean
idle_process_update (struct RhythmDBQueryModelUpdate *update)
{
	switch (update->type) {
	case RHYTHMDB_QUERY_MODEL_UPDATE_ROWS_INSERTED:
	{
		guint i;
		rb_debug ("inserting %d rows", update->entrydata.entries->len);
		for (i = 0; i < update->entrydata.entries->len; i++ ) {
			RhythmDBEntry *entry = g_ptr_array_index (update->entrydata.entries, i);

			if (update->model->priv->show_hidden || !rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN)) {
				RhythmDBQueryModel *base_model = update->model->priv->base_model;
				if (base_model &&
				    g_hash_table_lookup (base_model->priv->reverse_map, entry) == NULL)
					       continue;

				rhythmdb_query_model_do_insert (update->model, entry, -1);
			}
			
			rhythmdb_entry_unref (update->model->priv->db, entry);
		}
		g_ptr_array_free (update->entrydata.entries, TRUE);
		break;
	}
	case RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED_INDEX:
	{
		rb_debug ("inserting row at index %d", update->entrydata.data.index);
		rhythmdb_query_model_do_insert (update->model, update->entrydata.data.entry, update->entrydata.data.index);
		rhythmdb_entry_unref (update->model->priv->db, update->entrydata.data.entry);
		break;
	}
	case RHYTHMDB_QUERY_MODEL_UPDATE_QUERY_COMPLETE:
		g_signal_emit (G_OBJECT (update->model), rhythmdb_query_model_signals[COMPLETE], 0);
		break;
	}

	g_atomic_int_add (&update->model->priv->pending_update_count, -1);
	g_object_unref (G_OBJECT (update->model));
	g_free (update);
	return FALSE;
}

void
rhythmdb_query_model_add_entry (RhythmDBQueryModel *model, RhythmDBEntry *entry, gint index)
{
	struct RhythmDBQueryModelUpdate *update;

	if (!model->priv->show_hidden && rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN)) {
		rb_debug ("attempting to add hidden entry");
		return;
	}

	if (model->priv->base_model) {
		/* add it to the base model, which will cause it to be added to this one */
		rhythmdb_query_model_add_entry (model->priv->base_model, entry,
						rhythmdb_query_model_child_index_to_base_index (model, index));
		return;
	}

	rb_debug ("inserting entry %p at index %d", entry, index);
	
	update = g_new (struct RhythmDBQueryModelUpdate, 1);
	update->type = RHYTHMDB_QUERY_MODEL_UPDATE_ROW_INSERTED_INDEX;
	update->entrydata.data.entry = entry;
	update->entrydata.data.index = index;
	update->model = model;

	g_object_ref (G_OBJECT (model));
	rhythmdb_entry_ref (model->priv->db, entry);
	rhythmdb_query_model_process_update (update);
}

guint64
rhythmdb_query_model_get_size (RhythmDBQueryModel *model)
{
	return model->priv->total_size;
}

long
rhythmdb_query_model_get_duration (RhythmDBQueryModel *model)
{
	return model->priv->total_duration;
}

static void
rhythmdb_query_model_insert_into_main_list (RhythmDBQueryModel *model, RhythmDBEntry *entry, gint index)
{
	GSequencePtr ptr;
	rhythmdb_entry_ref (model->priv->db, entry);

	if (model->priv->sort_func) {
		GCompareDataFunc sort_func;
		gpointer sort_data;
		struct ReverseSortData reverse_data;

		if (model->priv->sort_reverse) {
			sort_func = (GCompareDataFunc) _reverse_sorting_func;
			sort_data = &reverse_data;
			reverse_data.func = model->priv->sort_func;
			reverse_data.data = model->priv->sort_data;
		} else {
			sort_func = model->priv->sort_func;
			sort_data = model->priv->sort_data;
		}

		ptr = g_sequence_insert_sorted (model->priv->entries, entry,
						sort_func,
						sort_data);
	} else {
		if (index == -1) {
			ptr = g_sequence_get_end_ptr (model->priv->entries);
		} else {
			ptr = g_sequence_get_ptr_at_pos (model->priv->entries, index);
		}

		g_sequence_insert (ptr, entry);
		ptr = g_sequence_ptr_prev (ptr);
	}

	g_hash_table_insert (model->priv->reverse_map, entry, ptr);

	model->priv->total_duration += rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
	model->priv->total_size += rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
}

static void
rhythmdb_query_model_insert_into_limited_list (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	GSequencePtr ptr;

	rhythmdb_entry_ref (model->priv->db, entry);

	if (model->priv->sort_func) {
		GCompareDataFunc sort_func;
		gpointer sort_data;
		struct ReverseSortData reverse_data;

		if (model->priv->sort_reverse) {
			sort_func = (GCompareDataFunc) _reverse_sorting_func;
			sort_data = &reverse_data;
			reverse_data.func = model->priv->sort_func;
			reverse_data.data = model->priv->sort_data;
		} else {
			sort_func = model->priv->sort_func;
			sort_data = model->priv->sort_data;
		}

		ptr = g_sequence_insert_sorted (model->priv->limited_entries, entry,
						sort_func,
						sort_data);
	} else {
		ptr = g_sequence_get_end_ptr (model->priv->limited_entries);
		g_sequence_insert (ptr, entry);
		ptr = g_sequence_ptr_prev (ptr);
	}

	g_hash_table_insert (model->priv->limited_reverse_map, entry, ptr);
}

static void
rhythmdb_query_model_remove_from_main_list (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	GSequencePtr ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	int index;
	GtkTreePath *path;

	index = g_sequence_ptr_get_position (ptr);
	
	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, index);

	gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
	gtk_tree_path_free (path);
	
	model->priv->total_duration -= rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
	model->priv->total_size -= rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);

	g_sequence_remove (ptr);
	g_assert (g_hash_table_remove (model->priv->reverse_map, entry));
	
	rhythmdb_entry_unref (model->priv->db, entry);
}

static void
rhythmdb_query_model_remove_from_limited_list (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	GSequencePtr ptr = g_hash_table_lookup (model->priv->limited_reverse_map, entry);

	g_sequence_remove (ptr);
	g_hash_table_remove (model->priv->limited_reverse_map, entry);
	rhythmdb_entry_unref (model->priv->db, entry);
}

static void
rhythmdb_query_model_update_limited_entries (RhythmDBQueryModel *model)
{
	RhythmDBEntry *entry;
	GSequencePtr ptr;

	/* make it fit inside the limits */
	while (!rhythmdb_query_model_within_limit (model, NULL)) {
		ptr = g_sequence_ptr_prev (g_sequence_get_end_ptr (model->priv->entries));
		entry = (RhythmDBEntry*) g_sequence_ptr_get_data (ptr);

		rhythmdb_query_model_remove_from_main_list (model, entry);
		rhythmdb_query_model_insert_into_limited_list (model, entry);
	}

	/* move entries that were previously limited, back to the main list */
	while (TRUE) {
		GtkTreePath *path;
		GtkTreeIter iter;

		ptr = g_sequence_get_begin_ptr (model->priv->limited_entries);
		if (!ptr || ptr == g_sequence_get_end_ptr (model->priv->limited_entries))
			break;
		entry = (RhythmDBEntry*) g_sequence_ptr_get_data (ptr);
		if (!entry)
			break;

		if (!rhythmdb_query_model_within_limit (model, entry))
			break;

		rhythmdb_query_model_remove_from_limited_list (model, entry);
		rhythmdb_query_model_insert_into_main_list (model, entry, -1);

		ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
		iter.stamp = model->priv->stamp;
		iter.user_data = ptr;
		path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
						      &iter);
		gtk_tree_model_row_inserted (GTK_TREE_MODEL (model),
					     path, &iter);
		gtk_tree_path_free (path);
	}
}

static gboolean
rhythmdb_query_model_emit_reorder (RhythmDBQueryModel *model, gint old_pos, gint new_pos)
{
	int length, i;
	gint *reorder_map;
	GtkTreePath *path;
	GtkTreeIter iter;

	if (new_pos == old_pos) {
		/* it hasn't moved, don't emit a re-order */
		return FALSE;
	}

	length = g_sequence_get_length (model->priv->entries);
	reorder_map = malloc (length * sizeof(gint));

	if (new_pos > old_pos) {
		/* it has mover further down the list */
		for (i = 0; i < old_pos; i++)
			reorder_map[i] = i;
		for (i = old_pos; i < new_pos; i++)
			reorder_map[i] = i + 1;
		reorder_map[new_pos] = old_pos;
		for (i = new_pos + 1; i < length; i++)
			reorder_map[i] = i;
	} else {
		/* it has moved up the list */
		for (i = 0; i < new_pos; i++)
			reorder_map[i] = i;
		reorder_map[new_pos] = old_pos;
		for (i = new_pos + 1; i < old_pos + 1; i++)
			reorder_map[i] = i - 1;
		for (i = old_pos + 1; i < length; i++)
			reorder_map[i] = i;
	}

	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter);
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	gtk_tree_model_rows_reordered (GTK_TREE_MODEL (model),
				       path, &iter,
				       reorder_map);
	gtk_tree_path_free (path);
	free (reorder_map);
	return TRUE;
}

static gboolean
rhythmdb_query_model_do_reorder (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	GSequencePtr ptr;
	int old_pos, new_pos;
	GtkTreePath *path;
	GtkTreeIter iter;
	GCompareDataFunc sort_func;
	gpointer sort_data;
	struct ReverseSortData reverse_data;

	if (model->priv->sort_func == NULL)
		return FALSE;

	if (model->priv->sort_reverse) {
		sort_func = (GCompareDataFunc) _reverse_sorting_func;
		sort_data = &reverse_data;
		reverse_data.func = model->priv->sort_func;
		reverse_data.data = model->priv->sort_data;
	} else {
		sort_func = model->priv->sort_func;
		sort_data = model->priv->sort_data;
	}

	ptr = g_sequence_get_begin_ptr (model->priv->limited_entries);

	if (ptr != NULL && !g_sequence_ptr_is_end (ptr)) {
		RhythmDBEntry *first_limited = g_sequence_ptr_get_data (ptr);
		int cmp = (sort_func) (entry, first_limited, sort_data);

		if (cmp > 0) {
			/* the entry belongs in the limited list, so we don't need a re-order */
			rhythmdb_query_model_remove_entry (model, entry);
			rhythmdb_query_model_do_insert (model, entry, -1);
			return TRUE;
		}
	}

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	iter.stamp = model->priv->stamp;
	iter.user_data = ptr;
	path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
					      &iter);
	gtk_tree_model_row_changed (GTK_TREE_MODEL (model),
				     path, &iter);
	gtk_tree_path_free (path);

	/* it may have moved, check for a re-order */
	g_hash_table_remove (model->priv->reverse_map, entry);
	old_pos = g_sequence_ptr_get_position (ptr);
	g_sequence_remove (ptr);

	ptr = g_sequence_insert_sorted (model->priv->entries, entry,
					sort_func,
					sort_data);
	new_pos = g_sequence_ptr_get_position (ptr);
	g_hash_table_insert (model->priv->reverse_map, entry, ptr);

	return rhythmdb_query_model_emit_reorder (model, old_pos, new_pos);
}

static void
rhythmdb_query_model_do_insert (RhythmDBQueryModel *model,
				RhythmDBEntry *entry,
				gint index)
{
	GSequencePtr ptr;
	GtkTreePath *path;
	GtkTreeIter iter;

	g_assert (model->priv->show_hidden || !rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN));

	/* we check again if the entry already exists in the hash table */
	if (g_hash_table_lookup (model->priv->reverse_map, entry) != NULL)
		return;
	ptr = g_hash_table_lookup (model->priv->limited_reverse_map, entry);
	if (ptr != NULL)
		rhythmdb_query_model_remove_from_limited_list (model, entry);

	rhythmdb_query_model_insert_into_main_list (model, entry, index);

	/* it was added to the main list, send out the inserted signal */
	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	iter.stamp = model->priv->stamp;
	iter.user_data = ptr;
	path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
					      &iter);
	gtk_tree_model_row_inserted (GTK_TREE_MODEL (model),
				     path, &iter);
	gtk_tree_path_free (path);

	rhythmdb_query_model_update_limited_entries (model);
}

static void
rhythmdb_query_model_filter_out_entry (RhythmDBQueryModel *model,
				       RhythmDBEntry *entry)
{
	GSequencePtr ptr;

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	if (ptr != NULL) {
		rhythmdb_query_model_remove_from_main_list (model, entry);
		rhythmdb_query_model_update_limited_entries (model);
		return;
	}

	ptr = g_hash_table_lookup (model->priv->limited_reverse_map, entry);
	if (ptr != NULL) {
		rhythmdb_query_model_remove_from_limited_list (model, entry);
		rhythmdb_query_model_update_limited_entries (model);
		return;
	}
}
				
void
rhythmdb_query_model_move_entry (RhythmDBQueryModel *model, RhythmDBEntry *entry, gint index)
{
	GSequencePtr ptr;
	GSequencePtr nptr;
	gint old_pos;

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	if (ptr == NULL)
		return;

	nptr = g_sequence_get_ptr_at_pos (model->priv->entries, index);
	if ((nptr == NULL) || (ptr == nptr))
		return;

	/* remove from old position */
	old_pos = g_sequence_ptr_get_position (ptr);
	g_sequence_remove (ptr);
	g_hash_table_remove (model->priv->reverse_map, entry);

	/* insert into new position */
	g_sequence_insert (nptr, entry);
	ptr = g_sequence_ptr_prev (nptr);
	g_hash_table_insert (model->priv->reverse_map, entry, ptr);

	rhythmdb_query_model_emit_reorder (model, old_pos, index);
}

gboolean
rhythmdb_query_model_remove_entry (RhythmDBQueryModel *model, 
				   RhythmDBEntry *entry)
{
	gboolean present = (g_hash_table_lookup (model->priv->reverse_map, entry) == NULL) ||
			    (g_hash_table_lookup (model->priv->limited_reverse_map, entry) == NULL);
	g_return_val_if_fail (present, FALSE);

	if (model->priv->base_model)
		return rhythmdb_query_model_remove_entry (model->priv->base_model, entry);

	/* emit entry-removed, so listeners know the
	 * entry has actually been removed, rather than filtered
	 * out.
	 */
	g_signal_emit (G_OBJECT (model),
		       rhythmdb_query_model_signals[ENTRY_REMOVED], 0,
		       entry);
	rhythmdb_query_model_filter_out_entry (model, entry);

	return TRUE;
}

gboolean
rhythmdb_query_model_entry_to_iter (RhythmDBQueryModel *model, RhythmDBEntry *entry,
				    GtkTreeIter *iter)
{
	GSequencePtr ptr;

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);

	if (G_UNLIKELY (ptr == NULL)) {
		/* Invalidate iterator so future uses break quickly. */
		iter->stamp = !(model->priv->stamp);
		return FALSE;
	}

	iter->stamp = model->priv->stamp;
	iter->user_data = ptr;
	return TRUE;
}

RhythmDBEntry *
rhythmdb_query_model_tree_path_to_entry (RhythmDBQueryModel *model,
					 GtkTreePath *path)
{
	GtkTreeIter entry_iter;

	g_assert (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &entry_iter, path));
	return rhythmdb_query_model_iter_to_entry (model, &entry_iter);
}

RhythmDBEntry *
rhythmdb_query_model_iter_to_entry (RhythmDBQueryModel *model,
				     GtkTreeIter *entry_iter)
{
	RhythmDBEntry *entry;
	gtk_tree_model_get (GTK_TREE_MODEL (model), entry_iter, 0, &entry, -1);
	return entry;
}

RhythmDBEntry *
rhythmdb_query_model_get_next_from_entry (RhythmDBQueryModel *model,
					  RhythmDBEntry *entry)
{
	GtkTreeIter iter;

	g_return_val_if_fail (entry != NULL, NULL);

	if (entry && rhythmdb_query_model_entry_to_iter (model, entry, &iter)) {
		if (!gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter))
			return NULL;
	} else {
		/* If the entry isn't in the model, the "next" entry is the first. */
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
			return NULL;
	}

	return rhythmdb_query_model_iter_to_entry (model, &iter);
}

RhythmDBEntry *
rhythmdb_query_model_get_previous_from_entry (RhythmDBQueryModel *model,
					      RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	g_return_val_if_fail (entry != NULL, NULL);

	if (!rhythmdb_query_model_entry_to_iter (model, entry, &iter))
		return NULL;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	g_assert (path);
	if (!gtk_tree_path_prev (path)) {
		gtk_tree_path_free (path);
		return NULL;
	}

	g_assert (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path));
	gtk_tree_path_free (path);
	return rhythmdb_query_model_iter_to_entry (model, &iter);
}

static gboolean
rhythmdb_query_model_row_draggable (RbTreeDragSource *dragsource,
					  GList *paths)
{
	return TRUE;
}

static gboolean
rhythmdb_query_model_drag_data_delete (RbTreeDragSource *dragsource, GList *paths)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (dragsource);
	GtkTreeModel *treemodel = GTK_TREE_MODEL (model);

	/* we don't delete if it is a reorder drag and drop because the deletion already
	   occured in rhythmdb_query_model_drag_data_received */
	if (model->priv->sort_func == NULL && !model->priv->reorder_drag_and_drop) {

		RhythmDBEntry *entry;
		GtkTreeIter iter;
		GtkTreePath *path;

		for (; paths; paths = paths->next) {

			path = gtk_tree_row_reference_get_path (paths->data);

			if (path) {
				if (rhythmdb_query_model_get_iter (treemodel, &iter, path)) {
					entry = g_sequence_ptr_get_data (iter.user_data);
					rhythmdb_query_model_remove_entry (model, entry);
				}
				gtk_tree_path_free (path);
			}
		}
	}

	model->priv->reorder_drag_and_drop = FALSE;
	return TRUE;

}

static gboolean
rhythmdb_query_model_drag_data_get (RbTreeDragSource *dragsource,
					  GList *paths,
					  GtkSelectionData *selection_data)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (dragsource);
	guint target;

	rb_debug ("getting drag data");

	if (gtk_target_list_find (rhythmdb_query_model_drag_target_list,
				  selection_data->target, &target)) {
		RhythmDBEntry *entry;
		GList *tem;
		GString *data;
		gboolean need_newline = FALSE;

		data = g_string_new ("");

		for (tem = paths; tem; tem = tem->next) {
			GtkTreeIter iter;
			GtkTreePath *path;
			const char *location;

			path = gtk_tree_row_reference_get_path (tem->data);

			gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);

			entry = g_sequence_ptr_get_data (iter.user_data);

			location = rhythmdb_entry_get_playback_uri (entry);
			if (location == NULL)
				continue;

			if (need_newline)
				g_string_append (data, "\r\n");

			g_string_append (data, location);
			need_newline = TRUE;
		}

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8, (guchar *) data->str,
					data->len);

		g_string_free (data, TRUE);

		return TRUE;
	}

	return FALSE;
}

static gboolean
rhythmdb_query_model_drag_data_received (RbTreeDragDest *drag_dest,
					 GtkTreePath *dest,
					 GtkTreeViewDropPosition pos,
					 GtkSelectionData  *selection_data)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (drag_dest);

	if (model->priv->base_model) {
		GtkTreeIter base_iter;
		GtkTreePath *base_dest;
		RhythmDBEntry *entry;
		gboolean result;

		if (dest) {
			entry = rhythmdb_query_model_tree_path_to_entry (model, dest);
			g_assert (entry);
			rhythmdb_query_model_entry_to_iter (model->priv->base_model, entry, &base_iter);
			base_dest = gtk_tree_model_get_path (GTK_TREE_MODEL (model->priv->base_model), &base_iter);
		} else {
			base_dest = NULL;
		}
		
		result = rhythmdb_query_model_drag_data_received ((RbTreeDragDest*)model->priv->base_model,
								  base_dest, pos, selection_data);
		if (base_dest)
			gtk_tree_path_free (base_dest);

		return result;
	}

	rb_debug ("drag received");

	if (model->priv->sort_func != NULL)
		return FALSE;

	if (selection_data->format == 8 && selection_data->length >= 0) {
		GtkTreeIter iter;
		GSequencePtr ptr;
		char **strv;
		RhythmDBEntry *entry;
		int i = 0;

		strv = g_strsplit ((char *) selection_data->data, "\r\n", -1);

		if (dest == NULL || !rhythmdb_query_model_get_iter (GTK_TREE_MODEL (model), &iter, dest))
			ptr = g_sequence_get_end_ptr (model->priv->entries);
		else
			ptr = iter.user_data;

		if (pos == GTK_TREE_VIEW_DROP_AFTER)
			ptr = g_sequence_ptr_next (ptr);

		for (; strv[i]; i++) {
			GSequencePtr tem_ptr;
			GtkTreeIter tem_iter;
			GtkTreePath *tem_path;

			if (g_utf8_strlen (strv[i], -1) == 0)
				continue;

			entry = rhythmdb_entry_lookup_by_location (model->priv->db,
								   strv[i]);

			if (entry == NULL) {
				int pos;

				if (g_sequence_ptr_is_end (ptr))
					pos = -1;
				else
					pos = g_sequence_ptr_get_position (ptr);

				g_signal_emit (G_OBJECT (model),
					       rhythmdb_query_model_signals[NON_ENTRY_DROPPED],
					       0, strv[i], pos);
			} else {
				GSequencePtr old_ptr = g_hash_table_lookup (model->priv->reverse_map,
									    entry);

				rhythmdb_entry_ref (model->priv->db, entry);

				/* the entry already exists it is either a reorder drag and drop
				   (or a drag and drop from another application), so we delete
				   the existing one before adding it again. */
				if (old_ptr) {
					model->priv->reorder_drag_and_drop = TRUE;

					/* trying to drag drop an entry on itself ! */
					if (old_ptr == ptr)
						continue;

					rhythmdb_query_model_remove_entry (model, entry);

				} else {

					model->priv->reorder_drag_and_drop = FALSE;
				}

				g_sequence_insert (ptr, entry);

				tem_ptr = g_sequence_ptr_prev (ptr);

				tem_iter.stamp = model->priv->stamp;
				tem_iter.user_data = tem_ptr;
				g_hash_table_insert (model->priv->reverse_map,
						     entry, tem_ptr);

				tem_path = rhythmdb_query_model_get_path (GTK_TREE_MODEL (model),
									  &tem_iter);

				gtk_tree_model_row_inserted (GTK_TREE_MODEL (model),
							     tem_path, &tem_iter);
				gtk_tree_path_free (tem_path);
			}
		}

		g_strfreev (strv);
		return TRUE;
	}
	return FALSE;
}

static gboolean
rhythmdb_query_model_row_drop_possible (RbTreeDragDest *drag_dest,
					GtkTreePath *dest,
					GtkTreeViewDropPosition pos,
					GtkSelectionData  *selection_data)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (drag_dest);
	
	while (model) {
		if (model->priv->sort_func != NULL)
			return FALSE;

		model = model->priv->base_model;
	}
	return TRUE;
}

static gboolean
rhythmdb_query_model_row_drop_position (RbTreeDragDest   *drag_dest,
					GtkTreePath       *dest_path,
					GList *targets,
					GtkTreeViewDropPosition *pos)
{
	return TRUE;
}

static void
rhythmdb_query_model_set_query (RhythmDBQueryResults *results, GPtrArray *query)
{
	g_object_set (G_OBJECT (results), "query", query, NULL);
}

/* Threading: Called from the database query thread for async queries,
 *  from the main thread for synchronous queries.
 */
static void
rhythmdb_query_model_add_results (RhythmDBQueryResults *results, GPtrArray *entries)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (results);
	struct RhythmDBQueryModelUpdate *update;
	guint i;

	rb_debug ("adding %d entries", entries->len);

	update = g_new (struct RhythmDBQueryModelUpdate, 1);
	update->type = RHYTHMDB_QUERY_MODEL_UPDATE_ROWS_INSERTED;
	update->entrydata.entries = entries;
	update->model = model;
	g_object_ref (G_OBJECT (model));

	for (i = 0; i < update->entrydata.entries->len; i++)
		rhythmdb_entry_ref (model->priv->db, g_ptr_array_index (update->entrydata.entries, i));

	rhythmdb_query_model_process_update (update);
}

static void
rhythmdb_query_model_query_complete (RhythmDBQueryResults *results)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (results);
	struct RhythmDBQueryModelUpdate *update;

	update = g_new0 (struct RhythmDBQueryModelUpdate, 1);
	update->type = RHYTHMDB_QUERY_MODEL_UPDATE_QUERY_COMPLETE;
	update->model = model;
	g_object_ref (G_OBJECT (model));

	rhythmdb_query_model_process_update (update);
}


static GtkTreeModelFlags
rhythmdb_query_model_get_flags (GtkTreeModel *model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint
rhythmdb_query_model_get_n_columns (GtkTreeModel *tree_model)
{
	return 2;
}

static GType
rhythmdb_query_model_get_column_type (GtkTreeModel *tree_model, int index)
{
	switch (index)
	{
	case 0:
		return G_TYPE_POINTER;
	case 1:
		return G_TYPE_INT;
	default:
		g_assert_not_reached ();
		return G_TYPE_INVALID;
	}
}

static gboolean
rhythmdb_query_model_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter,
			       GtkTreePath *path)
{
	RhythmDBQueryModel *model = RHYTHMDB_QUERY_MODEL (tree_model);
	guint index;
	GSequencePtr ptr;

	index = gtk_tree_path_get_indices (path)[0];

	if (index >= g_sequence_get_length (model->priv->entries))
		return FALSE;

	ptr = g_sequence_get_ptr_at_pos (model->priv->entries, index);
	g_assert (ptr);

	iter->stamp = model->priv->stamp;
	iter->user_data = ptr;

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

	switch (column)
	{
	case 0:
		g_value_init (value, G_TYPE_POINTER);
		g_value_set_pointer (value, entry);
		break;
	case 1:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, g_sequence_ptr_get_position (iter->user_data)+1);
		break;
	default:
		g_assert_not_reached ();
	}
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

char *
rhythmdb_query_model_compute_status_normal (RhythmDBQueryModel *model)
{
	return rhythmdb_compute_status_normal (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL),
					       rhythmdb_query_model_get_duration (model),
					       rhythmdb_query_model_get_size (model));
}

static void
apply_updated_entry_sequence (RhythmDBQueryModel *model, GSequence *new_entries)
{
	int *reorder_map;
	int length, i;
	GtkTreePath *path;
	GtkTreeIter iter;
	GSequencePtr ptr;

	length = g_sequence_get_length (new_entries);
	/* generate resort map and rebuild reverse map */
	reorder_map = malloc (length * sizeof(gint));

	ptr = g_sequence_get_begin_ptr (new_entries);
	for (i = 0; i < length; i++) {
		gpointer entry = g_sequence_ptr_get_data (ptr);
		GSequencePtr old_ptr;

		old_ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
		reorder_map[i] = g_sequence_ptr_get_position (ptr);
		g_hash_table_replace (model->priv->reverse_map, entry, ptr);

		ptr = g_sequence_ptr_next (ptr);
	}

	/* emit the re-order and clean up */
	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter);
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	gtk_tree_model_rows_reordered (GTK_TREE_MODEL (model),
				       path, &iter,
				       reorder_map);

	gtk_tree_path_free (path);
	free (reorder_map);
	g_sequence_free (model->priv->entries);
	model->priv->entries = new_entries;
}

void
rhythmdb_query_model_set_sort_order (RhythmDBQueryModel *model,
				     GCompareDataFunc sort_func,
				     gpointer sort_data,
				     GDestroyNotify sort_data_destroy,
				     gboolean sort_reverse)
{
	GSequence *new_entries;
	GSequencePtr ptr;
	int length, i;
	struct ReverseSortData reverse_data;

	if ((model->priv->sort_func == sort_func) &&
	    (model->priv->sort_data == sort_data) &&
	    (model->priv->sort_data_destroy == sort_data_destroy) &&
	    (model->priv->sort_reverse == sort_reverse))
		return;
	
	g_return_if_fail ((model->priv->limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) ||
			  (model->priv->sort_func == NULL));
	if (model->priv->sort_func == NULL)
		g_assert (g_sequence_get_length (model->priv->limited_entries) == 0);
	
	if (model->priv->sort_data_destroy && model->priv->sort_data)
		model->priv->sort_data_destroy (model->priv->sort_data);
	
	model->priv->sort_func = sort_func;
	model->priv->sort_data = sort_data;
	model->priv->sort_data_destroy = sort_data_destroy;
	model->priv->sort_reverse = sort_reverse;

	if (model->priv->sort_reverse) {
		reverse_data.func = sort_func;
		reverse_data.data = sort_data;
		sort_func = (GCompareDataFunc) _reverse_sorting_func;
		sort_data = &reverse_data;
	}

	/* create the new sorted entry sequence */
	length = g_sequence_get_length (model->priv->entries);
	if (length > 0) {
		new_entries = g_sequence_new (NULL);
		ptr = g_sequence_get_begin_ptr (model->priv->entries);
		for (i = 0; i < length; i++) {
			gpointer entry = g_sequence_ptr_get_data (ptr);

			g_sequence_insert_sorted (new_entries, entry,
						  sort_func,
						  sort_data);
			ptr = g_sequence_ptr_next (ptr);
		}

		apply_updated_entry_sequence (model, new_entries);
	}
}

static int
rhythmdb_query_model_child_index_to_base_index (RhythmDBQueryModel *model, int index)
{
	GSequencePtr ptr;
	RhythmDBEntry *entry;
	g_assert (model->priv->base_model);

	ptr = g_sequence_get_ptr_at_pos (model->priv->entries, index);
	if (ptr == NULL || g_sequence_ptr_is_end (ptr))
		return -1;
	entry = (RhythmDBEntry*)g_sequence_ptr_get_data (ptr);

	ptr = g_hash_table_lookup (model->priv->base_model->priv->reverse_map, entry);
	g_assert (ptr); /* all child model entries are in the base model */

	return g_sequence_ptr_get_position (ptr);
}

/*static int
rhythmdb_query_model_base_index_to_child_index (RhythmDBQueryModel *model, int index)
{
	GSequencePtr ptr;
	RhythmDBEntry *entry;
	int pos;

	g_assert (model->priv->base_model);
	if (index == -1)
		return -1;

	ptr = g_sequence_get_ptr_at_pos (model->priv->base_model->priv->entries, index);
	if (ptr == NULL || g_sequence_ptr_is_end (ptr))
		return -1;
	entry = (RhythmDBEntry*)g_sequence_ptr_get_data (ptr);

	ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	if (ptr == NULL)
		return -1;

	pos = g_sequence_ptr_get_position (ptr);
	return pos;
}*/

static int
rhythmdb_query_model_get_entry_index (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	GSequencePtr ptr = g_hash_table_lookup (model->priv->reverse_map, entry);
	
	if (ptr)
		return g_sequence_ptr_get_position (ptr);
	else
		return -1;
}

static void
rhythmdb_query_model_base_row_inserted (GtkTreeModel *tree_model,
					GtkTreePath *path,
					GtkTreeIter *iter,
					RhythmDBQueryModel *model)
{
	RhythmDBQueryModel *base_model = RHYTHMDB_QUERY_MODEL (tree_model);
	RhythmDBEntry *entry;
	RhythmDBEntry *prev_entry;
	int index;

	g_assert (base_model == model->priv->base_model);
	
	entry = rhythmdb_query_model_iter_to_entry (base_model, iter);
	if (!model->priv->show_hidden && rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN))
		return;

	if (rhythmdb_evaluate_query (model->priv->db, model->priv->query, entry)) {
		/* find the closest previous entry that is in the filter model, and it it after that */
		prev_entry = rhythmdb_query_model_get_previous_from_entry (base_model, entry);
		while (prev_entry && g_hash_table_lookup (model->priv->reverse_map, prev_entry) == NULL) {
			prev_entry = rhythmdb_query_model_get_previous_from_entry (base_model, prev_entry);
		}
	
		if (entry != NULL)
			index = rhythmdb_query_model_get_entry_index (model, prev_entry) + 1;
		else
			index = 0;

		rb_debug ("inserting entry %p from base model %p to model %p in position %d", entry, base_model, model, index);
		rhythmdb_query_model_do_insert (model, entry, index);
	}
}

static void
rhythmdb_query_model_base_row_deleted (GtkTreeModel *base_model,
				       GtkTreePath *path,
				       RhythmDBQueryModel *model)
{
	RhythmDBEntry *entry;
       	
	entry = rhythmdb_query_model_tree_path_to_entry (RHYTHMDB_QUERY_MODEL (base_model), path);
	rb_debug ("deleting entry %p from base model %p to model %p", entry, base_model, model);

	rhythmdb_query_model_filter_out_entry (model, entry);
}

static void
rhythmdb_query_model_base_non_entry_dropped (GtkTreeModel *base_model,
					     const char *location,
					     int position,
					     RhythmDBQueryModel *model)
{
	g_signal_emit (G_OBJECT (model), rhythmdb_query_model_signals[NON_ENTRY_DROPPED], 0,
		       location, rhythmdb_query_model_child_index_to_base_index (model, position));
}

static void
rhythmdb_query_model_base_complete (GtkTreeModel *base_model,
				    RhythmDBQueryModel *model)
{
	g_signal_emit (G_OBJECT (model), rhythmdb_query_model_signals[COMPLETE], 0);
}

static void
rhythmdb_query_model_base_rows_reordered (GtkTreeModel *base_model,
					  GtkTreePath *arg1,
					  GtkTreeIter *arg2,
					  gint *order_map,
					  RhythmDBQueryModel *model)
{
	RhythmDBQueryModel *base_query_model = RHYTHMDB_QUERY_MODEL (base_model);
	GSequence *new_entries;
	GSequencePtr ptr;

	/* ignore, if this model sorts */
	if (model->priv->sort_func)
		return;

	new_entries = g_sequence_new (NULL);

	/* iterate over the entries in the base, and recreate the sequence */
	ptr = g_sequence_get_begin_ptr (base_query_model->priv->entries);
	while (!g_sequence_ptr_is_end (ptr)) {
		gpointer entry = g_sequence_ptr_get_data (ptr);

		if (g_hash_table_lookup (model->priv->reverse_map, entry))
			g_sequence_append (new_entries, entry);

		ptr = g_sequence_ptr_next (ptr);
	}

	apply_updated_entry_sequence (model, new_entries);
}

static void
rhythmdb_query_model_base_entry_removed (RhythmDBQueryModel *base_model,
					 RhythmDBEntry *entry,
					 RhythmDBQueryModel *model)
{
	/* propagate the signal out to any attached property models */
	g_signal_emit (G_OBJECT (model),
		       rhythmdb_query_model_signals[ENTRY_REMOVED], 0,
		       entry);
}


void
rhythmdb_query_model_reapply_query (RhythmDBQueryModel *model, gboolean filter)
{
	GSequencePtr ptr;
	GSequencePtr next;
	RhythmDBEntry *entry;
	gboolean changed = FALSE;
	GList *remove = NULL;

	/* process limited list first, so entries that don't match can't sneak in 
	 * to the main list from there
	 */
	if (model->priv->limited_entries) {
		ptr = g_sequence_get_begin_ptr (model->priv->limited_entries);
		while (!g_sequence_ptr_is_end (ptr)) {
			next = g_sequence_ptr_next (ptr);
			entry = (RhythmDBEntry *)g_sequence_ptr_get_data (ptr);
			if (!rhythmdb_evaluate_query (model->priv->db, 
						      model->priv->query, 
						      entry)) {
				remove = g_list_prepend (remove, entry);
			}

			ptr = next;
		}

		if (remove) {
			GList *t;
			for (t = remove; t; t = t->next) {
				RhythmDBEntry *entry = t->data;
				rhythmdb_query_model_remove_from_limited_list (model, entry);
			}
			
			changed = TRUE;
			g_list_free (remove);
			remove = NULL;
		}
	}

	if (model->priv->entries) {
		ptr = g_sequence_get_begin_ptr (model->priv->entries);
		while (!g_sequence_ptr_is_end (ptr)) {
			next = g_sequence_ptr_next (ptr);
			entry = (RhythmDBEntry *)g_sequence_ptr_get_data (ptr);
			if (!rhythmdb_evaluate_query (model->priv->db, 
						      model->priv->query, 
						      entry)) {
				remove = g_list_prepend (remove, entry);
			}

			ptr = next;
		}

		if (remove) {
			GList *t;
			for (t = remove; t; t = t->next) {
				RhythmDBEntry *entry = t->data;
				if (!filter) {
					g_signal_emit (G_OBJECT (model),
						       rhythmdb_query_model_signals[ENTRY_REMOVED], 0,
						       entry);
				}
				rhythmdb_query_model_remove_from_main_list (model, entry);
			}
			
			changed = TRUE;
			g_list_free (remove);
			remove = NULL;
		}
	}

	if (changed)
		rhythmdb_query_model_update_limited_entries (model);
}

static gint
_reverse_sorting_func (gpointer a, gpointer b, 
		       struct ReverseSortData *reverse_data)
{
	return - reverse_data->func (a, b, reverse_data->data);
}

gint
rhythmdb_query_model_location_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
					 gpointer data)
{
	const char *a_val;
	const char *b_val;

	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_LOCATION);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_LOCATION);

	if (a_val == NULL) {
		if (b_val == NULL)
			return 0;
		else
			return -1;
	} else if (b_val == NULL)
		return 1;
	else
		return strcmp (a_val, b_val);
}

gint
rhythmdb_query_model_title_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				      gpointer data)
{
	const char *a_val;
	const char *b_val;
	gint ret;

	/* Sort by album name */
	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_TITLE_SORT_KEY);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_TITLE_SORT_KEY);

	if (a_val == NULL) {
		if (b_val == NULL)
			ret = 0;
		else
			ret = -1;
	} else if (b_val == NULL)
		ret = 1;
	else
		ret = strcmp (a_val, b_val);

	if (ret != 0)
		return ret;
	else
		return rhythmdb_query_model_location_sort_func (a, b, data);
}

gint
rhythmdb_query_model_album_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				      gpointer data)
{
	const char *a_val;
	const char *b_val;
	gulong a_num;
	gulong b_num;
	gint ret;

	/* Sort by album name */
	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_ALBUM_SORT_KEY);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_ALBUM_SORT_KEY);

	if (a_val == NULL) {
		if (b_val == NULL)
			ret = 0;
		else
			ret = -1;
	} else if (b_val == NULL)
		ret = 1;
	else
		ret = strcmp (a_val, b_val);

	if (ret != 0)
		return ret;

	/* Then by disc number, */
	a_num = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_DISC_NUMBER);
	b_num = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_DISC_NUMBER);
	if (a_num != b_num)
		return (a_num < b_num ? -1 : 1);

	/* by track number */
	a_num = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_TRACK_NUMBER);
	b_num = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_TRACK_NUMBER);
	if (a_num != b_num)
		return (a_num < b_num ? -1 : 1);

	/*  by title */
	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_TITLE_SORT_KEY);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_TITLE_SORT_KEY);

	if (a_val == NULL) {
		if (b_val == NULL)
			return 0;
		else
			return -1;
	} else if (b_val == NULL)
		return 1;
	else
		return rhythmdb_query_model_location_sort_func (a, b, data);
}


gint
rhythmdb_query_model_artist_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				       gpointer data)
{
	const char *a_val;
	const char *b_val;
	gint ret;

	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_ARTIST_SORT_KEY);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_ARTIST_SORT_KEY);

	if (a_val == NULL) {
		if (b_val == NULL)
			ret = 0;
		else
			ret = -1;
	} else if (b_val == NULL)
		ret = 1;
	else
		ret = strcmp (a_val, b_val);

	if (ret != 0)
		return ret;
	else
		return rhythmdb_query_model_album_sort_func (a, b, data);
}

gint
rhythmdb_query_model_genre_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				      gpointer data)
{
	const char *a_val;
	const char *b_val;
	gint ret;

	a_val = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_GENRE_SORT_KEY);
	b_val = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_GENRE_SORT_KEY);

	if (a_val == NULL) {
		if (b_val == NULL)
			ret = 0;
		else
			ret = -1;
	} else if (b_val == NULL)
		ret = 1;
	else
		ret = strcmp (a_val, b_val);

	if (ret != 0)
		return ret;
	else
		return rhythmdb_query_model_artist_sort_func (a, b, data);
}


gint
rhythmdb_query_model_track_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				      gpointer data)
{
	return rhythmdb_query_model_album_sort_func (a, b, data);
}


gint
rhythmdb_query_model_double_ceiling_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
					       gpointer data)
{
	gdouble a_val, b_val;
	RhythmDBPropType prop_id;

	prop_id = (RhythmDBPropType) GPOINTER_TO_INT (data);

	a_val = ceil (rhythmdb_entry_get_double (a, prop_id));
	b_val = ceil (rhythmdb_entry_get_double (b, prop_id));

	if (a_val != b_val)
		return (a_val > b_val ? 1 : -1);
	else
		return rhythmdb_query_model_location_sort_func (a, b, data);
}

gint
rhythmdb_query_model_ulong_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				      gpointer data)
{
	gulong a_val, b_val;
	RhythmDBPropType prop_id;

	prop_id = (RhythmDBPropType) GPOINTER_TO_INT (data);
	a_val = rhythmdb_entry_get_ulong (a, prop_id);
	b_val = rhythmdb_entry_get_ulong (b, prop_id);

	if (a_val != b_val)
		return (a_val > b_val ? 1 : -1);
	else
		return rhythmdb_query_model_location_sort_func (a, b, data);
}

gint
rhythmdb_query_model_date_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				     gpointer data)
			      
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_DATE);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_DATE);

	ret = (a_val == b_val ? 0 : (a_val > b_val ? 1 : -1));
	if (a_val > b_val)
		return 1;
	else if (a_val < b_val)
		return -1;
	else
		return rhythmdb_query_model_album_sort_func (a, b, data);
}

gint
rhythmdb_query_model_string_sort_func (RhythmDBEntry *a, RhythmDBEntry *b,
				       gpointer data)
{
	const char *a_val;
	const char *b_val;
	gint ret;
	RhythmDBPropType prop_id;

	prop_id = (RhythmDBPropType) GPOINTER_TO_INT (data);
	a_val = rhythmdb_entry_get_string (a, prop_id);
	b_val = rhythmdb_entry_get_string (b, prop_id);

	if (a_val == NULL) {
		if (b_val == NULL)
			ret = 0;
		else
			ret = -1;
	} else if (b_val == NULL)
		ret = 1;
	else
		ret = strcmp (a_val, b_val);

	if (ret != 0)
		return ret;
	else
		return rhythmdb_query_model_location_sort_func (a, b, data);
}

static gboolean
rhythmdb_query_model_within_limit (RhythmDBQueryModel *model, RhythmDBEntry *entry)
{
	gboolean result = TRUE;

	switch (model->priv->limit_type) {
	case RHYTHMDB_QUERY_MODEL_LIMIT_NONE:
		result = TRUE;
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_COUNT:
		{
			gulong limit_count;
			gulong current_count;
			
			limit_count = g_value_get_ulong (g_value_array_get_nth (model->priv->limit_value, 0));
			current_count = g_hash_table_size (model->priv->reverse_map);

			if (entry)
				current_count++;

			result = (current_count <= limit_count);
			break;
		}

	case RHYTHMDB_QUERY_MODEL_LIMIT_SIZE:
		{
			guint64 limit_size;
			guint64 current_size;
			
			limit_size = g_value_get_uint64 (g_value_array_get_nth (model->priv->limit_value, 0));
			current_size = model->priv->total_size;

			if (entry)
				current_size += rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);

			/* the limit is in MB */
			result = (current_size / (1024 * 1024) <= limit_size);
			break;
		}

	case RHYTHMDB_QUERY_MODEL_LIMIT_TIME:
		{
			gulong limit_time;
			gulong current_time;
			
			limit_time = g_value_get_ulong (g_value_array_get_nth (model->priv->limit_value, 0));
			current_time = model->priv->total_duration;

			if (entry)
				current_time += rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

			result = (current_time <= limit_time);
			break;
		}
	}

	return result;
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rhythmdb_query_model_limit_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (RHYTHMDB_QUERY_MODEL_LIMIT_NONE, "No limit"),
			ENUM_ENTRY (RHYTHMDB_QUERY_MODEL_LIMIT_COUNT, "Limit by number of entries (count)"),
			ENUM_ENTRY (RHYTHMDB_QUERY_MODEL_LIMIT_SIZE, "Limit by data size (Mb)"),
			ENUM_ENTRY (RHYTHMDB_QUERY_MODEL_LIMIT_TIME, "Limit by duration (seconds)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBQueryModelLimitType", values);
	}

	return etype;
}

