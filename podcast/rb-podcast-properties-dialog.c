/* 
 *  arch-tag: Headfile of podcast properties dialog
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org>
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

#include "rb-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"

static void rb_podcast_properties_dialog_class_init (RBPodcastPropertiesDialogClass *klass);
static void rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_finalize (GObject *object);
static void rb_podcast_properties_dialog_set_property (GObject *object, 
						       guint prop_id,
						       const GValue *value, 
						       GParamSpec *pspec);
static void rb_podcast_properties_dialog_get_property (GObject *object, 
						       guint prop_id,
						       GValue *value, 
						       GParamSpec *pspec);
static gboolean rb_podcast_properties_dialog_get_current_entry (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_response_cb (GtkDialog *gtkdialog,
						      int response_id,
						      RBPodcastPropertiesDialog *dialog);

static void rb_podcast_properties_dialog_update (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_play_count (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_bitrate (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_last_played (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_rating (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_date (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_description (RBPodcastPropertiesDialog *dialog);
static gchar* rb_podcast_properties_dialog_parse_time (gulong time);
static void rb_podcast_properties_dialog_rated_cb (RBRating *rating,
						   double score,
						   RBPodcastPropertiesDialog *dialog);
static GtkWidget * boldify_label (GtkWidget *label);

struct RBPodcastPropertiesDialogPrivate
{
	RBEntryView *entry_view;
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *feed;
	GtkWidget   *location;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *bitrate;
	GtkWidget   *rating;
	GtkWidget   *date;
	GtkWidget   *description;
	
	GtkWidget   *okbutton;
};

enum 
{
	PROP_0,
	PROP_ENTRY_VIEW,
	PROP_BACKEND
};

G_DEFINE_TYPE (RBPodcastPropertiesDialog, rb_podcast_properties_dialog, GTK_TYPE_DIALOG)


static void
rb_podcast_properties_dialog_class_init (RBPodcastPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_podcast_properties_dialog_set_property;
	object_class->get_property = rb_podcast_properties_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_podcast_properties_dialog_finalize;
}

static void
rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog)
{
	GladeXML *xml;
	
	dialog->priv = g_new0 (RBPodcastPropertiesDialogPrivate, 1);
	
	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_podcast_properties_dialog_response_cb),
				 dialog, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);

	xml = rb_glade_xml_new ("podcast-properties.glade",
				"podcastproperties",
				dialog);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "podcastproperties"));
	dialog->priv->okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog),
							GTK_STOCK_OK,
							GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	/* get the widgets from the XML */
	dialog->priv->title = glade_xml_get_widget (xml, "titleLabel");
	dialog->priv->feed = glade_xml_get_widget (xml, "feedLabel");
	dialog->priv->location = glade_xml_get_widget (xml, "locationLabel");
	dialog->priv->lastplayed = glade_xml_get_widget (xml, "lastplayedLabel");
	dialog->priv->playcount = glade_xml_get_widget (xml, "playcountLabel");
	dialog->priv->bitrate = glade_xml_get_widget (xml, "bitrateLabel");
	dialog->priv->date = glade_xml_get_widget (xml, "dateLabel");
	dialog->priv->description = glade_xml_get_widget (xml, "descriptionLabel");

	boldify_label (glade_xml_get_widget (xml, "titleDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "feedDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "locationDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "ratingDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "lastplayedDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "playcountDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "bitrateDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "dateDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "descriptionDescLabel"));

	dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (dialog->priv->rating, 
				 "rated",
				 G_CALLBACK (rb_podcast_properties_dialog_rated_cb),
				 G_OBJECT (dialog), 0);
	gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (xml, "ratingVBox")),
			   dialog->priv->rating);
	g_object_unref (G_OBJECT (xml));
}

static void
rb_podcast_properties_dialog_finalize (GObject *object)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	g_free (dialog->priv);

	G_OBJECT_CLASS (rb_podcast_properties_dialog_parent_class)->finalize (object);
}

static void
rb_podcast_properties_dialog_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBPodcastPropertiesDialog *dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

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
rb_podcast_properties_dialog_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBPodcastPropertiesDialog *dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

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
rb_podcast_properties_dialog_new (RBEntryView *entry_view)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	dialog = g_object_new (RB_TYPE_PODCAST_PROPERTIES_DIALOG,
			       "entry-view", entry_view, NULL);

	if (!rb_podcast_properties_dialog_get_current_entry (dialog)) {
		g_object_unref (G_OBJECT (dialog));
		return NULL;
	}
	rb_podcast_properties_dialog_update (dialog);

	return GTK_WIDGET (dialog);
}

