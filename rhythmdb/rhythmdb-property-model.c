/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib.h>

#include "rhythmdb-property-model.h"
#include "rb-debug.h"
#include "rb-refstring.h"
#include "rb-tree-dnd.h"

static void rhythmdb_property_model_tree_model_init (GtkTreeModelIface *iface);
static void rhythmdb_property_model_drag_source_init (RbTreeDragSourceIface *iface);

G_DEFINE_TYPE_WITH_CODE(RhythmDBPropertyModel, rhythmdb_property_model, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL,
					      rhythmdb_property_model_tree_model_init)
			G_IMPLEMENT_INTERFACE(RB_TYPE_TREE_DRAG_SOURCE,
					      rhythmdb_property_model_drag_source_init))

/*
 * Structure for entries in the property model.
 * The sort string is derived from one of a list of properties, so we
 * store the index in the list of the property that gave us the current
 * sort string, so that if a newly added entry has a value for a more preferred
 * property, we can use that instead.
 */
typedef struct {
	RBRefString *string;
	RBRefString *sort_string;
	gint sort_string_from;
	gint refcount;
} RhythmDBPropertyModelEntry;

static void rhythmdb_property_model_dispose (GObject *object);
static void rhythmdb_property_model_finalize (GObject *object);
static void rhythmdb_property_model_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_property_model_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rhythmdb_property_model_sync (RhythmDBPropertyModel *model);
static void rhythmdb_property_model_row_inserted_cb (GtkTreeModel *model,
						     GtkTreePath *path,
						     GtkTreeIter *iter,
						     RhythmDBPropertyModel *propmodel);
static void rhythmdb_property_model_prop_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
						     RhythmDBPropType prop, const GValue *old,
						     const GValue *new,
						     RhythmDBPropertyModel *propmodel);
static void rhythmdb_property_model_entry_removed_cb (RhythmDBQueryModel *model,
						      RhythmDBEntry *entry,
						      RhythmDBPropertyModel *propmodel);
static gboolean update_sort_string (RhythmDBPropertyModel *model,
                                    RhythmDBPropertyModelEntry *prop,
                                    RhythmDBEntry *entry);
static void property_sort_changed (RhythmDBPropertyModel *model,
                                   GSequenceIter *ptr,
                                   GtkTreeIter *iter);
static RhythmDBPropertyModelEntry* rhythmdb_property_model_insert (RhythmDBPropertyModel *model,
								   RhythmDBEntry *entry);
static void rhythmdb_property_model_delete (RhythmDBPropertyModel *model,
					    RhythmDBEntry *entry);
static void rhythmdb_property_model_delete_prop (RhythmDBPropertyModel *model,
						 const char *propstr);
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

static gboolean rhythmdb_property_model_drag_data_get (RbTreeDragSource *dragsource,
						       GList *paths,
						       GtkSelectionData *selection_data);
static gboolean rhythmdb_property_model_drag_data_delete (RbTreeDragSource *dragsource,
							  GList *paths);
static gboolean rhythmdb_property_model_row_draggable (RbTreeDragSource *dragsource,
						       GList *paths);

enum {
	TARGET_ALBUMS,
	TARGET_GENRE,
	TARGET_ARTISTS,
	TARGET_LOCATION,
	TARGET_ENTRIES,
	TARGET_URIS,
	TARGET_COMPOSERS
};

