/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <libintl.h>
#include <locale.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-ui-init.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h> /* For _get_user_time... */
#include <glade/glade-init.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/gconf/gconf.h>
#include <gst/control/control.h>
#endif
#ifdef WITH_MONKEYMEDIA
#include "monkey-media.h"
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
#include "rb-string-helpers.h"
#include "rb-stock-icons.h"
#include "rb-util.h"
#include "eel-gconf-extensions.h"
#include "rb-util.h"


#ifdef WITH_BONOBO
#include "rb-remote-client-proxy.h"
#include <bonobo/bonobo-main.h>
#include "bonobo/rb-remote-bonobo.h"
#endif
#ifdef WITH_DBUS
#include <dbus/dbus-glib.h>
#include "rb-shell-glue.h"
#include "rb-shell-player-glue.h"
#endif

static gboolean debug           = FALSE;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;
static gboolean no_update	= FALSE;
static gboolean dry_run		= FALSE;
static char *rhythmdb_file = NULL;
static gboolean load_uri_args (const char **args, GFunc handler, gpointer user_data);

#ifdef WITH_BONOBO
static gboolean print_playing = FALSE;
static gboolean print_playing_artist = FALSE;
static gboolean print_playing_album = FALSE;
static gboolean print_playing_track = FALSE;
static gboolean print_playing_genre = FALSE;
static gboolean print_playing_path = FALSE;
static gboolean playpause       = FALSE;
static gboolean play            = FALSE;
static gboolean do_pause        = FALSE;
static gboolean focus           = FALSE;
static gboolean previous        = FALSE;
static gboolean next            = FALSE;
static gboolean shuffle         = FALSE;
static gboolean repeat          = FALSE;
static gboolean print_play_time = FALSE;
static gboolean print_song_length = FALSE;
static long seek_time           = 0;
static long seek_relative       = 0;
static double set_rating          = -1.0;
static float set_volume         = -1.0;
static gboolean toggle_mute     = FALSE;
static gboolean toggle_hide     = FALSE;

static void handle_cmdline (RBRemoteClientProxy *proxy, gboolean activated,
			    const char **args);
#endif
#ifdef WITH_DBUS
static void dbus_load_uri (const char *filename, DBusGProxy *proxy);
#endif

static void main_shell_weak_ref_cb (gpointer data, GObject *objptr);

