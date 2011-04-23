/*
 *  Copyright (C) 2007 Christophe Fergeau  <teuf@gnome.org>
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
#ifndef __RB_IPOD_DB_H
#define __RB_IPOD_DB_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gpod/itdb.h>


G_BEGIN_DECLS

#define RB_TYPE_IPOD_DB         (rb_ipod_db_get_type ())
#define RB_IPOD_DB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_DB, RbIpodDb))
#define RB_IPOD_DB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_DB, RbIpodDbClass))
#define RB_IS_IPOD_DB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_DB))
#define RB_IS_IPOD_DB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_DB))
#define RB_IPOD_DB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_DB, RbIpodDbClass))

typedef struct 
{
	GObject parent;
} RbIpodDb;

typedef struct
{
	GObjectClass parent;
} RbIpodDbClass;

RbIpodDb *rb_ipod_db_new (GMount *mount);
GType rb_ipod_db_get_type (void);
void _rb_ipod_db_register_type (GTypeModule *module);

void rb_ipod_db_save_async (RbIpodDb *db);

void rb_ipod_db_set_thumbnail (RbIpodDb* ipod_db, Itdb_Track *track, 
			       GdkPixbuf *pixbuf);
void rb_ipod_db_add_track (RbIpodDb* ipod_db, Itdb_Track *track);
void rb_ipod_db_add_playlist (RbIpodDb* ipod_db, Itdb_Playlist *playlist);
void rb_ipod_db_remove_playlist (RbIpodDb* ipod_db, Itdb_Playlist *playlist);
void rb_ipod_db_rename_playlist (RbIpodDb* ipod_db, Itdb_Playlist *playlist, const char *name);
void rb_ipod_db_add_to_playlist (RbIpodDb* ipod_db, Itdb_Playlist *playlist,
				 Itdb_Track *track);
void rb_ipod_db_remove_from_playlist (RbIpodDb* ipod_db, 
				      Itdb_Playlist *playlist,
				      Itdb_Track *track);
void rb_ipod_db_remove_track (RbIpodDb* ipod_db, Itdb_Track *track);
void rb_ipod_db_set_ipod_name (RbIpodDb *db, const char *name);
const char *rb_ipod_db_get_ipod_name (RbIpodDb *db);

GList *rb_ipod_db_get_playlists (RbIpodDb *ipod_db);
Itdb_Playlist *rb_ipod_db_get_playlist_by_name (RbIpodDb *ipod_db, gchar *name);
GList *rb_ipod_db_get_tracks (RbIpodDb *ipod_db);
const char *rb_ipod_db_get_mount_path (RbIpodDb *ipod_db);
Itdb_Device *rb_ipod_db_get_device (RbIpodDb *ipod_db);
guint32 rb_ipod_db_get_database_version (RbIpodDb *ipod_db);
#endif
