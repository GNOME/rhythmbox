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
#include <gtk/gtkmain.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkhpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtktooltips.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-library-view.h"
#include "rb-node-song.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"
#include "rb-node-filter.h"
#include "rb-search-entry.h"
#include "rb-preferences.h"

static void rb_library_view_class_init (RBLibraryViewClass *klass);
static void rb_library_view_init (RBLibraryView *view);
static void rb_library_view_finalize (GObject *object);
static void rb_library_view_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_library_view_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void album_node_selected_cb (RBNodeView *view,
			            RBNode *node,
			            RBLibraryView *libview);
static void genre_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBLibraryView *libview);
static void artist_node_selected_cb (RBNodeView *view,
			             RBNode *node,
			             RBLibraryView *libview);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBLibraryView *view);
static void rb_library_view_drop_cb (GtkWidget        *widget,
				     GdkDragContext   *context,
				     gint              x,
				     gint              y,
				     GtkSelectionData *data,
				     guint             info,
				     guint             time,
				     gpointer          user_data);
static void rb_library_view_search_cb (RBSearchEntry *search,
			               const char *search_text,
			               RBLibraryView *view);
static void artists_filter (RBLibraryView *view,
	                    RBNode *genre);
static void albums_filter (RBLibraryView *view,
			   RBNode *genre,
	                   RBNode *artist);
static void songs_filter (RBLibraryView *view,
	                  RBNode *genre,
	                  RBNode *artist,
			  RBNode *album);
static void songs_node_activated_cb (RBNodeView *nodeview,
			             RBNode *node,
			             RBLibraryView *view);
static void play_album_cb (GtkWidget *button,
	                   RBLibraryView *view);
static void play_album_later_cb (GtkWidget *button,
	                         RBLibraryView *view);
static void play_song_cb (GtkWidget *button,
	                  RBLibraryView *view);
static void play_song_later_cb (GtkWidget *button,
	                        RBLibraryView *view);
static void songs_changed_cb (RBNodeView *nodeview,
		              RBLibraryView *view);
static void check_button_sensitivity (RBLibraryView *view);

#define CONF_STATE_PANED_POSITION "/apps/rhythmbox/state/library/paned_position"

struct RBLibraryViewPrivate
{
	RB *rb;

	RBLibrary *library;

	GtkTooltips *tooltips;

	GtkWidget *browser;

	GtkWidget *play_album;
	GtkWidget *play_album_later;
	GtkWidget *play_song;
	GtkWidget *play_song_later;

	RBNodeView *genres;
	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	RBSearchEntry *search;

	GtkWidget *paned;
	int paned_position;

	RBNodeFilter *artists_filter;
	RBNodeFilter *songs_filter;
	RBNodeFilter *albums_filter;

	gboolean changing_artist;
	gboolean changing_genre;

	guint views_notif;
};

enum
{
	PROP_0,
	PROP_RB
};

static GObjectClass *parent_class = NULL;

/* dnd */
static const GtkTargetEntry target_uri [] =
		{ { RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST } };
static const GtkTargetEntry target_id [] =
		{ { RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID } };

GType
rb_library_view_get_type (void)
{
	static GType rb_library_view_type = 0;

	if (rb_library_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_view_class_init,
			NULL,
			NULL,
			sizeof (RBLibraryView),
			0,
			(GInstanceInitFunc) rb_library_view_init
		};

		rb_library_view_type = g_type_register_static (GTK_TYPE_HBOX,
							       "RBLibraryView",
							       &our_info, 0);
	}

	return rb_library_view_type;
}

