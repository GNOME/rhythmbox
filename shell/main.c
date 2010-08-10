/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <config.h>

#ifdef ENABLE_PYTHON
/* pyconfig.h usually defines _XOPEN_SOURCE */
#undef _XOPEN_SOURCE
#define NO_IMPORT_PYGOBJECT
#define NO_IMPORT_PYGTK
#include <pygobject.h>
#include "rb-python-module.h"

/* make sure it's defined somehow */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#endif

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h> /* For _get_user_time... */
#include <gtk/gtk.h>

#include <gst/gst.h>

#ifdef WITH_RHYTHMDB_GDA
#include <libgda/libgda.h>
#endif

#include "rb-refstring.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-stock-icons.h"
#include "rb-util.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"
#include "eggdesktopfile.h"
#include "eggsmclient.h"

#include <dbus/dbus-glib.h>
#include "rb-shell-glue.h"
#include "rb-shell-player-glue.h"
#include "rb-playlist-manager.h"
#include "rb-playlist-manager-glue.h"


static gboolean debug           = FALSE;
static char *debug_match        = NULL;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;
static gboolean no_update	= FALSE;
static gboolean dry_run		= FALSE;
static char *rhythmdb_file	= NULL;
static char *playlists_file	= NULL;
static char **remaining_args    = NULL;

static gboolean load_uri_args (const char **args, GFunc handler, gpointer user_data);
static void dbus_load_uri (const char *filename, DBusGProxy *proxy);
static void database_load_complete (RBShell *shell, gpointer data);
static void local_load_uri (const char *filename, RBShell *shell);

static void main_shell_weak_ref_cb (gpointer data, GObject *objptr);

