/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "rb-uri-dialog.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"

/**
 * SECTION:rburidialog
 * @short_description: simple URI entry dialog
 * @include: rb-uri-dialog.h
 *
 * A simple dialog used to request a single URI from the user.
 */

static void rb_uri_dialog_class_init (RBURIDialogClass *klass);
static void rb_uri_dialog_init (RBURIDialog *dialog);
static void rb_uri_dialog_response_cb (GtkDialog *gtkdialog,
				       int response_id,
				       RBURIDialog *dialog);
static void rb_uri_dialog_text_changed (GtkEditable *buffer,
					RBURIDialog *dialog);
static void rb_uri_dialog_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_uri_dialog_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);
static void rb_uri_dialog_clipboard_yank_url (GtkClipboard *clipboard,
					      const char *text,
					      gpointer data);

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

enum
{
	PROP_0,
	PROP_LABEL
};

static guint rb_uri_dialog_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBURIDialog, rb_uri_dialog, GTK_TYPE_DIALOG)

static void
rb_uri_dialog_class_init (RBURIDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->set_property = rb_uri_dialog_set_property;
	object_class->get_property = rb_uri_dialog_get_property;

	/**
	 * RBURIDialog:label:
	 *
	 * The label displayed in the dialog.
	 */
	g_object_class_install_property (object_class,
					 PROP_LABEL,
					 g_param_spec_string ("label",
					                      "label",
					                      "label",
							      "",
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBURIDialog::location-added:
	 * @dialog: the #RBURIDialog
	 * @uri: URI entered
	 *
	 * Emitted when the user has entered a URI into the dialog.
	 */
	rb_uri_dialog_signals [LOCATION_ADDED] =
		g_signal_new ("location-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBURIDialogClass, location_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBURIDialogPrivate));
}

static void
rb_uri_dialog_init (RBURIDialog *dialog)
{
	GtkWidget  *content_area;
	GtkBuilder *builder;

	/* create the dialog and some buttons forward - close */
	dialog->priv = RB_URI_DIALOG_GET_PRIVATE (dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_uri_dialog_response_cb),
				 dialog, 0);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	dialog->priv->cancelbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    _("_Cancel"),
							    GTK_RESPONSE_CANCEL);
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							_("_Add"),
							GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	builder = rb_builder_load ("uri-new.ui", dialog);

	gtk_container_add (GTK_CONTAINER (content_area),
			   GTK_WIDGET (gtk_builder_get_object (builder, "newuri")));

	/* get the widgets from the GtkBuilder */
	dialog->priv->label = GTK_WIDGET (gtk_builder_get_object (builder, "label"));
	dialog->priv->url = GTK_WIDGET (gtk_builder_get_object (builder, "txt_url"));
	gtk_entry_set_activates_default (GTK_ENTRY (dialog->priv->url), TRUE);

	g_signal_connect_object (G_OBJECT (dialog->priv->url),
				 "changed",
				 G_CALLBACK (rb_uri_dialog_text_changed),
				 dialog, 0);

	/* if we can get a url from the clipboard, populate the entry with that,
	 * since there's a good chance that's what the user wants to do anyway.
	 */
	gtk_clipboard_request_text (gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
				    rb_uri_dialog_clipboard_yank_url,
				    dialog);
	gtk_clipboard_request_text (gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				    rb_uri_dialog_clipboard_yank_url,
				    dialog);

	/* default focus */
	gtk_widget_grab_focus (dialog->priv->url);

	/* FIXME */
	gtk_widget_set_sensitive (dialog->priv->okbutton, FALSE);

	g_object_unref (builder);
}

static void
rb_uri_dialog_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBURIDialog *dialog = RB_URI_DIALOG (object);

	switch (prop_id) {
	case PROP_LABEL:
		gtk_label_set_text (GTK_LABEL (dialog->priv->label), g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_uri_dialog_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBURIDialog *dialog = RB_URI_DIALOG (object);

	switch (prop_id) {
	case PROP_LABEL:
		g_value_set_string (value, gtk_label_get_text (GTK_LABEL (dialog->priv->label)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_uri_dialog_new:
 * @title: Window title for the dialog
 * @label: Label to display in the dialog
 *
 * Creates a URI entry dialog.
 *
 * Returns: URI dialog instance.
 */
GtkWidget *
rb_uri_dialog_new (const char *title, const char *label)
{
	RBURIDialog *dialog;

	dialog = g_object_new (RB_TYPE_URI_DIALOG,
			       "title", title,
			       "label", label,
			       NULL);
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

static void
rb_uri_dialog_clipboard_yank_url (GtkClipboard *clipboard, const char *text, gpointer data)
{
	RBURIDialog *dialog = RB_URI_DIALOG (data);
	GUri *uri;
	const char *scheme;

	if (text == NULL) {
		return;
	}

	uri = g_uri_parse (text, SOUP_HTTP_URI_FLAGS, NULL);
	if (uri == NULL) {
		rb_debug ("did not autofill from clipboard: not a valid URL");
		return;
	}

	scheme = g_uri_get_scheme (uri);
	if ((g_strcmp0 (scheme, "http") == 0) || (g_strcmp0 (scheme, "https") == 0)) {
		gtk_entry_set_text (GTK_ENTRY (dialog->priv->url), text);
		gtk_editable_select_region (GTK_EDITABLE (dialog->priv->url), 0, -1);
	}

	g_uri_unref (uri);
}
