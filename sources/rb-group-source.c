/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <unistd.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-group-source.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "rb-node-song.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"

#define RB_GROUP_XML_VERSION "1.0"

static void rb_group_source_class_init (RBGroupSourceClass *klass);
static void rb_group_source_init (RBGroupSource *source);
static void rb_group_source_finalize (GObject *object);
static void rb_group_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_group_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

static void songs_view_changed_cb (RBNodeView *view, RBGroupSource *source);

/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static const char *impl_get_description (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBNodeView *impl_get_node_view (RBSource *source);
static void impl_song_properties (RBSource *source);
static gboolean impl_can_pause (RBSource *player);
static gboolean impl_have_artist_album	(RBSource *player);
static const char * impl_get_artist (RBSource *player);
static const char * impl_get_album (RBSource *player);
static gboolean impl_have_url (RBSource *player);

static void rb_group_source_songs_show_popup_cb (RBNodeView *view, RBGroupSource *group_view);
static void rb_group_source_drop_cb (GtkWidget *widget,
				     GdkDragContext *context,
				     gint x,
				     gint y,
				     GtkSelectionData *data,
				     guint info,
				     guint time,
				     gpointer user_data);
static void rb_group_source_add_list_uri (RBGroupSource *source,
					  GList *list);
static char * filename_from_name (const char *name);


#define GROUP_SOURCE_SONGS_POPUP_PATH "/popups/GroupSongsList"

struct RBGroupSourcePrivate
{
	RBLibrary *library;

	RBNode *group;

	GtkWidget *vbox;
	GdkPixbuf *pixbuf;

	RBNodeView *songs;

	char *title;

	char *status;

	char *file;
	char *name;
	char *description;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
	PROP_FILE,
	PROP_NAME
};

static GObjectClass *parent_class = NULL;

/* dnd */
static const GtkTargetEntry target_table[] = 
		{ 
			{ RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST }, 
			{ RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID }
		};
static const GtkTargetEntry target_uri[] =
		{
			{ RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST }
		};

GType
rb_group_source_get_type (void)
{
	static GType rb_group_source_type = 0;

	if (rb_group_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBGroupSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_group_source_class_init,
			NULL,
			NULL,
			sizeof (RBGroupSource),
			0,
			(GInstanceInitFunc) rb_group_source_init
		};

		rb_group_source_type = g_type_register_static (RB_TYPE_SOURCE,
							       "RBGroupSource",
							       &our_info, 0);
	}

	return rb_group_source_type;
}

static void
rb_group_source_class_init (RBGroupSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_group_source_finalize;

	object_class->set_property = rb_group_source_set_property;
	object_class->get_property = rb_group_source_get_property;

	source_class->impl_get_status = impl_get_status;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_description  = impl_get_description;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_get_node_view = impl_get_node_view;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = impl_can_pause;
	source_class->impl_have_artist_album = impl_have_artist_album;
	source_class->impl_get_artist = impl_get_artist;
	source_class->impl_get_album = impl_get_album;
	source_class->impl_have_url = impl_have_url;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_FILE,
					 g_param_spec_string ("file",
							      "Group file",
							      "Group file",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Group name",
							      "Group name",
							      NULL,
							      G_PARAM_READWRITE));
}

static void
rb_group_source_songs_show_popup_cb (RBNodeView *view,
		   		     RBGroupSource *group_view)
{
	GtkWidget *menu;
	GtkWidget *window;
	
	window = gtk_widget_get_ancestor (GTK_WIDGET (view), 
					  BONOBO_TYPE_WINDOW);
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
	
	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu), 
			         GROUP_SOURCE_SONGS_POPUP_PATH);
		
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static void
rb_group_source_init (RBGroupSource *source)
{
	GtkWidget *dummy = gtk_tree_view_new ();
	source->priv = g_new0 (RBGroupSourcePrivate, 1);

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);

	source->priv->group = rb_node_new ();

	source->priv->songs = rb_node_view_new (source->priv->group,
						rb_file ("rb-node-view-songs.xml"),
						NULL);
	g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
			  G_CALLBACK (rb_group_source_songs_show_popup_cb), source);

	g_signal_connect (G_OBJECT (source->priv->songs), "drag_data_received",
			  G_CALLBACK (rb_group_source_drop_cb), source);
	gtk_drag_dest_set (GTK_WIDGET (source->priv->songs), GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table), GDK_ACTION_COPY);
	rb_node_view_enable_drag_source (source->priv->songs, target_uri, 1);

	source->priv->pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_PLAYLIST,
						       GTK_ICON_SIZE_MENU,
						       NULL);
	gtk_widget_destroy (dummy);

	g_signal_connect (G_OBJECT (source->priv->songs),
			  "changed",
			  G_CALLBACK (songs_view_changed_cb),
			  source);

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), GTK_WIDGET (source->priv->songs));

	gtk_widget_show_all (GTK_WIDGET (source));
			
}

