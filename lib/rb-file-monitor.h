/*
 *  arch-tag: Header for Rhythmbox file monitoring object
 *
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
 */

#ifndef __RB_FILE_MONITOR_H
#define __RB_FILE_MONITOR_H

#include <glib-object.h>

#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_FILE_MONITOR         (rb_file_monitor_get_type ())
#define RB_FILE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_FILE_MONITOR, RBFileMonitor))
#define RB_FILE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_FILE_MONITOR, RBFileMonitorClass))
#define RB_IS_FILE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_FILE_MONITOR))
#define RB_IS_FILE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_FILE_MONITOR))
#define RB_FILE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_FILE_MONITOR, RBFileMonitorClass))

typedef struct RBFileMonitorPrivate RBFileMonitorPrivate;

typedef struct
{
	GObject parent;

	RBFileMonitorPrivate *priv;
} RBFileMonitor;

typedef struct
{
	GObjectClass parent;

	void (*file_changed) (RBFileMonitor *monitor, const char *uri);
	void (*file_removed) (RBFileMonitor *monitor, const char *uri);
} RBFileMonitorClass;

GType          rb_file_monitor_get_type (void);

RBFileMonitor *rb_file_monitor_new      (void);

RBFileMonitor *rb_file_monitor_get      (void);

void           rb_file_monitor_add      (RBFileMonitor *monitor,
					 const char *uri);
void           rb_file_monitor_remove   (RBFileMonitor *monitor,
					 const char *uri);

G_END_DECLS

#endif /* __RB_FILE_MONITOR_H */
