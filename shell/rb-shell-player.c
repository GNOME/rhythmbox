/* 
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <bonobo/bonobo-ui-util.h>
#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>

#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "rb-glade-helpers.h"
#include "rb-bonobo-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-dialog.h"
#include "rb-preferences.h"
#include "rb-glist-wrapper.h"
#include "rb-debug.h"
#include "rb-player.h"
#include "rb-remote.h"
#include "eel-gconf-extensions.h"

typedef enum
{
	PLAY_BUTTON_PLAY,
	PLAY_BUTTON_PAUSE,
	PLAY_BUTTON_STOP
} PlayButtonState;

static void rb_shell_player_class_init (RBShellPlayerClass *klass);
static void rb_shell_player_init (RBShellPlayer *shell_player);
static void rb_shell_player_finalize (GObject *object);
static void rb_shell_player_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_shell_player_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_shell_player_do_previous (RBShellPlayer *player);
static void rb_shell_player_cmd_previous (BonoboUIComponent *component,
			                  RBShellPlayer *player,
			                  const char *verbname);
static void rb_shell_player_cmd_play (BonoboUIComponent *component,
			              RBShellPlayer *player,
			              const char *verbname);
static void rb_shell_player_playpause (RBShellPlayer *player);
static void rb_shell_player_cmd_pause (BonoboUIComponent *component,
			               RBShellPlayer *player,
			               const char *verbname);
static void rb_shell_player_cmd_stop (BonoboUIComponent *component,
			              RBShellPlayer *player,
			              const char *verbname);
static void rb_shell_player_do_next (RBShellPlayer *player);
static void rb_shell_player_cmd_next (BonoboUIComponent *component,
			              RBShellPlayer *player,
			              const char *verbname);
static void rb_shell_player_shuffle_changed_cb (BonoboUIComponent *component,
						const char *path,
						Bonobo_UIComponent_EventType type,
						const char *state,
						RBShellPlayer *player);
static void rb_shell_player_repeat_changed_cb (BonoboUIComponent *component,
						const char *path,
						Bonobo_UIComponent_EventType type,
						const char *state,
						RBShellPlayer *player);
static void rb_shell_player_cmd_current_song (BonoboUIComponent *component,
					      RBShellPlayer *player,
					      const char *verbname);
static void rb_shell_player_cmd_song_info (BonoboUIComponent *component,
					   RBShellPlayer *player,
					   const char *verbname);

static void rb_shell_player_set_play_button (RBShellPlayer *player,
			                     PlayButtonState state);
static void rb_shell_player_sync_with_source (RBShellPlayer *player);
static void rb_shell_player_sync_status (RBShellPlayer *player);
static void rb_shell_player_set_playing_source (RBShellPlayer *player,
				                RBSource *source);
static void rb_shell_player_sync_buttons (RBShellPlayer *player);
static void rb_shell_player_sync_with_selected_source (RBShellPlayer *player);

static void rb_shell_player_nodeview_changed_cb (RBNodeView *view,
						 RBShellPlayer *playa);
static void rb_shell_player_node_activated_cb (RBNodeView *view,
					       RBNode *node,
					       RBShellPlayer *playa);
static void rb_shell_player_extra_node_activated_cb (RBNodeView *view,
						     RBNode *node,
						     RBShellPlayer *playa);
static void rb_shell_player_state_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBShellPlayer *playa);
static void rb_shell_player_sync_volume (RBShellPlayer *player);
void tick_cb (MonkeyMediaPlayer *player, long elapsed, gpointer data);
void eos_cb (MonkeyMediaPlayer *player, gpointer data);
void error_cb (MonkeyMediaPlayer *player, GError *err, gpointer data);

static void cancel_buffering_dialog (RBShellPlayer *player);

static void info_available_cb (MonkeyMediaPlayer *player,
			       MonkeyMediaStreamInfoField field,
			       GValue *value,
			       gpointer data);
static void cancel_buffering_clicked_cb (GtkWidget *button,
					 gpointer data);
void buffering_end_cb (MonkeyMediaPlayer *player, gpointer data);
void buffering_begin_cb (MonkeyMediaPlayer *player, gpointer data);

#define MENU_PATH_PLAY     "/menu/Controls/Play"
#define TRAY_PATH_PLAY     "/popups/TrayPopup/Play"

#define CMD_PATH_PLAY		"/commands/Play"
#define CMD_PATH_PREVIOUS	"/commands/Previous"
#define CMD_PATH_NEXT		"/commands/Next"
#define CMD_PATH_SHUFFLE	"/commands/Shuffle"
#define CMD_PATH_REPEAT		"/commands/Repeat"
#define CMD_PATH_CURRENT_SONG	"/commands/CurrentSong"
#define CMD_PATH_SONG_INFO	"/commands/SongInfo"

#define CONF_STATE		CONF_PREFIX "/state"

struct RBShellPlayerPrivate
{
	RBSource *selected_source;
	RBSource *source;

	BonoboUIComponent *component;
	BonoboUIComponent *tray_component;

	gboolean handling_error;

	MonkeyMediaPlayer *mmplayer;

	GList *active_uris;

	char *song;
	gboolean have_url;
	gboolean have_artist_album;
	char *url;
	GList *alt_locations;

	gboolean buffering_blocked;
	GtkWidget *buffering_dialog;
	GtkWidget *buffering_progress;
	guint buffering_progress_idle_id;

	GtkWidget *prev_button;
	GtkWidget *play_pause_stop_button;
	GtkWidget *play_image;
	GtkWidget *pause_image;
	GtkWidget *stop_image;
	GtkWidget *next_button;

	RBPlayer *player_widget;

	GtkWidget *shuffle_button;
	GtkWidget *volume_button;
	GtkWidget *magic_button;

	RBRemote *remote;
};

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_COMPONENT,
	PROP_TRAY_COMPONENT
};

enum
{
	WINDOW_TITLE_CHANGED,
	LAST_SIGNAL
};

static BonoboUIVerb rb_shell_player_verbs[] =
{
	BONOBO_UI_VERB ("Previous",    (BonoboUIVerbFn) rb_shell_player_cmd_previous),
	BONOBO_UI_VERB ("Play",        (BonoboUIVerbFn) rb_shell_player_cmd_play),
	BONOBO_UI_VERB ("Pause",       (BonoboUIVerbFn) rb_shell_player_cmd_pause),
	BONOBO_UI_VERB ("Stop",        (BonoboUIVerbFn) rb_shell_player_cmd_stop),
	BONOBO_UI_VERB ("Next",        (BonoboUIVerbFn) rb_shell_player_cmd_next),
	BONOBO_UI_VERB ("CurrentSong", (BonoboUIVerbFn) rb_shell_player_cmd_current_song),
	BONOBO_UI_VERB ("SongInfo",    (BonoboUIVerbFn) rb_shell_player_cmd_song_info),
	BONOBO_UI_VERB_END
};

static RBBonoboUIListener rb_shell_player_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("Shuffle",     (BonoboUIListenerFn) rb_shell_player_shuffle_changed_cb),
	RB_BONOBO_UI_LISTENER ("Repeat",      (BonoboUIListenerFn) rb_shell_player_repeat_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static GObjectClass *parent_class = NULL;

static guint rb_shell_player_signals[LAST_SIGNAL] = { 0 };

GType
rb_shell_player_get_type (void)
{
	static GType rb_shell_player_type = 0;

	if (rb_shell_player_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBShellPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_shell_player_class_init,
			NULL,
			NULL,
			sizeof (RBShellPlayer),
			0,
			(GInstanceInitFunc) rb_shell_player_init
		};

		rb_shell_player_type = g_type_register_static (GTK_TYPE_HBOX,
							       "RBShellPlayer",
							       &our_info, 0);
	}

	return rb_shell_player_type;
}

static void
rb_shell_player_class_init (RBShellPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_shell_player_finalize;

	object_class->set_property = rb_shell_player_set_property;
	object_class->get_property = rb_shell_player_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_TRAY_COMPONENT,
					 g_param_spec_object ("tray-component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_shell_player_signals[WINDOW_TITLE_CHANGED] =
		g_signal_new ("window_title_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, window_title_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static void
rb_shell_player_init (RBShellPlayer *player)
{
	GError *error = NULL;
	GtkWidget *hbox, *image;
	GtkWidget *alignment;

	player->priv = g_new0 (RBShellPlayerPrivate, 1);

	player->priv->mmplayer = monkey_media_player_new (&error);
	if (error != NULL)
	{
		rb_error_dialog (_("Failed to create the player: %s"), error->message);
		g_error_free (error);
		exit (1);
	}

	gtk_box_set_spacing (GTK_BOX (player), 12);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "info",
			  G_CALLBACK (info_available_cb),
			  player);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "eos",
			  G_CALLBACK (eos_cb),
			  player);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "tick",
			  G_CALLBACK (tick_cb),
			  player);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "error",
			  G_CALLBACK (error_cb),
			  player);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "buffering_begin",
			  G_CALLBACK (buffering_begin_cb),
			  player);

	g_signal_connect (G_OBJECT (player->priv->mmplayer),
			  "buffering_end",
			  G_CALLBACK (buffering_end_cb),
			  player);

	rb_shell_player_sync_volume (player);

	hbox = gtk_hbox_new (FALSE, 5);

	/* Previous button */
	image = gtk_image_new_from_stock (RB_STOCK_PREVIOUS,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	player->priv->prev_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->prev_button), image);
	g_signal_connect_swapped (G_OBJECT (player->priv->prev_button),
				  "clicked", G_CALLBACK (rb_shell_player_do_previous), player);

	/* Button images */
	player->priv->play_image = gtk_image_new_from_stock (RB_STOCK_PLAY,
							     GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (player->priv->play_image);
	player->priv->pause_image = gtk_image_new_from_stock (RB_STOCK_PAUSE,
							     GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (player->priv->pause_image);
	player->priv->stop_image = gtk_image_new_from_stock (RB_STOCK_STOP,
							     GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (player->priv->stop_image);

	player->priv->play_pause_stop_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button), player->priv->play_image);
	g_signal_connect_swapped (G_OBJECT (player->priv->play_pause_stop_button),
				  "clicked", G_CALLBACK (rb_shell_player_playpause), player);

	/* Next button */
	image = gtk_image_new_from_stock (RB_STOCK_NEXT,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	player->priv->next_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->next_button), image);
	g_signal_connect_swapped (G_OBJECT (player->priv->next_button),
				  "clicked", G_CALLBACK (rb_shell_player_do_next), player);

	gtk_box_pack_start (GTK_BOX (hbox), player->priv->prev_button, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->play_pause_stop_button, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->next_button, FALSE, TRUE, 0);

	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	gtk_container_add (GTK_CONTAINER (alignment), hbox);
	gtk_box_pack_start (GTK_BOX (player), alignment, FALSE, TRUE, 0);

	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	player->priv->player_widget = rb_player_new (player->priv->mmplayer);
	gtk_container_add (GTK_CONTAINER (alignment), GTK_WIDGET (player->priv->player_widget));
	gtk_box_pack_start (GTK_BOX (player), alignment, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (RB_STOCK_VOLUME_MAX,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	player->priv->volume_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->volume_button), image);

	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	gtk_container_add (GTK_CONTAINER (alignment), player->priv->volume_button);
	gtk_box_pack_end (GTK_BOX (player), alignment, FALSE, TRUE, 0);

	eel_gconf_notification_add (CONF_STATE,
				    (GConfClientNotifyFunc) rb_shell_player_state_changed_cb,
				    player);
}

