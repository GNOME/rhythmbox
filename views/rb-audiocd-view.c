/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                2002 Kenneth Christiansen <kenneth@gnu.org>
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
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkalignment.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <bonobo/bonobo-ui-component.h>
#include <unistd.h>
#include <string.h>
#include <monkey-media-audio-cd.h>

#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-view-player.h"
#include "rb-view-clipboard.h"
#include "rb-view-status.h"
#include "rb-search-entry.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-audiocd-view.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "rb-node-song.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"
#include "rb-song-info-helpers.h"

#define RB_AUDIOCD_XML_VERSION "1.0"

static void rb_audiocd_view_class_init (RBAudiocdViewClass *klass);
static void rb_audiocd_view_init (RBAudiocdView *view);
static void rb_audiocd_view_finalize (GObject *object);
static void rb_audiocd_view_set_property (GObject *object,
			                guint prop_id,
			                const GValue *value,
			                GParamSpec *pspec);
static void rb_audiocd_view_get_property (GObject *object,
			                guint prop_id,
			                GValue *value,
			                GParamSpec *pspec);
static void rb_audiocd_view_player_init (RBViewPlayerIface *iface);
static void rb_audiocd_view_set_shuffle (RBViewPlayer *player,
			               gboolean shuffle);
static void rb_audiocd_view_set_repeat (RBViewPlayer *player,
			              gboolean repeat);
static RBViewPlayerResult rb_audiocd_view_have_first (RBViewPlayer *player);
static RBViewPlayerResult rb_audiocd_view_have_next (RBViewPlayer *player);
static RBViewPlayerResult rb_audiocd_view_have_previous (RBViewPlayer *player);
static void rb_audiocd_view_next (RBViewPlayer *player);
static void rb_audiocd_view_previous (RBViewPlayer *player);
static const char *rb_audiocd_view_get_title (RBViewPlayer *player);
static const char *rb_audiocd_view_get_artist (RBViewPlayer *player);
static const char *rb_audiocd_view_get_album (RBViewPlayer *player);
static const char *rb_audiocd_view_get_song (RBViewPlayer *player);
static long rb_audiocd_view_get_duration (RBViewPlayer *player);
static GdkPixbuf *rb_audiocd_view_get_pixbuf (RBViewPlayer *player);
static MonkeyMediaAudioStream *rb_audiocd_view_get_stream (RBViewPlayer *player);
static void rb_audiocd_view_start_playing (RBViewPlayer *player);
static void rb_audiocd_view_stop_playing (RBViewPlayer *player);
static void rb_audiocd_view_set_playing_node (RBAudiocdView *view,
			                    RBNode *node);
static void song_activated_cb (RBNodeView *view,
		               RBNode *node,
		               RBAudiocdView *audiocd_view);
static void node_view_changed_cb (RBNodeView *view,
		                  RBAudiocdView *audiocd_view);
static void song_eos_cb (MonkeyMediaStream *stream,
	                 RBAudiocdView *view);
static RBNode *rb_audiocd_view_get_first_node (RBAudiocdView *view);
static RBNode *rb_audiocd_view_get_previous_node (RBAudiocdView *view);
static RBNode *rb_audiocd_view_get_next_node (RBAudiocdView *view);
static void rb_audiocd_view_status_init (RBViewStatusIface *iface);
static const char *rb_audiocd_view_status_get (RBViewStatus *status);
static void rb_audiocd_view_clipboard_init (RBViewClipboardIface *iface);
static gboolean rb_audiocd_view_can_cut (RBViewClipboard *clipboard);
static gboolean rb_audiocd_view_can_copy (RBViewClipboard *clipboard);
static gboolean rb_audiocd_view_can_paste (RBViewClipboard *clipboard);
static gboolean rb_audiocd_view_can_delete (RBViewClipboard *clipboard);
static void rb_audiocd_view_cmd_select_all (BonoboUIComponent *component,
                                            RBAudiocdView *view,
                                            const char *verbname);
static void rb_audiocd_view_cmd_select_none (BonoboUIComponent *component,
                                             RBAudiocdView *view,
                                             const char *verbname);
