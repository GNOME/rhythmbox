/* 
 *  arch-tag: Header for RhythmDB playlist model
 *
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

#include "config.h"
#include <glib-object.h>
#include <glib.h>

#include "rhythmdb.h"

#ifndef RHYTHMDB_PLAYLIST_MODEL_H
#define RHYTHMDB_PLAYLIST_MODEL_H

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST_MODEL		(rhythmdb_playlist_model_get_type ())
#define RHYTHMDB_PLAYLIST_MODEL(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYLIST_MODEL, RhythmDBPlaylistModel))
#define RHYTHMDB_PLAYLIST_MODEL_CLASS(k)(G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_PLAYLIST_MODEL_TYPE, RhythmDBPlaylistModelClass))
#define RB_IS_PLAYLIST_MODEL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYLIST_MODEL))
#define RB_IS_PLAYLIST_MODEL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYLIST_MODEL))
#define RHYTHMDB_PLAYLIST_MODEL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYLIST_MODEL, RhythmDBPlaylistModelClass))

typedef struct RhythmDBPlaylistModelPrivate RhythmDBPlaylistModelPrivate;

typedef struct
{
	GObject parent;

	RhythmDBPlaylistModelPrivate *priv;
} RhythmDBPlaylistModel;

typedef struct
{
	GObjectClass parent;
} RhythmDBPlaylistModelClass;

GType			rhythmdb_playlist_model_get_type	(void);

RhythmDBPlaylistModel *	rhythmdb_playlist_model_new		(RhythmDB *db, GPtrArray *query);

G_END_DECLS

#endif /* __RHYTHMDB_PLAYLIST_MODEL_H */
