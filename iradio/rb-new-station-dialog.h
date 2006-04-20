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
#include "rb-entry-view.h"

#ifndef __RB_NEW_STATION_DIALOG_H
#define __RB_NEW_STATION_DIALOG_H

G_BEGIN_DECLS

#define RB_TYPE_NEW_STATION_DIALOG         (rb_new_station_dialog_get_type ())
#define RB_NEW_STATION_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NEW_STATION_DIALOG, RBNewStationDialog))
#define RB_NEW_STATION_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NEW_STATION_DIALOG, RBNewStationDialogClass))
#define RB_IS_NEW_STATION_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NEW_STATION_DIALOG))
#define RB_IS_NEW_STATION_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NEW_STATION_DIALOG))
#define RB_NEW_STATION_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NEW_STATION_DIALOG, RBNewStationDialogClass))

typedef struct RBNewStationDialogPrivate RBNewStationDialogPrivate;

typedef struct
{
	GtkDialog parent;

	RBNewStationDialogPrivate *priv;
} RBNewStationDialog;

typedef struct
{
	GtkDialogClass parent_class;

        void (*location_added) (RBNewStationDialog *dialog,
				const char         *uri);
} RBNewStationDialogClass;

GType      rb_new_station_dialog_get_type (void);
GtkWidget* rb_new_station_dialog_new      (RBEntryView *view);

G_END_DECLS

#endif /* __RB_NEW_STATION_DIALOG_H */
