/*
 *  arch-tag: Implementation of local song properties dialog
 *
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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
#include <gtk/gtkstock.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkbbox.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktable.h>
#include <glade/glade.h>
#include <string.h>
#include <time.h>
#include <monkey-media-stream-info.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rhythmdb.h"
#include "rb-song-info.h"
#include "rb-enums.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-ellipsizing-label.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
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
static void rb_song_info_update_title (RBSongInfo *song_info);
static void rb_song_info_update_track (RBSongInfo *song_info);
static void rb_song_info_update_quality (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_genre (RBSongInfo *song_info);
static void rb_song_info_update_play_count (RBSongInfo *song_info);
static void rb_song_info_update_last_played (RBSongInfo *song_info);
static void rb_song_info_update_encoding (RBSongInfo *song_info);
static void rb_song_info_update_entry (RBSongInfo *song_info,
		                       MonkeyMediaStreamInfoField field,
		                       GtkWidget *widget);
static void rb_song_info_update_comments (RBSongInfo *song_info);
static void rb_song_info_update_buttons (RBSongInfo *song_info);
static void rb_song_info_update_rating (RBSongInfo *song_info);
static gboolean rb_song_info_update_current_values (RBSongInfo *song_info);

static void rb_song_info_backward_clicked_cb (GtkWidget *button,
					      RBSongInfo *song_info);
static void rb_song_info_forward_clicked_cb (GtkWidget *button,
					     RBSongInfo *song_info);
static void rb_song_info_view_changed_cb (RBEntryView *entry_view,
					  RBSongInfo *song_info);
static void rb_song_info_rated_cb (RBRating *rating,
				   int score,
				   RBSongInfo *song_info);
static void rb_song_info_mnemonic_cb (GtkWidget *target);
static void cleanup (RBSongInfo *dlg);

struct RBSongInfoPrivate
{
	RhythmDB *db;
	RBEntryView *entry_view;

	/* information on the displayed song */
	RhythmDBEntry *current_entry;
	MonkeyMediaStreamInfo *current_info;

	/* the dialog widgets */
	GtkWidget   *backward;
	GtkWidget   *forward;

	GtkWidget   *title;
	GtkWidget   *artist;
	GtkWidget   *album;
	GtkWidget   *date;
	GtkWidget   *track_cur;
	GtkWidget   *track_max;
	GtkWidget   *genre;
	GtkWidget   *comments;

	GtkWidget   *quality;
	GtkWidget   *duration;
	GtkWidget   *name;
	GtkWidget   *location;
	GtkWidget   *play_count;
	GtkWidget   *last_played;
	GtkWidget   *encoding;
	GtkWidget   *rating;
};

enum 
{
	PROP_0,
	PROP_ENTRY_VIEW
};

