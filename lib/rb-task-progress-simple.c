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

#include <lib/rb-task-progress-simple.h>

enum {
	CANCEL_TASK,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

struct _RBTaskProgressSimplePrivate
{
	char *label;
	char *detail;
	double progress;
	RBTaskOutcome outcome;
	gboolean notify;
	gboolean cancellable;
};

static void rb_task_progress_simple_class_init (RBTaskProgressSimpleClass *klass);
static void rb_task_progress_simple_init (RBTaskProgressSimple *task);
static void rb_task_progress_simple_task_progress_init (RBTaskProgressInterface *iface);


/**
 * SECTION:rbtaskprogresssimple
 * @short_description: implementation of RBTaskProgress interface
 *
 * This implementation of #RBTaskProgress can be used to represent
 * tasks that aren't bound to the lifecycle of an object that can
 * implement the interface directly.
 */

G_DEFINE_TYPE_EXTENDED (RBTaskProgressSimple,
			rb_task_progress_simple,
			G_TYPE_OBJECT,
			0,
			G_IMPLEMENT_INTERFACE (RB_TYPE_TASK_PROGRESS, rb_task_progress_simple_task_progress_init));

enum {
	PROP_0,
	PROP_TASK_LABEL,
	PROP_TASK_DETAIL,
	PROP_TASK_PROGRESS,
	PROP_TASK_OUTCOME,
	PROP_TASK_NOTIFY,
	PROP_TASK_CANCELLABLE
};

static void
task_progress_cancel (RBTaskProgress *task)
{
	g_signal_emit (RB_TASK_PROGRESS_SIMPLE (task), signals[CANCEL_TASK], 0);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RBTaskProgressSimple *task = RB_TASK_PROGRESS_SIMPLE (object);
	switch (prop_id) {
	case PROP_TASK_LABEL:
		g_free (task->priv->label);
		task->priv->label = g_value_dup_string (value);
		break;
	case PROP_TASK_DETAIL:
		g_free (task->priv->detail);
		task->priv->detail = g_value_dup_string (value);
		break;
	case PROP_TASK_PROGRESS:
		task->priv->progress = g_value_get_double (value);
		break;
	case PROP_TASK_OUTCOME:
		task->priv->outcome = g_value_get_enum (value);
		break;
	case PROP_TASK_NOTIFY:
		task->priv->notify = g_value_get_boolean (value);
		break;
	case PROP_TASK_CANCELLABLE:
		task->priv->cancellable = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBTaskProgressSimple *task = RB_TASK_PROGRESS_SIMPLE (object);
	switch (prop_id) {
	case PROP_TASK_LABEL:
		g_value_set_string (value, task->priv->label);
		break;
	case PROP_TASK_DETAIL:
		g_value_set_string (value, task->priv->detail);
		break;
	case PROP_TASK_PROGRESS:
		g_value_set_double (value, task->priv->progress);
		break;
	case PROP_TASK_OUTCOME:
		g_value_set_enum (value, task->priv->outcome);
		break;
	case PROP_TASK_NOTIFY:
		g_value_set_boolean (value, task->priv->notify);
		break;
	case PROP_TASK_CANCELLABLE:
		g_value_set_boolean (value, task->priv->cancellable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_finalize (GObject *object)
{
	RBTaskProgressSimple *task = RB_TASK_PROGRESS_SIMPLE (object);

	g_free (task->priv->label);
	g_free (task->priv->detail);

	G_OBJECT_CLASS (rb_task_progress_simple_parent_class)->finalize (object);
}

static void
rb_task_progress_simple_init (RBTaskProgressSimple *task)
{
	task->priv = G_TYPE_INSTANCE_GET_PRIVATE (task, RB_TYPE_TASK_PROGRESS_SIMPLE, RBTaskProgressSimplePrivate);
}

static void
rb_task_progress_simple_task_progress_init (RBTaskProgressInterface *interface)
{
	interface->cancel = task_progress_cancel;
}

static void
rb_task_progress_simple_class_init (RBTaskProgressSimpleClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBTaskProgressSimplePrivate));

	gobject_class->finalize = impl_finalize;
	gobject_class->set_property = impl_set_property;
	gobject_class->get_property = impl_get_property;

	g_object_class_override_property (gobject_class, PROP_TASK_LABEL, "task-label");
	g_object_class_override_property (gobject_class, PROP_TASK_DETAIL, "task-detail");
	g_object_class_override_property (gobject_class, PROP_TASK_PROGRESS, "task-progress");
	g_object_class_override_property (gobject_class, PROP_TASK_OUTCOME, "task-outcome");
	g_object_class_override_property (gobject_class, PROP_TASK_NOTIFY, "task-notify");
	g_object_class_override_property (gobject_class, PROP_TASK_CANCELLABLE, "task-cancellable");

	signals[CANCEL_TASK] =
		g_signal_new ("cancel-task",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      0);
}

/**
 * rb_task_progress_simple_new:
 *
 * Creates a new simple task progress object.
 *
 * Return value: (transfer full): the task object
 */
RBTaskProgress *
rb_task_progress_simple_new (void)
{
	return RB_TASK_PROGRESS (g_object_new (RB_TYPE_TASK_PROGRESS_SIMPLE, NULL));
}
