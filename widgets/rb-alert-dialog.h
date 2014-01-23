/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* rb-alert-dialog.h: An HIG compliant alert dialog.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   see <http://www.gnu.org/licenses/>.

*/

#ifndef RB_ALERT_DIALOG_H
#define RB_ALERT_DIALOG_H

#include <gtk/gtk.h>

#define RB_TYPE_ALERT_DIALOG        (rb_alert_dialog_get_type ())
#define RB_ALERT_DIALOG(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ALERT_DIALOG, RBAlertDialog))

typedef struct _RBAlertDialog        RBAlertDialog;
typedef struct _RBAlertDialogClass   RBAlertDialogClass;
typedef struct _RBAlertDialogDetails RBAlertDialogDetails;

struct _RBAlertDialog
{
	GtkDialog parent_instance;
	RBAlertDialogDetails *details;
};

struct _RBAlertDialogClass
{
	GtkDialogClass parent_class;
};

GType      rb_alert_dialog_get_type (void);

GtkWidget* rb_alert_dialog_new                 (GtkWindow      *parent,
                                                GtkDialogFlags  flags,
                                                GtkMessageType  type,
                                                GtkButtonsType  buttons,
                                                const gchar    *primary_message,
                                                const gchar    *secondary_message);
void       rb_alert_dialog_set_primary_label   (RBAlertDialog *dialog,
		                                const gchar    *message);
void       rb_alert_dialog_set_secondary_label (RBAlertDialog *dialog,
		                                const gchar    *message);
void       rb_alert_dialog_set_details_label   (RBAlertDialog *dialog,
						const gchar    *message);

#endif /* RB_ALERT_DIALOG_H */
