/* 
 *  arch-tag: Implementation of Rhythmbox file monitoring object
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

#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-file-monitor.h"

static void rb_file_monitor_class_init (RBFileMonitorClass *klass);
static void rb_file_monitor_init (RBFileMonitor *thread);
static void rb_file_monitor_finalize (GObject *object);

typedef struct
{
	GHashTable *files;

	GnomeVFSMonitorHandle *handle;

	RBFileMonitor *monitor;
} RBFileMonitorHandle;

struct RBFileMonitorPrivate
{
	GHashTable *dir_to_handle;

	GMutex *lock;
};

enum
{
	FILE_CHANGED,
	FILE_REMOVED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_file_monitor_signals[LAST_SIGNAL] = { 0 };

GType
rb_file_monitor_get_type (void)
{
	static GType rb_file_monitor_type = 0;

	if (rb_file_monitor_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBFileMonitorClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_file_monitor_class_init,
			NULL,
			NULL,
			sizeof (RBFileMonitor),
			0,
			(GInstanceInitFunc) rb_file_monitor_init
		};

		rb_file_monitor_type = g_type_register_static (G_TYPE_OBJECT,
						               "RBFileMonitor",
						               &our_info, 0);
	}

	return rb_file_monitor_type;
}

static void
rb_file_monitor_class_init (RBFileMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_file_monitor_finalize;

	rb_file_monitor_signals[FILE_CHANGED] =
		g_signal_new ("file_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBFileMonitorClass, file_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	rb_file_monitor_signals[FILE_REMOVED] =
		g_signal_new ("file_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBFileMonitorClass, file_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static void
handle_free (RBFileMonitorHandle *handle)
{
	if (handle->handle != NULL)
		gnome_vfs_monitor_cancel (handle->handle);
	g_hash_table_destroy (handle->files);
	g_free (handle);
}

static void
rb_file_monitor_init (RBFileMonitor *monitor)
{
	monitor->priv = g_new0 (RBFileMonitorPrivate, 1);

	monitor->priv->dir_to_handle = g_hash_table_new_full (g_str_hash,
							      g_str_equal,
							      (GDestroyNotify) g_free,
							      (GDestroyNotify) handle_free);

	monitor->priv->lock = g_mutex_new ();
}

static void
rb_file_monitor_finalize (GObject *object)
{
	RBFileMonitor *monitor;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_FILE_MONITOR (object));

	monitor = RB_FILE_MONITOR (object);

	g_mutex_free (monitor->priv->lock);

	g_hash_table_destroy (monitor->priv->dir_to_handle);

	g_free (monitor->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBFileMonitor *
rb_file_monitor_new (void)
{
	RBFileMonitor *file_monitor;

	file_monitor = RB_FILE_MONITOR (g_object_new (RB_TYPE_FILE_MONITOR, NULL));

	g_return_val_if_fail (file_monitor->priv != NULL, NULL);

	return file_monitor;
}

#if 0
static void
monitor_callback (GnomeVFSMonitorHandle *vfs_handle,
		  const char *monitor_uri,
		  const char *info_uri,
		  GnomeVFSMonitorEventType event_type,
		  RBFileMonitorHandle *handle)
{
	if (g_hash_table_lookup (handle->files, info_uri) == NULL)
		return;

	switch (event_type)
	{
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
		g_signal_emit (G_OBJECT (handle->monitor),
			       rb_file_monitor_signals[FILE_CHANGED], 0, info_uri);
		break;
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		g_signal_emit (G_OBJECT (handle->monitor),
			       rb_file_monitor_signals[FILE_REMOVED], 0, info_uri);
		break;
	default:
		break;
	}
}
#endif

void
rb_file_monitor_add (RBFileMonitor *monitor,
		     const char *uri)
{
#if 0
	RBFileMonitorHandle *handle;
	char *dir, *realuri;
	
	g_return_if_fail (RB_IS_FILE_MONITOR (monitor));
	g_return_if_fail (uri != NULL);

	if (uri[0] == '/')
		realuri = gnome_vfs_get_uri_from_local_path (uri);
	else
		realuri = g_strdup (uri);

	dir = g_path_get_dirname (realuri);

	g_mutex_lock (monitor->priv->lock);

	handle = g_hash_table_lookup (monitor->priv->dir_to_handle, dir);
	
	if (handle == NULL)
	{
		handle = g_new0 (RBFileMonitorHandle, 1);

		handle->monitor = monitor;

		handle->files = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       (GDestroyNotify) g_free,
						       NULL);
		
		gnome_vfs_monitor_add (&(handle->handle), dir, GNOME_VFS_MONITOR_DIRECTORY,
				       (GnomeVFSMonitorCallback) monitor_callback, handle);

		g_hash_table_insert (monitor->priv->dir_to_handle, g_strdup (dir), handle);
	}

	g_hash_table_replace (handle->files, g_strdup (realuri), GINT_TO_POINTER (TRUE));

	g_mutex_unlock (monitor->priv->lock);

	g_free (dir);
	g_free (realuri);
#endif
}

void
rb_file_monitor_remove (RBFileMonitor *monitor,
			const char *uri)
{
#if 0
	RBFileMonitorHandle *handle;
	char *dir, *realuri;
	
	g_return_if_fail (RB_IS_FILE_MONITOR (monitor));
	g_return_if_fail (uri != NULL);

	if (uri[0] == '/')
		realuri = gnome_vfs_get_uri_from_local_path (uri);
	else
		realuri = g_strdup (uri);

	dir = g_path_get_dirname (realuri);

	g_mutex_lock (monitor->priv->lock);

	handle = g_hash_table_lookup (monitor->priv->dir_to_handle, dir);

	if (handle == NULL)
	{
		g_free (dir);
		g_free (realuri);
		return;
	}

	g_hash_table_remove (handle->files, realuri);
	if (g_hash_table_size (handle->files) == 0)
	{
		handle_free (handle);
		g_hash_table_remove (monitor->priv->dir_to_handle, dir);
	}

	g_mutex_unlock (monitor->priv->lock);

	g_free (realuri);
	g_free (dir);
#endif
}

RBFileMonitor *
rb_file_monitor_get (void)
{
	static RBFileMonitor *monitor = NULL;

	if (monitor == NULL)
		monitor = rb_file_monitor_new ();

	return monitor;
}
