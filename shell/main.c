/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  arch-tag: The Rhythmbox main entrypoint
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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
#include <bonobo/Bonobo.h>
#include <bonobo/bonobo-main.h>
#include <libbonobo.h>
#include <bonobo/bonobo-property-bag-client.h>
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

#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

static gboolean rb_init (RBShell *shell);
static void rb_handle_cmdline (char **argv, int argc,
			       gboolean already_running);

static CORBA_Environment ev;

static gboolean debug           = FALSE;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;
static gboolean no_update	= FALSE;
static gboolean dry_run		= FALSE;
static char *rhythmdb_file = NULL;
static gboolean print_playing = FALSE;
static gboolean print_playing_path = FALSE;
static gboolean playpause       = FALSE;
static gboolean focus           = FALSE;
static gboolean previous        = FALSE;
static gboolean next            = FALSE;
static gboolean volume_up       = FALSE;
static gboolean volume_down     = FALSE;
static gboolean toggle_mute     = FALSE;
static gboolean rate_up         = FALSE;
static gboolean rate_down       = FALSE;
static gboolean shuffle         = FALSE;
static gboolean print_play_time = FALSE;
static gboolean print_song_length = FALSE;
static long seek_time           = 0;

int
main (int argc, char **argv)
{
	GnomeProgram *program;
	CORBA_Object object;
	RBShell *rb_shell;
	char **new_argv;

	struct poptOption popt_options[] =
	{
		{ "print-playing",	0,  POPT_ARG_NONE,          &print_playing,                                  0, N_("Print the playing song and exit"),     NULL },
		{ "print-playing-path",	0,  POPT_ARG_NONE,          &print_playing_path,                          0, N_("Print the playing song URI and exit"),     NULL },
        
        { "print-song-length",	0,  POPT_ARG_NONE,			&print_song_length,		0, N_("Print the playing song length in seconds and exit"),	NULL },
        { "print-play-time", 	0,  POPT_ARG_NONE,			&print_play_time,		0, N_("Print the current elapsed time of playing song and exit"),	NULL },
        { "set-play-time", 	    0,  POPT_ARG_LONG,			&seek_time,				0, N_("Seek to the specified time in playing song if possible and exit"),	NULL },
        
		{ "play-pause",		    0,  POPT_ARG_NONE,			&playpause,	        	0, N_("Toggle play/pause mode"),     NULL },
		{ "focus",	    0,  POPT_ARG_NONE,			&focus,	        	0, N_("Focus the running player"),     NULL },
		{ "previous",		    0,  POPT_ARG_NONE,			&previous,	        	0, N_("Jump to previous song"),     NULL },
		{ "next",		        0,  POPT_ARG_NONE,			&next,		        	0, N_("Jump to next song"),     NULL },
		{ "volume-up",		        0,  POPT_ARG_NONE,			&volume_up,		        	0, N_("Turn up volume"),     NULL },
		{ "volume-down",       	        0,  POPT_ARG_NONE,			&volume_down,		        	0, N_("Turn down volume"),     NULL },
		{ "toggle-mute",		0,  POPT_ARG_NONE,			&toggle_mute,		        	0, N_("Toggle muting"),     NULL },
		{ "rate-up",		        0,  POPT_ARG_NONE,			&rate_up,		        	0, N_("Increase rating"),     NULL },
		{ "rate-down",       	        0,  POPT_ARG_NONE,			&rate_down,		        	0, N_("Decrease rating"),     NULL },
		
		{ "shuffle",		        0,  POPT_ARG_NONE,			&shuffle,		        	0, N_("Toggle shuffling"),     NULL },

		{ "debug",           'd',  POPT_ARG_NONE,          &debug,                                        0, N_("Enable debugging code"),     NULL },
		{ "no-update", 0,  POPT_ARG_NONE,          &no_update,                              0, N_("Do not update the library"), NULL },
		{ "no-registration", 'n',  POPT_ARG_NONE,          &no_registration,                              0, N_("Do not register the shell"), NULL },
		{ "dry-run", 0,  POPT_ARG_NONE,          &dry_run,                             0, N_("Don't save any data permanently (implies --no-registration)"), NULL },
		{ "rhythmdb-file", 0,  POPT_ARG_STRING,          &rhythmdb_file,                             0, N_("Path for database file to use"), NULL },
		{ "quit",            'q',  POPT_ARG_NONE,          &quit,                                         0, N_("Quit Rhythmbox"),            NULL },
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

	gtk_set_locale ();
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE, argc+1, new_argv,
				      GNOME_PARAM_POPT_TABLE, popt_options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);

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
	
	gdk_threads_init ();

	CORBA_exception_init (&ev);

	rb_debug ("initializing Rhythmbox %s", VERSION);

	rb_file_helpers_init ();
	rb_string_helpers_init ();

	if (no_registration == FALSE && dry_run == FALSE) {
		object = bonobo_activation_activate_from_id (RB_SHELL_OAFIID,
			 				     Bonobo_ACTIVATION_FLAG_EXISTING_ONLY,
							     NULL, NULL);
	}
	else
		object = NULL;

	if (object == NULL) {
		rb_debug ("Going to create a new shell");

		glade_gnome_init ();

		rb_stock_icons_init ();

		rb_shell = rb_shell_new (argc, argv, no_registration,
					 no_update, dry_run, rhythmdb_file);
		
		g_idle_add ((GSourceFunc) rb_init, rb_shell);
		
		bonobo_main ();
	} else {
		rb_debug ("already running");

		rb_handle_cmdline (argv, argc, TRUE);
	}

	/* cleanup */
	CORBA_exception_free (&ev);

#ifdef WITH_MONKEYMEDIA
	monkey_media_shutdown ();
#endif
	rb_file_helpers_shutdown ();
	rb_string_helpers_shutdown ();
	if (object == NULL)
		rb_stock_icons_shutdown ();

	g_strfreev (new_argv);

	return 0;
}