static void rb_audiocd_view_cmd_current_song (BonoboUIComponent *component,
                                              RBAudiocdView *view,
                                              const char *verbname);
static void rb_audiocd_view_cmd_song_info (BonoboUIComponent *component,
                                           RBAudiocdView *view,
                                           const char *verbname);
static void rb_audiocd_view_cmd_eject_cd (BonoboUIComponent *component,
                                           RBAudiocdView *view,
                                           const char *verbname);
static const char *impl_get_description (RBView *view);
static GList *impl_get_selection (RBView *view);
static void rb_audiocd_view_node_removed_cb (RBNode *node,
                                             RBAudiocdView *view);
static GtkWidget *rb_audiocd_view_get_extra_widget (RBView *base_view);

void rb_audiocd_add_tracks (RBAudiocdView *view);
void rb_audiocd_discinfo_save (RBAudiocdView *view);
gboolean rb_audiocd_discinfo_load (RBAudiocdView *view);
void update_musicbrainz_info_thread (RBAudiocdView *view);

void rb_audiocd_refresh_cd (RBAudiocdView *view);


#define CMD_PATH_CURRENT_SONG "/commands/CurrentSong"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"

struct RBAudiocdViewPrivate
{
	RBNode *audiocd;

	GtkWidget *vbox;

        GThread *thread;

	RBNodeView *songs;

        MonkeyMediaAudioCD *cd;

	MonkeyMediaAudioStream *playing_stream;

	char *title;

	gboolean shuffle;
	gboolean repeat;

	char *status;

	char *name;
	char *description;
};

enum
{
	PROP_0,
};

static BonoboUIVerb rb_audiocd_view_verbs[] = 
{
	BONOBO_UI_VERB ("SelectAll",   (BonoboUIVerbFn) rb_audiocd_view_cmd_select_all),
	BONOBO_UI_VERB ("SelectNone",  (BonoboUIVerbFn) rb_audiocd_view_cmd_select_none),
	BONOBO_UI_VERB ("CurrentSong", (BonoboUIVerbFn) rb_audiocd_view_cmd_current_song),
	BONOBO_UI_VERB ("SongInfo",    (BonoboUIVerbFn) rb_audiocd_view_cmd_song_info),
        BONOBO_UI_VERB ("EjectCD",     (BonoboUIVerbFn) rb_audiocd_view_cmd_eject_cd),

	BONOBO_UI_VERB_END
};

static GObjectClass *parent_class = NULL;

GType
rb_audiocd_view_get_type (void)
{
	static GType rb_audiocd_view_type = 0;

	if (rb_audiocd_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBAudiocdViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_audiocd_view_class_init,
			NULL,
			NULL,
			sizeof (RBAudiocdView),
			0,
			(GInstanceInitFunc) rb_audiocd_view_init
		};

		static const GInterfaceInfo player_info =
		{
			(GInterfaceInitFunc) rb_audiocd_view_player_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo clipboard_info =
		{
			(GInterfaceInitFunc) rb_audiocd_view_clipboard_init,
			NULL,
			NULL
		};
		
		static const GInterfaceInfo status_info =
		{
			(GInterfaceInitFunc) rb_audiocd_view_status_init,
			NULL,
			NULL
		};

		rb_audiocd_view_type = g_type_register_static (RB_TYPE_VIEW,
							     "RBAudiocdView",
							     &our_info, 0);
		
		g_type_add_interface_static (rb_audiocd_view_type,
					     RB_TYPE_VIEW_PLAYER,
					     &player_info);

		g_type_add_interface_static (rb_audiocd_view_type,
					     RB_TYPE_VIEW_CLIPBOARD,
					     &clipboard_info);

		g_type_add_interface_static (rb_audiocd_view_type,
					     RB_TYPE_VIEW_STATUS,
					     &status_info);
	}

	return rb_audiocd_view_type;
}

