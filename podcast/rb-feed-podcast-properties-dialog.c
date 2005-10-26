/* 
 *  arch-tag: Headfile of feed_podcast feed properties dialog
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

#include "rb-feed-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"

static void rb_feed_podcast_properties_dialog_class_init (RBFeedPodcastPropertiesDialogClass *klass);
static void rb_feed_podcast_properties_dialog_init (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_finalize (GObject *object);
static void rb_feed_podcast_properties_dialog_update_title (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_title_label (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_location (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_response_cb (GtkDialog *gtkdialog,
						      int response_id,
						      RBFeedPodcastPropertiesDialog *dialog);

static void rb_feed_podcast_properties_dialog_update (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_author (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_language (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_last_update (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog);
static gchar* rb_feed_podcast_properties_dialog_parse_time (gulong time);

static GtkWidget * boldify_label (GtkWidget *label);

struct RBFeedPodcastPropertiesDialogPrivate
{
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *author;
	GtkWidget   *location;
	GtkWidget   *language;
	GtkWidget   *last_update;
	GtkWidget   *copyright;
	GtkWidget   *summary;
	
	GtkWidget   *okbutton;
};

enum 
{
	PROP_0,
	PROP_BACKEND
};

G_DEFINE_TYPE (RBFeedPodcastPropertiesDialog, rb_feed_podcast_properties_dialog, GTK_TYPE_DIALOG)


static void
rb_feed_podcast_properties_dialog_class_init (RBFeedPodcastPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_feed_podcast_properties_dialog_finalize;
}

static void
rb_feed_podcast_properties_dialog_init (RBFeedPodcastPropertiesDialog *dialog)
{
	GladeXML *xml;
	
	dialog->priv = g_new0 (RBFeedPodcastPropertiesDialogPrivate, 1);
	
	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_feed_podcast_properties_dialog_response_cb),
				 dialog, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);

	xml = rb_glade_xml_new ("podcast-feed-properties.glade",
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
	dialog->priv->author = glade_xml_get_widget (xml, "authorLabel");
	dialog->priv->location = glade_xml_get_widget (xml, "locationLabel");
	dialog->priv->language = glade_xml_get_widget (xml, "languageLabel");
	dialog->priv->last_update = glade_xml_get_widget (xml, "lastupdateLabel");
	dialog->priv->copyright = glade_xml_get_widget (xml, "copyrightLabel");
	dialog->priv->summary = glade_xml_get_widget (xml, "summaryLabel");

	boldify_label (glade_xml_get_widget (xml, "titleDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "authorDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "locationDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "languageDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "lastupdateDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "copyrightDescLabel"));
	boldify_label (glade_xml_get_widget (xml, "summaryDescLabel"));

	g_object_unref (G_OBJECT (xml));
}

static void
rb_feed_podcast_properties_dialog_finalize (GObject *object)
{
	RBFeedPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_FEED_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_FEED_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	g_free (dialog->priv);

	G_OBJECT_CLASS (rb_feed_podcast_properties_dialog_parent_class)->finalize (object);
}

GtkWidget *
rb_feed_podcast_properties_dialog_new (RhythmDBEntry *entry)
{
	RBFeedPodcastPropertiesDialog *dialog;

	dialog = g_object_new (RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, NULL);
	dialog->priv->current_entry = entry;

	rb_feed_podcast_properties_dialog_update (dialog);

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
rb_feed_podcast_properties_dialog_response_cb (GtkDialog *gtkdialog,
					  int response_id,
					  RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
rb_feed_podcast_properties_dialog_update (RBFeedPodcastPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_entry != NULL);
	rb_feed_podcast_properties_dialog_update_location (dialog);
	rb_feed_podcast_properties_dialog_update_title (dialog);
	rb_feed_podcast_properties_dialog_update_title_label (dialog);
	rb_feed_podcast_properties_dialog_update_author (dialog);
	rb_feed_podcast_properties_dialog_update_language (dialog);
	rb_feed_podcast_properties_dialog_update_last_update (dialog);
	rb_feed_podcast_properties_dialog_update_copyright (dialog);
	rb_feed_podcast_properties_dialog_update_summary (dialog);
}

static void
rb_feed_podcast_properties_dialog_update_title (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;	
	name = rb_refstring_get (dialog->priv->current_entry->title);
	tmp = g_strdup_printf (_("Properties for %s"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);
}

static void
rb_feed_podcast_properties_dialog_update_title_label (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->title),
			    rb_refstring_get (dialog->priv->current_entry->title));
}

static void
rb_feed_podcast_properties_dialog_update_author (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->author),
			    rb_refstring_get (dialog->priv->current_entry->artist));
}

static void
rb_feed_podcast_properties_dialog_update_location (RBFeedPodcastPropertiesDialog *dialog)
{
	char *unescaped;
	unescaped = gnome_vfs_unescape_string_for_display (dialog->priv->current_entry->location);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->copyright),
			    rb_refstring_get (dialog->priv->current_entry->podcast->copyright));
}

static void
rb_feed_podcast_properties_dialog_update_language (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set_text (GTK_LABEL (dialog->priv->language),
			    rb_refstring_get (dialog->priv->current_entry->podcast->lang));
}

static void
rb_feed_podcast_properties_dialog_update_last_update (RBFeedPodcastPropertiesDialog *dialog)
{
	gchar *time = rb_feed_podcast_properties_dialog_parse_time (dialog->priv->current_entry->podcast->post_time);
	time[strlen (time) - 1] = '\0';
	gtk_label_set (GTK_LABEL (dialog->priv->last_update),
			time);

}

static void
rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set (GTK_LABEL (dialog->priv->summary),
		       rb_refstring_get (dialog->priv->current_entry->podcast->summary));
}

static gchar*
rb_feed_podcast_properties_dialog_parse_time (gulong value)
{
	time_t result = (time_t) value;
	return asctime(localtime(&result));
}