static gboolean
rb_init (RBShell *shell)
{
	char **argv;
	int argc;

	GDK_THREADS_ENTER ();
	
	rb_shell_construct (shell);

	g_object_get (G_OBJECT (shell), "argc", &argc, "argv", &argv, NULL);

	if (!no_registration && !dry_run)
		rb_handle_cmdline (argv, argc, FALSE);
	
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static GNOME_Rhythmbox_SongInfo*
get_song_info (GNOME_Rhythmbox shell)
{
	Bonobo_PropertyBag pb;
	CORBA_any *any;
	GNOME_Rhythmbox_SongInfo *song_info = NULL;

	pb = GNOME_Rhythmbox_getPlayerProperties (shell, &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning (_("An exception occured '%s'"), err);
		return NULL;
	}

	any = bonobo_pbclient_get_value (pb, "song", 
					 TC_GNOME_Rhythmbox_SongInfo, 
					 &ev);
	if (BONOBO_EX (&ev)) {
		char *err = bonobo_exception_get_text (&ev);
		g_warning (_("An exception occured '%s'"), err);
		g_free (err);
		bonobo_object_release_unref ((Bonobo_Unknown) pb, &ev);
		return NULL;
	}
	
	if ((any == NULL) || (!CORBA_TypeCode_equivalent (any->_type, TC_GNOME_Rhythmbox_SongInfo, NULL))) {
		song_info = NULL;
	} else {
		song_info = (GNOME_Rhythmbox_SongInfo*)any->_value;
		any->_release = FALSE;
		CORBA_free (any);
	}

	bonobo_object_release_unref ((Bonobo_Unknown) pb, &ev);
	return song_info;
}

static void
rb_toggle_shuffle (GNOME_Rhythmbox shell)
{
	Bonobo_PropertyBag pb;
	gboolean shuffle;
		
	pb = GNOME_Rhythmbox_getPlayerProperties (shell, &ev);

	g_return_if_fail (!BONOBO_EX (&ev));

	shuffle = bonobo_pbclient_get_boolean (pb,
					       "shuffle",
					       &ev);
	g_return_if_fail (!BONOBO_EX (&ev));

		
	bonobo_pbclient_set_boolean (pb,
				     "shuffle",
				     shuffle ? FALSE : TRUE,
				     &ev);
	g_return_if_fail (!BONOBO_EX (&ev));

	bonobo_object_release_unref ((Bonobo_Unknown)pb, &ev);
}


static void
rb_handle_cmdline (char **argv, int argc,
		   gboolean already_running)
{
	GNOME_Rhythmbox shell;
	int i;
	gboolean grab_focus = TRUE;
	
	/*
	 * All 'remote control' type actions should set grab_focus 
	 * to false.  There are two ways to focus the window.  
	 * Running 'rhythmbox' with no arguments when it is already
	 * running.  Explicitly adding the --focus argument combined 
	 * with other 'remote control' arguments.
	 */

	shell = bonobo_activation_activate_from_id (RB_SHELL_OAFIID, 0, NULL, &ev);
	if (shell == NULL)
	{
		char *msg = rb_shell_corba_exception_to_string (&ev);
		rb_warning_dialog (_("Failed to activate the shell:\n%s"), msg);
		g_free (msg);
		
		return;
	}

	if (rate_up)
	{
		GNOME_Rhythmbox_SongInfo *song_info;

		song_info = get_song_info (shell);
		if (song_info != NULL) {
			GNOME_Rhythmbox_setRating (shell, song_info->rating + 1, &ev);
			CORBA_free (song_info);
		}
		grab_focus = FALSE;
	}

	if (rate_down)
	{
		GNOME_Rhythmbox_SongInfo *song_info;

		song_info = get_song_info (shell);
		if (song_info != NULL) {
			GNOME_Rhythmbox_setRating (shell, song_info->rating - 1, &ev);
			CORBA_free (song_info);
		}
		grab_focus = FALSE;
	}

        if (volume_up) {
                GNOME_Rhythmbox_volumeUp (shell, &ev);
		grab_focus = FALSE;
        }

        if (volume_down) {
                GNOME_Rhythmbox_volumeDown (shell, &ev);
		grab_focus = FALSE;
        }

        if (toggle_mute) {
                GNOME_Rhythmbox_toggleMute (shell, &ev);
		grab_focus = FALSE;
        }

	if (print_playing)
	{
		GNOME_Rhythmbox_SongInfo *song_info;

		song_info = get_song_info (shell);
		if (song_info == NULL) {
			g_print ("\n");
		} else {
			g_print ("%s\n", song_info->title);
			CORBA_free (song_info);
		}
		grab_focus = FALSE;
	}
	
	if (print_playing_path)
	{
		GNOME_Rhythmbox_SongInfo *song_info;

		song_info = get_song_info (shell);
		if (song_info == NULL) {
			g_print ("\n");
		} else {
			g_print ("%s\n", song_info->path);
			CORBA_free (song_info);
		}
		grab_focus = FALSE;
	}
	
	if (print_song_length)
	{
		GNOME_Rhythmbox_SongInfo *song_info;
		song_info = get_song_info (shell);
		if (song_info == NULL) {
			g_print ("-1\n");
		} else {
			g_print ("%d\n", song_info->duration);
			CORBA_free (song_info);
		}
		grab_focus = FALSE;
	}
	
	if (print_play_time)
	{
		long play_time = GNOME_Rhythmbox_getPlayingTime (shell, &ev);
		printf ("%ld\n", play_time);
		grab_focus = FALSE;
	}
	
	if (seek_time > 0)
	{
		GNOME_Rhythmbox_setPlayingTime (shell, seek_time, &ev);
		grab_focus = FALSE;
	}
	
	if (playpause)
	{
		GNOME_Rhythmbox_playPause (shell, &ev);
		grab_focus = FALSE;
	}

	if (previous)
	{
		GNOME_Rhythmbox_previous (shell, &ev);
		grab_focus = FALSE;
	}

	if (next)
	{
		GNOME_Rhythmbox_next (shell, &ev);
		grab_focus = FALSE;
	}

	if (shuffle)
	{
		rb_toggle_shuffle (shell);
	}

	for (i = 1; i < argc; i++)
	{
		char *tmp;

		tmp = rb_uri_resolve_relative (argv[i]);
			
		if (rb_uri_exists (tmp) == TRUE) {
			GError *error = NULL;
			char *utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, &error);
			if (!utf8 && error) {
				g_error (error->message);
				continue;
			}
 			GNOME_Rhythmbox_handleFile (shell, utf8, &ev);
			g_free (utf8);
		}
		
		g_free (tmp);
		grab_focus = FALSE;
	}
	
	if (quit == TRUE)
	{
		GNOME_Rhythmbox_quit (shell, &ev);
	}

	if (focus)
		grab_focus = TRUE;

	if (already_running) {
		gdk_notify_startup_complete ();
	}

	/* at the very least, we focus the window */
	if (already_running == TRUE && grab_focus == TRUE)
		GNOME_Rhythmbox_grabFocus (shell, &ev);
}
