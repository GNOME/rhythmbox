/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of main playback logic object
 *
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
 */

#include <config.h>

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-uri.h>

#ifdef HAVE_MMKEYS
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <gdk/gdkx.h>
#endif /* HAVE_MMKEYS */

#include "rb-property-view.h"
#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "rb-glade-helpers.h"
#include "rb-file-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-dialog.h"
#include "rb-preferences.h"
#include "rb-debug.h"
#include "rb-player.h"
#include "rb-header.h"
#include "totem-pl-parser.h"
#include "rb-metadata.h"
#include "rb-iradio-source.h"
#include "rb-library-source.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"
#include "rb-play-order.h"
#include "rb-statusbar.h"
#include "rb-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rhythmdb.h"
#include "rb-podcast-manager.h"

#ifdef HAVE_XIDLE_EXTENSION
#include <X11/extensions/xidle.h>
#endif /* HAVE_XIDLE_EXTENSION */

static const char* const state_to_play_order[2][2] =
	{{"linear",	"linear-loop"},
	 {"shuffle",	"random-by-age-and-rating"}};

static void rb_shell_player_class_init (RBShellPlayerClass *klass);
static void rb_shell_player_init (RBShellPlayer *shell_player);
static GObject *rb_shell_player_constructor (GType type, guint n_construct_properties,
					     GObjectConstructParam *construct_properties);
static void rb_shell_player_finalize (GObject *object);
static void rb_shell_player_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_shell_player_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);

static void rb_shell_player_cmd_previous (GtkAction *action,
			                  RBShellPlayer *player);
static void rb_shell_player_cmd_play (GtkAction *action,
			              RBShellPlayer *player);
static void rb_shell_player_cmd_next (GtkAction *action,
			              RBShellPlayer *player);
static void rb_shell_player_shuffle_changed_cb (GtkAction *action,
						RBShellPlayer *player);
static void rb_shell_player_repeat_changed_cb (GtkAction *action,
					       RBShellPlayer *player);
static void rb_shell_player_view_song_position_slider_changed_cb (GtkAction *action,
								  RBShellPlayer *player);
static void rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
							 RBSource *source,
							 gboolean sync_entry_view);
static void rb_shell_player_sync_with_source (RBShellPlayer *player);
static void rb_shell_player_sync_with_selected_source (RBShellPlayer *player);
static void rb_shell_player_entry_changed_cb (RhythmDB *db,
							RhythmDBEntry *entry,
				       		GSList *changes,
				       		RBShellPlayer *player);
				       
static void rb_shell_player_entry_activated_cb (RBEntryView *view,
						RhythmDBEntry *entry,
						RBShellPlayer *playa);
static void rb_shell_player_property_row_activated_cb (RBPropertyView *view,
						       const char *name,
						       RBShellPlayer *playa);
static void rb_shell_player_sync_volume (RBShellPlayer *player, gboolean notify); 
static void rb_shell_player_sync_replaygain (RBShellPlayer *player,
                                             RhythmDBEntry *entry);
static void tick_cb (RBPlayer *player, long elapsed, gpointer data);
static void eos_cb (RBPlayer *player, gpointer data);
static void error_cb (RBPlayer *player, const GError *err, gpointer data);
static void buffering_cb (RBPlayer *player, guint progress, gpointer data);
static void rb_shell_player_error (RBShellPlayer *player, gboolean async, const GError *err);

static void info_available_cb (RBPlayer *player,
                               RBMetaDataField field,
                               GValue *value,
                               gpointer data);
static void rb_shell_player_set_play_order (RBShellPlayer *player,
					    const gchar *new_val);
static void rb_shell_player_play_order_update_cb (RBPlayOrder *porder,
						  gboolean has_next,
						  gboolean has_previous,
						  RBShellPlayer *player);

static void rb_shell_player_sync_play_order (RBShellPlayer *player);
static void rb_shell_player_sync_control_state (RBShellPlayer *player);
static void rb_shell_player_sync_song_position_slider_visibility (RBShellPlayer *player);

static void gconf_play_order_changed (GConfClient *client,guint cnxn_id,
				      GConfEntry *entry, RBShellPlayer *player);
static void gconf_song_position_slider_visibility_changed (GConfClient *client,guint cnxn_id,
							   GConfEntry *entry, RBShellPlayer *player);
static void rb_shell_player_playing_changed_cb (RBShellPlayer *player,
						GParamSpec *arg1,
						gpointer user_data);

static gboolean rb_shell_player_jump_to_current_idle (RBShellPlayer *player);

#ifdef HAVE_MMKEYS
static void grab_mmkey (int key_code, GdkWindow *root);
static GdkFilterReturn filter_mmkeys (GdkXEvent *xevent,
				      GdkEvent *event,
				      gpointer data);
static void rb_shell_player_init_mmkeys (RBShellPlayer *shell_player);
#endif /* HAVE_MMKEYS */

#define CONF_STATE		CONF_PREFIX "/state"

struct RBShellPlayerPrivate
{
	RhythmDB *db;

	gboolean syncing_state;
	
	RBSource *selected_source;
	RBSource *source;
	RBPlayQueueSource *queue_source;
	RBSource *current_playing_source;

	gboolean did_retry;
	GTimeVal last_retry;

	GtkUIManager *ui_manager;
	GtkActionGroup *actiongroup;

	gboolean handling_error;

	RBPlayer *mmplayer;

	char *song;
	gboolean have_url;
	char *url;

	RBPlayOrder *play_order;
	RBPlayOrder *queue_play_order;

	GError *playlist_parse_error;

	RBHeader *header_widget;
	RBStatusbar *statusbar_widget;

	guint gconf_play_order_id;
	guint gconf_song_position_slider_visibility_id;

	gboolean mute;
	float volume;

	guint do_next_idle_id;
};

#define RB_SHELL_PLAYER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayerPrivate))

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_DB,
	PROP_UI_MANAGER,
	PROP_ACTION_GROUP,
	PROP_PLAY_ORDER,
	PROP_PLAYING,
	PROP_VOLUME,
	PROP_STATUSBAR,
	PROP_QUEUE_SOURCE,
	PROP_STREAM_SONG
};

enum
{
	WINDOW_TITLE_CHANGED,
	ELAPSED_CHANGED,
	PLAYING_SOURCE_CHANGED,
	PLAYING_FROM_QUEUE,
	PLAYING_CHANGED,
	PLAYING_SONG_CHANGED,
	PLAYING_URI_CHANGED,
	LAST_SIGNAL
};

static GtkActionEntry rb_shell_player_actions [] =
{
	{ "ControlPrevious", GTK_STOCK_MEDIA_PREVIOUS, N_("P_revious"), "<alt>Left",
	  N_("Start playing the previous song"),
	  G_CALLBACK (rb_shell_player_cmd_previous) },
	{ "ControlNext", GTK_STOCK_MEDIA_NEXT, N_("_Next"), "<alt>Right",
	  N_("Start playing the next song"),
	  G_CALLBACK (rb_shell_player_cmd_next) },
};
static guint rb_shell_player_n_actions = G_N_ELEMENTS (rb_shell_player_actions);


static GtkToggleActionEntry rb_shell_player_toggle_entries [] =
{
	{ "ControlPlay", GTK_STOCK_MEDIA_PLAY, N_("_Play"), "<control>space",
	  N_("Start playback"),
	  G_CALLBACK (rb_shell_player_cmd_play) },
	{ "ControlShuffle", GNOME_MEDIA_SHUFFLE, N_("Sh_uffle"), "<control>U",
	  N_("Play songs in a random order"),
	  G_CALLBACK (rb_shell_player_shuffle_changed_cb) },
	{ "ControlRepeat", GNOME_MEDIA_REPEAT, N_("_Repeat"), "<control>R",
	  N_("Play first song again after all songs are played"),
	  G_CALLBACK (rb_shell_player_repeat_changed_cb) },
	{ "ViewSongPositionSlider", NULL, N_("_Song Position Slider"), "<control>S",
	  N_("Change the visibility of the song position slider"),
	  G_CALLBACK (rb_shell_player_view_song_position_slider_changed_cb), TRUE }
};
static guint rb_shell_player_n_toggle_entries = G_N_ELEMENTS (rb_shell_player_toggle_entries);

static guint rb_shell_player_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBShellPlayer, rb_shell_player, GTK_TYPE_HBOX)