static void
rb_shell_player_finalize (GObject *object)
{
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PLAYER (object));

	player = RB_SHELL_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	eel_gconf_set_float (CONF_STATE_VOLUME,
			     monkey_media_player_get_volume (player->priv->mmplayer));

	g_object_unref (G_OBJECT (player->priv->mmplayer));

	if (player->priv->remote != NULL)
		g_object_unref (G_OBJECT (player->priv->remote));
	
	g_free (player->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shell_player_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		if (player->priv->selected_source != NULL)
		{
			RBNodeView *songs = rb_source_get_node_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_player_nodeview_changed_cb),
							      player);
			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_player_node_activated_cb),
							      player);
			for (; extra_views; extra_views = extra_views->next)
				g_signal_handlers_disconnect_by_func (G_OBJECT (extra_views->data),
								      G_CALLBACK (rb_shell_player_extra_node_activated_cb),
								      player);
			g_list_free (extra_views);
			
		}
		
		player->priv->selected_source = g_value_get_object (value);
		rb_debug ("selected source %p", g_value_get_object (value));

		rb_shell_player_sync_with_selected_source (player);
		rb_shell_player_sync_buttons (player);

		if (player->priv->selected_source != NULL)
		{
			RBNodeView *songs = rb_source_get_node_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_connect (G_OBJECT (songs),
					  "changed",
					  G_CALLBACK (rb_shell_player_nodeview_changed_cb),
					  player);
			g_signal_connect (G_OBJECT (songs),
					  "node_activated",
					  G_CALLBACK (rb_shell_player_node_activated_cb),
					  player);
			for (; extra_views; extra_views = extra_views->next)
				g_signal_connect (G_OBJECT (extra_views->data),
						  "node_activated",
						  G_CALLBACK (rb_shell_player_extra_node_activated_cb),
						  player);
				
		}
		
		break;
	case PROP_COMPONENT:
		player->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (player->priv->component,
							     rb_shell_player_verbs,
							     player);
		rb_bonobo_add_listener_list_with_data (player->priv->component,
						       rb_shell_player_listeners,
						       player);
		rb_shell_player_set_playing_source (player, NULL);
		break;
	case PROP_TRAY_COMPONENT:
		player->priv->tray_component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (player->priv->tray_component,
							     rb_shell_player_verbs,
							     player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_shell_player_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, player->priv->selected_source);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, player->priv->component);
		break;
	case PROP_TRAY_COMPONENT:
		g_value_set_object (value, player->priv->tray_component);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_player_set_source (RBShellPlayer *player,
			    RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (player),
		      "source", source,
		      NULL);
}

