/* 
   arch-tag: Header for Rhythmbox playlist parser

   Copyright (C) 2002, 2003 Bastien Nocera <hadess@hadess.net>
   Copyright (C) 2003 Colin Walters <walters@verbum.org>

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Bastien Nocera <hadess@hadess.net>
 */

#ifndef RB_PLAYLIST_H
#define RB_PLAYLIST_H

#include <glib.h>
#include <gtk/gtktreemodel.h>

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST            (rb_playlist_get_type ())
#define RB_PLAYLIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PLAYLIST, RBPlaylist))
#define RB_PLAYLIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_PLAYLIST, RBPlaylistClass))
#define RB_IS_PLAYLIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PLAYLIST))
#define RB_IS_PLAYLIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_PLAYLIST))

typedef struct RBPlaylist	       RBPlaylist;
typedef struct RBPlaylistClass      RBPlaylistClass;
typedef struct RBPlaylistPrivate    RBPlaylistPrivate;

struct RBPlaylist {
	GObject parent;
	RBPlaylistPrivate *priv;
};

struct RBPlaylistClass {
	GObjectClass parent_class;

	/* signals */
	void (*entry) (RBPlaylist *playlist, const char *uri, const char *title,
		       const char *genre);
};

typedef enum
{
	RB_PLAYLIST_ERROR_VFS_OPEN,
	RB_PLAYLIST_ERROR_VFS_WRITE,
} RBPlaylistError;

#define RB_PLAYLIST_ERROR (rb_playlist_error_quark ())

GQuark rb_playlist_error_quark (void);

typedef void (*RBPlaylistIterFunc) (GtkTreeModel *model, GtkTreeIter *iter, char **uri, char **title);

GtkType    rb_playlist_get_type (void);

gboolean   rb_playlist_write (RBPlaylist *playlist, GtkTreeModel *model,
			      RBPlaylistIterFunc func,
			      const char *output, GError **error);

gboolean   rb_playlist_can_handle (const char *url);

gboolean   rb_playlist_parse (RBPlaylist *playlist, const char *url);

RBPlaylist *rb_playlist_new (void);


G_END_DECLS

#endif /* RB_PLAYLIST_H */
