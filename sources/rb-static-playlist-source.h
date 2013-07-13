/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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

/*
 * Header for static playlist class
 */

#ifndef __RB_STATIC_PLAYLIST_SOURCE_H
#define __RB_STATIC_PLAYLIST_SOURCE_H

#include <sources/rb-playlist-source.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_STATIC_PLAYLIST_SOURCE         (rb_static_playlist_source_get_type ())
#define RB_STATIC_PLAYLIST_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STATIC_PLAYLIST_SOURCE, RBStaticPlaylistSource))
#define RB_STATIC_PLAYLIST_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STATIC_PLAYLIST_SOURCE, RBStaticPlaylistSourceClass))
#define RB_IS_STATIC_PLAYLIST_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STATIC_PLAYLIST_SOURCE))
#define RB_IS_STATIC_PLAYLIST_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STATIC_PLAYLIST_SOURCE))
#define RB_STATIC_PLAYLIST_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STATIC_PLAYLIST_SOURCE, RBStaticPlaylistSourceClass))

typedef struct _RBStaticPlaylistSource RBStaticPlaylistSource;
typedef struct _RBStaticPlaylistSourceClass RBStaticPlaylistSourceClass;

struct _RBStaticPlaylistSource
{
	RBPlaylistSource parent;
};

struct _RBStaticPlaylistSourceClass
{
	RBPlaylistSourceClass parent;
};

GType		rb_static_playlist_source_get_type 	(void);

RBSource *	rb_static_playlist_source_new		(RBShell *shell,
							 const char *name,
							 GSettings *settings,
							 gboolean local,
							 RhythmDBEntryType *entry_type);

RBSource *	rb_static_playlist_source_new_from_xml	(RBShell *shell,
							 const char *name,
							 xmlNodePtr node);
void		rb_static_playlist_source_load_from_xml	(RBStaticPlaylistSource *source,
							 xmlNodePtr node);

void		rb_static_playlist_source_add_entry	(RBStaticPlaylistSource *source,
						 	 RhythmDBEntry *entry,
							 gint index);

void		rb_static_playlist_source_remove_entry	(RBStaticPlaylistSource *source,
							 RhythmDBEntry *entry);

void		rb_static_playlist_source_add_location	(RBStaticPlaylistSource *source,
							 const char *location,
							 gint index);

void            rb_static_playlist_source_add_locations (RBStaticPlaylistSource *source,
							 GList *locations);

void		rb_static_playlist_source_remove_location(RBStaticPlaylistSource *source,
						 	 const char *location);

void		rb_static_playlist_source_move_entry	(RBStaticPlaylistSource *source,
							 RhythmDBEntry *entry,
							 gint index);

G_END_DECLS

#endif /* __RB_STATIC_PLAYLIST_SOURCE_H */
