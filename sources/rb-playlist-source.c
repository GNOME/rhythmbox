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
#include "rb-playlist.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"

#define RB_PLAYLIST_XML_VERSION "1.0"

static void rb_playlist_source_class_init (RBPlaylistSourceClass *klass);
static void rb_playlist_source_init (RBPlaylistSource *source);
static void rb_playlist_source_finalize (GObject *object);
static void rb_playlist_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_playlist_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

static void songs_view_changed_cb (RBNodeView *view, RBPlaylistSource *source);

/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBNodeView *impl_get_node_view (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static GList * impl_cut (RBSource *source);
static void impl_paste (RBSource *asource, GList *nodes);
static void impl_delete (RBSource *source);
static void impl_song_properties (RBSource *source);
static const char * impl_get_artist (RBSource *player);
static const char * impl_get_album (RBSource *player);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_show_popup (RBSource *source);

static void rb_playlist_source_songs_show_popup_cb (RBNodeView *view, RBPlaylistSource *playlist_view);
static void rb_playlist_source_drop_cb (GtkWidget *widget,
				     GdkDragContext *context,
				     gint x,
				     gint y,
				     GtkSelectionData *data,
				     guint info,
				     guint time,
				     gpointer user_data);
static void rb_playlist_source_add_list_uri (RBPlaylistSource *source,
					  GList *list);
static char * filename_from_name (const char *name);
static gboolean rb_playlist_source_periodic_save (RBPlaylistSource *source);
static void name_notify_cb (GObject *obj, const char *property, gpointer unused);


#define PLAYLIST_SOURCE_SONGS_POPUP_PATH "/popups/PlaylistSongsList"
#define PLAYLIST_SOURCE_POPUP_PATH "/popups/PlaylistSourceList"

struct RBPlaylistSourcePrivate
{
	RBLibrary *library;
	RBLibrarySource *library_source;

	RBNode *root;

	GtkWidget *vbox;
	GdkPixbuf *pixbuf;

	RBNodeView *songs;
	RBNodeFilter *filter;
	gboolean filter_changed;

	char *title;

	char *status;

	char *file;

	guint idle_save_id;

	gboolean deleted;
};

enum
{
	PROP_0,
	PROP_LIBRARY,
	PROP_LIBRARY_SOURCE,
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
rb_playlist_source_get_type (void)
{
	static GType rb_playlist_source_type = 0;

	if (rb_playlist_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlaylistSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_playlist_source_class_init,
			NULL,
			NULL,
			sizeof (RBPlaylistSource),
			0,
			(GInstanceInitFunc) rb_playlist_source_init
		};

		rb_playlist_source_type = g_type_register_static (RB_TYPE_SOURCE,
								  "RBPlaylistSource",
								  &our_info, 0);
	}

	return rb_playlist_source_type;
}

static void
rb_playlist_source_class_init (RBPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_playlist_source_finalize;

	object_class->set_property = rb_playlist_source_set_property;
	object_class->get_property = rb_playlist_source_get_property;

	source_class->impl_get_status = impl_get_status;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_get_node_view = impl_get_node_view;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_cut = impl_cut;
	source_class->impl_paste = impl_paste;
	source_class->impl_delete = impl_delete;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_artist = impl_get_artist;
	source_class->impl_get_album = impl_get_album;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_LIBRARY_SOURCE,
					 g_param_spec_object ("libsource",
							      "Library Source",
							      "Library Source",
							      RB_TYPE_LIBRARY_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_FILE,
					 g_param_spec_string ("file",
							      "playlist file",
							      "playlist file",
							      NULL,
							      G_PARAM_READWRITE));
}


static void
rb_playlist_source_songs_show_popup_cb (RBNodeView *view,
		   		     RBPlaylistSource *playlist_view)
{
	rb_bonobo_show_popup (GTK_WIDGET (view), PLAYLIST_SOURCE_SONGS_POPUP_PATH);
}

static void
rb_playlist_source_init (RBPlaylistSource *source)
{
	source->priv = g_new0 (RBPlaylistSourcePrivate, 1);

	source->priv->idle_save_id = g_idle_add ((GSourceFunc) rb_playlist_source_periodic_save,
						 source);

	g_signal_connect_object (G_OBJECT (source), "notify", G_CALLBACK (name_notify_cb), NULL, 0);
}

static void
rb_playlist_source_finalize (GObject *object)
{
	RBPlaylistSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (object));

	source = RB_PLAYLIST_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_node_unref (source->priv->root);

	g_free (source->priv->title);
	g_free (source->priv->status);

	g_free (source->priv->file);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_playlist_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
	{
		GtkWidget *dummy = gtk_tree_view_new ();
		source->priv->library = g_value_get_object (value);
		source->priv->vbox = gtk_vbox_new (FALSE, 5);

		source->priv->filter = rb_node_filter_new ();
		
		gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);
		
		source->priv->root = rb_node_new (rb_library_get_node_db (source->priv->library));
		
		source->priv->songs = rb_node_view_new (source->priv->root,
							rb_file ("rb-node-view-songs.xml"),
							source->priv->filter);
		g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
				  G_CALLBACK (rb_playlist_source_songs_show_popup_cb), source);
		
		g_signal_connect (G_OBJECT (source->priv->songs), "drag_data_received",
				  G_CALLBACK (rb_playlist_source_drop_cb), source);
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
	break;
	case PROP_LIBRARY_SOURCE:
	{
		source->priv->library_source = g_value_get_object (value);
	}
	break;
	case PROP_FILE:
		g_free (source->priv->file);

		source->priv->file = g_strdup (g_value_get_string (value));

		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, source->priv->library);
		break;
	case PROP_FILE:
		g_value_set_string (value, source->priv->file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_playlist_source_new (BonoboUIContainer *container,
		     RBLibrary *library,
		     RBLibrarySource *libsource)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_PLAYLIST_SOURCE,
					  "name", _("Unknown"),
					  "container", container,
					  "library", library,
					  "libsource", libsource,
					  NULL));

	return source;
}