static GObjectClass *parent_class = NULL;

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

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry_view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_song_info_finalize;
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	GladeXML *xml;
	GtkWidget *close, *cont;
	
	/* create the dialog and some buttons backward - forward - close */
	song_info->priv = g_new0 (RBSongInfoPrivate, 1);

	g_signal_connect (G_OBJECT (song_info),
			  "response",
			  G_CALLBACK (rb_song_info_response_cb),
			  song_info);

	gtk_dialog_set_has_separator (GTK_DIALOG (song_info), FALSE);

	song_info->priv->backward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   GTK_STOCK_GO_BACK,
							   GTK_RESPONSE_NONE);
	
	g_signal_connect (G_OBJECT (song_info->priv->backward),
			  "clicked",
			  G_CALLBACK (rb_song_info_backward_clicked_cb),
			  song_info);
	
	song_info->priv->forward = gtk_dialog_add_button (GTK_DIALOG (song_info),
							   GTK_STOCK_GO_FORWARD,
							   GTK_RESPONSE_NONE);
	
	g_signal_connect (G_OBJECT (song_info->priv->forward),
			  "clicked",
			  G_CALLBACK (rb_song_info_forward_clicked_cb),
			  song_info);

	close = gtk_dialog_add_button (GTK_DIALOG (song_info),
				       GTK_STOCK_CLOSE,
				       GTK_RESPONSE_CLOSE);

	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);

	gtk_container_set_border_width (GTK_CONTAINER (song_info), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (song_info)->vbox), 2);

	gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));

	xml = rb_glade_xml_new ("song-info.glade",
				"song_info_vbox",
				song_info);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (song_info)->vbox),
			   glade_xml_get_widget (xml, "song_info_vbox"));

	/* get the widgets from the XML */
	song_info->priv->title         = glade_xml_get_widget (xml, "song_info_title");
	song_info->priv->artist        = glade_xml_get_widget (xml, "song_info_artist");
	song_info->priv->album         = glade_xml_get_widget (xml, "song_info_album");
	song_info->priv->date          = glade_xml_get_widget (xml, "song_info_date");
	song_info->priv->track_cur     = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->track_max     = glade_xml_get_widget (xml, "song_info_track_max");
	song_info->priv->genre         = glade_xml_get_widget (xml, "song_info_genre");
	song_info->priv->comments      = glade_xml_get_widget (xml, "song_info_comments");
	song_info->priv->quality       = glade_xml_get_widget (xml, "song_info_quality");
	song_info->priv->duration      = glade_xml_get_widget (xml, "song_info_duration");
	cont = glade_xml_get_widget (xml, "song_info_location_container");
	song_info->priv->location = rb_ellipsizing_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (song_info->priv->location), 0.0, 0.5);
	gtk_label_set_selectable (GTK_LABEL (song_info->priv->location), TRUE);
	gtk_container_add (GTK_CONTAINER (cont), song_info->priv->location);
	gtk_widget_show (song_info->priv->location);
	song_info->priv->play_count    = glade_xml_get_widget (xml, "song_info_playcount");
	song_info->priv->last_played   = glade_xml_get_widget (xml, "song_info_lastplayed");
	song_info->priv->encoding      = glade_xml_get_widget (xml, "song_info_encoding");
	cont = glade_xml_get_widget (xml, "song_info_name_container");
	song_info->priv->name = rb_ellipsizing_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (song_info->priv->name), 0.0, 0.5);
	gtk_label_set_selectable (GTK_LABEL (song_info->priv->name), TRUE);
	gtk_container_add (GTK_CONTAINER (cont), song_info->priv->name);
	gtk_widget_show (song_info->priv->name);

	/* We add now the Pango attributes (look at bug #99867 and #97061) */
	{
		gchar *str_final;
		GtkWidget *label;

		label = glade_xml_get_widget (xml, "comments_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "date_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "album_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "artist_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);
		
		label = glade_xml_get_widget (xml, "title_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "genre_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "trackn_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "name_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "rating_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "location_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "last_played_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "play_count_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "duration_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "quality_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "encoding_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);
		
	}

	/* make those fields not editable for now */
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->title), FALSE);
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->artist), FALSE);
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->album), FALSE);
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->date), FALSE);
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->track_cur), FALSE);
	gtk_entry_set_editable (GTK_ENTRY (song_info->priv->track_max), FALSE);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (song_info->priv->comments), FALSE);

	/* whenever you press a mnemonic, the associated GtkEntry's text gets highlighted */
	g_signal_connect (G_OBJECT (song_info->priv->title),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->artist),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->album),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->date),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->track_cur),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->track_max),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (GTK_COMBO (song_info->priv->genre)->entry),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);
	g_signal_connect (G_OBJECT (song_info->priv->comments),
			  "mnemonic-activate",
			  G_CALLBACK (rb_song_info_mnemonic_cb),
			  NULL);

	/* this widget has to be customly created */
	song_info->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (song_info->priv->rating, 
				 "rated",
				 G_CALLBACK (rb_song_info_rated_cb),
				 G_OBJECT (song_info), 0);
	gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (xml, "song_info_rating_container")),
			   song_info->priv->rating);

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->title);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_song_info_finalize (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->current_info != NULL)
	{
		cleanup (song_info);
	}

	g_free (song_info->priv);

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
			RBEntryView *entry_view = g_value_get_object (value);
			song_info->priv->entry_view = entry_view;
			g_object_get (G_OBJECT (entry_view), "db", &song_info->priv->db, NULL);

			rb_song_info_update_current_values (song_info);

			g_signal_connect_object (G_OBJECT (entry_view),
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

static void
rb_song_info_response_cb (GtkDialog *dialog,
			  int response_id,
			  RBSongInfo *song_info)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
rb_song_info_rated_cb (RBRating *rating,
		       int score,
		       RBSongInfo *song_info)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_SONG_INFO (song_info));
	g_return_if_fail (score >= 0 && score <= 5 );

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, score);
	rhythmdb_write_lock (song_info->priv->db);
	rhythmdb_entry_set (song_info->priv->db,
			    song_info->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	g_value_unset (&value);
	rhythmdb_write_unlock (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "score", score,
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
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);
	
	/* update the fields values */
	rb_song_info_update_title (song_info);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
				   song_info->priv->artist);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
				   song_info->priv->album);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,
				   song_info->priv->date);
	rb_song_info_update_track (song_info);
	rb_song_info_update_genre (song_info);
	rb_song_info_update_comments (song_info);
	rb_song_info_update_quality (song_info);
	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
	rb_song_info_update_play_count (song_info);
	rb_song_info_update_last_played (song_info);
	rb_song_info_update_encoding (song_info);
	rb_song_info_update_rating (song_info);
}

