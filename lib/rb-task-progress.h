/*
 *  Copyright (C) 2013 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_TASK_PROGRESS_H
#define RB_TASK_PROGRESS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_TASK_PROGRESS         (rb_task_progress_get_type ())
#define RB_TASK_PROGRESS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TASK_PROGRESS, RBTaskProgress))
#define RB_IS_TASK_PROGRESS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TASK_PROGRESS))
#define RB_TASK_PROGRESS_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_TASK_PROGRESS, RBTaskProgressInterface))

typedef struct _RBTaskProgress RBTaskProgress;
typedef struct _RBTaskProgressInterface RBTaskProgressInterface;

typedef enum
{
	RB_TASK_OUTCOME_NONE,
	RB_TASK_OUTCOME_COMPLETE,
	RB_TASK_OUTCOME_CANCELLED
} RBTaskOutcome;

GType rb_task_outcome_get_type (void);
#define RB_TASK_OUTCOME_TYPE (rb_task_outcome_get_type())

struct _RBTaskProgressInterface
{
	GTypeInterface g_iface;

	/* methods */
	void	(*cancel)	(RBTaskProgress *progress);
};

GType		rb_task_progress_get_type	(void);

void		rb_task_progress_cancel		(RBTaskProgress *progress);

G_END_DECLS

#endif /* RB_TASK_PROGRESS_H */
