/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#ifndef __RB_LIBRARY_ACTION_QUEUE_H
#define __RB_LIBRARY_ACTION_QUEUE_H

#include <glib-object.h>

#include "rb-library-action.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_ACTION_QUEUE         (rb_library_action_queue_get_type ())
#define RB_LIBRARY_ACTION_QUEUE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_ACTION_QUEUE, RBLibraryActionQueue))
#define RB_LIBRARY_ACTION_QUEUE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_ACTION_QUEUE, RBLibraryActionQueueClass))
#define RB_IS_LIBRARY_ACTION_QUEUE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_ACTION_QUEUE))
#define RB_IS_LIBRARY_ACTION_QUEUE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_ACTION_QUEUE))
#define RB_LIBRARY_ACTION_QUEUE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_ACTION_QUEUE, RBLibraryActionQueueClass))

typedef struct RBLibraryActionQueuePrivate RBLibraryActionQueuePrivate;

typedef struct
{
	GObject parent;

	RBLibraryActionQueuePrivate *priv;
} RBLibraryActionQueue;

typedef struct
{
	GObjectClass parent;
} RBLibraryActionQueueClass;

GType                 rb_library_action_queue_get_type  (void);

RBLibraryActionQueue *rb_library_action_queue_new       (void);

RBLibraryAction      *rb_library_action_queue_add       (RBLibraryActionQueue *queue,
							 gboolean priority,
							 RBLibraryActionType type,
							 const char *uri);

gboolean              rb_library_action_queue_is_empty  (RBLibraryActionQueue *queue);

RBLibraryAction      *rb_library_action_queue_peek_head (RBLibraryActionQueue *queue,
							 RBLibraryActionType *type,
							 char **uri);
void                  rb_library_action_queue_pop_head  (RBLibraryActionQueue *queue);

G_END_DECLS

#endif /* __RB_LIBRARY_ACTION_QUEUE_H */
