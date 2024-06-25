/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2013 Jonathan Matthew <jonathan@d14n.org>
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

#include <config.h>

#include <shell/rb-task-list.h>

/* leave completed and cancelled tasks in the list for a while */
#define	FINISHED_EXPIRE_TIME		2

enum {
	PROP_0,
	PROP_MODEL
};

typedef struct {
	RBTaskList *list;
	RBTaskProgress *task;
	gulong expiry_id;
} TaskExpiry;

struct _RBTaskList
{
	GObject parent;

	RBListModel *model;
	GList *expiring;
};

struct _RBTaskListClass
{
	GObjectClass parent;
};

G_DEFINE_TYPE (RBTaskList, rb_task_list, G_TYPE_OBJECT);

static void rb_task_list_class_init (RBTaskListClass *klass);
static void rb_task_list_init (RBTaskList *list);



static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBTaskList *list = RB_TASK_LIST (object);
	
	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_object (value, list->model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	/*RBTaskList *list = RB_TASK_LIST (object);*/
	
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBTaskList *list = RB_TASK_LIST (object);
	
	g_clear_object (&list->model);

	G_OBJECT_CLASS (rb_task_list_parent_class)->dispose (object);
}

static void
unref_task (gpointer element)
{
	gpointer item = *(gpointer *)element;
	g_object_unref (item);
}

static void
rb_task_list_init (RBTaskList *list)
{
	list->model = rb_list_model_new (G_TYPE_OBJECT, unref_task);
}

static void
rb_task_list_class_init (RBTaskListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "model",
							      "model",
							      RB_TYPE_LIST_MODEL,
							      G_PARAM_READABLE));
}

static void
cancel_expiry (RBTaskList *list, RBTaskProgress *task)
{
	GList *l;
	TaskExpiry *expiry;

	for (l = list->expiring; l != NULL; l = l->next) {
		expiry = l->data;
		if (expiry->task == task) {
			expiry->list->expiring = g_list_remove (expiry->list->expiring, expiry);
			g_source_remove (expiry->expiry_id);
			return;
		}
	}
}

static gboolean
task_expired (TaskExpiry *expiry)
{
	rb_list_model_remove_item (expiry->list->model, expiry->task);
	expiry->list->expiring = g_list_remove (expiry->list->expiring, expiry);
	return FALSE;
}

static void
expire_task (RBTaskList *list, RBTaskProgress *task, gint seconds)
{
	TaskExpiry *expiry;

	cancel_expiry (list, task);
	if (rb_list_model_find (list->model, task) == -1)
		return;

	expiry = g_new0 (TaskExpiry, 1);
	expiry->task = task;
	expiry->list = list;
	expiry->expiry_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
							seconds,
							(GSourceFunc)task_expired,
							expiry,
							(GDestroyNotify) g_free);
	list->expiring = g_list_append (list->expiring, expiry);
}

static void
task_outcome_notify_cb (GObject *object, GParamSpec *pspec, RBTaskList *list)
{
	RBTaskOutcome outcome;
	g_object_get (object, "task-outcome", &outcome, NULL);
	switch (outcome) {
	case RB_TASK_OUTCOME_COMPLETE:
	case RB_TASK_OUTCOME_CANCELLED:
		expire_task (list, RB_TASK_PROGRESS (object), FINISHED_EXPIRE_TIME);
		break;

	case RB_TASK_OUTCOME_NONE:
		break;

	default:
		g_assert_not_reached ();
	}
}

void
rb_task_list_add_task (RBTaskList *list, RBTaskProgress *task)
{
	g_signal_connect (task, "notify::task-outcome", G_CALLBACK (task_outcome_notify_cb), list);
	rb_list_model_append (list->model, g_object_ref (task));
}

void
rb_task_list_remove_task (RBTaskList *list, RBTaskProgress *task)
{
	cancel_expiry (list, task);
	rb_list_model_remove_item (list->model, task);
}

RBTaskList *
rb_task_list_new (void)
{
	return RB_TASK_LIST (g_object_new (RB_TYPE_TASK_LIST, NULL));
}

/**
 * rb_task_list_get_model:
 * @list: a #RBTaskList
 *
 * Returns the #RBListModel backing the list
 *
 * Return value: (transfer none): list model
 */
RBListModel *
rb_task_list_get_model (RBTaskList *list)
{
	return list->model;
}