static void
rb_audiocd_view_class_init (RBAudiocdViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBViewClass *view_class = RB_VIEW_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_audiocd_view_finalize;

	object_class->set_property = rb_audiocd_view_set_property;
	object_class->get_property = rb_audiocd_view_get_property;

	view_class->impl_get_description  = impl_get_description;
	view_class->impl_get_selection    = impl_get_selection;
	view_class->impl_get_extra_widget = rb_audiocd_view_get_extra_widget;
}

static void
rb_audiocd_view_init (RBAudiocdView *view)
{
	RBSidebarButton *button;

	view->priv = g_new0 (RBAudiocdViewPrivate, 1);

	button = rb_sidebar_button_new ("RbAudiocdView",
					_("music audiocd"));
	rb_sidebar_button_set (button,
			       RB_STOCK_AUDIOCD,
			       _("Audio-CD"),
			       FALSE);
	g_object_set_data (G_OBJECT (button), "view", view);

	g_object_set (G_OBJECT (view),
		      "sidebar-button", button,
		      NULL);

	view->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (view), view->priv->vbox);

        view->priv->cd = monkey_media_audio_cd_new (NULL);
	g_assert (view->priv->cd != NULL);

        monkey_media_audio_cd_available (view->priv->cd, NULL);
        
	view->priv->audiocd = rb_node_new ();
        

	view->priv->songs = rb_node_view_new (view->priv->audiocd,
				              rb_file ("rb-node-view-songs.xml"),
					      NULL);
	g_signal_connect (G_OBJECT (view->priv->songs), "playing_node_removed",
			  G_CALLBACK (rb_audiocd_view_node_removed_cb), view);

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
rb_audiocd_view_finalize (GObject *object)
{
	RBAudiocdView *view;
	GPtrArray *kids;
	int i;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_AUDIOCD_VIEW (object));

	view = RB_AUDIOCD_VIEW (object);

	g_return_if_fail (view->priv != NULL);
        
        g_thread_join (view->priv->thread);

        printf ("Saving discinfo");
        rb_audiocd_discinfo_save (RB_AUDIOCD_VIEW (view));

	kids = rb_node_get_children (view->priv->audiocd);
	rb_node_thaw (view->priv->audiocd);
 
	for (i = kids->len - 1; i >= 0; i--)
	{
		rb_node_remove_child (view->priv->audiocd,
				      g_ptr_array_index (kids, i));
	}

	g_free (view->priv->title);
	g_free (view->priv->status);

	g_free (view->priv->name);
	g_free (view->priv->description);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

gboolean
rb_audiocd_is_cd_available (RBAudiocdView *audiocd)
{
        return monkey_media_audio_cd_available (audiocd->priv->cd, NULL);
}

static void
rb_audiocd_view_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audiocd_view_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBView *
rb_audiocd_view_new (BonoboUIContainer *container)
{
	RBView *view;

	view = RB_VIEW (g_object_new (RB_TYPE_AUDIOCD_VIEW,
				      "ui-file", "rhythmbox-audiocd-view.xml",
				      "ui-name", "AudiocdView",
				      "container", container,
                                      "verbs", rb_audiocd_view_verbs,
				      NULL));

	return view;
}

void
rb_audiocd_view_set_name (RBAudiocdView *audiocd,
                          const char *name)
{
	g_object_set (G_OBJECT (audiocd),
		      "name", name,
		      NULL);
}

static void
rb_audiocd_view_player_init (RBViewPlayerIface *iface)
{
	iface->impl_set_shuffle      = rb_audiocd_view_set_shuffle;
	iface->impl_set_repeat       = rb_audiocd_view_set_repeat;
	iface->impl_have_first       = rb_audiocd_view_have_first;
	iface->impl_have_next        = rb_audiocd_view_have_next;
	iface->impl_have_previous    = rb_audiocd_view_have_previous;
	iface->impl_next             = rb_audiocd_view_next;
	iface->impl_previous         = rb_audiocd_view_previous;
	iface->impl_get_title        = rb_audiocd_view_get_title;
	iface->impl_get_artist       = rb_audiocd_view_get_artist;
	iface->impl_get_album        = rb_audiocd_view_get_album;
	iface->impl_get_song         = rb_audiocd_view_get_song;
	iface->impl_get_duration     = rb_audiocd_view_get_duration;
	iface->impl_get_pixbuf       = rb_audiocd_view_get_pixbuf;
	iface->impl_get_stream       = rb_audiocd_view_get_stream;
	iface->impl_start_playing    = rb_audiocd_view_start_playing;
	iface->impl_stop_playing     = rb_audiocd_view_stop_playing;
}

