/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_PODCAST_ENTRY_TYPES_H
#define RB_PODCAST_ENTRY_TYPES_H

#include <rhythmdb/rhythmdb-entry-type.h>
#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

RhythmDBEntryType *rb_podcast_get_post_entry_type  (void);
RhythmDBEntryType *rb_podcast_get_feed_entry_type  (void);
RhythmDBEntryType *rb_podcast_get_search_entry_type  (void);
#define RHYTHMDB_ENTRY_TYPE_PODCAST_POST (rb_podcast_get_post_entry_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_FEED (rb_podcast_get_feed_entry_type ())
#define RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH (rb_podcast_get_search_entry_type ())

void			rb_podcast_register_entry_types		(RhythmDB *db);

G_END_DECLS

#endif /* RB_PODCAST_ENTRY_TYPES_H */
