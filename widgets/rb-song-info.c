/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of local song properties dialog
 *
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

/*
 * Yes, this code is ugly.
 */

#include <config.h>

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rhythmdb.h"
#include "rb-song-info.h"
#include "rb-enums.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
static GObject *rb_song_info_constructor (GType type, guint n_construct_properties,
					  GObjectConstructParam *construct_properties);

static void rb_song_info_show (GtkWidget *widget);
static void rb_song_info_finalize (GObject *object);
static void rb_song_info_set_property (GObject *object, 
				       guint prop_id,
				       const GValue *value, 
				       GParamSpec *pspec);
static void rb_song_info_get_property (GObject *object, 
				       guint prop_id,
				       GValue *value, 
				       GParamSpec *pspec);
static void rb_song_info_response_cb (GtkDialog *dialog,
				      int response_id,
				      RBSongInfo *song_info);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_play_count (RBSongInfo *song_info);
static void rb_song_info_update_last_played (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_buttons (RBSongInfo *song_info);
static void rb_song_info_update_rating (RBSongInfo *song_info);
static void rb_song_info_update_year (RBSongInfo *song_info);
static void rb_song_info_update_playback_error (RBSongInfo *song_info);

static void rb_song_info_backward_clicked_cb (GtkWidget *button,
					      RBSongInfo *song_info);
static void rb_song_info_forward_clicked_cb (GtkWidget *button,
					     RBSongInfo *song_info);
static void rb_song_info_view_changed_cb (RBEntryView *entry_view,
					  RBSongInfo *song_info);
static void rb_song_info_rated_cb (RBRating *rating,
				   double score,
				   RBSongInfo *song_info);
static void rb_song_info_mnemonic_cb (GtkWidget *target);
static void rb_song_info_sync_entries (RBSongInfo *dialog);

struct RBSongInfoPrivate
{
	RhythmDB *db;
	RBEntryView *entry_view;

	/* information on the displayed song */
	RhythmDBEntry *current_entry;
	GList *selected_entries;

	gboolean editable;

	/* the dialog widgets */
	GtkWidget   *backward;
	GtkWidget   *forward;

	GtkWidget   *title;
	GtkWidget   *artist;
	GtkWidget   *album;
	GtkWidget   *genre;
	GtkWidget   *track_cur;
	GtkWidget   *disc_cur;
	GtkWidget   *year;
	GtkWidget   *playback_error_box;
	GtkWidget   *playback_error_label;

	GtkWidget   *bitrate;
	GtkWidget   *duration;
	GtkWidget   *name;
	GtkWidget   *location;
	GtkWidget   *play_count;
	GtkWidget   *last_played;
	GtkWidget   *rating;
};

#define RB_SONG_INFO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SONG_INFO, RBSongInfoPrivate))

enum
{
	PRE_METADATA_CHANGE,
	POST_METADATA_CHANGE,
	LAST_SIGNAL
};

enum 
{
	PROP_0,
	PROP_ENTRY_VIEW
};

static GObjectClass *parent_class = NULL;

static guint rb_song_info_signals[LAST_SIGNAL] = { 0 };

GType
rb_song_info_get_type (void)
{
	static GType rb_song_info_type = 0;

	if (rb_song_info_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSongInfoClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_song_info_class_init,
			NULL,
			NULL,
			sizeof (RBSongInfo),
			0,
			(GInstanceInitFunc) rb_song_info_init
		};

		rb_song_info_type = g_type_register_static (GTK_TYPE_DIALOG,
							    "RBSongInfo",
							    &our_info, 0);
	}

	return rb_song_info_type;
}

static void
rb_song_info_class_init (RBSongInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;
	object_class->constructor = rb_song_info_constructor;

	widget_class->show = rb_song_info_show;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry_view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_song_info_finalize;

	rb_song_info_signals[PRE_METADATA_CHANGE] =
		g_signal_new ("pre-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, pre_metadata_change),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);

	rb_song_info_signals[POST_METADATA_CHANGE] =
		g_signal_new ("post-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, post_metadata_change),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);

	g_type_class_add_private (klass, sizeof (RBSongInfoPrivate));
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	/* create the dialog and some buttons backward - forward - close */
	song_info->priv = RB_SONG_INFO_GET_PRIVATE (song_info);

	g_signal_connect_object (G_OBJECT (song_info),
				 "response",
				 G_CALLBACK (rb_song_info_response_cb),
				 song_info, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (song_info), FALSE);

	gtk_dialog_add_button (GTK_DIALOG (song_info),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);

	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);



	gtk_container_set_border_width (GTK_CONTAINER (song_info), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (song_info)->vbox), 2);
}

