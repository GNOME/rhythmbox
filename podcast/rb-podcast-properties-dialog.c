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

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glib.h>

#if defined(WITH_WEBKIT)
#include <webkit/webkit.h>
#endif

#include "rb-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"

static void rb_podcast_properties_dialog_class_init (RBPodcastPropertiesDialogClass *klass);
static void rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_dispose (GObject *object);
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
static void rb_podcast_properties_dialog_response_cb (GtkDialog *gtkdialog,
						      int response_id,
						      RBPodcastPropertiesDialog *dialog);

static void rb_podcast_properties_dialog_update (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_download_location (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_duration (RBPodcastPropertiesDialog *dialog);
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

struct RBPodcastPropertiesDialogPrivate
{
	RBEntryView *entry_view;
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *feed;
	GtkWidget   *location;
	GtkWidget   *download_location;
	GtkWidget   *duration;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *bitrate;
	GtkWidget   *rating;
	GtkWidget   *date;
	GtkWidget   *description;
	GtkWidget   *description_window;

	GtkWidget   *close_button;
};

enum
{
	PROP_0,
	PROP_ENTRY_VIEW,
	PROP_BACKEND
};

G_DEFINE_TYPE (RBPodcastPropertiesDialog, rb_podcast_properties_dialog, GTK_TYPE_DIALOG)

#if defined(WITH_WEBKIT)
/* list of HTML-ish strings that we search for to distinguish plain text from HTML podcast
 * descriptions.  we don't really have anything else to go on - regular content type
 * sniffing only works for proper HTML documents, but these are just tiny fragments, usually
 * with some simple formatting tags.  if we find any of these in a podcast description,
 * we'll display it as HTML rather than text.
 */
static const char *html_clues[] = {
	"<a ",
	"<b>",
	"<i>",
	"<ul>",
	"<br",
	"<img ",
	"&lt;",
	"&gt;",
	"&amp;",
	"&quo;",
	"&#8",
	"&#x"
};

/* list of URI prefixes for things we ignore when handling navigation requests.
 * some podcast descriptions include facebook 'like' buttons as iframes, which otherwise
 * show up as external web browser windows.
 */
static const char *ignore_uris[] = {
	"http://www.facebook.com/plugins/like.php?"
};

#endif

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

	object_class->dispose = rb_podcast_properties_dialog_dispose;
	object_class->finalize = rb_podcast_properties_dialog_finalize;

	g_type_class_add_private (klass, sizeof (RBPodcastPropertiesDialogPrivate));
}

#if defined(WITH_WEBKIT)

static WebKitNavigationResponse
navigation_requested_cb (WebKitWebView *web_view,
			 WebKitWebFrame *frame,
			 WebKitNetworkRequest *request,
			 RBPodcastPropertiesDialog *dialog)
{
	const char *uri;
	GError *error = NULL;
	int i;

	uri = webkit_network_request_get_uri (request);

	/* ignore some obnoxious social networking stuff */
	for (i = 0; i < G_N_ELEMENTS (ignore_uris); i++) {
		if (g_str_has_prefix (uri, ignore_uris[i])) {
			rb_debug ("ignoring external URI %s", uri);
			return WEBKIT_NAVIGATION_RESPONSE_IGNORE;
		}
	}

	gtk_show_uri (gtk_widget_get_screen (GTK_WIDGET (dialog)), uri, GDK_CURRENT_TIME, &error);
	if (error != NULL) {
		rb_error_dialog (NULL, _("Unable to display requested URI"), "%s", error->message);
		g_error_free (error);
	}

	return WEBKIT_NAVIGATION_RESPONSE_IGNORE;
}

static void
set_webkit_settings (WebKitWebView *view)
{
	WebKitWebSettings *settings;

	settings = webkit_web_settings_new ();
	g_object_set (settings,
		      "enable-scripts", FALSE,
		      "enable-plugins", FALSE,
		      NULL);
	webkit_web_view_set_settings (view, settings);
}

static void
set_webkit_font_from_gtk_style (WebKitWebView *view)
{
	WebKitWebSettings *settings;
	GtkStyle *style;
	int font_size;
	const char *font_family;

	style = gtk_widget_get_style (GTK_WIDGET (view));
	settings = webkit_web_view_get_settings (view);

	font_size = pango_font_description_get_size (style->font_desc);
	if (pango_font_description_get_size_is_absolute (style->font_desc) == FALSE)
		font_size /= PANGO_SCALE;

	font_family = pango_font_description_get_family (style->font_desc);

	rb_debug ("setting font settings: %s / %d", font_family, font_size);
	g_object_set (settings,
		      "default-font-size", font_size,
		      "default-monospace-font-size", font_size,
		      "sans-serif-font-family", font_family,
		      "monospace-font-family", font_family,
		      NULL);
}
#endif

static void
rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog)
{
	GtkWidget  *content_area;
	GtkWidget  *bin;
	GtkWidget  *widget;
	GtkBuilder *builder;
	AtkObject *lobj, *robj;

	dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog,
						    RB_TYPE_PODCAST_PROPERTIES_DIALOG,
						    RBPodcastPropertiesDialogPrivate);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (rb_podcast_properties_dialog_response_cb),
				 dialog, 0);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);

	builder = rb_builder_load ("podcast-properties.ui", dialog);

	gtk_container_add (GTK_CONTAINER (content_area),
			   GTK_WIDGET (gtk_builder_get_object (builder, "podcastproperties")));
	dialog->priv->close_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    GTK_STOCK_CLOSE,
							    GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

	/* get the widgets from the builder */
	dialog->priv->title = GTK_WIDGET (gtk_builder_get_object (builder, "titleLabel"));
	dialog->priv->feed = GTK_WIDGET (gtk_builder_get_object (builder, "feedLabel"));
	dialog->priv->duration = GTK_WIDGET (gtk_builder_get_object (builder, "durationLabel"));
	dialog->priv->location = GTK_WIDGET (gtk_builder_get_object (builder, "locationLabel"));
	dialog->priv->download_location = GTK_WIDGET (gtk_builder_get_object (builder, "downloadLocationLabel"));
	dialog->priv->lastplayed = GTK_WIDGET (gtk_builder_get_object (builder, "lastplayedLabel"));
	dialog->priv->playcount = GTK_WIDGET (gtk_builder_get_object (builder, "playcountLabel"));
	dialog->priv->bitrate = GTK_WIDGET (gtk_builder_get_object (builder, "bitrateLabel"));
	dialog->priv->date = GTK_WIDGET (gtk_builder_get_object (builder, "dateLabel"));