RBShellPlayer *
rb_shell_player_new (BonoboUIComponent *component,
		     BonoboUIComponent *tray_component)
{
	RBShellPlayer *player;

	player = g_object_new (RB_TYPE_SHELL_PLAYER,
			       "component", component,
			       "tray-component", tray_component,
			       NULL);

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

static RBNode *
rb_shell_player_get_playing_node (RBShellPlayer *player)
{
	RBNodeView *songs;
	if (player->priv->source) {
		songs = rb_source_get_node_view (player->priv->source);
		return rb_node_view_get_playing_node (songs);
	}
	return NULL;
}

static gboolean
rb_shell_player_have_first (RBShellPlayer *player, RBSource *source)
{
	RBNodeView *songs;
	if (source) {
		songs = rb_source_get_node_view (source);
		return rb_node_view_get_first_node (songs) != NULL;
	}
	return FALSE;
}

static gboolean
rb_shell_player_have_previous (RBShellPlayer *player, RBSource *source)
{
	RBNodeView *songs;

	/* Since it's totally random which one plays next, there's no
	 * point in the previous button.  We just have next choose a
	 * random node.
	 */
	if (eel_gconf_get_boolean (CONF_STATE_SHUFFLE))
		return FALSE;
	
	if (source) {
		songs = rb_source_get_node_view (source);
		return rb_node_view_get_previous_node (songs) != NULL;
	}
	return FALSE;
}

static gboolean
rb_shell_player_have_next (RBShellPlayer *player, RBSource *source)
{
	RBNodeView *songs;

	/* If we're not playing, then we can only go next if there
	 * is something to play after the current node.  However,
	 * if we are playing, then the availablity of a "next" node
	 * depends on the repeat/shuffle state.
	 */
	if (monkey_media_player_playing (player->priv->mmplayer)) {
		if (eel_gconf_get_boolean (CONF_STATE_SHUFFLE))
			return TRUE;
		
		if (eel_gconf_get_boolean (CONF_STATE_REPEAT))
			return rb_shell_player_have_first (player, source);
	}
	
	if (source) {
		songs = rb_source_get_node_view (source);
		return rb_node_view_get_next_node (songs) != NULL;
	}
	return FALSE;
}

static void
rb_shell_player_open_location (RBShellPlayer *player,
			       const char *location,
			       GError **error)
{
	char *unescaped = gnome_vfs_unescape_string_for_display (location);
	char *msg = g_strdup_printf (_("Opening %s..."), unescaped);
	gboolean show_buffering_dialog = !strncmp ("http://", location, 7);

	rb_debug ("%s", msg);

	g_free (unescaped);
	g_free (msg);

	if (show_buffering_dialog && player->priv->buffering_blocked) {
		g_signal_handlers_unblock_by_func (G_OBJECT (player->priv->mmplayer),
						 G_CALLBACK (buffering_begin_cb), player);
		g_signal_handlers_unblock_by_func (G_OBJECT (player->priv->mmplayer),
						 G_CALLBACK (buffering_end_cb), player);
		player->priv->buffering_blocked = FALSE;
	} else if (!show_buffering_dialog && !player->priv->buffering_blocked) {
		g_signal_handlers_block_by_func (G_OBJECT (player->priv->mmplayer),
						 G_CALLBACK (buffering_begin_cb), player);
		g_signal_handlers_block_by_func (G_OBJECT (player->priv->mmplayer),
						 G_CALLBACK (buffering_end_cb), player);
		player->priv->buffering_blocked = TRUE;
	}

	monkey_media_player_close (player->priv->mmplayer);
	monkey_media_player_open (player->priv->mmplayer, location, error);
	
	if (!monkey_media_player_playing (player->priv->mmplayer))
		monkey_media_player_play (player->priv->mmplayer);
}

static void
rb_shell_player_try_alt_location (RBShellPlayer *player, GError **error)
{
	int len = g_list_length (player->priv->alt_locations);
	int i = rand () % len;
	char *location = (char*) g_list_nth (player->priv->alt_locations, i)->data;

	rb_shell_player_open_location (player, location, error);
		
	if (*error == NULL)
		return;

	rb_debug ("Error is set!");
	fprintf (stderr, "Got error opening \"%s\": %s\n", location, (*error)->message);
	player->priv->alt_locations = g_list_remove (player->priv->alt_locations, location);
}

static void
rb_shell_player_open_node (RBShellPlayer *player, RBNode *node, GError **error)
{
	const char *location = rb_node_get_property_string (node,
							    RB_NODE_PROP_LOCATION);
	RBGListWrapper *glistwrap =
		RB_GLIST_WRAPPER (rb_node_get_property_object (node, RB_NODE_PROP_ALT_LOCATIONS));

	rb_shell_player_open_location (player, location, error);
	if (*error == NULL)
		return;

	fprintf (stderr, "Got error opening \"%s\": %s\n", location, (*error)->message);
	if (glistwrap == NULL)
		return;

	player->priv->alt_locations = rb_glist_wrapper_get_list (glistwrap);

	while (*error != NULL && player->priv->alt_locations != NULL) {
		g_error_free (*error);
		*error = NULL;
		rb_shell_player_try_alt_location (player, error);
	}

	if (error != NULL) {
		rb_error_dialog (_("All fallback locations failed, unable to play: %s\n"),
				 (*error)->message);
		return;
	}

}

static void
rb_shell_player_play (RBShellPlayer *player)
{
	RBNodeView *songs = rb_source_get_node_view (player->priv->selected_source);

	rb_node_view_set_playing (songs, TRUE);

	monkey_media_player_play (player->priv->mmplayer);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
}

static void
rb_shell_player_set_playing_node (RBShellPlayer *player, RBNode *node)
{
	GError *error = NULL;
	RBNodeView *songs;
	
	g_return_if_fail (player->priv->source != NULL);
	g_return_if_fail (node != NULL);
	
	songs = rb_source_get_node_view (player->priv->source);

	rb_shell_player_open_node (player, node, &error);

	if (error != NULL) {
		rb_error_dialog (error->message);
		return;
	}

	rb_debug ("Success!");
	rb_node_view_set_playing_node (songs, node);
	rb_shell_player_play (player);
}

static void
rb_shell_player_previous (RBShellPlayer *player)
{
	RBNodeView *songs = rb_source_get_node_view (player->priv->source);
	RBNode *node;

	if (eel_gconf_get_boolean (CONF_STATE_SHUFFLE)) {
		rb_debug ("choosing random node");
		node =  rb_node_view_get_random_node (songs);
	} else {
		rb_debug ("choosing previous linked node");
		node = rb_node_view_get_previous_node (songs);
	}
	if (node == NULL) {
		rb_debug ("No previous node, stopping playback");
		rb_shell_player_set_playing_source (player, NULL);
		return;
	}

	rb_shell_player_set_playing_node (player, node);
}

static void
rb_shell_player_next (RBShellPlayer *player)
{
	RBNodeView *songs = rb_source_get_node_view (player->priv->source);
	RBNode *node;

	if (eel_gconf_get_boolean (CONF_STATE_SHUFFLE)) {
		rb_debug ("choosing random node");
		node =  rb_node_view_get_random_node (songs);
	} else {
		rb_debug ("choosing next linked node");
		node = rb_node_view_get_next_node (songs);
	}

	if (node == NULL) {
		/* If repeat is enabled, loop back to the start */
		if (eel_gconf_get_boolean (CONF_STATE_REPEAT)
		    && rb_shell_player_have_first (player, player->priv->source)) {
			rb_debug ("No next node, but repeat is enabled");
			node = rb_node_view_get_first_node (songs);
		} else {
			rb_debug ("No next node, stopping playback");
			rb_shell_player_set_playing_source (player, NULL);
			rb_shell_player_sync_buttons (player);
			return;
		}
	}

	rb_shell_player_set_playing_node (player, node);
}

static void
rb_shell_player_do_previous (RBShellPlayer *player)
{
	if (monkey_media_player_get_time (player->priv->mmplayer) < 3 &&
	    rb_shell_player_have_previous (player, player->priv->selected_source) == TRUE)
	{
		/* we're in the first 2 seconds of the song, go to previous */
		rb_shell_player_previous (player);
	}
	else
	{
		/* we're further in the song, restart it */
		monkey_media_player_set_time (player->priv->mmplayer, 0);
	}

	/* FIXME: uncomment - disabled for 0.4 release */
	//rb_source_jump_to_current (player->priv->source);
}

static void
rb_shell_player_cmd_previous (BonoboUIComponent *component,
			      RBShellPlayer *player,
			      const char *verbname)
{
	rb_debug ("previous");
	rb_shell_player_do_previous (player);
}

static void
rb_shell_player_cmd_play (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_debug ("play!");
	rb_shell_player_playpause (player);
}

static void
rb_shell_player_playpause (RBShellPlayer *player)
{
	if (monkey_media_player_playing (player->priv->mmplayer)) {
		monkey_media_player_pause (player->priv->mmplayer);
	} else {
		RBNode *node;
		if (player->priv->source == NULL) {
			/* no current stream, pull one in from the currently
			 * selected source */
			rb_shell_player_set_playing_source (player, player->priv->selected_source);
		}
		
		node = rb_shell_player_get_playing_node (player);
		if (!node) {
			RBNodeView *songs = rb_source_get_node_view (player->priv->source);
			node = rb_node_view_get_first_node (songs);
			g_return_if_fail (node != NULL);
			rb_shell_player_set_playing_node (player, node);
		} else {
			rb_shell_player_play (player);
		}
	}
	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
}

static void
rb_shell_player_cmd_pause (BonoboUIComponent *component,
			   RBShellPlayer *player,
			   const char *verbname)
{
	rb_debug ("This appears to be a mild setback for the stop faction");
	rb_shell_player_playpause (player);

}

static void
rb_shell_player_cmd_stop (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_debug ("STOP FACTION WINS AGAIN!!");
	rb_shell_player_set_playing_source (player, NULL);
}

static void
rb_shell_player_sync_control_state (RBShellPlayer *player)
{
	rb_debug ("syncing control state");
	rb_bonobo_set_active (player->priv->component,
			      CMD_PATH_SHUFFLE,
			      eel_gconf_get_boolean (CONF_STATE_SHUFFLE));
	rb_bonobo_set_active (player->priv->component,
			      CMD_PATH_REPEAT,
			      eel_gconf_get_boolean (CONF_STATE_REPEAT));
}

static void
rb_shell_player_sync_volume (RBShellPlayer *player)
{
	monkey_media_player_set_volume (player->priv->mmplayer,
					eel_gconf_get_float (CONF_STATE_VOLUME));
}

static void
rb_shell_player_state_changed_cb (GConfClient *client,
				  guint cnxn_id,
				  GConfEntry *entry,
				  RBShellPlayer *playa)
{
	rb_debug ("state changed");
	rb_shell_player_sync_control_state (playa);
	rb_shell_player_sync_buttons (playa);
	rb_shell_player_sync_volume (playa);
}

static void
rb_shell_player_shuffle_changed_cb (BonoboUIComponent *component,
				    const char *path,
				    Bonobo_UIComponent_EventType type,
				    const char *state,
				    RBShellPlayer *player)
{
	rb_debug ("shuffle changed");
	eel_gconf_set_boolean (CONF_STATE_SHUFFLE,
			       rb_bonobo_get_active (component,
						     CMD_PATH_SHUFFLE));
}
	
static void rb_shell_player_repeat_changed_cb (BonoboUIComponent *component,
						const char *path,
						Bonobo_UIComponent_EventType type,
						const char *state,
						RBShellPlayer *player)
{
	rb_debug ("repeat changed");
	eel_gconf_set_boolean (CONF_STATE_REPEAT,
			       rb_bonobo_get_active (component,
						     CMD_PATH_REPEAT));
}

static void
rb_shell_player_cmd_current_song (BonoboUIComponent *component,
				  RBShellPlayer *player,
				  const char *verbname)
{
	RBNodeView *songs;
	RBNode *node;

	rb_debug ("current song");

	g_return_if_fail (player->priv->source != NULL);

	songs = rb_source_get_node_view (player->priv->source);
	node = rb_shell_player_get_playing_node (player);	

	g_return_if_fail (node != NULL);
	
	rb_node_view_scroll_to_node (songs, node);
}

static void
rb_shell_player_cmd_song_info (BonoboUIComponent *component,
			       RBShellPlayer *player,
			       const char *verbname)
{
	rb_debug ("song info");

	rb_source_song_properties (player->priv->selected_source);
}

static void
rb_shell_player_do_next (RBShellPlayer *player)
{
	if (player->priv->source != NULL)
	{
		rb_shell_player_next (player);

		/* FIXME: uncomment - disabled for 0.4 release */
		//rb_source_jump_to_current (player->priv->source);
	}
}

static void
rb_shell_player_cmd_next (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_debug ("next");
	rb_shell_player_do_next (player);
}

static void
rb_shell_player_nodeview_changed_cb (RBNodeView *view,
				     RBShellPlayer *playa)
{
	rb_debug ("nodeview changed");
	rb_shell_player_sync_buttons (playa);
	rb_shell_player_sync_status (playa);
}

static void
rb_shell_player_node_activated_cb (RBNodeView *view,
				   RBNode *node,
				   RBShellPlayer *playa)
{
	g_return_if_fail (node != NULL);

	rb_debug  ("got node %p activated", node);
	
	rb_shell_player_set_playing_source (playa, playa->priv->selected_source);

	rb_shell_player_set_playing_node (playa, node);
}

static void rb_shell_player_extra_node_activated_cb (RBNodeView *view,
						     RBNode *node,
						     RBShellPlayer *playa)
{
	RBNodeView *songs;
	g_return_if_fail (node != NULL);

	rb_debug  ("got extra node %p activated", node);
	
	rb_shell_player_set_playing_source (playa, playa->priv->selected_source);

	songs = rb_source_get_node_view (playa->priv->source);
	node = rb_node_view_get_first_node (songs);
	g_return_if_fail (node != NULL);

	rb_shell_player_set_playing_node (playa, node);
}

static void
rb_shell_player_set_play_button (RBShellPlayer *player,
			         PlayButtonState state)
{
	const char *tlabel = NULL, *mlabel = NULL, *verb = NULL;

	gtk_container_remove (GTK_CONTAINER (player->priv->play_pause_stop_button),
			      gtk_bin_get_child (GTK_BIN (player->priv->play_pause_stop_button)));

	switch (state)
	{
	case PLAY_BUTTON_PAUSE:
		rb_debug ("setting pause button");
		tlabel = _("Pause");
		mlabel = _("_Pause");
		verb = "Pause";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->pause_image);
		break;
	case PLAY_BUTTON_PLAY:
		rb_debug ("setting play button");
		tlabel = _("Play");
		mlabel = _("_Play");
		verb = "Play";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->play_image);
		break;
	case PLAY_BUTTON_STOP:
		rb_debug ("setting STOP button");
		tlabel = _("Stop");
		mlabel = _("_Stop");
		verb = "Stop";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->stop_image);
		break;
	default:
		g_error ("Should not get here!");
		break;
	}
	gtk_widget_show_all (GTK_WIDGET (player->priv->play_pause_stop_button));

	rb_bonobo_set_label (player->priv->component, MENU_PATH_PLAY, mlabel);
	rb_bonobo_set_label (player->priv->component, TRAY_PATH_PLAY, mlabel);
	rb_bonobo_set_verb (player->priv->component, MENU_PATH_PLAY, verb);
	rb_bonobo_set_verb (player->priv->component, TRAY_PATH_PLAY, verb);
}

