/* 
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

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <bonobo/bonobo-ui-util.h>
#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>

#ifdef HAVE_MMKEYS
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <gdk/gdkx.h>
#endif /* HAVE_MMKEYS */

#include "rb-property-view.h"
#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "rb-glade-helpers.h"
#include "rb-bonobo-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-file-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-dialog.h"
#include "rb-preferences.h"
#include "rb-debug.h"
#include "rb-player.h"
#include "rb-playlist.h"
#include "rb-volume.h"
#include "rb-remote.h"
#include "eel-gconf-extensions.h"

#include "rb-play-order.h"

typedef enum
{
	PLAY_BUTTON_PLAY,
	PLAY_BUTTON_PAUSE,
	PLAY_BUTTON_STOP
} PlayButtonState;

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
static void rb_shell_player_cmd_previous (BonoboUIComponent *component,
			                  RBShellPlayer *player,
			                  const char *verbname);
static void rb_shell_player_cmd_play (BonoboUIComponent *component,
			              RBShellPlayer *player,
			              const char *verbname);
static void rb_shell_player_cmd_pause (BonoboUIComponent *component,
			               RBShellPlayer *player,
			               const char *verbname);
static void rb_shell_player_cmd_stop (BonoboUIComponent *component,
			              RBShellPlayer *player,
			              const char *verbname);
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
static void rb_shell_player_cmd_song_info (BonoboUIComponent *component,
					   RBShellPlayer *player,
					   const char *verbname);
static void rb_shell_player_cmd_sl_properties (BonoboUIComponent *component,
					       RBShellPlayer *player,
					       const char *verbname);
static void rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
							 RBSource *source,
							 gboolean sync_entry_view);
static void rb_shell_player_set_play_button (RBShellPlayer *player,
			                     PlayButtonState state);
static void rb_shell_player_sync_with_source (RBShellPlayer *player);
static void rb_shell_player_sync_with_selected_source (RBShellPlayer *player);

static void rb_shell_player_playing_entry_deleted_cb (RBEntryView *view,
						      RhythmDBEntry *entry,
						      RBShellPlayer *playa);
static void rb_shell_player_entry_view_changed_cb (RBEntryView *view,
						   RBShellPlayer *playa);
static void rb_shell_player_entry_activated_cb (RBEntryView *view,
						RhythmDBEntry *entry,
						RBShellPlayer *playa);
static void rb_shell_player_property_row_activated_cb (RBPropertyView *view,
						       const char *name,
						       RBShellPlayer *playa);
static void rb_shell_player_state_changed_cb (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBShellPlayer *playa);
static void rb_shell_player_sync_volume (RBShellPlayer *player);
static void tick_cb (MonkeyMediaPlayer *player, long elapsed, gpointer data);
static void eos_cb (MonkeyMediaPlayer *player, gpointer data);
static void error_cb (MonkeyMediaPlayer *player, GError *err, gpointer data);
static void rb_shell_player_error (RBShellPlayer *player, GError *err,
				   gboolean lock);

static void cancel_buffering_dialog (RBShellPlayer *player);

static void info_available_cb (MonkeyMediaPlayer *player,
			       MonkeyMediaStreamInfoField field,
			       GValue *value,
			       gpointer data);
static void cancel_buffering_clicked_cb (GtkWidget *button,
					 gpointer data);
static void buffering_end_cb (MonkeyMediaPlayer *player, gpointer data);
static void buffering_begin_cb (MonkeyMediaPlayer *player, gpointer data);
static void rb_shell_player_set_play_order (RBShellPlayer *player);

#ifdef HAVE_MMKEYS
static void grab_mmkey (int key_code, GdkWindow *root);
static GdkFilterReturn filter_mmkeys (GdkXEvent *xevent,
				      GdkEvent *event,
				      gpointer data);
static void rb_shell_player_init_mmkeys (RBShellPlayer *shell_player);
#endif /* HAVE_MMKEYS */

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
	RhythmDB *db;
	
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

	gboolean have_previous_entry;

	RBPlayOrder *play_order;

	gboolean buffering_blocked;
	GtkWidget *buffering_dialog;
	GtkWidget *buffering_progress;
	guint buffering_progress_idle_id;

	GError *playlist_parse_error;

	GtkTooltips *tooltips;
	GtkWidget *prev_button;
	PlayButtonState playbutton_state;
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
	PROP_PLAYING_SOURCE,
	PROP_COMPONENT,
	PROP_TRAY_COMPONENT,
	PROP_REPEAT,
	PROP_SHUFFLE,
	PROP_PLAY_ORDER,
	PROP_PLAYING,
};

enum
{
	WINDOW_TITLE_CHANGED,
	DURATION_CHANGED,
	LAST_SIGNAL
};

