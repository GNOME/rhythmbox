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
 *  NOTES: log domain hack stolen from nautilus
 *
 *  $Id$
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>

#include "rb-debug.h"

static void log_handler (const char *domain,
	                 GLogLevelFlags level,
	                 const char *message,
	                 gpointer data);

static gboolean debugging = FALSE;

/* Our own funky debugging function, should only be used when something
 * is not going wrong, if something *is* wrong use g_warning.
 */
void
rb_debug (const char *format, ...)
{
	va_list args;
	char buffer[1025];

	if (debugging == FALSE) return;

	va_start (args, format);

	vsnprintf (buffer, 1024, format, args);
	
	va_end (args);

	fprintf (stderr, "** DEBUG ** %s\n", buffer);
}

void
rb_debug_init (gboolean debug)
{
	guint i;

	/* This is a workaround for the fact that there is not way to 
	 * make this useful debugging feature happen for ALL domains.
	 *
	 * What we did here is list all the ones we could think of that
	 * were interesting to us. It's OK to add more to the list.
	 */
	static const char * const standard_log_domains[] = {
		"",
		"Bonobo",
		"BonoboUI",
		"Echo",
		"Eel",
		"GConf",
		"GConf-Backends",
		"GConf-Tests",
		"GConfEd",
		"GLib",
		"GLib-GObject",
		"GModule",
		"GThread",
		"Gdk",
		"Gdk-Pixbuf",
		"GdkPixbuf",
		"Glib",
		"Gnome",
		"GnomeCanvas",
		"GnomePrint",
		"GnomeUI",
		"GnomeVFS",
		"GnomeVFS-CORBA",
		"GnomeVFS-pthread",
		"GnomeVFSMonikers",
		"Gtk",
		"Rhythmbox",
		"MonkeyMedia",
		"ORBit",
		"ZVT",
		"libIDL",
		"libgconf-scm",
		"libglade",
		"libgnomevfs",
		"librsvg",
	};

	debugging = debug;

	for (i = 0; i < G_N_ELEMENTS (standard_log_domains); i++)
	{
		g_log_set_handler (standard_log_domains[i], G_LOG_LEVEL_MASK, log_handler, NULL);
	}

	rb_debug ("Debugging enabled");
}

/* Raise a SIGINT signal to get the attention of the debugger.
 * When not running under the debugger, we don't want to stop,
 * so we ignore the signal for just the moment that we raise it.
 */
void
rb_debug_stop_in_debugger (void)
{
        void (* saved_handler) (int);

        saved_handler = signal (SIGINT, SIG_IGN);
        raise (SIGINT);
        signal (SIGINT, saved_handler);
}

/* Stop in the debugger after running the default log handler.
 * This makes certain kinds of messages stop in the debugger
 * without making them fatal (you can continue).
 */
static void
log_handler (const char *domain,
	     GLogLevelFlags level,
	     const char *message,
	     gpointer data)
{
	g_log_default_handler (domain, level, message, data);
	if ((level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) != 0)
	{
		rb_debug_stop_in_debugger ();
	}
}
