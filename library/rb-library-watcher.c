/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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
#include <gconf/gconf-client.h>
#include <string.h>

#include "rb-library-watcher.h"

struct _FileWatcherPrivate
{
	GHashTable *handles;
	
	GConfClient *gconf_client;
};

/* all the different signals */
enum
{
	FILE_CREATED,
	FILE_DELETED,
	FILE_CHANGED,
	LAST_SIGNAL
};

/* globals */
static GObjectClass *parent_class = NULL;

static guint file_watcher_signals[LAST_SIGNAL] = { 0 };

/* object funtion prototypes */
static void file_watcher_class_init (FileWatcherClass *klass);
static void file_watcher_init (FileWatcher *s);
static void file_watcher_finalize (GObject *object);

/* local function protypes */
static void monitor_callback (GnomeVFSMonitorHandle *handle,
		              const gchar *monitor_uri,
                              const gchar *info_uri,
                              GnomeVFSMonitorEventType event_type,
                              FileWatcher *w);
static void add_uri (FileWatcher *w, const gchar *uri);
static void pref_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry,
	                  FileWatcher *w);
static void check_dirs (FileWatcher *w);
static gboolean remove (gchar *uri, GnomeVFSMonitorHandle *handle, gpointer data);

/**
 * file_watcher_get_type: get the GObject type of the FileWatcher 
 */
GType
file_watcher_get_type (void)
{
	static GType file_watcher_type = 0;

  	if (file_watcher_type == 0)
    	{
      		static const GTypeInfo our_info =
      		{
        		sizeof (FileWatcherClass),
        		NULL, /* base_init */
        		NULL, /* base_finalize */
        		(GClassInitFunc) file_watcher_class_init,
        		NULL, /* class_finalize */
        		NULL, /* class_data */
        		sizeof (FileWatcher),
        		0,    /* n_preallocs */
        		(GInstanceInitFunc) file_watcher_init
      		};

      		file_watcher_type = g_type_register_static (G_TYPE_OBJECT,
                				            "FileWatcher",
                                           	            &our_info, 0);
    	}

	return file_watcher_type;
}

/**
 * file_watcher_class_init: initialize the FileWatcher class
 */
