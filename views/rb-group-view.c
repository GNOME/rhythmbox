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
#include <gtk/gtkvpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <unistd.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-group-view.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "rb-node-song.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"
#include "rb-view-cmd.h"

#define RB_GROUP_XML_VERSION "1.0"

static void rb_group_view_class_init (RBGroupViewClass *klass);
static void rb_group_view_init (RBGroupView *view);
static void rb_group_view_finalize (GObject *object);
static void rb_group_view_set_property (GObject *object,
			                guint prop_id,
			                const GValue *value,
			                GParamSpec *pspec);
static void rb_group_view_get_property (GObject *object,
			                guint prop_id,
			                GValue *value,
			                GParamSpec *pspec);
static void rb_group_view_player_init (RBViewPlayerIface *iface);
static void rb_group_view_set_shuffle (RBViewPlayer *player,
			               gboolean shuffle);
static void rb_group_view_set_repeat (RBViewPlayer *player,
			              gboolean repeat);
static gboolean rb_group_view_can_pause (RBViewPlayer *player);
static RBViewPlayerResult rb_group_view_have_first (RBViewPlayer *player);
static RBViewPlayerResult rb_group_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_group_view_have_previous (RBViewPlayer *player);
static void rb_group_view_next (RBViewPlayer *player);
static void rb_group_view_previous (RBViewPlayer *player);
static void rb_group_view_jump_to_current (RBViewPlayer *player);
static const char *rb_group_view_get_title (RBViewPlayer *player);
static const char *rb_group_view_get_artist (RBViewPlayer *player);
static const char *rb_group_view_get_album (RBViewPlayer *player);
static const char *rb_group_view_get_song (RBViewPlayer *player);
static long rb_group_view_get_duration (RBViewPlayer *player);
static GdkPixbuf *rb_group_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_group_view_get_stream (RBViewPlayer *player);
static void rb_group_view_start_playing (RBViewPlayer *player);
static void rb_group_view_stop_playing (RBViewPlayer *player);
static void rb_group_view_set_playing_node (RBGroupView *view,
			                    RBNode *node);
static void song_activated_cb (RBNodeView *view,
		               RBNode *node,
		               RBGroupView *group_view);
static void node_view_changed_cb (RBNodeView *view,
		                  RBGroupView *group_view);
static void song_eos_cb (MonkeyMediaStream *stream,
	                 RBGroupView *view);
static RBNode *rb_group_view_get_first_node (RBGroupView *view);
static RBNode *rb_group_view_get_previous_node (RBGroupView *view,
						gboolean just_check);
static RBNode *rb_group_view_get_next_node (RBGroupView *view,
					    gboolean just_check);
static void rb_group_view_status_init (RBViewStatusIface *iface);
static const char *rb_group_view_status_get (RBViewStatus *status);
static void rb_group_view_clipboard_init (RBViewClipboardIface *iface);
static gboolean rb_group_view_can_cut (RBViewClipboard *clipboard);
static gboolean rb_group_view_can_copy (RBViewClipboard *clipboard);
static gboolean rb_group_view_can_paste (RBViewClipboard *clipboard);
static gboolean rb_group_view_can_delete (RBViewClipboard *clipboard);
static GList *rb_group_view_cut (RBViewClipboard *clipboard);
static GList *rb_group_view_copy (RBViewClipboard *clipboard);
static void rb_group_view_paste (RBViewClipboard *clipboard,
		                 GList *nodes);
static void rb_group_view_delete (RBViewClipboard *clipboard);
static void rb_group_view_song_info (RBViewClipboard *clipboard);
static void rb_group_view_cmd_select_all (BonoboUIComponent *component,
				          RBGroupView *view,
				          const char *verbname);
static void rb_group_view_cmd_select_none (BonoboUIComponent *component,
				           RBGroupView *view,
				           const char *verbname);
static void rb_group_view_cmd_current_song (BonoboUIComponent *component,
				            RBGroupView *view,
				            const char *verbname);
static void sidebar_button_edited_cb (RBSidebarButton *button,
			              RBGroupView *view);
static char *filename_from_name (const char *name);
static void rb_group_view_cmd_rename_group (BonoboUIComponent *component,
			                    RBGroupView *view,
			                    const char *verbname);
static void rb_group_view_cmd_delete_group (BonoboUIComponent *component,
			                    RBGroupView *view,
			                    const char *verbname);
