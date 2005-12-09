/*
 *  arch-tag: Header file for internet radio station properties dialog
 *
 *  Copyright (C) Colin Walters <walters@debian.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <gtk/gtkdialog.h>
#include "rb-entry-view.h"

#ifndef __RB_STATION_PROPERTIES_DIALOG_H
#define __RB_STATION_PROPERTIES_DIALOG_H

G_BEGIN_DECLS

#define RB_TYPE_STATION_PROPERTIES_DIALOG         (rb_station_properties_dialog_get_type ())
#define RB_STATION_PROPERTIES_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STATION_PROPERTIES_DIALOG, RBStationPropertiesDialog))
#define RB_STATION_PROPERTIES_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STATION_PROPERTIES_DIALOG, RBStationPropertiesDialogClass))
#define RB_IS_STATION_PROPERTIES_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STATION_PROPERTIES_DIALOG))
#define RB_IS_STATION_PROPERTIES_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STATION_PROPERTIES_DIALOG))
#define RB_STATION_PROPERTIES_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STATION_PROPERTIES_DIALOG, RBStationPropertiesDialogClass))

typedef struct RBStationPropertiesDialogPrivate RBStationPropertiesDialogPrivate;

typedef struct
{
	GtkDialog parent;

	RBStationPropertiesDialogPrivate *priv;
} RBStationPropertiesDialog;

typedef struct
{
	GtkDialogClass parent_class;
} RBStationPropertiesDialogClass;

GType      rb_station_properties_dialog_get_type (void);

GtkWidget *rb_station_properties_dialog_new      (RBEntryView *view);

G_END_DECLS

#endif /* __RB_STATION_PROPERTIES_DIALOG_H */