static void
rb_library_view_class_init (RBLibraryViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_view_finalize;

	object_class->set_property = rb_library_view_set_property;
	object_class->get_property = rb_library_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_RB,
					 g_param_spec_object ("rb",
							      "Rhythmbox",
							      "Rhythmbox object",
							      RB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
update_browser_views_visibility (RBLibraryView *view)
{
	int views;
	GtkWidget *genres = GTK_WIDGET (view->priv->genres);
	GtkWidget *artists = GTK_WIDGET (view->priv->artists);
	GtkWidget *albums = GTK_WIDGET (view->priv->albums);

	views = eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS);

	switch (views)
	{
		case 0:
			gtk_widget_hide (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);

			rb_node_view_select_node (view->priv->genres,
						  rb_library_get_all_artists
						  (view->priv->library));
		break;
		case 1:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_hide (albums);

			rb_node_view_select_node (view->priv->albums,
						  rb_library_get_all_songs
						  (view->priv->library));
		break;
		case 2:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
		break;
	}
}

static void
browser_views_notifier (GConfClient *client,
			guint cnxn_id,
			GConfEntry *entry,
			RBLibraryView *view)
{
	update_browser_views_visibility (view);
}

static void
rb_library_view_init (RBLibraryView *view)
{
	view->priv = g_new0 (RBLibraryViewPrivate, 1);
}

static void
rb_library_view_finalize (GObject *object)
{
	RBLibraryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_VIEW (object));

	view = RB_LIBRARY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	/* save state */
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION, view->priv->paned_position);

	g_object_unref (G_OBJECT (view->priv->artists_filter));
	g_object_unref (G_OBJECT (view->priv->songs_filter));
	g_object_unref (G_OBJECT (view->priv->albums_filter));

	eel_gconf_notification_remove (view->priv->views_notif);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_library_view_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (object);

	switch (prop_id)
	{
	case PROP_RB:
		view->priv->rb = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_view_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (object);

	switch (prop_id)
	{
	case PROP_RB:
		g_value_set_object (value, view->priv->rb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_view_construct (RBLibraryView *view)
{
	GtkWidget *buttonbox, *vbox;

	vbox = gtk_vbox_new (FALSE, 5);

	view->priv->tooltips = gtk_tooltips_new ();

	view->priv->search = rb_search_entry_new ();

	g_signal_connect (G_OBJECT (view->priv->search),
			  "search",
			  G_CALLBACK (rb_library_view_search_cb),
			  view);

	view->priv->views_notif = eel_gconf_notification_add
		(CONF_UI_BROWSER_VIEWS, (GConfClientNotifyFunc) browser_views_notifier, view);

	view->priv->library = rb_get_library (view->priv->rb);

	/* the buttons */
	buttonbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), buttonbox,
			    FALSE, FALSE, 0);

	view->priv->play_album = gtk_button_new_with_mnemonic (_("_Play Album"));
	gtk_tooltips_set_tip (view->priv->tooltips,
			      view->priv->play_album,
			      _("Start playing this album"), NULL);
	g_signal_connect (G_OBJECT (view->priv->play_album),
			  "clicked", G_CALLBACK (play_album_cb), view);
	gtk_box_pack_start (GTK_BOX (buttonbox), view->priv->play_album,
			    FALSE, FALSE, 0);
	view->priv->play_album_later = gtk_button_new_with_mnemonic (_("Play Album _Later"));
	gtk_tooltips_set_tip (view->priv->tooltips,
			      view->priv->play_album_later,
			      _("Enqueue this album for later playback"), NULL);
	g_signal_connect (G_OBJECT (view->priv->play_album_later),
			  "clicked", G_CALLBACK (play_album_later_cb), view);
	gtk_box_pack_start (GTK_BOX (buttonbox), view->priv->play_album_later,
			    FALSE, FALSE, 0);

	view->priv->play_song_later = gtk_button_new_with_mnemonic (_("Play Song Later"));
	gtk_tooltips_set_tip (view->priv->tooltips,
			      view->priv->play_song_later,
			      _("Enqueue the selected song(s) for later playback"), NULL);
	g_signal_connect (G_OBJECT (view->priv->play_song_later),
			  "clicked", G_CALLBACK (play_song_later_cb), view);
	gtk_box_pack_end (GTK_BOX (buttonbox), view->priv->play_song_later,
			  FALSE, FALSE, 0);
	view->priv->play_song = gtk_button_new_with_mnemonic (_("Play _Song"));
	gtk_tooltips_set_tip (view->priv->tooltips,
			      view->priv->play_song,
			      _("Start playing the selected song(s)"), NULL);
	g_signal_connect (G_OBJECT (view->priv->play_song),
			  "clicked", G_CALLBACK (play_song_cb), view);
	gtk_box_pack_end (GTK_BOX (buttonbox), view->priv->play_song,
			  FALSE, FALSE, 0);

	view->priv->paned = gtk_hpaned_new ();

	view->priv->browser = gtk_vbox_new (FALSE, 5);

	/* Initialize the filters */
	view->priv->artists_filter = rb_node_filter_new ();
	view->priv->songs_filter = rb_node_filter_new ();
	view->priv->albums_filter = rb_node_filter_new ();

	/* set up genres treeview */
	view->priv->genres = rb_node_view_new (rb_library_get_all_genres (view->priv->library),
					       rb_file ("rb-node-view-genres.xml"),
					       NULL);
	g_signal_connect (G_OBJECT (view->priv->genres),
			  "node_selected",
			  G_CALLBACK (genre_node_selected_cb),
			  view);

	gtk_box_pack_start (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->genres), TRUE, TRUE, 0);

	/* set up artist treeview */
	view->priv->artists = rb_node_view_new (rb_library_get_all_artists (view->priv->library),
				                rb_file ("rb-node-view-artists.xml"),
						view->priv->artists_filter);
	g_signal_connect (G_OBJECT (view->priv->artists),
			  "node_selected",
			  G_CALLBACK (artist_node_selected_cb),
			  view);

	gtk_box_pack_start (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->artists), TRUE, TRUE, 0);

	/* set up albums treeview */
	view->priv->albums = rb_node_view_new (rb_library_get_all_albums (view->priv->library),
				               rb_file ("rb-node-view-albums.xml"),
					       view->priv->albums_filter);
	g_signal_connect (G_OBJECT (view->priv->albums),
			  "node_selected",
			  G_CALLBACK (album_node_selected_cb),
			  view);

	gtk_box_pack_start (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->albums), TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (view->priv->browser), GTK_WIDGET (view->priv->search), FALSE, TRUE, 0);

	gtk_paned_pack1 (GTK_PANED (view->priv->paned), view->priv->browser, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (view->priv->paned), vbox, TRUE, FALSE);

	/* set up songs tree view */
	view->priv->songs = rb_node_view_new (rb_library_get_all_songs (view->priv->library),
				              rb_file ("rb-node-view-songs.xml"),
					      view->priv->songs_filter);
	g_signal_connect (G_OBJECT (view->priv->songs),
			  "node_activated",
			  G_CALLBACK (songs_node_activated_cb),
			  view);
	g_signal_connect (G_OBJECT (view->priv->songs),
			  "changed",
			  G_CALLBACK (songs_changed_cb),
			  view);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (view->priv->songs),
			    TRUE, TRUE, 0);

	/* Drag'n'Drop for songs view */
	g_signal_connect (G_OBJECT (view->priv->songs), "drag_data_received",
			  G_CALLBACK (rb_library_view_drop_cb), view);
	gtk_drag_dest_set (GTK_WIDGET (view->priv->songs), GTK_DEST_DEFAULT_ALL,
			   target_uri, 1, GDK_ACTION_COPY);
	rb_node_view_enable_drag_source (view->priv->songs, target_id, 1);

	/* Drag'n'Drop for albums view */
	g_signal_connect (G_OBJECT (view->priv->albums), "drag_data_received",
			  G_CALLBACK (rb_library_view_drop_cb), view);
	gtk_drag_dest_set (GTK_WIDGET (view->priv->albums), GTK_DEST_DEFAULT_ALL,
			   target_uri, 1, GDK_ACTION_COPY);
	rb_node_view_enable_drag_source (view->priv->albums, target_id, 1);

	/* Drag'n'Drop for artists view */
	g_signal_connect (G_OBJECT (view->priv->artists), "drag_data_received",
			  G_CALLBACK (rb_library_view_drop_cb), view);
	gtk_drag_dest_set (GTK_WIDGET (view->priv->artists), GTK_DEST_DEFAULT_ALL,
			   target_uri, 1, GDK_ACTION_COPY);
	rb_node_view_enable_drag_source (view->priv->artists, target_id, 1);

	/* this gets emitted when the paned thingie is moved */
	g_signal_connect (G_OBJECT (view->priv->songs),
			  "size_allocate",
			  G_CALLBACK (paned_size_allocate_cb),
			  view);

	gtk_container_add (GTK_CONTAINER (view), view->priv->paned);

	view->priv->paned_position = eel_gconf_get_integer (CONF_STATE_PANED_POSITION);

	gtk_widget_show_all (GTK_WIDGET (view));

	gtk_paned_set_position (GTK_PANED (view->priv->paned), view->priv->paned_position);

	update_browser_views_visibility (view);

	rb_node_view_select_node (view->priv->artists,
			          rb_library_get_all_albums (view->priv->library));

	check_button_sensitivity (view);
}

