/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen
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
 *  NOTES: log domain hack stolen from nautilus
 *
 */

#include "config.h"

#if defined(HAVE_PRCTL)
#include <sys/prctl.h>
#elif defined(HAVE_PTHREAD_GETNAME_NP)
#define _GNU_SOURCE
#include <pthread.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>

#include <glib.h>

#include "rb-debug.h"

/**
 * SECTION:rbdebug
 * @short_description: debugging support functions
 *
 * In addition to a simple debug output system, we have two distinct
 * profiling mechanisms for timing sections of code.
 */

static void log_handler (const char *domain,
	                 GLogLevelFlags level,
	                 const char *message,
	                 gpointer data);

static const char *debug_everything = "everything";
static const char *debug_match = NULL;

/**
 * rb_debug_matches:
 * @func: function to check
 * @file: filename to check
 *
 * Checks if @file or @func matches the current debug output settings.
 *
 * Return value: %TRUE if matched
 */
gboolean
rb_debug_matches (const char *func,
		  const char *file)
{
	if (debug_match == NULL ||
	   (debug_match != debug_everything &&
	   (strstr (file, debug_match) == NULL) &&
	   (strstr (func, debug_match) == NULL)))
		return FALSE;

	return TRUE;
}

/**
 * rb_debug:
 * @...: printf-style format string followed by any substitution values
 *
 * If the call site function or file name matches the current debug output
 * settings, the message will be formatted and printed to standard error,
 * including a timestamp, the thread ID, the file and function names, and
 * the line number.  A newline will be appended, so the format string shouldn't
 * include one.
 */

static void
_rb_debug_print (const char *func, const char *file, const int line, gboolean newline, const char *buffer)
{
	char str_time[255];
	time_t the_time;
	char thread_name[17] = {0,};

#if defined(HAVE_PRCTL)
	prctl(PR_GET_NAME, thread_name, 0, 0, 0);
#elif defined(HAVE_PTHREAD_GETNAME_NP)
	pthread_getname_np(pthread_self (), thread_name, sizeof(thread_name));
#endif
	if (thread_name[0] == '\0' || g_str_equal (thread_name, "pool-rhythmbox"))
		snprintf (thread_name, sizeof (thread_name)-1, "%p", g_thread_self ());

	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));

	g_printerr (newline ? "(%s) <%s> [%s] %s:%d: %s\n" : "(%s) <%s> [%s] %s:%d: %s",
		    str_time, thread_name, func, file, line, buffer);
}

/**
 * rb_debug_real:
 * @func: function name
 * @file: file name
 * @line: line number
 * @newline: if TRUE, add a newline to the output
 * @message: the debug message
 *
 * If the debug output settings match the function or file names,
 * the debug message will be formatted and written to standard error.
 */
void
rb_debug_real (const char *func, const char *file, const int line, gboolean newline, const char *message)
{
	if (rb_debug_matches (func, file)) {
		_rb_debug_print (func, file, line, newline, message);
	}
}

/**
 * rb_debug_realf:
 * @func: function name
 * @file: file name
 * @line: line number
 * @newline: if TRUE, add a newline to the output
 * @format: printf style format specifier
 * @...: substitution values for @format
 *
 * If the debug output settings match the function or file names,
 * the debug message will be formatted and written to standard error.
 */
void
rb_debug_realf (const char *func,
		const char *file,
		const int line,
		gboolean newline,
		const char *format, ...)
{
	va_list args;
	char buffer[1025];

	if (!rb_debug_matches (func, file))
		return;

	va_start (args, format);

	g_vsnprintf (buffer, 1024, format, args);

	va_end (args);

	_rb_debug_print (func, file, line, newline, buffer);
}

/**
 * rb_debug_init:
 * @debug: if TRUE, enable all debug output
 *
 * Sets up debug output, with either all debug enabled
 * or none.
 */
void
rb_debug_init (gboolean debug)
{
	rb_debug_init_match (debug ? debug_everything : NULL);
}

/**
 * rb_debug_init_match:
 * @match: string to match functions and filenames against
 *
 * Sets up debug output, enabling debug output from file and function
 * names that contain the specified match string.
 *
 * Also sets up a GLib log handler that will trigger a debugger
 * break for critical or warning level output if any debug output
 * at all is enabled.
 */
