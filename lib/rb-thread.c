/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Colin Walters <walters@verbum.org>
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
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>

#include "rb-thread.h"
#include "rb-debug.h"

static void rb_thread_class_init (RBThreadClass *klass);
static void rb_thread_init (RBThread *view);
static void rb_thread_finalize (GObject *object);
static void rb_thread_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_thread_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static GObject * rb_thread_constructor (GType type, guint n_construct_properties,
					GObjectConstructParam *construct_properties);
static gpointer rb_thread_action_thread_main (gpointer data);

struct RBThreadPrivate
{
	GMainContext *context;
	GMainContext *target_context;
	GMainLoop *loop;
	GAsyncQueue *action_queue;
	GAsyncQueue *result_queue;

	gint action_queue_processors;

	gboolean thread_running;
	GMutex *state_mutex;
	GCond *state_condition;

	GSList *queued_result_callbacks;

	gboolean action_processor_queued;

	RBThreadActionFunc action_func;
	RBThreadResultFunc result_func;
	RBThreadActionDestroyFunc action_destroy_func;
	RBThreadResultDestroyFunc result_destroy_func;
	gpointer user_data;

	GThread *thread; 

	gint exit_flag;
};

#define RB_THREAD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_THREAD, RBThreadPrivate))

enum
{
	PROP_0,
	PROP_CONTEXT,
	PROP_ACTION_FUNC,
	PROP_RESULT_FUNC,
	PROP_ACTION_DESTROY,
	PROP_RESULT_DESTROY,
	PROP_DATA,
};

G_DEFINE_TYPE(RBThread, rb_thread, G_TYPE_OBJECT)

