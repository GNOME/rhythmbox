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

#include <gtk/gtkvbox.h>
#include <gtk/gtkalignment.h>
#include <bonobo/bonobo-ui-util.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>

#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "rb-bonobo-helpers.h"
#include "rb-dialog.h"
#include "rb-player.h"
#include "rb-remote.h"
#include "eel-gconf-extensions.h"

#define RB_SHELL_PLAYER_REMOTE_SEEK_INTERVAL 10

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
static void rb_shell_player_set_play_button (RBShellPlayer *player,
			                     PlayButtonState state);
static void rb_shell_player_sync_with_player (RBShellPlayer *player);
static void rb_view_player_changed_cb (RBViewPlayer *player,
			               RBShellPlayer *shell_player);
static void rb_shell_player_sync_mixer (RBShellPlayer *player);
static void rb_shell_player_set_playing_player (RBShellPlayer *shell_player,
				                RBViewPlayer *player);
static void rb_shell_player_update_play_button (RBShellPlayer *player);
static void rb_shell_player_player_start_playing_cb (RBViewPlayer *player,
				                     RBShellPlayer *shell_player);
static void rb_shell_player_sync_with_selected_player (RBShellPlayer *player);
static void rb_shell_player_player_changed_cb (RBViewPlayer *view_player,
				               RBShellPlayer *player);

#ifdef HAVE_REMOTE
static void rb_shell_player_remote_cb (RBRemote *remote, RBRemoteCommand cmd,
				       RBShellPlayer *player);
#endif

#define MENU_PATH_PLAY     "/menu/Controls/Play"
#define TRAY_PATH_PLAY     "/popups/TrayPopup/Play"
#define TOOLBAR_PATH_PLAY  "/Toolbar/Play"

#define CMD_PATH_PLAY     "/commands/Play"
#define CMD_PATH_PREVIOUS "/commands/Previous"
#define CMD_PATH_NEXT     "/commands/Next"

#define CONF_STATE_VOLUME "/apps/rhythmbox/state/volume"

struct RBShellPlayerPrivate
{
	RBViewPlayer *selected_player;
	RBViewPlayer *player;

	BonoboUIComponent *component;
	BonoboUIComponent *tray_component;

	MonkeyMediaMixer *mixer;

	MonkeyMediaAudioStream *current_stream;

	RBPlayer *player_widget;

	RBRemote *remote;

	GtkWidget *bin;
};

