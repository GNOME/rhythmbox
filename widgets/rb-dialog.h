/*
 *  arch-tag: Header file for Rhythmbox dialog wrapper functions
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DIALOG_H
#define __RB_DIALOG_H

#include <gtk/gtkwindow.h>
#include <gtk/gtkfilechooserdialog.h>
#include <stdarg.h>

G_BEGIN_DECLS

void		rb_error_dialog_full	(GtkWindow *parent,
					 gboolean modal,
					 const char *primary,
					 const char *secondary,
					 const char *first_button,
					 ...);

void		rb_error_dialog		(GtkWindow *parent,
					 const char *primary,
					 const char *secondary,
					 ...);
					 
GtkWidget *	rb_file_chooser_new	(const char *title,
					 GtkWindow *parent,
					 GtkFileChooserAction action,
					 gboolean local_only);

G_END_DECLS

#endif /* __RB_DIALOG_H */
