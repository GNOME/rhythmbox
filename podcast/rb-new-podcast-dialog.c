/*
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
#include <libgnomevfs/gnome-vfs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkbox.h>
#include <gtk/gtktable.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktextview.h>
#include <gtk/gtktextbuffer.h>
#include <gtk/gtkmessagedialog.h>
#include <glade/glade.h>
#include <string.h>
#include <time.h>

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
static gboolean rb_new_podcast_dialog_text_changed_cb (GtkTextView *text,
		                                       GdkEventKey *event,
						       RBNewPodcastDialog *dialog);
//static gpointer rb_new_podcast_dialog_copy_file(gpointer data);


struct RBNewPodcastDialogPrivate
{
	RBPodcastManager    *pd;

	GtkWidget   *url;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

enum 
{
	PROP_0,
	PROP_PODCAST_MANAGER
};

static GObjectClass *parent_class = NULL;

GType
rb_new_podcast_dialog_get_type (void)
{
	static GType rb_new_podcast_dialog_type = 0;

	if (rb_new_podcast_dialog_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNewPodcastDialogClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_new_podcast_dialog_class_init,
			NULL,
			NULL,
			sizeof (RBNewPodcastDialog),
			0,
			(GInstanceInitFunc) rb_new_podcast_dialog_init
		};

		rb_new_podcast_dialog_type = g_type_register_static (GTK_TYPE_DIALOG,
								     "RBNewPodcastDialog",
								     &our_info, 0);
	}

	return rb_new_podcast_dialog_type;
}

static void
rb_new_podcast_dialog_class_init (RBNewPodcastDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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
}

static void
rb_new_podcast_dialog_init (RBNewPodcastDialog *dialog)
{
	GladeXML *xml;

	/* create the dialog and some buttons forward - close */
	dialog->priv = g_new0 (RBNewPodcastDialogPrivate, 1);


	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_new_podcast_dialog_response_cb),
				 dialog, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_title (GTK_WINDOW (dialog), _("New Podcast"));

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
				 "key-release-event",
				 G_CALLBACK (rb_new_podcast_dialog_text_changed_cb),
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

	g_free (dialog->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
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
	GtkTextIter begin, end;
	const gchar *str;
	GtkTextBuffer *buffer;

	if (response_id != GTK_RESPONSE_OK)
		return;

	
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->priv->url));
	gtk_text_buffer_get_start_iter (buffer, &begin);
	gtk_text_buffer_get_end_iter (buffer, &end);
	
	str = gtk_text_buffer_get_text (buffer, &begin, &end, TRUE);	


	valid_url = g_strstrip ( g_strdup (str));

	rb_podcast_manager_subscribe_feed (dialog->priv->pd, valid_url);
	gtk_widget_hide(GTK_WIDGET(gtkdialog));	
	g_free (valid_url);
	
}

static gboolean 
rb_new_podcast_dialog_text_changed_cb (GtkTextView *text,
                                       GdkEventKey *event,
				       RBNewPodcastDialog *dialog)
{
	GtkTextIter begin, end;
	const gchar *str;
	
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (text);
	gtk_text_buffer_get_start_iter (buffer, &begin);
	gtk_text_buffer_get_end_iter (buffer, &end);
	
	str = gtk_text_buffer_get_text (buffer, &begin, &end, TRUE);	
	
	gtk_widget_set_sensitive (dialog->priv->okbutton,
				  g_utf8_strlen (str, -1) > 0);

	return TRUE;
}