static void
rb_shell_player_class_init (RBShellPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_shell_player_finalize;
	object_class->constructor = rb_shell_player_constructor;

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
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ACTION_GROUP,
					 g_param_spec_object ("action-group",
							      "GtkActionGroup",
							      "GtkActionGroup object",
							      GTK_TYPE_ACTION_GROUP,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_QUEUE_SOURCE,
					 g_param_spec_object ("queue-source",
						 	      "RBPlaylistSource",
							      "RBPlaylistSource object",
							      RB_TYPE_PLAYLIST_SOURCE,
							      G_PARAM_READWRITE));
	
	/* If you change these, be sure to update the CORBA interface
	 * in rb-remote-bonobo.c! */
	g_object_class_install_property (object_class,
					 PROP_PLAY_ORDER,
					 g_param_spec_string ("play-order", 
							      "play-order", 
							      "What play order to use",
							      "linear",
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_PLAYING,
					 g_param_spec_boolean ("playing", 
							       "playing", 
							      "Whether Rhythmbox is currently playing", 
							       FALSE,
							       G_PARAM_READABLE));
	
	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_float ("volume", 
							     "volume", 
							     "Current playback volume",
							     0.0f, 1.0f, 1.0f,
							     G_PARAM_READWRITE));
	
	g_object_class_install_property (object_class,
					 PROP_STATUSBAR,
					 g_param_spec_object ("statusbar", 
							      "RBStatusbar", 
							      "RBStatusbar object", 
							      RB_TYPE_STATUSBAR,
							      G_PARAM_READWRITE));
	
	g_object_class_install_property (object_class,
					 PROP_STREAM_SONG,
					 g_param_spec_string ("stream-song", 
							      "stream-song", 
							      "Current stream song title",
							      "",
							      G_PARAM_READABLE));


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

	rb_shell_player_signals[ELAPSED_CHANGED] =
		g_signal_new ("elapsed_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, elapsed_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_UINT);

	rb_shell_player_signals[PLAYING_SOURCE_CHANGED] =
		g_signal_new ("playing-source-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_source_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_SOURCE);

	rb_shell_player_signals[PLAYING_FROM_QUEUE] =
		g_signal_new ("playing-from-queue",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_from_queue),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	rb_shell_player_signals[PLAYING_CHANGED] =
		g_signal_new ("playing-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	rb_shell_player_signals[PLAYING_SONG_CHANGED] =
		g_signal_new ("playing-song-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_song_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);

	rb_shell_player_signals[PLAYING_URI_CHANGED] =
		g_signal_new ("playing-uri-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_uri_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBShellPlayerPrivate));
}

static GObject *
rb_shell_player_constructor (GType type, guint n_construct_properties,
			     GObjectConstructParam *construct_properties)
{
	RBShellPlayer *player;
	RBShellPlayerClass *klass;
	GtkAction *action;
	
	klass = RB_SHELL_PLAYER_CLASS (g_type_class_peek (RB_TYPE_SHELL_PLAYER));

	player = RB_SHELL_PLAYER (G_OBJECT_CLASS (rb_shell_player_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	gtk_action_group_add_actions (player->priv->actiongroup,
				      rb_shell_player_actions,
				      rb_shell_player_n_actions,
				      player);
	gtk_action_group_add_toggle_actions (player->priv->actiongroup,
					     rb_shell_player_toggle_entries,
					     rb_shell_player_n_toggle_entries,
					     player);

	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlPlay");
	g_object_set (action, "is-important", TRUE, NULL);

	player->priv->syncing_state = TRUE;
	rb_shell_player_set_playing_source (player, NULL);
	rb_shell_player_sync_play_order (player);
	rb_shell_player_sync_control_state (player);
	player->priv->syncing_state = FALSE;

	rb_shell_player_sync_song_position_slider_visibility (player);

	g_signal_connect (G_OBJECT (player),
			  "notify::playing",
			  G_CALLBACK (rb_shell_player_playing_changed_cb),
			  NULL);

	return G_OBJECT (player);
}

static void
volume_pre_unmount_cb (GnomeVFSVolumeMonitor *monitor, 
		       GnomeVFSVolume *volume, 
		       RBShellPlayer *player)
{
	gchar *uri_mount_point;
	gchar *volume_mount_point;
	RhythmDBEntry *entry;
	const char *uri;
	gboolean playing;

	rb_shell_player_get_playing (player, &playing, NULL);
	if (playing) {
		return;
	}

	entry = rb_shell_player_get_playing_entry (player);
	if (entry == NULL) {
		/* At startup for example, playing path can be NULL */
		return;
	}

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	uri_mount_point = rb_uri_get_mount_point (uri);
	volume_mount_point = gnome_vfs_volume_get_activation_uri (volume);

	if (!strcmp (uri_mount_point, volume_mount_point)) {
		rb_shell_player_stop (player);
	}
	g_free (uri_mount_point);
	g_free (volume_mount_point);
}

static void
reemit_playing_signal (RBShellPlayer *player, GParamSpec *pspec, gpointer data)
{
	g_signal_emit (player, rb_shell_player_signals[PLAYING_CHANGED], 0,
		       rb_player_playing (player->priv->mmplayer));
}

static void
rb_shell_player_init (RBShellPlayer *player)
{
	GError *error = NULL;

	player->priv = RB_SHELL_PLAYER_GET_PRIVATE (player);

	player->priv->mmplayer = rb_player_new (&error);
	if (error != NULL) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Failed to create the player: %s"),
						 error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));
		exit (1);
	}

	gtk_box_set_spacing (GTK_BOX (player), 12);
	gtk_container_set_border_width (GTK_CONTAINER (player), 3);

	g_signal_connect_object (G_OBJECT (player->priv->mmplayer),
				 "info",
				 G_CALLBACK (info_available_cb),
				 player, 0);

	g_signal_connect_object (G_OBJECT (player->priv->mmplayer),
				 "eos",
				 G_CALLBACK (eos_cb),
				 player, 0);

	g_signal_connect_object (G_OBJECT (player->priv->mmplayer),
				 "tick",
				 G_CALLBACK (tick_cb),
				 player, 0);

	g_signal_connect_object (G_OBJECT (player->priv->mmplayer),
				 "error",
				 G_CALLBACK (error_cb),
				 player, 0);

	g_signal_connect_object (G_OBJECT (player->priv->mmplayer),
				 "buffering",
				 G_CALLBACK (buffering_cb),
				 player, 0);

	g_signal_connect (G_OBJECT (gnome_vfs_get_volume_monitor ()), 
			  "volume-pre-unmount",
			  G_CALLBACK (volume_pre_unmount_cb),
			  player);

	player->priv->gconf_play_order_id =
		eel_gconf_notification_add (CONF_STATE_PLAY_ORDER,
					    (GConfClientNotifyFunc)gconf_play_order_changed,
					    player);

	player->priv->header_widget = rb_header_new (player->priv->mmplayer);
	gtk_widget_show (GTK_WIDGET (player->priv->header_widget));
	gtk_box_pack_start (GTK_BOX (player), GTK_WIDGET (player->priv->header_widget), TRUE, TRUE, 0);

	player->priv->volume = eel_gconf_get_float (CONF_STATE_VOLUME);
	rb_shell_player_sync_volume (player, FALSE);

	g_signal_connect (player, "notify::playing",
			  G_CALLBACK (reemit_playing_signal), NULL);

	player->priv->gconf_song_position_slider_visibility_id =
		eel_gconf_notification_add (CONF_UI_SONG_POSITION_SLIDER_HIDDEN,
					    (GConfClientNotifyFunc) gconf_song_position_slider_visibility_changed,
					    player);

				 
#ifdef HAVE_MMKEYS
	/* Enable Multimedia Keys */
	rb_shell_player_init_mmkeys (player);
#endif /* HAVE_MMKEYS */
}

static void
rb_shell_player_finalize (GObject *object)
{
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PLAYER (object));

	player = RB_SHELL_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	eel_gconf_notification_remove (player->priv->gconf_play_order_id);

	eel_gconf_set_float (CONF_STATE_VOLUME, player->priv->volume);

	g_object_unref (G_OBJECT (player->priv->mmplayer));
	g_object_unref (G_OBJECT (player->priv->play_order));

	G_OBJECT_CLASS (rb_shell_player_parent_class)->finalize (object);
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
		if (player->priv->selected_source != NULL) {
			RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_player_entry_activated_cb),
							      player);
			for (; extra_views; extra_views = extra_views->next)
				g_signal_handlers_disconnect_by_func (G_OBJECT (extra_views->data),
								      G_CALLBACK (rb_shell_player_property_row_activated_cb),
								      player);
			g_list_free (extra_views);
			
		}

		
		player->priv->selected_source = g_value_get_object (value);
		rb_debug ("selected source %p", player->priv->selected_source);

		rb_shell_player_sync_with_selected_source (player);
		rb_shell_player_sync_buttons (player);

		if (player->priv->selected_source != NULL) {
			RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_connect_object (G_OBJECT (songs),
						 "entry-activated",
						 G_CALLBACK (rb_shell_player_entry_activated_cb),
						 player, 0);
			for (; extra_views; extra_views = extra_views->next)
				g_signal_connect_object (G_OBJECT (extra_views->data),
							 "property-activated",
							 G_CALLBACK (rb_shell_player_property_row_activated_cb),
							 player, 0);

			g_list_free (extra_views);
		}

		/* If we're not playing, change the play order's view of the current source;
		 * if the selected source is the queue, however, set it to NULL so it'll stop
		 * once the queue is empty.
		 */
		if (player->priv->current_playing_source == NULL) {
			RBSource *source = player->priv->selected_source;
			if (source == RB_SOURCE (player->priv->queue_source))
				source = NULL;

			rb_play_order_playing_source_changed (player->priv->play_order, source);
		}
		
		break;
	case PROP_UI_MANAGER:
		player->priv->ui_manager = g_value_get_object (value);
		break;
	case PROP_DB:
		player->priv->db = g_value_get_object (value);
		
		/* Listen for changed entries to update metadata display */
		g_signal_connect_object (G_OBJECT (player->priv->db),
			 "entry_changed",
			 G_CALLBACK (rb_shell_player_entry_changed_cb),
			 player, 0);
		break;
	case PROP_ACTION_GROUP:
		player->priv->actiongroup = g_value_get_object (value);
		break;
	case PROP_PLAY_ORDER:
		eel_gconf_set_string (CONF_STATE_PLAY_ORDER, 
				      g_value_get_string (value));
		break;
	case PROP_VOLUME:
		player->priv->volume = g_value_get_float (value);
		rb_shell_player_sync_volume (player, FALSE);
		break;
	case PROP_STATUSBAR:
		player->priv->statusbar_widget = g_value_get_object (value);
		break;
	case PROP_QUEUE_SOURCE:
		player->priv->queue_source = g_value_get_object (value);
		if (player->priv->queue_source) {
			RBEntryView *sidebar;

			player->priv->queue_play_order = rb_play_order_new ("queue", player);
			g_signal_connect_object (G_OBJECT (player->priv->queue_play_order),
						 "have_next_previous_changed",
						 G_CALLBACK (rb_shell_player_play_order_update_cb),
						 player, 0);
			rb_play_order_playing_source_changed (player->priv->queue_play_order,
							      RB_SOURCE (player->priv->queue_source));

			g_object_get (G_OBJECT (player->priv->queue_source), "sidebar", &sidebar, NULL);
			g_signal_connect_object (G_OBJECT (sidebar),
						 "entry-activated",
						 G_CALLBACK (rb_shell_player_entry_activated_cb),
						 player, 0);
			g_object_unref (sidebar);
		}
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
	case PROP_UI_MANAGER:
		g_value_set_object (value, player->priv->ui_manager);
		break;
	case PROP_DB:
		g_value_set_object (value, player->priv->db);
		break;
	case PROP_ACTION_GROUP:
		g_value_set_object (value, player->priv->actiongroup);
		break;
	case PROP_PLAY_ORDER:
	{
		char *play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
		if (!play_order)
			play_order = g_strdup ("linear");
		g_value_set_string_take_ownership (value, play_order);
		break;
	}
	case PROP_PLAYING:
		g_value_set_boolean (value, rb_player_playing (player->priv->mmplayer));
		break;
	case PROP_VOLUME:
		g_value_set_float (value, player->priv->volume);
		break;
	case PROP_STATUSBAR:
		g_value_set_object (value, player->priv->statusbar_widget);
		break;
	case PROP_QUEUE_SOURCE:
		g_value_set_object (value, player->priv->queue_source);
		break;
	case PROP_STREAM_SONG:
		g_value_set_string (value, player->priv->song);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GQuark
rb_shell_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_shell_player_error");

	return quark;
}

void
rb_shell_player_set_selected_source (RBShellPlayer *player,
				     RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));
	g_return_if_fail (RB_IS_SOURCE (source));

	g_object_set (G_OBJECT (player),
		      "source", source,
		      NULL);
}

