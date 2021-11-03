/*
 * rb-mmkeys-plugin.c
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
 *  Copyright (C) 2007  James Livingston  <doclivingston@gmail.com>
 *  Copyright (C) 2007  Jonathan Matthew  <jonathan@d14n.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>

#include "rb-plugin-macros.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"

#ifdef HAVE_MMKEYS
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <gdk/gdkx.h>
#endif /* HAVE_MMKEYS */

#define RB_TYPE_MMKEYS_PLUGIN		(rb_mmkeys_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBMMKeysPlugin, rb_mmkeys_plugin, RB, MMKEYS_PLUGIN, PeasExtensionBase)

struct _RBMMKeysPlugin
{
	PeasExtensionBase parent;

	enum {
		NONE = 0,
		SETTINGS_DAEMON,
		X_KEY_GRAB
	} grab_type;
	RBShell *shell;
	RBShellPlayer *shell_player;
	GDBusProxy *proxy;
} ;

struct _RBMMKeysPluginClass
{
	PeasExtensionBaseClass parent_class;
};

RB_DEFINE_PLUGIN(RB_TYPE_MMKEYS_PLUGIN, RBMMKeysPlugin, rb_mmkeys_plugin,)

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void
rb_mmkeys_plugin_init (RBMMKeysPlugin *plugin)
{
	rb_debug ("RBMMKeysPlugin initialising");
}

static void
media_player_key_pressed (GDBusProxy *proxy,
			  const char *sender,
			  const char *signal,
			  GVariant *parameters,
			  RBMMKeysPlugin *plugin)
{
	char *key;
	char *application;

	if (g_strcmp0 (signal, "MediaPlayerKeyPressed") != 0) {
		rb_debug ("got unexpected signal '%s' from media player keys", signal);
		return;
	}

	g_variant_get (parameters, "(ss)", &application, &key);

	rb_debug ("got media key '%s' for application '%s'",
		  key, application);

	if (strcmp (application, "Rhythmbox")) {
		rb_debug ("got media player key signal for unexpected application '%s'", application);
		return;
	}

	if (strcmp (key, "Play") == 0) {
		rb_shell_player_playpause (plugin->shell_player, NULL);
	} else if (strcmp (key, "Pause") == 0) {
		rb_shell_player_pause (plugin->shell_player, NULL);
	} else if (strcmp (key, "Stop") == 0) {
		rb_shell_player_stop (plugin->shell_player);
	} else if (strcmp (key, "Previous") == 0) {
		rb_shell_player_do_previous (plugin->shell_player, NULL);
	} else if (strcmp (key, "Next") == 0) {
		rb_shell_player_do_next (plugin->shell_player, NULL);
	} else if (strcmp (key, "Repeat") == 0) {
		gboolean shuffle, repeat;

		if (rb_shell_player_get_playback_state (plugin->shell_player, &shuffle, &repeat)) {
			rb_shell_player_set_playback_state (plugin->shell_player, shuffle, !repeat);
		}
	} else if (strcmp (key, "Shuffle") == 0) {
		gboolean shuffle, repeat;

		if (rb_shell_player_get_playback_state (plugin->shell_player, &shuffle, &repeat)) {
			rb_shell_player_set_playback_state (plugin->shell_player, !shuffle, repeat);
		}
	} else if (strcmp (key, "FastForward") == 0) {
		rb_shell_player_seek (plugin->shell_player, FFWD_OFFSET, NULL);
	} else if (strcmp (key, "Rewind") == 0) {
		rb_shell_player_seek (plugin->shell_player, -RWD_OFFSET, NULL);
	}

	g_free (key);
	g_free (application);
}

static void
grab_call_complete (GObject *proxy, GAsyncResult *res, RBMMKeysPlugin *plugin)
{
	GError *error = NULL;
	GVariant *result;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
	if (error != NULL) {
		g_warning ("Unable to grab media player keys: %s", error->message);
		g_clear_error (&error);
	} else {
		g_variant_unref (result);
	}
}

