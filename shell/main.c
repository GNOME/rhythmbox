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
#include <libgnomeui/gnome-authentication-manager.h>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#ifdef HAVE_GSTREAMER_0_8
#include <gst/gconf/gconf.h>
#include <gst/control/control.h>
#endif
#endif
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

#if WITH_DBUS
#include <dbus/dbus-glib.h>
#include "rb-shell-glue.h"
#include "rb-shell-player-glue.h"
#include "rb-playlist-manager.h"
#include "rb-playlist-manager-glue.h"
#elif WITH_OLD_DBUS
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#endif

#include <nautilus-burn-drive.h>
#ifndef NAUTILUS_BURN_CHECK_VERSION
#define NAUTILUS_BURN_CHECK_VERSION(a,b,c) FALSE
#endif

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
#include <nautilus-burn.h>
#endif

static gboolean debug           = FALSE;
static char *debug_match        = NULL;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;
static gboolean no_update	= FALSE;
static gboolean dry_run		= FALSE;
static char *rhythmdb_file = NULL;
#if WITH_DBUS
static gboolean load_uri_args (const char **args, GFunc handler, gpointer user_data);
static void dbus_load_uri (const char *filename, DBusGProxy *proxy);
#endif

static void main_shell_weak_ref_cb (gpointer data, GObject *objptr);

#if WITH_OLD_DBUS
static void register_dbus_handler (DBusConnection *connection, RBShell *shell);
static gboolean send_present_message (DBusConnection *connection, guint32 current_time);

#endif

int
main (int argc, char **argv)
{
	GnomeProgram *program;
#if WITH_DBUS || WITH_OLD_DBUS
	DBusGConnection *session_bus;
	GError *error = NULL;
#endif
	RBShell *rb_shell;
	char **new_argv;
	gboolean activated;
	poptContext poptContext;
        GValue context_as_value = { 0 };

	rb_profile_start ("starting rhythmbox");

	struct poptOption popt_options[] =
	{
		{ "debug",			'd',POPT_ARG_NONE,	&debug,				0, N_("Enable debug output"), NULL },
		{ "debug-match",		'D',POPT_ARG_STRING,	&debug_match,			0, N_("Enable debug output matching a specified string"), NULL },
		{ "no-update",			0,  POPT_ARG_NONE,	&no_update,			0, N_("Do not update the library with file changes"), NULL },
		{ "no-registration",		'n',POPT_ARG_NONE,	&no_registration,		0, N_("Do not register the shell"), NULL },
		{ "dry-run",			0,  POPT_ARG_NONE,	&dry_run,			0, N_("Don't save any data permanently (implies --no-registration)"), NULL },
		{ "rhythmdb-file",		0,  POPT_ARG_STRING,	&rhythmdb_file,			0, N_("Path for database file to use"), NULL },
		{ "quit",			'q',POPT_ARG_NONE,	&quit,				0, N_("Quit Rhythmbox"), NULL },
#ifdef HAVE_GSTREAMER_0_8
		{NULL, '\0', POPT_ARG_INCLUDE_TABLE, NULL, 0, "GStreamer", NULL},
#endif
		POPT_TABLEEND
	};

	/* Disable event sounds for now by passing "--disable-sound" to libgnomeui.
	 * See: http://bugzilla.gnome.org/show_bug.cgi?id=119222 */
	new_argv = g_strdupv (argv);
	new_argv = g_realloc (new_argv, (argc+2)*sizeof(char*));
	new_argv[argc] = g_strdup ("--disable-sound");
	new_argv[argc+1] = NULL;

#ifdef HAVE_GSTREAMER_0_8
	popt_options[(sizeof(popt_options)/sizeof(popt_options[0]))-2].arg
		= (void *) gst_init_get_popt_table ();
#elif HAVE_GSTREAMER_0_10
        /* To pass options to GStreamer in 0.10, RB needs to use GOption, for
         * now, GStreamer can live without them (people can still use env
         * vars, after all) */
	rb_profile_start ("initializing gstreamer");
        gst_init (NULL, NULL);
	rb_profile_end ("initializing gstreamer");
#endif

	gtk_set_locale ();
	rb_profile_start ("initializing gnome program");
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE, argc+1, new_argv,
				      GNOME_PARAM_POPT_TABLE, popt_options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);
	rb_profile_end ("initializing gnome program");

	g_object_get_property (G_OBJECT (program),
                               GNOME_PARAM_POPT_CONTEXT,
                               g_value_init (&context_as_value, G_TYPE_POINTER));
        poptContext = g_value_get_pointer (&context_as_value);

	rb_profile_start ("initializing gnome auth manager");
	gnome_authentication_manager_init ();
	rb_profile_end ("initializing gnome auth manager");

	g_random_set_seed (time(0));

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

#ifdef HAVE_GSTREAMER_0_8
	gst_control_init (&argc, &argv);
#endif

	if (!debug && debug_match)
		rb_debug_init_match (debug_match);
	else
		rb_debug_init (debug);
	rb_debug ("initializing Rhythmbox %s", VERSION);

	/* TODO: kill this function */
	rb_threads_init ();

	activated = FALSE;

