/*
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
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

#ifndef __RB_MEDIA_PLAYER_SOURCE_H
#define __RB_MEDIA_PLAYER_SOURCE_H

#include <glib.h>

#include <shell/rb-shell.h>
#include <sources/rb-removable-media-source.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_MEDIA_PLAYER_SOURCE         (rb_media_player_source_get_type ())
#define RB_MEDIA_PLAYER_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSource))
#define RB_MEDIA_PLAYER_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))
#define RB_IS_MEDIA_PLAYER_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_IS_MEDIA_PLAYER_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_MEDIA_PLAYER_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))

typedef struct _RBMediaPlayerSource RBMediaPlayerSource;
typedef struct _RBMediaPlayerSourceClass RBMediaPlayerSourceClass;

typedef void (*RBMediaPlayerSourceDeleteCallback) (RBMediaPlayerSource *source, gpointer data);

struct _RBMediaPlayerSource
{
	RBRemovableMediaSource parent_instance;
};

struct _RBMediaPlayerSourceClass
{
	RBRemovableMediaSourceClass parent_class;

	/* class members */
	void		(*impl_get_entries)	(RBMediaPlayerSource *source, const char *category, GHashTable *map);
	guint64		(*impl_get_capacity)	(RBMediaPlayerSource *source);
	guint64		(*impl_get_free_space)	(RBMediaPlayerSource *source);
	void		(*impl_delete_entries)	(RBMediaPlayerSource *source,
						 GList *entries,
						 RBMediaPlayerSourceDeleteCallback callback,
						 gpointer data,
						 GDestroyNotify destroy_data);
	void		(*impl_add_playlist)	(RBMediaPlayerSource *source, gchar *name, GList *entries);
	void		(*impl_remove_playlists) (RBMediaPlayerSource *source);
	void		(*impl_show_properties)	(RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook);
};

GType	rb_media_player_source_get_type	(void);

void	rb_media_player_source_init_actions	(RBShell *shell);

void	rb_media_player_source_load		(RBMediaPlayerSource *source);

guint64 rb_media_player_source_get_capacity	(RBMediaPlayerSource *source);
guint64 rb_media_player_source_get_free_space	(RBMediaPlayerSource *source);
void	rb_media_player_source_get_entries	(RBMediaPlayerSource *source,
						 const char *category,	/* defined in rb-sync-settings.h */
						 GHashTable *entries);

void	rb_media_player_source_delete_entries	(RBMediaPlayerSource *source,
						 GList *entries,
						 RBMediaPlayerSourceDeleteCallback callback,
						 gpointer user_data,
						 GDestroyNotify destroy_data);

void	rb_media_player_source_show_properties (RBMediaPlayerSource *source);

void	rb_media_player_source_sync (RBMediaPlayerSource *source);

void	_rb_media_player_source_add_to_map 	(GHashTable *device_map, RhythmDBEntry *entry);

G_END_DECLS

#endif