static void
rb_song_info_update_title (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text = NULL;
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
							  0, &value);
	}

	if (res == TRUE)
	{
		text = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	if (text != NULL)
	{
		char *tmp;
		
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);

		tmp = g_strdup_printf (_("%s Properties"), text);
		gtk_window_set_title (GTK_WINDOW (song_info), tmp);
		g_free (tmp);
	}
	else
	{
		if (song_info->priv->current_entry != NULL)
		{
			const char *url;
			char *tmp;

			rhythmdb_read_lock (song_info->priv->db);

			url = rhythmdb_entry_get_string (song_info->priv->db,
							 song_info->priv->current_entry,
							 RHYTHMDB_PROP_LOCATION);
			rhythmdb_read_unlock (song_info->priv->db);

			tmp = g_strdup_printf (_("%s Properties"), url);
			gtk_window_set_title (GTK_WINDOW (song_info), tmp);
			g_free (tmp);
		}
		else
			gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));
	}

	g_value_unset (&value);
}

static void
rb_song_info_update_comments (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text = NULL;
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,
							  0, &value);
	}
	if (res == TRUE)
	{
		text = g_value_get_string (&value); 
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (song_info->priv->comments));

	gtk_text_buffer_get_bounds (buffer, &start, &end);

	gtk_text_buffer_delete (buffer, &start, &end);

	if (text != NULL)
		gtk_text_buffer_insert (buffer, &start, text, -1);

	g_value_unset (&value);
}

static void
rb_song_info_update_track (RBSongInfo *song_info)
{
	int val = 0;
	GValue value = { 0, };
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL) {
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
							  0, &value);
	}

	if (res == TRUE) {
		val = g_value_get_int (&value);
	} else {
		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, 0);
	}

	if (val >= 0) {
		char *tmp;
		tmp = g_strdup_printf ("%.2d", val);
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_cur), tmp);
		g_free (tmp);
	}

	g_value_unset (&value);

	res = FALSE;
	if (song_info->priv->current_info != NULL) {
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,
							  0, &value);
	}

	if (res == TRUE) {
		val = g_value_get_int (&value);
	} else {
		val = 0;
		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, 0);
	}

	if (val > 0) {
		char *tmp;
		tmp = g_strdup_printf ("%.2d", val);
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_max), tmp);
		g_free (tmp);
	} else {
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_max), "");
	}

	g_value_unset (&value);
}

