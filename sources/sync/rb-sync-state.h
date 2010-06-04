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

#ifndef __RB_SYNC_STATE_H
#define __RB_SYNC_STATE_H

#include <glib-object.h>

#include "rb-media-player-source.h"
#include "rb-sync-settings.h"

#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_SYNC_STATE         (rb_sync_state_get_type ())
#define RB_SYNC_STATE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SYNC_STATE, RBSyncState))
#define RB_SYNC_STATE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SYNC_STATE, RBSyncStateClass))
#define RB_IS_SYNC_STATE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SYNC_STATE))
#define RB_IS_SYNC_STATE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SYNC_STATE))
#define RB_SYNC_STATE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SYNC_STATE, RBSyncStateClass))

typedef struct _RBSyncState RBSyncState;
typedef struct _RBSyncStateClass RBSyncStateClass;
typedef struct _RBSyncStatePrivate RBSyncStatePrivate;

struct _RBSyncState
{
	GObject parent;

	guint64 total_music_size;
	guint64 total_podcast_size;
	guint64 sync_music_size;
	guint64 sync_podcast_size;

	guint64 sync_space_needed;
	guint64 sync_add_size;
	guint64 sync_remove_size;
	int sync_add_count;
	int sync_remove_count;
	int sync_keep_count;

	GList *sync_to_add;
	GList *sync_to_remove;

	RBSyncStatePrivate *priv;
};

struct _RBSyncStateClass
{
	GObjectClass parent_class;

	/* signals */
	void (*updated) (RBSyncState *state);
};

char *		rb_sync_state_make_track_uuid (RhythmDBEntry *entry);

GType		rb_sync_state_get_type (void);

RBSyncState *	rb_sync_state_new (RBMediaPlayerSource *source, RBSyncSettings *settings);

void		rb_sync_state_update (RBSyncState *state);

G_END_DECLS

#endif /* __RB_SYNC_STATE_H */