static gboolean
window_focus_cb (GtkWidget *window,
		 GdkEventFocus *event,
		 RBMMKeysPlugin *plugin)
{
	rb_debug ("window got focus, re-grabbing media keys");

	g_dbus_proxy_call (plugin->proxy,
			   "GrabMediaPlayerKeys",
			   g_variant_new ("(su)", "Rhythmbox", 0),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   (GAsyncReadyCallback) grab_call_complete,
			   plugin);
	return FALSE;
}

#ifdef HAVE_MMKEYS

static void
grab_mmkey (int key_code,
	    GdkWindow *root)
{
	Display *display;
	gdk_error_trap_push ();

	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	XGrabKey (display, key_code,
		  0,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod2Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod5Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod2Mask | Mod5Mask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod2Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod5Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);
	XGrabKey (display, key_code,
		  Mod2Mask | Mod5Mask | LockMask,
		  GDK_WINDOW_XID (root), True,
		  GrabModeAsync, GrabModeAsync);

	gdk_flush ();
        if (gdk_error_trap_pop ()) {
		rb_debug ("Error grabbing key");
	}
}

static void
ungrab_mmkey (int key_code,
	      GdkWindow *root)
{
	Display *display;
	gdk_error_trap_push ();

	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	XUngrabKey (display, key_code, 0, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod2Mask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod5Mask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod2Mask | Mod5Mask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod2Mask | LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod5Mask | LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (display, key_code, Mod2Mask | Mod5Mask | LockMask, GDK_WINDOW_XID (root));

	gdk_flush ();
        if (gdk_error_trap_pop ()) {
		rb_debug ("Error grabbing key");
	}
}


static GdkFilterReturn
filter_mmkeys (GdkXEvent *xevent,
	       GdkEvent *event,
	       gpointer data)
{
	XEvent *xev;
	XKeyEvent *key;
	Display *display;
	RBShellPlayer *player;
	xev = (XEvent *) xevent;
	if (xev->type != KeyPress) {
		return GDK_FILTER_CONTINUE;
	}

	key = (XKeyEvent *) xevent;

	player = (RBShellPlayer *)data;
	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	if (XKeysymToKeycode (display, XF86XK_AudioPlay) == key->keycode) {
		rb_shell_player_playpause (player, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (display, XF86XK_AudioPause) == key->keycode) {
		rb_shell_player_pause (player, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (display, XF86XK_AudioStop) == key->keycode) {
		rb_shell_player_stop (player);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (display, XF86XK_AudioPrev) == key->keycode) {
		rb_shell_player_do_previous (player, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (display, XF86XK_AudioNext) == key->keycode) {
		rb_shell_player_do_next (player, NULL);
		return GDK_FILTER_REMOVE;
	} else {
		return GDK_FILTER_CONTINUE;
	}
}

static void
mmkeys_grab (RBMMKeysPlugin *plugin, gboolean grab)
{
	gint keycodes[] = {0, 0, 0, 0, 0};
	GdkDisplay *display;
	GdkScreen *screen;
	GdkWindow *root;
	guint j;

	display = gdk_display_get_default ();
	keycodes[0] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPlay);
	keycodes[1] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioStop);
	keycodes[2] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPrev);
	keycodes[3] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioNext);
	keycodes[4] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPause);

	screen = gdk_display_get_default_screen (display);

	if (screen != NULL) {
		root = gdk_screen_get_root_window (screen);

		for (j = 0; j < G_N_ELEMENTS (keycodes) ; j++) {
			if (keycodes[j] != 0) {
				if (grab)
					grab_mmkey (keycodes[j], root);
				else
					ungrab_mmkey (keycodes[j], root);
			}
		}

		if (grab)
			gdk_window_add_filter (root, filter_mmkeys,
					       (gpointer) plugin->shell_player);
		else
			gdk_window_remove_filter (root, filter_mmkeys,
						  (gpointer) plugin->shell_player);
	}
}

#endif