RBSource *
rb_shell_player_get_playing_source (RBShellPlayer *player)
{
	return player->priv->current_playing_source;
}


RBShellPlayer *
rb_shell_player_new (RhythmDB *db, GtkUIManager *mgr, 
		     GtkActionGroup *actiongroup)
{
	return g_object_new (RB_TYPE_SHELL_PLAYER,			       
			     "ui-manager", mgr,
			     "action-group", actiongroup,
			     "db", db,
			     NULL);
}

RhythmDBEntry *
rb_shell_player_get_playing_entry (RBShellPlayer *player)
{
	RBPlayOrder *porder;
	if (player->priv->current_playing_source == NULL)
		return NULL;

	if (player->priv->current_playing_source == RB_SOURCE (player->priv->queue_source))
		porder = player->priv->queue_play_order;
	else
		porder = player->priv->play_order;

	if (!porder) {
		return NULL;
	}

	return rb_play_order_get_playing_entry (porder);
}

static void
rb_shell_player_open_playlist_location (TotemPlParser *playlist, const char *uri,
					const char *title, const char *genre,
					RBShellPlayer *player)
{
	GError *error = NULL;

	if (rb_player_playing (player->priv->mmplayer))
		return;

	if (!rb_player_open (player->priv->mmplayer, uri, &error)) {
		if (player->priv->playlist_parse_error != NULL) {
			g_error_free (player->priv->playlist_parse_error);
			player->priv->playlist_parse_error = NULL;
		}
		player->priv->playlist_parse_error = g_error_copy (error);
		return;
	}

	rb_player_play (player->priv->mmplayer, &error);
	if (error)
		player->priv->playlist_parse_error = g_error_copy (error);

	g_object_notify (G_OBJECT (player), "playing");
}


typedef struct {
	RBShellPlayer *player;
	char *location;
} OpenLocationThreadData;

static gpointer
open_location_thread (OpenLocationThreadData *data)
{
	TotemPlParser *playlist;
	TotemPlParserResult playlist_result;
	gboolean playlist_parsed;
	GError *error = NULL;

	rb_statusbar_set_progress (data->player->priv->statusbar_widget, 0.01, _("Connecting"));

	playlist = totem_pl_parser_new ();
	g_signal_connect_object (G_OBJECT (playlist), "entry",
				 G_CALLBACK (rb_shell_player_open_playlist_location),
				 data->player, 0);
	totem_pl_parser_add_ignored_mimetype (playlist, "x-directory/normal");

	playlist_result = totem_pl_parser_parse (playlist, data->location, FALSE);
	playlist_parsed = (playlist_result == TOTEM_PL_PARSER_RESULT_SUCCESS);
	g_object_unref (playlist);

	if (!playlist_parsed) {
		/* We get here if we failed to parse as a playlist */
		rb_player_open (data->player->priv->mmplayer, data->location, &error);
		if (error == NULL)
			rb_player_play (data->player->priv->mmplayer, &error);
	} else if (playlist_result == TOTEM_PL_PARSER_RESULT_ERROR
		   && data->player->priv->playlist_parse_error) {
		error = data->player->priv->playlist_parse_error;
		data->player->priv->playlist_parse_error = NULL;
	} else {
		rb_player_play (data->player->priv->mmplayer, &error);
		g_object_notify (G_OBJECT (data->player), "playing");
	}

	if (error)
		rb_shell_player_error (data->player, TRUE, error);

	g_free (data);
	return NULL;
}

static gboolean
rb_shell_player_open_location (RBShellPlayer *player,
			       const char *location,
			       GError **error)
{
	char *unescaped;
	gboolean was_playing;

	unescaped = gnome_vfs_unescape_string_for_display (location);
	rb_debug ("Opening %s...", unescaped);
	g_free (unescaped);

	was_playing = rb_player_playing (player->priv->mmplayer);

	if (!rb_player_close (player->priv->mmplayer, error))
		return FALSE;

	g_free (player->priv->song);
	player->priv->song = NULL;
	g_object_notify (G_OBJECT (player), "stream-song");

	if (rb_source_try_playlist (player->priv->source)) {
		OpenLocationThreadData *data;
	       
		data = g_new0 (OpenLocationThreadData, 1);
		data->player = player;

		/* add http:// as a prefix, if it doesn't have a URI scheme */
		if (strstr (location, "://"))
			data->location = g_strdup (location);
		else
			data->location = g_strconcat ("http://", location, NULL);

		g_thread_create ((GThreadFunc)open_location_thread, data, FALSE, NULL);
		return TRUE;
	} else {
		if (!rb_player_open (player->priv->mmplayer, location, error))
			return FALSE;

		if (!rb_player_play (player->priv->mmplayer, error))
			return FALSE;

		g_object_notify (G_OBJECT (player), "playing");
	}

	return TRUE;
}

static gboolean
rb_shell_player_open_entry (RBShellPlayer *player, RhythmDBEntry *entry, GError **error)
{
	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		return rb_shell_player_open_location (player, rb_refstring_get (entry->mountpoint), error);
	else
		return rb_shell_player_open_location (player, entry->location, error);
}

static gboolean
rb_shell_player_play (RBShellPlayer *player, GError **error)
{
	RBEntryView *songs = rb_source_get_entry_view (player->priv->current_playing_source);

	if (!rb_player_play (player->priv->mmplayer, error))
		return FALSE;

	rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PLAYING);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);

	return TRUE;
}

static void
rb_shell_player_set_entry_playback_error (RBShellPlayer *player,
					  RhythmDBEntry *entry,
					  char *message)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, message);
	rhythmdb_entry_set (player->priv->db,
			    entry,
			    RHYTHMDB_PROP_PLAYBACK_ERROR,
			    &value);
	g_value_unset (&value);
	rhythmdb_commit (player->priv->db);
}

static gboolean
do_next_idle (RBShellPlayer *player)
{
	/* use the EOS callback, so that EOF_SOURCE_ conditions are handled properly */
	eos_cb (player->priv->mmplayer, player);
	player->priv->do_next_idle_id = 0;

	return FALSE;
}

