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

#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rb-library-watcher.h"
#include "rb-library-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_library_watcher_class_init (RBLibraryWatcherClass *klass);
static void rb_library_watcher_init (RBLibraryWatcher *watcher);
static void rb_library_watcher_finalize (GObject *object);
static void rb_library_watcher_get_files_foreach_cb (GnomeVFSURI *uri,
					             gpointer unused,
					             void **data);
static void rb_library_watcher_load_files (RBLibraryWatcherPrivate *priv);
static void rb_library_watcher_pref_changed_cb (GConfClient *client,
				                guint cnxn_id,
				                GConfEntry *entry,
				                RBLibraryWatcher *watcher);
static void rb_library_watcher_monitor_cb (GnomeVFSMonitorHandle *handle,
			                   const char *monitor_uri,
			                   const char *info_uri,
			                   GnomeVFSMonitorEventType event_type,
			                   RBLibraryWatcherPrivate *priv);
static void rb_library_watcher_add_directory (RBLibraryWatcherPrivate *priv,
				              const char *dir);
static void rb_library_watcher_insert_file (RBLibraryWatcherPrivate *priv,
				            const char *file);
static void rb_library_watcher_remove_file (RBLibraryWatcherPrivate *priv,
				            const char *file);
static void rb_library_watcher_change_file (RBLibraryWatcherPrivate *priv,
				            const char *file);
static gpointer rb_library_watcher_thread_main (RBLibraryWatcherPrivate *priv);
static gboolean rb_library_watcher_timeout_cb (RBLibraryWatcher *watcher);

typedef struct
{
	guint signal;
	char *file;
} SignalInfo;

struct RBLibraryWatcherPrivate
{
	GHashTable *handles;
	GHashTable *files;

	guint timeout;

	GMutex *signals_lock;
	GList *signals;

	GMutex *thread_lock;

	gboolean check_dirs;

	gboolean dead;
};

enum
{
	FILE_CREATED,
	FILE_DELETED,
	FILE_CHANGED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_library_watcher_signals[LAST_SIGNAL] = { 0 };

GType
rb_library_watcher_get_type (void)
{
	static GType rb_library_watcher_type = 0;

	if (rb_library_watcher_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryWatcherClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_watcher_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryWatcher),
			0,
			(GInstanceInitFunc) rb_library_watcher_init
		};

		rb_library_watcher_type = g_type_register_static (G_TYPE_OBJECT,
						                  "RBLibraryWatcher",
						                  &our_info, 0);
	}

	return rb_library_watcher_type;
}