#if WITH_DBUS || WITH_OLD_DBUS
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

#ifndef WITH_OLD_DBUS
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

#else
		DBusError dbus_error = {0,};
		DBusConnection *connection;

		connection = dbus_g_connection_get_connection (session_bus);
		request_name_reply = dbus_bus_request_name (connection,
			       				    "org.gnome.Rhythmbox",
							    flags,
							    &dbus_error);
		if (dbus_error_is_set (&dbus_error)) {
			g_warning ("Failed to invoke RequestName: %s",
				   dbus_error.message);
		}
		dbus_error_free (&dbus_error);
#endif
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

#endif

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

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
		nautilus_burn_init ();
#endif

		gtk_window_set_default_icon_name ("rhythmbox");

		rb_shell = rb_shell_new (argc, argv, no_registration, no_update, dry_run, rhythmdb_file);
		g_object_weak_ref (G_OBJECT (rb_shell), main_shell_weak_ref_cb, NULL);
		if (!no_registration && session_bus != NULL) {
#if WITH_DBUS
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
#elif WITH_OLD_DBUS
			register_dbus_handler (dbus_g_connection_get_connection (session_bus),
					       rb_shell);
#endif /* WITH_DBUS */
		}
	} else if (!no_registration && session_bus != NULL) {
#if WITH_DBUS
		DBusGProxy *shell_proxy;
#endif
		guint32 current_time;
#if GTK_MINOR_VERSION >= 8
		current_time = gdk_x11_display_get_user_time (gdk_display_get_default ());
#else
		/* FIXME - this does not work; it will return 0 since
		 * we're not in an event.  When we pass this to
		 * gtk_window_present_with_time, it ignores the value
		 * since it's 0.  The only alternative is to parse the
		 * startup-notification junk from the environment
		 * ourself...
		 */
		current_time = GDK_CURRENT_TIME;
#endif	/* GTK_MINOR_VERSION */

#if WITH_DBUS
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
				load_uri_args (poptGetArgs (poptContext), (GFunc) dbus_load_uri, shell_proxy);
				dbus_g_proxy_call_no_reply (shell_proxy, "present",
							    G_TYPE_UINT, current_time,
							    G_TYPE_INVALID);
			}
			g_object_unref (G_OBJECT (shell_proxy));
		}
#elif WITH_OLD_DBUS
		if (!send_present_message (dbus_g_connection_get_connection (session_bus),
					   current_time))
			g_warning ("Unable to send dbus message to existing rhythmbox instance");
#endif /* WITH_DBUS */
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

#if NAUTILUS_BURN_CHECK_VERSION(2,15,3)
		nautilus_burn_shutdown ();
#endif
	}

	g_strfreev (new_argv);

	rb_debug ("THE END");
	rb_profile_end ("starting rhythmbox");
	exit (0);
}

#if WITH_DBUS
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
#endif

static void
main_shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	rb_debug ("caught shell finalization");
	gtk_main_quit ();
}

#ifdef WITH_OLD_DBUS
/* old dbus support (0.31-0.34) */

static gboolean
send_present_message (DBusConnection *connection, guint32 current_time)
{
	DBusMessage *message;
	gboolean result;

	message = dbus_message_new_method_call ("org.gnome.Rhythmbox",
						"/org/gnome/Rhythmbox/Shell",
						"org.gnome.Rhythmbox.Shell",
						"present");
	if (!message)
		return FALSE;

	if (!dbus_message_append_args (message,
				       DBUS_TYPE_UINT32, &current_time,
				       DBUS_TYPE_INVALID)) {
		dbus_message_unref (message);
		return FALSE;
	}

	result = dbus_connection_send (connection, message, NULL);
	dbus_message_unref (message);

	return result;
}

static void
unregister_dbus_handler (DBusConnection *connection, void *data)
{
	/* nothing */
}

static DBusHandlerResult
handle_dbus_message (DBusConnection *connection, DBusMessage *message, void *data)
{
	RBShell *shell = (RBShell *)data;

	rb_debug ("Handling dbus message");
	if (dbus_message_is_method_call (message, "org.gnome.Rhythmbox.Shell", "present")) {
		DBusMessageIter iter;
		guint32 current_time;

		if (!dbus_message_iter_init (message, &iter))
			return DBUS_HANDLER_RESULT_NEED_MEMORY;

		if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_UINT32)
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		dbus_message_iter_get_basic (&iter, &current_time);

		rb_shell_present (shell, current_time, NULL);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
}

static void
register_dbus_handler (DBusConnection *connection, RBShell *shell)
{
	DBusObjectPathVTable vt = {
		unregister_dbus_handler,
		handle_dbus_message,
		NULL, NULL, NULL, NULL
	};

	dbus_connection_register_object_path (connection,
					      "/org/gnome/Rhythmbox/Shell",
					      &vt,
					      shell);
	dbus_connection_ref (connection);
	dbus_connection_setup_with_g_main (connection, NULL);
}

#endif
