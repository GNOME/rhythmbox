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

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "rb-dialog.h"

static void rb_dialog (const char *format, va_list args, GtkMessageType type);

void
rb_error_dialog (const char *format, ...)
{
	va_list args;

	va_start (args, format);

	rb_dialog (format, args, GTK_MESSAGE_ERROR);

	va_end (args);
}

void
rb_warning_dialog (const char *format, ...)
{
	va_list args;

	va_start (args, format);

	rb_dialog (format, args, GTK_MESSAGE_WARNING);

	va_end (args);
}

void
rb_message_dialog (const char *format, ...)
{
	va_list args;

	va_start (args, format);

	rb_dialog (format, args, GTK_MESSAGE_INFO);

	va_end (args);
}

static void
rb_dialog (const char *format, va_list args, GtkMessageType type)
{
	GtkWidget *dialog;
	char buffer[1025];

	vsnprintf (buffer, 1024, format, args);

	dialog = gtk_message_dialog_new (NULL, 0,
					 type,
					 GTK_BUTTONS_OK,
					 buffer);

	gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);
}

char *
rb_ask_file (const char *title, GtkWindow *parent)
{
	GtkWidget *filesel;
	char *ret = NULL;

	filesel = gtk_file_selection_new (title);
	gtk_window_set_transient_for (GTK_WINDOW (filesel),
				      parent);
	if (gtk_dialog_run (GTK_DIALOG (filesel)) == GTK_RESPONSE_OK)
		ret = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (filesel)));

	gtk_widget_destroy (filesel);

	return ret;
}