static void rb_group_view_drop_cb (GtkWidget        *widget,
		    		   GdkDragContext   *context,
				   gint              x,
				   gint              y,
				   GtkSelectionData *data,
				   guint             info,
				   guint             time,
				   gpointer          user_data);
static const char *impl_get_description (RBView *view);
static GList *impl_get_selection (RBView *view);
static void rb_group_view_add_list_uri (RBGroupView *view, GList *list);
static void rb_group_view_node_removed_cb (RBNode *node,
					   RBGroupView *view);
static GtkWidget *rb_group_view_get_extra_widget (RBView *base_view);

#define CMD_PATH_CURRENT_SONG "/commands/CurrentSong"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"
#define GROUP_VIEW_SONGS_POPUP_PATH "/popups/GroupSongsList"

struct RBGroupViewPrivate
{
	RBLibrary *library;

	RBNode *group;

	GtkWidget *vbox;

	RBNodeView *songs;

	MonkeyMediaAudioStream *playing_stream;

	char *title;

	gboolean shuffle;
	gboolean repeat;

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

static BonoboUIVerb rb_group_view_verbs[] = 
{
	BONOBO_UI_VERB ("SelectAll",   (BonoboUIVerbFn) rb_group_view_cmd_select_all),
	BONOBO_UI_VERB ("SelectNone",  (BonoboUIVerbFn) rb_group_view_cmd_select_none),
	BONOBO_UI_VERB ("CurrentSong", (BonoboUIVerbFn) rb_group_view_cmd_current_song),
	BONOBO_UI_VERB ("RenameGroup", (BonoboUIVerbFn) rb_group_view_cmd_rename_group),
	BONOBO_UI_VERB ("DeleteGroup", (BonoboUIVerbFn) rb_group_view_cmd_delete_group),
	BONOBO_UI_VERB ("SLCopy", (BonoboUIVerbFn) rb_view_cmd_song_copy),
	BONOBO_UI_VERB ("SLCut", (BonoboUIVerbFn) rb_view_cmd_song_cut),
	BONOBO_UI_VERB ("SLPaste", (BonoboUIVerbFn) rb_view_cmd_song_paste),
	BONOBO_UI_VERB ("SLDelete", (BonoboUIVerbFn) rb_view_cmd_song_delete),
	BONOBO_UI_VERB ("SLProperties", (BonoboUIVerbFn) rb_view_cmd_song_properties),
	BONOBO_UI_VERB_END
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
rb_group_view_get_type (void)
{
	static GType rb_group_view_type = 0;

	if (rb_group_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBGroupViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_group_view_class_init,
			NULL,
			NULL,
			sizeof (RBGroupView),
			0,
			(GInstanceInitFunc) rb_group_view_init
		};

		static const GInterfaceInfo player_info =
		{
			(GInterfaceInitFunc) rb_group_view_player_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo clipboard_info =
		{
			(GInterfaceInitFunc) rb_group_view_clipboard_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo status_info =
		{
			(GInterfaceInitFunc) rb_group_view_status_init,
			NULL,
			NULL
		};

		rb_group_view_type = g_type_register_static (RB_TYPE_VIEW,
							     "RBGroupView",
							     &our_info, 0);
		
		g_type_add_interface_static (rb_group_view_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_group_view_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_group_view_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_group_view_type;
}

static void
rb_group_view_class_init (RBGroupViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBViewClass *view_class = RB_VIEW_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_group_view_finalize;

	object_class->set_property = rb_group_view_set_property;
	object_class->get_property = rb_group_view_get_property;

	view_class->impl_get_description  = impl_get_description;
	view_class->impl_get_selection    = impl_get_selection;
	view_class->impl_get_extra_widget = rb_group_view_get_extra_widget;

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
rb_library_view_songs_show_popup_cb (RBNodeView *view,
		   		     RBGroupView *group_view)
{
	GtkWidget *menu;
	GtkWidget *window;
	
	window = gtk_widget_get_ancestor (GTK_WIDGET (view), 
					  BONOBO_TYPE_WINDOW);
	
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
	
	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu), 
			         GROUP_VIEW_SONGS_POPUP_PATH);
		
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static void
rb_group_view_init (RBGroupView *view)
{
	RBSidebarButton *button;
	
	view->priv = g_new0 (RBGroupViewPrivate, 1);

	button = rb_sidebar_button_new ("RbGroupView",
					_("music group"));
	rb_sidebar_button_set (button,
			       RB_STOCK_GROUP,
			       _("Unnamed"),
			       FALSE);
	g_object_set_data (G_OBJECT (button), "view", view);
	g_signal_connect (G_OBJECT (button),
			  "edited",
			  G_CALLBACK (sidebar_button_edited_cb),
			  view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

#if 0
	gtk_box_pack_start (GTK_BOX (view->priv->vbox),
			    GTK_WIDGET (rb_search_entry_new ()),
			    FALSE, TRUE, 0);
#endif
	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);

	view->priv->group = rb_node_new ();

	view->priv->songs = rb_node_view_new (view->priv->group,
				              rb_file ("rb-node-view-songs.xml"),
					      NULL);
	g_signal_connect (G_OBJECT (view->priv->songs), "playing_node_removed",
			  G_CALLBACK (rb_group_view_node_removed_cb), view);
	g_signal_connect (G_OBJECT (view->priv->songs), "show_popup",
			  G_CALLBACK (rb_library_view_songs_show_popup_cb), view);


	/* Drag'n'Drop */
	rb_sidebar_button_add_dnd_targets (button,
					   target_table, 
					   G_N_ELEMENTS (target_table));
	g_signal_connect (G_OBJECT (button), "drag_data_received",
			  G_CALLBACK (rb_group_view_drop_cb), view);
	g_signal_connect (G_OBJECT (view->priv->songs), "drag_data_received",
			  G_CALLBACK (rb_group_view_drop_cb), view);
	gtk_drag_dest_set (GTK_WIDGET (view->priv->songs), GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table), GDK_ACTION_COPY);
	rb_node_view_enable_drag_source (view->priv->songs, target_uri, 1);


	g_signal_connect (G_OBJECT (view->priv->songs),
			  "node_activated",
			  G_CALLBACK (song_activated_cb),
			  view);
	g_signal_connect (G_OBJECT (view->priv->songs),
			  "changed",
			  G_CALLBACK (node_view_changed_cb),
			  view);

	gtk_box_pack_start_defaults (GTK_BOX (view->priv->vbox), GTK_WIDGET (view->priv->songs));

	gtk_widget_show_all (GTK_WIDGET (view));
			
	rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, FALSE);
}

static void
rb_group_view_finalize (GObject *object)
{
	RBGroupView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_GROUP_VIEW (object));

	view = RB_GROUP_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	rb_node_unref (view->priv->group);

	g_free (view->priv->title);
	g_free (view->priv->status);

	g_free (view->priv->name);
	g_free (view->priv->file);
	g_free (view->priv->description);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_group_view_set_property (GObject *object,
		            guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBGroupView *view = RB_GROUP_VIEW (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		view->priv->library = g_value_get_object (value);
		break;
	case PROP_FILE:
		g_free (view->priv->file);

		view->priv->file = g_strdup (g_value_get_string (value));

		g_object_set (G_OBJECT (rb_view_get_sidebar_button (RB_VIEW (view))),
			      "unique_id", view->priv->file,
			      NULL);
		break;
	case PROP_NAME:
		{
			char *file;
			
			g_free (view->priv->name);
			g_free (view->priv->description);
		
			view->priv->name = g_strdup (g_value_get_string (value));
			view->priv->description = g_strdup_printf ("\"%s\" Group", view->priv->name);

			g_object_set (G_OBJECT (rb_view_get_sidebar_button (RB_VIEW (view))),
				      "text", view->priv->name,
				      NULL);

			if (view->priv->file == NULL)
			{
				file = filename_from_name (view->priv->name);
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
rb_group_view_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBGroupView *view = RB_GROUP_VIEW (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, view->priv->library);
		break;
	case PROP_FILE:
		g_value_set_string (value, view->priv->file);
		break;
	case PROP_NAME:
		g_value_set_string (value, view->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBView *
rb_group_view_new (BonoboUIContainer *container,
	           RBLibrary *library)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_GROUP_VIEW,
				      "ui-file", "rhythmbox-group-view.xml",
				      "ui-name", "GroupView",
				      "container", container,
				      "library", library,
				      "verbs", rb_group_view_verbs,
				      NULL));

	return view;
}

RBView *
rb_group_view_new_from_file (BonoboUIContainer *container,
			     RBLibrary *library,
			     const char *file)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_GROUP_VIEW,
				      "ui-file", "rhythmbox-group-view.xml",
				      "ui-name", "GroupView",
				      "container", container,
				      "library", library,
				      "file", file,
				      "verbs", rb_group_view_verbs,
				      NULL));

	rb_group_view_load (RB_GROUP_VIEW (view));

	return view;
}

void
rb_group_view_set_name (RBGroupView *group,
		        const char *name)
{
	g_object_set (G_OBJECT (group),
		      "name", name,
		      NULL);
}

const char *
rb_group_view_get_file (RBGroupView *group)
{
	return group->priv->file;
}

static void
rb_group_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle      = rb_group_view_set_shuffle;
	iface->impl_set_repeat       = rb_group_view_set_repeat;
	iface->impl_can_pause        = rb_group_view_can_pause;
	iface->impl_have_first       = rb_group_view_have_first;
	iface->impl_have_next        = rb_group_view_have_next;
	iface->impl_have_previous    = rb_group_view_have_previous;
	iface->impl_next             = rb_group_view_next;
	iface->impl_previous         = rb_group_view_previous;
	iface->impl_jump_to_current  = rb_group_view_jump_to_current;
	iface->impl_get_title        = rb_group_view_get_title;
	iface->impl_get_artist       = rb_group_view_get_artist;
	iface->impl_get_album        = rb_group_view_get_album;
	iface->impl_get_song         = rb_group_view_get_song;
	iface->impl_get_duration     = rb_group_view_get_duration;
	iface->impl_get_pixbuf       = rb_group_view_get_pixbuf;
	iface->impl_get_stream       = rb_group_view_get_stream;
	iface->impl_start_playing    = rb_group_view_start_playing;
	iface->impl_stop_playing     = rb_group_view_stop_playing;
}

static void
rb_group_view_status_init (RBViewStatusIface *iface)
{
	iface->impl_get = rb_group_view_status_get;
}

static void
rb_group_view_clipboard_init (RBViewClipboardIface *iface)
{
	iface->impl_can_cut    = rb_group_view_can_cut;
	iface->impl_can_copy   = rb_group_view_can_copy;
	iface->impl_can_paste  = rb_group_view_can_paste;
	iface->impl_can_delete = rb_group_view_can_delete;
	iface->impl_cut        = rb_group_view_cut;
	iface->impl_copy       = rb_group_view_copy;
	iface->impl_paste      = rb_group_view_paste;
	iface->impl_delete     = rb_group_view_delete;
	iface->impl_song_info  = rb_group_view_song_info;
}

static void
rb_group_view_set_shuffle (RBViewPlayer *player,
			   gboolean shuffle)
{
	RBGroupView *view = RB_GROUP_VIEW (player);

	view->priv->shuffle = shuffle;
}

static void
rb_group_view_set_repeat (RBViewPlayer *player,
			  gboolean repeat)
{
	RBGroupView *view = RB_GROUP_VIEW (player);

	view->priv->repeat = repeat;
}

static gboolean
rb_group_view_can_pause (RBViewPlayer *player)
{
	return TRUE;
}

static RBViewPlayerResult
rb_group_view_have_first (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *first;

	first = rb_group_view_get_first_node (view);
	
	return (first != NULL);
}

static RBViewPlayerResult
rb_group_view_have_next (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *next;

	next = rb_group_view_get_next_node (view, TRUE);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_group_view_have_previous (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *previous;

	previous = rb_group_view_get_previous_node (view, TRUE);

	return (previous != NULL);
}

static void
rb_group_view_next (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_group_view_get_next_node (view, FALSE);
	
	rb_group_view_set_playing_node (view, node);
}

static void
rb_group_view_previous (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_group_view_get_previous_node (view, FALSE);
	
	rb_group_view_set_playing_node (view, node);
}

static void 
rb_group_view_jump_to_current (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);
	if (node != NULL)
	{
		rb_node_view_scroll_to_node (view->priv->songs, node);
	}
}

static const char *
rb_group_view_get_title (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);