RBLibraryView *
rb_library_view_new (RB *rb)
{
	RBLibraryView *view;

	view = RB_LIBRARY_VIEW (g_object_new (RB_TYPE_LIBRARY_VIEW,
					      "rb", rb,
				              NULL));

	rb_library_view_construct (view);

	return view;
}

static RBNode *
ensure_node_selection (RBNodeView *view,
		       RBNode *all_node,
		       gboolean *changing_flag)
{
	GList *selection = rb_node_view_get_selection (view);
	RBNode *ret;

	if (selection == NULL)
	{
		*changing_flag = TRUE;
		rb_node_view_select_node (view, all_node);
		*changing_flag = FALSE;
		selection = rb_node_view_get_selection (view);
	}

	ret = RB_NODE (selection->data);

	g_list_free (selection);

	return ret;
}


static void
genre_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibraryView *libview)
{
	if (libview->priv->changing_genre == TRUE)
		return;

	artists_filter (libview, node);
	rb_node_view_select_node (libview->priv->artists,
				  rb_library_get_all_albums (libview->priv->library));

	rb_search_entry_clear (libview->priv->search);
}

static void
artist_node_selected_cb (RBNodeView *view,
			 RBNode *node,
			 RBLibraryView *libview)
{
	RBNode *genre;

	if (libview->priv->changing_artist == TRUE)
		return;

	genre = ensure_node_selection (libview->priv->genres,
			               rb_library_get_all_artists (libview->priv->library),
				       &libview->priv->changing_genre);

	albums_filter (libview, genre, node);
	rb_node_view_select_node (libview->priv->albums,
				  rb_library_get_all_songs (libview->priv->library));

	rb_search_entry_clear (libview->priv->search);
}