static void
rb_shell_player_sync_status (RBShellPlayer *player)
{
	const char *text = rb_source_get_status (player->priv->selected_source);
	rb_debug ("status: %s", text);
/* 	rb_echo_area_msg_full (player->priv->echo_area, text, 0); */
}

static void
rb_shell_player_sync_with_source (RBShellPlayer *player)
{
	const char *nodetitle = NULL, *artist = NULL;
	char *title;
	RBNode *node;

	node = rb_shell_player_get_playing_node (player);
	rb_debug ("playing source: %p, active node: %p", player->priv->source, node);

	if (node != NULL) {
		nodetitle = rb_node_get_property_string (node, RB_NODE_PROP_NAME);
		artist = rb_node_get_property_string (node, RB_NODE_PROP_ARTIST);
	}

	if (player->priv->have_url)
		rb_player_set_urldata (player->priv->player_widget,
				       nodetitle,
				       player->priv->url);
	else
		rb_player_set_urldata (player->priv->player_widget,
				       NULL, NULL);

	if (player->priv->song && nodetitle)
		title = g_strdup_printf ("%s (%s)", player->priv->song,
					 nodetitle);
	else if (nodetitle && artist)
		title = g_strdup_printf ("%s - %s", artist, nodetitle);
	else if (nodetitle)
		title = g_strdup (nodetitle);
	else
		title = NULL;

	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[WINDOW_TITLE_CHANGED], 0,
		       title);

	/* Sync the player */
	if (player->priv->song)
		rb_player_set_title (player->priv->player_widget, title);
	else
		rb_player_set_title (player->priv->player_widget, nodetitle);
	g_free (title);
	rb_player_set_playing_node (player->priv->player_widget, node);
	rb_player_sync (player->priv->player_widget);
}

