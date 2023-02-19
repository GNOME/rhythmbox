/*
 *  Container / Playlist database adapter class for DMAP sharing
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

#include "rb-playlist-manager.h"
#include "rb-playlist-source.h"
#include "rb-dmap-container-db-adapter.h"
#include "rb-daap-container-record.h"

#include <libdmapsharing/dmap.h>

static guint next_playlist_id = 2;

struct RBDMAPContainerDbAdapterPrivate {
	RBPlaylistManager *playlist_manager;
};

typedef struct ForeachAdapterData {
	gpointer data;
	DmapIdContainerRecordFunc func;
} ForeachAdapterData;

static guint find_by_id (gconstpointer a, gconstpointer b)
{
	return GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (a), "daap_id")) != GPOINTER_TO_UINT (b);
}

static DmapContainerRecord *
rb_dmap_container_db_adapter_lookup_by_id (DmapContainerDb *db, guint id)
{
	gchar *name;
	GList *playlists;
	DmapContainerRecord *fnval = NULL;

	playlists = rb_playlist_manager_get_playlists (RB_DMAP_CONTAINER_DB_ADAPTER (db)->priv->playlist_manager);

	if (playlists != NULL && playlists->data != NULL) {
		GList *result;
		result = g_list_find_custom (playlists, GINT_TO_POINTER (id), (GCompareFunc) find_by_id);
		if (result != NULL && result->data != NULL) {
			RBPlaylistSource *source;
			source = RB_PLAYLIST_SOURCE (result->data);
			g_object_get (source, "name", &name, NULL);
			fnval = DMAP_CONTAINER_RECORD (rb_daap_container_record_new (name, source));
		}
	}

	g_list_free (playlists);

	return fnval;
}

static void
foreach_adapter (RBPlaylistSource *entry, gpointer data)
{
	guint id;
	gchar *name;
	DmapContainerRecord *record;
	ForeachAdapterData *foreach_adapter_data;

	foreach_adapter_data = data;
	g_object_get (entry, "name", &name, NULL);
	record = DMAP_CONTAINER_RECORD (rb_daap_container_record_new (name, entry));
	id = rb_daap_container_record_get_id (record);

	foreach_adapter_data->func (id,
				    record,
				    foreach_adapter_data->data);

	g_object_unref (record);
}

static void
rb_dmap_container_db_adapter_foreach	(DmapContainerDb *db,
					 DmapIdContainerRecordFunc func,
				         gpointer data)
{
	ForeachAdapterData *foreach_adapter_data;
	GList *playlists;

	playlists = rb_playlist_manager_get_playlists (RB_DMAP_CONTAINER_DB_ADAPTER (db)->priv->playlist_manager);

	foreach_adapter_data = g_new (ForeachAdapterData, 1);
	foreach_adapter_data->data = data;
	foreach_adapter_data->func = func;
	g_list_foreach (playlists, (GFunc) foreach_adapter, foreach_adapter_data);

	g_list_free (playlists);
	g_free (foreach_adapter_data);
}

static gint64
rb_dmap_container_db_adapter_count (DmapContainerDb *db)
{
	gint64 count = 0;
	GList *playlists = rb_playlist_manager_get_playlists (
		RB_DMAP_CONTAINER_DB_ADAPTER (db)->priv->playlist_manager);
	count = g_list_length (playlists);
	g_list_free (playlists);
	return count;
}

static void
rb_dmap_container_db_adapter_init (RBDMAPContainerDbAdapter *db)
{
	db->priv = RB_DMAP_CONTAINER_DB_ADAPTER_GET_PRIVATE (db);
}

static void
rb_dmap_container_db_adapter_class_init (RBDMAPContainerDbAdapterClass *klass)
{
	g_type_class_add_private (klass, sizeof (RBDMAPContainerDbAdapterPrivate));
}

static void
rb_dmap_container_db_adapter_class_finalize (RBDMAPContainerDbAdapterClass *klass)
{
}

static void
rb_dmap_container_db_adapter_interface_init (gpointer iface, gpointer data)
{
	DmapContainerDbInterface *dmap_db = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dmap_db) == DMAP_TYPE_CONTAINER_DB);

	dmap_db->lookup_by_id = rb_dmap_container_db_adapter_lookup_by_id;
	dmap_db->foreach = rb_dmap_container_db_adapter_foreach;
	dmap_db->count = rb_dmap_container_db_adapter_count;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBDMAPContainerDbAdapter,
				rb_dmap_container_db_adapter,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_CONTAINER_DB,
							       rb_dmap_container_db_adapter_interface_init))

static void
assign_id (RBPlaylistManager *mgr,
	   RBSource *source)
{
	if (g_object_get_data (G_OBJECT (source), "daap_id") == NULL)
		g_object_set_data (G_OBJECT (source), "daap_id", GUINT_TO_POINTER (next_playlist_id++));
}

RBDMAPContainerDbAdapter *
rb_dmap_container_db_adapter_new (RBPlaylistManager *playlist_manager)
{
	RBDMAPContainerDbAdapter *db;
	GList *playlists;
	
	playlists = rb_playlist_manager_get_playlists (playlist_manager);

	/* These IDs are DAAP-specific, so they are not a part of the
	 * general-purpose RBPlaylistSource class:
	 */
	if (playlists != NULL && playlists->data != NULL) {
		GList *l;
		for (l = playlists; l != NULL; l = l->next) {
			assign_id (playlist_manager, RB_SOURCE (l->data));
		}
	}
	
	g_signal_connect (G_OBJECT (playlist_manager),
			 "playlist_created",
			  G_CALLBACK (assign_id),
			  NULL);
	
	g_signal_connect (G_OBJECT (playlist_manager),
			 "playlist_added",
			  G_CALLBACK (assign_id),
			  NULL);

	db = RB_DMAP_CONTAINER_DB_ADAPTER (g_object_new (RB_TYPE_DMAP_CONTAINER_DB_ADAPTER,
					       NULL));

	db->priv->playlist_manager = playlist_manager;

	return db;
}

void
_rb_dmap_container_db_adapter_register_type (GTypeModule *module)
{
	rb_dmap_container_db_adapter_register_type (module);
}