static void
rb_audiocd_view_status_init (RBViewStatusIface *iface)
{
	iface->impl_get = rb_audiocd_view_status_get;
}

static void
rb_audiocd_view_clipboard_init (RBViewClipboardIface *iface)
{
	iface->impl_can_cut    = rb_audiocd_view_can_cut;
	iface->impl_can_copy   = rb_audiocd_view_can_copy;
	iface->impl_can_paste  = rb_audiocd_view_can_paste;
	iface->impl_can_delete = rb_audiocd_view_can_delete;
}

static void
rb_audiocd_view_set_shuffle (RBViewPlayer *player,
                             gboolean shuffle)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	view->priv->shuffle = shuffle;
}

static void
rb_audiocd_view_set_repeat (RBViewPlayer *player,
                            gboolean repeat)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	view->priv->repeat = repeat;
}

static RBViewPlayerResult
rb_audiocd_view_have_first (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *first;

	first = rb_audiocd_view_get_first_node (view);
	
	return (first != NULL);
}

static RBViewPlayerResult
rb_audiocd_view_have_next (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *next;

	next = rb_audiocd_view_get_next_node (view);
	
	return (next != NULL);
}

static RBViewPlayerResult
rb_audiocd_view_have_previous (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *previous;

	previous = rb_audiocd_view_get_previous_node (view);

	return (previous != NULL);
}

static void
rb_audiocd_view_next (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_audiocd_view_get_next_node (view);
	
	rb_audiocd_view_set_playing_node (view, node);
}

static void
rb_audiocd_view_previous (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	if (monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (view->priv->playing_stream)) < 3 &&
	    rb_audiocd_view_have_previous (player) == TRUE)
	{
		/* we're in the first 2 seconds of the song, go to previous */
		RBNode *node;

		node = rb_audiocd_view_get_previous_node (view);
	
		rb_audiocd_view_set_playing_node (view, node);
	}
	else
	{
		/* we're further in the song, restart it */
		monkey_media_stream_set_elapsed_time (MONKEY_MEDIA_STREAM (view->priv->playing_stream), 0);
	}
}

static const char *
rb_audiocd_view_get_title (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	return (const char *) view->priv->title;
}

static const char *
rb_audiocd_view_get_artist (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_SONG_PROP_ARTIST);
	else
		return NULL;
}

static const char *
rb_audiocd_view_get_album (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_SONG_PROP_ALBUM);
	else
		return NULL;
}

static const char *
rb_audiocd_view_get_song (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_NAME);
	else
		return NULL;
}

static long
rb_audiocd_view_get_duration (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);

	if (node != NULL)
		return rb_node_get_property_long (node, RB_NODE_SONG_PROP_REAL_DURATION);
	else
		return -1;
}

static GdkPixbuf *
rb_audiocd_view_get_pixbuf (RBViewPlayer *player)
{
	return NULL;
}

static MonkeyMediaAudioStream *
rb_audiocd_view_get_stream (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	return view->priv->playing_stream;
}

static GtkWidget *
rb_audiocd_view_get_extra_widget (RBView *base_view)
{
	return NULL;
}

static void
rb_audiocd_view_start_playing (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);
	RBNode *node;

	node = rb_audiocd_view_get_first_node (view);

	rb_audiocd_view_set_playing_node (view, node);
}

static void
rb_audiocd_view_stop_playing (RBViewPlayer *player)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (player);

	rb_audiocd_view_set_playing_node (view, NULL);
}

