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
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-marshal.h"

#ifdef WITH_DBUS
#include <dbus/dbus-glib.h>
#endif

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
	RBShellPlayer *shell_player;
#ifdef WITH_DBUS
	DBusGProxy *proxy;
#endif
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

#ifdef WITH_DBUS
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
	} else if (strcmp (key, "Pause") == 0 ||
		   strcmp (key, "Stop") == 0) {
		rb_shell_player_pause (plugin->shell_player, NULL);
	} else if (strcmp (key, "Previous") == 0) {
		rb_shell_player_do_previous (plugin->shell_player, NULL);
	} else if (strcmp (key, "Next") == 0) {
		rb_shell_player_do_next (plugin->shell_player, NULL);
	}
}

static gboolean
window_focus_cb (GtkWidget *window,
		 GdkEventFocus *event,
		 RBMMKeysPlugin *plugin)
{
	rb_debug ("window got focus, re-grabbing media keys");

	dbus_g_proxy_call (plugin->proxy,
			   "GrabMediaPlayerKeys", NULL,
			   G_TYPE_STRING, "Rhythmbox",
			   G_TYPE_UINT, 0,
			   G_TYPE_INVALID, G_TYPE_INVALID);

	return FALSE;
}

#endif

#ifdef HAVE_MMKEYS

static void
grab_mmkey (int key_code,
	    GdkWindow *root)
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

static void
ungrab_mmkey (int key_code,
	      GdkWindow *root)
{
	gdk_error_trap_push ();

	XUngrabKey (GDK_DISPLAY (), key_code, 0, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod2Mask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod5Mask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod2Mask | Mod5Mask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod2Mask | LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod5Mask | LockMask, GDK_WINDOW_XID (root));
	XUngrabKey (GDK_DISPLAY (), key_code, Mod2Mask | Mod5Mask | LockMask, GDK_WINDOW_XID (root));

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
	RBShellPlayer *player;
	xev = (XEvent *) xevent;
	if (xev->type != KeyPress) {
		return GDK_FILTER_CONTINUE;
	}

	key = (XKeyEvent *) xevent;

	player = (RBShellPlayer *)data;

	if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPlay) == key->keycode) {
		rb_shell_player_playpause (player, FALSE, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPause) == key->keycode) {
		rb_shell_player_pause (player, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioStop) == key->keycode) {
		rb_shell_player_stop (player);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioPrev) == key->keycode) {
		rb_shell_player_do_previous (player, NULL);
		return GDK_FILTER_REMOVE;
	} else if (XKeysymToKeycode (GDK_DISPLAY (), XF86XK_AudioNext) == key->keycode) {
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
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
#ifdef WITH_DBUS
	DBusGConnection *bus;
#endif
	RBMMKeysPlugin *plugin;

	rb_debug ("activating media player keys plugin");

	plugin = RB_MMKEYS_PLUGIN (bplugin);
	g_object_get (shell,
		      "shell-player", &plugin->shell_player,
		      NULL);

#ifdef WITH_DBUS
	bus = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	if (plugin->grab_type == NONE && bus != NULL) {
		GError *error = NULL;

		plugin->proxy = dbus_g_proxy_new_for_name (bus,
				"org.gnome.SettingsDaemon",
				"/org/gnome/SettingsDaemon",
				"org.gnome.SettingsDaemon");
		if (plugin->proxy != NULL) {
			dbus_g_proxy_call (plugin->proxy,
					   "GrabMediaPlayerKeys", &error,
					   G_TYPE_STRING, "Rhythmbox",
					   G_TYPE_UINT, 0,
					   G_TYPE_INVALID,
					   G_TYPE_INVALID);
			if (error == NULL) {
				GtkWindow *window;

				rb_debug ("created dbus proxy for org.gnome.SettingsDaemon; grabbing keys");
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
				g_object_get (shell, "window", &window, NULL);
				g_signal_connect_object (window, "focus-in-event",
							 G_CALLBACK (window_focus_cb),
							 plugin, 0);
				g_object_unref (window);

				plugin->grab_type = SETTINGS_DAEMON;

			} else if (error->domain == DBUS_GERROR &&
				   (error->code != DBUS_GERROR_NAME_HAS_NO_OWNER ||
				   error->code != DBUS_GERROR_SERVICE_UNKNOWN)) {
				/* settings daemon dbus service doesn't exist.
				 * just silently fail.
				 */
				rb_debug ("org.gnome.SettingsDaemon dbus service not found");
				g_error_free (error);
			} else {
				g_warning ("Unable to grab media player keys: %s", error->message);
				g_error_free (error);
			}
		}
	} else {
		rb_debug ("couldn't get dbus session bus");
	}
#endif

#ifdef HAVE_MMKEYS
	if (plugin->grab_type == NONE) {
		rb_debug ("attempting old-style key grabs");
		mmkeys_grab (plugin, TRUE);
		plugin->grab_type = X_KEY_GRAB;
	}
#endif
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

#ifdef WITH_DBUS
	if (plugin->proxy != NULL) {
		GError *error = NULL;

		if (plugin->grab_type == SETTINGS_DAEMON) {
			dbus_g_proxy_call (plugin->proxy,
					   "ReleaseMediaPlayerKeys", &error,
					   G_TYPE_STRING, "Rhythmbox",
					   G_TYPE_INVALID, G_TYPE_INVALID);
			if (error != NULL) {
				g_warning ("Could not release media player keys: %s", error->message);
				g_error_free (error);
			}
			plugin->grab_type = NONE;
		}

		g_object_unref (plugin->proxy);
		plugin->proxy = NULL;
	}
#endif
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