static void
rb_group_source_finalize (GObject *object)
{
	RBGroupSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_GROUP_SOURCE (object));

	source = RB_GROUP_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_node_unref (source->priv->group);

	g_free (source->priv->title);
	g_free (source->priv->status);

	g_free (source->priv->name);
	g_free (source->priv->file);
	g_free (source->priv->description);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_group_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBGroupSource *source = RB_GROUP_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		source->priv->library = g_value_get_object (value);
		break;
	case PROP_FILE:
		g_free (source->priv->file);

		source->priv->file = g_strdup (g_value_get_string (value));

/* 		g_object_set (G_OBJECT (rb_view_get_sidebar_button (RB_VIEW (source))), */
/* 			      "unique_id", source->priv->file, */
/* 			      NULL); */
		break;
	case PROP_NAME:
		{
			char *file;
			
			g_free (source->priv->name);
			g_free (source->priv->description);
		
			source->priv->name = g_strdup (g_value_get_string (value));
			source->priv->description = g_strdup_printf ("\"%s\" Group", source->priv->name);

			if (source->priv->file == NULL)
			{
				file = filename_from_name (source->priv->name);
				g_object_set (object, "file", file, NULL);
				g_free (file);
			}
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_group_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBGroupSource *source = RB_GROUP_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, source->priv->library);
		break;
	case PROP_FILE:
		g_value_set_string (value, source->priv->file);
		break;
	case PROP_NAME:
		g_value_set_string (value, source->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_group_source_new (BonoboUIContainer *container,
		     RBLibrary *library)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_GROUP_SOURCE,
					  "ui-file", "net-rhythmbox-group-view.xml",
					  "ui-name", "GroupView",
					  "container", container,
					  "library", library,
					  NULL));

	return source;
}

RBSource *
rb_group_source_new_from_file (BonoboUIContainer *container,
			       RBLibrary *library,
			       const char *file)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_GROUP_SOURCE,
					  "ui-file", "net-rhythmbox-group-view.xml",
					  "ui-name", "GroupView",
					  "container", container,
					  "library", library,
					  "file", file,
					  NULL));

	rb_group_source_load (RB_GROUP_SOURCE (source));

	return source;
}

void
rb_group_source_set_name (RBGroupSource *group,
		        const char *name)
{
	g_object_set (G_OBJECT (group),
		      "name", name,
		      NULL);
}

const char *
rb_group_source_get_file (RBGroupSource *group)
{
	return group->priv->file;
}

static const char *
impl_get_description (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);
	return source->priv->description;
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return NULL;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);

	return source->priv->pixbuf;
}

static RBNodeView *
impl_get_node_view (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);

	return source->priv->songs;
}

gboolean
impl_can_pause (RBSource *source)
{
	return TRUE;
}

static gboolean
impl_have_artist_album (RBSource *source)
{
	return TRUE;
}

static const char *
impl_get_artist (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);
	RBNode *node;

	node = rb_node_view_get_playing_node (source->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ARTIST);
	else
		return NULL;
}

static const char *
impl_get_album (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);
	RBNode *node;

	node = rb_node_view_get_playing_node (source->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ALBUM);
	else
		return NULL;
}

static gboolean
impl_have_url (RBSource *source)
{
	return FALSE;
}

static const char *
impl_get_status (RBSource *asource)
{
	return ""; /* FIXME; factor out status code from library */
}

