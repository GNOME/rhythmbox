/*
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org>
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

#include <rhythmdb/rhythmdb.h>

#ifndef __RB_FEED_PODCAST_PROPERTIES_DIALOG_H
#define __RB_FEED_PODCAST_PROPERTIES_DIALOG_H

G_BEGIN_DECLS

#define RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG         (rb_feed_podcast_properties_dialog_get_type ())
#define RB_FEED_PODCAST_PROPERTIES_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, RBFeedPodcastPropertiesDialog))
#define RB_FEED_PODCAST_PROPERTIES_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, RBFeedPodcastPropertiesDialogClass))
#define RB_IS_FEED_PODCAST_PROPERTIES_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG))
#define RB_IS_FEED_PODCAST_PROPERTIES_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG))
#define RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, RBFeedPodcastPropertiesDialogClass))

typedef struct RBFeedPodcastPropertiesDialogPrivate RBFeedPodcastPropertiesDialogPrivate;

typedef struct
{
	GtkDialog parent;

	RBFeedPodcastPropertiesDialogPrivate *priv;
} RBFeedPodcastPropertiesDialog;

typedef struct
{
	GtkDialogClass parent_class;
} RBFeedPodcastPropertiesDialogClass;

GType      rb_feed_podcast_properties_dialog_get_type (void);

GtkWidget *rb_feed_podcast_properties_dialog_new      (RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RB_FEED_PODCAST_PROPERTIES_DIALOG_H */