static void
rb_song_info_show (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (parent_class)->show)
		GTK_WIDGET_CLASS (parent_class)->show (widget);

	rb_song_info_update_playback_error (RB_SONG_INFO (widget));
}

static void
rb_song_info_construct_single (RBSongInfo *song_info, GladeXML *xml,
			       gboolean editable)
{
	song_info->priv->backward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   GTK_STOCK_GO_BACK,
							   GTK_RESPONSE_NONE);
	
	g_signal_connect_object (G_OBJECT (song_info->priv->backward),
				 "clicked",
				 G_CALLBACK (rb_song_info_backward_clicked_cb),
				 song_info, 0);
	
	song_info->priv->forward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   GTK_STOCK_GO_FORWARD,
							   GTK_RESPONSE_NONE);
	
	g_signal_connect_object (G_OBJECT (song_info->priv->forward),
				 "clicked",
				 G_CALLBACK (rb_song_info_forward_clicked_cb),
				 song_info, 0);

	gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));

	/* get the widgets from the XML */
	song_info->priv->title         = glade_xml_get_widget (xml, "song_info_title");
	song_info->priv->track_cur     = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->bitrate       = glade_xml_get_widget (xml, "song_info_bitrate");
	song_info->priv->duration      = glade_xml_get_widget (xml, "song_info_duration");
	song_info->priv->location = glade_xml_get_widget (xml, "song_info_location");
	song_info->priv->play_count    = glade_xml_get_widget (xml, "song_info_playcount");
	song_info->priv->last_played   = glade_xml_get_widget (xml, "song_info_lastplayed");
	song_info->priv->name = glade_xml_get_widget (xml, "song_info_name");
	song_info->priv->disc_cur     = glade_xml_get_widget (xml, "song_info_disc_cur");
	song_info->priv->year		= glade_xml_get_widget (xml, "song_info_year");

	rb_glade_boldify_label (xml, "title_label");
	rb_glade_boldify_label (xml, "trackn_label");
	rb_glade_boldify_label (xml, "name_label");
	rb_glade_boldify_label (xml, "location_label");
	rb_glade_boldify_label (xml, "last_played_label");
	rb_glade_boldify_label (xml, "play_count_label");
	rb_glade_boldify_label (xml, "duration_label");
	rb_glade_boldify_label (xml, "bitrate_label");
	rb_glade_boldify_label (xml, "discn_label");
	rb_glade_boldify_label (xml, "year_label");

	/* whenever you press a mnemonic, the associated GtkEntry's text gets highlighted */
	g_signal_connect_object (G_OBJECT (song_info->priv->title),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->track_cur),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);

	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->title), editable);
	gtk_editable_set_editable  (GTK_EDITABLE (song_info->priv->track_cur), editable);

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->title);

}

static void
rb_song_info_construct_multiple (RBSongInfo *song_info, GladeXML *xml,
				 gboolean editable)
{
	gtk_window_set_title (GTK_WINDOW (song_info),
			      _("Multiple Song Properties"));
	gtk_widget_grab_focus (song_info->priv->artist);
}