static void
impl_song_properties (RBSource *asource)
{
	RBGroupSource *source = RB_GROUP_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

	song_info = rb_song_info_new (source->priv->songs);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static void
songs_view_changed_cb (RBNodeView *view, RBGroupSource *source)
{
	rb_debug ("got node view change");
	rb_source_notify_status_changed (RB_SOURCE (source));
}

/* static void */
/* song_update_statistics (RBGroupSource *source) */
/* { */
/* 	RBNode *node; */

/* 	node = rb_node_view_get_playing_node (source->priv->songs); */
/* 	rb_node_update_play_statistics (node); */
/* } */

/* static GList * */
/* rb_group_source_cut (RBViewClipboard *clipboard) */
/* { */
/* 	RBGroupSource *source = RB_GROUP_SOURCE (clipboard); */
/* 	GList *sel, *l; */

/* 	sel = g_list_copy (rb_node_view_get_selection (source->priv->songs)); */
/* 	for (l = sel; l != NULL; l = g_list_next (l)) */
/* 	{ */
/* 		rb_node_remove_child (source->priv->group, RB_NODE (l->data)); */
/* 	} */
	
/* 	return sel; */
/* } */

/* static GList * */
/* rb_group_source_copy (RBViewClipboard *clipboard) */
/* { */
/* 	RBGroupSource *source = RB_GROUP_SOURCE (clipboard); */

/* 	return g_list_copy (rb_node_view_get_selection (source->priv->songs)); */
/* } */

/* static void */
/* rb_group_source_paste (RBViewClipboard *clipboard, */
/* 		     GList *nodes) */
/* { */
/* 	RBGroupSource *source = RB_GROUP_SOURCE (clipboard); */
/* 	GList *l; */

/* 	for (l = nodes; l != NULL; l = g_list_next (l)) */
/* 	{ */
/* 		rb_group_source_add_node (view, RB_NODE (l->data)); */
/* 	} */
/* } */

/* static void */
/* rb_group_source_delete (RBViewClipboard *clipboard) */
/* { */
/* 	RBGroupSource *source = RB_GROUP_SOURCE (clipboard); */
/* 	GList *sel, *l; */

/* 	sel = g_list_copy (rb_node_view_get_selection (source->priv->songs)); */
/* 	for (l = sel; l != NULL; l = g_list_next (l)) */
/* 	{ */
/* 		rb_node_remove_child (source->priv->group, RB_NODE (l->data)); */
/* 	} */
/* 	g_list_free (sel); */
/* } */

void
rb_group_source_save (RBGroupSource *source)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *kids;
	int i;
	char *dir;

	g_return_if_fail (RB_IS_GROUP_SOURCE (source));

	dir = g_build_filename (rb_dot_dir (), "groups", NULL);
	rb_ensure_dir_exists (dir);
	g_free (dir);

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_music_group", NULL);
	xmlSetProp (root, "version", RB_GROUP_XML_VERSION);
	xmlSetProp (root, "name", source->priv->name);
	xmlDocSetRootElement (doc, root);

	kids = rb_node_get_children (source->priv->group);
	for (i = 0; i < kids->len; i++)
	{
		RBNode *node = g_ptr_array_index (kids, i);
		xmlNodePtr xmlnode;
		char *tmp;

		xmlnode = xmlNewChild (root, NULL, "node_pointer", NULL);

		tmp = g_strdup_printf ("%ld", rb_node_get_id (node));
		xmlSetProp (xmlnode, "id", tmp);
		g_free (tmp);
	}
	rb_node_thaw (source->priv->group);

	xmlSaveFormatFile (source->priv->file, doc, 1);
	xmlFreeDoc (doc);
}

void
rb_group_source_load (RBGroupSource *source)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	char *name, *tmp;
	
	g_return_if_fail (RB_IS_GROUP_SOURCE (source));

	if (g_file_test (source->priv->file, G_FILE_TEST_EXISTS) == FALSE)
		return;

	doc = xmlParseFile (source->priv->file);

	if (doc == NULL)
	{
		rb_warning_dialog (_("Failed to parse %s as group file"), source->priv->file);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_GROUP_XML_VERSION) != 0)
	{
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (source->priv->file);
		return;
	}
	g_free (tmp);

	name = xmlGetProp (root, "name");

	for (child = root->children; child != NULL; child = child->next)
	{
		long id;
		char *tmp;
		RBNode *node;

		tmp = xmlGetProp (child, "id");
		if (tmp == NULL)
			continue;
		id = atol (tmp);
		g_free (tmp);

		node = rb_node_get_from_id (id);

		if (node == NULL)
			continue;

		rb_group_source_add_node (source, node);
	}

	xmlFreeDoc (doc);

	rb_group_source_set_name (source, name);
	g_free (name);
}


void
rb_group_source_remove_file (RBGroupSource *source)
{
	unlink (source->priv->file);
}

/* rb_group_view_add_node: append a node to this group
 */