static void
rb_shell_player_sync_buttons (RBShellPlayer *player)
{
	PlayButtonState pstate = PLAY_BUTTON_PLAY;
	RBSource *source = rb_shell_player_get_playing_node (player) == NULL ?
		player->priv->selected_source : player->priv->source;
	RBNodeView *songs = rb_source_get_node_view (source);
	gboolean have_previous, have_next;

	rb_debug ("syncing with source %p", source);

	have_previous = rb_shell_player_have_previous (player, source);
	
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PREVIOUS,
				 have_previous);
	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->prev_button), have_previous);

	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->play_pause_stop_button),
				  rb_shell_player_have_first (player, source));

	have_next = rb_shell_player_have_next (player, source);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_NEXT,
				 have_next);
	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->next_button), have_next);

	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_CURRENT_SONG,
				 rb_shell_player_get_playing_node (player) != NULL);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_SONG_INFO,
				 rb_node_view_have_selection (songs));

	if (monkey_media_player_playing (player->priv->mmplayer)) {
		if (player->priv->source == player->priv->selected_source
		    && rb_source_can_pause (RB_SOURCE (player->priv->selected_source)))
			pstate = PLAY_BUTTON_PAUSE;
		else
			pstate = PLAY_BUTTON_STOP;

		rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PLAY, TRUE);

	} else  {
		if (monkey_media_player_get_uri (player->priv->mmplayer) == NULL) {
			pstate = PLAY_BUTTON_PLAY;
		} else {
			if (player->priv->source == player->priv->selected_source)
				pstate = PLAY_BUTTON_PLAY;
			else
				pstate = PLAY_BUTTON_STOP;
		}

		rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PLAY,
					 rb_shell_player_have_first (player, source));

	}

	rb_shell_player_set_play_button (player, pstate);
}