	return (const char *) view->priv->title;
}

static const char *
rb_group_view_get_artist (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ARTIST);
	else
		return NULL;
}

static const char *
rb_group_view_get_album (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ALBUM);
	else
		return NULL;
}

static const char *
rb_group_view_get_song (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_NAME);
	else
		return NULL;
}

static long
rb_group_view_get_duration (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_long (node, RB_NODE_PROP_REAL_DURATION);
	else
		return -1;
}

static GdkPixbuf *
rb_group_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_group_view_get_stream (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);

	return view->priv->playing_stream;
}

static GtkWidget *
rb_group_view_get_extra_widget (RBView *base_view)
{
	return NULL;
}

static void
rb_group_view_start_playing (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);
	RBNode *node;

	node = rb_group_view_get_first_node (view);

	rb_group_view_set_playing_node (view, node);
}

static void
rb_group_view_stop_playing (RBViewPlayer *player)
{
	RBGroupView *view = RB_GROUP_VIEW (player);

	rb_group_view_set_playing_node (view, NULL);
}

static void
rb_group_view_set_playing_node (RBGroupView *view,
			        RBNode *node)
{
	rb_node_view_set_playing_node (view->priv->songs, node);

	g_free (view->priv->title);

	if (node == NULL)
	{
		view->priv->playing_stream = NULL;

		view->priv->title = NULL;

		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, FALSE);
	}
	else
	{
		GError *error = NULL;
		const char *artist = rb_group_view_get_artist (RB_VIEW_PLAYER (view));
		const char *song = rb_group_view_get_song (RB_VIEW_PLAYER (view));
		const char *uri;

		uri = rb_node_get_property_string (node,
				                    RB_NODE_PROP_LOCATION);

		g_assert (uri != NULL);
		
		view->priv->playing_stream = monkey_media_audio_stream_new (uri, &error);
		if (error != NULL)
		{
			rb_error_dialog (_("Failed to create stream for %s, error was:\n%s"),
					 uri, error->message);
			g_error_free (error);
			return;
		}
		
		g_signal_connect (G_OBJECT (view->priv->playing_stream),
				  "end_of_stream",
				  G_CALLBACK (song_eos_cb),
				  view);
		
		view->priv->title = g_strdup_printf ("%s - %s", artist, song);
		
		rb_view_set_sensitive (RB_VIEW (view), CMD_PATH_CURRENT_SONG, TRUE);
	}
}

