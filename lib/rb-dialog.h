/*
 *  arch-tag: Header file for Rhythmbox dialog wrapper functions
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

#ifndef __RB_DIALOG_H
#define __RB_DIALOG_H

#include <gtk/gtkwindow.h>
#include <stdarg.h>

G_BEGIN_DECLS

void       rb_error_dialog      (const char *format, ...);
void       rb_warning_dialog    (const char *format, ...);
void       rb_message_dialog    (const char *format, ...);

GtkWidget *rb_ask_file_multiple (const char *title,
			         const char *default_file,
			         GtkWindow *parent);

GtkWidget *rb_ask_file          (const char *title,
			         const char *default_file,
			         GtkWindow *parent);

GtkWidget *rb_ask_string        (const char *question,
			         const char *accept_button_text,
			         const char *default_text,
			         GtkWindow *parent);

G_END_DECLS

#endif /* __RB_DIALOG_H */
