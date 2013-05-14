/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

#ifndef __RB_TRACK_TRANSFER_QUEUE_H
#define __RB_TRACK_TRANSFER_QUEUE_H

#include <rhythmdb/rhythmdb.h>
#include <shell/rb-shell.h>
#include <shell/rb-track-transfer-batch.h>

G_BEGIN_DECLS

#define RB_TYPE_TRACK_TRANSFER_QUEUE         (rb_track_transfer_queue_get_type ())
#define RB_TRACK_TRANSFER_QUEUE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TRACK_TRANSFER_QUEUE, RBTrackTransferQueue))
#define RB_TRACK_TRANSFER_QUEUE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TRACK_TRANSFER_QUEUE, RBTrackTransferQueueClass))
#define RB_IS_TRACK_TRANSFER_QUEUE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TRACK_TRANSFER_QUEUE))
#define RB_IS_TRACK_TRANSFER_QUEUE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TRACK_TRANSFER_QUEUE))
#define RB_TRACK_TRANSFER_QUEUE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TRACK_TRANSFER_QUEUE, RBTrackTransferQueueClass))

typedef struct _RBTrackTransferQueue RBTrackTransferQueue;
typedef struct _RBTrackTransferQueueClass RBTrackTransferQueueClass;
typedef struct _RBTrackTransferQueuePrivate RBTrackTransferQueuePrivate;

struct _RBTrackTransferQueue
{
	GObject parent;
	RBTrackTransferQueuePrivate *priv;
};

struct _RBTrackTransferQueueClass
{
	GObjectClass parent_class;

	/* signals */
	void	(*transfer_progress)	(RBTrackTransferQueue *queue,
					 int done,
					 int total,
					 double fraction,
					 int time_left);
};

RBTrackTransferQueue*   rb_track_transfer_queue_new		(RBShell *shell);
GType			rb_track_transfer_queue_get_type	(void);

void			rb_track_transfer_queue_start_batch	(RBTrackTransferQueue *queue,
								 RBTrackTransferBatch *batch);
void			rb_track_transfer_queue_cancel_batch	(RBTrackTransferQueue *queue,
								 RBTrackTransferBatch *batch);
GList *			rb_track_transfer_queue_find_batch_by_source (RBTrackTransferQueue *queue,
								 RBSource *source);
void			rb_track_transfer_queue_cancel_for_source (RBTrackTransferQueue *queue,
								 RBSource *source);

G_END_DECLS

#endif /* __RB_TRACK_TRANSFER_QUEUE_H */