static void
rb_audiocd_view_set_playing_node (RBAudiocdView *view,
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
		const char *artist = rb_audiocd_view_get_artist (RB_VIEW_PLAYER (view));
		const char *song = rb_audiocd_view_get_song (RB_VIEW_PLAYER (view));
		const char *uri;

		uri = rb_node_get_property_string (node,
				                    RB_NODE_SONG_PROP_LOCATION);

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
		   RBAudiocdView *audiocd_view)
{
	rb_audiocd_view_set_playing_node (audiocd_view, node);

	rb_view_player_notify_changed (RB_VIEW_PLAYER (audiocd_view));
	rb_view_player_notify_playing (RB_VIEW_PLAYER (audiocd_view));
}

static void
node_view_changed_cb (RBNodeView *view,
		      RBAudiocdView *audiocd_view)
{

	rb_view_player_notify_changed (RB_VIEW_PLAYER (audiocd_view));
	rb_view_status_notify_changed (RB_VIEW_STATUS (audiocd_view));
	rb_view_clipboard_notify_changed (RB_VIEW_CLIPBOARD (audiocd_view));
	rb_view_set_sensitive (RB_VIEW (audiocd_view), CMD_PATH_SONG_INFO,
			       rb_node_view_have_selection (view));
}

static void
song_update_statistics (RBAudiocdView *view)
{
	RBNode *node;

	node = rb_node_view_get_playing_node (view->priv->songs);
	rb_node_song_update_play_statistics (node);
}

static void
song_eos_cb (MonkeyMediaStream *stream,
	     RBAudiocdView *view)
{
	GDK_THREADS_ENTER ();

	song_update_statistics (view);
	
	rb_audiocd_view_next (RB_VIEW_PLAYER (view));

	rb_view_player_notify_changed (RB_VIEW_PLAYER (view));

	GDK_THREADS_LEAVE ();
}

static RBNode *
rb_audiocd_view_get_previous_node (RBAudiocdView *view)
{
	RBNode *node;
	
	if (view->priv->shuffle == FALSE)
		node = rb_node_view_get_previous_node (view->priv->songs);
	else
		node = rb_node_view_get_previous_random_node (view->priv->songs);

	return node;
}

static RBNode *
rb_audiocd_view_get_first_node (RBAudiocdView *view)
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
rb_audiocd_view_get_next_node (RBAudiocdView *view)
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
		node = rb_node_view_get_next_random_node (view->priv->songs);

	return node;
}

static const char *
rb_audiocd_view_status_get (RBViewStatus *status)
{
	RBAudiocdView *view = RB_AUDIOCD_VIEW (status);

	g_free (view->priv->status);
	view->priv->status = rb_node_view_get_status (view->priv->songs);

	return (const char *) view->priv->status;
}

