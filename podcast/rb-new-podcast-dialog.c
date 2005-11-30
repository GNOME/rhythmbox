/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of new podcast dialog
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs.h>

#include "rb-new-podcast-dialog.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-podcast-manager.h"

static void rb_new_podcast_dialog_class_init (RBNewPodcastDialogClass *klass);
static void rb_new_podcast_dialog_init (RBNewPodcastDialog *dialog);
static void rb_new_podcast_dialog_finalize (GObject *object);
static void rb_new_podcast_dialog_set_property (GObject *object,
						guint prop_id,
						const GValue *value,
						GParamSpec *pspec);
static void rb_new_podcast_dialog_get_property (GObject *object,
						guint prop_id,
						GValue *value,
						GParamSpec *pspec);
static void rb_new_podcast_dialog_response_cb (GtkDialog *gtkdialog,
					       int response_id,
					       RBNewPodcastDialog *dialog);
static void rb_new_podcast_dialog_text_changed (GtkEditable *buffer,
						RBNewPodcastDialog *dialog);

struct RBNewPodcastDialogPrivate
{
	RBPodcastManager    *pd;

	GtkWidget   *url;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

#define RB_NEW_PODCAST_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_NEW_PODCAST_DIALOG, RBNewPodcastDialogPrivate))

enum 
{
	PROP_0,
	PROP_PODCAST_MANAGER
};

G_DEFINE_TYPE (RBNewPodcastDialog, rb_new_podcast_dialog, GTK_TYPE_DIALOG)


static void
rb_new_podcast_dialog_class_init (RBNewPodcastDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_new_podcast_dialog_set_property;
	object_class->get_property = rb_new_podcast_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_PODCAST_MANAGER,
					 g_param_spec_object ("podcast-manager",
					                      "RBPodcastManager",
					                      "RBPodcastManager object",
					                      RB_TYPE_PODCAST_MANAGER,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_new_podcast_dialog_finalize;

	g_type_class_add_private (klass, sizeof (RBNewPodcastDialogPrivate));
}

static void
rb_new_podcast_dialog_init (RBNewPodcastDialog *dialog)
{
	GladeXML *xml;

	/* create the dialog and some buttons forward - close */
	dialog->priv = RB_NEW_PODCAST_DIALOG_GET_PRIVATE (dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_new_podcast_dialog_response_cb),
				 dialog, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_title (GTK_WINDOW (dialog), _("New Podcast Feed"));

	dialog->priv->cancelbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    GTK_STOCK_CANCEL,
							    GTK_RESPONSE_CANCEL);
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							GTK_STOCK_ADD,
							GTK_RESPONSE_OK);
	
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	xml = rb_glade_xml_new ("podcast-new.glade",
				"newpodcast",
				dialog);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "newpodcast"));

	
	/* get the widgets from the XML */
	dialog->priv->url = glade_xml_get_widget (xml, "txt_url");

	g_signal_connect_object (G_OBJECT (dialog->priv->url),
				 "changed",
				 G_CALLBACK (rb_new_podcast_dialog_text_changed),
				 dialog, 0);

	/* default focus */
	gtk_widget_grab_focus (dialog->priv->url);
	
	/* FIXME */
	gtk_widget_set_sensitive (dialog->priv->okbutton, FALSE);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_new_podcast_dialog_finalize (GObject *object)
{
	RBNewPodcastDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NEW_PODCAST_DIALOG (object));

	dialog = RB_NEW_PODCAST_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_new_podcast_dialog_parent_class)->finalize (object);
}

static void
rb_new_podcast_dialog_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec)
{
	RBNewPodcastDialog *dialog = RB_NEW_PODCAST_DIALOG (object);

	switch (prop_id)
	{
	case PROP_PODCAST_MANAGER:
		dialog->priv->pd = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_new_podcast_dialog_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec)
{
	RBNewPodcastDialog *dialog = RB_NEW_PODCAST_DIALOG (object);

	switch (prop_id)
	{
	case PROP_PODCAST_MANAGER:
		g_value_set_object (value, dialog->priv->pd);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_new_podcast_dialog_new (RBPodcastManager *pd)
{
	RBNewPodcastDialog *dialog;
	
	g_return_val_if_fail (RB_PODCAST_MANAGER (pd), NULL);
	
	dialog = g_object_new (RB_TYPE_NEW_PODCAST_DIALOG, "podcast-manager", pd, NULL);

	g_return_val_if_fail (dialog->priv != NULL, NULL);

	return GTK_WIDGET (dialog);
}

static void
rb_new_podcast_dialog_response_cb (GtkDialog *gtkdialog,
				   int response_id,
				   RBNewPodcastDialog *dialog)
{
	gchar *valid_url;
	gchar *str;

	if (response_id != GTK_RESPONSE_OK)
		return;

	str = gtk_editable_get_chars (GTK_EDITABLE (dialog->priv->url), 0, -1);

	valid_url = g_strstrip (str);

	rb_podcast_manager_subscribe_feed (dialog->priv->pd, valid_url);

	gtk_widget_hide (GTK_WIDGET (gtkdialog));	

	g_free (str);
}

static void
rb_new_podcast_dialog_text_changed (GtkEditable *buffer,
				    RBNewPodcastDialog *dialog)
{
	char *text = gtk_editable_get_chars (buffer, 0, -1);
	gboolean has_text = ((text != NULL) && (*text != 0));

	gtk_widget_set_sensitive (dialog->priv->okbutton, has_text);
	g_free (text);
}