static BonoboUIVerb rb_shell_player_verbs[] =
{
	BONOBO_UI_VERB ("Previous",	(BonoboUIVerbFn) rb_shell_player_cmd_previous),
	BONOBO_UI_VERB ("Play",		(BonoboUIVerbFn) rb_shell_player_cmd_play),
	BONOBO_UI_VERB ("Pause",	(BonoboUIVerbFn) rb_shell_player_cmd_pause),
	BONOBO_UI_VERB ("Stop",		(BonoboUIVerbFn) rb_shell_player_cmd_stop),
	BONOBO_UI_VERB ("Next",		(BonoboUIVerbFn) rb_shell_player_cmd_next),
	BONOBO_UI_VERB ("SongInfo",	(BonoboUIVerbFn) rb_shell_player_cmd_song_info),
	BONOBO_UI_VERB ("SLProperties",	(BonoboUIVerbFn) rb_shell_player_cmd_sl_properties),
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

static void
gconf_key_changed (GConfClient *client,guint cnxn_id,
		   GConfEntry *entry, RBShellPlayer *player)
{
	if (strcmp (CONF_STATE_REPEAT, entry->key) == 0) {
		g_object_notify (G_OBJECT (player), "repeat");
	} else 	if (strcmp (CONF_STATE_PLAY_ORDER, entry->key) == 0) {
		rb_shell_player_set_play_order (player);
		g_object_notify (G_OBJECT (player), "play-order");
	}
}


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

void 
rb_shell_player_set_repeat (RBShellPlayer *player, gboolean new_val)
{
	gboolean old_val;
	/* Warning: there's probably a small race if the value stored in 
	 * gconf is modified after the old_val != new_val test, but that
	 * doesn't matter that much
	 */
	g_object_get (G_OBJECT (player), "repeat", &old_val, NULL);
	if (old_val != new_val) {
		/* The notify signal will be emitted by the gconf notifier */
		eel_gconf_set_boolean (CONF_STATE_REPEAT, new_val);
	}
}


void 
rb_shell_player_set_shuffle (RBShellPlayer *player, gboolean new_val)
{
	gboolean old_val;
	/* Warning: there's probably a small race if the value stored in 
	 * gconf is modified after the old_val != new_val test, but that
	 * doesn't matter that much
	 */
	g_object_get (G_OBJECT (player), "shuffle", &old_val, NULL);
	if (old_val != new_val) {
		/* The notify signal will be emitted by the gconf notifier */
		if (new_val) {
			eel_gconf_set_string (CONF_STATE_PLAY_ORDER, "shuffle");
		} else {
			eel_gconf_set_string (CONF_STATE_PLAY_ORDER, "linear");
		}
	}
}

static void
rb_shell_player_class_init (RBShellPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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
					 PROP_PLAYING_SOURCE,
					 g_param_spec_object ("playing-source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READABLE));

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

	/* If you change these, be sure to update the CORBA interface
	 * in rb-shell.c! */
	g_object_class_install_property (object_class,
					 PROP_REPEAT,
					 g_param_spec_boolean ("repeat", 
							       "repeat", 
							       "Whether repeat is enabled or not", 
							       FALSE,
							       G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_SHUFFLE,
					 g_param_spec_boolean ("shuffle", 
							       "shuffle", 
							       "Whether shuffle is enabled or not", 
							       FALSE,
							       G_PARAM_READABLE));
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

	rb_shell_player_signals[DURATION_CHANGED] =
		g_signal_new ("duration_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, duration_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static GObject *
rb_shell_player_constructor (GType type, guint n_construct_properties,
			     GObjectConstructParam *construct_properties)
{
	RBShellPlayer *player;
	RBShellPlayerClass *klass;
	GObjectClass *parent_class;  

	klass = RB_SHELL_PLAYER_CLASS (g_type_class_peek (type));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	player = RB_SHELL_PLAYER (parent_class->constructor (type, n_construct_properties,
							     construct_properties));

	rb_shell_player_set_play_order (player);

	return G_OBJECT (player);
}

static void
rb_shell_player_init (RBShellPlayer *player)
{
	GError *error = NULL;
	GtkWidget *hbox, *image;
	GtkWidget *alignment;

	player->priv = g_new0 (RBShellPlayerPrivate, 1);

	player->priv->mmplayer = monkey_media_player_new (&error);
	if (error != NULL) {
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

	eel_gconf_notification_add (CONF_STATE_REPEAT,
				    (GConfClientNotifyFunc)gconf_key_changed,
				    player);
	eel_gconf_notification_add (CONF_STATE_PLAY_ORDER,
				    (GConfClientNotifyFunc)gconf_key_changed,
				    player);

	rb_shell_player_sync_volume (player);

	hbox = gtk_hbox_new (FALSE, 5);

	player->priv->tooltips = gtk_tooltips_new ();
	gtk_tooltips_enable (player->priv->tooltips);

	/* Previous button */
	image = gtk_image_new_from_stock (RB_STOCK_PREVIOUS,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);

	player->priv->prev_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->prev_button), image);
	g_signal_connect_swapped (G_OBJECT (player->priv->prev_button),
				  "clicked", G_CALLBACK (rb_shell_player_do_previous), player);
	gtk_tooltips_set_tip (GTK_TOOLTIPS (player->priv->tooltips), 
			      GTK_WIDGET (player->priv->prev_button), 
			      _("Go to previous song"), NULL);

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
	player->priv->playbutton_state = PLAY_BUTTON_PLAY;

	g_signal_connect_swapped (G_OBJECT (player->priv->play_pause_stop_button),
				  "clicked", G_CALLBACK (rb_shell_player_playpause), player);

	/* Next button */
	image = gtk_image_new_from_stock (RB_STOCK_NEXT,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	player->priv->next_button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (player->priv->next_button), image);
	g_signal_connect_swapped (G_OBJECT (player->priv->next_button),
				  "clicked", G_CALLBACK (rb_shell_player_do_next), player);
	gtk_tooltips_set_tip (GTK_TOOLTIPS (player->priv->tooltips), 
			      GTK_WIDGET (player->priv->next_button), 
			      _("Go to next song"), NULL);

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

	player->priv->volume_button = GTK_WIDGET (rb_volume_new ());

	gtk_tooltips_set_tip (GTK_TOOLTIPS (player->priv->tooltips), 
			      GTK_WIDGET (player->priv->volume_button), 
			      _("Change the music volume"), NULL);

	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	gtk_container_add (GTK_CONTAINER (alignment), player->priv->volume_button);
	gtk_box_pack_end (GTK_BOX (player), alignment, FALSE, TRUE, 0);

	eel_gconf_notification_add (CONF_STATE,
				    (GConfClientNotifyFunc) rb_shell_player_state_changed_cb,
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

	eel_gconf_set_float (CONF_STATE_VOLUME,
			     monkey_media_player_get_volume (player->priv->mmplayer));

	g_object_unref (G_OBJECT (player->priv->mmplayer));

	g_object_unref (G_OBJECT (player->priv->play_order));

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
			RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_player_entry_view_changed_cb),
							      player);
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
		rb_debug ("selected source %p", g_value_get_object (value));

		rb_shell_player_sync_with_selected_source (player);
		rb_shell_player_sync_buttons (player);

		if (player->priv->selected_source != NULL)
		{
			RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
			GList *extra_views = rb_source_get_extra_views (player->priv->selected_source);

			g_signal_connect (G_OBJECT (songs),
					  "changed",
					  G_CALLBACK (rb_shell_player_entry_view_changed_cb),
					  player);
			g_signal_connect (G_OBJECT (songs),
					  "entry-activated",
					  G_CALLBACK (rb_shell_player_entry_activated_cb),
					  player);
			for (; extra_views; extra_views = extra_views->next)
				g_signal_connect (G_OBJECT (extra_views->data),
						  "property-activated",
						  G_CALLBACK (rb_shell_player_property_row_activated_cb),
						  player);

			g_list_free (extra_views);
			
			/* Set database object */
			g_object_get (G_OBJECT (player->priv->selected_source),
				      "db", &player->priv->db, NULL);
			g_object_set (G_OBJECT (player->priv->player_widget),
				      "db", player->priv->db, NULL);
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
	case PROP_REPEAT:
		eel_gconf_set_boolean (CONF_STATE_REPEAT, 
				       g_value_get_boolean (value));
		break;

	case PROP_SHUFFLE:
		eel_gconf_set_string (CONF_STATE_PLAY_ORDER, 
				      g_value_get_boolean (value) ? "shuffle" : "linear");
		break;

	case PROP_PLAY_ORDER:
		eel_gconf_set_string (CONF_STATE_PLAY_ORDER, 
				      g_value_get_string (value));
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
	case PROP_PLAYING_SOURCE:
		g_value_set_object (value, player->priv->source);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, player->priv->component);
		break;
	case PROP_TRAY_COMPONENT:
		g_value_set_object (value, player->priv->tray_component);
		break;
	case PROP_REPEAT:
		g_value_set_boolean (value, eel_gconf_get_boolean (CONF_STATE_REPEAT));
		break;
	case PROP_SHUFFLE: {
		char *play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
		g_value_set_boolean (value, (strcmp (play_order, "linear") != 0));
		g_free (play_order);
		} break;
	case PROP_PLAY_ORDER:
		g_value_set_string_take_ownership (value, eel_gconf_get_string (CONF_STATE_PLAY_ORDER));
		break;
	case PROP_PLAYING:
		g_value_set_boolean (value, monkey_media_player_playing (player->priv->mmplayer));
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
	return player->priv->source;
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

static RhythmDBEntry *
rb_shell_player_get_playing_entry (RBShellPlayer *player)
{
	RBEntryView *songs;
	if (player->priv->source) {
		songs = rb_source_get_entry_view (player->priv->source);
		return rb_entry_view_get_playing_entry (songs);
	}
	return NULL;
}

static gboolean
rb_shell_player_have_first (RBShellPlayer *player, RBSource *source)
{
	RBEntryView *songs;
	if (source) {
		songs = rb_source_get_entry_view (source);
		return rb_entry_view_get_first_entry (songs) != NULL;
	}
	return FALSE;
}

static void
rb_shell_player_open_playlist_location (RBPlaylist *playlist, const char *uri,
					const char *title, const char *genre,
					RBShellPlayer *player)
{
	GError *error = NULL;

	if (monkey_media_player_playing (player->priv->mmplayer))
		return;

	monkey_media_player_open (player->priv->mmplayer, uri, &error);
	if (error != NULL) {
		if (player->priv->playlist_parse_error != NULL) {
			g_error_free (player->priv->playlist_parse_error);
			player->priv->playlist_parse_error = NULL;
		}
		player->priv->playlist_parse_error = g_error_copy (error);
		return;
	}

	monkey_media_player_play (player->priv->mmplayer);

	g_object_notify (G_OBJECT (player), "playing");
}

static void
rb_shell_player_open_location (RBShellPlayer *player,
			       const char *location,
			       GError **error)
{
	char *unescaped = gnome_vfs_unescape_string_for_display (location);
	char *msg = g_strdup_printf (_("Opening %s..."), unescaped);
	gboolean was_playing;
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

	was_playing = monkey_media_player_playing (player->priv->mmplayer);

	monkey_media_player_close (player->priv->mmplayer);

	if (rb_uri_is_iradio (location) != FALSE
	    && rb_playlist_can_handle (location) != FALSE) {
		RBPlaylist *playlist;

		playlist = rb_playlist_new ();
		g_signal_connect_object (G_OBJECT (playlist), "entry",
					 G_CALLBACK (rb_shell_player_open_playlist_location),
					 player, 0);
		if (rb_playlist_parse (playlist, location) == FALSE) {
			g_object_unref (playlist);
			goto normal_open;
		}
		g_object_unref (playlist);
		if (!monkey_media_player_playing (player->priv->mmplayer)) {
			if (error) {
				*error = g_error_copy (player->priv->playlist_parse_error);
				g_error_free (player->priv->playlist_parse_error);
				player->priv->playlist_parse_error = NULL;
			}
		}
		return;
	}
 normal_open:
	monkey_media_player_open (player->priv->mmplayer, location, error);
	if (error && *error)
		return;

	monkey_media_player_play (player->priv->mmplayer);

	if (!was_playing) {
		g_object_notify (G_OBJECT (player), "playing");
	}
}

static void
rb_shell_player_open_entry (RBShellPlayer *player, RhythmDBEntry *entry, GError **error)
{
	const char *location;

	rhythmdb_read_lock (player->priv->db);

	location = rhythmdb_entry_get_string (player->priv->db, entry,
					      RHYTHMDB_PROP_LOCATION);

	rhythmdb_read_unlock (player->priv->db);

	rb_shell_player_open_location (player, location, error);
	if (*error == NULL)
		return;

	fprintf (stderr, "Got error opening \"%s\": %s\n", location, (*error)->message);
}


static void
rb_shell_player_play (RBShellPlayer *player)
{
	RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);

	rb_entry_view_set_playing (songs, TRUE);

	monkey_media_player_play (player->priv->mmplayer);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
}

static void
rb_shell_player_set_playing_entry (RBShellPlayer *player, RhythmDBEntry *entry)
{
	GError *error = NULL;
	RBEntryView *songs;
	
	g_return_if_fail (player->priv->source != NULL);
	g_return_if_fail (entry != NULL);
	
	songs = rb_source_get_entry_view (player->priv->source);

	rb_shell_player_open_entry (player, entry, &error);

	if (error != NULL) {
		rb_error_dialog (error->message);
		return;
	}

	rb_debug ("Success!");
	rb_entry_view_set_playing_entry (songs, entry);
	rb_shell_player_play (player);
	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
}

static void
rb_shell_player_set_play_order (RBShellPlayer *player)
{
	static char *current_play_order = NULL;

	char *new_play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
	if (!new_play_order) {
		g_critical (CONF_STATE_PLAY_ORDER " gconf key not found!");
		new_play_order = g_strdup ("");
	}

	if (current_play_order == NULL
			|| strcmp (current_play_order, new_play_order) != 0) {
		g_free (current_play_order);

		if (player->priv->play_order != NULL)
			g_object_unref (player->priv->play_order);

		player->priv->play_order = rb_play_order_new (new_play_order, player);

		current_play_order = new_play_order;
	}
}

void
rb_shell_player_jump_to_current (RBShellPlayer *player)
{
	RBSource *source;
	RhythmDBEntry *entry;
	RBEntryView *songs;

	source = player->priv->source ? player->priv->source :
		player->priv->selected_source;

	songs = rb_source_get_entry_view (source);

	entry = rb_shell_player_get_playing_entry (player);	

	if (entry == NULL)
		return;
	
	rb_entry_view_scroll_to_entry (songs, entry);
	rb_entry_view_select_entry (songs, rb_entry_view_get_playing_entry (songs));
}

void
rb_shell_player_do_previous (RBShellPlayer *player)
{
	RhythmDBEntry* entry;

	if (player->priv->source != NULL) {
		/* If we're in the first 2 seconds go to the previous song,
		 * else restart the current one.
		 */
		if (monkey_media_player_get_time (player->priv->mmplayer) < 3) {
			rb_debug ("doing previous");
			entry = rb_play_order_get_previous (player->priv->play_order);
			if (entry) {
				rb_play_order_go_previous (player->priv->play_order);
				rb_shell_player_set_playing_entry (player, entry);
			} else {
				/* Is this the right thing to do when there's no previous song? */
				rb_debug ("No previous entry, restarting song");
				monkey_media_player_set_time (player->priv->mmplayer, 0);
				rb_player_sync_time (player->priv->player_widget);
			}
		} else {
			rb_debug ("restarting song");
			monkey_media_player_set_time (player->priv->mmplayer, 0);
			rb_player_sync_time (player->priv->player_widget);
		}

		rb_shell_player_jump_to_current (player);
	}
}

void
rb_shell_player_do_next (RBShellPlayer *player)
{
	if (player->priv->source != NULL) {
		RhythmDBEntry *entry = rb_play_order_get_next (player->priv->play_order);
		if (entry) {
			rb_play_order_go_next (player->priv->play_order);
			rb_shell_player_set_playing_entry (player, entry);
		} else {
			rb_debug ("No next entry, stopping playback");
			rb_shell_player_set_playing_source (player, NULL);
			g_object_notify (G_OBJECT (player), "playing");
		}
		rb_shell_player_jump_to_current (player);
	}
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
rb_shell_player_cmd_next (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_debug ("next");
	rb_shell_player_do_next (player);
}

void
rb_shell_player_play_entry (RBShellPlayer *player,
			    RhythmDBEntry *entry)
{
	rb_shell_player_set_playing_source (player, player->priv->selected_source);
	rb_shell_player_set_playing_entry (player, entry);
}

static void
rb_shell_player_cmd_play (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_debug ("play!");
	rb_shell_player_playpause (player);
}

void
rb_shell_player_playpause (RBShellPlayer *player)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	switch (player->priv->playbutton_state) {
	case PLAY_BUTTON_STOP:
		rb_debug ("setting playing source to NULL");
		rb_shell_player_set_playing_source (player, NULL);
		break;
	case PLAY_BUTTON_PAUSE:
		rb_debug ("pausing mm player");
		monkey_media_player_pause (player->priv->mmplayer);
		break;
	case PLAY_BUTTON_PLAY:
	{
		RhythmDBEntry *entry;
		if (player->priv->source == NULL) {
			/* no current stream, pull one in from the currently
			 * selected source */
			rb_debug ("no playing source, using selected source");
			rb_shell_player_set_playing_source (player, player->priv->selected_source);
		}

		entry = rb_shell_player_get_playing_entry (player);
		if (entry == NULL) {
			RBEntryView *songs = rb_source_get_entry_view (player->priv->source);

			GList* selection = rb_entry_view_get_selected_entries (songs);
			if (selection != NULL) {
				rb_debug ("choosing first selected entry");
				entry = (RhythmDBEntry*) selection->data;
			} else {
				entry = rb_play_order_get_next (player->priv->play_order);
			}
			if (entry != NULL) {
				rb_play_order_go_next (player->priv->play_order);
				rb_shell_player_set_playing_entry (player, entry);
			}
		} else {
			rb_shell_player_play (player);
		}
	}
	break;
	default:
		g_assert_not_reached ();
	}
	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");
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
	char *play_order;
	
	rb_debug ("syncing control state");
	play_order = eel_gconf_get_string (CONF_STATE_PLAY_ORDER);
	rb_bonobo_set_active (player->priv->component,
			      CMD_PATH_SHUFFLE,
			      strcmp (play_order, "linear") != 0);
	g_free (play_order);
	rb_bonobo_set_active (player->priv->component,
			      CMD_PATH_REPEAT,
			      eel_gconf_get_boolean (CONF_STATE_REPEAT));
}

static void
rb_shell_player_sync_volume (RBShellPlayer *player)
{
	float volume = eel_gconf_get_float (CONF_STATE_VOLUME);
	if (volume < 0.0)
		volume = 0.0;
	else if (volume > 1.0)
		volume = 1.0;
	monkey_media_player_set_volume (player->priv->mmplayer,
					volume);
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
	gboolean newval = rb_bonobo_get_active (component, CMD_PATH_SHUFFLE);
	rb_debug ("shuffle changed");

	rb_shell_player_set_shuffle (player, newval);
}
	
static void rb_shell_player_repeat_changed_cb (BonoboUIComponent *component,
					       const char *path,
					       Bonobo_UIComponent_EventType type,
					       const char *state,
					       RBShellPlayer *player)
{
	gboolean newval = rb_bonobo_get_active (component, CMD_PATH_REPEAT);
	rb_debug ("repeat changed");

	rb_shell_player_set_repeat (player, newval);
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
rb_shell_player_cmd_sl_properties (BonoboUIComponent *component,
				   RBShellPlayer *player,
				   const char *verbname)
{
	rb_debug ("sl properties");
	
	rb_source_song_properties (player->priv->selected_source);
}

static void
rb_shell_player_playing_entry_deleted_cb (RBEntryView *view,
					  RhythmDBEntry *entry,
					  RBShellPlayer *playa)
{
	rb_debug ("playing entry removed!");
	/* Here we are called via a signal from the entry view.
	 * Thus, we ensure we don't call back into the entry view
	 * to change things again.  When the playing entry is removed,
	 * the entry view takes care of setting itself to stop playing.
	 */
	rb_shell_player_set_playing_source_internal (playa, NULL, FALSE);
}

static void
rb_shell_player_entry_view_changed_cb (RBEntryView *view,
				       RBShellPlayer *playa)
{
	rb_debug ("entry view changed");
	rb_shell_player_sync_buttons (playa);
}

static void
rb_shell_player_entry_activated_cb (RBEntryView *view,
				   RhythmDBEntry *entry,
				   RBShellPlayer *playa)
{
	g_return_if_fail (entry != NULL);

	rb_debug  ("got entry %p activated", entry);
	
	rb_shell_player_set_playing_source (playa, playa->priv->selected_source);

	rb_shell_player_set_playing_entry (playa, entry);
}

static void
rb_shell_player_property_row_activated_cb (RBPropertyView *view,
					   const char *name,
					   RBShellPlayer *playa)
{
	RhythmDBEntry *entry;
	RBEntryView *songs;

	rb_debug  ("got property activated");
	
	rb_shell_player_set_playing_source (playa, playa->priv->selected_source);

	/* RHYTHMDBFIXME - do we need to wait here until the query is finished?
	 */
	songs = rb_source_get_entry_view (playa->priv->source);
	entry = rb_entry_view_get_first_entry (songs);

	if (entry != NULL) {
		rb_shell_player_set_playing_entry (playa, entry);
	}
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
		tlabel = _("Pause playback");
		mlabel = _("_Pause");
		verb = "Pause";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->pause_image);
		break;
	case PLAY_BUTTON_PLAY:
		rb_debug ("setting play button");
		tlabel = _("Start playing");
		mlabel = _("_Play");
		verb = "Play";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->play_image);
		break;
	case PLAY_BUTTON_STOP:
		rb_debug ("setting STOP button");
		tlabel = _("Stop playback");
		mlabel = _("_Stop");
		verb = "Stop";
		gtk_container_add (GTK_CONTAINER (player->priv->play_pause_stop_button),
				   player->priv->stop_image);
		break;
	default:
		g_error ("Should not get here!");
		break;
	}
	
	gtk_tooltips_set_tip (GTK_TOOLTIPS (player->priv->tooltips), 
			      GTK_WIDGET (player->priv->play_pause_stop_button), 
			      tlabel, NULL);

	gtk_widget_show_all (GTK_WIDGET (player->priv->play_pause_stop_button));

	rb_bonobo_set_label (player->priv->component, MENU_PATH_PLAY, mlabel);
	rb_bonobo_set_label (player->priv->component, TRAY_PATH_PLAY, mlabel);
	rb_bonobo_set_verb (player->priv->component, MENU_PATH_PLAY, verb);
	rb_bonobo_set_verb (player->priv->component, TRAY_PATH_PLAY, verb);

	player->priv->playbutton_state = state;
}

static void
rb_shell_player_sync_with_source (RBShellPlayer *player)
{
	const char *entry_title = NULL;
	const char *artist = NULL;	
	char *title;
	RhythmDBEntry *entry;
	char *duration;

	entry = rb_shell_player_get_playing_entry (player);
	rb_debug ("playing source: %p, active entry: %p", player->priv->source, entry);

	if (entry != NULL) {
		rhythmdb_read_lock (player->priv->db);

		entry_title = rhythmdb_entry_get_string (player->priv->db,
							 entry, RHYTHMDB_PROP_TITLE);
		artist = rhythmdb_entry_get_string (player->priv->db, entry,
						    RHYTHMDB_PROP_ARTIST);

		rhythmdb_read_unlock (player->priv->db);
	}

	if (player->priv->have_url)
		rb_player_set_urldata (player->priv->player_widget,
				       entry_title,
				       player->priv->url);
	else
		rb_player_set_urldata (player->priv->player_widget,
				       NULL, NULL);

	if (player->priv->song && entry_title)
		title = g_strdup_printf ("%s (%s)", player->priv->song,
					 entry_title);
	else if (entry_title && artist)
		title = g_strdup_printf ("%s - %s", artist, entry_title);
	else if (entry_title)
		title = g_strdup (entry_title);
	else
		title = NULL;

	duration = rb_player_get_elapsed_string (player->priv->player_widget);

	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[WINDOW_TITLE_CHANGED], 0,
		       title);
	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[DURATION_CHANGED], 0,
		       duration);
	g_free (duration);

	/* Sync the player */
	if (player->priv->song)
		rb_player_set_title (player->priv->player_widget, title);
	else
		rb_player_set_title (player->priv->player_widget, entry_title);
	g_free (title);
	rb_player_set_playing_entry (player->priv->player_widget, entry);
	rb_player_sync (player->priv->player_widget);
}

