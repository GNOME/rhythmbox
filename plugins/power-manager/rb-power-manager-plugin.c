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
#include <dbus/dbus-glib.h>
#include <gdk/gdkx.h>

#include "rb-plugin.h"
#include "rb-debug.h"

#define RB_TYPE_GPM_PLUGIN		(rb_gpm_plugin_get_type ())
#define RB_GPM_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GPM_PLUGIN, RBGPMPlugin))
#define RB_GPM_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GPM_PLUGIN, RBGPMPluginClass))
#define RB_IS_GPM_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GPM_PLUGIN))
#define RB_IS_GPM_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GPM_PLUGIN))
#define RB_GPM_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GPM_PLUGIN, RBGPMPluginClass))

typedef struct
{
	RBPlugin parent;

	DBusGConnection *bus;
	DBusGProxy *proxy;
	guint32 cookie;
	gint handler_id;
	gint timeout_id;
	RBShell *shell;
} RBGPMPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBGPMPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_gpm_plugin_get_type		(void) G_GNUC_CONST;

static void rb_gpm_plugin_init (RBGPMPlugin *plugin);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER(RBGPMPlugin, rb_gpm_plugin)

static void
rb_gpm_plugin_class_init (RBGPMPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);
	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

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
	if (error->domain == DBUS_GERROR) {
		if (error->code == DBUS_GERROR_NAME_HAS_NO_OWNER ||
		    error->code == DBUS_GERROR_SERVICE_UNKNOWN)
			return TRUE;
	}

	return FALSE;
}

static void
proxy_destroy_cb (DBusGProxy *proxy,
		  RBGPMPlugin *plugin)
{
	rb_debug ("dbus proxy destroyed");
	plugin->proxy = NULL;
}

static gboolean
create_dbus_proxy (RBGPMPlugin *plugin)
{
	GError *error = NULL;

	if (plugin->proxy != NULL) {
		return TRUE;
	}

	/* try new name first */
	plugin->proxy = dbus_g_proxy_new_for_name_owner (plugin->bus,
						   "org.gnome.SessionManager",
						   "/org/gnome/SessionManager",
						   "org.gnome.SessionManager",
						   &error);
	if (error != NULL && ignore_error (error) == FALSE) {
		g_warning ("Failed to create dbus proxy for org.gnome.SessionManager: %s",
			   error->message);
		g_error_free (error);
		return FALSE;
	}

	g_signal_connect_object (plugin->proxy,
				 "destroy",
				 G_CALLBACK (proxy_destroy_cb),
				 plugin, 0);
	return TRUE;
}

static void
inhibit_cb (DBusGProxy *proxy,
	    DBusGProxyCall *call_id,
	    RBGPMPlugin *plugin)
{
	GError *error = NULL;

	dbus_g_proxy_end_call (proxy,
			       call_id,
			       &error,
			       G_TYPE_UINT, &plugin->cookie,
			       G_TYPE_INVALID);
	if (error != NULL) {
		if (!ignore_error (error)) {
			g_warning ("Failed to invoke %s.Inhibit: %s",
				   dbus_g_proxy_get_interface (proxy),
				   error->message);
		} else {
			rb_debug ("inhibit failed: %s", error->message);
		}
		g_error_free (error);
	} else {
		rb_debug ("got cookie %u", plugin->cookie);
	}

	g_object_unref (plugin);
}

static gboolean
inhibit (RBGPMPlugin *plugin)
{
	GtkWindow *window;
	plugin->timeout_id = 0;
	gulong xid = 0;

	if (plugin->cookie != 0) {
		rb_debug ("Was going to inhibit gnome-session, but we already have done");
		return FALSE;
	}

	if (create_dbus_proxy (plugin) == FALSE) {
		return FALSE;
	}

	rb_debug ("inhibiting");
	g_object_ref (plugin);
	g_object_get (plugin->shell, "window", &window, NULL);
	xid = GDK_WINDOW_XWINDOW (gtk_widget_get_window (GTK_WIDGET (window)));
	dbus_g_proxy_begin_call (plugin->proxy, "Inhibit",
				 (DBusGProxyCallNotify) inhibit_cb,
				 plugin,
				 NULL,
				 G_TYPE_STRING, "rhythmbox",
				 G_TYPE_UINT, xid,
				 G_TYPE_STRING, _("Playing"),
				 G_TYPE_UINT, 4, /* flags */
				 G_TYPE_INVALID);

	return FALSE;
}

static void
uninhibit_cb (DBusGProxy *proxy,
	      DBusGProxyCall *call_id,
	      RBGPMPlugin *plugin)
{
	GError *error = NULL;

	dbus_g_proxy_end_call (proxy,
			       call_id,
			       &error,
			       G_TYPE_INVALID);
	if (error != NULL) {
		if (!ignore_error (error)) {
			g_warning ("Failed to invoke %s.Inhibit: %s",
				   dbus_g_proxy_get_interface (proxy),
				   error->message);
		} else {
			rb_debug ("uninhibit failed: %s", error->message);
		}
		g_error_free (error);
	} else {
		rb_debug ("uninhibited");
		plugin->cookie = 0;
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
	g_object_ref (plugin);
	dbus_g_proxy_begin_call (plugin->proxy, "Uninhibit",
				 (DBusGProxyCallNotify) uninhibit_cb,
				 plugin,
				 NULL,
				 G_TYPE_UINT, plugin->cookie,
				 G_TYPE_INVALID);
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
impl_activate (RBPlugin *rbplugin,
	       RBShell *shell)
{
	RBGPMPlugin *plugin;
	GError *error = NULL;
	GObject *shell_player;
	gboolean playing;

	plugin = RB_GPM_PLUGIN (rbplugin);

	plugin->shell = g_object_ref (shell);
	plugin->bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (plugin->bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s", (error) ? error->message : "(null)");
		return;
	}

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
}

static void
impl_deactivate (RBPlugin *rbplugin,
		 RBShell *shell)
{
	RBGPMPlugin *plugin;
	GObject *shell_player;

	plugin = RB_GPM_PLUGIN (rbplugin);

	if (plugin->timeout_id != 0) {
		g_source_remove (plugin->timeout_id);
		plugin->timeout_id = 0;
	}

	if (plugin->cookie != 0) {
		uninhibit (plugin);
		plugin->cookie = 0;
	}

	g_object_get (shell, "shell-player", &shell_player, NULL);

	if (plugin->handler_id != 0) {
		g_signal_handler_disconnect (shell_player, plugin->handler_id);
		plugin->handler_id = 0;
	}

	g_object_unref (plugin->shell);
	g_object_unref (shell_player);

	if (plugin->proxy != NULL) {
		g_object_unref (plugin->proxy);
		plugin->proxy = NULL;
	}
}