RBSource *
rb_playlist_source_new_from_file (BonoboUIContainer *container,
			       RBLibrary *library,
			       RBLibrarySource *libsource,
			       const char *file)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_PLAYLIST_SOURCE,
					  "name", _("Unknown"),
					  "container", container,
					  "library", library,
					  "libsource", libsource,
					  "file", file,
					  NULL));

	rb_playlist_source_load (RB_PLAYLIST_SOURCE (source));

	return source;
}

static void
name_notify_cb (GObject *obj, const char *property, gpointer unused)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (obj);

	rb_debug ("caught notify");

	if (!strcmp (property, "name")) {
		char *file;
		char *name;
		
		g_object_get (obj, "name", &name, NULL);
		
		if (source->priv->file == NULL) {
			file = filename_from_name (name);
			g_object_set (obj, "file", file, NULL);
			g_free (file);
		}
	}
}

const char *
rb_playlist_source_get_file (RBPlaylistSource *playlist)
{
	return playlist->priv->file;
}

static const char *
impl_get_status (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	g_free (source->priv->status); 
	source->priv->status = rb_library_compute_status (source->priv->library, source->priv->root,
							  source->priv->filter);
	return source->priv->status;
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return NULL;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	return source->priv->pixbuf;
}

static RBNodeView *
impl_get_node_view (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	return source->priv->songs;
}

static const char *
impl_get_artist (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
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
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	RBNode *node;

	node = rb_node_view_get_playing_node (source->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ALBUM);
	else
		return NULL;
}

static GList *
impl_cut (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GList *sel = rb_node_view_get_selection (source->priv->songs);

	for (; sel != NULL; sel = g_list_next (sel))
		rb_node_remove_child (source->priv->root, sel->data);
	
	return sel;
}

static void
impl_paste (RBSource *asource, GList *nodes)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	for (; nodes; nodes = g_list_next (nodes))
		rb_playlist_source_add_node (source, nodes->data);
}

static void
impl_delete (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GList *sel = rb_node_view_get_selection (source->priv->songs);

	for (; sel != NULL; sel = g_list_next (sel))
		rb_node_remove_child (source->priv->root, sel->data);
}

static void
impl_song_properties (RBSource *asource)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

	song_info = rb_song_info_new (source->priv->songs);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static void
songs_view_changed_cb (RBNodeView *view, RBPlaylistSource *source)
{
	rb_debug ("got node view change");
	if (source->priv->filter_changed)
		rb_source_notify_status_changed (RB_SOURCE (source));
	source->priv->filter_changed = FALSE;
}