void
rb_shell_player_sync_buttons (RBShellPlayer *player)
{
	RBSource *source;
	gboolean not_empty = FALSE;
	gboolean have_previous = FALSE;
	gboolean have_next = FALSE;
	PlayButtonState pstate = PLAY_BUTTON_PLAY;
        gboolean not_small;

	source = rb_shell_player_get_playing_entry (player) == NULL ?
		 player->priv->selected_source : player->priv->source;

	rb_debug ("syncing with source %p", source);

	/* If we have a source and it's not empty, next and prev depend
	 * on the availability of the next/prev entry. However if we are 
	 * shuffling only next make sense and if we are repeating next
	 * is always ok (restart)
	 */
	if (source && rb_shell_player_have_first (player, source)) {
		RBEntryView *songs;
		songs = rb_source_get_entry_view (source);

		not_empty = TRUE;

		/* Should these be up to the play order? */
		have_previous = monkey_media_player_get_uri (player->priv->mmplayer) != NULL;
		player->priv->have_previous_entry = (rb_entry_view_get_previous_entry (songs) != NULL);

		have_next = rb_play_order_has_next (player->priv->play_order);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->play_pause_stop_button), not_empty);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PREVIOUS, have_previous);
	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->prev_button), have_previous);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_NEXT, have_next);
	gtk_widget_set_sensitive (GTK_WIDGET (player->priv->next_button), have_next);

        not_small = !eel_gconf_get_boolean (CONF_UI_SMALL_DISPLAY);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_CURRENT_SONG,
				 rb_shell_player_get_playing_entry (player) != NULL
				 && not_small );

	{
		RBEntryView *view = rb_source_get_entry_view (player->priv->selected_source);
		rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_SONG_INFO,
					 rb_entry_view_have_selection (view));
	}

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

