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

#include "rb-library-walker-thread.h"

static void rb_library_walker_thread_class_init (RBLibraryWalkerThreadClass *klass);
static void rb_library_walker_thread_init (RBLibraryWalkerThread *thread);
static void rb_library_walker_thread_finalize (GObject *object);
static void rb_library_walker_thread_set_property (GObject *object,
                                                   guint prop_id,
                                                   const GValue *value,
                                                   GParamSpec *pspec);
static void rb_library_walker_thread_get_property (GObject *object,
                                                   guint prop_id,
                                                   GValue *value,
                                                   GParamSpec *pspec);
static gpointer thread_main (RBLibraryWalkerThreadPrivate *priv);

struct RBLibraryWalkerThreadPrivate
{
	RBLibrary *library;

	GThread *thread;
	GMutex *lock;
	gboolean dead;
};

enum
{
	PROP_0,
	PROP_LIBRARY
};

static GObjectClass *parent_class = NULL;

GType
rb_library_walker_thread_get_type (void)
{
	static GType rb_library_walker_thread_type = 0;

	if (rb_library_walker_thread_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryWalkerThreadClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_walker_thread_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryWalkerThread),
			0,
			(GInstanceInitFunc) rb_library_walker_thread_init
		};

		rb_library_walker_thread_type = g_type_register_static (G_TYPE_OBJECT,
						                        "RBLibraryWalkerThread",
						                        &our_info, 0);
	}

	return rb_library_walker_thread_type;
}

static void
rb_library_walker_thread_class_init (RBLibraryWalkerThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_walker_thread_finalize;

	object_class->set_property = rb_library_walker_thread_set_property;
        object_class->get_property = rb_library_walker_thread_get_property;
		                        
        g_object_class_install_property (object_class,
                                         PROP_LIBRARY,
                                         g_param_spec_object ("library",
                                                              "Library object",
                                                              "Library object",
                                                              RB_TYPE_LIBRARY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_library_walker_thread_init (RBLibraryWalkerThread *thread)
{
	thread->priv = g_new0 (RBLibraryWalkerThreadPrivate, 1);

	thread->priv->lock = g_mutex_new ();
}

static void
rb_library_walker_thread_finalize (GObject *object)
{
	RBLibraryWalkerThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_WALKER_THREAD (object));

	thread = RB_LIBRARY_WALKER_THREAD (object);

	g_return_if_fail (thread->priv != NULL);

	g_mutex_lock (thread->priv->lock);
	thread->priv->dead = TRUE;
	g_mutex_unlock (thread->priv->lock);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibraryWalkerThread *
rb_library_walker_thread_new (RBLibrary *library)
{
	RBLibraryWalkerThread *library_walker_thread;

	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	library_walker_thread = RB_LIBRARY_WALKER_THREAD (g_object_new (RB_TYPE_LIBRARY_WALKER_THREAD,
								        "library", library,
								        NULL));

	g_return_val_if_fail (library_walker_thread->priv != NULL, NULL);

	return library_walker_thread;
}

static void
rb_library_walker_thread_set_property (GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
	RBLibraryWalkerThread *thread = RB_LIBRARY_WALKER_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		thread->priv->library = g_value_get_object (value);                    
	
		thread->priv->thread = g_thread_create ((GThreadFunc) thread_main,
							thread->priv, TRUE, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_walker_thread_get_property (GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
	RBLibraryWalkerThread *thread = RB_LIBRARY_WALKER_THREAD (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, thread->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
add_directory (RBLibraryWalkerThreadPrivate *priv,
	       const char *dir)
{
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
				add_directory (priv, subdir_uri_text);
				g_free (subdir_uri_text);
			}

			continue;
		}

		file_uri = gnome_vfs_uri_append_file_name (uri, info->name);
		filename = gnome_vfs_uri_to_string (file_uri, GNOME_VFS_URI_HIDE_NONE);

		rb_library_action_queue_add (rb_library_get_main_queue (priv->library),
					     FALSE,
					     RB_LIBRARY_ACTION_ADD_FILE,
					     filename);

		g_free (filename);
		gnome_vfs_uri_unref (file_uri);
	}

	gnome_vfs_file_info_list_free (list);

	g_free (text_uri);

	gnome_vfs_uri_unref (uri);
}

static gpointer
thread_main (RBLibraryWalkerThreadPrivate *priv)
{
	while (TRUE)
	{
		RBLibraryActionQueue *queue;

		g_mutex_lock (priv->lock);
		
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			g_free (priv);
			g_thread_exit (NULL);
		}

		queue = rb_library_get_walker_queue (priv->library);
		while (rb_library_action_queue_is_empty (queue) == FALSE)
		{
			RBLibraryActionType type;
			char *uri;
			
			rb_library_action_queue_peek_head (queue,
							   &type,
							   &uri);

			switch (type)
			{
			case RB_LIBRARY_ACTION_ADD_DIRECTORY:
				add_directory (priv, uri);
				break;
			default:
				break;
			}

			rb_library_action_queue_pop_head (queue);
		}

		g_mutex_unlock (priv->lock);

		g_usleep (10);
	}

	return NULL;
}
