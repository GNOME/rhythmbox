/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/*
 * gnome-session integration.
 * currently consists of inhibiting suspend while playing.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gio/gio.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"

#define RB_TYPE_GPM_PLUGIN		(rb_gpm_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBGPMPlugin, rb_gpm_plugin, RB, GPM_PLUGIN, PeasExtensionBase)

struct _RBGPMPlugin
{
	PeasExtensionBase parent;

	guint cookie;
	gint handler_id;
	gint timeout_id;
};

struct _RBGPMPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

RB_DEFINE_PLUGIN(RB_TYPE_GPM_PLUGIN, RBGPMPlugin, rb_gpm_plugin,)

static void
rb_gpm_plugin_init (RBGPMPlugin *plugin)
{
	rb_debug ("RBGPMPlugin initialising");
}

static gboolean
inhibit (RBGPMPlugin *plugin)
{
	RBShell *shell;
	GtkApplication *app;
	GtkWindow *window;

	plugin->timeout_id = 0;
	if (plugin->cookie != 0) {
		rb_debug ("Was going to inhibit session manager, but we already have done");
		return FALSE;
	}

	rb_debug ("inhibiting");

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "application", &app,
		      "window", &window,
		      NULL);
	g_object_unref (shell);

	plugin->cookie = gtk_application_inhibit (app, window, GTK_APPLICATION_INHIBIT_SUSPEND, _("Playing"));

	g_object_unref (window);
	g_object_unref (app);
	return FALSE;
}

static gboolean
uninhibit (RBGPMPlugin *plugin)
{
	GtkApplication *app;
	RBShell *shell;

	plugin->timeout_id = 0;

	if (plugin->cookie == 0) {
		rb_debug ("Was going to uninhibit session manager, but we haven't inhibited it");
		return FALSE;
	}

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "application", &app, NULL);
	g_object_unref (shell);

	rb_debug ("uninhibiting; cookie = %u", plugin->cookie);
	gtk_application_uninhibit (app, plugin->cookie);
	plugin->cookie = 0;
	g_object_unref (app);
	return FALSE;
}

static void
playing_changed_cb (GObject *player, gboolean playing, RBGPMPlugin *plugin)
{
	if (plugin->timeout_id != 0) {
		g_source_remove (plugin->timeout_id);
		plugin->timeout_id = 0;
	}

	/* small delay to avoid uninhibit/inhibit
	 * cycles when changing sources etc.
	 */
	plugin->timeout_id = g_timeout_add (1000,
					    (GSourceFunc) (playing ? inhibit : uninhibit),
					    plugin);
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBGPMPlugin *plugin;
	GObject *shell_player;
	gboolean playing;
	RBShell *shell;

	plugin = RB_GPM_PLUGIN (bplugin);

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "shell-player", &shell_player, NULL);

	plugin->handler_id = g_signal_connect_object (shell_player,
						      "playing-changed",
						      (GCallback) playing_changed_cb,
						      plugin, 0);

	g_object_get (shell_player, "playing", &playing, NULL);
	if (playing) {
		inhibit (plugin);
	}

	g_object_unref (shell_player);
	g_object_unref (shell);
}

static void
impl_deactivate (PeasActivatable *bplugin)
{
	RBGPMPlugin *plugin;
	GObject *shell_player;
	RBShell *shell;

	plugin = RB_GPM_PLUGIN (bplugin);

	if (plugin->timeout_id != 0) {
		g_source_remove (plugin->timeout_id);
		plugin->timeout_id = 0;
	}

	if (plugin->cookie != 0) {
		uninhibit (plugin);
		plugin->cookie = 0;
	}

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "shell-player", &shell_player, NULL);

	if (plugin->handler_id != 0) {
		g_signal_handler_disconnect (shell_player, plugin->handler_id);
		plugin->handler_id = 0;
	}

	g_object_unref (shell);
	g_object_unref (shell_player);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_gpm_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_GPM_PLUGIN);
}