static GObject *
rb_song_info_constructor (GType type, guint n_construct_properties,
			  GObjectConstructParam *construct_properties)
{
	RBSongInfo *song_info;
	RBSongInfoClass *klass;
	GObjectClass *parent_class;  
	GladeXML *xml;
	GList *selected_entries;
	GList *tem;
	gboolean editable = TRUE;

	klass = RB_SONG_INFO_CLASS (g_type_class_peek (RB_TYPE_SONG_INFO));

	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	song_info = RB_SONG_INFO (parent_class->constructor (type,
							     n_construct_properties,
							     construct_properties));

	selected_entries = rb_entry_view_get_selected_entries (song_info->priv->entry_view);

	g_return_val_if_fail (selected_entries != NULL, NULL);

	for (tem = selected_entries; tem; tem = tem->next)
		if (!rhythmdb_entry_is_editable (song_info->priv->db,
						 selected_entries->data)) {
			editable = FALSE;
			break;
		}
	song_info->priv->editable = editable;

	if (selected_entries->next == NULL) {
		song_info->priv->current_entry = selected_entries->data;
		song_info->priv->selected_entries = NULL;
		g_list_free (selected_entries);
	} else {
		song_info->priv->current_entry = NULL;
		song_info->priv->selected_entries = selected_entries;
	}

	if (song_info->priv->current_entry) {
		xml = rb_glade_xml_new ("song-info.glade",
					"song_info_vbox",
					song_info);
		gtk_container_add (GTK_CONTAINER (GTK_DIALOG (song_info)->vbox),
				   glade_xml_get_widget (xml, "song_info_vbox"));
	} else {
		xml = rb_glade_xml_new ("song-info-multiple.glade",
					"song_info_basic",
					song_info);
		gtk_container_add (GTK_CONTAINER (GTK_DIALOG (song_info)->vbox),
				   glade_xml_get_widget (xml, "song_info_basic"));
	}
		
	
	glade_xml_signal_autoconnect (xml);

	song_info->priv->artist = glade_xml_get_widget (xml, "song_info_artist");
	song_info->priv->album = glade_xml_get_widget (xml, "song_info_album");
	song_info->priv->genre = glade_xml_get_widget (xml, "song_info_genre");
	song_info->priv->year = glade_xml_get_widget (xml, "song_info_year");
	song_info->priv->playback_error_box = glade_xml_get_widget (xml, "song_info_error_box");
	song_info->priv->playback_error_label = glade_xml_get_widget (xml, "song_info_error_label");

	rb_glade_boldify_label (xml, "album_label");
	rb_glade_boldify_label (xml, "artist_label");
	rb_glade_boldify_label (xml, "genre_label");
	rb_glade_boldify_label (xml, "year_label");
	rb_glade_boldify_label (xml, "rating_label");

	g_signal_connect_object (G_OBJECT (song_info->priv->artist),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->album),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->genre),
				 "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb),
				 NULL, 0);

	/* this widget has to be customly created */
	song_info->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (song_info->priv->rating, "rated",
				 G_CALLBACK (rb_song_info_rated_cb),
				 G_OBJECT (song_info), 0);
	gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (xml, "song_info_rating_container")),
			   song_info->priv->rating);

	gtk_editable_set_editable (GTK_EDITABLE (song_info->priv->artist), editable);
	gtk_editable_set_editable  (GTK_EDITABLE (song_info->priv->album), editable);
	gtk_editable_set_editable  (GTK_EDITABLE (song_info->priv->genre), editable);

	/* Finish construction */
	if (song_info->priv->current_entry)
		rb_song_info_construct_single (song_info, xml, editable);
	else
		rb_song_info_construct_multiple (song_info, xml, editable);

	g_object_unref (G_OBJECT (xml));
	return G_OBJECT (song_info);
}

static void
rb_song_info_finalize (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_song_info_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id)
	{
	case PROP_ENTRY_VIEW:
	{
		song_info->priv->entry_view = g_value_get_object (value);
		g_object_get (G_OBJECT (song_info->priv->entry_view), "db",
			      &song_info->priv->db, NULL);
		g_signal_connect_object (G_OBJECT (song_info->priv->entry_view),
					 "changed",
					 G_CALLBACK (rb_song_info_view_changed_cb),
					 song_info,
					 G_CONNECT_AFTER);
	}
	break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_song_info_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id)
	{
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, song_info->priv->entry_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_song_info_new (RBEntryView *entry_view)
{
	RBSongInfo *song_info;

        g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	if (rb_entry_view_have_selection (entry_view) == FALSE) 
		return NULL;

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO, "entry_view", entry_view, NULL);

	g_return_val_if_fail (song_info->priv != NULL, NULL);

	rb_song_info_populate_dialog (song_info);
	return GTK_WIDGET (song_info);
}

typedef void (*RBSongInfoSelectionFunc)(RBSongInfo *info,
					RhythmDBEntry *entry,
					void *data);

static void
rb_song_info_selection_for_each (RBSongInfo *info, RBSongInfoSelectionFunc func,
				 void *data)
{
	if (info->priv->current_entry)
		func (info, info->priv->current_entry, data);
	else {
		GList *tem;
		for (tem = info->priv->selected_entries; tem ; tem = tem->next)
			func (info, tem->data, data);
	}
}