void
rb_shell_player_set_playing_source (RBShellPlayer *player,
				    RBSource *source)
{
	rb_shell_player_set_playing_source_internal (player, source, TRUE);
}

static void
rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
					     RBSource *source,
					     gboolean sync_entry_view)

{
	if (player->priv->source == source && source != NULL)
		return;

	rb_debug ("setting playing source to %p", source);

	/* Stop the already playing source. */
	if (player->priv->source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->source);
		if (sync_entry_view) {
			rb_debug ("source is already playing, stopping it");
			rb_entry_view_set_playing_entry (songs, NULL);
			rb_entry_view_set_playing (songs, FALSE);
		}
		g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
						      G_CALLBACK (rb_shell_player_playing_entry_deleted_cb),
						      player);
	}
	
	player->priv->source = source;

	if (source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->source);
		g_signal_connect (G_OBJECT (songs),
				  "playing_entry_deleted",
				  G_CALLBACK (rb_shell_player_playing_entry_deleted_cb),
				  player);
	}

	g_object_notify (G_OBJECT (player), "playing-source");

	player->priv->song = NULL;
	player->priv->url = NULL;
	player->priv->have_url = FALSE;
	player->priv->have_artist_album = FALSE;

	if (source == NULL)
		rb_shell_player_stop (player);

	rb_shell_player_sync_with_source (player);
	if (player->priv->selected_source)
		rb_shell_player_sync_buttons (player);
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