static const GtkTargetEntry targets_album  [] = {
	{ "text/x-rhythmbox-album",  0, TARGET_ALBUMS },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_genre  [] = {
	{ "text/x-rhythmbox-genre",  0, TARGET_GENRE },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_artist [] = {
	{ "text/x-rhythmbox-artist", 0, TARGET_ARTISTS },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_location [] = {
	{ "text/x-rhythmbox-location", 0, TARGET_LOCATION },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
};
static const GtkTargetEntry targets_composer [] = {
	{ "text/x-rhythmbox-composer", 0, TARGET_COMPOSERS },
	{ "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
	{ "text/uri-list", 0, TARGET_URIS },
};

static GtkTargetList *rhythmdb_property_model_album_drag_target_list = NULL;
static GtkTargetList *rhythmdb_property_model_artist_drag_target_list = NULL;
static GtkTargetList *rhythmdb_property_model_genre_drag_target_list = NULL;
static GtkTargetList *rhythmdb_property_model_location_drag_target_list = NULL;
static GtkTargetList *rhythmdb_property_model_composer_drag_target_list = NULL;

struct RhythmDBPropertyModelPrivate
{
	RhythmDB *db;

	RhythmDBQueryModel *query_model;
	GHashTable *entries;

	RhythmDBPropType propid;
	GArray *sort_propids;

	guint stamp;

	GSequence *properties;
	GHashTable *reverse_map;

	RhythmDBPropertyModelEntry *all;

	guint syncing_id;
};

#define RHYTHMDB_PROPERTY_MODEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RHYTHMDB_TYPE_PROPERTY_MODEL, RhythmDBPropertyModelPrivate))

enum
{
	PRE_ROW_DELETION,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_RHYTHMDB,
	PROP_PROP,
	PROP_QUERY_MODEL,
};

static guint rhythmdb_property_model_signals[LAST_SIGNAL] = { 0 };

/**
 * SECTION:rhythmdbpropertymodel
 * @short_description:  tree model grouping entries from a query model by property values
 *
 * A RhythmDBPropertyModel groups the entries in a #RhythmDBQueryModel by
 * the value of a property.  For example, a RhythmDBPropertyModel using
 * the RHYTHMDB_PROP_ARTIST property can be used as the model for a 
 * #GtkTreeView that will list the artists present in the query model.
 *
 * The album/artist/genre browsers displayed in the library and other sources are
 * populated using a RhythmDBPropertyModel for each property.
 */

static void
rhythmdb_property_model_class_init (RhythmDBPropertyModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rhythmdb_property_model_set_property;
	object_class->get_property = rhythmdb_property_model_get_property;

	object_class->dispose = rhythmdb_property_model_dispose;
	object_class->finalize = rhythmdb_property_model_finalize;

	/**
	 * RhythmDBPropertyModel::pre-row-deletion:
	 * @model: the #RhythmDBPropertyModel
	 *
	 * Emitted just before a row is deleted from the model.
	 */
	rhythmdb_property_model_signals[PRE_ROW_DELETION] =
		g_signal_new ("pre-row-deletion",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RhythmDBPropertyModelClass, pre_row_deletion),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RhythmDBPropertyModel:db:
	 *
	 * The #RhythmDB object the model is associated with.
	 */
	g_object_class_install_property (object_class,
					 PROP_RHYTHMDB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RhythmDBPropertyModel:prop:
	 *
	 * The property that this property model indexes.
	 */
	g_object_class_install_property (object_class,
					 PROP_PROP,
					 g_param_spec_int ("prop",
							   "propid",
							   "Property id",
							   0, RHYTHMDB_NUM_PROPERTIES,
							   RHYTHMDB_PROP_TYPE,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RhythmDBPropertyModel:query-model:
	 *
	 * The query model that this property model indexes.
	 */
	g_object_class_install_property (object_class,
					 PROP_QUERY_MODEL,
					 g_param_spec_object ("query-model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel object ",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RhythmDBPropertyModelPrivate));
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
rhythmdb_property_model_drag_source_init (RbTreeDragSourceIface *iface)
{
	iface->rb_row_draggable = rhythmdb_property_model_row_draggable;
	iface->rb_drag_data_delete = rhythmdb_property_model_drag_data_delete;
	iface->rb_drag_data_get = rhythmdb_property_model_drag_data_get;
}

static gboolean
_remove_entry_cb (GtkTreeModel *model,
		  GtkTreePath *path,
		  GtkTreeIter *iter,
		  RhythmDBPropertyModel *propmodel)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);
	rhythmdb_property_model_entry_removed_cb (RHYTHMDB_QUERY_MODEL (model),
						  entry,
						  propmodel);
	rhythmdb_entry_unref (entry);
	return FALSE;
}

static gboolean
_add_entry_cb (GtkTreeModel *model,
	       GtkTreePath *path,
	       GtkTreeIter *iter,
	       RhythmDBPropertyModel *propmodel)
{
	rhythmdb_property_model_row_inserted_cb (model, path, iter, propmodel);
	return FALSE;
}

static void
rhythmdb_property_model_set_query_model_internal (RhythmDBPropertyModel *model,
						  RhythmDBQueryModel    *query_model)
{
	if (model->priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (model->priv->query_model,
						      G_CALLBACK (rhythmdb_property_model_row_inserted_cb),
						      model);
		g_signal_handlers_disconnect_by_func (model->priv->query_model,
						      G_CALLBACK (rhythmdb_property_model_entry_removed_cb),
						      model);
		g_signal_handlers_disconnect_by_func (model->priv->query_model,
						      G_CALLBACK (rhythmdb_property_model_prop_changed_cb),
						      model);

		gtk_tree_model_foreach (GTK_TREE_MODEL (model->priv->query_model),
					(GtkTreeModelForeachFunc)_remove_entry_cb,
					model);

		g_object_unref (model->priv->query_model);
	}

	model->priv->query_model = query_model;
	g_assert (rhythmdb_property_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1);

	if (model->priv->query_model != NULL) {
		g_object_ref (model->priv->query_model);

		g_signal_connect_object (model->priv->query_model,
					 "row_inserted",
					 G_CALLBACK (rhythmdb_property_model_row_inserted_cb),
					 model,
					 0);
		g_signal_connect_object (model->priv->query_model,
					 "post-entry-delete",
					 G_CALLBACK (rhythmdb_property_model_entry_removed_cb),
					 model,
					 0);
		g_signal_connect_object (model->priv->query_model,
					 "entry-prop-changed",
					 G_CALLBACK (rhythmdb_property_model_prop_changed_cb),
					 model,
					 0);
		gtk_tree_model_foreach (GTK_TREE_MODEL (model->priv->query_model),
					(GtkTreeModelForeachFunc)_add_entry_cb,
					model);
	}
}

static void
append_sort_property (RhythmDBPropertyModel *model, RhythmDBPropType prop)
{
	RhythmDBPropType p = prop;
	g_array_append_val (model->priv->sort_propids, p);
}

static void
rhythmdb_property_model_set_property (GObject *object,
				      guint prop_id,
				      const GValue *value,
				      GParamSpec *pspec)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (object);

	switch (prop_id) {
	case PROP_RHYTHMDB:
		model->priv->db = g_value_get_object (value);
		break;
	case PROP_PROP:
		model->priv->propid = g_value_get_int (value);
		switch (model->priv->propid) {
		case RHYTHMDB_PROP_GENRE:
			append_sort_property (model, RHYTHMDB_PROP_GENRE);
			break;
		case RHYTHMDB_PROP_ARTIST:
			append_sort_property (model, RHYTHMDB_PROP_ARTIST_SORTNAME);
			append_sort_property (model, RHYTHMDB_PROP_ARTIST);
			break;
		case RHYTHMDB_PROP_ALBUM:
			append_sort_property (model, RHYTHMDB_PROP_ALBUM_SORTNAME);
			append_sort_property (model, RHYTHMDB_PROP_ALBUM);
			break;
		case RHYTHMDB_PROP_SUBTITLE:
			append_sort_property (model, RHYTHMDB_PROP_ALBUM);
			append_sort_property (model, RHYTHMDB_PROP_SUBTITLE);
			break;
		case RHYTHMDB_PROP_TITLE:
		case RHYTHMDB_PROP_LOCATION:
			append_sort_property (model, RHYTHMDB_PROP_TITLE);
			break;
		case RHYTHMDB_PROP_COMPOSER:
			append_sort_property (model, RHYTHMDB_PROP_COMPOSER_SORTNAME);
			append_sort_property (model, RHYTHMDB_PROP_COMPOSER);
			break;
		default:
			g_assert_not_reached ();
			break;
		}
		break;
	case PROP_QUERY_MODEL:
		rhythmdb_property_model_set_query_model_internal (model, g_value_get_object (value));
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

	switch (prop_id) {
	case PROP_RHYTHMDB:
		g_value_set_object (value, model->priv->db);
		break;
	case PROP_PROP:
		g_value_set_int (value, model->priv->propid);
		break;
	case PROP_QUERY_MODEL:
		g_value_set_object (value, model->priv->query_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rhythmdb_property_model_init (RhythmDBPropertyModel *model)
{
	if (!rhythmdb_property_model_artist_drag_target_list)
		rhythmdb_property_model_artist_drag_target_list =
			gtk_target_list_new (targets_artist,
					     G_N_ELEMENTS (targets_artist));
	if (!rhythmdb_property_model_album_drag_target_list)
		rhythmdb_property_model_album_drag_target_list =
			gtk_target_list_new (targets_album,
					     G_N_ELEMENTS (targets_album));
	if (!rhythmdb_property_model_genre_drag_target_list)
		rhythmdb_property_model_genre_drag_target_list =
			gtk_target_list_new (targets_genre,
					     G_N_ELEMENTS (targets_genre));
	if (!rhythmdb_property_model_location_drag_target_list)
		rhythmdb_property_model_location_drag_target_list =
			gtk_target_list_new (targets_location,
					     G_N_ELEMENTS (targets_location));
	if (!rhythmdb_property_model_composer_drag_target_list)
		rhythmdb_property_model_composer_drag_target_list =
			gtk_target_list_new (targets_composer,
					     G_N_ELEMENTS (targets_composer));

	model->priv = RHYTHMDB_PROPERTY_MODEL_GET_PRIVATE (model);

	model->priv->stamp = g_random_int ();

	model->priv->properties = g_sequence_new (NULL);
	model->priv->reverse_map = g_hash_table_new (g_str_hash, g_str_equal);
	model->priv->entries = g_hash_table_new (g_direct_hash, g_direct_equal);

	model->priv->all = g_new0 (RhythmDBPropertyModelEntry, 1);
	model->priv->all->string = rb_refstring_new (_("All"));

	model->priv->sort_propids = g_array_new (FALSE, FALSE, sizeof (RhythmDBPropType));
}

static void
_prop_model_entry_cleanup (RhythmDBPropertyModelEntry *prop, gpointer data)
{
	rb_refstring_unref (prop->string);
	rb_refstring_unref (prop->sort_string);
	g_free (prop);
}

static void
rhythmdb_property_model_dispose (GObject *object)
{
	RhythmDBPropertyModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_PROPERTY_MODEL (object));

	model = RHYTHMDB_PROPERTY_MODEL (object);

	rb_debug ("disposing property model %p", model);

	g_return_if_fail (model->priv != NULL);

	if (model->priv->syncing_id != 0) {
		g_source_remove (model->priv->syncing_id);
		model->priv->syncing_id = 0;
	}

	if (model->priv->query_model != NULL) {
		g_object_unref (model->priv->query_model);
		model->priv->query_model = NULL;
	}

	G_OBJECT_CLASS (rhythmdb_property_model_parent_class)->dispose (object);
}

static void
rhythmdb_property_model_finalize (GObject *object)
{
	RhythmDBPropertyModel *model;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_PROPERTY_MODEL (object));

	model = RHYTHMDB_PROPERTY_MODEL (object);

	rb_debug ("finalizing property model %p", model);

	g_return_if_fail (model->priv != NULL);

	g_hash_table_destroy (model->priv->reverse_map);

	g_sequence_foreach (model->priv->properties, (GFunc)_prop_model_entry_cleanup, NULL);
	g_sequence_free (model->priv->properties);

	g_hash_table_destroy (model->priv->entries);

	g_free (model->priv->all);

	g_array_free (model->priv->sort_propids, TRUE);

	G_OBJECT_CLASS (rhythmdb_property_model_parent_class)->finalize (object);
}

/**
 * rhythmdb_property_model_new:
 * @db: the #RhythmDB object
 * @propid: the property to index
 *
 * Creates a new property model for the specified property ID.
 *
 * Return value: the new #RhythmDBPropertyModel
 */
RhythmDBPropertyModel *
rhythmdb_property_model_new (RhythmDB *db,
			     RhythmDBPropType propid)
{
	return g_object_new (RHYTHMDB_TYPE_PROPERTY_MODEL, "db", db, "prop", propid, NULL);
}

static void
rhythmdb_property_model_row_inserted_cb (GtkTreeModel *model,
					 GtkTreePath *path,
					 GtkTreeIter *iter,
					 RhythmDBPropertyModel *propmodel)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);

	rhythmdb_property_model_insert (propmodel, entry);
	rhythmdb_property_model_sync (propmodel);

	rhythmdb_entry_unref (entry);
}

static void
rhythmdb_property_model_prop_changed_cb (RhythmDB *db,
					 RhythmDBEntry *entry,
					 RhythmDBPropType propid,
					 const GValue *old,
					 const GValue *new,
					 RhythmDBPropertyModel *propmodel)
{
	if (propid == RHYTHMDB_PROP_HIDDEN) {
		gboolean old_val = g_value_get_boolean (old);
		gboolean new_val = g_value_get_boolean (new);

		if (old_val != new_val) {
			if (new_val == FALSE) {
				g_assert (g_hash_table_remove (propmodel->priv->entries, entry));
				rhythmdb_property_model_insert (propmodel, entry);
			} else {
				g_assert (g_hash_table_lookup (propmodel->priv->entries, entry) == NULL);

				rhythmdb_property_model_delete (propmodel, entry);
				g_hash_table_insert (propmodel->priv->entries, entry, GINT_TO_POINTER (1));
			}
			rhythmdb_property_model_sync (propmodel);
		}
	} else if (g_hash_table_lookup (propmodel->priv->entries, entry) == NULL) {
		RhythmDBPropertyModelEntry *prop;

		if (propid == propmodel->priv->propid) {
			/* the updated property is the propmodel's prop */
			rhythmdb_property_model_delete_prop (propmodel, g_value_get_string (old));
			prop = rhythmdb_property_model_insert (propmodel, entry);
			rhythmdb_property_model_sync (propmodel);
		} else {
			int pi;
			const char *propstr;
			GtkTreeIter iter;
			GSequenceIter *ptr;

			/* check if the updated property is one of the propmodel's sort props */
			for (pi = 0; pi < propmodel->priv->sort_propids->len; pi++) {
				if (propid == g_array_index (propmodel->priv->sort_propids, RhythmDBPropType, pi)) {
					propstr = rhythmdb_entry_get_string (entry, propmodel->priv->propid);
					ptr = g_hash_table_lookup (propmodel->priv->reverse_map, propstr);
					prop = g_sequence_get (ptr);
					iter.stamp = propmodel->priv->stamp;
					iter.user_data = ptr;

					/* check if the sort prop needs updated and the propmodel needs resorting */
					if (update_sort_string (propmodel, prop, entry)) {
						property_sort_changed (propmodel, ptr, &iter);
					} else if (pi == prop->sort_string_from) {
						rb_refstring_unref (prop->sort_string);
						prop->sort_string = rb_refstring_new (g_value_get_string (new));
						property_sort_changed (propmodel, ptr, &iter);
					}
					break;
				}
			}
		}
	}
}

static void
rhythmdb_property_model_entry_removed_cb (RhythmDBQueryModel *model,
					  RhythmDBEntry *entry,
					  RhythmDBPropertyModel *propmodel)
{
	if (g_hash_table_remove (propmodel->priv->entries, entry))
		return;

	rhythmdb_property_model_delete (propmodel, entry);
	rhythmdb_property_model_sync (propmodel);
}

static gint
rhythmdb_property_model_compare (RhythmDBPropertyModelEntry *a,
				 RhythmDBPropertyModelEntry *b,
				 RhythmDBPropertyModel *model)
{
	const char *a_str, *b_str;

	a_str = rb_refstring_get_sort_key (a->sort_string);
	b_str = rb_refstring_get_sort_key (b->sort_string);

	return strcmp (a_str, b_str);
}

/*
 * looks for an updated sort string in a new entry for the specified
 * property.  if the new entry provides a value for a higher priority
 * sort property, updates the property to use the new sort string and
 * returns TRUE.  otherwise, returns FALSE.
 */
static gboolean
update_sort_string (RhythmDBPropertyModel *model,
		    RhythmDBPropertyModelEntry *prop,
		    RhythmDBEntry *entry)
{
	const char *newvalue = NULL;
	int pi;
	int upto;

	/* if the property that gave us the current sort string is being cleared,
	 * forget about the current string.
	 */
	if (prop->sort_string != NULL) {
		RhythmDBPropType propid;
		const char *newvalue;

		propid = g_array_index (model->priv->sort_propids, RhythmDBPropType, prop->sort_string_from);
		newvalue = rhythmdb_entry_get_string (entry, propid);
		if (newvalue == NULL || newvalue[0] == '\0') {
			rb_debug ("current sort string %s is being removed", rb_refstring_get (prop->sort_string));
			rb_refstring_unref (prop->sort_string);
			prop->sort_string = NULL;
		}
	}

	/* we only need to check properties that are preferred to the one that
	 * gave us the current sort string.
	 */
	upto = model->priv->sort_propids->len;
	if (prop->sort_string != NULL) {
		upto = prop->sort_string_from;
	}

	/* search for a non-empty sort string value */
	for (pi = 0; pi < upto; pi++) {
		RhythmDBPropType propid;

		propid = g_array_index (model->priv->sort_propids, RhythmDBPropType, pi);
		newvalue = rhythmdb_entry_get_string (entry, propid);
		if (newvalue != NULL && newvalue[0] != '\0') {
			break;
		}
	}

	/* if we found one, replace the current sort string */
	if (newvalue != NULL && newvalue[0] != '\0' && (prop->sort_string == NULL || pi < prop->sort_string_from)) {
		rb_debug ("replacing current sort string %s with %s (%d -> %d)",
			  prop->sort_string ? rb_refstring_get (prop->sort_string) : "NULL",
			  newvalue,
			  prop->sort_string_from,
			  pi);
		if (prop->sort_string != NULL) {
			rb_refstring_unref (prop->sort_string);
		}
		prop->sort_string = rb_refstring_new (newvalue);
		g_assert (pi < model->priv->sort_propids->len);
		prop->sort_string_from = pi;
		return TRUE;
	}

	/* if we can't find a sort string at all, use the display string as fallback */
	if (prop->sort_string == NULL) {
		prop->sort_string = rb_refstring_ref (prop->string);
	}

	return FALSE;
}

/*
 * to be called when a property's position in the sorted order may have changed.
 * emits row-deleted, resorts the property sequence, and emits row-inserted.
 */
static void
property_sort_changed (RhythmDBPropertyModel *model, GSequenceIter *ptr, GtkTreeIter *iter)
{
	GtkTreePath *path;

	path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), iter);
	gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
	gtk_tree_path_free (path);

	g_sequence_sort_changed (ptr,
				 (GCompareDataFunc) rhythmdb_property_model_compare,
				 model);

	path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), iter);
	gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, iter);
	gtk_tree_path_free (path);
}