int
main (int argc, char **argv)
{
	GnomeProgram *program;
#ifdef WITH_DBUS
	DBusGConnection *session_bus;
#endif
	RBShell *rb_shell;
	char **new_argv;
	gboolean activated;
	poptContext poptContext;
        GValue context_as_value = { 0 };
#ifdef WITH_BONOBO
	RBRemoteClientProxy *client_proxy;
	RBRemoteBonobo *bonobo;
#endif
#if WITH_DBUS || WITH_BONOBO
	GError *error = NULL;
#endif

	struct poptOption popt_options[] =
	{
		/* These are all legacy; for D-BUS just use dbus-send or Python scripts */ 
#ifdef WITH_BONOBO
		{ "print-playing",		0,  POPT_ARG_NONE,	&print_playing,			0, N_("Print the playing song and exit"), NULL },
		{ "print-playing-artist",	0,  POPT_ARG_NONE,	&print_playing_artist,		0, N_("Print the playing song artist and exit"), NULL },
		{ "print-playing-album",	0,  POPT_ARG_NONE,	&print_playing_album,		0, N_("Print the playing song album and exit"), NULL },
		{ "print-playing-track",	0,  POPT_ARG_NONE,	&print_playing_track,		0, N_("Print the playing song track and exit"), NULL },
		{ "print-playing-genre",	0,  POPT_ARG_NONE,	&print_playing_genre,		0, N_("Print the playing song genre and exit"), NULL },
		{ "print-playing-path",		0,  POPT_ARG_NONE,	&print_playing_path,		0, N_("Print the playing song URI and exit"), NULL },
        
		{ "print-song-length",		0,  POPT_ARG_NONE,	&print_song_length,		0, N_("Print the playing song length in seconds and exit"), NULL },
		{ "print-play-time",		0,  POPT_ARG_NONE,	&print_play_time,		0, N_("Print the current elapsed time of playing song and exit"), NULL },
		{ "set-play-time",		0,  POPT_ARG_LONG,	&seek_time,			0, N_("Seek to the specified time in playing song if possible and exit"), NULL },
		{ "seek",			0,  POPT_ARG_LONG,	&seek_relative,			0, N_("Seek by the specified amount if possible and exit"), NULL },
		{ "set-rating",			0,  POPT_ARG_DOUBLE,	&set_rating,			0, N_("Set the rating of the currently playing song and exit"), NULL },
        
		{ "play-pause",			0,  POPT_ARG_NONE,	&playpause,			0, N_("Toggle play/pause mode"), NULL },
		{ "pause",			0,  POPT_ARG_NONE,	&do_pause,			0, N_("Pause playback if currently playing"), NULL },
		{ "play",			0,  POPT_ARG_NONE,	&play,				0, N_("Resume playback if currently paused"), NULL },
		{ "focus",			0,  POPT_ARG_NONE,	&focus,				0, N_("Focus the running player"), NULL },
		{ "previous",			0,  POPT_ARG_NONE,	&previous,			0, N_("Jump to previous song"), NULL },
		{ "next",			0,  POPT_ARG_NONE,	&next,				0, N_("Jump to next song"), NULL },
		
		{ "shuffle",			0,  POPT_ARG_NONE,	&shuffle,			0, N_("Toggle shuffling"), NULL },
		{ "repeat",			0,  POPT_ARG_NONE,	&repeat,			0, N_("Toggle repeat"), NULL },

		{ "set-volume",			0,  POPT_ARG_FLOAT,	&set_volume,			0, N_("Set the volume level"), NULL },
		{ "toggle-mute",		0,  POPT_ARG_NONE,	&toggle_mute,			0, N_("Mute or unmute playback"), NULL },
		{ "toggle-hide",		0,  POPT_ARG_NONE,	&toggle_hide,			0, N_("Change visibility of the main Rhythmbox window"), NULL },
#endif

		{ "debug",			'd',POPT_ARG_NONE,	&debug,				0, N_("Enable debugging code"), NULL },
		{ "no-update",			0,  POPT_ARG_NONE,	&no_update,			0, N_("Do not update the library"), NULL },
		{ "no-registration",		'n',POPT_ARG_NONE,	&no_registration,		0, N_("Do not register the shell"), NULL },
		{ "dry-run",			0,  POPT_ARG_NONE,	&dry_run,			0, N_("Don't save any data permanently (implies --no-registration)"), NULL },
		{ "rhythmdb-file",		0,  POPT_ARG_STRING,	&rhythmdb_file,			0, N_("Path for database file to use"), NULL },
		{ "quit",			'q',POPT_ARG_NONE,	&quit,				0, N_("Quit Rhythmbox"), NULL },
#ifdef HAVE_GSTREAMER
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

#ifdef HAVE_GSTREAMER	
	popt_options[(sizeof(popt_options)/sizeof(popt_options[0]))-2].arg
		= (void *) gst_init_get_popt_table ();
#endif
#ifdef WITH_BONOBO
	rb_remote_bonobo_preinit ();
#endif

	gtk_set_locale ();
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE, argc+1, new_argv,
				      GNOME_PARAM_POPT_TABLE, popt_options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);
	g_object_get_property (G_OBJECT (program),
                               GNOME_PARAM_POPT_CONTEXT,
                               g_value_init (&context_as_value, G_TYPE_POINTER));
        poptContext = g_value_get_pointer (&context_as_value);

	/* Disabled because it breaks internet radio and other things -
	 * doing synchronous calls in the main thread causes deadlock.
	 * This is too hard to fix right now, so we punt.
	 */
#if 0
	gnome_authentication_manager_init ();
#endif

	g_random_set_seed (time(0));

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

#ifdef HAVE_GSTREAMER	
	gst_control_init (&argc, &argv);
#endif
#ifdef WITH_MONKEYMEDIA
	monkey_media_init (&argc, &argv);
#endif

	rb_debug_init (debug);
	rb_debug ("initializing Rhythmbox %s", VERSION);
	
	/* TODO: kill this function */
	rb_threads_init ();

	activated = FALSE;

#ifdef WITH_BONOBO
	rb_debug ("going to create Bonobo object");
	bonobo = rb_remote_bonobo_new ();
	client_proxy = NULL;
	if (!no_registration) {
		if ((activated = rb_remote_bonobo_activate (bonobo))) {
			rb_debug ("successfully activated Bonobo");
			client_proxy = RB_REMOTE_CLIENT_PROXY (bonobo);
		}
	}
#endif
#ifdef WITH_DBUS
	rb_debug ("going to create DBus object");

	dbus_g_thread_init ();

	session_bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (session_bus == NULL) {
		g_critical ("couldn't connect to session bus: %s", error->message);
		g_clear_error (&error);
	} else {
		guint request_name_reply;
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
					DBUS_NAME_FLAG_PROHIBIT_REPLACEMENT,
					G_TYPE_INVALID,
					G_TYPE_UINT,
					&request_name_reply,
					G_TYPE_INVALID)) {
			g_critical ("Failed to invoke RequestName: %s",
				    error->message);
		}
		g_object_unref (bus_proxy);

		if (request_name_reply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
		    || request_name_reply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER)
			activated = FALSE;
		else if (request_name_reply == DBUS_REQUEST_NAME_REPLY_EXISTS)
			activated = TRUE;
		else {
			g_critical ("Got unhandled reply %u from RequestName",
				    request_name_reply);
			activated = FALSE;
		}
	}
		
