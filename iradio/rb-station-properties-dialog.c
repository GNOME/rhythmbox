/* 
 *  arch-tag: Implementation of internet radio station properties dialog
 *
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
#include <gtk/gtktable.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtkbox.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkcellrenderer.h>
#include <gtk/gtkcellrenderertext.h>
#include <glade/glade.h>
#include <string.h>
#include <time.h>

#include "rb-station-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"

static void rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass);
static void rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_finalize (GObject *object);
static void rb_station_properties_dialog_set_property (GObject *object, 
						       guint prop_id,
						       const GValue *value, 
						       GParamSpec *pspec);
static void rb_station_properties_dialog_get_property (GObject *object, 
						       guint prop_id,
						       GValue *value, 
						       GParamSpec *pspec);
static gboolean rb_station_properties_dialog_get_current_node (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_response_cb (GtkDialog *gtkdialog,
						      int response_id,
						      RBStationPropertiesDialog *dialog);

static void rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_rated_cb (RBRating *rating,
						   int score,
						   RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog);

struct RBStationPropertiesDialogPrivate
{
	RBNodeView *node_view;
	RBNode *current_node;
	RBIRadioBackend *backend;

	GtkWidget   *title;
	GtkWidget   *genre;
	GtkWidget   *location;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *rating;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

enum 
{
	PROP_0,
	PROP_NODE_VIEW,
	PROP_BACKEND
};

static GObjectClass *parent_class = NULL;

GType
rb_station_properties_dialog_get_type (void)
{
	static GType rb_station_properties_dialog_type = 0;

	if (rb_station_properties_dialog_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBStationPropertiesDialogClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_station_properties_dialog_class_init,
			NULL,
			NULL,
			sizeof (RBStationPropertiesDialog),
			0,
			(GInstanceInitFunc) rb_station_properties_dialog_init
		};
		
		rb_station_properties_dialog_type = g_type_register_static (GTK_TYPE_DIALOG,
									    "RBStationPropertiesDialog",
									    &our_info, 0);
	}

	return rb_station_properties_dialog_type;
}

static void
rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_station_properties_dialog_set_property;
	object_class->get_property = rb_station_properties_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_NODE_VIEW,
					 g_param_spec_object ("node-view",
					                      "RBNodeView",
					                      "RBNodeView object",
					                      RB_TYPE_NODE_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_BACKEND,
					 g_param_spec_object ("backend",
					                      "RBIRadioBackend",
					                      "RBIRadioBackend object",
					                      RB_TYPE_IRADIO_BACKEND,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_station_properties_dialog_finalize;
}

static void
rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog)
{
	GladeXML *xml;
	
	dialog->priv = g_new0 (RBStationPropertiesDialogPrivate, 1);
	
	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (rb_station_properties_dialog_response_cb),
			  dialog);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_title (GTK_WINDOW (dialog), _("%s Properties"));

	xml = rb_glade_xml_new ("station-properties.glade",
				"stationproperties",
				dialog);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "stationproperties"));
	dialog->priv->cancelbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    GTK_STOCK_CANCEL,
							    GTK_RESPONSE_CANCEL);
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							GTK_STOCK_OK,
							GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	/* get the widgets from the XML */
	dialog->priv->title = glade_xml_get_widget (xml, "titleEntry");
	dialog->priv->genre = glade_xml_get_widget (xml, "genreEntry");
	dialog->priv->location = glade_xml_get_widget (xml, "locationEntry");

	dialog->priv->lastplayed = glade_xml_get_widget (xml, "lastplayedLabel");
	dialog->priv->playcount = glade_xml_get_widget (xml, "playcountLabel");

	dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (dialog->priv->rating, 
				 "rated",
				 G_CALLBACK (rb_station_properties_dialog_rated_cb),
				 G_OBJECT (dialog), 0);
	gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (xml, "ratingVBox")),
			   dialog->priv->rating);
	g_object_unref (G_OBJECT (xml));
}