static RhythmDBPropertyModelEntry *
rhythmdb_property_model_insert (RhythmDBPropertyModel *model,
				RhythmDBEntry *entry)
{
	RhythmDBPropertyModelEntry *prop;
	GtkTreeIter iter;
	GtkTreePath *path;
	GSequenceIter *ptr;
	const char *propstr;

	iter.stamp = model->priv->stamp;
	propstr = rhythmdb_entry_get_string (entry, model->priv->propid);

	g_atomic_int_inc (&model->priv->all->refcount);

	if ((ptr = g_hash_table_lookup (model->priv->reverse_map, propstr))) {
		iter.user_data = ptr;
		prop = g_sequence_get (ptr);
		g_atomic_int_inc (&prop->refcount);
		rb_debug ("adding \"%s\": refcount %d", propstr, prop->refcount);

		if (update_sort_string (model, prop, entry)) {
			property_sort_changed (model, ptr, &iter);
		}

		path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
		gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
		gtk_tree_path_free (path);

		return prop;
	}
	rb_debug ("adding new property \"%s\"", propstr);

	prop = g_new0 (RhythmDBPropertyModelEntry, 1);
	prop->string = rb_refstring_new (propstr);
	update_sort_string (model, prop, entry);
	g_atomic_int_set (&prop->refcount, 1);

	ptr = g_sequence_insert_sorted (model->priv->properties, prop,
					(GCompareDataFunc) rhythmdb_property_model_compare,
					model);
	g_hash_table_insert (model->priv->reverse_map,
			     (gpointer)rb_refstring_get (prop->string),
			     ptr);

	iter.user_data = ptr;
	path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
	gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);

	return prop;
}