static void
rb_song_info_response_cb (GtkDialog *dialog,
			  int response_id,
			  RBSongInfo *song_info)
{
	if (response_id == GTK_RESPONSE_CLOSE) {
		if (song_info->priv->editable)
			rb_song_info_sync_entries (RB_SONG_INFO (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
	}
}

static void
rb_song_info_set_entry_rating (RBSongInfo *info,
			       RhythmDBEntry *entry,
			       void *data)
{
	GValue value = {0, };
	double trouble = *((double*) data);

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, trouble);
	rhythmdb_entry_set (info->priv->db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);
}
	

static void
rb_song_info_rated_cb (RBRating *rating,
		       double score,
		       RBSongInfo *song_info)
{
	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_SONG_INFO (song_info));
	g_return_if_fail (score >= 0 && score <= 5 );

	rb_song_info_selection_for_each (song_info,
					 rb_song_info_set_entry_rating,
					 &score);
	rhythmdb_commit (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "rating", score,
		      NULL);
}

static void
rb_song_info_mnemonic_cb (GtkWidget *target)
{
	g_return_if_fail (GTK_IS_EDITABLE (target) || GTK_IS_TEXT_VIEW (target));

	gtk_widget_grab_focus (target);

	if (GTK_IS_EDITABLE (target)) {
		gtk_editable_select_region (GTK_EDITABLE (target), 0, -1);
	} else { /* GtkTextViews need special treatment */
		g_signal_emit_by_name (G_OBJECT (target), "select-all");
	}
}

static void
rb_song_info_populate_num_field (GtkEntry *field, gulong num)
{
	char *tmp;
	if (num > 0)
		tmp = g_strdup_printf ("%.2ld", num);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_entry_set_text (field, tmp);
	g_free (tmp);
}

static void 
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	const char *text = NULL;
	char *tmp;
	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);

	if (!song_info->priv->current_entry)
		return;
	

	text = rb_refstring_get (song_info->priv->current_entry->title);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);
	
	tmp = g_strdup_printf (_("%s Properties"), text);
	gtk_window_set_title (GTK_WINDOW (song_info), tmp);
	g_free (tmp);

	text = rb_refstring_get (song_info->priv->current_entry->artist);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist), text);
	text = rb_refstring_get (song_info->priv->current_entry->album);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album), text);
	text = rb_refstring_get (song_info->priv->current_entry->genre);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->genre), text);

	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_cur),
					 song_info->priv->current_entry->tracknum);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_cur),
					 song_info->priv->current_entry->discnum);

	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
	rb_song_info_update_play_count (song_info);
	rb_song_info_update_last_played (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_rating (song_info);
	rb_song_info_update_year (song_info);
	rb_song_info_update_playback_error (song_info);
}

static void
rb_song_info_update_playback_error (RBSongInfo *song_info)
{
	char *message = NULL;

	if (!song_info->priv->current_entry)
		return;

	if (song_info->priv->current_entry->playback_error)
		message = g_strdup (song_info->priv->current_entry->playback_error);

	if (message) {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    message);
		gtk_widget_show (song_info->priv->playback_error_box);
	} else {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    "No errors");
		gtk_widget_hide (song_info->priv->playback_error_box);
	}

	g_free (message);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	char *tmp = NULL;
	gulong bitrate = 0;
	bitrate = song_info->priv->current_entry->bitrate;

	if (bitrate > 0)
		tmp = g_strdup_printf (_("%lu kbps"), bitrate);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate),
			    tmp);
	g_free (tmp);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	char *text = NULL;
	long duration = 0;
	int minutes, seconds;
	duration = song_info->priv->current_entry->duration;
	minutes = duration / 60;
	seconds = duration % 60;
	text = g_strdup_printf ("%d:%02d", minutes, seconds);
	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_free (text);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	const char *text;

	g_return_if_fail (song_info != NULL);

	text = song_info->priv->current_entry->location;

	if (text != NULL) {
		char *tmp, *tmp_utf8;
		char *basename, *dir, *desktopdir;
		
		basename = g_path_get_basename (text);
		tmp = gnome_vfs_unescape_string_for_display (basename);
		g_free (basename);
		tmp_utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, NULL);
		g_free (tmp);
		if (tmp_utf8 != NULL) {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->name), 
					    tmp_utf8);
		} else {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->name), 
					    _("Unknown file name"));
		}

		g_free (tmp_utf8);
	
		tmp = gnome_vfs_get_local_path_from_uri (text);
		if (tmp == NULL)
			tmp = g_strdup (text);
		dir = g_path_get_dirname (tmp);
		g_free (tmp);
		tmp = gnome_vfs_unescape_string_for_display (dir);
		g_free (dir);
		tmp_utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, NULL);
		g_free (tmp);

		desktopdir = g_build_filename (g_get_home_dir (), "Desktop", NULL);
		if ((tmp_utf8 != NULL) && (strcmp (tmp_utf8, desktopdir) == 0))
		{
			g_free (tmp_utf8);
			tmp_utf8 = g_strdup (_("On the desktop"));
		}
		g_free (desktopdir);

		if (tmp_utf8 != NULL) {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->location), 
					    tmp_utf8);
		} else {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->location), 
					    _("Unknown location"));
		}
		g_free (tmp_utf8);
	}
}

