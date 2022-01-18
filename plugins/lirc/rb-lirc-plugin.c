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
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
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

#include <config.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <lirc/lirc_client.h>

#include "rb-plugin-macros.h"
#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-shell-player.h"
#include "rb-file-helpers.h"

#define RB_TYPE_LIRC_PLUGIN		(rb_lirc_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBLircPlugin, rb_lirc_plugin, RB, LIRC_PLUGIN, PeasExtensionBase)

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
#define RB_IR_COMMAND_QUIT "quit"

struct _RBLircPlugin
{
	PeasExtensionBase parent;
	RBShellPlayer *shell_player;
	struct lirc_config *lirc_config;
	GIOChannel *lirc_channel;
};

struct _RBLircPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_lirc_plugin_init (RBLircPlugin *plugin);

RB_DEFINE_PLUGIN (RB_TYPE_LIRC_PLUGIN, RBLircPlugin, rb_lirc_plugin,)

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
	gboolean processed = FALSE;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		/* TODO: retry after a minute? */
		rb_debug ("LIRC connection broken.  sorry.");
		return FALSE;
	}

	lirc_nextcode (&code);
	if (code == NULL) {
		rb_debug ("Got incomplete lirc code");
		return TRUE;
	}

	do {
		ok = lirc_code2char (plugin->lirc_config, code, &str);

		if (ok != 0) {
			rb_debug ("couldn't convert lirc code \"%s\" to string", code);
		} else if (str == NULL) {
			if (processed == FALSE)
				rb_debug ("unknown LIRC code \"%s\"", code);
			break;
		} else if (strcmp (str, RB_IR_COMMAND_PLAY) == 0) {
			gboolean playing;
			rb_shell_player_get_playing (plugin->shell_player, &playing, NULL);
			if (playing == FALSE)
				rb_shell_player_playpause (plugin->shell_player, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_PAUSE) == 0) {
			rb_shell_player_pause (plugin->shell_player, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_PLAYPAUSE) == 0) {
			rb_shell_player_playpause (plugin->shell_player, NULL);
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
			rb_shell_player_seek (plugin->shell_player, FFWD_OFFSET, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_SEEK_BACKWARD) == 0) {
			rb_shell_player_seek (plugin->shell_player, -RWD_OFFSET, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_VOLUME_UP) == 0) {
			rb_shell_player_set_volume_relative (plugin->shell_player, 0.1, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_VOLUME_DOWN) == 0) {
			rb_shell_player_set_volume_relative (plugin->shell_player, -0.1, NULL);
		} else if (strcmp (str, RB_IR_COMMAND_MUTE) == 0) {
			gboolean mute;
			if (rb_shell_player_get_mute (plugin->shell_player, &mute, NULL)) {
				rb_shell_player_set_mute (plugin->shell_player, !mute, NULL);
			}
		} else if (strcmp (str,RB_IR_COMMAND_QUIT) == 0) {
			RBShell *shell;

			g_object_get (plugin, "object", &shell, NULL);
			rb_shell_quit (shell, NULL);
			g_object_unref (shell);
			/* the plugin will have been deactivated, so we can't continue the loop */
			break;
		}
		processed = TRUE;
	} while (ok == 0);
	g_free (code);

	return TRUE;
}

static void
impl_activate (PeasActivatable *bplugin)
{
	int fd;
	char *path;
	RBLircPlugin *plugin = RB_LIRC_PLUGIN (bplugin);
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);

	g_object_get (shell, "shell-player", &plugin->shell_player, NULL);

	rb_debug ("Activating lirc plugin");

	fd = lirc_init ("Rhythmbox", 1);
	if (fd < 0) {
		rb_debug ("Couldn't initialize lirc");
		g_object_unref (shell);
		return;
	}

	/* Load the default Rhythmbox setup */
	path = rb_find_plugin_data_file (G_OBJECT (plugin), "rhythmbox_lirc_default");
	if (path == NULL || lirc_readconfig (path, &plugin->lirc_config, NULL) == -1) {
		g_free (path);
		close (fd);
		rb_debug ("Couldn't read lirc configuration");
		g_object_unref (shell);
		return;
	}
	g_free (path);

	lirc_readconfig (NULL, &plugin->lirc_config, NULL);

	plugin->lirc_channel = g_io_channel_unix_new (fd);
	g_io_channel_set_encoding (plugin->lirc_channel, NULL, NULL);
	g_io_channel_set_buffered (plugin->lirc_channel, FALSE);
	g_io_add_watch (plugin->lirc_channel, G_IO_IN | G_IO_ERR | G_IO_HUP,
			(GIOFunc) rb_lirc_plugin_read_code, plugin);

	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBLircPlugin *plugin = RB_LIRC_PLUGIN (bplugin);
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

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_lirc_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_LIRC_PLUGIN);
}
