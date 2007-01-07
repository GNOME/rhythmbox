/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Header files for new station dialog
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <gtk/gtkdialog.h>

#ifndef __RB_URI_DIALOG_H
#define __RB_URI_DIALOG_H

G_BEGIN_DECLS

#define RB_TYPE_URI_DIALOG         (rb_uri_dialog_get_type ())
#define RB_URI_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_URI_DIALOG, RBURIDialog))
#define RB_URI_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_URI_DIALOG, RBURIDialogClass))
#define RB_IS_URI_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_URI_DIALOG))
#define RB_IS_URI_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_URI_DIALOG))
#define RB_URI_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_URI_DIALOG, RBURIDialogClass))

typedef struct RBURIDialogPrivate RBURIDialogPrivate;

typedef struct
{
	GtkDialog parent;

	RBURIDialogPrivate *priv;
} RBURIDialog;

typedef struct
{
	GtkDialogClass parent_class;

        void (*location_added) (RBURIDialog *dialog,
				const char  *uri);
} RBURIDialogClass;

GType      rb_uri_dialog_get_type (void);
GtkWidget* rb_uri_dialog_new      (const char *title,
				   const char *label);

G_END_DECLS

#endif /* __RB_URI_DIALOG_H */