#endif

	if (!activated) {
#ifdef WITH_RHYTHMDB_GDA
		gda_init (PACKAGE, VERSION, argc, argv);
#endif
 	
		rb_refstring_system_init ();

		rb_file_helpers_init ();
		rb_string_helpers_init ();

		rb_debug ("Going to create a new shell");
	
		glade_gnome_init ();
	
		rb_stock_icons_init ();
	
		rb_shell = rb_shell_new (argc, argv, TRUE, no_update, dry_run, rhythmdb_file);
		g_object_weak_ref (G_OBJECT (rb_shell), main_shell_weak_ref_cb, NULL);
#ifdef WITH_BONOBO
		rb_remote_bonobo_acquire (bonobo, RB_REMOTE_PROXY (rb_shell), &error);
		if (error) {
			g_warning ("error: %s", error->message);
		} else {
			if (rb_remote_bonobo_activate (bonobo))
				client_proxy = RB_REMOTE_CLIENT_PROXY (bonobo);
			else
				g_critical ("acquired bonobo service but couldn't activate!");
		}

#endif
#ifdef WITH_DBUS
		if (session_bus != NULL) {
			GObject *player;
			const char *path;

			dbus_g_object_type_install_info (RB_TYPE_SHELL, &dbus_glib_rb_shell_object_info);
			
			dbus_g_connection_register_g_object (session_bus, "/org/gnome/Rhythmbox/Shell", G_OBJECT (rb_shell));
			dbus_g_object_type_install_info (RB_TYPE_SHELL_PLAYER, &dbus_glib_rb_shell_player_object_info);
			player = rb_shell_get_player (rb_shell);
			path = rb_shell_get_player_path (rb_shell);
			dbus_g_connection_register_g_object (session_bus, path, G_OBJECT (player));
		}
#endif
	} else {
#ifdef WITH_DBUS
		DBusGProxy *shell_proxy;

		shell_proxy = dbus_g_proxy_new_for_name_owner (session_bus,
							       "org.gnome.Rhythmbox",
							       "/org/gnome/Rhythmbox/Shell",
							       "org.gnome.Rhythmbox.Shell",
							       &error);
		if (!shell_proxy) {
			g_critical ("Couldn't create proxy for Rhythmbox shell: %s",
				    error->message);
		} else {
			load_uri_args (poptGetArgs (poptContext), (GFunc) dbus_load_uri, shell_proxy);
		}
		dbus_g_proxy_call_no_reply (shell_proxy, "present",
					    G_TYPE_UINT, gdk_x11_display_get_user_time (gdk_display_get_default ()),
					    G_TYPE_INVALID);
#endif
	}
#ifdef WITH_BONOBO	
	handle_cmdline (client_proxy, activated, poptGetArgs (poptContext));
#endif

	if (activated) {
		gdk_notify_startup_complete ();
	} else {

#ifdef WITH_BONOBO
                /* Unfortunately Bonobo takes over the main loop ... */
		bonobo_main ();
#else
		gtk_main ();
#endif

		rb_debug ("out of toplevel loop");

		rb_file_helpers_shutdown ();
		rb_string_helpers_shutdown ();
		rb_stock_icons_shutdown ();
		rb_refstring_system_shutdown ();

		gnome_vfs_shutdown ();
	}

	g_strfreev (new_argv);

	rb_debug ("THE END");
	exit (0);
}