#if defined(WITH_WEBKIT)
	dialog->priv->description = webkit_web_view_new ();
	set_webkit_settings (WEBKIT_WEB_VIEW (dialog->priv->description));
	set_webkit_font_from_gtk_style (WEBKIT_WEB_VIEW (dialog->priv->description));

	g_signal_connect_object (dialog->priv->description,
				 "navigation-requested",
				 G_CALLBACK (navigation_requested_cb),
				 dialog,
				 0);
#else
	dialog->priv->description = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (dialog->priv->description), TRUE);
#endif
	/* add relationship between the description label and the description widget */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "descriptionDescLabel"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (widget), dialog->priv->description);
	lobj = gtk_widget_get_accessible (widget);
	robj = gtk_widget_get_accessible (dialog->priv->description);
	atk_object_add_relationship (lobj, ATK_RELATION_LABEL_FOR, robj);
	atk_object_add_relationship (robj, ATK_RELATION_LABELLED_BY, lobj);

	bin = GTK_WIDGET (gtk_builder_get_object (builder, "descriptionViewport"));
	gtk_container_add (GTK_CONTAINER (bin), dialog->priv->description);

	dialog->priv->description_window = GTK_WIDGET (gtk_builder_get_object (builder, "descriptionWindow"));

	rb_builder_boldify_label (builder, "titleDescLabel");
	rb_builder_boldify_label (builder, "feedDescLabel");
	rb_builder_boldify_label (builder, "locationDescLabel");
	rb_builder_boldify_label (builder, "downloadLocationDescLabel");
	rb_builder_boldify_label (builder, "durationDescLabel");
	rb_builder_boldify_label (builder, "ratingDescLabel");
	rb_builder_boldify_label (builder, "lastplayedDescLabel");
	rb_builder_boldify_label (builder, "playcountDescLabel");
	rb_builder_boldify_label (builder, "bitrateDescLabel");
	rb_builder_boldify_label (builder, "dateDescLabel");
	rb_builder_boldify_label (builder, "descriptionDescLabel");

	dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (dialog->priv->rating,
				 "rated",
				 G_CALLBACK (rb_podcast_properties_dialog_rated_cb),
				 G_OBJECT (dialog), 0);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (builder, "ratingVBox")),
			   dialog->priv->rating);

	/* add relationship between the rating label and the rating widget */
	lobj = gtk_widget_get_accessible (GTK_WIDGET (gtk_builder_get_object (builder, "ratingDescLabel")));
	robj = gtk_widget_get_accessible (dialog->priv->rating);
	
	atk_object_add_relationship (lobj, ATK_RELATION_LABEL_FOR, robj);
	atk_object_add_relationship (robj, ATK_RELATION_LABELLED_BY, lobj);

	g_object_unref (builder);
}