enum
{
	PROP_0,
	PROP_PLAYER,
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
	BONOBO_UI_VERB_END
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
					 PROP_PLAYER,
					 g_param_spec_object ("player",
							      "RBViewPlayer",
							      "RBViewPlayer object",
							      RB_TYPE_VIEW_PLAYER,
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
rb_shell_player_init (RBShellPlayer *shell_player)
{
	GError *error = NULL;
	GtkWidget *align, *vbox;

	shell_player->priv = g_new0 (RBShellPlayerPrivate, 1);

	shell_player->priv->mixer = monkey_media_mixer_new (&error);
	if (error != NULL)
	{
		rb_error_dialog (_("Failed to create the mixer, error was:\n%s"), error->message);
		g_error_free (error);
	}

	monkey_media_mixer_set_volume (shell_player->priv->mixer,
				       eel_gconf_get_float (CONF_STATE_VOLUME));

	gtk_box_set_spacing (GTK_BOX (shell_player), 5);

	shell_player->priv->player_widget = rb_player_new ();
	gtk_box_pack_start (GTK_BOX (shell_player),
			    GTK_WIDGET (shell_player->priv->player_widget), TRUE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_box_pack_end (GTK_BOX (shell_player), vbox, FALSE, TRUE, 0);

	shell_player->priv->bin = gtk_hbox_new (FALSE, 0);
	align = gtk_alignment_new (1.0, 1.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), shell_player->priv->bin);
	gtk_box_pack_end (GTK_BOX (vbox), align, FALSE, FALSE, 0);

#ifdef HAVE_REMOTE
	shell_player->priv->remote = rb_remote_new ();
	g_signal_connect (shell_player->priv->remote, "button_pressed",
			  G_CALLBACK (rb_shell_player_remote_cb),
			  shell_player);
#else
	shell_player->priv->remote = NULL;
#endif
}

static void
rb_shell_player_finalize (GObject *object)
{
	RBShellPlayer *shell_player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PLAYER (object));

	shell_player = RB_SHELL_PLAYER (object);

	g_return_if_fail (shell_player->priv != NULL);

	eel_gconf_set_float (CONF_STATE_VOLUME,
			     monkey_media_mixer_get_volume (shell_player->priv->mixer));

	/* FIXME hangs somewhere in gstreamer .. */
	/*g_object_unref (G_OBJECT (shell_player->priv->mixer));*/

	if (shell_player->priv->remote != NULL)
		g_object_unref (G_OBJECT (shell_player->priv->remote));
	
	g_free (shell_player->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_shell_player_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBShellPlayer *shell_player = RB_SHELL_PLAYER (object);
	GtkWidget *extra;

	switch (prop_id)
	{
	case PROP_PLAYER:
		if (shell_player->priv->selected_player != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (shell_player->priv->selected_player),
							      G_CALLBACK (rb_shell_player_player_start_playing_cb),
							      shell_player);
			g_signal_handlers_disconnect_by_func (G_OBJECT (shell_player->priv->selected_player),
							      G_CALLBACK (rb_shell_player_player_changed_cb),
							      shell_player);
			
			extra = rb_view_get_extra_widget (RB_VIEW (shell_player->priv->selected_player));
			if (extra != NULL)
			{
				gtk_widget_hide (extra);
				gtk_container_remove (GTK_CONTAINER (shell_player->priv->bin), extra);
			}
		}
		
		shell_player->priv->selected_player = g_value_get_object (value);

		if (shell_player->priv->selected_player != NULL)
		{
			g_signal_connect (G_OBJECT (shell_player->priv->selected_player),
				          "start_playing",
					  G_CALLBACK (rb_shell_player_player_start_playing_cb),
				          shell_player);
			g_signal_connect (G_OBJECT (shell_player->priv->selected_player),
				          "changed",
					  G_CALLBACK (rb_shell_player_player_changed_cb),
				          shell_player);
		}

		rb_shell_player_update_play_button (shell_player);
		rb_shell_player_sync_with_selected_player (shell_player);

		extra = rb_view_get_extra_widget (RB_VIEW (shell_player->priv->selected_player));
		if (extra != NULL)
		{
			gtk_container_add (GTK_CONTAINER (shell_player->priv->bin), extra);
			gtk_widget_show_all (extra);
		}
		
		break;
	case PROP_COMPONENT:
		shell_player->priv->component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (shell_player->priv->component,
							     rb_shell_player_verbs,
							     shell_player);
		rb_shell_player_set_playing_player (shell_player, NULL);
		break;
	case PROP_TRAY_COMPONENT:
		shell_player->priv->tray_component = g_value_get_object (value);
		bonobo_ui_component_add_verb_list_with_data (shell_player->priv->tray_component,
							     rb_shell_player_verbs,
							     shell_player);
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
	RBShellPlayer *shell_player = RB_SHELL_PLAYER (object);

	switch (prop_id)
	{
	case PROP_PLAYER:
		g_value_set_object (value, shell_player->priv->selected_player);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, shell_player->priv->component);
		break;
	case PROP_TRAY_COMPONENT:
		g_value_set_object (value, shell_player->priv->tray_component);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_shell_player_set_player (RBShellPlayer *shell_player,
			    RBViewPlayer *player)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (shell_player));
	g_return_if_fail (RB_IS_VIEW_PLAYER (player));

	g_object_set (G_OBJECT (shell_player),
		      "player", player,
		      NULL);
}

RBViewPlayer *
rb_shell_player_get_player (RBShellPlayer *shell_player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (shell_player), NULL);

	return shell_player->priv->selected_player;
}

RBShellPlayer *
rb_shell_player_new (BonoboUIComponent *component,
		     BonoboUIComponent *tray_component)
{
	RBShellPlayer *shell_player;

	shell_player = g_object_new (RB_TYPE_SHELL_PLAYER,
				     "component", component,
				     "tray-component", tray_component,
				     NULL);

	g_return_val_if_fail (shell_player->priv != NULL, NULL);

	return shell_player;
}

static void
rb_shell_player_cmd_previous (BonoboUIComponent *component,
			      RBShellPlayer *player,
			      const char *verbname)
{
	if (monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (player->priv->current_stream)) < 3 &&
	    rb_view_player_have_previous (player->priv->player) == TRUE)
	{
		/* we're in the first 2 seconds of the song, go to previous */
		rb_view_player_previous (player->priv->player);
	}
	else
	{
		/* we're further in the song, restart it */
		monkey_media_stream_set_elapsed_time (MONKEY_MEDIA_STREAM (player->priv->current_stream), 0);
	}

	rb_view_player_jump_to_current (player->priv->player);
}

static void
rb_shell_player_cmd_play (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	if (player->priv->player == NULL)
	{
		/* no current stream, pull one in from the currently
		 * selected player */
		rb_shell_player_set_playing_player (player, player->priv->selected_player);

		return;
	}

	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PLAYING);

	rb_view_player_notify_changed (player->priv->player);

	rb_shell_player_update_play_button (player);
}