static GtkWidget *
boldify_label (GtkWidget *label)
{
	gchar *str_final;
	str_final = g_strdup_printf ("<b>%s</b>",
				     gtk_label_get_label (GTK_LABEL (label)));
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
	g_free (str_final);
	return label;
}

static void
rb_podcast_properties_dialog_response_cb (GtkDialog *gtkdialog,
					  int response_id,
					  RBPodcastPropertiesDialog *dialog)
{
	if (response_id != GTK_RESPONSE_OK)
		goto cleanup;
cleanup:
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
rb_podcast_properties_dialog_get_current_entry (RBPodcastPropertiesDialog *dialog)
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
rb_podcast_properties_dialog_update (RBPodcastPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_entry != NULL);
	rb_podcast_properties_dialog_update_location (dialog);
	rb_podcast_properties_dialog_update_title (dialog);
	rb_podcast_properties_dialog_update_title_label (dialog);
	rb_podcast_properties_dialog_update_feed (dialog);
	rb_podcast_properties_dialog_update_play_count (dialog);
	rb_podcast_properties_dialog_update_bitrate (dialog);
	rb_podcast_properties_dialog_update_last_played (dialog);
	rb_podcast_properties_dialog_update_rating (dialog);
	rb_podcast_properties_dialog_update_date (dialog);
	rb_podcast_properties_dialog_update_description (dialog);
}

static void
rb_podcast_properties_dialog_update_title (RBPodcastPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;	
	name = rb_refstring_get (dialog->priv->current_entry->title);
	tmp = g_strdup_printf (_("Properties for %s"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);
}

static void
rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->title),
			    rb_refstring_get (dialog->priv->current_entry->title));
}

static void
rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->feed),
			    rb_refstring_get (dialog->priv->current_entry->podcast->subtitle));
}

static void
rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog)
{
	char *unescaped;
	unescaped = gnome_vfs_unescape_string_for_display (dialog->priv->current_entry->location);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_podcast_properties_dialog_rated_cb (RBRating *rating,
				       double score,
				       RBPodcastPropertiesDialog *dialog)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (score >= 0 && score <= 5 );

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, score);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	rhythmdb_commit (dialog->priv->db);
	g_value_unset (&value);

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "rating", score,
		      NULL);
}

static void
rb_podcast_properties_dialog_update_play_count (RBPodcastPropertiesDialog *dialog)
{
	char *text;
	text = g_strdup_printf ("%ld", dialog->priv->current_entry->play_count);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_podcast_properties_dialog_update_bitrate (RBPodcastPropertiesDialog *dialog)
{
	guint val;
	char *text;

	val = dialog->priv->current_entry->bitrate;
	if (val == 0)
		text = g_strdup (_("Unknown"));
	else
		text = g_strdup_printf ("%d", val);

	gtk_label_set_text (GTK_LABEL (dialog->priv->bitrate), text);
	g_free (text);
}

static void
rb_podcast_properties_dialog_update_last_played (RBPodcastPropertiesDialog *dialog)
{
	gtk_label_set (GTK_LABEL (dialog->priv->lastplayed),
		       rb_refstring_get (dialog->priv->current_entry->last_played_str));
}

static void
rb_podcast_properties_dialog_update_rating (RBPodcastPropertiesDialog *dialog)
{
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (dialog));

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "rating", dialog->priv->current_entry->rating, NULL);
}

static void
rb_podcast_properties_dialog_update_date (RBPodcastPropertiesDialog *dialog)
{
	gchar *time = rb_podcast_properties_dialog_parse_time (dialog->priv->current_entry->podcast->post_time);
	time[strlen (time) - 1] = '\0';
	gtk_label_set (GTK_LABEL (dialog->priv->date),
			time);
//_free (time);

}

static void
rb_podcast_properties_dialog_update_description (RBPodcastPropertiesDialog *dialog)
{
	gtk_label_set (GTK_LABEL (dialog->priv->description),
		       rb_refstring_get (dialog->priv->current_entry->podcast->description));
}

static gchar*
rb_podcast_properties_dialog_parse_time (gulong value)
{
	time_t result = (time_t) value;
	return asctime(localtime(&result));
}