static void
song_activated_cb (RBNodeView *view,
		   RBNode *node,
		   RBGroupView *group_view)
{
	rb_group_view_set_playing_node (group_view, node);

	rb_view_player_notify_changed (RB_VIEW_PLAYER (group_view));
	rb_view_player_notify_playing (RB_VIEW_PLAYER (group_view));
}

static void
node_view_changed_cb (RBNodeView *view,
		      RBGroupView *group_view)
{

	rb_view_player_notify_changed (RB_VIEW_PLAYER (group_view));
	rb_view_status_notify_changed (RB_VIEW_STATUS (group_view));
	rb_view_clipboard_notify_changed (RB_VIEW_CLIPBOARD (group_view));
	rb_view_set_sensitive (RB_VIEW (group_view), CMD_PATH_SONG_INFO,
			       rb_node_view_have_selection (view));
}

static void
song_update_statistics (RBGroupView *view)
{
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);
	rb_node_update_play_statistics (node);
}

static void
song_eos_cb (MonkeyMediaStream *stream,
	     RBGroupView *view)
{
	GDK_THREADS_ENTER ();

	song_update_statistics (view);
	
	rb_group_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));

	GDK_THREADS_LEAVE ();
}

static RBNode *
rb_group_view_get_previous_node (RBGroupView *view,
				 gboolean just_check)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->songs);
	else
	{
		if (just_check == TRUE)
			node = rb_node_view_get_first_node (view->priv->songs);
		else
			node = rb_node_view_get_previous_random_node (view->priv->songs);
	}

	return node;
}

