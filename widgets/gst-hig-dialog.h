/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* 
 * Copyright (C) 2004 Carlos Garnacho
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Carlos Garnacho Parro <carlosg@gnome.org>
 */

#ifndef __GST_HIG_DIALOG_H__
#define __GST_HIG_DIALOG_H__

#include <gtk/gtkdialog.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum
{
  GST_HIG_MESSAGE_INFO,
  GST_HIG_MESSAGE_WARNING,
  GST_HIG_MESSAGE_QUESTION,
  GST_HIG_MESSAGE_ERROR,
	GST_HIG_MESSAGE_AUTHENTICATION
} GstHigMessageType;

#define GST_TYPE_HIG_MESSAGE_TYPE            (gst_hig_message_type_get_type())
#define GST_TYPE_HIG_DIALOG                  (gst_hig_dialog_get_type ())
#define GST_HIG_DIALOG(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HIG_DIALOG, GstHigDialog))
#define GST_HIG_DIALOG_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass),  GST_TYPE_HIG_DIALOG, GstHigDialogClass))
#define GST_IS_HIG_DIALOG(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HIG_DIALOG))
#define GST_IS_HIG_DIALOG_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass),  GST_TYPE_HIG_DIALOG))
#define GST_HIG_DIALOG_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj),  GST_TYPE_HIT_DIALOG, GstHigDialogClass))

typedef struct _GstHigDialog        GstHigDialog;
typedef struct _GstHigDialogClass   GstHigDialogClass;
typedef struct _GstHigDialogPrivate GstHigDialogPrivate;

struct _GstHigDialog
{
  GtkDialog parent_instance;

  /*< private >*/
	GstHigDialogPrivate *_priv;
};

struct _GstHigDialogClass
{
  GtkDialogClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

GType      gst_hig_dialog_get_type (void);
GType      gst_hig_message_type_get_type (void);

GtkWidget* gst_hig_dialog_new      (GtkWindow         *parent,
                                    GtkDialogFlags     flags,
                                    GstHigMessageType  type,
                                    const gchar       *primary_text,
                                    const gchar       *secondary_text,
                                    const gchar       *first_button_text,
                                    ...) G_GNUC_PRINTF (6, 7);

void       gst_hig_dialog_set_primary_text (GstHigDialog *dialog,
                                            const gchar  *message_format,
                                            ...) G_GNUC_PRINTF (2, 3);

void       gst_hig_dialog_set_secondary_text (GstHigDialog *dialog,
                                              const gchar  *message_format,
                                              ...) G_GNUC_PRINTF (2, 3);

void       gst_hig_dialog_set_extra_widget   (GstHigDialog *dialog,
                                              GtkWidget    *extra_widget);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_HIG_DIALOG_H__ */