static void
rb_song_info_update_entry (RBSongInfo *song_info,
		           MonkeyMediaStreamInfoField field,
		           GtkWidget *widget)
{
	GValue value = { 0, };
	const char *text = NULL;
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  field, 0, &value);
	}

	if (res == TRUE)
	{
		text = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (widget), text);

	g_value_unset (&value);
}

static void
rb_song_info_update_genre (RBSongInfo *song_info)
{
	const char *genre = NULL;
	GValue value = { 0, };
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
							  0, &value);
	}

	if (res == TRUE)
	{
		genre = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	gtk_combo_set_popdown_strings (GTK_COMBO (song_info->priv->genre),
				       monkey_media_stream_info_list_all_genres ());

	if (genre != NULL)
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (song_info->priv->genre)->entry),
				    genre);
	
	g_value_unset (&value);
}

static void
rb_song_info_update_quality (RBSongInfo *song_info)
{
	char *text = NULL;
	GValue value = { 0, };
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY,
							  0, &value);
	}

	if (res == TRUE)
	{
		text = monkey_media_audio_quality_to_string (g_value_get_enum (&value));
	}
	else
	{
		g_value_init (&value, G_TYPE_INT);
		g_value_set_string (&value, 0);
	}

	if (text != NULL)
	{
		gtk_label_set_text (GTK_LABEL (song_info->priv->quality), _(text));
		g_free (text);
	}

	g_value_unset (&value);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text = NULL;
	long duration = 0;
	int minutes, seconds;
	gboolean res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
							  0, &value);
	}

	if (res == TRUE)
	{
		duration = g_value_get_long (&value);
		minutes = duration / 60;
		seconds = duration % 60;
		text = g_strdup_printf ("%d:%02d", minutes, seconds);
	}
	else
	{
		g_value_init (&value, G_TYPE_LONG);
		g_value_set_long (&value, 0);
	}


	if (text != NULL)
	{
		gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
		g_free (text);
	}

	g_value_unset (&value);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	const char *text;
	char *basename, *dir, *desktopdir;

	g_return_if_fail (song_info != NULL);

	rhythmdb_read_lock (song_info->priv->db);
	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (song_info->priv->db);

	if (text != NULL)
	{
		char *tmp;
		
		basename = g_path_get_basename (text);
		tmp = gnome_vfs_unescape_string_for_display (basename);
		g_free (basename);

		if (tmp != NULL)
		{
			rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (song_info->priv->name), RB_ELLIPSIZE_END);
			rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (song_info->priv->name), tmp);
		}

		g_free (tmp);
	
		tmp = gnome_vfs_get_local_path_from_uri (text);
		if (tmp == NULL)
			tmp = g_strdup (text);
		dir = g_path_get_dirname (tmp);
		g_free (tmp);
		tmp = gnome_vfs_unescape_string_for_display (dir);
		g_free (dir);

		desktopdir = g_build_filename (g_get_home_dir (), ".gnome-desktop", NULL);
		if (strcmp (tmp, desktopdir) == 0)
		{
			g_free (tmp);
			tmp = g_strdup (_("on the desktop"));
		}
		g_free (desktopdir);
		
		rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (song_info->priv->location), RB_ELLIPSIZE_END);
		rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (song_info->priv->location), tmp);
		g_free (tmp);
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

	cleanup (song_info);

	if (rb_song_info_update_current_values (song_info) == TRUE)
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

	cleanup (song_info);

	if (rb_song_info_update_current_values (song_info) == TRUE)
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
	g_return_if_fail (song_info->priv->current_entry != NULL);

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

/*
 * rb_song_info_update_current_values: get the selection from
 * the node view and then updates current_node & current_info
 */