static void
rb_shell_player_set_playing_source (RBShellPlayer *player,
				    RBSource *source)
{
	if (player->priv->source == source && source != NULL)
		return;

	if (player->priv->source != NULL && source == NULL) {
		RBNodeView *songs = rb_source_get_node_view (player->priv->source);	
		rb_node_view_set_playing_node (songs, NULL);
		rb_node_view_set_playing (songs, FALSE);
	}

	rb_debug ("setting playing source to %p", source);

	player->priv->source = source;

	player->priv->song = NULL;
	player->priv->url = NULL;
	player->priv->have_url = FALSE;
	player->priv->have_artist_album = FALSE;

	if (source == NULL)
		rb_shell_player_stop (player);

	rb_shell_player_sync_with_source (player);
}

void
rb_shell_player_stop (RBShellPlayer *player)
{
	rb_debug ("stopping");

	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	if (monkey_media_player_playing (player->priv->mmplayer))
		monkey_media_player_pause (player->priv->mmplayer);
	monkey_media_player_close (player->priv->mmplayer);
}

gboolean
rb_shell_player_get_playing (RBShellPlayer *player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), -1);

	return monkey_media_player_playing (player->priv->mmplayer);
}

MonkeyMediaPlayer *
rb_shell_player_get_mm_player (RBShellPlayer *player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), NULL);

	return player->priv->mmplayer;
}

