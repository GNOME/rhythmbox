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

#include <widgets/rb-task-list-display.h>
#include <lib/rb-task-progress.h>
#include <lib/rb-list-model.h>
#include <lib/rb-builder-helpers.h>
#include <lib/rb-text-helpers.h>
#include <lib/rb-util.h>

#define TASK_REMOVE_DELAY	15

static void rb_task_list_display_class_init (RBTaskListDisplayClass *klass);
static void rb_task_list_display_init (RBTaskListDisplay *list);

struct _RBTaskListDisplayPrivate
{
	RBListModel *model;
	GArray *widgets;
};

G_DEFINE_TYPE (RBTaskListDisplay, rb_task_list_display, GTK_TYPE_GRID);

enum {
	PROP_0,
	PROP_MODEL
};

static void
stop_clicked_cb (GtkButton *button, RBTaskProgress *task)
{
	rb_task_progress_cancel (task);
}

static gboolean
transform_outcome (GBinding *binding, const GValue *source, GValue *target, gpointer data)
{
	RBTaskOutcome outcome;
	gboolean sensitive;

	outcome = g_value_get_enum (source);
	switch (outcome) {
	case RB_TASK_OUTCOME_NONE:
		sensitive = TRUE;
		break;
	case RB_TASK_OUTCOME_COMPLETE:
	case RB_TASK_OUTCOME_CANCELLED:
		sensitive = FALSE;
		break;
	default:
		g_assert_not_reached ();
	}

	g_value_set_boolean (target, sensitive);
	return TRUE;
}

static void
task_list_changed_cb (RBListModel *model, int position, int removed, int added, RBTaskListDisplay *list)
{
	int i;

	for (i = 0; i < removed; i++) {
		GtkWidget *w;

		w = g_array_index (list->priv->widgets, GtkWidget *, position);
		gtk_container_remove (GTK_CONTAINER (list), w);
		g_array_remove_index (list->priv->widgets, position);
	}

	for (i = 0; i < added; i++) {
		GtkBuilder *b;
		GtkWidget *entry;
		GtkWidget *widget;
		gboolean cancellable;
		RBTaskProgress *task;

		task = RB_TASK_PROGRESS (rb_list_model_get (model, position + i));

		b = rb_builder_load ("task-list-entry.ui", NULL);

		entry = GTK_WIDGET (gtk_builder_get_object (b, "task-list-entry"));

		widget = GTK_WIDGET (gtk_builder_get_object (b, "task-label"));
		g_object_bind_property (task, "task-label", widget, "label", G_BINDING_SYNC_CREATE);

		widget = GTK_WIDGET (gtk_builder_get_object (b, "task-detail"));
		g_object_bind_property (task, "task-detail", widget, "label", G_BINDING_SYNC_CREATE);
		gtk_label_set_attributes (GTK_LABEL (widget), rb_text_numeric_get_pango_attr_list ());

		widget = GTK_WIDGET (gtk_builder_get_object (b, "task-progress"));
		g_object_bind_property (task, "task-progress", widget, "fraction", G_BINDING_SYNC_CREATE);

		widget = GTK_WIDGET (gtk_builder_get_object (b, "task-cancel"));
		g_object_get (task, "task-cancellable", &cancellable, NULL);
		if (cancellable) {
			g_object_bind_property_full (task, "task-outcome",
						     widget, "sensitive",
						     G_BINDING_SYNC_CREATE,
						     transform_outcome,
						     NULL,
						     NULL,
						     NULL);
		} else {
			g_object_set (widget, "sensitive", FALSE, NULL);
		}
		g_signal_connect_object (widget, "clicked", G_CALLBACK (stop_clicked_cb), task, 0);

		gtk_grid_insert_column (GTK_GRID (list), position + i);
		gtk_grid_attach (GTK_GRID (list), entry, 0, position + i, 1, 1);
		gtk_widget_show_all (entry);
		g_array_insert_val (list->priv->widgets, position + i, entry);
	}
}

static void
impl_constructed (GObject *object)
{
	RBTaskListDisplay *list;

	RB_CHAIN_GOBJECT_METHOD (rb_task_list_display_parent_class, constructed, object);

	list = RB_TASK_LIST_DISPLAY (object);
	g_signal_connect (list->priv->model, "items-changed", G_CALLBACK (task_list_changed_cb), list);
	task_list_changed_cb (list->priv->model, 0, 0, rb_list_model_n_items (list->priv->model), list);
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBTaskListDisplay *list = RB_TASK_LIST_DISPLAY (object);
	
	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_object (value, list->priv->model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBTaskListDisplay *list = RB_TASK_LIST_DISPLAY (object);
	
	switch (prop_id) {
	case PROP_MODEL:
		list->priv->model = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBTaskListDisplay *list = RB_TASK_LIST_DISPLAY (object);
	
	if (list->priv->model != NULL) {
		g_signal_handlers_disconnect_by_func (list->priv->model, task_list_changed_cb, list);
		g_clear_object (&list->priv->model);
	}
	if (list->priv->widgets != NULL) {
		g_array_free (list->priv->widgets, TRUE);
		list->priv->widgets = NULL;
	}
	G_OBJECT_CLASS (rb_task_list_display_parent_class)->dispose (object);
}

static void
rb_task_list_display_init (RBTaskListDisplay *list)
{
	list->priv = G_TYPE_INSTANCE_GET_PRIVATE (list, RB_TYPE_TASK_LIST_DISPLAY, RBTaskListDisplayPrivate);

	list->priv->widgets = g_array_new (FALSE, FALSE, sizeof (GtkWidget *));
}

static void
rb_task_list_display_class_init (RBTaskListDisplayClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBTaskListDisplayPrivate));

	gobject_class->constructed = impl_constructed;
	gobject_class->dispose = impl_dispose;
	gobject_class->set_property = impl_set_property;
	gobject_class->get_property = impl_get_property;

	g_object_class_install_property (gobject_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "model",
							      "model",
							      RB_TYPE_LIST_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GtkWidget *
rb_task_list_display_new (RBListModel *model)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_TASK_LIST_DISPLAY,
					 "model", model,
					 "row-spacing", 12,
					 NULL));
}