static void
rb_library_watcher_class_init (RBLibraryWatcherClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_watcher_finalize;

	rb_library_watcher_signals[FILE_CREATED] =
		g_signal_new ("file_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryWatcherClass, file_created),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	rb_library_watcher_signals[FILE_CHANGED] =
		g_signal_new ("file_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryWatcherClass, file_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	rb_library_watcher_signals[FILE_DELETED] =
		g_signal_new ("file_deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryWatcherClass, file_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static void
rb_library_watcher_init (RBLibraryWatcher *watcher)
{
	watcher->priv = g_new0 (RBLibraryWatcherPrivate, 1);

	watcher->priv->handles = g_hash_table_new_full (gnome_vfs_uri_hash,
							gnome_vfs_uri_hequal,
							(GDestroyNotify) gnome_vfs_uri_unref,
							(GDestroyNotify) gnome_vfs_monitor_cancel);
	watcher->priv->files = g_hash_table_new_full (gnome_vfs_uri_hash,
						      gnome_vfs_uri_hequal,
						      (GDestroyNotify) gnome_vfs_uri_unref,
						      NULL);
	
	eel_gconf_notification_add (CONF_LIBRARY_BASE_FOLDER,
				    (GConfClientNotifyFunc) rb_library_watcher_pref_changed_cb,
				    watcher);
	eel_gconf_notification_add (CONF_LIBRARY_MUSIC_FOLDERS,
				    (GConfClientNotifyFunc) rb_library_watcher_pref_changed_cb,
				    watcher);

	watcher->priv->timeout = g_timeout_add (10, (GSourceFunc) rb_library_watcher_timeout_cb, watcher);

	watcher->priv->signals_lock = g_mutex_new ();
	watcher->priv->thread_lock = g_mutex_new ();

	watcher->priv->check_dirs = TRUE;

	g_thread_create ((GThreadFunc) rb_library_watcher_thread_main, watcher->priv, TRUE, NULL);
}

static void
rb_library_watcher_finalize (GObject *object)
{
	RBLibraryWatcher *watcher;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_WATCHER (object));

	watcher = RB_LIBRARY_WATCHER (object);

	g_return_if_fail (watcher->priv != NULL);

	g_mutex_lock (watcher->priv->thread_lock);

	g_source_remove (watcher->priv->timeout);

	watcher->priv->dead = TRUE;

	g_mutex_unlock (watcher->priv->thread_lock);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryWatcher *
rb_library_watcher_new (void)
{
	RBLibraryWatcher *watcher;

	watcher = RB_LIBRARY_WATCHER (g_object_new (RB_TYPE_LIBRARY_WATCHER, NULL));

	g_return_val_if_fail (watcher->priv != NULL, NULL);

	return watcher;
}

static void
rb_library_watcher_get_files_foreach_cb (GnomeVFSURI *uri,
					 gpointer unused,
					 void **data)
{
	GList **ret = (GList **) data;

	*ret = g_list_append (*ret, gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE));
}

GList *
rb_library_watcher_get_files (RBLibraryWatcher *watcher)
{
	GList *ret = NULL;
	
	g_hash_table_foreach (watcher->priv->files,
			      (GHFunc) rb_library_watcher_get_files_foreach_cb,
			      (void **) &ret);

	return ret;
}

static void
rb_library_watcher_load_files (RBLibraryWatcherPrivate *priv)
{
	GSList *dirs, *l;
	char *base_folder;

	base_folder = eel_gconf_get_string (CONF_LIBRARY_BASE_FOLDER);
	rb_library_watcher_add_directory (priv, base_folder);
	g_free (base_folder);

	dirs = eel_gconf_get_string_list (CONF_LIBRARY_MUSIC_FOLDERS);

	for (l = dirs; l != NULL; l = g_slist_next (l))
	{
		rb_library_watcher_add_directory (priv, l->data);
	}

	g_slist_foreach (dirs, (GFunc) g_free, NULL);
	g_slist_free (dirs);
}

static void
rb_library_watcher_pref_changed_cb (GConfClient *client,
				    guint cnxn_id,
				    GConfEntry *entry,
				    RBLibraryWatcher *watcher)
{
	watcher->priv->check_dirs = TRUE;
}

static void
rb_library_watcher_monitor_cb (GnomeVFSMonitorHandle *handle,
			       const char *monitor_uri,
			       const char *info_uri,
			       GnomeVFSMonitorEventType event_type,
			       RBLibraryWatcherPrivate *priv)
{
	GnomeVFSFileInfo *info;
	gboolean directory;

	info = gnome_vfs_file_info_new ();
	gnome_vfs_get_file_info (info_uri, info, (GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
						  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
						  GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	directory = (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY);
	gnome_vfs_file_info_unref (info);

	switch (event_type)
	{
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
		if (directory == FALSE)
		{
			rb_library_watcher_change_file (priv, info_uri);
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		if (directory == FALSE)
		{
			rb_library_watcher_remove_file (priv, info_uri);
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_CREATED:
		if (directory == FALSE)
		{
			GnomeVFSURI *uri;

			uri = gnome_vfs_uri_new (info_uri);
			g_assert (uri != NULL);
			if (g_hash_table_lookup (priv->files, uri) == NULL)
			{
				rb_library_watcher_insert_file (priv, info_uri);
			}
			else
			{
				rb_library_watcher_change_file (priv, info_uri);
			}
			gnome_vfs_uri_unref (uri);
		}
		else
		{
			rb_library_watcher_add_directory (priv, info_uri);
		}
		break;
	default:
		break;
	}
}

static void
rb_library_watcher_add_directory (RBLibraryWatcherPrivate *priv,
				  const char *dir)
{
	GnomeVFSMonitorHandle *handle = NULL;
	GList *list, *l;
	GnomeVFSURI *uri, *subdir_uri, *file_uri;
	char *tmp, *text_uri, *subdir_uri_text;

	if (dir == NULL)
		return;

	tmp = gnome_vfs_expand_initial_tilde (dir);
	uri = gnome_vfs_uri_new (tmp);
	g_free (tmp);
	if (uri == NULL || gnome_vfs_uri_exists (uri) == FALSE)
		return;

	if (g_hash_table_lookup (priv->handles, uri) != NULL)
	{
		gnome_vfs_uri_unref (uri);
		return;
	}
	
	text_uri = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);

	gnome_vfs_directory_list_load (&list, text_uri,
				       (GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
		  			GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
					GNOME_VFS_FILE_INFO_FOLLOW_LINKS));

	for (l = list; l != NULL; l = g_list_next (l))
	{
		GnomeVFSFileInfo *info = l->data;
		gchar *filename;

		if (info->type != GNOME_VFS_FILE_TYPE_REGULAR)
		{
		        /* recurse into directories, unless they start with ".", so we 
			   avoid hidden dirs, '.' and '..' */
		        if ((info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) && 
			    (info->name) && (info->name[0] != '.'))
			{
			        subdir_uri = gnome_vfs_uri_append_path (uri, info->name);
				subdir_uri_text = gnome_vfs_uri_to_string
					(subdir_uri, GNOME_VFS_URI_HIDE_NONE);
				gnome_vfs_uri_unref (subdir_uri);
				rb_library_watcher_add_directory (priv, subdir_uri_text);
				g_free (subdir_uri_text);
			}

			continue;
		}

		file_uri = gnome_vfs_uri_append_file_name (uri, info->name);
		filename = gnome_vfs_uri_to_string (file_uri, GNOME_VFS_URI_HIDE_NONE);

		if (g_hash_table_lookup (priv->files, file_uri) == NULL)
			rb_library_watcher_insert_file (priv, filename);

		g_free (filename);
		gnome_vfs_uri_unref (file_uri);
	}

	gnome_vfs_file_info_list_free (list);

	if (0) //FIXME
	{
		gnome_vfs_monitor_add (&handle, text_uri, GNOME_VFS_MONITOR_DIRECTORY,
				       (GnomeVFSMonitorCallback) rb_library_watcher_monitor_cb, priv);
	}

	g_free (text_uri);

	if (handle != NULL)
		g_hash_table_insert (priv->handles, uri, handle);
}

static void
rb_library_watcher_insert_file (RBLibraryWatcherPrivate *priv,
				const char *file)
{
	GnomeVFSURI *uri;
	SignalInfo *signal;

	uri = gnome_vfs_uri_new (file);
	g_hash_table_insert (priv->files, uri, GINT_TO_POINTER (TRUE));

	g_mutex_lock (priv->signals_lock);

	signal = g_new0 (SignalInfo, 1);
	signal->signal = rb_library_watcher_signals[FILE_CREATED];
	signal->file = g_strdup (file);
	priv->signals = g_list_append (priv->signals, signal);
		
	g_mutex_unlock (priv->signals_lock);
}

static void
rb_library_watcher_change_file (RBLibraryWatcherPrivate *priv,
				const char *file)
{
	SignalInfo *signal;
			
	g_mutex_lock (priv->signals_lock);

	signal = g_new0 (SignalInfo, 1);
	signal->signal = rb_library_watcher_signals[FILE_CHANGED];
	signal->file = g_strdup (file);
	priv->signals = g_list_append (priv->signals, signal);
			
	g_mutex_unlock (priv->signals_lock);
}

static void
rb_library_watcher_remove_file (RBLibraryWatcherPrivate *priv,
				const char *file)
{
	GnomeVFSURI *uri;
	SignalInfo *signal;
	
	uri = gnome_vfs_uri_new (file);
	g_hash_table_remove (priv->files, uri);
	gnome_vfs_uri_unref (uri);

	g_mutex_lock (priv->signals_lock);

	signal = g_new0 (SignalInfo, 1);
	signal->signal = rb_library_watcher_signals[FILE_DELETED];
	signal->file = g_strdup (file);
	priv->signals = g_list_append (priv->signals, signal);
		
	g_mutex_unlock (priv->signals_lock);
}

static gboolean
rb_library_watcher_timeout_cb (RBLibraryWatcher *watcher)
{
	GList *list;
	SignalInfo *signal;

	if (watcher->priv->signals == NULL)
		return TRUE;

	g_mutex_lock (watcher->priv->signals_lock);

	list = g_list_first (watcher->priv->signals);
	signal = (SignalInfo *) list->data;
	watcher->priv->signals = g_list_remove (watcher->priv->signals, signal);

	g_mutex_unlock (watcher->priv->signals_lock);

	g_signal_emit (G_OBJECT (watcher), signal->signal, 0, signal->file);
	g_free (signal->file);
	g_free (signal);

	return TRUE;
}

static gpointer
rb_library_watcher_thread_main (RBLibraryWatcherPrivate *priv)
{
	while (TRUE)
	{
		g_mutex_lock (priv->thread_lock);
		if (priv->dead == TRUE)
		{
			GList *l;

			g_mutex_unlock (priv->thread_lock);
			g_mutex_free (priv->thread_lock);

			g_hash_table_destroy (priv->handles);
			g_hash_table_destroy (priv->files);

			for (l = priv->signals; l != NULL; l = g_list_next (l))
			{
				SignalInfo *signal = (SignalInfo *) l->data;
				g_free (signal->file);
				g_free (signal);
			}
			g_list_free (priv->signals);
			g_mutex_free (priv->signals_lock);

			g_free (priv);
			g_thread_exit (NULL);
		}

		if (priv->check_dirs == TRUE)
		{
			rb_library_watcher_load_files (priv);

			priv->check_dirs = FALSE;
		}

		g_mutex_unlock (priv->thread_lock);
		
		g_usleep (10);
	}
}