static void
rhythmdb_property_model_delete (RhythmDBPropertyModel *model,
				RhythmDBEntry *entry)
{
	const char *propstr;

	if (g_hash_table_lookup (model->priv->entries, entry))
		return;

	propstr = rhythmdb_entry_get_string (entry, model->priv->propid);
	rhythmdb_property_model_delete_prop (model, propstr);
}

static void
rhythmdb_property_model_delete_prop (RhythmDBPropertyModel *model,
				     const char *propstr)
{
	GSequenceIter *ptr;
	RhythmDBPropertyModelEntry *prop;
	GtkTreePath *path;
	GtkTreeIter iter;
	gboolean ret;

	g_assert ((ptr = g_hash_table_lookup (model->priv->reverse_map, propstr)));

	iter.stamp = model->priv->stamp;
	iter.user_data = ptr;

	ret = g_atomic_int_dec_and_test (&model->priv->all->refcount);

	prop = g_sequence_get (ptr);
	rb_debug ("deleting \"%s\": refcount: %d", propstr, prop->refcount);
	if (g_atomic_int_dec_and_test (&prop->refcount) == FALSE) {
		g_assert (ret == FALSE);
		path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
		gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
		gtk_tree_path_free (path);
		return;
	}

	path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
	g_signal_emit (G_OBJECT (model), rhythmdb_property_model_signals[PRE_ROW_DELETION], 0);
	g_sequence_remove (ptr);
	g_hash_table_remove (model->priv->reverse_map, propstr);
	prop->refcount = 0xdeadbeef;
	rb_refstring_unref (prop->string);
	rb_refstring_unref (prop->sort_string);

	gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
	gtk_tree_path_free (path);

	g_free (prop);
}

