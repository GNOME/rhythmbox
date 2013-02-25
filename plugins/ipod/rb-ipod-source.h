/*
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
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

#ifndef __RB_IPOD_SOURCE_H
#define __RB_IPOD_SOURCE_H

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rhythmdb.h"
#include "mediaplayerid.h"
#include "rb-ipod-db.h"

G_BEGIN_DECLS

#define RB_TYPE_IPOD_SOURCE         (rb_ipod_source_get_type ())
#define RB_IPOD_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_SOURCE, RBiPodSource))
#define RB_IPOD_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))
#define RB_IS_IPOD_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_SOURCE))
#define RB_IS_IPOD_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_SOURCE))
#define RB_IPOD_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))

typedef struct
{
	RBMediaPlayerSource parent;
} RBiPodSource;

typedef struct
{
	RBMediaPlayerSourceClass parent;
} RBiPodSourceClass;

RBMediaPlayerSource	*rb_ipod_source_new		(GObject *plugin,
							 RBShell *shell,
							 GMount *mount,
							 MPIDDevice *device_info);
GType			rb_ipod_source_get_type		(void);
void                    _rb_ipod_source_register_type   (GTypeModule *module);

void			rb_ipod_source_remove_playlist	(RBiPodSource *ipod_source,
							 RBSource *source);

Itdb_Playlist *		rb_ipod_source_get_playlist	(RBiPodSource *source,
							 gchar *name);
void			rb_ipod_source_show_properties	(RBiPodSource *source);

void			rb_ipod_source_delete_entries	(RBiPodSource *source,
							 GList *entries);

Itdb_Track *		rb_ipod_source_lookup_track	(RBiPodSource *source,
							 RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RB_IPOD_SOURCE_H */