static void
file_watcher_class_init (FileWatcherClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

  	parent_class = g_type_class_peek_parent (klass);

  	object_class->finalize = file_watcher_finalize;

	/* init signals */
	file_watcher_signals[FILE_CREATED] =
   		g_signal_new ("file_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (FileWatcherClass, file_created),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	file_watcher_signals[FILE_DELETED] =
   		g_signal_new ("file_deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FileWatcherClass, file_deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	file_watcher_signals[FILE_CHANGED] =
   		g_signal_new ("file_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FileWatcherClass, file_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

/**
 * file_watcher_init: intialize the FileWatcher object
 */
static void
file_watcher_init (FileWatcher *w)
{
	w->priv = g_new0 (FileWatcherPrivate, 1);

	w->priv->handles = g_hash_table_new (g_str_hash, g_str_equal);

	w->priv->gconf_client = gconf_client_get_default ();
}

/**
 * file_watcher_release_brakes: start watching
 */
void
file_watcher_release_brakes (FileWatcher *w)
{
	/* init */
	check_dirs (w);

	/* init notifier */
	gconf_client_notify_add (w->priv->gconf_client,
				 CONF_FILE_WATCHER_URIS,
				 (GConfClientNotifyFunc) pref_changed,
				 w, NULL, NULL);
}

/**
 * file_watcher_finalize: finalize the FileWatcher object
 */
static void
file_watcher_finalize (GObject *object)
{
	FileWatcher *s;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_FILE_WATCHER (object));
	
   	s = FILE_WATCHER (object);

	g_return_if_fail (s->priv != NULL);

	g_hash_table_foreach_remove (s->priv->handles, (GHRFunc) remove, NULL);
	g_hash_table_destroy (s->priv->handles);
	
	g_object_unref (G_OBJECT (s->priv->gconf_client));

	g_free (s->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * file_watcher_new: create a new FileWatcher object
 */
FileWatcher *
file_watcher_new (void)
{
 	FileWatcher *s;

	s = FILE_WATCHER (g_object_new (TYPE_FILE_WATCHER, NULL));

	g_return_val_if_fail (s->priv != NULL, NULL);

	return s;
}

/**
 * add_uri: recursively add a uri
 */
static void
add_uri (FileWatcher *w, const gchar *uri)
{
	GnomeVFSMonitorHandle *handle;
	GList *list, *li;
	GnomeVFSURI *vuri;
	GnomeVFSURI *subdir_uri;
	GnomeVFSURI *file_uri;

	gchar *tmp, *newuri, *subdir_uri_text;

	tmp = gnome_vfs_expand_initial_tilde (uri);
	vuri = gnome_vfs_uri_new (tmp);
	g_free (tmp);
	if (!gnome_vfs_uri_exists (vuri)) return;
	newuri = gnome_vfs_uri_to_string (vuri, GNOME_VFS_URI_HIDE_NONE);

	gnome_vfs_directory_list_load (&list, newuri,
				       (GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
		  			GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
					GNOME_VFS_FILE_INFO_FOLLOW_LINKS));

	for (li = list; li != NULL; li = g_list_next (li))
	{
		GnomeVFSFileInfo *info = li->data;
		gchar *filename;

		if (info->type != GNOME_VFS_FILE_TYPE_REGULAR)
		{
		        /* recurse into directories, unless they start with ".", so we 
			   avoid hidden dirs, '.' and '..' */
		        if ((info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) && 
			    (info->name) && (info->name[0] != '.'))
			{
			        subdir_uri = gnome_vfs_uri_append_path (vuri, info->name);
				subdir_uri_text = gnome_vfs_uri_to_string
					(subdir_uri, GNOME_VFS_URI_HIDE_NONE);
				gnome_vfs_uri_unref (subdir_uri);
				add_uri (w, subdir_uri_text);
				g_free (subdir_uri_text);
			}

			continue;
		}

		file_uri = gnome_vfs_uri_append_file_name (vuri, info->name);
		filename = gnome_vfs_uri_to_string (file_uri, GNOME_VFS_URI_HIDE_NONE);

		g_signal_emit (G_OBJECT (w), file_watcher_signals[FILE_CREATED], 0,
			       filename);

		g_free (filename);	
	}

	gnome_vfs_uri_unref (vuri);

	gnome_vfs_file_info_list_free (list);

	gnome_vfs_monitor_add (&handle, newuri, GNOME_VFS_MONITOR_DIRECTORY,
			       (GnomeVFSMonitorCallback) monitor_callback, w);

	g_assert (handle != NULL);

	g_hash_table_insert (w->priv->handles, newuri, handle);
}

/**
 * monitor_callback: GnomeVFSMonitor callback, emit signals if something was
 * created/changed/removed
 */
static void
monitor_callback (GnomeVFSMonitorHandle *handle,
		  const gchar *monitor_uri,
                  const gchar *info_uri,
                  GnomeVFSMonitorEventType event_type,
                  FileWatcher *w)
{
	gboolean directory;

	GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
	gnome_vfs_get_file_info (info_uri, info, (GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
						  GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE |
						  GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	directory = (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY);
	gnome_vfs_file_info_unref (info);

	switch (event_type)
	{
	case GNOME_VFS_MONITOR_EVENT_CHANGED:
		if (!directory)
		{
			g_signal_emit (G_OBJECT (w), file_watcher_signals[FILE_CHANGED], 0,
				       info_uri);
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_DELETED:
		if (!directory)
		{
			g_signal_emit (G_OBJECT (w), file_watcher_signals[FILE_DELETED], 0,
				       info_uri);
		}
		break;
	case GNOME_VFS_MONITOR_EVENT_CREATED:
		if (!directory)
		{
			g_signal_emit (G_OBJECT (w), file_watcher_signals[FILE_CREATED], 0,
				       info_uri);
		}
		else
		{
			add_uri (w, info_uri);
		}
		break;
	default:
		/* ignore all others */
		break;
	}
}

/**
 * pref_changed: gconf pref was changed, check for changes in the pref
 */
static void
pref_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry,
	      FileWatcher *w)
{
	check_dirs (w);
}

/**
 * check_dirs: check for changes in the pref
 */
static void
check_dirs (FileWatcher *w)
{
	GSList *uris, *l;

	/* load dirs */
	uris = gconf_client_get_list (w->priv->gconf_client,
				      CONF_FILE_WATCHER_URIS,
				      GCONF_VALUE_STRING,
				      NULL);

	for (l = uris; l != NULL; l = g_slist_next (l))
	{
		gchar *uri = l->data;

		add_uri (w, uri);

		g_free (uri);
	}

	g_slist_free (uris);
}

/**
 * remove: remove a watcher
 */
static gboolean
remove (gchar *uri, GnomeVFSMonitorHandle *handle, gpointer data)
{
	gnome_vfs_monitor_cancel (handle);
	g_free (uri);
	return TRUE;
}
