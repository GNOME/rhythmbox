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
#include <gdk/gdkx.h>
#include <gio/gio.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"

#define RB_TYPE_GPM_PLUGIN		(rb_gpm_plugin_get_type ())
#define RB_GPM_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GPM_PLUGIN, RBGPMPlugin))
#define RB_GPM_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GPM_PLUGIN, RBGPMPluginClass))
#define RB_IS_GPM_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GPM_PLUGIN))
#define RB_IS_GPM_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GPM_PLUGIN))
#define RB_GPM_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GPM_PLUGIN, RBGPMPluginClass))

typedef struct
{
	PeasExtensionBase parent;

	GDBusProxy *proxy;
	guint32 cookie;
	gint handler_id;
	gint timeout_id;
} RBGPMPlugin;

typedef struct
{
	PeasExtensionBaseClass parent_class;
} RBGPMPluginClass;

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

RB_DEFINE_PLUGIN(RB_TYPE_GPM_PLUGIN, RBGPMPlugin, rb_gpm_plugin,)

static void
rb_gpm_plugin_init (RBGPMPlugin *plugin)
{
	rb_debug ("RBGPMPlugin initialising");
}

static gboolean
ignore_error (GError *error)
{
	if (error == NULL)
		return TRUE;

	/* ignore 'no such service' type errors */
	if (error->domain == G_DBUS_ERROR) {
		if (error->code == G_DBUS_ERROR_NAME_HAS_NO_OWNER ||
		    error->code == G_DBUS_ERROR_SERVICE_UNKNOWN)
			return TRUE;
	}

	return FALSE;
}

static gboolean
create_dbus_proxy (RBGPMPlugin *plugin)
{
	GError *error = NULL;

	if (plugin->proxy != NULL) {
		return TRUE;
	}

	plugin->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       "org.gnome.SessionManager",
						       "/org/gnome/SessionManager",
						       "org.gnome.SessionManager",
						       NULL,
						       &error);
	if (error != NULL && ignore_error (error) == FALSE) {
		g_warning ("Failed to create dbus proxy for org.gnome.SessionManager: %s",
			   error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

static void
inhibit_done (GObject *proxy, GAsyncResult *res, RBGPMPlugin *plugin)
{
	GError *error = NULL;
	GVariant *result;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
	if (error != NULL) {
		if (!ignore_error (error)) {
			g_warning ("Unable to inhibit session suspend: %s", error->message);
		} else {
			rb_debug ("unable to inhibit: %s", error->message);
		}
		g_clear_error (&error);
	} else {
		g_variant_get (result, "(u)", &plugin->cookie);
		rb_debug ("inhibited, got cookie %u", plugin->cookie);

		g_variant_unref (result);
	}
	g_object_unref (plugin);
}

static gboolean
inhibit (RBGPMPlugin *plugin)
{
	GtkWindow *window;
	gulong xid = 0;
	GError *error = NULL;
	RBShell *shell;

	plugin->timeout_id = 0;
	if (plugin->cookie != 0) {
		rb_debug ("Was going to inhibit gnome-session, but we already have done");
		return FALSE;
	}

	if (create_dbus_proxy (plugin) == FALSE) {
		return FALSE;
	}

	rb_debug ("inhibiting");
	g_object_ref (plugin);

	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell, "window", &window, NULL);
	g_object_unref (shell);

	xid = gdk_x11_window_get_xid (gtk_widget_get_window (GTK_WIDGET (window)));
	g_dbus_proxy_call (plugin->proxy,
			   "Inhibit",
			   g_variant_new ("(susu)", "rhythmbox", xid, _("Playing"), 4),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   (GAsyncReadyCallback) inhibit_done,
			   plugin);
	if (error != NULL) {
		g_warning ("Unable to inhibit session suspend: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (window);
	return FALSE;
}

static void
uninhibit_done (GObject *proxy, GAsyncResult *res, RBGPMPlugin *plugin)
{
	GError *error = NULL;
	GVariant *result;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
	if (error != NULL) {
		if (!ignore_error (error)) {
			g_warning ("Failed to uninhibit session suspend: %s", error->message);
		} else {
			rb_debug ("failed to uninhibit: %s", error->message);
		}
		g_clear_error (&error);
	} else {
		rb_debug ("uninhibited");
		plugin->cookie = 0;

		g_variant_unref (result);
	}
	g_object_unref (plugin);
}

static gboolean
uninhibit (RBGPMPlugin *plugin)
{
	plugin->timeout_id = 0;

	if (plugin->cookie == 0) {
		rb_debug ("Was going to uninhibit session manager, but we haven't inhibited it");
		return FALSE;
	}

	if (create_dbus_proxy (plugin) == FALSE) {
		return FALSE;
	}

	rb_debug ("uninhibiting; cookie = %u", plugin->cookie);
	g_dbus_proxy_call (plugin->proxy,
			   "Uninhibit",
			   g_variant_new ("(u)", plugin->cookie),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   (GAsyncReadyCallback) uninhibit_done,
			   g_object_ref (plugin));
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

	if (plugin->proxy != NULL) {
		g_object_unref (plugin->proxy);
		plugin->proxy = NULL;
	}
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_gpm_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_GPM_PLUGIN);
}