static gboolean
rb_shell_player_set_playing_entry (RBShellPlayer *player, 
				   RhythmDBEntry *entry, 
				   gboolean out_of_order, 
				   GError **error)
{
	GError *tmp_error = NULL;
	
	g_return_val_if_fail (player->priv->current_playing_source != NULL, TRUE);
	g_return_val_if_fail (entry != NULL, TRUE);

	if (out_of_order) {
		RBPlayOrder *porder;
		if (player->priv->current_playing_source == RB_SOURCE (player->priv->queue_source))
			porder = player->priv->queue_play_order;
		else
			porder = player->priv->play_order;
		rb_play_order_set_playing_entry (porder, entry);
	}

	if (!rb_shell_player_open_entry (player, entry, &tmp_error))
		goto lose;
	rb_shell_player_sync_replaygain (player, entry);

	rb_debug ("Success!");
	/* clear error on successful playback */
	g_free (entry->playback_error);
	entry->playback_error = NULL;

	g_signal_emit (G_OBJECT (player),
		       rb_shell_player_signals[PLAYING_SONG_CHANGED], 0,
		       entry);
	g_signal_emit (G_OBJECT (player),
		       rb_shell_player_signals[PLAYING_URI_CHANGED], 0,
		       entry->location);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");

	return TRUE;
 lose:
	/* Ignore errors, shutdown the player */
	rb_player_close (player->priv->mmplayer, NULL);
	if (tmp_error == NULL)
		tmp_error = g_error_new (RB_SHELL_PLAYER_ERROR,
					 RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
					 "Problem occurred without error being set. "
					 "This is a bug in Rhythmbox or GStreamer.");
	/* Mark this song as failed */
	rb_shell_player_set_entry_playback_error (player, entry, tmp_error->message);
	g_propagate_error (error, tmp_error);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");

	return FALSE;
}

static void
gconf_play_order_changed (GConfClient *client,guint cnxn_id,
			  GConfEntry *entry, RBShellPlayer *player)
{
	rb_debug ("gconf play order changed");
	player->priv->syncing_state = TRUE;
	rb_shell_player_sync_play_order (player);
	rb_shell_player_sync_buttons (player);
	rb_shell_player_sync_control_state (player);
	g_object_notify (G_OBJECT (player), "play-order");
	player->priv->syncing_state = FALSE;
}

gboolean
rb_shell_player_get_playback_state (RBShellPlayer *player,
				    gboolean *shuffle,
				    gboolean *repeat)
{
	int i, j;
	char *play_order;

	play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
	if (!play_order) {
		g_warning (CONF_STATE_PLAY_ORDER " gconf key not found!");
		return FALSE;
	}

	for (i = 0; i < G_N_ELEMENTS(state_to_play_order); i++)
		for (j = 0; j < G_N_ELEMENTS(state_to_play_order[0]); j++)
			if (!strcmp (play_order, state_to_play_order[i][j]))
				goto found;

	g_free (play_order);
	return FALSE;

found:
	*shuffle = i > 0;
	*repeat = j > 0;
	g_free (play_order);
	return TRUE;
}

static void 
rb_shell_player_set_play_order (RBShellPlayer *player, const gchar *new_val)
{
	char *old_val;
	g_object_get (G_OBJECT (player), "play-order", &old_val, NULL);
	if (strcmp (old_val, new_val) != 0) {
		/* The notify signal will be emitted by the gconf notifier */
		eel_gconf_set_string (CONF_STATE_PLAY_ORDER, new_val);
	}
	g_free (old_val);
}

void
rb_shell_player_set_playback_state (RBShellPlayer *player, gboolean shuffle, gboolean repeat)
{
	const char *neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	rb_shell_player_set_play_order (player, neworder);
}

static void
rb_shell_player_sync_play_order (RBShellPlayer *player)
{
	char *new_play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
	RhythmDBEntry *playing_entry = NULL;
	RBSource *source;

	if (!new_play_order) {
		g_warning (CONF_STATE_PLAY_ORDER " gconf key not found!");
		new_play_order = g_strdup ("linear");
	}

	if (player->priv->play_order != NULL) {
		playing_entry = rb_play_order_get_playing_entry (player->priv->play_order);
		g_signal_handlers_disconnect_by_func (G_OBJECT (player->priv->play_order),
						      G_CALLBACK (rb_shell_player_play_order_update_cb),
						      player);
		g_object_unref (player->priv->play_order);
	}

	player->priv->play_order = rb_play_order_new (new_play_order, player);
	g_signal_connect_object (G_OBJECT (player->priv->play_order),
				 "have_next_previous_changed",
				 G_CALLBACK (rb_shell_player_play_order_update_cb),
				 player, 0);

	source = player->priv->current_playing_source;
	if (!source)
		source = player->priv->selected_source;
	rb_play_order_playing_source_changed (player->priv->play_order, source);

	if (playing_entry)
		rb_play_order_set_playing_entry (player->priv->play_order, playing_entry);
	g_free (new_play_order);
}

static void
rb_shell_player_play_order_update_cb (RBPlayOrder *porder,
				      gboolean has_next,
				      gboolean has_previous,
				      RBShellPlayer *player)
{
	gboolean have_next;
	gboolean have_previous;
	gboolean not_empty;
	GtkAction *action;

	if (rb_shell_player_get_playing_entry (player)) {
		have_next = TRUE;
		have_previous = TRUE;
	} else {
		have_next = rb_play_order_has_next (player->priv->play_order);
		have_previous = rb_play_order_has_previous (player->priv->play_order);
		if (player->priv->queue_play_order) {
			have_next |= rb_play_order_has_next (player->priv->queue_play_order);
			have_previous |= rb_play_order_has_previous (player->priv->queue_play_order);
		}
	}
	
	not_empty = have_next || have_previous;

	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlPrevious");
	g_object_set (G_OBJECT (action), "sensitive", have_previous, NULL);
	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlNext");
	g_object_set (G_OBJECT (action), "sensitive", have_next, NULL);
}

static gboolean
rb_shell_player_jump_to_current_idle (RBShellPlayer *player)
{
	rb_shell_player_jump_to_current (player);
	return FALSE;
}

static void
rb_shell_player_sync_song_position_slider_visibility (RBShellPlayer *player)
{
	gboolean visible;
	GtkAction *action;

	visible = !eel_gconf_get_boolean (CONF_UI_SONG_POSITION_SLIDER_HIDDEN);

	rb_header_set_show_position_slider (player->priv->header_widget,
					    visible);

	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ViewSongPositionSlider");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
				      visible);
}

void
rb_shell_player_jump_to_current (RBShellPlayer *player)
{
	RBSource *source;
	RhythmDBEntry *entry;
	RBEntryView *songs;

	source = player->priv->current_playing_source ? player->priv->current_playing_source :
		player->priv->selected_source;

	songs = rb_source_get_entry_view (source);
	entry = rb_shell_player_get_playing_entry (player);	
	if (entry) {
		rb_entry_view_scroll_to_entry (songs, entry);
		rb_entry_view_select_entry (songs, entry);
	} else {
		rb_entry_view_select_none (songs);
	}
}

static void
swap_playing_source (RBShellPlayer *player, RBSource *new_source)
{
	if (player->priv->current_playing_source != NULL) {
		RBEntryView *old_songs = rb_source_get_entry_view (player->priv->current_playing_source);
		rb_entry_view_set_state (old_songs, RB_ENTRY_VIEW_NOT_PLAYING);
	}
	if (new_source != NULL) {
		RBEntryView *new_songs = rb_source_get_entry_view (new_source);
		rb_entry_view_set_state (new_songs, RB_ENTRY_VIEW_PLAYING);

		rb_shell_player_set_playing_source (player, new_source);
	}
}

gboolean
rb_shell_player_do_previous (RBShellPlayer *player, GError **error)
{
	RhythmDBEntry* entry = NULL;
	RBSource *new_source;

	if (player->priv->current_playing_source == NULL) {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
			     _("Not currently playing"));
		return FALSE;
	}

	rb_debug ("going to previous");

	if (player->priv->queue_play_order) {
		entry = rb_play_order_get_previous (player->priv->queue_play_order);
		if (entry) {
			new_source = RB_SOURCE (player->priv->queue_source);
			rb_play_order_go_previous (player->priv->queue_play_order);
		}
	}

	if (!entry) {
		new_source = player->priv->source;
		entry = rb_play_order_get_previous (player->priv->play_order);
		if (entry)
			rb_play_order_go_previous (player->priv->play_order);
	}
		
	if (entry) {
		rb_debug ("previous song found, doing previous");
		if (new_source != player->priv->current_playing_source)
			swap_playing_source (player, new_source);

		if (!rb_shell_player_set_playing_entry (player, entry, FALSE, error))
			return FALSE;
		rb_shell_player_jump_to_current (player);
	} else {
		rb_debug ("no previous song found, signaling error");
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
			     _("No previous song"));
		rb_shell_player_set_playing_source (player, NULL);
		return FALSE;
	}
		
	return TRUE;
}

