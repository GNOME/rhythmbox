/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
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

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs.h>

#include "rb-feed-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-cut-and-paste-code.h"

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
static void rb_feed_podcast_properties_dialog_update_last_episode (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog);
static gchar* rb_feed_podcast_properties_dialog_parse_time (gulong time);

struct RBFeedPodcastPropertiesDialogPrivate
{
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *author;
	GtkWidget   *location;
	GtkWidget   *language;
	GtkWidget   *last_update;
	GtkWidget   *last_episode;
	GtkWidget   *copyright;
	GtkWidget   *summary;
	
	GtkWidget   *close_button;
};

#define RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, RBFeedPodcastPropertiesDialogPrivate))

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

	g_type_class_add_private (klass, sizeof (RBFeedPodcastPropertiesDialogPrivate));
}

static void
rb_feed_podcast_properties_dialog_init (RBFeedPodcastPropertiesDialog *dialog)
{
	GladeXML *xml;
	
	dialog->priv = RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_PRIVATE (dialog);
	
	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_feed_podcast_properties_dialog_response_cb),
				 dialog, 0);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 400);

	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

	xml = rb_glade_xml_new ("podcast-feed-properties.glade",
				"podcastproperties",
				dialog);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
			   glade_xml_get_widget (xml, "podcastproperties"));
	
	dialog->priv->close_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    GTK_STOCK_CLOSE,
							    GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_CLOSE);

	/* get the widgets from the XML */
	dialog->priv->title = glade_xml_get_widget (xml, "titleLabel");
	dialog->priv->author = glade_xml_get_widget (xml, "authorLabel");
	dialog->priv->location = glade_xml_get_widget (xml, "locationLabel");
	dialog->priv->language = glade_xml_get_widget (xml, "languageLabel");
	dialog->priv->last_update = glade_xml_get_widget (xml, "lastupdateLabel");
	dialog->priv->last_episode = glade_xml_get_widget (xml, "lastepisodeLabel");
	dialog->priv->copyright = glade_xml_get_widget (xml, "copyrightLabel");
	dialog->priv->summary = glade_xml_get_widget (xml, "summaryLabel");

	rb_glade_boldify_label (xml, "titleDescLabel");
	rb_glade_boldify_label (xml, "authorDescLabel");
	rb_glade_boldify_label (xml, "locationDescLabel");
	rb_glade_boldify_label (xml, "languageDescLabel");
	rb_glade_boldify_label (xml, "lastupdateDescLabel");
	rb_glade_boldify_label (xml, "lastepisodeDescLabel");
	rb_glade_boldify_label (xml, "copyrightDescLabel");
	rb_glade_boldify_label (xml, "summaryDescLabel");

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
	rb_feed_podcast_properties_dialog_update_last_episode (dialog);
	rb_feed_podcast_properties_dialog_update_copyright (dialog);
	rb_feed_podcast_properties_dialog_update_summary (dialog);
}

static void
rb_feed_podcast_properties_dialog_update_title (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;	
	name = rb_refstring_get (dialog->priv->current_entry->title);
	tmp = g_strdup_printf (_("%s Properties"), name);
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
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_SEEN);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set (GTK_LABEL (dialog->priv->last_update), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_last_episode (RBFeedPodcastPropertiesDialog *dialog)
{
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_POST_TIME);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set (GTK_LABEL (dialog->priv->last_episode), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog)
{
	gtk_label_set (GTK_LABEL (dialog->priv->summary),
		       rb_refstring_get (dialog->priv->current_entry->podcast->summary));
}

static char *
rb_feed_podcast_properties_dialog_parse_time (gulong value)
{
	char *str;

	if (0 == value) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time ((time_t)value);
	}

	return str;
}
