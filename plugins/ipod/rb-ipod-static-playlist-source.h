/*
 *  Copyright (C) 2007 James Livingston <doclivingston@gmail.co>
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

#ifndef __RB_IPOD_STATIC_PLAYLIST_SOURCE_H
#define __RB_IPOD_STATIC_PLAYLIST_SOURCE_H

#include <gpod/itdb.h>

#include "rb-static-playlist-source.h"
#include "rb-ipod-source.h"
#include "rb-ipod-db.h"

G_BEGIN_DECLS

#define RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE         (rb_ipod_static_playlist_source_get_type ())
#define RB_IPOD_STATIC_PLAYLIST_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE, RBIpodStaticPlaylistSource))
#define RB_IPOD_STATIC_PLAYLIST_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE, RBIpodStaticPlaylistSourceClass))
#define RB_IS_IPOD_STATIC_PLAYLIST_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE))
#define RB_IS_IPOD_STATIC_PLAYLIST_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE))
#define RB_IPOD_STATIC_PLAYLIST_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_STATIC_PLAYLIST_SOURCE, RBIpodStaticPlaylistSourceClass))

typedef struct
{
	RBStaticPlaylistSource parent;
} RBIpodStaticPlaylistSource;

typedef struct
{
	RBStaticPlaylistSourceClass parent;
} RBIpodStaticPlaylistSourceClass;

GType		rb_ipod_static_playlist_source_get_type 	(void);
void		_rb_ipod_static_playlist_source_register_type   (GTypeModule *module);

RBIpodStaticPlaylistSource *	rb_ipod_static_playlist_source_new (RBShell *shell,
								    RBiPodSource *source,
								    RbIpodDb *ipod_db,
								    Itdb_Playlist *playlist,
								    RhythmDBEntryType *entry_type,
								    GMenuModel *playlist_menu);

G_END_DECLS

#endif /* __RB_IPOD_STATIC_PLAYLIST_SOURCE_H */