/**
 * rhythmdb_property_model_iter_from_string:
 * @model: the #RhythmDBPropertyModel
 * @name: the property value to find
 * @iter: a #GtkTreeIter to point to the row
 *
 * Locates the row in the model for a property value.
 *
 * Return value: TRUE if the value was found.
 */
gboolean
rhythmdb_property_model_iter_from_string (RhythmDBPropertyModel *model,
					  const char *name,
					  GtkTreeIter *iter)
{
	GSequenceIter *ptr;

	if (name == NULL) {
		if (iter) {
			iter->stamp = model->priv->stamp;
			iter->user_data = model->priv->all;
		}
		return TRUE;
	}

	ptr = g_hash_table_lookup (model->priv->reverse_map, name);
	if (!ptr)
		return FALSE;

	if (iter) {
		iter->stamp = model->priv->stamp;
		iter->user_data = ptr;
	}

	return TRUE;
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
rhythmdb_property_model_get_column_type (GtkTreeModel *tree_model,
					 int index)
{
	switch (index) {
	case RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE:
		return G_TYPE_STRING;
	case RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY:
		return G_TYPE_BOOLEAN;
	case RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER:
		return G_TYPE_UINT;
	default:
		g_assert_not_reached ();
		return G_TYPE_INVALID;
	}
}

static gboolean
rhythmdb_property_model_get_iter (GtkTreeModel *tree_model,
				  GtkTreeIter *iter,
				  GtkTreePath *path)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	guint index;
	GSequenceIter *ptr;

	index = gtk_tree_path_get_indices (path)[0];

	if (index == 0) {
		iter->stamp = model->priv->stamp;
		iter->user_data = model->priv->all;
		return TRUE;
	}

	index--;
	if (index >= g_sequence_get_length (model->priv->properties))
		return FALSE;

	ptr = g_sequence_get_iter_at_pos (model->priv->properties, index);

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

	if (iter->user_data == model->priv->all) {
		return gtk_tree_path_new_first ();
	}

	if (g_sequence_iter_is_end (iter->user_data))
		return NULL;

	path = gtk_tree_path_new ();
	if (iter->user_data == model->priv->all)
		gtk_tree_path_append_index (path, 0);
	else
		gtk_tree_path_append_index (path, g_sequence_iter_get_position (iter->user_data) + 1);
	return path;
}

