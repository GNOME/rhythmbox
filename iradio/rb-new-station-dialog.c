/* 
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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
 *  $Id$
 */

#include <config.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkbox.h>
#include <gtk/gtktable.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkstock.h>
#include <glade/glade.h>
#include <string.h>
#include <time.h>

#include "rb-new-station-dialog.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"

static void rb_new_station_dialog_class_init (RBNewStationDialogClass *klass);
static void rb_new_station_dialog_init (RBNewStationDialog *dialog);
static void rb_new_station_dialog_finalize (GObject *object);
static void rb_new_station_dialog_set_property (GObject *object, 
						guint prop_id,
						const GValue *value, 
						GParamSpec *pspec);
static void rb_new_station_dialog_get_property (GObject *object, 
						guint prop_id,
						GValue *value, 
						GParamSpec *pspec);
static void rb_new_station_dialog_response_cb (GtkDialog *gtkdialog,
					       int response_id,
					       RBNewStationDialog *dialog);
static void rb_new_station_dialog_entry_changed_cb (GtkEntry *entry,
						    RBNewStationDialog *dialog);

struct RBNewStationDialogPrivate
{
	RBIRadioBackend *backend;

	GtkWidget   *title;
	GtkWidget   *genre;
	GtkWidget   *location;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

enum 
{
	PROP_0,
	PROP_BACKEND
};

static GObjectClass *parent_class = NULL;

GType
rb_new_station_dialog_get_type (void)
{
	static GType rb_new_station_dialog_type = 0;

	if (rb_new_station_dialog_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNewStationDialogClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_new_station_dialog_class_init,
			NULL,
			NULL,
			sizeof (RBNewStationDialog),
			0,
			(GInstanceInitFunc) rb_new_station_dialog_init
		};

		rb_new_station_dialog_type = g_type_register_static (GTK_TYPE_DIALOG,
								     "RBNewStationDialog",
								     &our_info, 0);
	}

	return rb_new_station_dialog_type;
}

static void
rb_new_station_dialog_class_init (RBNewStationDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_new_station_dialog_set_property;
	object_class->get_property = rb_new_station_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_BACKEND,
					 g_param_spec_object ("backend",
					                      "RBIRadioBackend",
					                      "RBIRadioBackend object",
					                      RB_TYPE_IRADIO_BACKEND,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_new_station_dialog_finalize;
}

static void
rb_new_station_dialog_init (RBNewStationDialog *dialog)
{
	GladeXML *xml;
	
	/* create the dialog and some buttons forward - close */
	dialog->priv = g_new0 (RBNewStationDialogPrivate, 1);
	
	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (rb_new_station_dialog_response_cb),
			  dialog);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_title (GTK_WINDOW (dialog), _("New Internet Radio Station"));

	dialog->priv->cancelbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    _("_Don't Add"),
							    GTK_RESPONSE_CANCEL);
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							GTK_STOCK_ADD,
							GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	xml = rb_glade_xml_new ("station-new.glade",
				"newstation",
				dialog);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "newstation"));

	/* get the widgets from the XML */
	dialog->priv->title = glade_xml_get_widget (xml, "titleEntry");
	dialog->priv->genre = glade_xml_get_widget (xml, "genreCombo");
	dialog->priv->location = glade_xml_get_widget (xml, "locationEntry");
	g_signal_connect (G_OBJECT (dialog->priv->title),
			  "changed",
			  G_CALLBACK (rb_new_station_dialog_entry_changed_cb),
			  dialog);

	g_signal_connect (G_OBJECT (GTK_COMBO (dialog->priv->genre)->entry),
			  "changed",
			  G_CALLBACK (rb_new_station_dialog_entry_changed_cb),
			  dialog);

	g_signal_connect (G_OBJECT (dialog->priv->location),
			  "changed",
			  G_CALLBACK (rb_new_station_dialog_entry_changed_cb),
			  dialog);

	gtk_combo_set_popdown_strings (GTK_COMBO (dialog->priv->genre),
				       g_list_append (NULL, _("Unknown")));
	
	/* default focus */
	gtk_widget_grab_focus (dialog->priv->title);
	/* FIXME */
	gtk_widget_set_sensitive (dialog->priv->okbutton, FALSE);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_new_station_dialog_finalize (GObject *object)
{
	RBNewStationDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NEW_STATION_DIALOG (object));

	dialog = RB_NEW_STATION_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	g_free (dialog->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_new_station_dialog_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBNewStationDialog *dialog = RB_NEW_STATION_DIALOG (object);

	switch (prop_id)
	{
	case PROP_BACKEND:
		dialog->priv->backend = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_new_station_dialog_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBNewStationDialog *dialog = RB_NEW_STATION_DIALOG (object);

	switch (prop_id)
	{
	case PROP_BACKEND:
		g_value_set_object (value, dialog->priv->backend);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_new_station_dialog_new (RBIRadioBackend *backend)
{
	RBNewStationDialog *dialog;
	GList *genrenames;

	g_return_val_if_fail (RB_IS_IRADIO_BACKEND (backend), NULL);

	dialog = g_object_new (RB_TYPE_NEW_STATION_DIALOG, "backend", backend, NULL);
	
	genrenames = rb_iradio_backend_get_genre_names (backend);
	gtk_combo_set_popdown_strings (GTK_COMBO (dialog->priv->genre),
				       genrenames);

	g_return_val_if_fail (dialog->priv != NULL, NULL);

	return GTK_WIDGET (dialog);
}

static void
rb_new_station_dialog_response_cb (GtkDialog *gtkdialog,
				   int response_id,
				   RBNewStationDialog *dialog)
{
	GList *locations = NULL;
	if (response_id != GTK_RESPONSE_OK)
		goto cleanup;
	locations = g_list_prepend (locations, g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->priv->location))));
	rb_iradio_backend_new_station (locations,
				       g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->priv->title))),
				       g_strdup (gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (dialog->priv->genre)->entry))),
				       "user",
				       dialog->priv->backend);
 cleanup:
	return;
}

static void
rb_new_station_dialog_entry_changed_cb (GtkEntry *entry,
					RBNewStationDialog *dialog)
{
	gtk_widget_set_sensitive (dialog->priv->okbutton,
				  g_utf8_strlen (gtk_entry_get_text (GTK_ENTRY (dialog->priv->title)), -1) > 0
				  && g_utf8_strlen (gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (dialog->priv->genre)->entry)), -1) > 0
				  && g_utf8_strlen (gtk_entry_get_text (GTK_ENTRY (dialog->priv->location)), -1) > 0);
}