gboolean
rb_shell_player_do_next (RBShellPlayer *player, GError **error)
{
	RBSource *new_source = NULL;
	RhythmDBEntry *entry = NULL;
	RhythmDBEntry *prev_entry = NULL;
	gboolean rv = TRUE;

	if (player->priv->source == NULL)
		return TRUE;

	prev_entry = rb_shell_player_get_playing_entry (player);

	/* look for something to play in the queue. 
	 * always call go_next to remove the current entry (if any) from the queue.
	 */
	if (player->priv->queue_play_order) {
		entry = rb_play_order_get_next (player->priv->queue_play_order);
		rb_play_order_go_next (player->priv->queue_play_order);
		if (entry)
			new_source = RB_SOURCE (player->priv->queue_source);
	}

	/* fall back to the playing source */
	if (entry == NULL) {
		entry = rb_play_order_get_next (player->priv->play_order);
		if (entry) {
			new_source = player->priv->source;
			rb_play_order_go_next (player->priv->play_order);
		}
	}

	/* play the new entry */
	if (entry) {
		/* if the entry view containing the playing entry changed, update it */
		if (new_source != player->priv->current_playing_source)
			swap_playing_source (player, new_source);

		if (!rb_shell_player_set_playing_entry (player, entry, FALSE, error))
			rv = FALSE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
			     _("No next song"));
		rb_debug ("No next entry, stopping playback");
		rb_shell_player_set_playing_source (player, NULL);
		rb_play_order_set_playing_entry (player->priv->play_order, NULL);
		g_object_notify (G_OBJECT (player), "playing");
		rv = FALSE;
	}
	g_idle_add ((GSourceFunc)rb_shell_player_jump_to_current_idle, player);
	
	return rv;
}

static gboolean
rb_shell_player_do_previous_or_seek (RBShellPlayer *player, GError **error)
{
	rb_debug ("previous");
	/* If we're in the first 3 seconds go to the previous song,
	 * else restart the current one.
	 */
	if (player->priv->current_playing_source != NULL
	    && rb_source_can_pause (player->priv->source)
	    && rb_player_get_time (player->priv->mmplayer) > 3) {

		/* see if there's anything to go back to */
		gboolean have_previous;
		have_previous = rb_play_order_has_previous (player->priv->play_order);
		if (player->priv->queue_play_order)
			have_previous |= rb_play_order_has_previous (player->priv->queue_play_order);

		if (have_previous) {
			rb_debug ("after 3 second previous, restarting song");
			rb_player_set_time (player->priv->mmplayer, 0);
			rb_header_sync_time (player->priv->header_widget);
			return TRUE;
		}
	} 
	
	return rb_shell_player_do_previous (player, error);
}

static void
rb_shell_player_cmd_previous (GtkAction *action,
			      RBShellPlayer *player)
{
	GError *error = NULL;
	
	if (!rb_shell_player_do_previous_or_seek (player, &error)) {
		if (error->domain != RB_SHELL_PLAYER_ERROR ||
		    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
			g_warning ("cmd_previous: Unhandled error: %s", error->message);
		else if (error->code == RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
			rb_shell_player_set_playing_source (player, NULL);
	}
}

static void
rb_shell_player_cmd_next (GtkAction *action,
			  RBShellPlayer *player)
{
	GError *error = NULL;
	
	if (!rb_shell_player_do_next (player, &error)) {
		if (error->domain != RB_SHELL_PLAYER_ERROR ||
		    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
			g_warning ("cmd_next: Unhandled error: %s", error->message);
		else if (error->code == RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
			rb_shell_player_set_playing_source (player, NULL);
	}
}

void
rb_shell_player_play_entry (RBShellPlayer *player,
			    RhythmDBEntry *entry)
{
	GError *error = NULL;
	rb_shell_player_set_playing_source (player, player->priv->selected_source);

	if (!rb_shell_player_set_playing_entry (player, entry, TRUE, &error)) {
		rb_shell_player_error (player, FALSE, error);
		g_clear_error (&error);
	}
}

static void
rb_shell_player_cmd_play (GtkAction *action,
			  RBShellPlayer *player)
{
	GError *error = NULL;
	rb_debug ("play!");
	if (!rb_shell_player_playpause (player, FALSE, &error))
		rb_error_dialog (NULL, _("Couldn't start playback: %s"), (error) ? error->message : "(null)");
	g_clear_error (&error);
}

gboolean
rb_shell_player_playpause (RBShellPlayer *player, gboolean ignore_stop, GError **error)
{
	gboolean ret;
	RBEntryView *songs;

	rb_debug ("doing playpause");

	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), TRUE);

	ret = TRUE;

	if (rb_player_playing (player->priv->mmplayer)) {
		if (rb_source_can_pause (player->priv->source)) {
			rb_debug ("pausing mm player");
			rb_player_pause (player->priv->mmplayer);
			songs = rb_source_get_entry_view (player->priv->current_playing_source);
			rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PAUSED);
		} else if (!ignore_stop) {
			rb_debug ("setting playing source to NULL");
			rb_shell_player_set_playing_source (player, NULL);
		}
	} else {
		RhythmDBEntry *entry;
		RBSource *new_source;
		gboolean out_of_order = FALSE;
		if (player->priv->source == NULL) {
			/* no current stream, pull one in from the currently
			 * selected source */
			rb_debug ("no playing source, using selected source");
			rb_shell_player_set_playing_source (player, player->priv->selected_source);
		}
		new_source = player->priv->current_playing_source;

		entry = rb_shell_player_get_playing_entry (player);
		if (entry == NULL) {
			/* queue takes precedence over selection */
			if (player->priv->queue_play_order) {
				entry = rb_play_order_get_next (player->priv->queue_play_order);
				if (entry != NULL) {
					new_source = RB_SOURCE (player->priv->queue_source);
					rb_play_order_go_next (player->priv->queue_play_order);
				}
			}

			/* selection takes precedence over first item in play order */
			if (entry == NULL) {
				songs = rb_source_get_entry_view (player->priv->source);
				GList* selection = rb_entry_view_get_selected_entries (songs);
				if (selection != NULL) {
					rb_debug ("choosing first selected entry");
					entry = (RhythmDBEntry*) selection->data;
					if (entry)
						out_of_order = TRUE;
				}
			}

			/* play order is last */
			if (entry == NULL) {
				rb_debug ("getting entry from play order");
				entry = rb_play_order_get_next (player->priv->play_order);
				if (entry != NULL)
					rb_play_order_go_next (player->priv->play_order);
			}

			if (entry != NULL) {
				/* if the entry view containing the playing entry changed, update it */
				if (new_source != player->priv->current_playing_source)
					swap_playing_source (player, new_source);

				if (!rb_shell_player_set_playing_entry (player, entry, out_of_order, error))
					ret = FALSE;
				rb_shell_player_jump_to_current (player);
			}
		} else {
			if (!rb_shell_player_play (player, error)) {
				rb_shell_player_set_playing_source (player, NULL);
				ret = FALSE;
			}
		}
	}

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");
	return ret;
}


static void
rb_shell_player_sync_control_state (RBShellPlayer *player)
{
	gboolean shuffle, repeat;
	GtkAction *action;
	rb_debug ("syncing control state");

	if (!rb_shell_player_get_playback_state (player, &shuffle,
						 &repeat))
		return;

	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlShuffle");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), shuffle);
	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlRepeat");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), repeat);
}

static void
rb_shell_player_sync_volume (RBShellPlayer *player, gboolean notify)
{
	if (player->priv->volume < 0.0)
		player->priv->volume = 0.0;
	else if (player->priv->volume > 1.0)
		player->priv->volume = 1.0;

	rb_player_set_volume (player->priv->mmplayer,
			      player->priv->mute ? 0.0 : player->priv->volume);

	eel_gconf_set_float (CONF_STATE_VOLUME, player->priv->volume);

	rb_shell_player_sync_replaygain (player, 
					 rb_shell_player_get_playing_entry (player));

	if (notify)
		g_object_notify (G_OBJECT (player), "volume");
}

void
rb_shell_player_toggle_mute (RBShellPlayer *player)
{
	player->priv->mute = !player->priv->mute;
	rb_shell_player_sync_volume (player, FALSE);
}

static void
rb_shell_player_sync_replaygain (RBShellPlayer *player, RhythmDBEntry *entry)
{
	double entry_track_gain = 0;
	double entry_track_peak = 0;
	double entry_album_gain = 0;
	double entry_album_peak = 0;
	
	if (entry != NULL) {
             	entry_track_gain = entry->track_gain;
             	entry_track_peak = entry->track_peak;
             	entry_album_gain = entry->album_gain;
             	entry_album_peak = entry->album_peak;
	}

	rb_player_set_replaygain (player->priv->mmplayer, entry_track_gain, 
				  entry_track_peak, entry_album_gain, entry_album_peak);
}

gboolean
rb_shell_player_set_volume (RBShellPlayer *player,
			    gdouble volume,
			    GError **error)
{
	player->priv->volume = volume;
	rb_shell_player_sync_volume (player, TRUE);
	return TRUE;
}

gboolean
rb_shell_player_set_volume_relative (RBShellPlayer *player,
				     gdouble delta,
				     GError **error)
{
	/* rb_shell_player_sync_volume does clipping */
	player->priv->volume += delta;
	rb_shell_player_sync_volume (player, TRUE);
	return TRUE;
}


gboolean
rb_shell_player_get_volume (RBShellPlayer *player,
			    gdouble *volume,
			    GError **error)
{
	*volume = player->priv->volume;
	return TRUE;
}

gboolean
rb_shell_player_set_mute (RBShellPlayer *player,
			  gboolean mute,
			  GError **error)
{
	player->priv->mute = mute;
	rb_shell_player_sync_volume (player, FALSE);
	return TRUE;
}

gboolean
rb_shell_player_get_mute (RBShellPlayer *player,
			  gboolean *mute,
			  GError **error)
{
	*mute = player->priv->mute;
	return TRUE;
}

static void
gconf_song_position_slider_visibility_changed (GConfClient *client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       RBShellPlayer *player)
{
	rb_debug ("song position slider visibility visibility changed"); 
	rb_shell_player_sync_song_position_slider_visibility (player);
}

static void
rb_shell_player_shuffle_changed_cb (GtkAction *action,
				    RBShellPlayer *player)
{
	const char *neworder;
	gboolean shuffle, repeat;

	if (player->priv->syncing_state)
		return;

	rb_debug ("shuffle changed");

	if (!rb_shell_player_get_playback_state (player, &shuffle, &repeat))
		return;
	shuffle = !shuffle;
	neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	rb_shell_player_set_play_order (player, neworder);
}
	
static void
rb_shell_player_repeat_changed_cb (GtkAction *action,
				   RBShellPlayer *player)
{
	const char *neworder;
	gboolean shuffle, repeat;
	rb_debug ("repeat changed");

	if (player->priv->syncing_state)
		return;

	if (!rb_shell_player_get_playback_state (player, &shuffle, &repeat))
		return;
	repeat = !repeat;
	neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	rb_shell_player_set_play_order (player, neworder);
}

static void
rb_shell_player_view_song_position_slider_changed_cb (GtkAction *action,
						      RBShellPlayer *player)
{
	eel_gconf_set_boolean (CONF_UI_SONG_POSITION_SLIDER_HIDDEN,
			       !gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}


static void
rb_shell_player_entry_activated_cb (RBEntryView *view,
				   RhythmDBEntry *entry,
				   RBShellPlayer *playa)
{
	gboolean was_from_queue = FALSE;
	RhythmDBEntry *prev_entry = NULL;
	GError *error = NULL;
	gboolean source_set = FALSE;

	g_return_if_fail (entry != NULL);

	rb_debug  ("got entry %p activated", entry);

	/* ensure the podcast has been downloaded */
	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		if (!rb_podcast_manager_entry_downloaded (entry))
			return;
	}
	
	/* figure out where the previous entry came from */
	if ((playa->priv->queue_source != NULL) &&
	    (playa->priv->current_playing_source == RB_SOURCE (playa->priv->queue_source))) {
		prev_entry = rb_shell_player_get_playing_entry (playa);
		was_from_queue = TRUE;
	}

	if (playa->priv->queue_source) {
		RBEntryView *queue_sidebar;
		g_object_get (G_OBJECT (playa->priv->queue_source), "sidebar", &queue_sidebar, NULL);
		if (view == queue_sidebar || view == rb_source_get_entry_view (RB_SOURCE (playa->priv->queue_source))) {
			/* fall back to the current selected source once the queue is empty */
			if (view == queue_sidebar && playa->priv->source == NULL) {
				rb_play_order_playing_source_changed (playa->priv->play_order, 
								      playa->priv->selected_source);
				playa->priv->source = playa->priv->selected_source;
			}
			
			/* queue entry activated: move it to the start of the queue */
			rb_static_playlist_source_move_entry (RB_STATIC_PLAYLIST_SOURCE (playa->priv->queue_source), entry, 0);
			rb_shell_player_set_playing_source (playa, RB_SOURCE (playa->priv->queue_source));

			/* since we just moved the entry, we should give it focus.
			 * just calling rb_shell_player_jump_to_current here
			 * looks terribly ugly, though. */
			g_idle_add ((GSourceFunc)rb_shell_player_jump_to_current_idle, playa);
			was_from_queue = FALSE;
			source_set = TRUE;
		}
		g_object_unref (G_OBJECT (queue_sidebar));
	}
	if (!source_set) {
		rb_shell_player_set_playing_source (playa, playa->priv->selected_source);
		source_set = TRUE;
	}

	if (!rb_shell_player_set_playing_entry (playa, entry, TRUE, &error)) {
		rb_shell_player_error (playa, FALSE, error);
		g_clear_error (&error);
	}

	/* if we were previously playing from the queue, clear its playing entry,
	 * so we'll start again from the start.
	 */
	if (was_from_queue && prev_entry != NULL) {
		rb_play_order_set_playing_entry (playa->priv->queue_play_order, NULL);
	}
}

static void
rb_shell_player_property_row_activated_cb (RBPropertyView *view,
					   const char *name,
					   RBShellPlayer *playa)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBQueryModel *model;
	GtkTreeIter iter;
	GError *error = NULL;

	rb_debug  ("got property activated");
	
	rb_shell_player_set_playing_source (playa, playa->priv->selected_source);

	/* RHYTHMDBFIXME - do we need to wait here until the query is finished?
	 * in theory, yes, but in practice the query is started when the row is
	 * selected (on the first click when doubleclicking, or when using the 
	 * keyboard to select then activate) and is pretty much always done by
	 * the time we get in here.
	 */
	g_object_get (G_OBJECT (playa->priv->selected_source), "query-model", &model, NULL);
	if (!model)
		return;
	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
		return;

	entry = rhythmdb_query_model_iter_to_entry (model, &iter); 
	if (!rb_shell_player_set_playing_entry (playa, entry, TRUE, &error)) {
		rb_shell_player_error (playa, FALSE, error);
		g_clear_error (&error);
	}
}

