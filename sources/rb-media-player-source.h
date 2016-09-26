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
#include <sources/rb-browser-source.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_MEDIA_PLAYER_SOURCE         (rb_media_player_source_get_type ())
#define RB_MEDIA_PLAYER_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSource))
#define RB_MEDIA_PLAYER_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))
#define RB_IS_MEDIA_PLAYER_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_IS_MEDIA_PLAYER_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MEDIA_PLAYER_SOURCE))
#define RB_MEDIA_PLAYER_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MEDIA_PLAYER_SOURCE, RBMediaPlayerSourceClass))

#define RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE         (rb_media_player_entry_type_get_type ())
#define RB_MEDIA_PLAYER_ENTRY_TYPE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE, RBMediaPlayerEntryType))
#define RB_MEDIA_PLAYER_ENTRY_TYPE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE, RBMediaPlayerEntryTypeClass))
#define RB_IS_MEDIA_PLAYER_ENTRY_TYPE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE))
#define RB_IS_MEDIA_PLAYER_ENTRY_TYPE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE))
#define RB_MEDIA_PLAYER_ENTRY_TYPE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MEDIA_PLAYER_ENTRY_TYPE, RBMediaPlayerEntryTypeClass))

typedef struct _RBMediaPlayerSource RBMediaPlayerSource;
typedef struct _RBMediaPlayerSourceClass RBMediaPlayerSourceClass;

typedef struct _RhythmDBEntryType RBMediaPlayerEntryType;
typedef struct _RhythmDBEntryTypeClass RBMediaPlayerEntryTypeClass;

struct _RBMediaPlayerSource
{
	RBBrowserSource parent_instance;
};

struct _RBMediaPlayerSourceClass
{
	RBBrowserSourceClass parent_class;

	/* class members */
	void		(*get_entries)	(RBMediaPlayerSource *source, const char *category, GHashTable *map);
	guint64		(*get_capacity)	(RBMediaPlayerSource *source);
	guint64		(*get_free_space)	(RBMediaPlayerSource *source);
	void		(*delete_entries)	(RBMediaPlayerSource *source,
						 GList *entries,
						 GAsyncReadyCallback callback,
						 gpointer data);
	void		(*add_playlist)	(RBMediaPlayerSource *source, gchar *name, GList *entries);
	void		(*remove_playlists) (RBMediaPlayerSource *source);
	void		(*show_properties)	(RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook);
};

GType	rb_media_player_entry_type_get_type (void);

GType	rb_media_player_source_get_type	(void);

void	rb_media_player_source_load		(RBMediaPlayerSource *source);

guint64 rb_media_player_source_get_capacity	(RBMediaPlayerSource *source);
guint64 rb_media_player_source_get_free_space	(RBMediaPlayerSource *source);
void	rb_media_player_source_get_entries	(RBMediaPlayerSource *source,
						 const char *category,	/* defined in rb-sync-settings.h */
						 GHashTable *map);

void	rb_media_player_source_delete_entries	(RBMediaPlayerSource *source,
						 GList *entries,
						 GAsyncReadyCallback callback,
						 gpointer data);

void	rb_media_player_source_show_properties (RBMediaPlayerSource *source);

void	rb_media_player_source_sync (RBMediaPlayerSource *source);

void	_rb_media_player_source_add_to_map 	(GHashTable *device_map, RhythmDBEntry *entry);

void	rb_media_player_source_purge_metadata_cache (RBMediaPlayerSource *source);

G_END_DECLS

#endif