void
rb_group_source_add_node (RBGroupSource *source,
			  RBNode *node)
{
	g_return_if_fail (source != NULL);
	g_return_if_fail (node != NULL);

	if (rb_node_has_child (source->priv->group, node) == FALSE)
		rb_node_add_child (source->priv->group, node);
}

static void
add_uri (const char *uri,
	 RBGroupSource *source)
{
	RBNode *node;

	node = rb_library_get_song_by_location (source->priv->library, uri);

	if (node != NULL)
	{
		rb_group_source_add_node (source, node);
	}
}

static void
dnd_add_handled_cb (RBLibraryAction *action,
		    RBGroupSource *source)
{
	char *uri;
	RBLibraryActionType type;

	rb_library_action_get (action,
			       &type,
			       &uri);
	
	switch (type)
	{
	case RB_LIBRARY_ACTION_ADD_FILE:
		{
			RBNode *node;

			node = rb_library_get_song_by_location (source->priv->library, uri);

			if (node != NULL)
			{
				rb_group_source_add_node (source, node);
			}
		}
		break;
	case RB_LIBRARY_ACTION_ADD_DIRECTORY:
		{
			rb_uri_handle_recursively (uri,
						   (GFunc) add_uri,
						   NULL,
						   NULL,
						   source);
		}
		break;
	default:
		break;
	}
}

static void
handle_songs_func (RBNode *node,
		   RBGroupSource *source)
{
	rb_group_source_add_node (source, node);
}

/* rb_group_source_drop_cb: received data from a dnd operation
 * This can be either a list of uris (from nautilus) or 
 * a list of node ids (from the node-view).
 */
static void
rb_group_source_drop_cb (GtkWidget *widget,
		       GdkDragContext *context,
		       gint x,
		       gint y,
		       GtkSelectionData *data,
		       guint info,
		       guint time,
		       gpointer user_data)
{
	RBGroupSource *source = RB_GROUP_SOURCE (user_data);
	GList *list;
	GtkTargetList *tlist;
	GdkAtom target;

	tlist = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	if (info == RB_LIBRARY_DND_NODE_ID)
	{
		long id;
		RBNode *node = NULL;

		id = atol (data->data);
		node = rb_node_get_from_id (id);

		if (node != NULL)
			rb_library_handle_songs (source->priv->library,
						 node,
						 (GFunc) handle_songs_func,
						 source);
	}
	else if (info == RB_LIBRARY_DND_URI_LIST)
	{
		list = gnome_vfs_uri_list_parse (data->data);
		if (list != NULL)
		{
			rb_group_source_add_list_uri (source, list);
		}
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

/* rb_group_source_add_list_uri: Insert nodes from a list
 * of GnomeVFSUri.
 * */
static void 
rb_group_source_add_list_uri (RBGroupSource *source,
			    GList *list)
{
	GList *i, *uri_list = NULL;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = g_list_next (i))
	{
		uri_list = g_list_append (uri_list, 
					  gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));
	}
	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL) return;

	for (i = uri_list; i != NULL; i = i->next)
	{
		char *uri = i->data;

		if (uri != NULL)
		{
			RBNode *node = rb_library_get_song_by_location (source->priv->library, uri);

			/* add the node, if already present in the library */
			if (node != NULL)
			{
				rb_group_source_add_node (source, node);
			}
			else
			{
				RBLibraryAction *action = rb_library_add_uri (source->priv->library, uri);
				g_signal_connect_object (G_OBJECT (action),
						         "handled",
						         G_CALLBACK (dnd_add_handled_cb),
						         G_OBJECT (source),
							 0);
			}
		}

		g_free (uri);
	}

	g_list_free (uri_list);
}

static char *
filename_from_name (const char *name)
{
	char *tmp, *ret = NULL, *asciiname;
	int i = 0;

	g_assert (name != NULL);

	asciiname = g_filename_from_utf8 (name, -1, NULL, NULL, NULL);

	tmp = g_strconcat (asciiname, ".xml", NULL);

	while (ret == NULL)
	{
		char *tmp2 = g_build_filename (rb_dot_dir (), "groups", tmp, NULL);
		g_free (tmp);
		
		if (g_file_test (tmp2, G_FILE_TEST_EXISTS) == FALSE)
			ret = tmp2;
		else
		{
			tmp = g_strdup_printf ("%s%d.xml", asciiname, i);
			g_free (tmp2);
		}

		i++;
	}

	g_free (asciiname);

	return ret;
}
