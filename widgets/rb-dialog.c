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
#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "rb-dialog.h"
#include "gst-hig-dialog.h"
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

	dialog = gst_hig_dialog_new (parent,
				     GTK_DIALOG_DESTROY_WITH_PARENT,
				     GST_HIG_MESSAGE_ERROR,
				     primary, text,
				     _("Close"), GTK_RESPONSE_ACCEPT,
				     NULL);
	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (gtk_widget_destroy),
				 NULL, 0);
	gtk_dialog_run (GTK_DIALOG (dialog));

	g_free (text);
}


GtkWidget *
rb_file_chooser_new (const char *title,
		     GtkWindow *parent,
		     GtkFileChooserAction action)
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
	} else {
		g_assert_not_reached ();
		return NULL;
	}

	if (parent != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	}

	gtk_widget_show_all (dialog);

	return dialog;
}
