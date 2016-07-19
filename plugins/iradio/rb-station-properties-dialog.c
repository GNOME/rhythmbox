/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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

#include "rb-station-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-util.h"

static void rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass);
static void rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_constructed (GObject *object);
static void rb_station_properties_dialog_dispose (GObject *object);
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
static void rb_station_properties_dialog_update_bitrate (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_playback_error (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_rated_cb (RBRating *rating,
						   double score,
						   RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_show (GtkWidget *widget);
static void rb_station_properties_dialog_location_changed_cb (GtkEntry *entry,
							      RBStationPropertiesDialog *dialog);

struct RBStationPropertiesDialogPrivate
{
	GObject     *plugin;
	RBEntryView *entry_view;
	RhythmDB    *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *genre;
	GtkWidget   *location;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *bitrate;
	GtkWidget   *rating;
	GtkWidget   *playback_error;
	GtkWidget   *playback_error_box;
	GtkWidget   *close_button;

};

#define RB_STATION_PROPERTIES_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_STATION_PROPERTIES_DIALOG, RBStationPropertiesDialogPrivate))

enum
{
	PROP_0,
	PROP_ENTRY_VIEW,
	PROP_PLUGIN
};

G_DEFINE_DYNAMIC_TYPE (RBStationPropertiesDialog, rb_station_properties_dialog, GTK_TYPE_DIALOG)

static void
rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->constructed = rb_station_properties_dialog_constructed;
	object_class->set_property = rb_station_properties_dialog_set_property;
	object_class->get_property = rb_station_properties_dialog_get_property;

	widget_class->show = rb_station_properties_dialog_show;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
					                      "plugin instance",
					                      "plugin instance to use to find files",
					                      G_TYPE_OBJECT,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->dispose = rb_station_properties_dialog_dispose;
	object_class->finalize = rb_station_properties_dialog_finalize;

	g_type_class_add_private (klass, sizeof (RBStationPropertiesDialogPrivate));
}

static void
rb_station_properties_dialog_class_finalize (RBStationPropertiesDialogClass *klass)
{
}

static void
rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog)
{
        dialog->priv = RB_STATION_PROPERTIES_DIALOG_GET_PRIVATE (dialog);
}

static void
rb_station_properties_dialog_constructed (GObject *object)
{
	RBStationPropertiesDialog *dialog;
	GtkWidget *content_area;
	GtkBuilder *builder;
	AtkObject *lobj, *robj;

	RB_CHAIN_GOBJECT_METHOD (rb_station_properties_dialog_parent_class, constructed, object);
	dialog = RB_STATION_PROPERTIES_DIALOG (object);

	g_signal_connect_object (dialog,
				 "response",
				 G_CALLBACK (rb_station_properties_dialog_response_cb),
				 dialog, 0);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	builder = rb_builder_load_plugin_file (dialog->priv->plugin, "station-properties.ui", dialog);

	gtk_container_add (GTK_CONTAINER (content_area),
			   GTK_WIDGET (gtk_builder_get_object (builder, "stationproperties")));

	dialog->priv->close_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
							    _("_Close"),
							    GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

	/* get the widgets from the builder */
	dialog->priv->title = GTK_WIDGET (gtk_builder_get_object (builder, "titleEntry"));
	dialog->priv->genre = GTK_WIDGET (gtk_builder_get_object (builder, "genreEntry"));
	dialog->priv->location = GTK_WIDGET (gtk_builder_get_object (builder, "locationEntry"));

	dialog->priv->lastplayed = GTK_WIDGET (gtk_builder_get_object (builder, "lastplayedLabel"));
	dialog->priv->playcount = GTK_WIDGET (gtk_builder_get_object (builder, "playcountLabel"));
	dialog->priv->bitrate = GTK_WIDGET (gtk_builder_get_object (builder, "bitrateLabel"));
	dialog->priv->playback_error = GTK_WIDGET (gtk_builder_get_object (builder, "errorLabel"));
	dialog->priv->playback_error_box = GTK_WIDGET (gtk_builder_get_object (builder, "errorBox"));

	rb_builder_boldify_label (builder, "titleLabel");
	rb_builder_boldify_label (builder, "genreLabel");
	rb_builder_boldify_label (builder, "locationLabel");
	rb_builder_boldify_label (builder, "ratingLabel");
	rb_builder_boldify_label (builder, "lastplayedDescLabel");
	rb_builder_boldify_label (builder, "playcountDescLabel");
	rb_builder_boldify_label (builder, "bitrateDescLabel");

	g_signal_connect_object (G_OBJECT (dialog->priv->location),
				 "changed",
				 G_CALLBACK (rb_station_properties_dialog_location_changed_cb),
				 dialog, 0);

	dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (dialog->priv->rating,
				 "rated",
				 G_CALLBACK (rb_station_properties_dialog_rated_cb),
				 G_OBJECT (dialog), 0);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (builder, "ratingVBox")),
			   dialog->priv->rating);

	/* add relationship between the rating label and the rating widget */
	lobj = gtk_widget_get_accessible (GTK_WIDGET (gtk_builder_get_object (builder, "ratingLabel")));
	robj = gtk_widget_get_accessible (dialog->priv->rating);

	atk_object_add_relationship (lobj, ATK_RELATION_LABEL_FOR, robj);
	atk_object_add_relationship (robj, ATK_RELATION_LABELLED_BY, lobj);

	g_object_unref (builder);
}