static void
rhythmdb_property_model_get_value (GtkTreeModel *tree_model,
				   GtkTreeIter *iter,
				   gint column,
				   GValue *value)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);

	g_return_if_fail (model->priv->stamp == iter->stamp);

	if (iter->user_data == model->priv->all) {
		switch (column) {
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, rb_refstring_get (model->priv->all->string));
			break;
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY:
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, TRUE);
			break;
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER:
			g_value_init (value, G_TYPE_UINT);
			g_value_set_uint (value, g_atomic_int_get (&model->priv->all->refcount));
			break;
		default:
			g_assert_not_reached ();
		}
	} else {
		RhythmDBPropertyModelEntry *prop;

		g_return_if_fail (!g_sequence_iter_is_end (iter->user_data));

		prop = g_sequence_get (iter->user_data);
		switch (column) {
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, rb_refstring_get (prop->string));
			break;
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY:
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, prop == model->priv->all);
			break;
		case RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER:
			g_value_init (value, G_TYPE_UINT);
			g_value_set_uint (value, g_atomic_int_get (&prop->refcount));
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

	if (iter->user_data == model->priv->all) {
		iter->user_data = g_sequence_get_begin_iter (model->priv->properties);
	} else {
		g_return_val_if_fail (!g_sequence_iter_is_end (iter->user_data), FALSE);
		iter->user_data = g_sequence_iter_next (iter->user_data);
	}

	return !g_sequence_iter_is_end (iter->user_data);
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
					GtkTreeIter *iter,
					GtkTreeIter *parent,
					gint n)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (tree_model);
	GSequenceIter *child;

	if (parent)
		return FALSE;

	if (n != 0) {
		/* -1 to account for the 'all' property at position 0 */
		child = g_sequence_get_iter_at_pos (model->priv->properties, n-1);

		if (g_sequence_iter_is_end (child))
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

static gboolean
rhythmdb_property_model_row_draggable (RbTreeDragSource *dragsource,
				       GList *paths)
{
	return TRUE;
}

static gboolean
rhythmdb_property_model_drag_data_delete (RbTreeDragSource *dragsource,
					  GList *paths)
{
	/* not supported */
	return TRUE;
}

/*Going through hoops to avoid nested functions*/
struct QueryModelCbStruct {
	RhythmDB *db;
	GString *reply;
	gint target;
};

static gboolean
query_model_cb (GtkTreeModel *query_model,
 		GtkTreePath *path,
		GtkTreeIter *iter,
 		struct QueryModelCbStruct *data)
{
 	RhythmDBEntry *entry;

 	gtk_tree_model_get (query_model, iter, 0, &entry, -1);
	if (data->target == TARGET_ENTRIES) {
		g_string_append_printf (data->reply,
					"%ld",
					rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID));
	} else if (data->target == TARGET_URIS) {
		const char *uri;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		g_string_append (data->reply, uri);
	}
 	g_string_append (data->reply, "\r\n");

 	rhythmdb_entry_unref (entry);
 	return FALSE;
}