static gboolean
rb_song_info_update_current_values (RBSongInfo *song_info)
{
	const char *url;
	RhythmDBEntry *entry = NULL;
	MonkeyMediaStreamInfo *info;
	GList *selected_entries;

	selected_entries = rb_entry_view_get_selected_entries (song_info->priv->entry_view);

	if ((selected_entries == NULL) ||
	    (selected_entries->data == NULL))
	{
		song_info->priv->current_info = NULL;
		song_info->priv->current_entry = NULL;
		gtk_widget_destroy (GTK_WIDGET (song_info));

		return FALSE;
	}

	song_info->priv->current_entry = entry = selected_entries->data;

	/* get the stream info */
	rhythmdb_read_lock (song_info->priv->db);
	url = rhythmdb_entry_get_string (song_info->priv->db,
					  entry,
					  RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (song_info->priv->db);

	info = monkey_media_stream_info_new (url, NULL);
	song_info->priv->current_info = info;

	return TRUE;
}

static void
rb_song_info_update_play_count (RBSongInfo *song_info)
{
	char *text;

	rhythmdb_read_lock (song_info->priv->db);

	text = g_strdup_printf ("%d", rhythmdb_entry_get_int (song_info->priv->db,
							      song_info->priv->current_entry,
							      RHYTHMDB_PROP_PLAY_COUNT));
	rhythmdb_read_unlock (song_info->priv->db);

	gtk_label_set_text (GTK_LABEL (song_info->priv->play_count), text);
}

static void
rb_song_info_update_last_played (RBSongInfo *song_info)
{
	const char *str;
	rhythmdb_read_lock (song_info->priv->db);
	str = rhythmdb_entry_get_string (song_info->priv->db,
					 song_info->priv->current_entry,
					 RHYTHMDB_PROP_LAST_PLAYED_STR);
	rhythmdb_read_unlock (song_info->priv->db);
	gtk_label_set_text (GTK_LABEL (song_info->priv->last_played), str);
}

static void
rb_song_info_update_rating (RBSongInfo *song_info)
{
	guint rating;
	
	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	rhythmdb_read_lock (song_info->priv->db);
	rating = rhythmdb_entry_get_int (song_info->priv->db,
					 song_info->priv->current_entry,
					 RHYTHMDB_PROP_RATING);
	rhythmdb_read_unlock (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "score", rating,
		      NULL);
}

static void
rb_song_info_update_encoding (RBSongInfo *song_info)
{
	const char *text_codec = NULL;
	int channels = -1, bitrate = -1;
	GValue value = { 0, };
	gboolean res = FALSE;
	GString *string;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,
							  0, &value);
	}

	if (res == TRUE)
	{
		bitrate = g_value_get_int (&value);
		g_value_unset (&value);
	}

	res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS,
							  0, &value);
	}

	if (res == TRUE)
	{
		channels = g_value_get_int (&value);
		g_value_unset (&value);
	}

	res = FALSE;

	if (song_info->priv->current_info != NULL)
	{
		res = monkey_media_stream_info_get_value (song_info->priv->current_info, 
							  MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO,
							  0, &value);
	}

	if (res == TRUE)
	{
		text_codec = g_value_get_string (&value);
	}

	string = g_string_new ("");

	if (bitrate >= 0)
	{
		g_string_append_printf (string, _("%d kbps"), bitrate);
	}

	if (text_codec != NULL)
	{
		g_string_append (string, text_codec);
	}

	if (channels >= 0)
	{
		switch (channels)
		{
		case 1:
			g_string_append (string, _(" (Mono)"));
			break;
		case 2:
			g_string_append (string, _(" (Stereo)"));
			break;
		default:
			g_string_append_printf (string,
						ngettext (" (%d channel)", " (%d channel)", channels),
						channels);
			break;
		}
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->encoding), string->str);
		
	g_string_free (string, TRUE);

	if (res == TRUE)
		g_value_unset (&value);
}

static void
cleanup (RBSongInfo *dlg)
{
	if (dlg->priv->current_info != NULL)
		g_object_unref (G_OBJECT (dlg->priv->current_info));
}
