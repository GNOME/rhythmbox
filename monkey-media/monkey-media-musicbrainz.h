/*  monkey-media
 *
 *  arch-tag: Header for MusicBrainz metadata loading object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __MONKEY_MEDIA_MUSICBRAINZ_H
#define __MONKEY_MEDIA_MUSICBRAINZ_H

#include <glib/gerror.h>
#include <glib-object.h>

#include "monkey-media-stream-info.h"

G_BEGIN_DECLS

typedef enum
{
	MONKEY_MEDIA_MUSICBRAINZ_QUERY_NONE,
	MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD,
	MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG
} MonkeyMediaMusicbrainzQueryType;

#define MONKEY_MEDIA_TYPE_MUSICBRAINZ            (monkey_media_musicbrainz_get_type ())
#define MONKEY_MEDIA_MUSICBRAINZ(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONKEY_MEDIA_TYPE_MUSICBRAINZ, MonkeyMediaMusicbrainz))
#define MONKEY_MEDIA_MUSICBRAINZ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MONKEY_MEDIA_TYPE_MUSICBRAINZ, MonkeyMediaMusicbrainzClass))
#define MONKEY_MEDIA_IS_MUSICBRAINZ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONKEY_MEDIA_TYPE_MUSICBRAINZ))
#define MONKEY_MEDIA_IS_MUSICBRAINZ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MONKEY_MEDIA_TYPE_MUSICBRAINZ))
#define MONKEY_MEDIA_MUSICBRAINZ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MONKEY_MEDIA_TYPE_MUSICBRAINZ, MonkeyMediaMusicbrainzClass))

typedef struct MonkeyMediaMusicbrainzPrivate MonkeyMediaMusicbrainzPrivate;

typedef struct
{
	GObject object;

	MonkeyMediaMusicbrainzPrivate *priv;
} MonkeyMediaMusicbrainz;

typedef struct
{
	GObjectClass klass;
} MonkeyMediaMusicbrainzClass;

GType                   monkey_media_musicbrainz_get_type        (void);

MonkeyMediaMusicbrainz *monkey_media_musicbrainz_new             (void);

void                    monkey_media_musicbrainz_unref_if_around (void);

gboolean                monkey_media_musicbrainz_load_info       (MonkeyMediaMusicbrainz *brainz,
								  MonkeyMediaMusicbrainzQueryType type,
								  const char *id /* disc id or TRM id, depending on the query type */);

gboolean                monkey_media_musicbrainz_query           (MonkeyMediaMusicbrainz *brainz,
							          MonkeyMediaStreamInfoField field,
								  int track, /* set this to -1 if querying for a single song */
							          GValue *value);

G_END_DECLS

#endif /* __MONKEY_MEDIA_MUSICBRAINZ_H */