long
rb_shell_player_get_playing_time (RBShellPlayer *player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), 0);
	
	return monkey_media_player_get_time (player->priv->mmplayer);
}

void
rb_shell_player_set_playing_time (RBShellPlayer *player, long time)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));
	
	if (monkey_media_player_seekable (player->priv->mmplayer))
		monkey_media_player_set_time (player->priv->mmplayer, time);
}

long
rb_shell_player_get_playing_song_duration (RBShellPlayer *player)
{
	RhythmDBEntry *current_entry;
	long ret;
	
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), -1);
	
	current_entry = rb_shell_player_get_playing_entry (player);

	if (current_entry == NULL) {
		rb_debug ("Did not get playing entry : return -1 as length");
		return -1;
	}
	
	rhythmdb_read_lock (player->priv->db);
	
	ret = rhythmdb_entry_get_long (player->priv->db, current_entry,
				       RHYTHMDB_PROP_DURATION);
	
	rhythmdb_read_unlock (player->priv->db);
	return ret;
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
}


static void
eos_cb (MonkeyMediaPlayer *mmplayer, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("eos!");

	GDK_THREADS_ENTER ();

	if (player->priv->source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->source);

		rb_debug ("updating play statistics");

		rb_entry_view_freeze (songs);
		rb_source_update_play_statistics (player->priv->source,
						  player->priv->db,
						  rb_shell_player_get_playing_entry (player));
		rb_entry_view_thaw (songs);

		switch (rb_source_handle_eos (player->priv->source))
		{
		case RB_SOURCE_EOF_ERROR:
			rb_error_dialog (_("Unexpected end of stream!"));
			rb_shell_player_set_playing_source (player, NULL);
			break;
		case RB_SOURCE_EOF_NEXT:
			rb_shell_player_do_next (player);
			break;
		}
	}

	GDK_THREADS_LEAVE ();
}

