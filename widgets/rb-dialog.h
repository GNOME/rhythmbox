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

#ifndef __RB_DIALOG_H
#define __RB_DIALOG_H

#include <gtk/gtk.h>
#include <stdarg.h>

G_BEGIN_DECLS

void		rb_error_dialog		(GtkWindow *parent,
					 const char *primary,
					 const char *secondary,
					 ...) G_GNUC_PRINTF (3, 4);

GtkWidget *	rb_file_chooser_new	(const char *title,
					 GtkWindow *parent,
					 GtkFileChooserAction action,
					 gboolean local_only);

G_END_DECLS

#endif /* __RB_DIALOG_H */