static RBNode *
rb_group_view_get_first_node (RBGroupView *view)
{
	RBNode *node;

	if (view->priv->shuffle == FALSE)
	{
		GList *sel = rb_node_view_get_selection (view->priv->songs);

		if (sel == NULL)
			node = rb_node_view_get_first_node (view->priv->songs);
		else
		{
			GList *first = g_list_first (sel);
			node = RB_NODE (first->data);
		}
	}
	else
		node = rb_node_view_get_next_random_node (view->priv->songs);

	return node;
}

static RBNode *
rb_group_view_get_next_node (RBGroupView *view,
			     gboolean just_check)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
	{
		node = rb_node_view_get_next_node (view->priv->songs);
		if (node == NULL && view->priv->repeat == TRUE)
		{
			node = rb_node_view_get_first_node (view->priv->songs);
		}
	}
	else
	{
		if (just_check == TRUE)
			node = rb_node_view_get_first_node (view->priv->songs);
		else
			node = rb_node_view_get_next_random_node (view->priv->songs);
	}

	return node;
}

static const char *
rb_group_view_status_get (RBViewStatus *status)
{
	RBGroupView *view = RB_GROUP_VIEW (status);

	g_free (view->priv->status);
	view->priv->status = rb_node_view_get_status (view->priv->songs);

	return (const char *) view->priv->status;
}