int
main (int argc, char **argv)
{
	DBusGConnection *session_bus;
	GError *error = NULL;
	RBShell *rb_shell;
	gboolean activated;
	gboolean autostarted;
	char *accel_map_file = NULL;
	char *desktop_file_path;

	GOptionContext *context;
	static const GOptionEntry options []  = {
		{ "debug",           'd', 0, G_OPTION_ARG_NONE,         &debug,           N_("Enable debug output"), NULL },
		{ "debug-match",     'D', 0, G_OPTION_ARG_STRING,       &debug_match,     N_("Enable debug output matching a specified string"), NULL },
		{ "no-update",	       0, 0, G_OPTION_ARG_NONE,         &no_update,       N_("Do not update the library with file changes"), NULL },
		{ "no-registration", 'n', 0, G_OPTION_ARG_NONE,         &no_registration, N_("Do not register the shell"), NULL },
		{ "dry-run",	       0, 0, G_OPTION_ARG_NONE,         &dry_run,         N_("Don't save any data permanently (implies --no-registration)"), NULL },
		{ "rhythmdb-file",     0, 0, G_OPTION_ARG_STRING,       &rhythmdb_file,   N_("Path for database file to use"), NULL },
		{ "playlists-file",    0, 0, G_OPTION_ARG_STRING,       &playlists_file,   N_("Path for playlists file to use"), NULL },
		{ "quit",	     'q', 0, G_OPTION_ARG_NONE,         &quit,            N_("Quit Rhythmbox"), NULL },
		{ G_OPTION_REMAINING,  0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args,  NULL, N_("[URI...]") },
		{ NULL }
	};

	g_thread_init (NULL);

	rb_profile_start ("starting rhythmbox");

	autostarted = (g_getenv ("DESKTOP_AUTOSTART_ID") != NULL);

#ifdef USE_UNINSTALLED_DIRS
	desktop_file_path = g_build_filename (SHARE_UNINSTALLED_BUILDDIR, "rhythmbox.desktop", NULL);
#else
	desktop_file_path = g_build_filename (DATADIR, "applications", "rhythmbox.desktop", NULL);
#endif
	egg_set_desktop_file (desktop_file_path);
	g_free (desktop_file_path);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	rb_profile_start ("initializing gstreamer");
	g_option_context_add_group (context, gst_init_get_option_group ());
	rb_profile_end ("initializing gstreamer");

	g_option_context_add_group (context, egg_sm_client_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));

	gtk_set_locale ();

	rb_profile_start ("parsing command line options");
	if (g_option_context_parse (context, &argc, &argv, &error) == FALSE) {
		g_print (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			 error->message, argv[0]);
		g_error_free (error);
		g_option_context_free (context);
		exit (1);
	}
	g_option_context_free (context);
	rb_profile_end ("parsing command line options");

	g_random_set_seed (time (0));

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	/* ask for utf-8 message text from GStreamer too,
	 * since it doesn't do that itself.
	 */
	bind_textdomain_codeset ("gstreamer-0.10", "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	if (!debug && debug_match)
		rb_debug_init_match (debug_match);
	else
		rb_debug_init (debug);
	rb_debug ("initializing Rhythmbox %s", VERSION);

	/* TODO: kill this function */
	rb_threads_init ();
	gdk_threads_enter ();

	activated = FALSE;

	rb_debug ("going to create DBus object");

	dbus_g_thread_init ();

	session_bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (session_bus == NULL) {
		g_warning ("couldn't connect to session bus: %s", (error) ? error->message : "(null)");
		g_clear_error (&error);
	} else if (!no_registration) {
		guint request_name_reply;
		int flags;
#ifndef DBUS_NAME_FLAG_DO_NOT_QUEUE
		flags = DBUS_NAME_FLAG_PROHIBIT_REPLACEMENT;
#else
		flags = DBUS_NAME_FLAG_DO_NOT_QUEUE;
#endif

		DBusGProxy *bus_proxy;

		bus_proxy = dbus_g_proxy_new_for_name (session_bus,
						       "org.freedesktop.DBus",
						       "/org/freedesktop/DBus",
						       "org.freedesktop.DBus");

		if (!dbus_g_proxy_call (bus_proxy,
					"RequestName",
					&error,
					G_TYPE_STRING,
					"org.gnome.Rhythmbox",
					G_TYPE_UINT,
					flags,
					G_TYPE_INVALID,
					G_TYPE_UINT,
					&request_name_reply,
					G_TYPE_INVALID)) {
			g_warning ("Failed to invoke RequestName: %s",
				   error->message);
		}
		g_object_unref (bus_proxy);

		if (request_name_reply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
		    || request_name_reply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER)
			activated = FALSE;
		else if (request_name_reply == DBUS_REQUEST_NAME_REPLY_EXISTS
			 || request_name_reply == DBUS_REQUEST_NAME_REPLY_IN_QUEUE)
			activated = TRUE;
		else {
			g_warning ("Got unhandled reply %u from RequestName",
				   request_name_reply);
			activated = FALSE;
		}
	}

	if (!activated) {
		if (quit) {
			rb_debug ("was asked to quit, but no instance was running");
			gdk_notify_startup_complete ();
			exit (0);
		}
#ifdef WITH_RHYTHMDB_GDA
		gda_init (PACKAGE, VERSION, argc, argv);
#endif

		rb_refstring_system_init ();

#ifdef USE_UNINSTALLED_DIRS
		rb_file_helpers_init (TRUE);
#else
		rb_file_helpers_init (FALSE);
#endif

		/* XXX not sure what to do with this.  should we move it to
		 * the config dir, or leave it where it is?
		 */
		accel_map_file = g_build_filename (g_get_home_dir (),
						   ".gnome2",
						   "accels",
						   "rhythmbox",
						   NULL);
		gtk_accel_map_load (accel_map_file);


		rb_debug ("Going to create a new shell");

		rb_stock_icons_init ();

		g_setenv ("PULSE_PROP_media.role", "music", TRUE);

		rb_shell = rb_shell_new (no_registration, no_update, dry_run, autostarted, rhythmdb_file, playlists_file);
		g_object_weak_ref (G_OBJECT (rb_shell), main_shell_weak_ref_cb, NULL);
		if (!no_registration && session_bus != NULL) {
			GObject *obj;
			const char *path;

			dbus_g_object_type_install_info (RB_TYPE_SHELL, &dbus_glib_rb_shell_object_info);
			dbus_g_connection_register_g_object (session_bus, "/org/gnome/Rhythmbox/Shell", G_OBJECT (rb_shell));

			/* register player object */
			dbus_g_object_type_install_info (RB_TYPE_SHELL_PLAYER, &dbus_glib_rb_shell_player_object_info);
			obj = rb_shell_get_player (rb_shell);
			path = rb_shell_get_player_path (rb_shell);
			dbus_g_connection_register_g_object (session_bus, path, obj);

			/* register playlist manager object */
			dbus_g_object_type_install_info (RB_TYPE_PLAYLIST_MANAGER, &dbus_glib_rb_playlist_manager_object_info);
			obj = rb_shell_get_playlist_manager (rb_shell);
			path = rb_shell_get_playlist_manager_path (rb_shell);
			dbus_g_connection_register_g_object (session_bus, path, obj);

			g_signal_connect (G_OBJECT (rb_shell),
					  "database-load-complete",
					  G_CALLBACK (database_load_complete),
					  NULL);
		}
	} else if (!no_registration && session_bus != NULL) {
		DBusGProxy *shell_proxy;
		guint32 current_time;
		current_time = gdk_x11_display_get_user_time (gdk_display_get_default ());
		shell_proxy = dbus_g_proxy_new_for_name_owner (session_bus,
							       "org.gnome.Rhythmbox",
							       "/org/gnome/Rhythmbox/Shell",
							       "org.gnome.Rhythmbox.Shell",
							       &error);
		if (!shell_proxy) {
			g_warning ("Couldn't create proxy for Rhythmbox shell: %s",
				   error->message);
		} else {
			if (quit) {
				dbus_g_proxy_call_no_reply (shell_proxy, "quit",
							    G_TYPE_INVALID);
			} else {
				load_uri_args ((const char **) remaining_args, (GFunc) dbus_load_uri, shell_proxy);
				dbus_g_proxy_call_no_reply (shell_proxy, "present",
							    G_TYPE_UINT, current_time,
							    G_TYPE_INVALID);
			}
			g_object_unref (G_OBJECT (shell_proxy));
		}
	}

	if (activated) {
		gdk_notify_startup_complete ();
	} else {

		rb_profile_start ("mainloop");
#ifdef ENABLE_PYTHON
		if (rb_python_init_successful ()) {
			pyg_begin_allow_threads;
			gtk_main ();
			pyg_end_allow_threads;
		} else {
			gtk_main ();
		}
#else
		gtk_main ();
#endif
		rb_profile_end ("mainloop");

		rb_debug ("out of toplevel loop");

		rb_file_helpers_shutdown ();
		rb_stock_icons_shutdown ();
		rb_refstring_system_shutdown ();
	}

	gst_deinit ();

	rb_debug ("THE END");
	rb_profile_end ("starting rhythmbox");

	if (accel_map_file != NULL) {
		gtk_accel_map_save (accel_map_file);
	}

	gdk_threads_leave ();

	exit (0);
}

