/*
 *  arch-tag: Implementation of Rhythmbox dialog wrapper functions
 *
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
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "rb-dialog.h"
#include "rb-stock-icons.h"

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

	dialog = gtk_message_dialog_new (NULL,
					 GTK_DIALOG_MODAL,
					 type,
					 GTK_BUTTONS_OK,
					 "%s",
					 buffer);

	g_signal_connect_swapped (GTK_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  GTK_OBJECT (dialog));

	gtk_widget_show (GTK_WIDGET (dialog));
}

GtkWidget *
rb_ask_file_multiple (const char *title,
		      const char *default_file,
		      GtkWindow *parent)
{
	GtkWidget *filesel;

	filesel = gtk_file_selection_new (title);
	if (default_file != NULL)
		gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), default_file);

	gtk_file_selection_hide_fileop_buttons (GTK_FILE_SELECTION (filesel));
	gtk_file_selection_set_select_multiple (GTK_FILE_SELECTION (filesel), TRUE);

	gtk_window_set_transient_for (GTK_WINDOW (filesel),
				      parent);
	gtk_window_set_modal (GTK_WINDOW (filesel),
			      FALSE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (filesel),
					    TRUE);

	gtk_widget_show_all (filesel);

	return filesel;
}

GtkWidget *
rb_ask_file (const char *title,
	     const char *default_file,
	     GtkWindow *parent)
{
	GtkWidget *filesel;

	filesel = gtk_file_selection_new (title);
	if (default_file != NULL)
		gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), default_file);

	gtk_file_selection_hide_fileop_buttons (GTK_FILE_SELECTION (filesel));

	gtk_window_set_transient_for (GTK_WINDOW (filesel),
				      parent);
	gtk_window_set_modal (GTK_WINDOW (filesel),
			      FALSE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (filesel),
					    TRUE);
	
	gtk_widget_show_all (filesel);

	return filesel;
}

GtkWidget *
rb_ask_string (const char *question,
	       const char *accept_button_text,
	       const char *default_text,
	       GtkWindow *parent)
{
	GtkWidget *dialog, *hbox, *image, *entry, *label, *vbox;
	char *tmp;

	dialog = gtk_dialog_new_with_buttons ("",
					      NULL,
					      0,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      accept_button_text,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);

	gtk_window_set_transient_for (GTK_WINDOW (dialog),
				      parent);
	gtk_window_set_modal (GTK_WINDOW (dialog),
			      FALSE);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog),
					    TRUE);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
	image = gtk_image_new_from_stock (RB_STOCK_PLAYLIST,
					  GTK_ICON_SIZE_DIALOG);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 0);

	tmp = g_strdup_printf ("%s\n", question);
	label = gtk_label_new (tmp);
	g_free (tmp);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), default_text);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), hbox);
	gtk_widget_show_all (hbox);

	gtk_widget_grab_focus (entry);

	g_object_set_data (G_OBJECT (dialog), "entry", entry);

	gtk_widget_show_all (dialog);

	return dialog;
}