static gboolean
load_uri_args (const char **args, GFunc handler, gpointer user_data)
{
	gboolean handled;
	guint i;
	GError *error = NULL;

	handled = FALSE;
	for (i = 0; args && args[i]; i++) {
		char *tmp;

		rb_debug ("examining argument %s", args[i]);
		tmp = rb_uri_resolve_relative (args[i]);
			
		if (rb_uri_exists (tmp) == TRUE) {
			char *utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, &error);
			if (!utf8 && error) {
				g_error (error->message);
				continue;
			}
			handler (utf8, user_data);
			g_free (utf8);
		}
		
		g_free (tmp);
		handled = TRUE;
	}
	return handled;
}

#ifdef WITH_DBUS
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

#ifdef WITH_BONOBO

static void
bonobo_load_uri (const char *filename, RBRemoteClientProxy *proxy)
{
	rb_remote_client_proxy_handle_uri (proxy, filename);
}

static void
handle_cmdline (RBRemoteClientProxy *proxy, gboolean activated, const char **args)
{
	gboolean grab_focus;
	RBRemoteSong *song;

	grab_focus = TRUE;

	rb_debug ("handling command line");

	song = NULL;
	if (print_playing
	    || print_playing_artist
	    || print_playing_album
	    || print_playing_track
	    || print_playing_genre
	    || print_playing_path
	    || print_song_length ) {
		rb_debug ("retrieving playing song");
		song = rb_remote_client_proxy_get_playing_song (proxy);
		grab_focus = FALSE;
	}

	if (print_playing)
		g_print ("%s\n", song ? song->title : "");

	if (print_playing_artist)
		g_print ("%s\n", song ? song->artist : "");

	if (print_playing_album)
		g_print ("%s\n", song ? song->album : "");

	if (print_playing_track)
		g_print ("%ld\n", song ? song->track_number : -1);

	if (print_playing_genre)
		g_print ("%s\n", song ? song->genre : "");
	
	if (print_playing_path)
		g_print ("%s\n", song ? song->uri : "");
	
	if (print_song_length)
		g_print ("%ld\n", song ? song->duration : -1);
	
	if (print_play_time
	    || seek_time > 0
	    || playpause
	    || play
	    || do_pause
	    || previous
	    || next
	    || shuffle)
		grab_focus = FALSE;

	if (print_play_time)
		printf ("%ld\n", rb_remote_client_proxy_get_playing_time (proxy));

	if (set_rating != -1)
		rb_remote_client_proxy_set_rating (proxy, set_rating);
	
	if (seek_time > 0)
		rb_remote_client_proxy_set_playing_time (proxy, seek_time);

	if (seek_relative != 0)
		rb_remote_client_proxy_seek (proxy, seek_relative);
	
	if (play)
		rb_remote_client_proxy_play (proxy);
	else if (do_pause)
		rb_remote_client_proxy_pause (proxy);
	else if (playpause)
		rb_remote_client_proxy_toggle_playing (proxy);

	if (previous)
                rb_remote_client_proxy_jump_previous (proxy);

	if (next)
                rb_remote_client_proxy_jump_next (proxy);

	if (shuffle)
		rb_remote_client_proxy_toggle_shuffle (proxy);

	if (repeat)
		rb_remote_client_proxy_toggle_repeat (proxy);

	if (set_volume > -0.0001) {
		if (set_volume > 1.0)
			set_volume = 1.0;
		else if (set_volume < 0.0)
			set_volume = 0.0;
		rb_remote_client_proxy_set_volume (proxy, set_volume);
	}

	if (toggle_mute)
		rb_remote_client_proxy_toggle_mute (proxy);

	if (load_uri_args (args, (GFunc) bonobo_load_uri, proxy))
		grab_focus = FALSE;

	if (quit)
		rb_remote_client_proxy_quit (proxy);

	if (focus)
		grab_focus = TRUE;

	if (toggle_hide)
		rb_remote_client_proxy_toggle_visibility (proxy);

	/* at the very least, we focus the window */
	if (activated && grab_focus) {
		rb_debug ("grabbing focus");
		rb_remote_client_proxy_grab_focus (proxy);
	}
}
#endif

static void
main_shell_weak_ref_cb (gpointer data, GObject *objptr)
{
	rb_debug ("caught shell finalization");
#ifdef WITH_BONOBO	
	bonobo_main_quit ();
#else
	gtk_main_quit ();
#endif
}