static gboolean
rhythmdb_property_model_drag_data_get (RbTreeDragSource *dragsource,
				       GList *paths,
				       GtkSelectionData *selection_data)
{
	RhythmDBPropertyModel *model = RHYTHMDB_PROPERTY_MODEL (dragsource);
	guint target;
	GtkTargetList *drag_target_list;
	GdkAtom selection_data_target;

	switch (model->priv->propid) {
	case RHYTHMDB_PROP_GENRE:
		drag_target_list = rhythmdb_property_model_genre_drag_target_list;
		break;
	case RHYTHMDB_PROP_ALBUM:
		drag_target_list = rhythmdb_property_model_album_drag_target_list;
		break;
	case RHYTHMDB_PROP_ARTIST:
		drag_target_list = rhythmdb_property_model_artist_drag_target_list;
		break;
	case RHYTHMDB_PROP_LOCATION:
		drag_target_list = rhythmdb_property_model_location_drag_target_list;
		break;
	case RHYTHMDB_PROP_COMPOSER:
		drag_target_list = rhythmdb_property_model_composer_drag_target_list;
		break;
	default:
		g_assert_not_reached ();
	}

	selection_data_target = gtk_selection_data_get_target (selection_data);
	if (!gtk_target_list_find (drag_target_list,
				   selection_data_target,
				   &target)) {
		return FALSE;
	}

	if (target == TARGET_URIS || target == TARGET_ENTRIES) {
		RhythmDB *db = model->priv->db;
 		RhythmDBQueryModel *query_model;
 		GString* reply = g_string_new ("");
 		GtkTreeIter iter;
 		gboolean is_all = FALSE;
 		struct QueryModelCbStruct tmp;
		GtkTreePath *path;
		GCompareDataFunc sort_func = NULL;
		gpointer sort_data;
		gboolean sort_reverse;

		query_model = rhythmdb_query_model_new_empty (db);
		/* FIXME the sort order on the query model at this point is usually
		 * not the user's selected sort order.
		 */
		g_object_get (G_OBJECT (model->priv->query_model),
			      "sort-func", &sort_func,
			      "sort-data", &sort_data,
			      "sort-reverse", &sort_reverse,
			      NULL);
		rhythmdb_query_model_set_sort_order (RHYTHMDB_QUERY_MODEL (query_model),
						     sort_func, GUINT_TO_POINTER (sort_data), NULL, sort_reverse);

		rb_debug ("getting drag data as uri list");
		/* check if first selected row is 'All' */
		path = gtk_tree_row_reference_get_path (paths->data);
		if (path && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path))
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY,
					    &is_all, -1);
		gtk_tree_path_free (path);
		if (is_all) {
			g_object_set (query_model,
				      "base-model", model->priv->query_model,
				      NULL);
		} else {
 			GList *row;
			GPtrArray *subquery = g_ptr_array_new ();

 			for (row = paths; row; row = row->next) {
				path = gtk_tree_row_reference_get_path (row->data);
 				if (path && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path)) {
					char *name;
	 				gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
	 						    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE,
							    &name, -1);
	 				if (row == paths) {
	 					rhythmdb_query_append (db, subquery,
	 							       RHYTHMDB_QUERY_PROP_EQUALS,
	 							       model->priv->propid, name,
								       RHYTHMDB_QUERY_END);
					} else {
	 					rhythmdb_query_append (db, subquery,
	 							       RHYTHMDB_QUERY_DISJUNCTION,
	 							       RHYTHMDB_QUERY_PROP_EQUALS,
	 							       model->priv->propid, name,
	 							       RHYTHMDB_QUERY_END);
	 				}
					g_free (name);
				}

				gtk_tree_path_free (path);
 			}

			g_object_set (query_model,
				      "query", subquery,
				      "base-model", model->priv->query_model,
				      NULL);
			rhythmdb_query_free (subquery);
		}

		tmp.db = db;
 		tmp.reply = reply;
		tmp.target = target;
 		/* Too bad that we're on the main thread. Why doesn't gtk call us async?
 		 * How does file-roller manage? - it seems it refuses the drop when it isn't
		 * done unpacking. In which case, we should tweak the drop acknowledgement,
		 * and prepare the query using do_full_query_async. The query would be
		 * hooked to the drag context.
		 */
 		gtk_tree_model_foreach (GTK_TREE_MODEL (query_model),
 					(GtkTreeModelForeachFunc) query_model_cb,
 					&tmp);

		g_object_unref (query_model);

 		gtk_selection_data_set (selection_data,
					selection_data_target,
 		                        8, (guchar *)reply->str,
 		                        reply->len);
 		g_string_free (reply, TRUE);

	} else {
		char* title;
		GList *p;
		GString* reply = g_string_new ("");

		rb_debug ("getting drag data as list of property values");

		for (p = paths; p; p = p->next) {
			GtkTreeIter iter;
			GtkTreePath *path;

			path = gtk_tree_row_reference_get_path (p->data);
			if (path && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path)) {
				gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
						    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &title, -1);
				g_string_append (reply, title);
				if (p->next)
					g_string_append (reply, "\r\n");
				g_free (title);
			}
			gtk_tree_path_free (path);
		}
		gtk_selection_data_set (selection_data,
					selection_data_target,
					8, (guchar *)reply->str,
					reply->len);
		g_string_free (reply, TRUE);
	}

	return TRUE;
}