static void
rb_station_properties_dialog_finalize (GObject *object)
{
	RBStationPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (object));

	dialog = RB_STATION_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	g_free (dialog->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_station_properties_dialog_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (object);

	switch (prop_id)
	{
	case PROP_NODE_VIEW:
		dialog->priv->node_view = g_value_get_object (value);
		break;
	case PROP_BACKEND:
		dialog->priv->backend = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_station_properties_dialog_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (object);

	switch (prop_id)
	{
	case PROP_NODE_VIEW:
		g_value_set_object (value, dialog->priv->node_view);
		break;
	case PROP_BACKEND:
		g_value_set_object (value, dialog->priv->backend);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_station_properties_dialog_new (RBNodeView *node_view, RBIRadioBackend *backend)
{
	RBStationPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_NODE_VIEW (node_view), NULL);
	g_return_val_if_fail (RB_IS_IRADIO_BACKEND (backend), NULL);

	dialog = g_object_new (RB_TYPE_STATION_PROPERTIES_DIALOG,
			       "node-view", node_view,
			       "backend", backend, NULL);
	g_return_val_if_fail (dialog->priv != NULL, NULL);

	if (!rb_station_properties_dialog_get_current_node (dialog))
	{
		g_object_unref (G_OBJECT (dialog));
		return NULL;
	}
	rb_station_properties_dialog_update (dialog);

	return GTK_WIDGET (dialog);
}

static void
rb_station_properties_dialog_response_cb (GtkDialog *gtkdialog,
					  int response_id,
					  RBStationPropertiesDialog *dialog)
{
	if (response_id != GTK_RESPONSE_OK)
		goto cleanup;
	rb_station_properties_dialog_sync_entries (dialog);
cleanup:
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
rb_station_properties_dialog_get_current_node (RBStationPropertiesDialog *dialog)
{
	GList *selected_nodes;
	
	/* get the node */
	selected_nodes = rb_node_view_get_selection (dialog->priv->node_view);

	if ((selected_nodes == NULL) ||
	    (selected_nodes->data == NULL) ||
	    (RB_IS_NODE (selected_nodes->data) == FALSE))
	{
		dialog->priv->current_node = NULL;
		return FALSE;
	}

	dialog->priv->current_node = selected_nodes->data;
	return TRUE;
}

static void
rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_node != NULL);
	g_return_if_fail (RB_IS_NODE (dialog->priv->current_node));
	rb_station_properties_dialog_update_location (dialog);
	rb_station_properties_dialog_update_title (dialog);
	rb_station_properties_dialog_update_title_entry (dialog);
	rb_station_properties_dialog_update_genre (dialog);
	rb_station_properties_dialog_update_play_count (dialog);
	rb_station_properties_dialog_update_last_played (dialog);
	rb_station_properties_dialog_update_rating (dialog);
}

static void
rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog)
{
	const char *name, *tmp;
	name = rb_node_get_property_string (dialog->priv->current_node,
					    RB_NODE_PROP_NAME);
	tmp = g_strdup_printf (_("Properties for %s"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free ((char*) tmp);
}

static void
rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog)
{
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->title),
			    rb_node_get_property_string (dialog->priv->current_node,
							 RB_NODE_PROP_NAME));
}

static void
rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog)
{
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->genre),
			    rb_node_get_property_string (dialog->priv->current_node,
							 RB_NODE_PROP_GENRE));
}

static void
rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog)
{
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->location),
			    rb_node_get_property_string (dialog->priv->current_node,
							 RB_NODE_PROP_LOCATION));
}

static void
rb_station_properties_dialog_rated_cb (RBRating *rating,
				       int score,
				       RBStationPropertiesDialog *dialog)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (RB_IS_NODE (dialog->priv->current_node));
	g_return_if_fail (score >= 0 && score <= 5 );

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, score);
	rb_node_set_property (dialog->priv->current_node,
			      RB_NODE_PROP_RATING,
			      &value);
	g_value_unset (&value);

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "score", score,
		      NULL);
}

static void
rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog)
{
	char *text = g_strdup_printf ("%d", rb_node_get_property_int (dialog->priv->current_node,
								      RB_NODE_PROP_PLAY_COUNT));
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->lastplayed),
			    rb_node_get_property_string (dialog->priv->current_node,
							 RB_NODE_PROP_LAST_PLAYED_STR));
}

static void
rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	if (rb_node_get_property (dialog->priv->current_node,
				  RB_NODE_PROP_RATING,
				  &value) == FALSE) {
		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, 0);
	}

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "score", g_value_get_int (&value),
		      NULL);

	g_value_unset (&value);
}

static void
rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog)
{
	const char *title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *location = gtk_entry_get_text (GTK_ENTRY (dialog->priv->location));
	GValue val = {0,};

	g_return_if_fail (RB_IS_NODE (dialog->priv->current_node));

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, title);
	rb_node_set_property (dialog->priv->current_node, RB_NODE_PROP_NAME, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, genre);
	rb_node_set_property (dialog->priv->current_node, RB_NODE_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, location);
	rb_node_set_property (dialog->priv->current_node, RB_NODE_PROP_LOCATION, &val);
	g_value_unset (&val);
}
