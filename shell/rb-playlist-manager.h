/*
 *  arch-tag: Header for Rhythmbox playlist management object
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_PLAYLIST_MANAGER_H
#define __RB_PLAYLIST_MANAGER_H

#include <bonobo/bonobo-ui-util.h>
#include "rb-source.h"
#include "rhythmdb.h"
#include "rb-sourcelist.h"
#include "rb-library-source.h"
#include "rb-iradio-source.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST_MANAGER         (rb_playlist_manager_get_type ())
#define RB_PLAYLIST_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManager))
#define RB_PLAYLIST_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManagerClass))
#define RB_IS_PLAYLIST_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYLIST_MANAGER))
#define RB_IS_PLAYLIST_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYLIST_MANAGER))
#define RB_PLAYLIST_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManagerClass))

typedef struct RBPlaylistManagerPrivate RBPlaylistManagerPrivate;

typedef struct
{
	GObject parent;

	RBPlaylistManagerPrivate *priv;
} RBPlaylistManager;

typedef struct
{
	GObjectClass parent_class;

	/* signals */
	void	(*playlist_added) (RBSource *source);
	void	(*load_start) (void);
	void	(*load_finish) (void);
} RBPlaylistManagerClass;

GType			rb_playlist_manager_get_type	(void);

RBPlaylistManager *	rb_playlist_manager_new		(BonoboUIComponent *component, GtkWindow *window,
							 RhythmDB *db, RBSourceList *sourcelist,
							 RBLibrarySource *libsource,
							 RBIRadioSource *iradio_source);

const char *		rb_playlist_manager_parse_file	(RBPlaylistManager *mgr,
							 const char *uri);

void			rb_playlist_manager_set_source	(RBPlaylistManager *mgr,
							 RBSource *player);

void			rb_playlist_manager_load_legacy_playlists (RBPlaylistManager *mgr);

void			rb_playlist_manager_load_playlists (RBPlaylistManager *mgr);

void			rb_playlist_manager_save_playlists (RBPlaylistManager *mgr);

RBSource *		rb_playlist_manager_new_playlist (RBPlaylistManager *mgr, gboolean automatic);

G_END_DECLS

#endif /* __RB_PLAYLIST_MANAGER_H */
