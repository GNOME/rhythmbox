/*
 * rb-mmkeys-plugin.c
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
 *  Copyright (C) 2007  James Livingston  <doclivingston@gmail.com>
 *  Copyright (C) 2007  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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
#include <dbus/dbus-glib.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-util.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-marshal.h"

#ifdef HAVE_MMKEYS
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <gdk/gdkx.h>
#endif /* HAVE_MMKEYS */

#define RB_TYPE_MMKEYS_PLUGIN		(rb_mmkeys_plugin_get_type ())
#define RB_MMKEYS_PLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MMKEYS_PLUGIN, RBMMKeysPlugin))
#define RB_MMKEYS_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MMKEYS_PLUGIN, RBMMKeysPluginClass))
#define RB_IS_MMKEYS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MMKEYS_PLUGIN))
#define RB_IS_MMKEYS_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MMKEYS_PLUGIN))
#define RB_MMKEYS_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MMKEYS_PLUGIN, RBMMKeysPluginClass))

typedef struct
{
	RBPlugin parent;

	enum {
		NONE = 0,
		SETTINGS_DAEMON,
		X_KEY_GRAB
	} grab_type;
	RBShell *shell;
	RBShellPlayer *shell_player;
	DBusGProxy *proxy;
} RBMMKeysPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBMMKeysPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_mmkeys_plugin_get_type		(void) G_GNUC_CONST;

RB_PLUGIN_REGISTER(RBMMKeysPlugin, rb_mmkeys_plugin)
#define RB_MMKEYS_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_MMKEYS_PLUGIN, RBMMKeysPluginPrivate))


static void
rb_mmkeys_plugin_init (RBMMKeysPlugin *plugin)
{
	rb_debug ("RBMMKeysPlugin initialising");
}

static void
media_player_key_pressed (DBusGProxy *proxy,
			  const gchar *application,
			  const gchar *key,
			  RBMMKeysPlugin *plugin)
{
	rb_debug ("got media key '%s' for application '%s'",
		  key, application);

	if (strcmp (application, "Rhythmbox"))
		return;

	if (strcmp (key, "Play") == 0) {
		rb_shell_player_playpause (plugin->shell_player, FALSE, NULL);
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
}

static void
grab_call_notify (DBusGProxy *proxy, DBusGProxyCall *call, RBMMKeysPlugin *plugin)
{
	GError *error = NULL;
	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID) == FALSE) {
		g_warning ("Unable to grab media player keys: %s", error->message);
		g_error_free (error);
	}
}

static gboolean
window_focus_cb (GtkWidget *window,
		 GdkEventFocus *event,
		 RBMMKeysPlugin *plugin)
{
	rb_debug ("window got focus, re-grabbing media keys");

	dbus_g_proxy_begin_call (plugin->proxy,
				 "GrabMediaPlayerKeys",
				 (DBusGProxyCallNotify) grab_call_notify,
				 g_object_ref (plugin),
				 (GDestroyNotify) g_object_unref,
				 G_TYPE_STRING, "Rhythmbox",
				 G_TYPE_UINT, 0,
				 G_TYPE_INVALID);
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
		rb_shell_player_playpause (player, FALSE, NULL);
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
	guint i, j;

	display = gdk_display_get_default ();
	keycodes[0] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPlay);
	keycodes[1] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioStop);
	keycodes[2] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPrev);
	keycodes[3] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioNext);
	keycodes[4] = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (display), XF86XK_AudioPause);

	for (i = 0; i < gdk_display_get_n_screens (display); i++) {
		screen = gdk_display_get_screen (display, i);

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
}

#endif

static void
first_call_notify (DBusGProxy *proxy, DBusGProxyCall *call, RBMMKeysPlugin *plugin)
{
	GError *error = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID) == FALSE) {
		g_warning ("Unable to grab media player keys: %s", error->message);
		g_error_free (error);
	} else {
		GtkWindow *window;

		rb_debug ("created dbus proxy for org.gnome.SettingsDaemon.MediaKeys; grabbing keys");
		dbus_g_object_register_marshaller (rb_marshal_VOID__STRING_STRING,
				G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

		dbus_g_proxy_add_signal (plugin->proxy,
					 "MediaPlayerKeyPressed",
					 G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INVALID);

		dbus_g_proxy_connect_signal (plugin->proxy,
					     "MediaPlayerKeyPressed",
					     G_CALLBACK (media_player_key_pressed),
					     plugin, NULL);

		/* re-grab keys when the main window gains focus */
		g_object_get (plugin->shell, "window", &window, NULL);
		g_signal_connect_object (window, "focus-in-event",
					 G_CALLBACK (window_focus_cb),
					 plugin, 0);
		g_object_unref (window);
	}
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	DBusGConnection *bus;
	RBMMKeysPlugin *plugin;

	rb_debug ("activating media player keys plugin");

	plugin = RB_MMKEYS_PLUGIN (bplugin);
	g_object_get (shell,
		      "shell-player", &plugin->shell_player,
		      NULL);
	plugin->shell = g_object_ref (shell);

	bus = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	if (plugin->grab_type == NONE && bus != NULL) {
		GError *error = NULL;

		plugin->proxy = dbus_g_proxy_new_for_name_owner (bus,
								 "org.gnome.SettingsDaemon",
								 "/org/gnome/SettingsDaemon/MediaKeys",
								 "org.gnome.SettingsDaemon.MediaKeys",
								 &error);
		if (plugin->proxy == NULL) {
			g_warning ("Unable to grab media player keys: %s", error->message);
			g_error_free (error);
		} else {
			dbus_g_proxy_begin_call (plugin->proxy,
						 "GrabMediaPlayerKeys",
						 (DBusGProxyCallNotify)first_call_notify,
						 g_object_ref (plugin),
						 (GDestroyNotify) g_object_unref,
						 G_TYPE_STRING, "Rhythmbox",
						 G_TYPE_UINT, 0,
						 G_TYPE_INVALID);
			plugin->grab_type = SETTINGS_DAEMON;
		}
	} else {
		rb_debug ("couldn't get dbus session bus");
	}

#ifdef HAVE_MMKEYS
	if (plugin->grab_type == NONE) {
		rb_debug ("attempting old-style key grabs");
		mmkeys_grab (plugin, TRUE);
		plugin->grab_type = X_KEY_GRAB;
	}
#endif
}

static void
final_call_notify (DBusGProxy *proxy, DBusGProxyCall *call, gpointer nothing)
{
	GError *error = NULL;
	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID) == FALSE) {
		g_warning ("Unable to release media player keys: %s", error->message);
		g_error_free (error);
	}
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBMMKeysPlugin *plugin;

	plugin = RB_MMKEYS_PLUGIN (bplugin);
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
			dbus_g_proxy_begin_call (plugin->proxy,
						 "ReleaseMediaPlayerKeys",
						 (DBusGProxyCallNotify) final_call_notify,
						 NULL,
						 NULL,
						 G_TYPE_STRING, "Rhythmbox",
						 G_TYPE_INVALID);
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


static void
rb_mmkeys_plugin_class_init (RBMMKeysPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}