static gboolean
rb_playlist_source_periodic_save (RBPlaylistSource *source)
{
	if (rb_library_is_idle (source->priv->library)) {
		rb_debug ("doing periodic save");
		rb_playlist_source_save (source);
	} else {
		rb_debug ("library is busy, skipping periodic save");
	}
	source->priv->idle_save_id = g_timeout_add (60000 + (g_random_int_range (0, 15) * 1000),
						    (GSourceFunc) rb_playlist_source_periodic_save,
						    source);
	return FALSE;
}

void
rb_playlist_source_save (RBPlaylistSource *source)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *kids;
	int i;
	char *dir;
	const char *name;

	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	dir = g_build_filename (rb_dot_dir (), "playlists", NULL);
	rb_ensure_dir_exists (dir);
	g_free (dir);

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_music_playlist", NULL);
	xmlSetProp (root, "version", RB_PLAYLIST_XML_VERSION);
	g_object_get (G_OBJECT (source), "name", &name, NULL);
	xmlSetProp (root, "name", name);
	xmlDocSetRootElement (doc, root);

	kids = rb_node_get_children (source->priv->root);
	for (i = 0; i < kids->len; i++) {
		RBNode *node = g_ptr_array_index (kids, i);
		xmlNodePtr xmlnode;
		char *tmp;

		xmlnode = xmlNewChild (root, NULL, "node_pointer", NULL);

		tmp = g_strdup_printf ("%ld", rb_node_get_id (node));
		xmlSetProp (xmlnode, "id", tmp);
		g_free (tmp);
	}
	rb_node_thaw (source->priv->root);

	{
		GString *tmpname = g_string_new (source->priv->file);
		g_string_append (tmpname, ".tmp");
		xmlSaveFormatFile (tmpname->str, doc, 1);
		rename (tmpname->str, source->priv->file);
		g_string_free (tmpname, TRUE);
	}

	xmlFreeDoc (doc);
}

void
rb_playlist_source_load (RBPlaylistSource *source)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	char *name, *tmp;
	
	g_return_if_fail (RB_IS_PLAYLIST_SOURCE (source));

	if (g_file_test (source->priv->file, G_FILE_TEST_EXISTS) == FALSE)
		return;

	doc = xmlParseFile (source->priv->file);

	if (doc == NULL) {
		rb_warning_dialog (_("Failed to parse %s as playlist file"), source->priv->file);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_PLAYLIST_XML_VERSION) != 0) {
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (source->priv->file);
		return;
	}
	g_free (tmp);

	name = xmlGetProp (root, "name");

	for (child = root->children; child != NULL; child = child->next) {
		long id;
		char *tmp;
		RBNode *node;

		tmp = xmlGetProp (child, "id");
		if (tmp == NULL)
			continue;
		id = atol (tmp);
		g_free (tmp);

		node = rb_node_db_get_node_from_id (rb_library_get_node_db (source->priv->library),
						    id);

		if (node == NULL)
			continue;

		rb_playlist_source_add_node (source, node);
	}

	xmlFreeDoc (doc);

	g_object_set (G_OBJECT (source), "name", name, NULL);
	g_free (name);
}


void
rb_playlist_source_delete (RBPlaylistSource *source)
{
	g_source_remove (source->priv->idle_save_id);
	unlink (source->priv->file);
	source->priv->deleted = TRUE;
}

/* rb_playlist_view_add_node: append a node to this playlist
 */
void
rb_playlist_source_add_node (RBPlaylistSource *source,
			  RBNode *node)
{
	g_return_if_fail (source != NULL);
	g_return_if_fail (node != NULL);

	if (rb_node_has_child (source->priv->root, node) == FALSE)
		rb_node_add_child (source->priv->root, node);
}