void
rb_debug_init_match (const char *match)
{
	guint i;

	/* This is a workaround for the fact that there is no way to
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
		"GStreamer",
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
		"RhythmDB",
		"ORBit",
		"ZVT",
		"libIDL",
		"libgconf-scm",
		"libgnomevfs",
		"librsvg",
		"libnotify",
		"GLib-GIO",
	};

	debug_match = match;

	if (debug_match != NULL)
		for (i = 0; i < G_N_ELEMENTS (standard_log_domains); i++)
			g_log_set_handler (standard_log_domains[i], G_LOG_LEVEL_MASK, log_handler, NULL);

	rb_debug ("Debugging enabled");
}

/**
 * rb_debug_get_args:
 *
 * Constructs arguments to pass to another process using
 * this debug output code that will produce the same debug output
 * settings.
 *
 * Return value: (transfer full): debug output arguments, must be freed with #g_strfreev()
 */
char **
rb_debug_get_args (void)
{
	char **args;
	if (debug_match == NULL) {
		args = (char **)g_new0 (char *, 1);
	} else if (debug_match == debug_everything) {
		args = (char **)g_new0 (char *, 2);
		args[0] = g_strdup ("--debug");
	} else {
		args = (char **)g_new0 (char *, 3);
		args[0] = g_strdup ("--debug-match");
		args[1] = g_strdup (debug_match);
	}
	return args;
}

/**
 * rb_debug_stop_in_debugger:
 *
 * Raises a SIGINT signal to get the attention of the debugger.
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

struct RBProfiler
{
	GTimer *timer;
	char *name;
};

/**
 * rb_profiler_new: (skip)
 * @name: profiler name
 *
 * Creates a new profiler instance.  This can be used to
 * time certain sections of code.
 *
 * Return value: profiler instance
 */
RBProfiler *
rb_profiler_new (const char *name)
{
	RBProfiler *profiler;
	
	if (debug_match == NULL)
		return NULL;

	profiler = g_new0 (RBProfiler, 1);
	profiler->timer = g_timer_new ();
	profiler->name  = g_strdup (name);

	g_timer_start (profiler->timer);

	return profiler;
}

/**
 * rb_profiler_dump: (skip)
 * @profiler: profiler instance
 *
 * Produces debug output for the profiler instance,
 * showing the elapsed time.
 */
void
rb_profiler_dump (RBProfiler *profiler)
{
	gulong elapsed;
	double seconds;

	if (debug_match == NULL)
		return;
	if (profiler == NULL)
		return;

	seconds = g_timer_elapsed (profiler->timer, &elapsed);

	rb_debug ("PROFILER %s %ld ms (%f s) elapsed", profiler->name, 
		  elapsed / (G_USEC_PER_SEC / 1000), seconds);
}

/**
 * rb_profiler_reset: (skip)
 * @profiler: profiler instance
 *
 * Resets the elapsed time for the profiler
 */
void
rb_profiler_reset (RBProfiler *profiler)
{
	if (debug_match == NULL)
		return;
	if (profiler == NULL)
		return;

	g_timer_start (profiler->timer);
}

/**
 * rb_profiler_free: (skip)
 * @profiler: profiler instance to destroy
 *
 * Frees the memory associated with a profiler instance.
 */
void
rb_profiler_free (RBProfiler *profiler)
{
	if (debug_match == NULL)
		return;
	if (profiler == NULL)
		return;

	g_timer_destroy (profiler->timer);
	g_free (profiler->name);
	g_free (profiler);
}

/* Profiling */

static int profile_indent;

static void
profile_add_indent (int indent)
{
	profile_indent += indent;
	if (profile_indent < 0) {
		g_error ("You screwed up your indentation");
	}
}

/**
 * rb_profile_start:
 * @msg: profile point message
 *
 * Records a start point for profiling.
 * This profile mechanism operates by issuing file access
 * requests with filenames indicating the profile points.
 * Use 'strace -e access' to gather this information.
 */

/**
 * rb_profile_end:
 * @msg: profile point message
 *
 * Records an end point for profiling.  See @rb_profile_start.
 */

/**
 * _rb_profile_log:
 * @func: call site function name
 * @file: call site file name
 * @line: call site line number
 * @indent: indent level for output
 * @msg1: message part 1
 * @msg2: message part 2
 *
 * Issues a file access request with a constructed filename.
 * Run rhythmbox under 'strace -ttt -e access' to show these profile
 * points.
 */
void
_rb_profile_log (const char *func,
		 const char *file,
		 int         line,
		 int	     indent,
		 const char *msg1,
		 const char *msg2)
{
	char *str;

	if (indent < 0) {
		profile_add_indent (indent);
	}

	if (profile_indent == 0) {
		str = g_strdup_printf ("MARK: [%s %s %d] %s %s", file, func, line, msg1 ? msg1 : "", msg2 ? msg2 : "");
	} else {
		str = g_strdup_printf ("MARK: %*c [%s %s %d] %s %s", profile_indent - 1, ' ', file, func, line, msg1 ? msg1 : "", msg2 ? msg2 : "");
	}

	access (str, F_OK);

	g_free (str);

	if (indent > 0) {
		profile_add_indent (indent);
	}
}