static void
rb_shell_player_sync_with_selected_source (RBShellPlayer *player)
{
	rb_debug ("syncing with selected source: %p", player->priv->selected_source);
	if (player->priv->source == NULL)
	{
		rb_debug ("no playing source, new source is %p", player->priv->selected_source);

		player->priv->have_url = rb_source_have_url (player->priv->selected_source);

		player->priv->have_artist_album
			= rb_source_have_artist_album (player->priv->selected_source);

		rb_shell_player_sync_with_source (player);
	}
	rb_shell_player_sync_status (player);
}

void
eos_cb (MonkeyMediaPlayer *mmplayer, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("eos!");

	gdk_threads_enter ();

	if (player->priv->source != NULL)
	{
		rb_debug ("updating play statistics");
		rb_node_update_play_statistics (rb_shell_player_get_playing_node (player));

		switch (rb_source_handle_eos (player->priv->source))
		{
		case RB_SOURCE_EOF_ERROR:
			rb_error_dialog (_("Unexpected end of stream!"));
			rb_shell_player_set_playing_source (player, NULL);
			break;
		case RB_SOURCE_EOF_NEXT:
			rb_shell_player_next (player);
			break;
		}
	}

	gdk_threads_leave ();
}

void
error_cb (MonkeyMediaPlayer *mmplayer, GError *err, gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	if (player->priv->handling_error)
	{
		rb_debug ("ignoring error: %s", err->message);
		return;
	}

	gdk_threads_enter ();

	if (!monkey_media_player_playing (mmplayer)) {
		rb_debug ("mmplayer is not playing, ignoring error");
		goto out_unlock;
	}

	cancel_buffering_dialog (player);
	rb_debug ("error: %s", err->message);
	player->priv->handling_error = TRUE;
	rb_shell_player_set_playing_source (player, NULL);
 	rb_error_dialog ("%s", err->message);
	player->priv->handling_error = FALSE;
	rb_debug ("exiting error hander");

 out_unlock:
	gdk_threads_leave ();
}

void
tick_cb (MonkeyMediaPlayer *mmplayer, long elapsed, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	gdk_threads_enter ();

	rb_player_sync_time (player->priv->player_widget);

	gdk_threads_leave ();
}