static void
album_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibraryView *libview)
{
	RBNode *artist;
	RBNode *genre;

	genre = ensure_node_selection (libview->priv->genres,
			               rb_library_get_all_artists (libview->priv->library),
				       &libview->priv->changing_genre);
	artist = ensure_node_selection (libview->priv->artists,
			                rb_library_get_all_albums (libview->priv->library),
				        &libview->priv->changing_artist);

	rb_search_entry_clear (libview->priv->search);

	songs_filter (libview,
		      genre,
		      artist,
		      node);
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBLibraryView *view)
{
	view->priv->paned_position = gtk_paned_get_position (GTK_PANED (view->priv->paned));
}

static void
rb_library_view_drop_cb (GtkWidget *widget,
			 GdkDragContext *context,
			 gint x,
			 gint y,
			 GtkSelectionData *data,
			 guint info,
			 guint time,
			 gpointer user_data)
{
	RBLibraryView *view = RB_LIBRARY_VIEW (user_data);
	GList *list, *uri_list, *i;
	GtkTargetList *tlist;
	gboolean ret;

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
	{
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));
	}
	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
	{
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = uri_list; i != NULL; i = i->next)
	{
		char *uri = i->data;

		if (uri != NULL)
		{
			rb_library_add_uri (view->priv->library, uri);
		}

		g_free (uri);
	}

	g_list_free (uri_list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
rb_library_view_search_cb (RBSearchEntry *search,
			   const char *search_text,
			   RBLibraryView *view)
{
	GDK_THREADS_ENTER ();

	/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0)
	{
		rb_node_view_select_node (view->priv->genres,
				          rb_library_get_all_artists (view->priv->library));
	}
	else
	{
		rb_node_view_select_none (view->priv->genres);
		rb_node_view_select_none (view->priv->artists);
		rb_node_view_select_none (view->priv->albums);

		artists_filter (view, rb_library_get_all_artists (view->priv->library));
		albums_filter (view, rb_library_get_all_artists (view->priv->library),
			             rb_library_get_all_albums (view->priv->library));

		rb_node_filter_empty (view->priv->songs_filter);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_ARTIST,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_ALBUM,
									      search_text),
					       0);
		rb_node_filter_add_expression (view->priv->songs_filter,
					       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_GENRE,
									      search_text),
					       0);

		rb_node_filter_done_changing (view->priv->songs_filter);
	}

	GDK_THREADS_LEAVE ();
}

