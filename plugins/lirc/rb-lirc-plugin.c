/*
 * rb-lirc-plugin.c
 *
 * Copyright (C) 2006  Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <lirc/lirc_client.h>

#include "rb-plugin.h"
#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-shell-player.h"

#define RB_TYPE_LIRC_PLUGIN		(rb_lirc_plugin_get_type ())
#define RB_LIRC_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIRC_PLUGIN, RBLircPlugin))
#define RB_LIRC_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIRC_PLUGIN, RBLircPluginClass))
#define RB_IS_LIRC_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIRC_PLUGIN))
#define RB_IS_LIRC_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIRC_PLUGIN))
#define RB_LIRC_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIRC_PLUGIN, RBLircPluginClass))

#define RB_IR_COMMAND_PLAY "play"
#define RB_IR_COMMAND_PAUSE "pause"
#define RB_IR_COMMAND_PLAYPAUSE "playpause"
#define RB_IR_COMMAND_STOP "stop"
#define RB_IR_COMMAND_SHUFFLE "shuffle"
#define RB_IR_COMMAND_REPEAT "repeat"
#define RB_IR_COMMAND_NEXT "next"
#define RB_IR_COMMAND_PREVIOUS "previous"
#define RB_IR_COMMAND_SEEK_FORWARD "seek_forward"
#define RB_IR_COMMAND_SEEK_BACKWARD "seek_backward"
#define RB_IR_COMMAND_VOLUME_UP "volume_up"
#define RB_IR_COMMAND_VOLUME_DOWN "volume_down"
#define RB_IR_COMMAND_MUTE "mute"

typedef struct
{
	RBPlugin parent;
	RBShellPlayer *shell_player;
	struct lirc_config *lirc_config;
	GIOChannel *lirc_channel;
} RBLircPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBLircPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_lirc_plugin_get_type		(void) G_GNUC_CONST;

static void rb_lirc_plugin_init (RBLircPlugin *plugin);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER(RBLircPlugin, rb_lirc_plugin)

static void
rb_lirc_plugin_class_init (RBLircPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);
	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

static void
rb_lirc_plugin_init (RBLircPlugin *plugin)
{
	rb_debug ("RBLircPlugin initialising");
}

static gboolean
rb_lirc_plugin_read_code (GIOChannel *source,
			  GIOCondition condition,
			  RBLircPlugin *plugin)
{
	char *code;
	char *str = NULL;	/* owned by lirc config, must not be freed */
	int ok;

	lirc_nextcode (&code);
	if (code == NULL) {
		rb_debug ("Got incomplete lirc code");
		return TRUE;
	}

	ok = lirc_code2char (plugin->lirc_config, code, &str);

	if (ok != 0) {
		rb_debug ("couldn't convert lirc code \"%s\" to string", code);
	} else if (str == NULL) {
		rb_debug ("unknown LIRC code");
	} else if (strcmp (str, RB_IR_COMMAND_PLAY) == 0) {
		rb_shell_player_play (plugin->shell_player, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_PAUSE) == 0) {
		rb_shell_player_pause (plugin->shell_player, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_PLAYPAUSE) == 0) {
		rb_shell_player_playpause (plugin->shell_player, FALSE, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_STOP) == 0) {
		rb_shell_player_stop (plugin->shell_player);
	} else if (strcmp (str, RB_IR_COMMAND_SHUFFLE) == 0) {
		gboolean shuffle;
		gboolean repeat;
		if (rb_shell_player_get_playback_state (plugin->shell_player, &shuffle, &repeat)) {
			rb_shell_player_set_playback_state (plugin->shell_player, !shuffle, repeat);
		}
	} else if (strcmp (str, RB_IR_COMMAND_REPEAT) == 0) {
		gboolean shuffle;
		gboolean repeat;
		if (rb_shell_player_get_playback_state (plugin->shell_player, &shuffle, &repeat)) {
			rb_shell_player_set_playback_state (plugin->shell_player, shuffle, !repeat);
		}
	} else if (strcmp (str, RB_IR_COMMAND_NEXT) == 0) {
		rb_shell_player_do_next (plugin->shell_player, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_PREVIOUS) == 0) {
		rb_shell_player_do_previous (plugin->shell_player, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_SEEK_FORWARD) == 0) {
		rb_shell_player_seek (plugin->shell_player, 10);
	} else if (strcmp (str, RB_IR_COMMAND_SEEK_BACKWARD) == 0) {
		rb_shell_player_seek (plugin->shell_player, -10);
	} else if (strcmp (str, RB_IR_COMMAND_VOLUME_UP) == 0) {
		rb_shell_player_set_volume_relative (plugin->shell_player, 0.1, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_VOLUME_DOWN) == 0) {
		rb_shell_player_set_volume_relative (plugin->shell_player, -0.1, NULL);
	} else if (strcmp (str, RB_IR_COMMAND_MUTE) == 0) {
		gboolean mute;
		if (rb_shell_player_get_mute (plugin->shell_player, &mute, NULL)) {
			rb_shell_player_set_mute (plugin->shell_player, !mute, NULL);
		}
	}
	g_free (code);

	return TRUE;
}

static void
impl_activate (RBPlugin *rbplugin,
	       RBShell *shell)
{
	int fd;
	RBLircPlugin *plugin = RB_LIRC_PLUGIN (rbplugin);

	g_object_get (G_OBJECT (shell), "shell-player", &plugin->shell_player, NULL);

	rb_debug ("Activating lirc plugin");

	if (lirc_readconfig (NULL, &plugin->lirc_config, NULL) == -1) {
		rb_debug ("Couldn't read lirc configuration");
		return;
	}

	fd = lirc_init ("Rhythmbox", 1);
	if (fd < 0) {
		rb_debug ("Couldn't initialize lirc");
		return;
	}

	plugin->lirc_channel = g_io_channel_unix_new (fd);
	g_io_add_watch (plugin->lirc_channel, G_IO_IN,
			(GIOFunc) rb_lirc_plugin_read_code, plugin);
}

static void
impl_deactivate	(RBPlugin *rbplugin,
		 RBShell *shell)
{
	RBLircPlugin *plugin = RB_LIRC_PLUGIN (rbplugin);
	GError *error = NULL;

	rb_debug ("Deactivating lirc plugin");

	if (plugin->lirc_channel) {
		g_io_channel_shutdown (plugin->lirc_channel, FALSE, &error);
		if (error != NULL) {
			g_warning ("Couldn't destroy lirc connection: %s",
				   error->message);
			g_error_free (error);
		}
		plugin->lirc_channel = NULL;
	}

	if (plugin->lirc_config) {
		lirc_freeconfig (plugin->lirc_config);
		plugin->lirc_config = NULL;

		lirc_deinit ();
	}

	if (plugin->shell_player) {
		g_object_unref (G_OBJECT (plugin->shell_player));
		plugin->shell_player = NULL;
	}
}

