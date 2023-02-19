/*
 *  Container / playlist database record class for DAAP sharing
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

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include "rhythmdb-query-model.h"
#include "rb-daap-container-record.h"
#include "rb-rhythmdb-query-model-dmap-db-adapter.h"

enum {
        PROP_0,
	PROP_NAME
};

struct RBDAAPContainerRecordPrivate {
	char *name;
	RBPlaylistSource *source;
};

static void rb_daap_container_record_finalize (GObject *object);

static void
rb_daap_container_record_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBDAAPContainerRecord *record = RB_DAAP_CONTAINER_RECORD (object);

	switch (prop_id) {
		case PROP_NAME:
			g_free (record->priv->name);
			record->priv->name = g_value_dup_string (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_container_record_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBDAAPContainerRecord *record = RB_DAAP_CONTAINER_RECORD (object);

	switch (prop_id) {
		case PROP_NAME:
			g_value_set_string (value, record->priv->name);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

guint
rb_daap_container_record_get_id (DmapContainerRecord *record)
{
	return GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (RB_DAAP_CONTAINER_RECORD (record)->priv->source), "daap_id"));
}

void
rb_daap_container_record_add_entry (DmapContainerRecord *container_record,
                                    DmapRecord *record, gint id, GError **error)
{
	g_error ("Unimplemented");
}

guint64
rb_daap_container_record_get_entry_count (DmapContainerRecord *record)
{
	RhythmDBQueryModel *model;
	guint64 count;
	g_object_get (RB_DAAP_CONTAINER_RECORD (record)->priv->source,
		     "base-query-model",
		     &model,
		      NULL);
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (model);
	return count;
}

DmapDb *
rb_daap_container_record_get_entries (DmapContainerRecord *record)
{
	RhythmDBQueryModel *model;
	g_object_get (RB_DAAP_CONTAINER_RECORD (record)->priv->source,
		     "base-query-model",
		     &model,
		      NULL);
	return DMAP_DB (rb_rhythmdb_query_model_dmap_db_adapter_new (model));
}

static void
rb_daap_container_record_init (RBDAAPContainerRecord *record)
{
	record->priv = RB_DAAP_CONTAINER_RECORD_GET_PRIVATE (record);
}

static void
rb_daap_container_record_class_init (RBDAAPContainerRecordClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBDAAPContainerRecordPrivate));

	gobject_class->set_property = rb_daap_container_record_set_property;
	gobject_class->get_property = rb_daap_container_record_get_property;
	gobject_class->finalize     = rb_daap_container_record_finalize;

	g_object_class_override_property (gobject_class, PROP_NAME, "name");
}

static void
rb_daap_container_record_class_finalize (RBDAAPContainerRecordClass *klass)
{
}

static void
rb_daap_container_record_daap_iface_init (gpointer iface, gpointer data)
{
	DmapContainerRecordInterface *dmap_container_record = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dmap_container_record) == DMAP_TYPE_CONTAINER_RECORD);

	dmap_container_record->get_id = rb_daap_container_record_get_id;
	dmap_container_record->add_entry = rb_daap_container_record_add_entry;
	dmap_container_record->get_entry_count = rb_daap_container_record_get_entry_count;
	dmap_container_record->get_entries = rb_daap_container_record_get_entries;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBDAAPContainerRecord,
				rb_daap_container_record,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_CONTAINER_RECORD,
							       rb_daap_container_record_daap_iface_init))

static void
rb_daap_container_record_finalize (GObject *object)
{
	RBDAAPContainerRecord *record = RB_DAAP_CONTAINER_RECORD (object);

        g_free (record->priv->name);

	G_OBJECT_CLASS (rb_daap_container_record_parent_class)->finalize (object);
}

RBDAAPContainerRecord *
rb_daap_container_record_new (char *name, RBPlaylistSource *source)
{
	RBDAAPContainerRecord *record;

	record = g_object_new (RB_TYPE_DAAP_CONTAINER_RECORD, NULL);

	record->priv->source = source;
	record->priv->name = name;

	return record;
}

void
_rb_daap_container_record_register_type (GTypeModule *module)
{
	rb_daap_container_record_register_type (module);
}