static void
rb_shell_player_cmd_pause (BonoboUIComponent *component,
			   RBShellPlayer *player,
			   const char *verbname)
{
	if (player->priv->player == NULL)
		return;

	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PAUSED);

	rb_view_player_notify_changed (player->priv->player);

	rb_shell_player_update_play_button (player);
}

static void
rb_shell_player_cmd_stop (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	rb_shell_player_set_playing_player (player, NULL);
}

static void
rb_shell_player_cmd_next (BonoboUIComponent *component,
			  RBShellPlayer *player,
			  const char *verbname)
{
	if (player->priv->player != NULL)
	{
		rb_view_player_next (player->priv->player);

		rb_view_player_jump_to_current (player->priv->player);
	}
}

static void
rb_shell_player_set_play_button (RBShellPlayer *player,
			         PlayButtonState state)
{
	const char *tlabel = NULL, *mlabel = NULL, *verb = NULL;

	switch (state)
	{
	case PLAY_BUTTON_PAUSE:
		tlabel = _("Pause");
		mlabel = _("_Pause");
		verb = "Pause";
		break;
	case PLAY_BUTTON_PLAY:
		tlabel = _("Play");
		mlabel = _("_Play");
		verb = "Play";
		break;
	case PLAY_BUTTON_STOP:
		tlabel = _("Stop");
		mlabel = _("_Stop");
		verb = "Stop";
		break;
	default:
		g_error ("Should not get here!");
		break;
	}

	rb_bonobo_set_label (player->priv->component, TOOLBAR_PATH_PLAY, tlabel);
	rb_bonobo_set_label (player->priv->component, MENU_PATH_PLAY, mlabel);
	rb_bonobo_set_label (player->priv->component, TRAY_PATH_PLAY, mlabel);
	rb_bonobo_set_verb (player->priv->component, TOOLBAR_PATH_PLAY, verb);
	rb_bonobo_set_verb (player->priv->component, MENU_PATH_PLAY, verb);
	rb_bonobo_set_verb (player->priv->component, TRAY_PATH_PLAY, verb);
}

static void
rb_shell_player_sync_with_player (RBShellPlayer *player)
{
	gboolean have_next, have_previous;
	const char *title;

	if (player->priv->player != NULL)
		title = rb_view_player_get_title (player->priv->player);
	else
		title = NULL;
	
	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[WINDOW_TITLE_CHANGED], 0,
		       title);

	if (player->priv->player != NULL)
	{
		have_previous = TRUE;
		have_next = (rb_view_player_have_next (player->priv->player) == RB_VIEW_PLAYER_TRUE);
	}
	else
		have_previous = have_next = FALSE;

	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PREVIOUS, have_previous);
	rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_NEXT, have_next);

	rb_shell_player_sync_mixer (player);
}

static void
rb_view_player_changed_cb (RBViewPlayer *player,
			   RBShellPlayer *shell_player)
{
	if (rb_view_player_get_stream (player) == NULL)
		rb_shell_player_set_playing_player (shell_player, NULL);
	else
		rb_shell_player_sync_with_player (shell_player);
}

static void
rb_shell_player_sync_mixer (RBShellPlayer *player)
{
	MonkeyMediaAudioStream *stream = NULL;

	if (player->priv->player != NULL)
		stream = rb_view_player_get_stream (player->priv->player);

	if (stream == player->priv->current_stream)
		return;

	if (stream != NULL)
	{
		player->priv->current_stream = stream;

		monkey_media_mixer_append_audio_stream (player->priv->mixer,
							stream);
		monkey_media_mixer_set_playing_audio_stream (player->priv->mixer,
							     stream);

		g_object_unref (G_OBJECT (stream)); /* mixer holds a ref */

		monkey_media_mixer_set_state (player->priv->mixer,
					      MONKEY_MEDIA_MIXER_STATE_PLAYING);

		rb_shell_player_update_play_button (player);
	}
	else
	{
		player->priv->current_stream = NULL;

		monkey_media_mixer_set_state (player->priv->mixer,
					      MONKEY_MEDIA_MIXER_STATE_STOPPED);

		rb_shell_player_update_play_button (player);
	}
}

static void
rb_shell_player_update_play_button (RBShellPlayer *player)
{
	MonkeyMediaMixerState state = monkey_media_mixer_get_state (player->priv->mixer);
	PlayButtonState pstate = PLAY_BUTTON_PLAY;

	switch (state)
	{
	case MONKEY_MEDIA_MIXER_STATE_STOPPED:
		pstate = PLAY_BUTTON_PLAY;
		break;
	case MONKEY_MEDIA_MIXER_STATE_PLAYING:
		if (player->priv->player == player->priv->selected_player)
			pstate = PLAY_BUTTON_PAUSE;
		else
			pstate = PLAY_BUTTON_STOP;
		break;
	case MONKEY_MEDIA_MIXER_STATE_PAUSED:
		if (player->priv->player == player->priv->selected_player)
			pstate = PLAY_BUTTON_PLAY;
		else
			pstate = PLAY_BUTTON_STOP;
		break;
	default:
		g_error ("Should not get here!");
		break;
	}
	
	rb_shell_player_set_play_button (player, pstate);
}