static void
rb_podcast_properties_dialog_dispose (GObject *object)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	if (dialog->priv->db != NULL) {
		g_object_unref (dialog->priv->db);
		dialog->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_podcast_properties_dialog_parent_class)->dispose (object);
}

static void
rb_podcast_properties_dialog_finalize (GObject *object)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_podcast_properties_dialog_parent_class)->finalize (object);
}

static void
rb_podcast_properties_dialog_set_entry_view (RBPodcastPropertiesDialog *dialog,
					     RBEntryView               *view)
{
	if (dialog->priv->db != NULL) {
		g_object_unref (dialog->priv->db);
	}

	dialog->priv->entry_view = view;

	if (dialog->priv->entry_view != NULL) {
		g_object_get (dialog->priv->entry_view,
			      "db", &dialog->priv->db, NULL);
	}
}

static void
rb_podcast_properties_dialog_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec)
{
	RBPodcastPropertiesDialog *dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		rb_podcast_properties_dialog_set_entry_view (dialog, g_value_get_object (value));
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

	switch (prop_id) {
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
	rb_podcast_properties_dialog_update_download_location (dialog);
	rb_podcast_properties_dialog_update_title (dialog);
	rb_podcast_properties_dialog_update_title_label (dialog);
	rb_podcast_properties_dialog_update_feed (dialog);
	rb_podcast_properties_dialog_update_duration (dialog);
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

	name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	tmp = g_strdup_printf (_("%s Properties"), name);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);
}

static void
rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_label_set_text (GTK_LABEL (dialog->priv->title), title);
}

static void
rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog)
{
	const char *feed;

	feed = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_ALBUM);
	gtk_label_set_text (GTK_LABEL (dialog->priv->feed), feed);
}

static void
rb_podcast_properties_dialog_update_duration (RBPodcastPropertiesDialog *dialog)
{
        char *text;
        gulong duration = 0;

        duration = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_DURATION);

	text = rb_make_duration_string (duration);
        gtk_label_set_text (GTK_LABEL (dialog->priv->duration), text);
        g_free (text);
}

static void
rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog)
{
	const char *location;
	char *display;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL)
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	display = g_uri_unescape_string (location, NULL);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), display);
	g_free (display);
}

