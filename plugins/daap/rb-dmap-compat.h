/*
 *  Compatibility layer between libdmapsharing 3.0 and 4.0 APIs
 *
 *  Copyright (C) 2020 W. Michael Petullo <mike@flyn.org>
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

#ifndef _RB_DMAP_COMPAT
#define _RB_DMAP_COMPAT

#ifdef LIBDMAPSHARING_COMPAT

/* Building against libdmapsharing 3 API. */

#define DmapConnection DMAPConnection
#define DmapConnectionFunc DMAPConnectionCallback
#define dmap_connection_start dmap_connection_connect
#define DmapConnectionState DMAPConnectionState
#define dmap_connection_stop dmap_connection_disconnect
#define DmapContainerDb DMAPContainerDb
#define DmapContainerDbInterface DMAPContainerDbIface
#define DmapContainerRecord DMAPContainerRecord
#define DmapContainerRecordInterface DMAPContainerRecordIface
#define DmapDb DMAPDb
#define DmapDbInterface DMAPDbIface
#define DMAP_GET_MEDIA DMAP_GET_SONGS
#define DmapIdContainerRecordFunc GHFunc
#define DmapIdRecordFunc GHFunc
#define DmapMdnsBrowser DMAPMdnsBrowser
#define DmapMdnsService DMAPMdnsBrowserService
#define DMAP_MDNS_SERVICE_TYPE_DAAP DMAP_MDNS_BROWSER_SERVICE_TYPE_DAAP
#define DmapPlaylist DMAPPlaylist
#define DmapRecord DMAPRecord
#define DmapRecordFactory DMAPRecordFactory
#define DmapRecordFactoryInterface DMAPRecordFactoryIface
#define DmapRecordInterface DMAPRecordIface

guint rb_rhythmdb_dmap_db_adapter_add (DMAPDb *_db, DMAPRecord *_record, GError **error);
guint rb_rhythmdb_query_model_dmap_db_adapter_add (DmapDb *db, DmapRecord *record, GError **error);

static inline guint
rb_rhythmdb_dmap_db_adapter_add_compat (DmapDb *_db, DmapRecord *_record)
{
	return rb_rhythmdb_dmap_db_adapter_add (_db, _record, NULL);
}

static inline guint
rb_rhythmdb_query_model_dmap_db_adapter_add_compat (DmapDb *db, DmapRecord *record)
{
	return rb_rhythmdb_query_model_dmap_db_adapter_add(db, record, NULL);
}

static inline DmapMdnsService *
rb_dmap_mdns_service_new_compat(const gchar *service_name,
                                const gchar *name,
                                gchar *host,
                                guint port,
                                gboolean password_protected)
{
	DmapMdnsService *service = g_new(DmapMdnsService, 1);

	service->service_name       = g_strdup(service_name);
	service->name               = g_strdup(name);
	service->host               = g_strdup(host);
        service->port               = port;
	service->password_protected = password_protected;

	return service;
}

static inline void
rb_dmap_mdns_service_get_compat(DmapMdnsService *service,
                                gchar **service_name,
                                gchar **name,
                                gchar **host,
                                guint *port,
                                gboolean *password_protected)
{
	*service_name       = g_strdup(service->service_name);
	*name               = g_strdup(service->name);
	*host               = g_strdup(service->host);
	*port               = service->port;
	*password_protected = service->password_protected;
}

static inline void
rb_dmap_mdns_service_free_compat(DmapMdnsService *service)
{
	g_free(service);
}

#else

/* Building against libdmapsharing 4 API. */

guint rb_rhythmdb_dmap_db_adapter_add (DmapDb *_db, DmapRecord *_record, GError **error);
guint rb_rhythmdb_query_model_dmap_db_adapter_add (DmapDb *db, DmapRecord *record, GError **error);

static inline guint
rb_rhythmdb_dmap_db_adapter_add_compat (DmapDb *_db, DmapRecord *_record, GError **error)
{
	return rb_rhythmdb_dmap_db_adapter_add (_db, _record, error);
}

static inline guint
rb_rhythmdb_query_model_dmap_db_adapter_add_compat (DmapDb *db, DmapRecord *record, GError **error)
{
	return rb_rhythmdb_query_model_dmap_db_adapter_add(db, record, error);
}

static inline DmapMdnsService *
rb_dmap_mdns_service_new_compat(const gchar *service_name,
                                const gchar *name,
                                gchar *host,
                                guint port,
                                gboolean password_protected)
{
	return g_object_new(DMAP_TYPE_MDNS_SERVICE,
	                   "service-name", service_name,
	                   "name", name,
                           "host", host,
                           "port", port,
                           "password-protected", FALSE,
                            NULL);
}

static inline void
rb_dmap_mdns_service_get_compat(DmapMdnsService *service,
                                gchar **service_name,
                                gchar **name,
                                gchar **host,
                                guint *port,
                                gboolean *password_protected)
{
	g_object_get(service, "service-name", service_name,
	                      "name", name,
	                      "host", host,
	                      "port", port,
	                      "password-protected", password_protected,
	                       NULL);
}

static inline void
rb_dmap_mdns_service_free_compat(DmapMdnsService *service)
{
	g_object_unref(service);
}

#endif

#endif /* _RB_DMAP_COMPAT */
