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
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-string-helpers.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"

static gboolean debug           = FALSE;
static gboolean quit            = FALSE;
static gboolean no_update	= FALSE;
static gboolean dry_run		= FALSE;
static char *rhythmdb_file = NULL;
static gboolean print_playing = FALSE;
static gboolean print_playing_path = FALSE;
static gboolean playpause       = FALSE;
static gboolean focus           = FALSE;
static gboolean previous        = FALSE;
static gboolean next            = FALSE;
static gboolean shuffle         = FALSE;
static gboolean print_play_time = FALSE;
static gboolean print_song_length = FALSE;
static long seek_time           = 0;

int
main (int argc, char **argv)
{
	GnomeProgram *program;
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
		
		{ "shuffle",		        0,  POPT_ARG_NONE,			&shuffle,		        	0, N_("Toggle shuffling"),     NULL },

		{ "debug",           'd',  POPT_ARG_NONE,          &debug,                                        0, N_("Enable debugging code"),     NULL },
		{ "no-update", 0,  POPT_ARG_NONE,          &no_update,                              0, N_("Do not update the library"), NULL },
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

	rb_refstring_system_init ();
	rb_debug_init (debug);
	
	gdk_threads_init ();

#ifdef WITH_RHYTHMDB_GDA
 	gda_init (PACKAGE, VERSION, argc, argv);
#endif
 	
	rb_debug ("initializing Rhythmbox %s", VERSION);

	rb_file_helpers_init ();
	rb_string_helpers_init ();

	rb_debug ("Going to create a new shell");
	
	glade_gnome_init ();
	
	rb_stock_icons_init ();
	
	rb_shell = rb_shell_new (argc, argv, TRUE,
				 no_update, dry_run, rhythmdb_file);
	rb_shell_construct (rb_shell);
	
	gtk_main ();


#ifdef WITH_MONKEYMEDIA
	monkey_media_shutdown ();
#endif
	rb_file_helpers_shutdown ();
	rb_string_helpers_shutdown ();
	rb_refstring_system_shutdown ();
	rb_stock_icons_shutdown ();

	g_strfreev (new_argv);

	return 0;
}