static void
rb_shell_player_entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry,
				       GSList *changes, RBShellPlayer *player)
{
	GSList *t;
	RhythmDBEntry *playing_entry = rb_shell_player_get_playing_entry (player);
	
	/* We try to update only if the changed entry is currently playing */
	if (entry != playing_entry) {
		return;
	}
	
	/* We update only if the artist, title or album has changed */
	for (t = changes; t; t = t->next)
	{
		RhythmDBEntryChange *change = t->data;
		switch (change->prop)
		{
			case RHYTHMDB_PROP_TITLE:
			case RHYTHMDB_PROP_ARTIST:
			case RHYTHMDB_PROP_ALBUM:
				rb_shell_player_sync_with_source (player);
				return;
			default:
				break;
		}
	}
}

static void
rb_shell_player_sync_with_source (RBShellPlayer *player)
{
	const char *entry_title = NULL;
	const char *artist = NULL;	
	char *title;
	RhythmDBEntry *entry;
	glong elapsed;

	entry = rb_shell_player_get_playing_entry (player);
	rb_debug ("playing source: %p, active entry: %p", player->priv->current_playing_source, entry);

	if (entry != NULL) {
		entry_title = rb_refstring_get (entry->title);
		artist = rb_refstring_get (entry->artist);
	}

	if (player->priv->have_url)
		rb_header_set_urldata (player->priv->header_widget,
				       entry_title,
				       player->priv->url);
	else
		rb_header_set_urldata (player->priv->header_widget,
				       NULL, NULL);

	if (player->priv->song && entry_title)
		title = g_strdup_printf ("%s (%s)", player->priv->song,
					 entry_title);
	else if (entry_title && artist && artist[0] != '\0')
		title = g_strdup_printf ("%s - %s", artist, entry_title);
	else if (entry_title)
		title = g_strdup (entry_title);
	else
		title = NULL;

	elapsed = rb_player_get_time (player->priv->mmplayer);

	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[WINDOW_TITLE_CHANGED], 0,
		       title);
	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[ELAPSED_CHANGED], 0,
		       (guint) elapsed);

	/* Sync the player */
	if (player->priv->song)
		rb_header_set_title (player->priv->header_widget, title);
	else
		rb_header_set_title (player->priv->header_widget, entry_title);
	g_free (title);
	
	rb_header_set_playing_entry (player->priv->header_widget, entry);
	rb_header_sync (player->priv->header_widget);
}

void
rb_shell_player_sync_buttons (RBShellPlayer *player)
{
	GtkAction *action;
	RBSource *source;
	gboolean not_small;
	gboolean playing_from_queue;
	RBEntryView *view;
	int entry_view_state;

	if (rb_shell_player_get_playing_entry (player)) {
		source = player->priv->current_playing_source;
		entry_view_state = rb_player_playing (player->priv->mmplayer) ?
			RB_ENTRY_VIEW_PLAYING : RB_ENTRY_VIEW_PAUSED;
	} else {
		source = player->priv->selected_source;
		entry_view_state = RB_ENTRY_VIEW_NOT_PLAYING;
	}
	
	source = rb_shell_player_get_playing_entry (player) == NULL ?
		 player->priv->selected_source : player->priv->current_playing_source;

	playing_from_queue = (source == RB_SOURCE (player->priv->queue_source));

	rb_debug ("syncing with source %p", source);

        not_small = !eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);
	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ViewJumpToPlaying");
	g_object_set (G_OBJECT (action),
		      "sensitive",
		      rb_shell_player_get_playing_entry (player) != NULL
		      && not_small, NULL);

	if (source) {
		view = rb_source_get_entry_view (source);
		rb_entry_view_set_state (view, entry_view_state);
	}
}

