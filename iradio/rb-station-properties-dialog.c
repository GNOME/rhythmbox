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

#include "monkey-media-audio-quality.h"
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
static gboolean rb_station_properties_dialog_get_current_entry (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_response_cb (GtkDialog *gtkdialog,
						      int response_id,
						      RBStationPropertiesDialog *dialog);

static void rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_quality (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_rated_cb (RBRating *rating,
						   int score,
						   RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog);

struct RBStationPropertiesDialogPrivate
{
	RBEntryView *entry_view;
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *genre;
	GtkWidget   *location;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *quality;
	GtkWidget   *rating;
	GtkWidget   *okbutton;
	GtkWidget   *cancelbutton;
};

enum 
{
	PROP_0,
	PROP_ENTRY_VIEW,
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
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
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
	dialog->priv->quality = glade_xml_get_widget (xml, "qualityLabel");

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
	case PROP_ENTRY_VIEW:
		dialog->priv->entry_view = g_value_get_object (value);
		g_object_get (G_OBJECT (dialog->priv->entry_view), "db",
			      &dialog->priv->db, NULL);
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
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, dialog->priv->entry_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_station_properties_dialog_new (RBEntryView *entry_view)
{
	RBStationPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	dialog = g_object_new (RB_TYPE_STATION_PROPERTIES_DIALOG,
			       "entry-view", entry_view, NULL);

	if (!rb_station_properties_dialog_get_current_entry (dialog)) {
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
rb_station_properties_dialog_get_current_entry (RBStationPropertiesDialog *dialog)
{
	GList *selected_entries;
	
	/* get the entry */
	selected_entries = rb_entry_view_get_selected_entries (dialog->priv->entry_view);

	if ((selected_entries == NULL) ||
	    (selected_entries->data == NULL)) {
		dialog->priv->current_entry = NULL;
		return FALSE;
	}

	dialog->priv->current_entry = selected_entries->data;
	return TRUE;
}

static void
rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_entry != NULL);
	rb_station_properties_dialog_update_location (dialog);
	rb_station_properties_dialog_update_title (dialog);
	rb_station_properties_dialog_update_title_entry (dialog);
	rb_station_properties_dialog_update_genre (dialog);
	rb_station_properties_dialog_update_play_count (dialog);
	rb_station_properties_dialog_update_quality (dialog);
	rb_station_properties_dialog_update_last_played (dialog);
	rb_station_properties_dialog_update_rating (dialog);
}

static void
rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;	
	rhythmdb_read_lock (dialog->priv->db);
	name = rhythmdb_entry_get_string (dialog->priv->db,
					  dialog->priv->current_entry,
					  RHYTHMDB_PROP_TITLE);
	rhythmdb_read_unlock (dialog->priv->db);
	tmp = g_strdup_printf (_("Properties for %s"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);
}

static void
rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog)
{
	const char *tmp;

	rhythmdb_read_lock (dialog->priv->db);
	tmp = rhythmdb_entry_get_string (dialog->priv->db,
					 dialog->priv->current_entry,
					 RHYTHMDB_PROP_TITLE);
	rhythmdb_read_unlock (dialog->priv->db);
	
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->title), tmp);
}

static void
rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog)
{
	const char *tmp;

	rhythmdb_read_lock (dialog->priv->db);
	tmp = rhythmdb_entry_get_string (dialog->priv->db,
					 dialog->priv->current_entry,
					 RHYTHMDB_PROP_GENRE);
	rhythmdb_read_unlock (dialog->priv->db);
	
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->genre), tmp);
}

static void
rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog)
{
	const char *tmp;

	rhythmdb_read_lock (dialog->priv->db);
	tmp = rhythmdb_entry_get_string (dialog->priv->db,
					 dialog->priv->current_entry,
					 RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (dialog->priv->db);
	
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->location), tmp);
}

static void
rb_station_properties_dialog_rated_cb (RBRating *rating,
				       int score,
				       RBStationPropertiesDialog *dialog)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (score >= 0 && score <= 5 );

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, score);
	rhythmdb_write_lock (dialog->priv->db);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	rhythmdb_write_unlock (dialog->priv->db);
	g_value_unset (&value);

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "score", score,
		      NULL);
}

static void
rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog)
{
	int val;
	char *text;

	rhythmdb_read_lock (dialog->priv->db);
	val = rhythmdb_entry_get_int (dialog->priv->db,
				      dialog->priv->current_entry,
				      RHYTHMDB_PROP_PLAY_COUNT);
	rhythmdb_read_unlock (dialog->priv->db);
	text = g_strdup_printf ("%d", val);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_quality (RBStationPropertiesDialog *dialog)
{
	MonkeyMediaAudioQuality val;
	char *text;

	rhythmdb_read_lock (dialog->priv->db);
	val = rhythmdb_entry_get_int (dialog->priv->db,
				      dialog->priv->current_entry,
				      RHYTHMDB_PROP_QUALITY);
	rhythmdb_read_unlock (dialog->priv->db);
	if (val > 0)
		text = monkey_media_audio_quality_to_string (val);
	else
		text = g_strdup (_("Unknown"));

	gtk_label_set_text (GTK_LABEL (dialog->priv->quality), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog)
{
	const char *tmp;

	rhythmdb_read_lock (dialog->priv->db);
	tmp = rhythmdb_entry_get_string (dialog->priv->db,
					 dialog->priv->current_entry,
					 RHYTHMDB_PROP_LAST_PLAYED_STR);
	rhythmdb_read_unlock (dialog->priv->db);
	
	gtk_label_set (GTK_LABEL (dialog->priv->lastplayed), tmp);
}

static void
rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog)
{
	guint rating;

	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	rhythmdb_read_lock (dialog->priv->db);
	rating = rhythmdb_entry_get_int (dialog->priv->db,
					 dialog->priv->current_entry,
					 RHYTHMDB_PROP_RATING);
	rhythmdb_read_unlock (dialog->priv->db);

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "score", rating, NULL);
}

static void
rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog)
{
	const char *title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *location = gtk_entry_get_text (GTK_ENTRY (dialog->priv->location));
	GValue val = {0,};

	rhythmdb_write_lock (dialog->priv->db);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, title);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, genre);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, location);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION, &val);
	g_value_unset (&val);
	rhythmdb_write_unlock (dialog->priv->db);
}
