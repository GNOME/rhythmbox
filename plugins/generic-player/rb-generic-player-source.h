/*
 *  Copyright (C) 2005 James Livingston <doclivingston@gmail.com>
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

#ifndef __RB_GENERIC_PLAYER_SOURCE_H
#define __RB_GENERIC_PLAYER_SOURCE_H

#include "rb-shell.h"
#include "rb-media-player-source.h"
#include "rhythmdb.h"

#include "mediaplayerid.h"

#include <totem-pl-parser.h>

G_BEGIN_DECLS

#define RB_TYPE_GENERIC_PLAYER_SOURCE         (rb_generic_player_source_get_type ())
#define RB_GENERIC_PLAYER_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSource))
#define RB_GENERIC_PLAYER_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourceClass))
#define RB_IS_GENERIC_PLAYER_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GENERIC_PLAYER_SOURCE))
#define RB_IS_GENERIC_PLAYER_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GENERIC_PLAYER_SOURCE))
#define RB_GENERIC_PLAYER_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourceClass))

typedef struct
{
	RBMediaPlayerSource parent;
} RBGenericPlayerSource;

typedef struct
{
	RBMediaPlayerSourceClass parent;

	char *		(*get_mount_path) (RBGenericPlayerSource *source);
	void		(*load_playlists) (RBGenericPlayerSource *source);

	char *		(*uri_from_playlist_uri) (RBGenericPlayerSource *source, const char *uri);
	char *		(*uri_to_playlist_uri) (RBGenericPlayerSource *source, const char *uri, TotemPlParserType playlist_type);

	/* used for track transfer - returns the filename relative to the audio folder on the device */
	char *		(*build_filename) (RBGenericPlayerSource *source, RhythmDBEntry *entry);
} RBGenericPlayerSourceClass;

GType			rb_generic_player_source_get_type		(void);

char *			rb_generic_player_source_get_mount_path		(RBGenericPlayerSource *source);
char *			rb_generic_player_source_uri_from_playlist_uri  (RBGenericPlayerSource *source,
									 const char *uri);
char *			rb_generic_player_source_uri_to_playlist_uri    (RBGenericPlayerSource *source,
									 const char *uri,
									 TotemPlParserType playlist_type);
void			rb_generic_player_source_set_supported_formats  (RBGenericPlayerSource *source,
									 TotemPlParser *parser);
TotemPlParserType	rb_generic_player_source_get_playlist_format	(RBGenericPlayerSource *source);
char *			rb_generic_player_source_get_playlist_path	(RBGenericPlayerSource *source);

gboolean		rb_generic_player_is_mount_player		(GMount *mount, MPIDDevice *device_info);

void			rb_generic_player_source_delete_entries		(RBGenericPlayerSource *source,
									 GList *entries);

/* for subclasses */
void			rb_generic_player_source_add_playlist		(RBGenericPlayerSource *source,
									 RBShell *shell,
									 RBSource *playlist);

void			_rb_generic_player_source_register_type		(GTypeModule *module);

G_END_DECLS

#endif /* __RB_GENERIC_PLAYER_SOURCE_H */
