/*
 *  Copyright (C) 2002 Olivier Martin <olive.martin@gmail.com>
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

#include <gtk/gtk.h>
#include <widgets/rb-entry-view.h>
#include <rhythmdb/rhythmdb.h>
#include <sources/rb-source.h>

#ifndef __RB_SONG_INFO_H
#define __RB_SONG_INFO_H

G_BEGIN_DECLS

#define RB_TYPE_SONG_INFO         (rb_song_info_get_type ())
#define RB_SONG_INFO(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SONG_INFO, RBSongInfo))
#define RB_SONG_INFO_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SONG_INFO, RBSongInfoClass))
#define RB_IS_SONG_INFO(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SONG_INFO))
#define RB_IS_SONG_INFO_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SONG_INFO))
#define RB_SONG_INFO_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SONG_INFO, RBSongInfoClass))

typedef struct _RBSongInfo RBSongInfo;
typedef struct _RBSongInfoClass RBSongInfoClass;
typedef struct RBSongInfoPrivate RBSongInfoPrivate;

struct _RBSongInfo
{
	GtkDialog parent;

	RBSongInfoPrivate *priv;
};

struct _RBSongInfoClass
{
	GtkDialogClass parent_class;

	void (*pre_metadata_change) (RBSongInfo *info, RhythmDBEntry *entry);
	void (*post_metadata_change) (RBSongInfo *info, RhythmDBEntry *entry);
};

GType      rb_song_info_get_type (void);

GtkWidget *rb_song_info_new      (RBSource *source, RBEntryView *entry_view);

guint      rb_song_info_append_page (RBSongInfo *info, const char *title, GtkWidget *page);

G_END_DECLS

#endif /* __RB_SONG_INFO_H */