static void
rb_shell_player_error (RBShellPlayer *player, GError *err, gboolean lock)
{
	if (player->priv->handling_error) {
		rb_debug ("ignoring error: %s", err->message);
		return;
	}

	if (lock != FALSE)
		GDK_THREADS_ENTER ();

	cancel_buffering_dialog (player);
	rb_debug ("error: %s", err->message);
	player->priv->handling_error = TRUE;
	rb_shell_player_set_playing_source (player, NULL);
 	rb_error_dialog ("%s", err->message);
	player->priv->handling_error = FALSE;
	rb_debug ("exiting error hander");

	if (lock != FALSE)
		GDK_THREADS_LEAVE ();
}

static void
error_cb (MonkeyMediaPlayer *mmplayer, GError *err, gpointer data)
{
	rb_shell_player_error (RB_SHELL_PLAYER (data), err, TRUE);
}

static void
tick_cb (MonkeyMediaPlayer *mmplayer, long elapsed, gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);

	GDK_THREADS_ENTER ();

	rb_player_sync_time (player->priv->player_widget);

	if (monkey_media_player_playing (mmplayer)) {
		static int callback_runs = 0;
		callback_runs++;
		if (callback_runs >= MONKEY_MEDIA_PLAYER_TICK_HZ) {
			gchar *duration;

			duration = rb_player_get_elapsed_string (player->priv->player_widget);
			g_signal_emit (G_OBJECT (player), rb_shell_player_signals[DURATION_CHANGED],
				       0, duration);
			g_free (duration);
			callback_runs = 0;
		}
	}


	GDK_THREADS_LEAVE ();
}