static gboolean
load_uri_args (const char **args, GFunc handler, gpointer user_data)
{
	gboolean handled;
	guint i;

	handled = FALSE;
	for (i = 0; args && args[i]; i++) {
		GFile *file;
		char *uri;

		rb_debug ("examining argument %s", args[i]);

		file = g_file_new_for_commandline_arg (args[i]);
		uri = g_file_get_uri (file);

		/*
		 * rb_uri_exists won't work if the location isn't mounted.
		 * however, things that are interesting to mount are generally
		 * non-local, so we'll process them anyway.
		 */
		if (rb_uri_is_local (uri) == FALSE || rb_uri_exists (uri)) {
			handler (uri, user_data);
		}
		g_free (uri);
		g_object_unref (file);

		handled = TRUE;
	}
	return handled;
}

static void
dbus_load_uri (const char *filename, DBusGProxy *proxy)
{
	GError *error = NULL;
	rb_debug ("Sending loadURI for %s", filename);
	if (!dbus_g_proxy_call (proxy, "loadURI", &error,
				G_TYPE_STRING, filename,
				G_TYPE_BOOLEAN, TRUE,
				G_TYPE_INVALID,
				G_TYPE_INVALID)) {
		g_printerr ("Failed to load %s: %s",
			    filename, error->message);
		g_error_free (error);
	}
}

static void
main_shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	rb_debug ("caught shell finalization");
	gtk_main_quit ();
}

static void
database_load_complete (RBShell *shell, gpointer data)
{
	load_uri_args ((const char **) remaining_args, (GFunc) local_load_uri, shell);
}

static void
local_load_uri (const char *filename, RBShell *shell)
{
	GError *error = NULL;
	rb_debug ("Using load_uri for %s", filename);
	if (!rb_shell_load_uri (shell, filename, TRUE, &error)) {
		if (error != NULL) {
			g_printerr ("Failed to load %s: %s",
				    filename, error->message);
			g_error_free (error);
		}
	}
}