static gboolean
rb_audiocd_view_can_cut (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_audiocd_view_can_copy (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_audiocd_view_can_paste (RBViewClipboard *clipboard)
{
	return FALSE;
}

static gboolean
rb_audiocd_view_can_delete (RBViewClipboard *clipboard)
{
	return FALSE;
}

static void
rb_audiocd_view_cmd_select_all (BonoboUIComponent *component,
			      RBAudiocdView *view,
			      const char *verbname)
{
	rb_node_view_select_all (view->priv->songs);
}

static void
rb_audiocd_view_cmd_select_none (BonoboUIComponent *component,
                                 RBAudiocdView *view,
                                 const char *verbname)
{
	rb_node_view_select_none (view->priv->songs);
}

static void
rb_audiocd_view_cmd_current_song (BonoboUIComponent *component,
                                  RBAudiocdView *view,
                                  const char *verbname)
{
	rb_node_view_scroll_to_node (view->priv->songs,
				     rb_node_view_get_playing_node (view->priv->songs));
	rb_node_view_select_node (view->priv->songs,
				  rb_node_view_get_playing_node (view->priv->songs));
}

static void
rb_audiocd_view_cmd_song_info (BonoboUIComponent *component,
                               RBAudiocdView *view,
                               const char *verbname)
{
	GtkWidget *song_info = NULL;

	g_return_if_fail (view->priv->songs != NULL);

	song_info = rb_song_info_new (view->priv->songs);
	gtk_widget_show_all (song_info);
}

static void
rb_audiocd_view_cmd_eject_cd (BonoboUIComponent *component,
                              RBAudiocdView *view,
                              const char *verbname)
{
        printf ("Ejecting CD\n");
        rb_audiocd_view_set_playing_node (view, NULL);
        monkey_media_audio_cd_open_tray (view->priv->cd, NULL);
}

static const char *
impl_get_description (RBView *view)
{
	RBAudiocdView *gv = RB_AUDIOCD_VIEW (view);

	return (const char *) gv->priv->description;
}

static GList *
impl_get_selection (RBView *view)
{
	RBAudiocdView *gv = RB_AUDIOCD_VIEW (view);

	return rb_node_view_get_selection (gv->priv->songs);
}

/* rb_audiocd_view_add_node: append a node to this audiocd
 */
void
rb_audiocd_view_add_node (RBAudiocdView *view,
                          RBNode *node)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (node != NULL);

	rb_node_add_child (view->priv->audiocd, node);
}

static void
rb_audiocd_view_node_removed_cb (RBNode *node,
                                 RBAudiocdView *view)
{
	rb_audiocd_view_set_playing_node (view, NULL);
}


void
update_musicbrainz_info_thread (RBAudiocdView *view)
{
        GPtrArray *kids;
        GValue value = { 0, }; 
        int i;

        kids = rb_node_get_children (view->priv->audiocd);
        rb_node_thaw (view->priv->audiocd);

	for (i = 0; i < kids->len; i++)
	{
                MonkeyMediaStreamInfo *info;
		RBNode *track = g_ptr_array_index (kids, i);

                rb_node_get_property (RB_NODE (track),
                                      RB_NODE_SONG_PROP_LOCATION,
                                      &value);
                info = monkey_media_stream_info_new (g_value_get_string (&value), NULL);
                g_value_unset (&value);

                g_assert (info != NULL);
       
                rb_song_set_artist (track, info);
                rb_song_set_album (track, info);
                rb_song_set_title (track, info);
        }
        printf ("About to exit thread\n");
        g_thread_exit (NULL);
}

RBNode *
rb_audiocd_node_fill_basic (char *location)
{
       RBNode *track;
       GValue value = { 0, };
       MonkeyMediaStreamInfo *info;
                
       track = rb_node_new ();

       g_value_init (&value, G_TYPE_STRING);
       g_value_set_string (&value, (char *) location);
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_SONG_PROP_LOCATION,
                             &value);
       g_value_unset (&value);

       info = monkey_media_stream_info_new ((char *) location, NULL);
       g_assert (info != NULL);
       
       g_value_init (&value, G_TYPE_STRING);
       g_value_set_string (&value, _("Unknown"));
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_SONG_PROP_ARTIST,
                             &value);
       g_value_unset (&value);
       
       g_value_init (&value, G_TYPE_STRING);
       g_value_set_string (&value, _("Unknown"));
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_SONG_PROP_ALBUM,
                             &value);
       g_value_unset (&value);
       
       monkey_media_stream_info_get_value (info, 
                                           MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
                                           0,
                                           &value);
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_SONG_PROP_TRACK_NUMBER,
                             &value);
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_SONG_PROP_REAL_TRACK_NUMBER,
                             &value);
       g_value_unset (&value);
       
       g_value_init (&value, G_TYPE_STRING);
       g_value_set_string (&value, location);
       rb_node_set_property (RB_NODE (track),
                             RB_NODE_PROP_NAME,
                             &value);
       g_value_unset (&value);
       
       rb_song_set_duration (RB_NODE (track), info);

       return RB_NODE (track);
}