void
rb_shell_player_set_playing_source (RBShellPlayer *player,
				    RBSource *source)
{
	rb_shell_player_set_playing_source_internal (player, source, TRUE);
}

static void
actually_set_playing_source (RBShellPlayer *player,
			     RBSource *source, 
			     gboolean sync_entry_view)
{
	player->priv->source = source;
	player->priv->current_playing_source = source;

	if (source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->source);
		if (sync_entry_view) {
			rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PLAYING); 
		}
	}

	if (player->priv->play_order && source != RB_SOURCE (player->priv->queue_source)) {
		if (source == NULL)
			source = player->priv->selected_source;
		rb_play_order_playing_source_changed (player->priv->play_order, source);
	}
}

static void
rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
					     RBSource *source,
					     gboolean sync_entry_view)

{
	gboolean emit_source_changed = TRUE;

	if (player->priv->source == source && 
	    player->priv->current_playing_source == source &&
	    source != NULL)
		return;

	rb_debug ("setting playing source to %p", source);

	if (RB_SOURCE (player->priv->queue_source) == source) {

		if (player->priv->current_playing_source != source) {
			g_signal_emit (G_OBJECT (player), rb_shell_player_signals[PLAYING_FROM_QUEUE],
				       0, TRUE);
		}

		if (player->priv->source == NULL) {
			actually_set_playing_source (player, source, sync_entry_view);
		} else {
			emit_source_changed = FALSE;
			player->priv->current_playing_source = source;
		}

	} else {
		if (player->priv->current_playing_source != source) {
			g_signal_emit (G_OBJECT (player), rb_shell_player_signals[PLAYING_FROM_QUEUE],
				       0, FALSE);

			/* stop the old source */
			if (player->priv->current_playing_source != NULL) {
				if (sync_entry_view) {
					RBEntryView *songs = rb_source_get_entry_view (player->priv->current_playing_source);
					rb_debug ("source is already playing, stopping it");
					
					/* clear the playing entry if we're switching between non-queue sources */
					if (player->priv->current_playing_source != RB_SOURCE (player->priv->queue_source))
						rb_play_order_set_playing_entry (player->priv->play_order, NULL);

					rb_entry_view_set_state (songs, RB_ENTRY_VIEW_NOT_PLAYING);
				}
			}
		}
		actually_set_playing_source (player, source, sync_entry_view);
	}

	g_free (player->priv->url);
	g_free (player->priv->song);
	player->priv->song = NULL;
	player->priv->url = NULL;
	player->priv->have_url = FALSE;

	if (player->priv->current_playing_source == NULL)
		rb_shell_player_stop (player);

	rb_shell_player_sync_with_source (player);
	g_object_notify (G_OBJECT (player), "playing");
	if (player->priv->selected_source)
		rb_shell_player_sync_buttons (player);

	if (emit_source_changed) {
		g_signal_emit (G_OBJECT (player), rb_shell_player_signals[PLAYING_SOURCE_CHANGED],
			       0, player->priv->source);
	}
}

void
rb_shell_player_stop (RBShellPlayer *player)
{
	GError *error = NULL;
	rb_debug ("stopping");

	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	if (rb_player_playing (player->priv->mmplayer))
		rb_player_pause (player->priv->mmplayer);
	rb_player_close (player->priv->mmplayer, &error);
	if (error) {
		rb_error_dialog (NULL,
				 _("Couldn't stop playback"),
				 "%s", error->message);
		g_error_free (error);
	}

	rb_shell_player_sync_buttons (player);
}

gboolean
rb_shell_player_get_playing (RBShellPlayer *player,
			     gboolean *playing,
			     GError **error)
{
	if (playing != NULL) {
		*playing = FALSE;
		if (player->priv->current_playing_source) {
			RBEntryView *songs = rb_source_get_entry_view (player->priv->current_playing_source); 
			RBEntryViewState state;
			g_object_get (G_OBJECT (songs), "playing-state", &state, NULL);
			if (state == RB_ENTRY_VIEW_PLAYING)
				*playing = TRUE;
		}
	}

	return TRUE;
}

char *
rb_shell_player_get_playing_time_string (RBShellPlayer *player)
{
	return rb_header_get_elapsed_string (player->priv->header_widget);
}

gboolean
rb_shell_player_get_playing_time (RBShellPlayer *player,
				  guint *time,
				  GError **error)
{
	if (time != NULL)
		*time = (guint) rb_player_get_time (player->priv->mmplayer);

	return TRUE;
}

gboolean
rb_shell_player_set_playing_time (RBShellPlayer *player,
				  guint time,
				  GError **error)
{
	if (rb_player_seekable (player->priv->mmplayer)) {
		rb_player_set_time (player->priv->mmplayer, (long) time);
		return TRUE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_SEEKABLE,
			     _("Current song is not seekable"));
		return FALSE;
	}
}

void
rb_shell_player_seek (RBShellPlayer *player, long offset)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	if (rb_player_seekable (player->priv->mmplayer)) {
		long t = rb_player_get_time (player->priv->mmplayer);
		rb_player_set_time (player->priv->mmplayer, t + offset);
	}
}

long
rb_shell_player_get_playing_song_duration (RBShellPlayer *player)
{
	RhythmDBEntry *current_entry;
	
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), -1);
	
	current_entry = rb_shell_player_get_playing_entry (player);

	if (current_entry == NULL) {
		rb_debug ("Did not get playing entry : return -1 as length");
		return -1;
	}
	
	return current_entry->duration;
}

static void
rb_shell_player_sync_with_selected_source (RBShellPlayer *player)
{
	rb_debug ("syncing with selected source: %p", player->priv->selected_source);
	if (player->priv->source == NULL)
	{
		rb_debug ("no playing source, new source is %p", player->priv->selected_source);

		player->priv->have_url = rb_source_have_url (player->priv->selected_source);

		rb_shell_player_sync_with_source (player);
	}
}


static void
eos_cb (RBPlayer *mmplayer, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("eos!");

	GDK_THREADS_ENTER ();

	if (player->priv->current_playing_source != NULL) {
		RhythmDBEntry *entry = rb_shell_player_get_playing_entry (player);
		RBSource *source = player->priv->current_playing_source;

		switch (rb_source_handle_eos (source))
		{
		case RB_SOURCE_EOF_ERROR:
			rb_error_dialog (NULL, _("Stream error"),
					 _("Unexpected end of stream!"));
			rb_shell_player_set_playing_source (player, NULL);
			break;
		case RB_SOURCE_EOF_STOP:
			rb_shell_player_set_playing_source (player, NULL);
			break;
		case RB_SOURCE_EOF_RETRY: {
			GTimeVal current;
			g_get_current_time (&current);
			if (player->priv->did_retry
			    && (current.tv_sec - player->priv->last_retry.tv_sec) < 4) {
				rb_debug ("Last retry was less than 4 seconds ago...aborting retry playback");
				player->priv->did_retry = FALSE;
				rb_shell_player_set_playing_source (player, NULL);
				break;
			} else {
				player->priv->did_retry = TRUE;
				g_get_current_time (&(player->priv->last_retry));
				rb_shell_player_play_entry (player, entry);
			}
		}
			break;
		case RB_SOURCE_EOF_NEXT:
			{
				GError *error = NULL;
	
				if (!rb_shell_player_do_next (player, &error)) {
					if (error->domain != RB_SHELL_PLAYER_ERROR ||
					    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
						g_warning ("eos_cb: Unhandled error: %s", error->message);
					else if (error->code == RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST)
						rb_shell_player_set_playing_source (player, NULL);
				}
			}
			break;
		}

		rb_debug ("updating play statistics");
		rb_source_update_play_statistics (source,
						  player->priv->db,
						  entry);
	}

	GDK_THREADS_LEAVE ();
}

static void
rb_shell_player_error (RBShellPlayer *player, gboolean async, const GError *err)
{
	RhythmDBEntry *entry;

	g_return_if_fail (player->priv->handling_error == FALSE);

	player->priv->handling_error = TRUE;

	entry = rb_shell_player_get_playing_entry (player);

	rb_debug ("playback error while playing: %s", err->message);
	/* For synchronous errors the entry playback error has already been set */
	if (entry && async)
		rb_shell_player_set_entry_playback_error (player, entry, err->message);

	if (err->code == RB_PLAYER_ERROR_NO_AUDIO)
		rb_shell_player_set_playing_source (player, NULL);
	else if (player->priv->do_next_idle_id == 0)
		player->priv->do_next_idle_id = g_idle_add ((GSourceFunc)do_next_idle, player);

	player->priv->handling_error = FALSE;
}

static void
error_cb (RBPlayer *mmplayer, const GError *err, gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);

	if (player->priv->handling_error)
		return;

	if (player->priv->source == NULL) {
		rb_debug ("ignoring error (no source): %s", err->message);
		return;
	}

	GDK_THREADS_ENTER ();

	rb_shell_player_error (player, TRUE, err);
	
	rb_debug ("exiting error hander");
	GDK_THREADS_LEAVE ();
}

