/*
 *  arch-tag: Header for generic audio player source object
 *
 *  Copyright (C) 2005 James Livingston <jrl@ids.org.au>
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

#ifndef __RB_GENERIC_PLAYER_SOURCE_H
#define __RB_GENERIC_PLAYER_SOURCE_H

#include "rb-shell.h"
#include "rb-removable-media-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_GENERIC_PLAYER_SOURCE         (rb_generic_player_source_get_type ())
#define RB_GENERIC_PLAYER_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSource))
#define RB_GENERIC_PLAYER_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourceClass))
#define RB_IS_GENERIC_PLAYER_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GENERIC_PLAYER_SOURCE))
#define RB_IS_GENERIC_PLAYER_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GENERIC_PLAYER_SOURCE))
#define RB_GENERIC_PLAYER_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourceClass))

typedef struct
{
	RBRemovableMediaSource parent;
} RBGenericPlayerSource;

typedef struct
{
	RBRemovableMediaSourceClass parent;

	char *		(*impl_get_mount_path) (RBGenericPlayerSource *source);
	void		(*impl_load_playlists) (RBGenericPlayerSource *source);

	/* used internally in the base load_playlist implementation*/
	char *		(*impl_transform_playlist_uri) (RBGenericPlayerSource *source, const char *uri);
} RBGenericPlayerSourceClass;

RBRemovableMediaSource *	rb_generic_player_source_new		(RBShell *shell, GnomeVFSVolume *volume);
GType			rb_generic_player_source_get_type		(void);
GType			rb_generic_player_source_register_type		(GTypeModule *module);

char *			rb_generic_player_source_get_mount_path		(RBGenericPlayerSource *source);

gboolean		rb_generic_player_is_volume_player		(GnomeVFSVolume *volume);

/* for subclasses */
void			rb_generic_player_source_add_playlist		(RBGenericPlayerSource *source,
									 RBShell *shell,
									 RBSource *playlist);

G_END_DECLS

#endif /* __RB_GENERIC_PLAYER_SOURCE_H */