static void
rb_podcast_properties_dialog_update_download_location (RBPodcastPropertiesDialog *dialog)
{
	const char *location;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location != NULL && location[0] != '\0') {
		char *display;
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
		display = g_uri_unescape_string (location, NULL);
		gtk_label_set_text (GTK_LABEL (dialog->priv->download_location), display);
		g_free (display);
	} else {
		gtk_label_set_text (GTK_LABEL (dialog->priv->download_location), _("Not Downloaded"));
	}
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
	gulong count;
	char *text;

	count = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);
	text = g_strdup_printf ("%ld", count);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_podcast_properties_dialog_update_bitrate (RBPodcastPropertiesDialog *dialog)
{
        char *tmp = NULL;
        gulong bitrate = 0;

	bitrate = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_BITRATE);
        if (bitrate > 0)
                tmp = g_strdup_printf (_("%lu kbps"), bitrate);
        else
                tmp = g_strdup (_("Unknown"));

	gtk_label_set_text (GTK_LABEL (dialog->priv->bitrate), tmp);
	g_free (tmp);
}

static void
rb_podcast_properties_dialog_update_last_played (RBPodcastPropertiesDialog *dialog)
{
	const char *str;

	str = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	gtk_label_set_text (GTK_LABEL (dialog->priv->lastplayed), str);
}

static void
rb_podcast_properties_dialog_update_rating (RBPodcastPropertiesDialog *dialog)
{
	double rating;

	rating = rhythmdb_entry_get_double (dialog->priv->current_entry, RHYTHMDB_PROP_RATING);
	g_object_set (G_OBJECT (dialog->priv->rating), "rating", rating, NULL);
}

static void
rb_podcast_properties_dialog_update_date (RBPodcastPropertiesDialog *dialog)
{
	gulong post_time;
	char *time;

	post_time = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_POST_TIME);
	time = rb_podcast_properties_dialog_parse_time (post_time);

	gtk_label_set_text (GTK_LABEL (dialog->priv->date), time);
	g_free (time);
}

#if defined(WITH_WEBKIT)
static gboolean
update_scrollbar_policy_cb (WebKitWebFrame *frame, RBPodcastPropertiesDialog *dialog)
{
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (dialog->priv->description_window),
					webkit_web_frame_get_horizontal_scrollbar_policy (frame),
					webkit_web_frame_get_vertical_scrollbar_policy (frame));
	return TRUE;
}

#endif

static void
rb_podcast_properties_dialog_update_description (RBPodcastPropertiesDialog *dialog)
{
#if defined(WITH_WEBKIT)
	WebKitWebFrame *frame;
	const char *str;
	int i;
	gboolean loaded = FALSE;
	str = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_DESCRIPTION);
	for (i = 0; i < G_N_ELEMENTS (html_clues); i++) {
		if (g_strstr_len (str, -1, html_clues[i]) != NULL) {
			webkit_web_view_load_html_string (WEBKIT_WEB_VIEW (dialog->priv->description),
							  str,
							  "");
			loaded = TRUE;
		}
	}

	if (loaded == FALSE) {
		webkit_web_view_load_string (WEBKIT_WEB_VIEW (dialog->priv->description),
					     str,
					     "text/plain",
					     "utf-8",
					     "");
	}

	/* ensure scrollbar policy for the frame matches the viewport */
	frame = webkit_web_view_get_main_frame (WEBKIT_WEB_VIEW (dialog->priv->description));
	g_signal_connect_object (frame,
				 "scrollbars-policy-changed",
				 G_CALLBACK (update_scrollbar_policy_cb),
				 dialog, 0);
	update_scrollbar_policy_cb (frame, dialog);
#else
	const char *str;
	str = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_DESCRIPTION);
	gtk_label_set_text (GTK_LABEL (dialog->priv->description), str);
#endif
}

static char *
rb_podcast_properties_dialog_parse_time (gulong value)
{
	char *str;

	if (0 == value) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time ((time_t)value);
	}

	return str;
}
