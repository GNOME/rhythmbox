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
#include <bonobo/bonobo-main.h>
#include <glade/glade-init.h>
#include <monkey-media.h>
#include <stdlib.h>
#include <time.h>

#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-stock-icons.h"

static gboolean rb_init (RBShell *shell);
static void rb_handle_cmdline (char **argv, int argc);

static CORBA_Environment ev;

static gboolean debug           = FALSE;
static gboolean quit            = FALSE;
static gboolean no_registration = FALSE;

int
main (int argc, char **argv)
{
	GnomeProgram *program;
	CORBA_Object object;
	RBShell *rb_shell;

	const struct poptOption popt_options[] =
	{
		{ "debug",           'd',  POPT_ARG_NONE,          &debug,                                        0, N_("Enable debugging code"),     NULL },
		{ "no-registration", 'n',  POPT_ARG_NONE,          &no_registration,                              0, N_("Do not register the shell"), NULL },
		{ "quit",            'q',  POPT_ARG_NONE,          &quit,                                         0, N_("Quit Rhythmbox"),            NULL },
		{ NULL,              '\0', POPT_ARG_INCLUDE_TABLE, (poptOption *) monkey_media_get_popt_table (), 0, N_("MonkeyMedia options:"),      NULL },
		POPT_TABLEEND
	};

	program = gnome_program_init (PACKAGE, VERSION,
				      LIBGNOMEUI_MODULE, argc, argv,
				      GNOME_PARAM_POPT_TABLE, popt_options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Rhythmbox"),
				      GNOME_PARAM_APP_DATADIR, DATADIR,
				      NULL);

	gdk_threads_init ();

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif
	
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

		rb_handle_cmdline (argv, argc);
	}

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

	rb_handle_cmdline (argv, argc);
	
	return FALSE;
}

static void
rb_handle_cmdline (char **argv, int argc)
{
	GNOME_RhythmboxShell shell;
	int i;

	shell = bonobo_activation_activate_from_id (RB_SHELL_OAFIID, 0, NULL, &ev);
	if (shell == NULL)
	{
		char *msg = rb_shell_corba_exception_to_string (&ev);
		rb_warning_dialog (_("Failed to activate the shell:\n%s"), msg);
		g_free (msg);
		
		return;
	}

	for (i = 1; i < argc; i++)
	{
		char *tmp;

		tmp = rb_uri_resolve_relative (argv[i]);
			
		if (rb_uri_exists (tmp) == TRUE)
			GNOME_RhythmboxShell_addToLibrary (shell, tmp, &ev);

		g_free (tmp);
	}
	
	if (quit == TRUE)
	{
		GNOME_RhythmboxShell_quit (shell, &ev);
	}

	/* at the very least, we focus the window */
	GNOME_RhythmboxShell_grabFocus (shell, &ev);

	
}