static void
first_call_complete (GObject *proxy, GAsyncResult *res, RBMMKeysPlugin *plugin)
{
	GVariant *result;
	GError *error = NULL;
	GtkWindow *window;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
	if (error != NULL) {
		g_warning ("Unable to grab media player keys: %s", error->message);
		g_clear_error (&error);
#ifdef HAVE_MMKEYS
		if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
			mmkeys_grab (plugin, TRUE);
			plugin->grab_type = X_KEY_GRAB;
		}
#endif
		return;
	}

	rb_debug ("grabbed media player keys");

	g_signal_connect_object (plugin->proxy, "g-signal", G_CALLBACK (media_player_key_pressed), plugin, 0);

	/* re-grab keys when the main window gains focus */
	g_object_get (plugin->shell, "window", &window, NULL);
	g_signal_connect_object (window, "focus-in-event",
				 G_CALLBACK (window_focus_cb),
				 plugin, 0);
	g_object_unref (window);

	g_variant_unref (result);
}

static void
impl_activate (PeasActivatable *pplugin)
{
	GDBusConnection *bus;
	RBMMKeysPlugin *plugin;
	GError *error = NULL;

	rb_debug ("activating media player keys plugin");

	plugin = RB_MMKEYS_PLUGIN (pplugin);
	g_object_get (plugin, "object", &plugin->shell, NULL);
	g_object_get (plugin->shell, "shell-player", &plugin->shell_player, NULL);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (plugin->grab_type == NONE && bus != NULL) {
		GError *error = NULL;

		plugin->proxy = g_dbus_proxy_new_sync (bus,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       "org.gnome.SettingsDaemon.MediaKeys",
						       "/org/gnome/SettingsDaemon/MediaKeys",
						       "org.gnome.SettingsDaemon.MediaKeys",
						       NULL,
						       &error);
		if (error != NULL) {
			g_warning ("Unable to grab media player keys: %s", error->message);
			g_clear_error (&error);
		} else {
			g_dbus_proxy_call (plugin->proxy,
					   "GrabMediaPlayerKeys",
					   g_variant_new ("(su)", "Rhythmbox", 0),
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   (GAsyncReadyCallback) first_call_complete,
					   plugin);
			plugin->grab_type = SETTINGS_DAEMON;
		}
	} else {
		g_warning ("couldn't get dbus session bus: %s", error->message);
		g_clear_error (&error);
	}

#ifdef HAVE_MMKEYS
	if (plugin->grab_type == NONE && GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		rb_debug ("attempting old-style key grabs");
		mmkeys_grab (plugin, TRUE);
		plugin->grab_type = X_KEY_GRAB;
	}
#endif
}

static void
final_call_complete (GObject *proxy, GAsyncResult *res, gpointer nothing)
{
	GError *error = NULL;
	GVariant *result;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
	if (error != NULL) {
		g_warning ("Unable to release media player keys: %s", error->message);
		g_clear_error (&error);
	} else {
		g_variant_unref (result);
	}
}

static void
impl_deactivate	(PeasActivatable *pplugin)
{
	RBMMKeysPlugin *plugin;

	plugin = RB_MMKEYS_PLUGIN (pplugin);
	if (plugin->shell_player != NULL) {
		g_object_unref (plugin->shell_player);
		plugin->shell_player = NULL;
	}
	if (plugin->shell != NULL) {
		g_object_unref (plugin->shell);
		plugin->shell = NULL;
	}

	if (plugin->proxy != NULL) {
		if (plugin->grab_type == SETTINGS_DAEMON) {
			g_dbus_proxy_call (plugin->proxy,
					   "ReleaseMediaPlayerKeys",
					   g_variant_new ("(s)", "Rhythmbox"),
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   (GAsyncReadyCallback) final_call_complete,
					   NULL);
			plugin->grab_type = NONE;
		}

		g_object_unref (plugin->proxy);
		plugin->proxy = NULL;
	}

#ifdef HAVE_MMKEYS
	if (plugin->grab_type == X_KEY_GRAB) {
		rb_debug ("undoing old-style key grabs");
		mmkeys_grab (plugin, FALSE);
		plugin->grab_type = NONE;
	}
#endif
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_mmkeys_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_MMKEYS_PLUGIN);
}
