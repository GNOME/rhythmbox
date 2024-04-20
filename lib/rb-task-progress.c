/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2013  Jonathan Matthew  <jonathan@d14n.org>
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <lib/rb-task-progress.h>

G_DEFINE_INTERFACE (RBTaskProgress, rb_task_progress, 0);

/**
 * SECTION:rbtaskprogress
 * @short_description: interface for objects that report task progress
 *
 */


static void
default_cancel (RBTaskProgress *progress)
{
	/* nothing */
}

static void
rb_task_progress_default_init (RBTaskProgressInterface *interface)
{
	interface->cancel = default_cancel;

	g_object_interface_install_property (interface,
					     g_param_spec_string ("task-label",
								  "task label",
								  "task label",
								  NULL,
								  G_PARAM_READWRITE));
	g_object_interface_install_property (interface,
					     g_param_spec_string ("task-detail",
								  "task detail",
								  "task detail",
								  NULL,
								  G_PARAM_READWRITE));
	g_object_interface_install_property (interface,
					     g_param_spec_double ("task-progress",
								  "task progress",
								  "task progress",
								  0.0, 1.0, 0.0,
								  G_PARAM_READWRITE));
	g_object_interface_install_property (interface,
					     g_param_spec_enum ("task-outcome",
								"task outcome",
								"task outcome",
								RB_TASK_OUTCOME_TYPE,
								RB_TASK_OUTCOME_NONE,
								G_PARAM_READWRITE));
	g_object_interface_install_property (interface,
					     g_param_spec_boolean ("task-notify",
								   "task notify",
								   "whether to notify on completion",
								   FALSE,
								   G_PARAM_READWRITE));
	g_object_interface_install_property (interface,
					     g_param_spec_boolean ("task-cancellable",
								   "task cancellable",
								   "whether the task can be cancelled",
								   FALSE,
								   G_PARAM_READWRITE));
}

void
rb_task_progress_cancel (RBTaskProgress *progress)
{
	RBTaskProgressInterface *iface = RB_TASK_PROGRESS_GET_IFACE (progress);
	iface->cancel (progress);
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_task_outcome_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_TASK_OUTCOME_NONE, "none"),
			ENUM_ENTRY (RB_TASK_OUTCOME_COMPLETE, "complete"),
			ENUM_ENTRY (RB_TASK_OUTCOME_CANCELLED, "cancelled"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBTaskOutcome", values);
	}

	return etype;
}