static void
rb_thread_class_init (RBThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_thread_finalize;
	object_class->constructor = rb_thread_constructor;

	object_class->set_property = rb_thread_set_property;
	object_class->get_property = rb_thread_get_property;

	g_object_class_install_property (object_class,
					 PROP_CONTEXT,
					 g_param_spec_pointer ("context",
							      "GMainContext",
							      "Context in which to invoke callbacks",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ACTION_FUNC,
					 g_param_spec_pointer ("action-func",
							      "GFunc",
							      "Callback function",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_ACTION_DESTROY,
					 g_param_spec_pointer ("action-destroy",
							      "GFunc",
							      "Action destroy function",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_RESULT_FUNC,
					 g_param_spec_pointer ("result-func",
							      "GFunc",
							      "Callback function",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_RESULT_DESTROY,
					 g_param_spec_pointer ("result-destroy",
							      "GFunc",
							      "Result destroy function",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DATA,
					 g_param_spec_pointer ("data",
							      "User data",
							      "User data",
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBThreadPrivate));
}

static void
rb_thread_init (RBThread *thread)
{
	thread->priv = RB_THREAD_GET_PRIVATE (thread);

	thread->priv->action_queue = g_async_queue_new ();
	thread->priv->result_queue = g_async_queue_new ();
	thread->priv->state_condition = g_cond_new ();
	thread->priv->action_queue_processors = 0;
	thread->priv->exit_flag = 0;
}

static void
rb_thread_finalize (GObject *object)
{
	RBThread *thread;
	gpointer action;
	GSList *link;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_THREAD (object));

	thread = RB_THREAD (object);

	g_return_if_fail (thread->priv != NULL);

	while ((action = g_async_queue_try_pop (thread->priv->action_queue)) != NULL)
		thread->priv->action_destroy_func (action, thread->priv->user_data);

	for (link = thread->priv->queued_result_callbacks; link; link = link->next) {
		GSource *source = link->data;
		g_source_destroy (source);
	}
	g_slist_free (thread->priv->queued_result_callbacks);
		
	g_async_queue_unref (thread->priv->action_queue);
	g_async_queue_unref (thread->priv->result_queue);

	G_OBJECT_CLASS (rb_thread_parent_class)->finalize (object);
}


static void
rb_thread_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBThread *thread = RB_THREAD (object);

	switch (prop_id)
	{
	case PROP_CONTEXT:
		thread->priv->context = g_value_get_pointer (value);
		break;
	case PROP_ACTION_FUNC:
		thread->priv->action_func = g_value_get_pointer (value);
		break;
	case PROP_RESULT_FUNC:
		thread->priv->action_func = g_value_get_pointer (value);
		break;
	case PROP_ACTION_DESTROY:
		thread->priv->action_destroy_func = g_value_get_pointer (value);
		break;
	case PROP_RESULT_DESTROY:
		thread->priv->result_destroy_func = g_value_get_pointer (value);
		break;
	case PROP_DATA:
		thread->priv->user_data = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_thread_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBThread *thread = RB_THREAD (object);

	switch (prop_id)
	{
	case PROP_CONTEXT:
		g_value_set_pointer (value, thread->priv->context);
		break;
	case PROP_ACTION_FUNC:
		g_value_set_pointer (value, thread->priv->action_func);
		break;
	case PROP_RESULT_FUNC:
		g_value_set_pointer (value, thread->priv->result_func);
		break;
	case PROP_ACTION_DESTROY:
		g_value_set_pointer (value, thread->priv->action_destroy_func);
		break;
	case PROP_RESULT_DESTROY:
		g_value_set_pointer (value, thread->priv->result_destroy_func);
		break;
	case PROP_DATA:
		g_value_set_pointer (value, thread->priv->user_data);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBThread *
rb_thread_new (GMainContext *context,
	       RBThreadActionFunc action_cb,
	       RBThreadResultFunc result_cb,
	       RBThreadActionDestroyFunc action_destroy_func,
	       RBThreadResultDestroyFunc result_destroy_func,
	       gpointer user_data)
{
	return RB_THREAD (g_object_new (RB_TYPE_THREAD,
					"context", context,
					"action-func", action_cb,
					"result-func", result_cb,
					"destroyfunc", action_destroy_func,
					"action-destroy", action_destroy_func,
					"result-destroy", result_destroy_func,
					"data", user_data, NULL));
}

static GObject *
rb_thread_constructor (GType type, guint n_construct_properties,
		       GObjectConstructParam *construct_properties)
{
	RBThread *thread;
	RBThreadClass *klass;
	GObjectClass *parent_class; 

	klass = RB_THREAD_CLASS (g_type_class_peek (RB_TYPE_THREAD));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	thread = RB_THREAD (parent_class->constructor (type, n_construct_properties,
						       construct_properties));
	thread->priv->thread = g_thread_new ("rb-thread", rb_thread_action_thread_main, thread);

	/* Wait until the thread's mainloop is running */
	g_mutex_lock (thread->priv->state_mutex);
	while (!thread->priv->thread_running)
		g_cond_wait (thread->priv->state_condition, thread->priv->state_mutex);
	g_mutex_unlock (thread->priv->state_mutex);

	return G_OBJECT (thread);
}

struct RBThreadResultData
{
	RBThread *thread;
	GPtrArray *results;
};

static void
free_results (gpointer data)
{
	struct RBThreadResultData *resultdata = data;
	RBThread *thread = resultdata->thread;
	guint i;

	for (i = 0; i < resultdata->results->len; i++)
		thread->priv->result_destroy_func (g_ptr_array_index (resultdata->results, i),
						   thread->priv->user_data);

	g_ptr_array_free (resultdata->results, TRUE);
	g_object_unref (thread);
	g_free (resultdata);
}

static gboolean
process_results (gpointer data)
{
	struct RBThreadResultData *resultdata = data;
	RBThread *thread = resultdata->thread;
	guint i;

	for (i = 0; i < resultdata->results->len; i++)
		thread->priv->result_func (g_ptr_array_index (resultdata->results, i), thread->priv->user_data);
	return FALSE;
}

#define MAX_ACTIONS 10

/* This function as an idle handler in the mainloop of the action
 * thread; it calls the action function on all queued action and
 * gathers results, queueing the results for invocation by the
 * result function in the context of the "target" mainloop
 */
static gboolean
process_actions (gpointer data)
{
	RBThread *thread = data;
	gpointer action;
	GPtrArray *results;
	GSource *source;
	struct RBThreadResultData *resultdata;

	/* Invoke the action function on our queued actions, gathering results */
	results = g_ptr_array_new ();
	while (results->len < MAX_ACTIONS
	       && (action = g_async_queue_try_pop (thread->priv->action_queue))) {
		gpointer result;
		if (G_UNLIKELY (thread->priv->exit_flag))
			break;
		result = thread->priv->action_func (action, thread->priv->user_data, &(thread->priv->exit_flag));
		thread->priv->action_destroy_func (action, thread->priv->user_data);

		g_ptr_array_add (results, result);
	}
	/* Race condition here is irrelevant; see rb_thread_push_action */
	g_atomic_int_dec_and_test (&(thread->priv->action_queue_processors));

	resultdata = g_new0 (struct RBThreadResultData, 1);
	resultdata->thread = g_object_ref (thread);
	resultdata->results = results;

	/* Now queue a callback in the target context */
	source = g_idle_source_new ();
	g_source_set_callback (source, process_results, resultdata, free_results);
	g_source_attach (source, thread->priv->target_context);
	g_source_unref (source);

	return FALSE;
}

void
rb_thread_push_action (RBThread *thread,
		       gpointer  action)
{
	g_async_queue_push (thread->priv->action_queue, action);
	/* Wake the thread up if necessary - note that it is not harmful if
	 * we queue this multiple times; hence the race between the test for
	 * zero and the inc is fine.
	 */
	if (g_atomic_int_get (&thread->priv->action_queue_processors) == 0) {
		GSource *source;
		source = g_idle_source_new ();

		g_source_set_callback (source, process_actions, thread, NULL);
		g_source_attach (source, thread->priv->context);
		g_source_unref (source);
		g_atomic_int_inc (&(thread->priv->action_queue_processors));
	}
}

static gboolean
mainloop_quit_cb (gpointer data)
{
	RBThread *thread = data;
	g_main_loop_quit (thread->priv->loop);
	return FALSE;
}

void
rb_thread_terminate (RBThread *thread)
{
	GSource *source;

	/* Setting the exit flag stops processing in the idle function */
	g_atomic_int_inc (&(thread->priv->exit_flag));

	source = g_idle_source_new ();
	g_source_set_callback (source, mainloop_quit_cb, thread, NULL);
	g_source_attach (source, thread->priv->context);
	g_source_unref (source);

	g_thread_join (thread->priv->thread);
}

static gpointer
rb_thread_action_thread_main (gpointer data)
{
	RBThread *thread = data;

	g_mutex_lock (thread->priv->state_mutex);
	thread->priv->thread_running = TRUE;
	g_cond_broadcast (thread->priv->state_condition);
	g_mutex_unlock (thread->priv->state_mutex);

	thread->priv->context = g_main_context_new ();
	thread->priv->loop = g_main_loop_new (thread->priv->context, TRUE);

	rb_debug ("running");

	g_main_loop_run (thread->priv->loop);
	
	rb_debug ("exiting");

	g_main_loop_unref (thread->priv->loop);

	g_thread_exit (NULL);
	return NULL;

}
