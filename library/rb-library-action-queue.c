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

#include "rb-library-action-queue.h"

static void rb_library_action_queue_class_init (RBLibraryActionQueueClass *klass);
static void rb_library_action_queue_init (RBLibraryActionQueue *library_action_queue);
static void rb_library_action_queue_finalize (GObject *object);

typedef struct
{
	RBLibraryActionType type;
	char *uri;
} RBLibraryAction;

struct RBLibraryActionQueuePrivate
{
	GQueue *queue;
	GMutex *lock;
};

static GObjectClass *parent_class = NULL;

GType
rb_library_action_queue_get_type (void)
{
	static GType rb_library_action_queue_type = 0;

	if (rb_library_action_queue_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryActionQueueClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_action_queue_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryActionQueue),
			0,
			(GInstanceInitFunc) rb_library_action_queue_init
		};

		rb_library_action_queue_type = g_type_register_static (G_TYPE_OBJECT,
						                       "RBLibraryActionQueue",
						                       &our_info, 0);
	}

	return rb_library_action_queue_type;
}

static void
rb_library_action_queue_class_init (RBLibraryActionQueueClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_action_queue_finalize;
}

static void
rb_library_action_queue_init (RBLibraryActionQueue *library_action_queue)
{
	library_action_queue->priv = g_new0 (RBLibraryActionQueuePrivate, 1);

	library_action_queue->priv->queue = g_queue_new ();

	library_action_queue->priv->lock = g_mutex_new ();
}

static void
rb_library_action_queue_finalize (GObject *object)
{
	RBLibraryActionQueue *library_action_queue;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_ACTION_QUEUE (object));

	library_action_queue = RB_LIBRARY_ACTION_QUEUE (object);

	g_return_if_fail (library_action_queue->priv != NULL);

	while (g_queue_is_empty (library_action_queue->priv->queue) == FALSE)
	{
		RBLibraryAction *action = g_queue_pop_head (library_action_queue->priv->queue);
		g_free (action->uri);
		g_free (action);
	}
	g_queue_free (library_action_queue->priv->queue);

	g_mutex_free (library_action_queue->priv->lock);

	g_free (library_action_queue->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryActionQueue *
rb_library_action_queue_new (void)
{
	RBLibraryActionQueue *library_action_queue;

	library_action_queue = RB_LIBRARY_ACTION_QUEUE (g_object_new (RB_TYPE_LIBRARY_ACTION_QUEUE, NULL));

	g_return_val_if_fail (library_action_queue->priv != NULL, NULL);

	return library_action_queue;
}

void
rb_library_action_queue_add (RBLibraryActionQueue *queue,
			     RBLibraryActionType type,
			     const char *uri)
{
	RBLibraryAction *action;

	action = g_new0 (RBLibraryAction, 1);
	action->type = type;
	action->uri = g_strdup (uri);
	
	g_mutex_lock (queue->priv->lock);
	g_queue_push_tail (queue->priv->queue, action);
	g_mutex_unlock (queue->priv->lock);
}

gboolean
rb_library_action_queue_is_empty (RBLibraryActionQueue *queue)
{
	gboolean ret;

	g_mutex_lock (queue->priv->lock);
	ret = g_queue_is_empty (queue->priv->queue);
	g_mutex_unlock (queue->priv->lock);

	return ret;
}

void
rb_library_action_queue_peek_head (RBLibraryActionQueue *queue,
                                   RBLibraryActionType *type,
                                   char **uri)
{
	RBLibraryAction *action;

	g_mutex_lock (queue->priv->lock);
	action = g_queue_peek_head (queue->priv->queue);
	g_mutex_unlock (queue->priv->lock);

	*type = action->type;
	*uri = action->uri;
}

void
rb_library_action_queue_pop_head (RBLibraryActionQueue *queue)
{
	RBLibraryAction *action;
	
	g_mutex_lock (queue->priv->lock);
	action = g_queue_pop_head (queue->priv->queue);
	g_mutex_unlock (queue->priv->lock);
	
	g_free (action->uri);
	g_free (action);
}
