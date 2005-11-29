/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of Rhythmbox dialog wrapper functions
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
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

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "rb-dialog.h"
#include "rb-stock-icons.h"

void
rb_error_dialog (GtkWindow *parent,
		 const char *primary,
		 const char *secondary,
		 ...)
{
	char *text = "";
	va_list args;
	GtkWidget *dialog;

	va_start (args, secondary);
	g_vasprintf (&text, secondary, args);
	va_end (args);

	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 primary);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", text);

	gtk_window_set_title (GTK_WINDOW (dialog), "");

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

	gtk_widget_show (dialog);

	g_free (text);
}


GtkWidget *
rb_file_chooser_new (const char *title,
		     GtkWindow *parent,
		     GtkFileChooserAction action,
		     gboolean local_only)
{
	GtkWidget *dialog;

	if (action == GTK_FILE_CHOOSER_ACTION_OPEN	    ||
	    action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER ||
	    action == GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER) {
		dialog = gtk_file_chooser_dialog_new (title, parent,
						      action,
						      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
						      NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_ACCEPT);
	} else if (action == GTK_FILE_CHOOSER_ACTION_SAVE) {
		dialog = gtk_file_chooser_dialog_new (title, parent,
						      action,
						      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
						      NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_ACCEPT);
#if GTK_MINOR_VERSION >= 8
		gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
#endif
	} else {
		g_assert_not_reached ();
		return NULL;
	}

	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), local_only);

	if (parent != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	}

	gtk_widget_show_all (dialog);

	return dialog;
}
