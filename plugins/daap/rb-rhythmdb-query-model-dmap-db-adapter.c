/*
 *  Database adapter class for DMAP sharing
 *
 *  Copyright (C) 2008 W. Michael Petullo <mike@flyn.org>
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

#include "rhythmdb-query-model.h"
#include "rb-rhythmdb-query-model-dmap-db-adapter.h"
#include "rb-daap-record.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libdmapsharing/dmap.h>

struct RBRhythmDBQueryModelDMAPDbAdapterPrivate {
	RhythmDBQueryModel *model;
};

typedef struct ForeachAdapterData {
	gpointer data;
	DmapIdRecordFunc func;
} ForeachAdapterData;

static DmapRecord *
rb_rhythmdb_query_model_dmap_db_adapter_lookup_by_id (const DmapDb *db,
						      guint id)
{
	g_error ("Not implemented");
	return NULL;
}

static gboolean
foreach_adapter (GtkTreeModel *model,
		 GtkTreePath *path,
		 GtkTreeIter *iter,
		 gpointer data)
{
	gulong id;
	DmapRecord *record;
	RhythmDBEntry *entry;
	ForeachAdapterData *foreach_adapter_data;

	gtk_tree_model_get (model, iter, 0, &entry, -1);
	
	id = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID);
	foreach_adapter_data = data;
	record = DMAP_RECORD (rb_daap_record_new (entry));

	foreach_adapter_data->func (id,
				    record,
				    foreach_adapter_data->data);

	g_object_unref (record);
	rhythmdb_entry_unref (entry);

	return FALSE;
}

static void
rb_rhythmdb_query_model_dmap_db_adapter_foreach	(const DmapDb *db,
                                                 DmapIdRecordFunc func,
                                                 gpointer data)
{
	ForeachAdapterData *foreach_adapter_data;

	g_assert (RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER (db)->priv->model != NULL);

	foreach_adapter_data = g_new (ForeachAdapterData, 1);
	foreach_adapter_data->data = data;
	foreach_adapter_data->func = func;

	gtk_tree_model_foreach (GTK_TREE_MODEL (RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER (db)->priv->model),
			       (GtkTreeModelForeachFunc) foreach_adapter,
				foreach_adapter_data);

	g_free (foreach_adapter_data);
}

static gint64
rb_rhythmdb_query_model_dmap_db_adapter_count (const DmapDb *db)
{
	g_assert (RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER (db)->priv->model != NULL); 
	return gtk_tree_model_iter_n_children (
		GTK_TREE_MODEL (RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER (db)->priv->model), NULL);
}

static guint
rb_rhythmdb_query_model_dmap_db_adapter_add (DmapDb *db, DmapRecord *record, GError **error)
{
	g_error ("Not implemented");
	return 0;
}

static void
rb_rhythmdb_query_model_dmap_db_adapter_init (RBRhythmDBQueryModelDMAPDbAdapter *db)
{
	db->priv = RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER_GET_PRIVATE (db);
}

static void
rb_rhythmdb_query_model_dmap_db_adapter_class_init (RBRhythmDBQueryModelDMAPDbAdapterClass *klass)
{
	g_type_class_add_private (klass, sizeof (RBRhythmDBQueryModelDMAPDbAdapterPrivate));
}

static void
rb_rhythmdb_query_model_dmap_db_adapter_class_finalize (RBRhythmDBQueryModelDMAPDbAdapterClass *klass)
{
}

static void
rb_rhythmdb_query_model_dmap_db_adapter_interface_init (gpointer iface, gpointer data)
{
	DmapDbInterface *dmap_db = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dmap_db) == DMAP_TYPE_DB);

	dmap_db->add = rb_rhythmdb_query_model_dmap_db_adapter_add;
	dmap_db->lookup_by_id = rb_rhythmdb_query_model_dmap_db_adapter_lookup_by_id;
	dmap_db->foreach = rb_rhythmdb_query_model_dmap_db_adapter_foreach;
	dmap_db->count = rb_rhythmdb_query_model_dmap_db_adapter_count;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBRhythmDBQueryModelDMAPDbAdapter,
				rb_rhythmdb_query_model_dmap_db_adapter,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_DB,
							       rb_rhythmdb_query_model_dmap_db_adapter_interface_init))

RBRhythmDBQueryModelDMAPDbAdapter *
rb_rhythmdb_query_model_dmap_db_adapter_new (RhythmDBQueryModel *model)
{
	RBRhythmDBQueryModelDMAPDbAdapter *db;

	db = RB_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER (g_object_new (RB_TYPE_RHYTHMDB_QUERY_MODEL_DMAP_DB_ADAPTER,
					       NULL));

	db->priv->model = model;

	return db;
}

void
_rb_rhythmdb_query_model_dmap_db_adapter_register_type (GTypeModule *module)
{
	rb_rhythmdb_query_model_dmap_db_adapter_register_type (module);
}