static void
tick_cb (RBPlayer *mmplayer, long elapsed, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);

	GDK_THREADS_ENTER ();

	rb_header_sync_time (player->priv->header_widget);

	if (rb_player_playing (mmplayer)) {
		static long last_elapsed = -1;
		if (last_elapsed != elapsed) {
			g_signal_emit (G_OBJECT (player), rb_shell_player_signals[ELAPSED_CHANGED],
				       0, (guint) elapsed);
			last_elapsed = elapsed;
		}
	}


	GDK_THREADS_LEAVE ();
}

static void
info_available_cb (RBPlayer *mmplayer,
                   RBMetaDataField field,
                   GValue *value,
                   gpointer data)
{
        RBShellPlayer *player = RB_SHELL_PLAYER (data);
        RhythmDBEntry *entry;
        RhythmDBPropType entry_field = 0;
        gboolean changed = FALSE;
        gboolean set_field = FALSE;
        rb_debug ("info: %d", field);

        /* Sanity check, this signal may come in after we stopped the
         * player */
        if (player->priv->source == NULL
            || !rb_player_opened (player->priv->mmplayer)) {
                rb_debug ("Got info_available but no playing source!");
                return;
        }

        GDK_THREADS_ENTER ();

	entry = rb_shell_player_get_playing_entry (player);
        if (entry == NULL) {
                rb_debug ("Got info_available but no playing entry!");
                goto out_unlock;
        }

	if (entry->type != RHYTHMDB_ENTRY_TYPE_IRADIO_STATION) {
		rb_debug ("Got info_available but entry isn't an iradio station");
		goto out_unlock;
	}

	switch (field)	{
	case RB_METADATA_FIELD_TITLE:
	{
		char *song = g_value_dup_string (value);
		if (!g_utf8_validate (song, -1, NULL)) {
			g_warning ("Invalid UTF-8 from internet radio: %s", song);
			goto out_unlock;
		}

		if ((!song && player->priv->song)
		    || !player->priv->song
		    || strcmp (song, player->priv->song)) {
			changed = TRUE;
			g_free (player->priv->song);
			player->priv->song = song;
			g_object_notify (G_OBJECT (player), "stream-song");
		}
		else
			g_free (song);
		break;
	}
	case RB_METADATA_FIELD_GENRE:
	{
		const char *genre = g_value_get_string (value);
		const char *existing;
		if (!g_utf8_validate (genre, -1, NULL)) {
			g_warning ("Invalid UTF-8 from internet radio: %s", genre);
			goto out_unlock;
		}

		/* check if the db entry already has a genre; if so, don't change it */
		existing = rb_refstring_get (entry->genre);
		if ((existing == NULL) || 
		    (strcmp (existing, "") == 0) ||
		    (strcmp (existing, _("Unknown")) == 0)) {
			entry_field = RHYTHMDB_PROP_GENRE;
			rb_debug ("setting genre of iradio station to %s", genre);
			set_field = TRUE;
		} else {
			rb_debug ("iradio station already has genre: %s; ignoring %s", existing, genre);
		}
		break;
	}
	case RB_METADATA_FIELD_COMMENT:
	{
		const char *name = g_value_get_string (value);
		const char *existing;
		if (!g_utf8_validate (name, -1, NULL)) {
			g_warning ("Invalid UTF-8 from internet radio: %s", name);
			goto out_unlock;
		}

		/* check if the db entry already has a title; if so, don't change it.
		 * consider title==URI to be the same as no title, since that's what
		 * happens for stations imported by DnD or commandline args.
		 * if the station title really is the same as the URI, then surely
		 * the station title in the stream metadata will say that too..
		 */
		existing = rb_refstring_get (entry->title);
		if ((existing == NULL) || 
		    (strcmp (existing, "") == 0) || 
		    (strcmp (existing, entry->location) == 0)) {
			entry_field = RHYTHMDB_PROP_TITLE;
			rb_debug ("setting title of iradio station to %s", name);
			set_field = TRUE;
		} else {
			rb_debug ("iradio station already has title: %s; ignoring %s", existing, name);
		}
		break;
	}
	case RB_METADATA_FIELD_BITRATE:
		if (!rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE)) {
			gulong bitrate;

			/* GStreamer sends us bitrate in bps, but we need it in kbps*/
			bitrate = g_value_get_ulong (value);
			g_value_set_ulong (value, bitrate/1000);
			
			rb_debug ("setting bitrate of iradio station to %d", 
				  g_value_get_ulong (value));
			entry_field = RHYTHMDB_PROP_BITRATE;
			set_field = TRUE;
		}
		break;
	default:
		break;
	}

	if (changed)
		rb_shell_player_sync_with_source (player);

	if (set_field && entry_field != 0) {
		rhythmdb_entry_set (player->priv->db, entry, entry_field, value);
		rhythmdb_commit (player->priv->db);
	}

 out_unlock:
	GDK_THREADS_LEAVE ();
}

static void
buffering_cb (RBPlayer *mmplayer, guint progress, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);

	GDK_THREADS_ENTER ();
	rb_statusbar_set_progress (player->priv->statusbar_widget, ((double)progress)/100, _("Buffering"));
	GDK_THREADS_LEAVE ();
}


gboolean
rb_shell_player_get_playing_path (RBShellPlayer *shell_player,
				  const gchar **path,
				  GError **error)
{
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (shell_player);
	if (entry != NULL) {
		*path = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	} else {
		*path = NULL;
	}

	return TRUE;
}

#ifdef HAVE_MMKEYS
static void
grab_mmkey (int key_code, GdkWindow *root)
{
	gdk_error_trap_push ();

	XGrabKey (GDK_DISPLAY (), key_code,
		  0,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod2Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod5Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod2Mask | Mod5Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod2Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod5Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (GDK_DISPLAY (), key_code,
		  Mod2Mask | Mod5Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	
	gdk_flush ();
        if (gdk_error_trap_pop ()) {
		rb_debug ("Error grabbing key");
	}
}

static GdkFilterReturn
filter_mmkeys (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	XEvent *xev;
	XKeyEvent *key;
	RBShellPlayer *player;
	xev = (XEvent *) xevent;
	if (xev->type != KeyPress) {
		return GDK_FILTER_CONTINUE;
	}

	key = (XKeyEvent *) xevent;

	player = (RBShellPlayer *)data;

	if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPlay) == key->keycode) {	
		rb_shell_player_playpause (player, TRUE, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPause) == key->keycode) {	
		gboolean playing;
		rb_shell_player_get_playing (player, &playing, NULL);
		if (playing) {
			rb_shell_player_playpause (player, TRUE, NULL);
		}
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioStop) == key->keycode) {
		rb_shell_player_set_playing_source (player, NULL);
		return GDK_FILTER_REMOVE;		
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPrev) == key->keycode) {
		rb_shell_player_cmd_previous (NULL, player);
		return GDK_FILTER_REMOVE;		
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioNext) == key->keycode) {
		rb_shell_player_cmd_next (NULL, player);
		return GDK_FILTER_REMOVE;
	} else {
		return GDK_FILTER_CONTINUE;
	}
}

static void
rb_shell_player_init_mmkeys (RBShellPlayer *shell_player)
{
	gint keycodes[] = {0, 0, 0, 0, 0};
	GdkDisplay *display;
	GdkScreen *screen;
	GdkWindow *root;
	guint i, j;

	keycodes[0] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPlay);
	keycodes[1] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioStop);
	keycodes[2] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPrev);
	keycodes[3] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioNext);
	keycodes[4] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPause);

	display = gdk_display_get_default ();

	for (i = 0; i < gdk_display_get_n_screens (display); i++) {
		screen = gdk_display_get_screen (display, i);

		if (screen != NULL) {
			root = gdk_screen_get_root_window (screen);

			for (j = 0; j < G_N_ELEMENTS (keycodes) ; j++) {
				if (keycodes[j] != 0)
					grab_mmkey (keycodes[j], root);
			}

			gdk_window_add_filter (root, filter_mmkeys,
					       (gpointer) shell_player);
		}
	}
}
#endif /* HAVE_MMKEYS */

static gboolean
_idle_unblock_signal_cb (gpointer data)
{
	RBShellPlayer *player = (RBShellPlayer *)data;
	GtkAction *action;
	gboolean playing;

	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlPlay");

	/* sync the active state of the action again */
	g_object_get (G_OBJECT (player), "playing", &playing, NULL);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), playing);
	
	g_signal_handlers_unblock_by_func (action, rb_shell_player_cmd_play, player);
	return FALSE;
}

static void
rb_shell_player_playing_changed_cb (RBShellPlayer *player,
				    GParamSpec *arg1,
				    gpointer user_data)
{
	GtkAction *action;
	gboolean playing;
	char *tooltip;

	g_object_get (G_OBJECT (player), "playing", &playing, NULL);
	action = gtk_action_group_get_action (player->priv->actiongroup,
					      "ControlPlay");
	if (playing) {
		tooltip = g_strdup (_("Stop playback"));
	} else {
		tooltip = g_strdup (_("Start playback"));
	}
	g_object_set (action, "tooltip", tooltip, NULL);
	g_free (tooltip);

	/* block the signal, so that it doesn't get stuck by triggering recursively,
	 * and don't unblock it until whatever else is happening has finished.
	 */
	g_signal_handlers_block_by_func (action, rb_shell_player_cmd_play, player);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), playing);
	g_idle_add (_idle_unblock_signal_cb, player);
}