void
rb_audiocd_discinfo_save (RBAudiocdView *view)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *kids;
	int i;
	char *dir, *filename, *path, *tmp;
        GValue value = { 0, };

	g_return_if_fail (RB_IS_AUDIOCD_VIEW (view));

        filename = monkey_media_audio_cd_get_disc_id (view->priv->cd, NULL);

	dir = g_build_filename (rb_dot_dir (), "audiocd", NULL);
	rb_ensure_dir_exists (dir);
        path = g_build_filename (dir, filename, NULL);
	g_free (dir);

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_audiocd_discinfo", NULL);
	xmlSetProp (root, "version", RB_AUDIOCD_XML_VERSION);
	xmlSetProp (root, "name", view->priv->name);
	xmlDocSetRootElement (doc, root);

	kids = rb_node_get_children (view->priv->audiocd);
	for (i = 0; i < kids->len; i++)
	{
		RBNode *node = g_ptr_array_index (kids, i);
		xmlNodePtr xmlnode;

		xmlnode = xmlNewChild (root, NULL, "node", NULL);

                rb_node_get_property (RB_NODE (node),
                                      RB_NODE_SONG_PROP_LOCATION,
                                      &value);

		xmlSetProp (xmlnode, "location", g_value_get_string (&value));
                g_value_unset (&value);

                rb_node_get_property (RB_NODE (node),
                                      RB_NODE_SONG_PROP_RATING,
                                      &value);

                tmp = g_strdup_printf ("%d", g_value_get_int (&value));
		xmlSetProp (xmlnode, "rating", tmp);
		g_free (tmp);
                g_value_unset (&value);
	}
	rb_node_thaw (view->priv->audiocd);

	xmlSaveFormatFile (path, doc, 1);
        g_free (path);
	xmlFreeDoc (doc);
}

gboolean
rb_audiocd_discinfo_load (RBAudiocdView *view)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	char *name, *tmp, *filename;
	
	g_return_val_if_fail (RB_IS_AUDIOCD_VIEW (view), FALSE);

        filename = g_build_filename (rb_dot_dir (), "audiocd", 
                                     monkey_media_audio_cd_get_disc_id (view->priv->cd, NULL), NULL);

	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE)
		return FALSE;

	doc = xmlParseFile (filename);

	if (doc == NULL)
	{
		rb_warning_dialog (_("Failed to parse %s as disc info file"), filename);
		return FALSE;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_AUDIOCD_XML_VERSION) != 0)
	{
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (filename);
		return FALSE;
	}
	g_free (tmp);

	name = xmlGetProp (root, "name");

	for (child = root->children; child != NULL; child = child->next)
	{
		char *tmp;
		RBNode *track;
                int rating;
                GValue value = { 0, };
                
                track = rb_node_new ();

		tmp = xmlGetProp (child, "location");

		if (tmp == NULL)
			continue;

                track = rb_audiocd_node_fill_basic (tmp);

		tmp = xmlGetProp (child, "rating");
		if (tmp != NULL)
                {
                        rating = atol (tmp);
                        g_free (tmp);

                        g_value_init (&value, G_TYPE_INT);
                        g_value_set_int (&value, rating);
                        rb_node_set_property (RB_NODE (track),
                                              RB_NODE_SONG_PROP_RATING,
                                              &value);
                        g_value_unset (&value);
                }

                rb_audiocd_view_add_node (view, track);
	}

	xmlFreeDoc (doc);

	rb_audiocd_view_set_name (view, name);
	g_free (name);
        g_free (filename);

        return TRUE;
}

void
rb_audiocd_discinfo_read_from_disc (RBAudiocdView *view)
{
        GList *l, *tracks;
        RBNode *track;

        tracks = monkey_media_audio_cd_list_tracks (view->priv->cd, NULL);

	for (l = tracks; l != NULL; l = g_list_next (l))
	{
                track = rb_audiocd_node_fill_basic (l->data);

                rb_audiocd_view_add_node (view, track);
	}

	monkey_media_audio_cd_free_tracks (tracks);
}

void
rb_audiocd_refresh_cd (RBAudiocdView *view)
{
        if (rb_audiocd_discinfo_load (view) == FALSE)
        {
                printf ("Reading CD for the first time\n");
                rb_audiocd_discinfo_read_from_disc (view);
        }

        view->priv->thread = g_thread_create ((GThreadFunc) update_musicbrainz_info_thread,
                                              view, TRUE, NULL);

}