static void
rb_song_info_backward_clicked_cb (GtkWidget *button,
				  RBSongInfo *song_info)
{
	song_info->priv->current_entry
		= rb_entry_view_get_previous_from_entry (song_info->priv->entry_view,
						     song_info->priv->current_entry);
	rb_entry_view_select_entry (song_info->priv->entry_view,
				    song_info->priv->current_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view,
				       song_info->priv->current_entry);

	rb_song_info_populate_dialog (song_info);
}

static void
rb_song_info_forward_clicked_cb (GtkWidget *button,
				 RBSongInfo *song_info)
{
	song_info->priv->current_entry
		= rb_entry_view_get_next_from_entry (song_info->priv->entry_view,
						     song_info->priv->current_entry);
	rb_entry_view_select_entry (song_info->priv->entry_view,
				    song_info->priv->current_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view,
				       song_info->priv->current_entry);

	rb_song_info_populate_dialog (song_info);
}

/*
 * rb_song_info_update_buttons: update back/forward sensitivity
 */
static void
rb_song_info_update_buttons (RBSongInfo *song_info)
{
	RhythmDBEntry *entry = NULL;

	g_return_if_fail (song_info != NULL);
	g_return_if_fail (song_info->priv->entry_view != NULL);

	if (!song_info->priv->current_entry)
		return;
	
	/* backward */
	entry = rb_entry_view_get_previous_from_entry (song_info->priv->entry_view,
						       song_info->priv->current_entry);
	
	gtk_widget_set_sensitive (song_info->priv->backward, entry != NULL);
	/* forward */
	entry = rb_entry_view_get_next_from_entry (song_info->priv->entry_view,
						   song_info->priv->current_entry);
	
	gtk_widget_set_sensitive (song_info->priv->forward, entry != NULL);
}

static void
rb_song_info_view_changed_cb (RBEntryView *entry_view,
			      RBSongInfo *song_info)
{
	/* update next button sensitivity */
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_update_play_count (RBSongInfo *song_info)
{
	char *text;
	text = g_strdup_printf ("%ld", song_info->priv->current_entry->play_count);
	gtk_label_set_text (GTK_LABEL (song_info->priv->play_count), text);
	g_free (text);
}

static void
rb_song_info_update_last_played (RBSongInfo *song_info)
{
	const char *str;
	str = rb_refstring_get (song_info->priv->current_entry->last_played_str);
	if (!strcmp ("", str))
		str = _("Never");
	gtk_label_set_text (GTK_LABEL (song_info->priv->last_played), str);
}

static void
rb_song_info_update_rating (RBSongInfo *song_info)
{
	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "rating", song_info->priv->current_entry->rating,
		      NULL);

}

static void
rb_song_info_update_year (RBSongInfo *song_info)
{
	char *text;

	if (song_info->priv->current_entry->date) {
		gulong year = g_date_get_year (song_info->priv->current_entry->date);
		text = g_strdup_printf ("%lu", year);
	} else {
		text = g_strdup (_("Unknown"));
	}
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->year), text);
	g_free (text);
}

