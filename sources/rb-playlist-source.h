/*
 *  arch-tag: Header for playlist source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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

#ifndef __RB_PLAYLIST_SOURCE_H
#define __RB_PLAYLIST_SOURCE_H

#include <bonobo/bonobo-ui-container.h>
#include <libxml/tree.h>

#include "rb-source.h"
#include "rhythmdb.h"
#include "rb-library-source.h"
#include "rb-query-creator.h"
#include "rhythmdb-query-model.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST_SOURCE         (rb_playlist_source_get_type ())
#define RB_PLAYLIST_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSource))
#define RB_PLAYLIST_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSourceClass))
#define RB_IS_PLAYLIST_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYLIST_SOURCE))
#define RB_IS_PLAYLIST_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYLIST_SOURCE))
#define RB_PLAYLIST_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYLIST_SOURCE, RBPlaylistSourceClass))

typedef struct RBPlaylistSourcePrivate RBPlaylistSourcePrivate;

typedef struct
{
	RBSource parent;

	RBPlaylistSourcePrivate *priv;
} RBPlaylistSource;

typedef struct
{
	RBSourceClass parent;
} RBPlaylistSourceClass;

GType		rb_playlist_source_get_type	(void);

RBSource *	rb_playlist_source_new		(RhythmDB *db, gboolean smart);

RBSource *	rb_playlist_source_new_from_xml	(RhythmDB *db,
						 xmlNodePtr node);

RhythmDBQueryModel *rb_playlist_source_get_model(RBPlaylistSource *source);

void		rb_playlist_source_set_query	(RBPlaylistSource *source,
						 GPtrArray *query,
						 guint limit_count,
						 guint limit_mb);

void		rb_playlist_source_save_playlist(RBPlaylistSource *source,
						 const char *uri);

void		rb_playlist_source_save_to_xml	(RBPlaylistSource *source,
						 xmlNodePtr node);

void		rb_playlist_source_add_entry	(RBPlaylistSource *source,
						 RhythmDBEntry *entry);

void		rb_playlist_source_remove_entry	(RBPlaylistSource *source,
						 RhythmDBEntry *entry);

void		rb_playlist_source_add_location	(RBPlaylistSource *source,
						 const char *location);

void		rb_playlist_source_remove_location	(RBPlaylistSource *source,
						 	 const char *location);

void		rb_playlist_source_delete	(RBPlaylistSource *source);

G_END_DECLS

#endif /* __RB_PLAYLIST_SOURCE_H */
