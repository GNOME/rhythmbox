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

#ifndef __RB_DAAP_COMPAT
#define __RB_DAAP_COMPAT

#include "rb-dmap-compat.h"

#ifdef LIBDMAPSHARING_COMPAT

/* Building against libdmapsharing 3 API. */

#define dmap_av_connection_new daap_connection_new
#define DmapAvRecord DAAPRecord
#define DMAP_AV_RECORD DAAP_RECORD
#define DmapAvRecordInterface DAAPRecordIface
#define DmapAvShare DAAPShare
#define dmap_av_share_new daap_share_new
#define DMAP_TYPE_AV_RECORD DAAP_TYPE_RECORD

DmapRecord *rb_daap_record_factory_create (DmapRecordFactory *factory, gpointer user_data, GError **error);

void rb_daap_container_record_add_entry(DmapContainerRecord *container_record,
                                        DmapRecord *record,
                                        gint id,
                                        GError **error);

static inline DmapRecord *
rb_daap_record_factory_create_compat (DmapRecordFactory *factory, gpointer user_data)
{
	return rb_daap_record_factory_create (factory, user_data, NULL);
}

static inline void
rb_daap_container_record_add_entry_compat(DmapContainerRecord *container_record,
                                          DmapRecord *record,
                                          gint id)
{
	rb_daap_container_record_add_entry(container_record, record, id, NULL);
}

#else

/* Building against libdmapsharing 4 API. */

void rb_daap_container_record_add_entry(DmapContainerRecord *container_record,
                                        DmapRecord *record,
                                        gint id,
                                        GError **error);

DmapRecord *rb_daap_record_factory_create (DmapRecordFactory *factory, gpointer user_data, GError **error);

static inline DmapRecord *
rb_daap_record_factory_create_compat (DmapRecordFactory *factory, gpointer user_data, GError **error)
{
	return rb_daap_record_factory_create (factory, user_data, error);
}

static inline void
rb_daap_container_record_add_entry_compat(DmapContainerRecord *container_record,
                                          DmapRecord *record,
                                          gint id,
                                          GError **error)
{
	rb_daap_container_record_add_entry(container_record, record, id, error);
}

#endif

#endif /* __RB_DAAP_COMPAT */