static void
rb_song_info_sync_entries_multiple (RBSongInfo *dialog)
{
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist));
	const char *album = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album));	
	const char *year_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->year));

	char *endptr;
	GValue val = {0,};
	GList *tem;
	gint year;
	gboolean changed = FALSE;
	RhythmDBEntry *entry;

	if (strlen (album) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, album);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			if (strcmp (album, rb_refstring_get (entry->album)) == 0)
				continue;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_ALBUM, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
	}
	
	if (strlen (artist) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, artist);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			if (strcmp (artist, rb_refstring_get (entry->artist)) == 0)
				continue;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_ARTIST, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
	}

	if (strlen (genre) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, genre);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			if (strcmp (genre, rb_refstring_get (entry->genre)) == 0)
				continue;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_GENRE, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
	}

	year = g_ascii_strtoull (year_str, &endptr, 10);
	if (strlen (year_str) > 0) {
		GDate *date = NULL;
		GType type;

		/* note: this will reset the day-of-year to Jan 1 for all entries */
		if (year > 0)
			date = g_date_new_dmy (1, G_DATE_JANUARY, year);

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);

		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));

		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_DATE, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
		if (date)
			g_date_free (date);

	}


	if (changed)
		rhythmdb_commit (dialog->priv->db);
}

static void
rb_song_info_sync_entry_single (RBSongInfo *dialog)
{
	const char *title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist));
	const char *album = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album));	
	const char *tracknum_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->track_cur));
	const char *discnum_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->disc_cur));
	const char *year_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->year));
	char *endptr;
	GType type;
	gint tracknum;
	gint discnum;
	gint year;
	GValue val = {0,};
	gboolean changed = FALSE;
	RhythmDBEntry *entry = dialog->priv->current_entry;

	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[PRE_METADATA_CHANGE], 0,
		       entry);

	tracknum = g_ascii_strtoull (tracknum_str, &endptr, 10);
	if ((endptr != tracknum_str) && (tracknum != entry->tracknum)) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_TRACK_NUMBER);
		g_value_init (&val, type);
		g_value_set_ulong (&val, tracknum);
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
		g_value_unset (&val);
		changed = TRUE;
	}
	
	discnum = g_ascii_strtoull (discnum_str, &endptr, 10);
	if ((endptr != discnum_str) && (discnum != entry->discnum)) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DISC_NUMBER);
		g_value_init (&val, type);
		g_value_set_ulong (&val, discnum);
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_DISC_NUMBER, &val);
		g_value_unset (&val);
		changed = TRUE;
	}
	
	year = g_ascii_strtoull (year_str, &endptr, 10);
	if ((endptr != year_str) && 
	    (entry->date == NULL || year != g_date_get_year (entry->date))) {
		GDate *date = NULL;
	
		if (year > 0) {
			if (entry->date) {
				date = g_date_new_julian (g_date_get_julian (entry->date));
				g_date_set_year (date, year);
			} else {
				date = g_date_new_dmy (1, G_DATE_JANUARY, year);
			}
		}

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);
		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_DATE, &val);
		changed = TRUE;
		
		g_value_unset (&val);
		if (date)
			g_date_free (date);
	}


	if (strcmp (title, rb_refstring_get (entry->title))) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_TITLE);
		g_value_init (&val, type);
		g_value_set_string (&val, title);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_TITLE, &val);
		g_value_unset (&val);
		changed = TRUE;
	}


	if (strcmp (album, rb_refstring_get (entry->album))) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_ALBUM);
		g_value_init (&val, type);
		g_value_set_string (&val, album);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_ALBUM, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	if (strcmp (artist, rb_refstring_get (entry->artist))) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_ARTIST);
		g_value_init (&val, type);
		g_value_set_string (&val, artist);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_ARTIST, &val);
		g_value_unset (&val);
		changed = TRUE;
	}


	if (strcmp (artist, rb_refstring_get (entry->artist))) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_GENRE);
		g_value_init (&val, type);
		g_value_set_string (&val, genre);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_GENRE, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	/* FIXME: when an entry is SYNCed, a changed signal is emitted, and
	 * this signal is also emitted, aren't they redundant?
	 */
	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[POST_METADATA_CHANGE], 0,
		       entry);

	if (changed)
		rhythmdb_commit (dialog->priv->db);
}	

static void
rb_song_info_sync_entries (RBSongInfo *dialog)
{
	if (dialog->priv->current_entry)
		rb_song_info_sync_entry_single (dialog);
	else
		rb_song_info_sync_entries_multiple (dialog);
}
