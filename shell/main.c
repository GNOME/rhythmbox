/*
 *  Copyright (C) 2002 Jorn Baayen
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
 *  $Id$
 */

#include <config.h>
#include <libintl.h>
#include <locale.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-ui-init.h>
#include <gtk/gtk.h>
#include <bonobo/bonobo-main.h>
#include <glade/glade-init.h>
#include <monkey-media.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-stock-icons.h"

static gboolean rb_init (RBShell *shell);
static void rb_handle_cmdline (char **argv, int argc,
			       gboolean already_running);

static CORBA_Environment ev;

static gboolean debug           = FALSE;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;
static gboolean print_playing = FALSE;
static gboolean print_playing_path = FALSE;

int
main (int argc, char **argv)
{
	GnomeProgram *program;
	CORBA_Object object;
	RBShell *rb_shell;
	char *old_collate = NULL;

	const struct poptOption popt_options[] =
	{
		{ "print-playing",   0,  POPT_ARG_NONE,          &print_playing,                                  0, N_("Print the playing song and exit"),     NULL },
		{ "print-playing-path", 0,  POPT_ARG_NONE,          &print_playing_path,                          0, N_("Print the playing song URI and exit"),     NULL },
		{ "debug",           'd',  POPT_ARG_NONE,          &debug,                                        0, N_("Enable debugging code"),     NULL },
		{ "no-registration", 'n',  POPT_ARG_NONE,          &no_registration,                              0, N_("Do not register the shell"), NULL },
		{ "quit",            'q',  POPT_ARG_NONE,          &quit,                                         0, N_("Quit Rhythmbox"),            NULL },
		{ NULL,              '\0', POPT_ARG_INCLUDE_TABLE, (poptOption *) monkey_media_get_popt_table (), 0, N_("MonkeyMedia options:"),      NULL },
		POPT_TABLEEND
	};

	srand(time(0));

	gtk_set_locale ();
	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE, argc, argv,
				      GNOME_PARAM_POPT_TABLE, popt_options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	/* workaround for non utf8 LC_COLLATE */
	old_collate = g_strdup_printf ("LC_COLLATE=%s",
				       g_getenv ("LC_COLLATE"));
	if (old_collate == NULL || strstr (old_collate, "UTF-8") == NULL)
	{
		char *lang = NULL, *new_collate;
		const char *env;

		env = g_getenv ("LANG");
		if (env == NULL)
		{
			env = "C";
		}

		if (strlen (env) >=5)
			lang = g_strndup (g_getenv ("LANG"), 5);
		else
			lang = g_strdup ("en_US");

		new_collate = g_strdup_printf ("LC_COLLATE=%s.UTF-8",
					       lang);
		putenv (new_collate);

		g_free (lang);
		g_free (new_collate);
	}
	
	gdk_threads_init ();

	CORBA_exception_init (&ev);

	rb_debug_init (debug);
	rb_file_helpers_init ();

	if (no_registration == FALSE)
	{
		object = bonobo_activation_activate_from_id (RB_SHELL_OAFIID,
			 				     Bonobo_ACTIVATION_FLAG_EXISTING_ONLY,
							     NULL, NULL);
	}
	else
		object = NULL;

	if (object == NULL)
	{
		rb_debug ("Going to create a new shell");

		glade_gnome_init ();

		rb_stock_icons_init ();

		rb_shell = rb_shell_new ();

		g_object_set_data (G_OBJECT (rb_shell), "argv", argv);
		g_object_set_data (G_OBJECT (rb_shell), "argc", GINT_TO_POINTER (argc));

		g_idle_add ((GSourceFunc) rb_init, rb_shell);

		bonobo_main ();
	}
	else
	{
		rb_debug ("already running");

		rb_handle_cmdline (argv, argc, TRUE);
	}

	/* restore original collate */
	putenv (old_collate);
	g_free (old_collate);

	/* cleanup */
	CORBA_exception_free (&ev);

	rb_file_helpers_shutdown ();

	monkey_media_shutdown ();

	return 0;
}

static gboolean
rb_init (RBShell *shell)
{
	char **argv;
	int argc;

	rb_shell_construct (shell);

	argv = (char **) g_object_get_data (G_OBJECT (shell), "argv");
	argc = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (shell), "argc"));

	rb_handle_cmdline (argv, argc, FALSE);
	
	return FALSE;
}

static void
rb_handle_cmdline (char **argv, int argc,
		   gboolean already_running)
{
	GNOME_Rhythmbox shell;
	int i;
	char *ret;

	shell = bonobo_activation_activate_from_id (RB_SHELL_OAFIID, 0, NULL, &ev);
	if (shell == NULL)
	{
		char *msg = rb_shell_corba_exception_to_string (&ev);
		rb_warning_dialog (_("Failed to activate the shell:\n%s"), msg);
		g_free (msg);
		
		return;
	}

	if (print_playing)
		printf ("%s\n", GNOME_Rhythmbox_getPlayingTitle (shell, &ev));
	if (print_playing_path)
		printf ("%s\n", GNOME_Rhythmbox_getPlayingPath (shell, &ev));

	for (i = 1; i < argc; i++)
	{
		char *tmp;

		tmp = rb_uri_resolve_relative (argv[i]);
			
		if (rb_uri_exists (tmp) == TRUE)
			GNOME_Rhythmbox_handleFile (shell, tmp, &ev);

		g_free (tmp);
	}
	
	if (quit == TRUE)
	{
		GNOME_Rhythmbox_quit (shell, &ev);
	}

	/* at the very least, we focus the window */
	if (already_running == TRUE && !(print_playing_path || print_playing))
		GNOME_Rhythmbox_grabFocus (shell, &ev);
}