static gboolean
rb_group_view_can_cut (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_GROUP_VIEW (clipboard)->priv->songs);
}

static gboolean
rb_group_view_can_copy (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_GROUP_VIEW (clipboard)->priv->songs);
}

static gboolean
rb_group_view_can_paste (RBViewClipboard *clipboard)
{
	return TRUE;
}

static gboolean
rb_group_view_can_delete (RBViewClipboard *clipboard)
{
	return rb_node_view_have_selection (RB_GROUP_VIEW (clipboard)->priv->songs);
}

static GList *
rb_group_view_cut (RBViewClipboard *clipboard)
{
	RBGroupView *view = RB_GROUP_VIEW (clipboard);
	GList *sel, *l;

	sel = g_list_copy (rb_node_view_get_selection (view->priv->songs));
	for (l = sel; l != NULL; l = g_list_next (l))
	{
		rb_node_remove_child (view->priv->group, RB_NODE (l->data));
	}
	
	return sel;
}

static GList *
rb_group_view_copy (RBViewClipboard *clipboard)
{
	RBGroupView *view = RB_GROUP_VIEW (clipboard);

	return g_list_copy (rb_node_view_get_selection (view->priv->songs));
}

static void
rb_group_view_paste (RBViewClipboard *clipboard,
		     GList *nodes)
{
	RBGroupView *view = RB_GROUP_VIEW (clipboard);
	GList *l;

	for (l = nodes; l != NULL; l = g_list_next (l))
	{
		rb_group_view_add_node (view, RB_NODE (l->data));
	}
}

static void
rb_group_view_delete (RBViewClipboard *clipboard)
{
	RBGroupView *view = RB_GROUP_VIEW (clipboard);
	GList *sel, *l;

	sel = g_list_copy (rb_node_view_get_selection (view->priv->songs));
	for (l = sel; l != NULL; l = g_list_next (l))
	{
		rb_node_remove_child (view->priv->group, RB_NODE (l->data));
	}
	g_list_free (sel);
}

static void
rb_group_view_song_info (RBViewClipboard *clipboard)
{
	RBGroupView *view = RB_GROUP_VIEW (clipboard);
	GtkWidget *song_info = NULL;

	g_return_if_fail (view->priv->songs != NULL);

	song_info = rb_song_info_new (view->priv->songs);
	gtk_widget_show_all (song_info);
}

static void
rb_group_view_cmd_select_all (BonoboUIComponent *component,
			      RBGroupView *view,
			      const char *verbname)
{
	rb_node_view_select_all (view->priv->songs);
}

static void
rb_group_view_cmd_select_none (BonoboUIComponent *component,
			       RBGroupView *view,
			       const char *verbname)
{
	rb_node_view_select_none (view->priv->songs);
}

static void
rb_group_view_cmd_current_song (BonoboUIComponent *component,
			        RBGroupView *view,
			        const char *verbname)
{
	rb_node_view_scroll_to_node (view->priv->songs,
				     rb_node_view_get_playing_node (view->priv->songs));
}

static void
sidebar_button_edited_cb (RBSidebarButton *button,
			  RBGroupView *view)
{
	char *text;
	
	g_object_get (G_OBJECT (button),
		      "text", &text,
		      NULL);

	rb_group_view_set_name (view, text);

	g_free (text);
}

void
rb_group_view_save (RBGroupView *view)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *kids;
	int i;
	char *dir;

	g_return_if_fail (RB_IS_GROUP_VIEW (view));

	dir = g_build_filename (rb_dot_dir (), "groups", NULL);
	rb_ensure_dir_exists (dir);
	g_free (dir);

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_music_group", NULL);
	xmlSetProp (root, "version", RB_GROUP_XML_VERSION);
	xmlSetProp (root, "name", view->priv->name);
	xmlDocSetRootElement (doc, root);

	kids = rb_node_get_children (view->priv->group);
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
	rb_node_thaw (view->priv->group);

	xmlSaveFormatFile (view->priv->file, doc, 1);
	xmlFreeDoc (doc);
}