static void
rb_station_properties_dialog_dispose (GObject *object)
{
	RBStationPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (object));

	dialog = RB_STATION_PROPERTIES_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);

	if (dialog->priv->db != NULL) {
		g_object_unref (dialog->priv->db);
	}

	G_OBJECT_CLASS (rb_station_properties_dialog_parent_class)->dispose (object);
}

static void
rb_station_properties_dialog_finalize (GObject *object)
{
	RBStationPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (object));

	dialog = RB_STATION_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_station_properties_dialog_parent_class)->finalize (object);
}

static void
rb_station_properties_dialog_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		if (dialog->priv->db != NULL) {
			g_object_unref (dialog->priv->db);
		}

		dialog->priv->entry_view = g_value_get_object (value);

		g_object_get (G_OBJECT (dialog->priv->entry_view),
			      "db", &dialog->priv->db, NULL);
		break;
	case PROP_PLUGIN:
		dialog->priv->plugin = g_value_get_object (value);
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

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, dialog->priv->entry_view);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, dialog->priv->plugin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_station_properties_dialog_new (GObject *plugin, RBEntryView *entry_view)
{
	RBStationPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	dialog = g_object_new (RB_TYPE_STATION_PROPERTIES_DIALOG,
			       "plugin", plugin,
			       "entry-view", entry_view,
			       NULL);

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
	if (dialog->priv->current_entry)
		rb_station_properties_dialog_sync_entries (dialog);

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

	if (dialog->priv->current_entry != NULL) {
		rhythmdb_entry_unref (dialog->priv->current_entry);
	}

	dialog->priv->current_entry = rhythmdb_entry_ref (selected_entries->data);

	g_list_foreach (selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selected_entries);

	return TRUE;
}

static void
rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog)
{
	rb_station_properties_dialog_update_title (dialog);

	if (dialog->priv->current_entry) {
		rb_station_properties_dialog_update_location (dialog);
		rb_station_properties_dialog_update_title_entry (dialog);
		rb_station_properties_dialog_update_genre (dialog);
	}

	rb_station_properties_dialog_update_play_count (dialog);
	rb_station_properties_dialog_update_bitrate (dialog);
	rb_station_properties_dialog_update_last_played (dialog);
	rb_station_properties_dialog_update_rating (dialog);
	rb_station_properties_dialog_location_changed_cb (GTK_ENTRY (dialog->priv->location), dialog);
}

static void
rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;

	if (dialog->priv->current_entry) {
		name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
		tmp = g_strdup_printf (_("%s Properties"), name);
		gtk_window_set_title (GTK_WINDOW (dialog), tmp);
		g_free (tmp);
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog), _("New Internet Radio Station"));
	}
}

