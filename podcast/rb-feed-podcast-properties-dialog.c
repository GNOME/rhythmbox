/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org>
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

#include <config.h>

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glib.h>
/* GStreamer happens to have some language name functions */
#include <gst/gst.h>
#include <gst/tag/tag.h>

#include "rb-feed-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-cut-and-paste-code.h"
#include "rhythmdb.h"
#include "rb-debug.h"

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
	GtkWidget  *content_area;
	GtkBuilder *builder;

	dialog->priv = RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_PRIVATE (dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_feed_podcast_properties_dialog_response_cb),
				 dialog, 0);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 400);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	builder = rb_builder_load ("podcast-feed-properties.ui", dialog);

	gtk_container_add (GTK_CONTAINER (content_area),
			   GTK_WIDGET (gtk_builder_get_object (builder, "podcastproperties")));

	dialog->priv->close_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    _("_Close"),
							    GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_CLOSE);

	/* get the widgets from the builder */
	dialog->priv->title = GTK_WIDGET (gtk_builder_get_object (builder, "titleLabel"));
	dialog->priv->author = GTK_WIDGET (gtk_builder_get_object (builder, "authorLabel"));
	dialog->priv->location = GTK_WIDGET (gtk_builder_get_object (builder, "locationLabel"));
	dialog->priv->language = GTK_WIDGET (gtk_builder_get_object (builder, "languageLabel"));
	dialog->priv->last_update = GTK_WIDGET (gtk_builder_get_object (builder, "lastupdateLabel"));
	dialog->priv->last_episode = GTK_WIDGET (gtk_builder_get_object (builder, "lastepisodeLabel"));
	dialog->priv->copyright = GTK_WIDGET (gtk_builder_get_object (builder, "copyrightLabel"));
	dialog->priv->summary = GTK_WIDGET (gtk_builder_get_object (builder, "summaryLabel"));

	rb_builder_boldify_label (builder, "titleDescLabel");
	rb_builder_boldify_label (builder, "authorDescLabel");
	rb_builder_boldify_label (builder, "locationDescLabel");
	rb_builder_boldify_label (builder, "languageDescLabel");
	rb_builder_boldify_label (builder, "lastupdateDescLabel");
	rb_builder_boldify_label (builder, "lastepisodeDescLabel");
	rb_builder_boldify_label (builder, "copyrightDescLabel");
	rb_builder_boldify_label (builder, "summaryDescLabel");

	g_object_unref (builder);
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
	name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	tmp = g_strdup_printf (_("%s Properties"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);
}

static void
rb_feed_podcast_properties_dialog_update_title_label (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_label_set_text (GTK_LABEL (dialog->priv->title), title);
}

static void
rb_feed_podcast_properties_dialog_update_author (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *artist;

	artist = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_ARTIST);
	gtk_label_set_text (GTK_LABEL (dialog->priv->author), artist);
}

static void
rb_feed_podcast_properties_dialog_update_location (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *location;
	char *unescaped;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL)
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	unescaped = g_uri_unescape_string (location, NULL);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *copyright;

	copyright = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_COPYRIGHT);
	gtk_label_set_text (GTK_LABEL (dialog->priv->copyright), copyright);
}

static void
rb_feed_podcast_properties_dialog_update_language (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *language;
	char *separator;
	char *iso636lang;
	const char *langname;

	language = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LANG);

	/* language tag is language[-subcode]; we only care about the language bit */
	iso636lang = g_strdup (language);
	separator = strchr (iso636lang, '-');
	if (separator != NULL) {
		*separator = '\0';
	}

	/* map the language code to a language name */
	langname = gst_tag_get_language_name (iso636lang);
	g_free (iso636lang);
	if (langname != NULL) {
		rb_debug ("mapped language code %s to %s", language, langname);
		gtk_label_set_text (GTK_LABEL (dialog->priv->language), langname);
		return;
	}

	gtk_label_set_text (GTK_LABEL (dialog->priv->language), language);
}

static void
rb_feed_podcast_properties_dialog_update_last_update (RBFeedPodcastPropertiesDialog *dialog)
{
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_SEEN);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set_text (GTK_LABEL (dialog->priv->last_update), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_last_episode (RBFeedPodcastPropertiesDialog *dialog)
{
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_POST_TIME);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set_text (GTK_LABEL (dialog->priv->last_episode), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *summary;

	summary = rhythmdb_entry_get_string (dialog->priv->current_entry,
					     RHYTHMDB_PROP_DESCRIPTION);
	if (summary == NULL || summary[0] == '\0') {
		summary = rhythmdb_entry_get_string (dialog->priv->current_entry,
						     RHYTHMDB_PROP_SUBTITLE);
	}

	gtk_label_set_text (GTK_LABEL (dialog->priv->summary), summary);
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
