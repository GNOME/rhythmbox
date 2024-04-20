/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
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
#include "rb-file-helpers.h"

/**
 * SECTION:rbdialog
 * @short_description: helper functions for creating gtk+ dialog windows
 *
 * This provides a couple of shortcuts for creating dialogs.  If you want
 * to annoy the user by displaying an error message, this is the way to do it.
 */

/**
 * rb_error_dialog:
 * @parent: parent #GtkWindow for the dialog
 * @primary: main error message
 * @secondary: secondary error message (printf-style format string)
 * @...: any substitution values for the secondary message
 *
 * Creates and displays a simple error dialog box containing a primary
 * message in bold, larger type and a secondary message in the regular
 * font.  Both the primary and secondary message strings should be
 * translated.
 *
 * Care should be taken to avoid opening multiple error dialog boxes
 * when a single error message (such as 'out of disk space') affects
 * multiple items.
 */
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
					 "%s", primary);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", text);

	gtk_window_set_title (GTK_WINDOW (dialog), "");

	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

	gtk_widget_show (dialog);

	g_free (text);
}

/**
 * rb_file_chooser_new:
 * @title: title for the file chooser
 * @parent: parent #GtkWindow for the file chooser
 * @action: the #GtkFileChooserAction
 * @local_only: if TRUE, don't show network locations
 *
 * Creates and shows a regular gtk+ file chooser dialog with
 * a given title.  The user's music directory (typically ~/Music) is
 * added as a shortcut.
 * 
 * For consistency, this should be used anywhere a file chooser is required.
 *
 * After creating the dialog, the caller should connect a handler to its 
 * 'response' signal to process the user's selected files or folders.
 *
 * Return value: (transfer full): the file chooser #GtkWidget
 */
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
						      _("_Cancel"), GTK_RESPONSE_CANCEL,
						      _("_Open"), GTK_RESPONSE_ACCEPT,
						      NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_ACCEPT);
	} else if (action == GTK_FILE_CHOOSER_ACTION_SAVE) {
		dialog = gtk_file_chooser_dialog_new (title, parent,
						      action,
						      _("_Cancel"), GTK_RESPONSE_CANCEL,
						      _("_Save"), GTK_RESPONSE_ACCEPT,
						      NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_ACCEPT);
		gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	} else {
		g_assert_not_reached ();
		return NULL;
	}

	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), local_only);
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (dialog),
					      rb_music_dir (),
					      NULL);

	if (parent != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
	}

	gtk_widget_show_all (dialog);

	return dialog;
}