static void
rb_shell_player_set_playing_player (RBShellPlayer *shell_player,
				    RBViewPlayer *player)
{
	if (shell_player->priv->player == player && player != NULL)
		return;

	if (shell_player->priv->player != NULL)
	{
		g_signal_handlers_disconnect_by_func (G_OBJECT (shell_player->priv->player),
						      G_CALLBACK (rb_view_player_changed_cb),
						      G_OBJECT (shell_player));
		rb_view_player_stop_playing (shell_player->priv->player);
	}

	shell_player->priv->player = player;
	rb_player_set_view (shell_player->priv->player_widget, player);

	if (shell_player->priv->player != NULL)
	{
		g_signal_connect_object (G_OBJECT (shell_player->priv->player),
					 "changed",
					 G_CALLBACK (rb_view_player_changed_cb),
					 G_OBJECT (shell_player),
					 0);
		if (rb_view_player_get_stream (shell_player->priv->player) == NULL)
			rb_view_player_start_playing (shell_player->priv->player);
	}

	rb_shell_player_sync_with_player (shell_player);

	rb_shell_player_update_play_button (shell_player);
}

static void
rb_shell_player_player_start_playing_cb (RBViewPlayer *player,
				         RBShellPlayer *shell_player)
{
	rb_shell_player_set_playing_player (shell_player, player);
}

void
rb_shell_player_stop (RBShellPlayer *shell_player)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (shell_player));

	monkey_media_mixer_set_state (shell_player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_STOPPED);
}

MonkeyMediaMixerState
rb_shell_player_get_state (RBShellPlayer *shell_player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (shell_player), -1);

	return monkey_media_mixer_get_state (shell_player->priv->mixer);
}

MonkeyMediaMixer *
rb_shell_player_get_mixer (RBShellPlayer *shell_player)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (shell_player), NULL);
	
	return shell_player->priv->mixer;
}

static void
rb_shell_player_player_changed_cb (RBViewPlayer *view_player,
				   RBShellPlayer *player)
{
	rb_shell_player_sync_with_selected_player (player);
}

static void
rb_shell_player_sync_with_selected_player (RBShellPlayer *player)
{
	if (player->priv->player == NULL)
	{
		gboolean sensitive;
		
		if (player->priv->selected_player == NULL)
			sensitive = FALSE;
		else
		{
			if (rb_view_player_have_first (player->priv->selected_player) == TRUE)
				sensitive = TRUE;
			else
				sensitive = FALSE;
		}

		rb_bonobo_set_sensitive (player->priv->component, CMD_PATH_PLAY, sensitive);
	}
}

#ifdef HAVE_REMOTE
static void rb_shell_player_remote_cb (RBRemote *remote, RBRemoteCommand cmd,
				       RBShellPlayer *player)
{
	MonkeyMediaAudioStream *stream = NULL;
	long elapsed;

	if (player->priv->player != NULL)
		stream = rb_view_player_get_stream (player->priv->player);

	switch (cmd) {
		case RB_REMOTE_COMMAND_PLAY:
			rb_shell_player_cmd_play (NULL, player, NULL);
			break;
		case RB_REMOTE_COMMAND_PAUSE:
			rb_shell_player_cmd_pause (NULL, player, NULL);
			break;
		case RB_REMOTE_COMMAND_NEXT:
			rb_shell_player_cmd_next (NULL, player, NULL);
			break;
		case RB_REMOTE_COMMAND_PREVIOUS:
			rb_shell_player_cmd_previous (NULL, player, NULL);
			break;
		case RB_REMOTE_COMMAND_SEEK_FORWARD:
			if (stream == NULL)
				return;

			elapsed = monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (stream));

			elapsed += RB_SHELL_PLAYER_REMOTE_SEEK_INTERVAL;

			monkey_media_stream_set_elapsed_time (MONKEY_MEDIA_STREAM (stream), elapsed);
			break;
		case RB_REMOTE_COMMAND_SEEK_BACKWARD:
			if (stream == NULL)
				return;

			elapsed = monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (stream));

			elapsed -= RB_SHELL_PLAYER_REMOTE_SEEK_INTERVAL;

			monkey_media_stream_set_elapsed_time (MONKEY_MEDIA_STREAM (stream), elapsed);
			break;
		default:
			break;
	}
}
#endif