void
rb_group_view_load (RBGroupView *view)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	char *name, *tmp;
	
	g_return_if_fail (RB_IS_GROUP_VIEW (view));

	if (g_file_test (view->priv->file, G_FILE_TEST_EXISTS) == FALSE)
		return;

	doc = xmlParseFile (view->priv->file);

	if (doc == NULL)
	{
		rb_warning_dialog (_("Failed to parse %s as group file"), view->priv->file);
		return;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_GROUP_XML_VERSION) != 0)
	{
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (view->priv->file);
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

		rb_group_view_add_node (view, node);
	}

	xmlFreeDoc (doc);

	rb_group_view_set_name (view, name);
	g_free (name);
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

static void
rb_group_view_cmd_rename_group (BonoboUIComponent *component,
			        RBGroupView *view,
			        const char *verbname)
{
	rb_sidebar_button_rename (rb_view_get_sidebar_button (RB_VIEW (view)));
}

static void
rb_group_view_cmd_delete_group (BonoboUIComponent *component,
			        RBGroupView *view,
			        const char *verbname)
{
	rb_view_deleted (RB_VIEW (view));
}

void
rb_group_view_remove_file (RBGroupView *view)
{
	unlink (view->priv->file);
}

static void
add_uri (const char *uri,
	 RBGroupView *view)
{
	RBNode *node;

	node = rb_library_get_song_by_location (view->priv->library, uri);

	if (node != NULL)
	{
		rb_group_view_add_node (view, node);
	}
}

static void
dnd_add_handled_cb (RBLibraryAction *action,
		    RBGroupView *view)
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

			node = rb_library_get_song_by_location (view->priv->library, uri);

			if (node != NULL)
			{
				rb_group_view_add_node (view, node);
			}
		}
		break;
	case RB_LIBRARY_ACTION_ADD_DIRECTORY:
		{
			rb_uri_handle_recursively (uri,
						   (GFunc) add_uri,
						   view);
		}
		break;
	default:
		break;
	}
}

static void
handle_songs_func (RBNode *node,
		   RBGroupView *view)
{
	rb_group_view_add_node (view, node);
}

/* rb_group_view_drop_cb: received data from a dnd operation
 * This can be either a list of uris (from nautilus) or 
 * a list of node ids (from the node-view).
 */
static void
rb_group_view_drop_cb (GtkWidget *widget,
		       GdkDragContext *context,
		       gint x,
		       gint y,
		       GtkSelectionData *data,
		       guint info,
		       guint time,
		       gpointer user_data)
{
	RBGroupView *view = RB_GROUP_VIEW (user_data);
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
			rb_library_handle_songs (view->priv->library,
						 node,
						 (GFunc) handle_songs_func,
						 view);
	}
	else if (info == RB_LIBRARY_DND_URI_LIST)
	{
		list = gnome_vfs_uri_list_parse (data->data);
		if (list != NULL)
		{
			rb_group_view_add_list_uri (view, list);
		}
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

/* rb_group_view_add_list_uri: Insert nodes from a list
 * of GnomeVFSUri.
 * */
static void 
rb_group_view_add_list_uri (RBGroupView *view,
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
			RBNode *node = rb_library_get_song_by_location (view->priv->library, uri);

			/* add the node, if already present in the library */
			if (node != NULL)
			{
				rb_group_view_add_node (view, node);
			}
			else
			{
				RBLibraryAction *action = rb_library_add_uri (view->priv->library, uri);
				g_signal_connect_object (G_OBJECT (action),
						         "handled",
						         G_CALLBACK (dnd_add_handled_cb),
						         G_OBJECT (view),
							 0);
			}
		}

		g_free (uri);
	}

	g_list_free (uri_list);
}

static const char *
impl_get_description (RBView *view)
{
	RBGroupView *gv = RB_GROUP_VIEW (view);

	return (const char *) gv->priv->description;
}

static GList *
impl_get_selection (RBView *view)
{
	RBGroupView *gv = RB_GROUP_VIEW (view);

	return rb_node_view_get_selection (gv->priv->songs);
}

/* rb_group_view_add_node: append a node to this group
 */
void
rb_group_view_add_node (RBGroupView *view,
			RBNode *node)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (node != NULL);

	if (rb_node_has_child (view->priv->group, node) == FALSE)
		rb_node_add_child (view->priv->group, node);
}

static void
rb_group_view_node_removed_cb (RBNode *node,
			       RBGroupView *view)
{
	rb_group_view_set_playing_node (view, NULL);
}