void
info_available_cb (MonkeyMediaPlayer *mmplayer,
		   MonkeyMediaStreamInfoField field,
		   GValue *value,
		   gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	RBNodeView *songs;
	RBNode *node;
	gboolean changed = FALSE;
	char *valcontents;
	GEnumValue *enumvalue = g_enum_get_value(g_type_class_peek(MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD),
						 field);
	valcontents = g_strdup_value_contents (value);
	rb_debug ("info: %s -> %s; source: %p uri: %s\n",
		  enumvalue->value_name,
		  valcontents,
		  player->priv->source,
		  monkey_media_player_get_uri (player->priv->mmplayer));
	g_free (valcontents);

	/* Sanity check, this signal may come in after we stopped the
	 * player */
	if (player->priv->source == NULL
	    || !monkey_media_player_get_uri (player->priv->mmplayer)) {
		rb_debug ("Got info_available but no playing source!");
		return;
	}

	gdk_threads_enter ();

	songs = rb_source_get_node_view (player->priv->source);
	node = rb_node_view_get_playing_node (songs);

	if (node == NULL) {
		rb_debug ("Got info_available but no playing node!");
		goto out_unlock;
	}

	switch (field)
	{
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
	{
		char *song = g_value_dup_string (value);
		g_return_if_fail (song == NULL || g_utf8_validate (song, -1, NULL));

		if ((!song && player->priv->song)
		    || !player->priv->song
		    || strcmp (song, player->priv->song)) {
			changed = TRUE;
			g_free (player->priv->song);
			player->priv->song = song;
		}
		else
			g_free (song);
		break;
	}
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	{
		const char *url = g_value_get_string (value);

		if (!url) break;

		g_return_if_fail (g_utf8_validate (url, -1, NULL));

		if (!player->priv->url || strcmp (url, player->priv->url))
		{
			changed = TRUE;
			g_free (player->priv->url);
			player->priv->url = g_strdup (url);
		}

		break;
	}
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
	{
		GValue newval = { 0, };
		int bitrate = g_value_get_int (value) / 1000;
		char *qualitystr;
		MonkeyMediaAudioQuality quality = monkey_media_audio_quality_from_bit_rate (bitrate);
		qualitystr = monkey_media_audio_quality_to_string (quality);
		rb_debug ("Got stream quality: \"%s\"", qualitystr);
		g_value_init (&newval, G_TYPE_STRING);
		g_value_set_string_take_ownership (&newval, qualitystr);
		rb_node_set_property (node, RB_NODE_PROP_QUALITY, &newval);
		g_value_unset (&newval);
		break;
	}
	default:
	{
/* 		GEnumValue *enumvalue = g_enum_get_value(g_type_class_peek(MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD), */
/* 							 field); */
/* 		fprintf (stderr, "unused info field: %s\n", enumvalue->value_name); */
		break;
	}
	}

	if (changed)
		rb_shell_player_sync_with_source (player);

 out_unlock:
	gdk_threads_leave ();
}

static gboolean
buffering_tick_cb (GtkProgressBar *progress)
{
	g_return_val_if_fail (GTK_IS_PROGRESS_BAR (progress), FALSE);

	gdk_threads_enter ();

	gtk_progress_bar_pulse (progress);

	gdk_threads_leave ();

	return TRUE;
}

static void
cancel_buffering_dialog (RBShellPlayer *player)
{
	if (player->priv->buffering_dialog) {
		rb_debug ("removing idle source, hiding buffering dialog");
		g_source_remove (player->priv->buffering_progress_idle_id);
		gtk_widget_hide (player->priv->buffering_dialog);
	}
}

void
buffering_begin_cb (MonkeyMediaPlayer *mmplayer,
		    gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	GladeXML *xml;
	rb_debug ("got buffering_begin_cb");

	if (!monkey_media_player_playing (mmplayer)) {
		rb_debug ("not playing, ignoring");
		return;
	}

	gdk_threads_enter ();

	if (!player->priv->buffering_dialog) {
		GladeXML *xml = rb_glade_xml_new ("buffering-dialog.glade",
						  "dialog",
						  player);
		
		player->priv->buffering_progress = glade_xml_get_widget (xml, "progressbar");

		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (player->priv->buffering_progress));

		player->priv->buffering_dialog = glade_xml_get_widget (xml, "dialog");
		g_signal_connect (G_OBJECT(glade_xml_get_widget (xml, "cancel_button")),
				  "clicked", G_CALLBACK (cancel_buffering_clicked_cb),
				  player);
	}
	player->priv->buffering_progress_idle_id =
		g_timeout_add (100, (GSourceFunc) buffering_tick_cb, player->priv->buffering_progress);

	gtk_widget_show (player->priv->buffering_dialog);
	rb_debug ("leaving buffering_begin");

	gdk_threads_leave ();
}

static void
cancel_buffering_clicked_cb (GtkWidget *button,
			     gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("Cancelling");

	cancel_buffering_dialog (player);
	rb_shell_player_set_playing_source (player, NULL);

	rb_debug ("Done cancelling");
}

void
buffering_end_cb (MonkeyMediaPlayer *mmplayer,
		  gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("got buffering_end_cb");

	gdk_threads_enter ();

	cancel_buffering_dialog (player);
	rb_source_buffering_done (player->priv->source);

	gdk_threads_leave ();
}