/**
 * rhythmdb_property_model_enable_drag:
 * @model: the #RhythmDBPropertyModel.
 * @view: the #GtkTreeView from which to enable drag and drop
 *
 * Enables drag and drop from a specified #GtkTreeView that is
 * backed by the #RhythmDBPropertyModel.  Drag targets are
 * determined by the indexed property.
 */
void
rhythmdb_property_model_enable_drag (RhythmDBPropertyModel *model,
				     GtkTreeView *view)
{
	const GtkTargetEntry *targets;
	gint n_elements;

	switch (model->priv->propid) {
	case RHYTHMDB_PROP_GENRE:
		targets = targets_genre;
		n_elements = G_N_ELEMENTS (targets_genre);
		break;
	case RHYTHMDB_PROP_ALBUM:
		targets = targets_album;
		n_elements = G_N_ELEMENTS (targets_album);
		break;
	case RHYTHMDB_PROP_ARTIST:
		targets = targets_artist;
		n_elements = G_N_ELEMENTS (targets_artist);
		break;
	case RHYTHMDB_PROP_LOCATION:
	case RHYTHMDB_PROP_SUBTITLE:		/* more or less */
		targets = targets_location;
		n_elements = G_N_ELEMENTS (targets_location);
		break;
	case RHYTHMDB_PROP_COMPOSER:
		targets = targets_composer;
		n_elements = G_N_ELEMENTS (targets_composer);
		break;
	default:
		g_assert_not_reached ();
	}

	rb_tree_dnd_add_drag_source_support (view,
					     GDK_BUTTON1_MASK,
					     targets, n_elements,
					     GDK_ACTION_COPY);
}

static gboolean
rhythmdb_property_model_perform_sync (RhythmDBPropertyModel *model)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	iter.stamp = model->priv->stamp;
	iter.user_data = model->priv->all;
	path = rhythmdb_property_model_get_path (GTK_TREE_MODEL (model), &iter);
	gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
	gtk_tree_path_free (path);

	model->priv->syncing_id = 0;
	return FALSE;
}

static void
rhythmdb_property_model_sync (RhythmDBPropertyModel *model)
{
	if (model->priv->syncing_id != 0)
		return;

	model->priv->syncing_id = g_idle_add ((GSourceFunc)rhythmdb_property_model_perform_sync, model);
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rhythmdb_property_model_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, "property-title"),
			ENUM_ENTRY (RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, "value-priority"),
			ENUM_ENTRY (RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, "track-count"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RhythmDBPropertyModelColumn", values);
	}

	return etype;
}