static void
rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->title),title);
}

static void
rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog)
{
	const char *genre;

	genre = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_GENRE);
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->genre), genre);
}

static void
rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog)
{
	const char *location;
	char *unescaped;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	unescaped = g_uri_unescape_string (location, NULL);
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_station_properties_dialog_rated_cb (RBRating *rating,
				       double score,
				       RBStationPropertiesDialog *dialog)
{
	GValue value = {0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (score >= 0 && score <= 5 );

	if (!dialog->priv->current_entry)
		return;

	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, score);

	/* set the new value for the song */
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	g_value_unset (&value);
	rhythmdb_commit (dialog->priv->db);

	g_object_set (G_OBJECT (dialog->priv->rating), "rating", score, NULL);
}

static void
rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog)
{
	char *text;
	long int count = 0;

	if (dialog->priv->current_entry)
		count = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);

	text = g_strdup_printf ("%ld", count);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_bitrate (RBStationPropertiesDialog *dialog)
{
	gulong val = 0;
	char *text;

	if (dialog->priv->current_entry)
		val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_BITRATE);

	if (val == 0)
		text = g_strdup (_("Unknown"));
	else
		text = g_strdup_printf (_("%lu kbps"), val);

	gtk_label_set_text (GTK_LABEL (dialog->priv->bitrate), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog)
{
	const char *last_played = _("Never");

	if (dialog->priv->current_entry)
		last_played = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	gtk_label_set_text (GTK_LABEL (dialog->priv->lastplayed), last_played);
}

static void
rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog)
{
	gdouble rating = 0.0;
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	if (dialog->priv->current_entry)
		rating = rhythmdb_entry_get_double (dialog->priv->current_entry, RHYTHMDB_PROP_RATING);

	g_object_set (G_OBJECT (dialog->priv->rating), "rating", rating, NULL);
}

static void
rb_station_properties_dialog_update_playback_error (RBStationPropertiesDialog *dialog)
{
	const char *error;

	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	error = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_PLAYBACK_ERROR);
	if (dialog->priv->current_entry && error) {
		gtk_label_set_text (GTK_LABEL (dialog->priv->playback_error), error);
		gtk_widget_show (dialog->priv->playback_error_box);
	} else {
		gtk_label_set_text (GTK_LABEL (dialog->priv->playback_error), "");
		gtk_widget_hide (dialog->priv->playback_error_box);
	}
}

static void
rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog)
{
	const char *title;
	const char *genre;
	const char *location;
	const char *string;
	GValue val = {0,};
	gboolean changed = FALSE;
	RhythmDBEntry *entry = dialog->priv->current_entry;

	title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	location = gtk_entry_get_text (GTK_ENTRY (dialog->priv->location));

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	if (strcmp (title, string)) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, title);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_TITLE,
				    &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
	if (strcmp (genre, string)) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, genre);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_GENRE, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	if (strcmp (location, string)) {
		if (rhythmdb_entry_lookup_by_location (dialog->priv->db, location) == NULL) {
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, location);
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_LOCATION, &val);
			g_value_unset (&val);
			changed = TRUE;
		} else {
			rb_error_dialog (NULL, _("Unable to change station property"), _("Unable to change station URI to %s, as that station already exists"), location);
		}
	}

	if (changed)
		rhythmdb_commit (dialog->priv->db);
}

static void
rb_station_properties_dialog_show (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (rb_station_properties_dialog_parent_class)->show)
		GTK_WIDGET_CLASS (rb_station_properties_dialog_parent_class)->show (widget);

	rb_station_properties_dialog_update_playback_error (
			RB_STATION_PROPERTIES_DIALOG (widget));
}

static void
rb_station_properties_dialog_location_changed_cb (GtkEntry *entry,
						  RBStationPropertiesDialog *dialog)
{
}

void
_rb_station_properties_dialog_register_type (GTypeModule *module)
{
	rb_station_properties_dialog_register_type (module);
}