static void
handle_songs_func (RBNode *node,
		   RBPlaylistSource *source)
{
	if (rb_library_source_eval_filter (source->priv->library_source, node))
		rb_playlist_source_add_node (source, node);
	else
		rb_debug ("node %p failed filter", node);
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GList *list;
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);
	if (data->type == gdk_atom_intern (RB_LIBRARY_DND_NODE_ID_TYPE, TRUE)) {
		long id;
		RBNode *node = NULL;
		
		id = atol (data->data);
		node = rb_node_db_get_node_from_id (rb_library_get_node_db (source->priv->library),
						    id);

		if (node != NULL)
			rb_library_handle_songs (source->priv->library,
						 node,
						 (GFunc) handle_songs_func,
						 source);
		else
			return FALSE;
	} else {
		list = gnome_vfs_uri_list_parse (data->data);

		if (list != NULL)
			rb_playlist_source_add_list_uri (source, list);
		else
			return FALSE;
	}
	return TRUE;
}

static gboolean
impl_show_popup (RBSource *source)
{
	rb_bonobo_show_popup (GTK_WIDGET (source), PLAYLIST_SOURCE_POPUP_PATH);
	return TRUE;
}

/* rb_playlist_source_drop_cb: received data from a dnd operation
 * This can be either a list of uris (from nautilus) or 
 * a list of node ids (from the node-view).
 */
static void
rb_playlist_source_drop_cb (GtkWidget *widget,
			 GdkDragContext *context,
			 gint x,
			 gint y,
			 GtkSelectionData *data,
			 guint info,
			 guint time,
			 gpointer user_data)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (user_data);
	GtkTargetList *tlist;
	GdkAtom target;

	tlist = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	impl_receive_drag (RB_SOURCE (source), data);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

/* rb_playlist_source_add_list_uri: Insert nodes from a list
 * of GnomeVFSUri.
 * */
static void 
rb_playlist_source_add_list_uri (RBPlaylistSource *source,
			    GList *list)
{
	GList *i, *uri_list = NULL;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, 
					  gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL) return;

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL) {
			RBNode *node = rb_library_get_song_by_location (source->priv->library, uri);

			/* add the node, if already present in the library */
			if (node != NULL)
				rb_playlist_source_add_node (source, node);
			else {
				GError *error = NULL;
				RBNode *node;
				rb_library_add_uri_sync (source->priv->library, uri, &error);
				
				/* FIXME error handling */
				if (error != NULL) {
					node  = rb_library_get_song_by_location (source->priv->library, uri);
					
					rb_playlist_source_add_node (source, node);
				}
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

	while (ret == NULL) {
		char *tmp2 = g_build_filename (rb_dot_dir (), "playlists", tmp, NULL);
		g_free (tmp);
		
		if (g_file_test (tmp2, G_FILE_TEST_EXISTS) == FALSE)
			ret = tmp2;
		else {
			tmp = g_strdup_printf ("%s%d.xml", asciiname, i);
			g_free (tmp2);
		}

		i++;
	}

	g_free (asciiname);

	return ret;
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBPlaylistSource *source = RB_PLAYLIST_SOURCE (asource);

	/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0) {
		rb_node_filter_empty (source->priv->filter);
		rb_node_filter_done_changing (source->priv->filter);
	} else {
		rb_node_view_select_none (source->priv->songs);

		rb_node_filter_empty (source->priv->filter);
		rb_node_filter_add_expression (source->priv->filter,
					       rb_node_filter_expression_new (source->priv->filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_done_changing (source->priv->filter);
	}
	source->priv->filter_changed = TRUE;
}

static void
playlist_iter_func (GtkTreeModel *model, GtkTreeIter *iter, char **uri, char **title)
{
	gtk_tree_model_get (model, iter, RB_TREE_MODEL_NODE_COL_LOCATION, uri,
			    RB_TREE_MODEL_NODE_COL_TITLE, title, -1);
}

void
rb_playlist_source_save_playlist (RBPlaylistSource *source, const char *uri)
{
	RBPlaylist *playlist;
	RBTreeModelNode *nodemodel;
	GError *error = NULL;
	rb_debug ("saving playlist");

	playlist = rb_playlist_new ();
	/* We use a nodemodel because it's the interface to RBPlaylist,
	 * due to compatibility with other programs (totem).
	 */
	nodemodel = rb_tree_model_node_new (source->priv->root, NULL);

	rb_playlist_write (playlist, GTK_TREE_MODEL (nodemodel),
			   playlist_iter_func, uri, &error);
	if (error != NULL)
		rb_error_dialog ("%s", error->message);
}
