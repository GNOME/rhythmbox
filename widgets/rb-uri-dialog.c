/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of new station dialog
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs.h>

#include "rb-uri-dialog.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"

static void rb_uri_dialog_class_init (RBURIDialogClass *klass);
static void rb_uri_dialog_init (RBURIDialog *dialog);
static void rb_uri_dialog_finalize (GObject *object);
static void rb_uri_dialog_response_cb (GtkDialog *gtkdialog,
				       int response_id,
				       RBURIDialog *dialog);
static void rb_uri_dialog_text_changed (GtkEditable *buffer,
					RBURIDialog *dialog);

struct RBURIDialogPrivate
{
	GtkWidget   *label;
	GtkWidget   *url;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

#define RB_URI_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_URI_DIALOG, RBURIDialogPrivate))

enum
{
	LOCATION_ADDED,
	LAST_SIGNAL
};

static guint rb_uri_dialog_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBURIDialog, rb_uri_dialog, GTK_TYPE_DIALOG)

static void
rb_uri_dialog_class_init (RBURIDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_uri_dialog_finalize;

	rb_uri_dialog_signals [LOCATION_ADDED] =
		g_signal_new ("location-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBURIDialogClass, location_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBURIDialogPrivate));
}

static void
rb_uri_dialog_init (RBURIDialog *dialog)
{
	GladeXML *xml;

	/* create the dialog and some buttons forward - close */
	dialog->priv = RB_URI_DIALOG_GET_PRIVATE (dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_uri_dialog_response_cb),
				 dialog, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	/*gtk_window_set_title (GTK_WINDOW (dialog), _("New Internet Radio Station"));*/

	dialog->priv->cancelbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    GTK_STOCK_CANCEL,
							    GTK_RESPONSE_CANCEL);
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							GTK_STOCK_ADD,
							GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	xml = rb_glade_xml_new ("uri-new.glade",
				"newuri",
				dialog);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "newuri"));

	/* get the widgets from the XML */
	dialog->priv->label = glade_xml_get_widget (xml, "label");
	dialog->priv->url = glade_xml_get_widget (xml, "txt_url");
	gtk_entry_set_activates_default (GTK_ENTRY (dialog->priv->url), TRUE);

	g_signal_connect_object (G_OBJECT (dialog->priv->url),
				 "changed",
				 G_CALLBACK (rb_uri_dialog_text_changed),
				 dialog, 0);

	/* default focus */
	gtk_widget_grab_focus (dialog->priv->url);

	/* FIXME */
	gtk_widget_set_sensitive (dialog->priv->okbutton, FALSE);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_uri_dialog_finalize (GObject *object)
{
	RBURIDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_URI_DIALOG (object));

	dialog = RB_URI_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_uri_dialog_parent_class)->finalize (object);
}

GtkWidget *
rb_uri_dialog_new (const char *title, const char *label)
{
	RBURIDialog *dialog;

	dialog = g_object_new (RB_TYPE_URI_DIALOG, NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), title);
	gtk_label_set_text (GTK_LABEL (dialog->priv->label), label);

	return GTK_WIDGET (dialog);
}

static void
rb_uri_dialog_response_cb (GtkDialog *gtkdialog,
				   int response_id,
				   RBURIDialog *dialog)
{
	char *valid_url;
	char *str;

	if (response_id != GTK_RESPONSE_OK)
		return;

	str = gtk_editable_get_chars (GTK_EDITABLE (dialog->priv->url), 0, -1);
	valid_url = g_strstrip (str);

	g_signal_emit (dialog, rb_uri_dialog_signals [LOCATION_ADDED], 0, valid_url);

	g_free (str);

	gtk_widget_hide (GTK_WIDGET (gtkdialog));
}

static void
rb_uri_dialog_text_changed (GtkEditable *buffer,
				    RBURIDialog *dialog)
{
	char *text = gtk_editable_get_chars (buffer, 0, -1);
	gboolean has_text = ((text != NULL) && (*text != 0));

	g_free (text);

	gtk_widget_set_sensitive (dialog->priv->okbutton, has_text);
}