static void
info_available_cb (MonkeyMediaPlayer *mmplayer,
		   MonkeyMediaStreamInfoField field,
		   GValue *value,
		   gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	RBEntryView *songs;
	RhythmDBEntry *entry;
	gboolean changed = FALSE;
	char *valcontents;
	GEnumValue *enumvalue = g_enum_get_value (g_type_class_peek (MONKEY_MEDIA_TYPE_STREAM_INFO_FIELD),
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

	songs = rb_source_get_entry_view (player->priv->source);
	entry = rb_entry_view_get_playing_entry (songs);

	if (entry == NULL) {
		rb_debug ("Got info_available but no playing entry!");
		goto out_unlock;
	}

	switch (field)	{
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
		rhythmdb_entry_queue_set (player->priv->db, entry, RHYTHMDB_PROP_QUALITY, &newval);
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

static void
buffering_begin_cb (MonkeyMediaPlayer *mmplayer,
		    gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("got buffering_begin_cb");

	if (!monkey_media_player_playing (mmplayer)) {
		rb_debug ("not playing, ignoring");
		return;
	}

	gdk_threads_enter ();

	if (!player->priv->buffering_dialog) {
		GladeXML *xml = rb_glade_xml_new ("buffering-dialog.glade", "dialog", player);
 		GtkWidget *label = glade_xml_get_widget (xml, "status_label");
		PangoAttrList *pattrlist = pango_attr_list_new ();
		PangoAttribute *attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);

		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
		gtk_label_set_attributes (GTK_LABEL (label), pattrlist);
		pango_attr_list_unref (pattrlist);
		
		player->priv->buffering_progress = glade_xml_get_widget (xml, "progressbar");

		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (player->priv->buffering_progress));

		player->priv->buffering_dialog = glade_xml_get_widget (xml, "dialog");
		g_signal_connect (G_OBJECT (glade_xml_get_widget (xml, "cancel_button")),
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

static void
buffering_end_cb (MonkeyMediaPlayer *mmplayer,
		  gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	rb_debug ("got buffering_end_cb");

	gdk_threads_enter ();

	cancel_buffering_dialog (player);
	if (player->priv->source)
		rb_source_buffering_done (player->priv->source);

	gdk_threads_leave ();
}

const char *
rb_shell_player_get_playing_path (RBShellPlayer *shell_player)
{
	return shell_player->priv->url;
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
	RBSource *source; 
	xev = (XEvent *) xevent;
	if (xev->type != KeyPress) {
		return GDK_FILTER_CONTINUE;
	}

	key = (XKeyEvent *) xevent;

	player = (RBShellPlayer *)data;

	source = rb_shell_player_get_playing_entry (player) == NULL ?
			player->priv->selected_source : player->priv->source;

	if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPlay) == key->keycode) {	
		rb_shell_player_playpause (player);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioStop) == key->keycode) {
		rb_shell_player_set_playing_source (player, NULL);
		return GDK_FILTER_REMOVE;		
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPrev) == key->keycode) {
		rb_shell_player_do_previous (player);
		return GDK_FILTER_REMOVE;		
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioNext) == key->keycode) {
		rb_shell_player_do_next (player);
		return GDK_FILTER_REMOVE;
	} else {
		return GDK_FILTER_CONTINUE;
	}
}

static void
rb_shell_player_init_mmkeys (RBShellPlayer *shell_player)
{
	gint keycodes[] = {0, 0, 0, 0};
	GdkDisplay *display;
	GdkScreen *screen;
	GdkWindow *root;
	guint i, j;

	keycodes[0] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPlay);
	keycodes[1] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioStop);
	keycodes[2] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPrev);
	keycodes[3] = XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioNext);

	display = gdk_display_get_default ();

	for (i = 0; i < gdk_display_get_n_screens (display); i++) {
		screen = gdk_display_get_screen (display, i);

		if (screen != NULL) {
			root = gdk_screen_get_root_window (screen);

			for (j = 0; j < 4 ; j++) {
				if (keycodes[j] != 0)
					grab_mmkey (keycodes[j], root);
			}

			gdk_window_add_filter (root, filter_mmkeys,
					       (gpointer) shell_player);
		}
	}
}
#endif /* HAVE_MMKEYS */
