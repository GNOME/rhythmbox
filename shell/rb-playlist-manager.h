/*
 *  arch-tag: Header for Rhythmbox playlist management object
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#ifndef __RB_PLAYLIST_MANAGER_H
#define __RB_PLAYLIST_MANAGER_H

#include "rb-source.h"
#include "rhythmdb.h"
#include "rb-sourcelist.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST_MANAGER         (rb_playlist_manager_get_type ())
#define RB_PLAYLIST_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManager))
#define RB_PLAYLIST_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManagerClass))
#define RB_IS_PLAYLIST_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYLIST_MANAGER))
#define RB_IS_PLAYLIST_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYLIST_MANAGER))
#define RB_PLAYLIST_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYLIST_MANAGER, RBPlaylistManagerClass))

typedef enum
{
	RB_PLAYLIST_MANAGER_ERROR_PARSE,
	RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_EXISTS,
	RB_PLAYLIST_MANAGER_ERROR_PLAYLIST_NOT_FOUND
} RBPlaylistManagerError;

#define RB_PLAYLIST_MANAGER_ERROR rb_playlist_manager_error_quark ()

GQuark rb_playlist_manager_error_quark (void);

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
	void	(*playlist_added) (RBPlaylistManager *manager, RBSource *source);
	void	(*playlist_created) (RBPlaylistManager *manager, RBSource *source);
	void	(*load_start) (RBPlaylistManager *manager);
	void	(*load_finish) (RBPlaylistManager *manager);
} RBPlaylistManagerClass;

typedef enum
{
	RB_PLAYLIST_EXPORT_TYPE_UNKNOWN,
	RB_PLAYLIST_EXPORT_TYPE_M3U,
	RB_PLAYLIST_EXPORT_TYPE_PLS,
} RBPlaylistExportType;

GType			rb_playlist_manager_get_type	(void);

RBPlaylistManager *	rb_playlist_manager_new		(RBShell *shell,
							 RBSourceList *sourcelist);

void			rb_playlist_manager_shutdown	(RBPlaylistManager *mgr);
gboolean 		rb_playlist_manager_parse_file	(RBPlaylistManager *mgr,
							 const char *uri,
							 GError **error);

void			rb_playlist_manager_load_playlists (RBPlaylistManager *mgr);

gboolean		rb_playlist_manager_save_playlists (RBPlaylistManager *mgr,
							    gboolean force);

RBSource *		rb_playlist_manager_new_playlist (RBPlaylistManager *mgr,
							  const char *suggested_name,
							  gboolean automatic);
RBSource *		rb_playlist_manager_new_playlist_from_selection_data (RBPlaylistManager *mgr,
                                                                              GtkSelectionData *data);

GList *			rb_playlist_manager_get_playlists (RBPlaylistManager *manager);

gboolean		rb_playlist_manager_get_playlist_names (RBPlaylistManager *manager,
								gchar ***playlists,
								GError **error);
gboolean		rb_playlist_manager_create_static_playlist (RBPlaylistManager *manager,
								    const gchar *name,
								    GError **error);
gboolean		rb_playlist_manager_delete_playlist (RBPlaylistManager *manager,
							     const gchar *name,
							     GError **error);
gboolean		rb_playlist_manager_add_to_playlist (RBPlaylistManager *manager,
							     const gchar *playlist,
							     const gchar *uri,
							     GError **error);
gboolean		rb_playlist_manager_remove_from_playlist (RBPlaylistManager *manager,
								  const gchar *playlist,
								  const gchar *uri,
								  GError **error);
gboolean		rb_playlist_manager_export_playlist (RBPlaylistManager *manager,
							     const gchar *playlist,
							     const gchar *uri,
							     gboolean m3u_format,
							     GError **error);

G_END_DECLS

#endif /* __RB_PLAYLIST_MANAGER_H */
