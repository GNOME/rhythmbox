/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: The Rhythmbox main entrypoint
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h> /* For _get_user_time... */
#include <gtk/gtk.h>
#include <glade/glade-init.h>
#include <libgnome/gnome-program.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomeui/gnome-app-helper.h>
#include <libgnomeui/gnome-authentication-manager.h>

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

#include <dbus/dbus-glib.h>
#include "rb-shell-glue.h"
#include "rb-shell-player-glue.h"
#include "rb-playlist-manager.h"
#include "rb-playlist-manager-glue.h"

#define HAVE_LIBGNOME_GOPTION defined(GNOME_PARAM_GOPTION_CONTEXT)

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

static void main_shell_weak_ref_cb (gpointer data, GObject *objptr);

int
main (int argc, char **argv)
{
	GnomeProgram *program = NULL;
	DBusGConnection *session_bus;
	GError *error = NULL;
	RBShell *rb_shell;
	char **new_argv;
	gboolean activated;

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

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	/* Disable event sounds for now by passing "--disable-sound" to libgnomeui.
	 * See: http://bugzilla.gnome.org/show_bug.cgi?id=119222 */
	new_argv = g_strdupv (argv);
	new_argv = g_realloc (new_argv, (argc+2)*sizeof(char*));
	new_argv[argc] = g_strdup ("--disable-sound");
	new_argv[argc+1] = NULL;

	rb_profile_start ("initializing gstreamer");
	g_option_context_add_group (context, gst_init_get_option_group ());
	rb_profile_end ("initializing gstreamer");

	gtk_set_locale ();
	rb_profile_start ("initializing gnome program");

#if HAVE_LIBGNOME_GOPTION
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE,
				      argc+1, new_argv,
				      GNOME_PARAM_GOPTION_CONTEXT, context,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);
#else
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE,
				      argc+1, new_argv,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);
#endif

	rb_profile_end ("initializing gnome program");

	rb_profile_start ("initializing gnome auth manager");
	gnome_authentication_manager_init ();
	rb_profile_end ("initializing gnome auth manager");

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

		rb_file_helpers_init ();

		rb_debug ("Going to create a new shell");

		glade_gnome_init ();

		rb_stock_icons_init ();

		gtk_window_set_default_icon_name ("rhythmbox");

		rb_shell = rb_shell_new (argc, argv, no_registration, no_update, dry_run, rhythmdb_file, playlists_file);
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
		gtk_main ();
		rb_profile_end ("mainloop");

		rb_debug ("out of toplevel loop");

		rb_file_helpers_shutdown ();
		rb_stock_icons_shutdown ();
		rb_refstring_system_shutdown ();

		gnome_vfs_shutdown ();
	}

	gst_deinit ();

	g_strfreev (new_argv);

	rb_debug ("THE END");
	rb_profile_end ("starting rhythmbox");
	g_object_unref (program);

	gnome_accelerators_sync ();
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
		char *uri;

		rb_debug ("examining argument %s", args[i]);

		uri = gnome_vfs_make_uri_from_shell_arg (args[i]);

		if (rb_uri_is_local (uri) == FALSE || rb_uri_exists (uri)) {
			handler (uri, user_data);
		}
		g_free (uri);

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
				G_TYPE_INVALID))
		g_printerr ("Failed to load %s: %s",
			    filename, error->message);
}

static void
main_shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	rb_debug ("caught shell finalization");
	gtk_main_quit ();
}
