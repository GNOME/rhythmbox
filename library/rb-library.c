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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <gtk/gtkmain.h>
#include <unistd.h>
#include <string.h>

#include "rb-library.h"
#include "rb-library-watcher-thread.h"
#include "rb-library-xml-thread.h"
#include "rb-library-main-thread.h"
#include "rb-library-action-queue.h"
#include "rb-node-song.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_save (RBLibrary *library);
static void rb_library_create_skels (RBLibrary *library);
static void xml_thread_done_loading_cb (RBLibraryXMLThread *thread,
			                RBLibrary *library);

struct RBLibraryPrivate
{
	RBLibraryWatcherThread *watcher_thread;
	RBLibraryXMLThread *xml_thread;
	RBLibraryMainThread *main_thread;

	RBLibraryActionQueue *queue;

	RBNode *all_genres;
	RBNode *all_artists;
	RBNode *all_albums;
	RBNode *all_songs;

	char *xml_file;
};

static GObjectClass *parent_class = NULL;

GType
rb_library_get_type (void)
{
	static GType rb_library_type = 0;

	if (rb_library_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_class_init,
			NULL,
			NULL,
			sizeof (RBLibrary),
			0,
			(GInstanceInitFunc) rb_library_init
		};

		rb_library_type = g_type_register_static (G_TYPE_OBJECT,
						          "RBLibrary",
						          &our_info, 0);
	}

	return rb_library_type;
}

static void
rb_library_class_init (RBLibraryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_finalize;
}

static void
rb_library_init (RBLibrary *library)
{
	library->priv = g_new0 (RBLibraryPrivate, 1);

	rb_node_system_init ();
	
	library->priv->xml_file = g_build_filename (rb_dot_dir (),
						    "library.xml",
						    NULL);

	rb_library_create_skels (library);

	library->priv->queue = rb_library_action_queue_new ();

	library->priv->main_thread = rb_library_main_thread_new (library);
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;
	GList *children, *l;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	g_object_unref (G_OBJECT (library->priv->main_thread));
	if (library->priv->xml_thread != NULL)
		g_object_unref (G_OBJECT (library->priv->xml_thread));
	if (library->priv->watcher_thread != NULL)
		g_object_unref (G_OBJECT (library->priv->watcher_thread));
	g_object_unref (G_OBJECT (library->priv->queue));

	rb_node_system_shutdown ();

	rb_library_save (library);

	/* unref all songs. this will set a nice chain of recursive unrefs in motion */
	children = rb_node_get_children (library->priv->all_songs);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		rb_node_unref (RB_NODE (l->data));
	}
	g_list_free (children);

	g_free (library->priv->xml_file);

	g_free (library->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBLibrary *
rb_library_new (void)
{
	RBLibrary *library;

	library = RB_LIBRARY (g_object_new (RB_TYPE_LIBRARY, NULL));

	g_return_val_if_fail (library->priv != NULL, NULL);

	return library;
}

void
rb_library_add_file (RBLibrary *library,
		     const char *uri)
{
	rb_library_action_queue_add (library->priv->queue,
				     TRUE,
				     RB_LIBRARY_ACTION_ADD_FILE,
				     uri);
}

void
rb_library_remove_node (RBLibrary *library,
			RBNode *node)
{
	rb_node_unref (RB_NODE (node));
}

RBNode *
rb_library_get_all_genres (RBLibrary *library)
{
	return library->priv->all_genres;
}

RBNode *
rb_library_get_all_artists (RBLibrary *library)
{
	return library->priv->all_artists;
}

RBNode *
rb_library_get_all_albums (RBLibrary *library)
{
	return library->priv->all_albums;
}

RBNode *
rb_library_get_all_songs (RBLibrary *library)
{
	return library->priv->all_songs;
}

static void
rb_library_create_skels (RBLibrary *library)
{
	/* create a boostrap setup */
	GValue value = { 0, };

	library->priv->all_genres  = rb_node_new (RB_NODE_TYPE_ALL_GENRES);
	library->priv->all_artists = rb_node_new (RB_NODE_TYPE_ALL_ARTISTS);
	library->priv->all_albums  = rb_node_new (RB_NODE_TYPE_ALL_ALBUMS);
	library->priv->all_songs   = rb_node_new (RB_NODE_TYPE_ALL_SONGS);
	
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, _("All"));
	rb_node_set_property (library->priv->all_genres,
			      "name",
			      &value);
	rb_node_set_property (library->priv->all_artists,
			      "name",
			      &value);
	rb_node_set_property (library->priv->all_albums,
			      "name",
			      &value);
	rb_node_set_property (library->priv->all_songs,
			      "name",
			      &value);
	g_value_unset (&value);

	rb_node_add_child (library->priv->all_genres,
			   library->priv->all_artists);
	rb_node_add_child (library->priv->all_artists,
			   library->priv->all_albums);
	rb_node_add_child (library->priv->all_albums,
			   library->priv->all_songs);
}

static void
rb_library_save (RBLibrary *library)
{
	xmlDocPtr doc;
	GList *children, *l;
	
	/* save nodes to xml */
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");
	doc->children = xmlNewDocNode (doc, NULL, "RBLibrary", NULL);

	children = rb_node_get_children (library->priv->all_genres);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_artists)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}
	g_list_free (children);

	children = rb_node_get_children (library->priv->all_artists);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_albums)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}
	g_list_free (children);

	children = rb_node_get_children (library->priv->all_albums);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		if (l->data != library->priv->all_songs)
			rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}
	g_list_free (children);

	children = rb_node_get_children (library->priv->all_songs);
	for (l = children; l != NULL; l = g_list_next (l))
	{
		rb_node_save_to_xml (RB_NODE (l->data), doc->children);
	}
	g_list_free (children);

	xmlSaveFormatFile (library->priv->xml_file, doc, 1);
}

static void
xml_thread_done_loading_cb (RBLibraryXMLThread *thread,
			    RBLibrary *library)
{
	g_object_unref (G_OBJECT (library->priv->xml_thread));
	library->priv->xml_thread = NULL;

	library->priv->watcher_thread = rb_library_watcher_thread_new (library);
}

RBLibraryActionQueue *
rb_library_get_action_queue (RBLibrary *library)
{
	return library->priv->queue;
}

void
rb_library_release_brakes (RBLibrary *library)
{
	library->priv->xml_thread = rb_library_xml_thread_new (library,
							       library->priv->xml_file);
	g_signal_connect (G_OBJECT (library->priv->xml_thread), "done_loading",
			  G_CALLBACK (xml_thread_done_loading_cb), library);
}