static void
artists_filter (RBLibraryView *view,
	        RBNode *genre)
{
	rb_node_filter_empty (view->priv->artists_filter);
	rb_node_filter_add_expression (view->priv->artists_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_albums (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->artists_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      genre),
				       0);
	rb_node_filter_done_changing (view->priv->artists_filter);
}

static void
albums_filter (RBLibraryView *view,
	       RBNode *genre,
	       RBNode *artist)
{
	rb_node_filter_empty (view->priv->albums_filter);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_CHILD_PROP_EQUALS,
								      RB_NODE_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (view->priv->library)),
				       1);
	rb_node_filter_add_expression (view->priv->albums_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      artist),
				       1);
	rb_node_filter_done_changing (view->priv->albums_filter);
}

static void
songs_filter (RBLibraryView *view,
	      RBNode *genre,
	      RBNode *artist,
	      RBNode *album)
{
	rb_node_filter_empty (view->priv->songs_filter);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (view->priv->library)),
				       0);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      artist, rb_library_get_all_albums (view->priv->library)),
				       1);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_ARTIST, artist),
				       1);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      album, rb_library_get_all_songs (view->priv->library)),
				       2);
	rb_node_filter_add_expression (view->priv->songs_filter,
				       rb_node_filter_expression_new (RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_ALBUM, album),
				       2);
	rb_node_filter_done_changing (view->priv->songs_filter);
}

static void
songs_node_activated_cb (RBNodeView *nodeview,
			 RBNode *node,
			 RBLibraryView *view)
{
	rb_player_queue_song (rb_get_player (view->priv->rb), node, TRUE, TRUE);
}

static void
play_song_cb (GtkWidget *button,
	      RBLibraryView *view)
{
	GList *sel, *l;
	gboolean once = TRUE;

	sel = rb_node_view_get_selection (view->priv->songs);

	for (l = sel; l != NULL; l = g_list_next (l)) {
		RBNode *node;

		node = (RBNode *) l->data;

		rb_player_queue_song (rb_get_player (view->priv->rb), node, TRUE, once);

		once = FALSE;
	}

	g_list_free (sel);
}

static void
play_song_later_cb (GtkWidget *button,
	            RBLibraryView *view)
{
	GList *sel, *l;

	sel = rb_node_view_get_selection (view->priv->songs);

	for (l = sel; l != NULL; l = g_list_next (l)) {
		RBNode *node;

		node = (RBNode *) l->data;

		rb_player_queue_song (rb_get_player (view->priv->rb), node, FALSE, FALSE);
	}

	g_list_free (sel);
}

static void
play_album_cb (GtkWidget *button,
	       RBLibraryView *view)
{
	GList *sel, *l;
	gboolean once = TRUE;

	sel = rb_node_view_get_rows (view->priv->songs);

	for (l = sel; l != NULL; l = g_list_next (l)) {
		RBNode *node;

		node = (RBNode *) l->data;

		rb_player_queue_song (rb_get_player (view->priv->rb), node, TRUE, once);

		once = FALSE;
	}

	g_list_free (sel);
}

static void
play_album_later_cb (GtkWidget *button,
	             RBLibraryView *view)
{
	GList *sel, *l;

	sel = rb_node_view_get_rows (view->priv->songs);

	for (l = sel; l != NULL; l = g_list_next (l)) {
		RBNode *node;

		node = (RBNode *) l->data;

		rb_player_queue_song (rb_get_player (view->priv->rb), node, FALSE, FALSE);
	}

	g_list_free (sel);
}

static void
songs_changed_cb (RBNodeView *nodeview,
		  RBLibraryView *view)
{
	check_button_sensitivity (view);
}

static void
check_button_sensitivity (RBLibraryView *view)
{
	gboolean have_sel, have_any;

	have_sel = rb_node_view_have_selection (view->priv->songs);
	have_any = (rb_node_view_get_first_node (view->priv->songs) != NULL);

	gtk_widget_set_sensitive (view->priv->play_song, have_sel);
	gtk_widget_set_sensitive (view->priv->play_song_later, have_sel);
	gtk_widget_set_sensitive (view->priv->play_album, have_any);
	gtk_widget_set_sensitive (view->priv->play_album_later, have_any);
}
