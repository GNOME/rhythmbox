/*
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
#include <libgnomevfs/gnome-vfs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <string.h>
#include <time.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rhythmdb.h"
#include "rb-song-info.h"
#include "rb-enums.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-ellipsizing-label.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
static GObject *rb_song_info_constructor (GType type, guint n_construct_properties,
					  GObjectConstructParam *construct_properties);

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
static void rb_song_info_update_auto_rate (RBSongInfo *song_info);
static void rb_song_info_update_rating (RBSongInfo *song_info);

static void rb_song_info_backward_clicked_cb (GtkWidget *button,
					      RBSongInfo *song_info);
static void rb_song_info_forward_clicked_cb (GtkWidget *button,
					     RBSongInfo *song_info);
static void rb_song_info_view_changed_cb (RBEntryView *entry_view,
					  RBSongInfo *song_info);
static void rb_song_info_auto_rate_toggled_cb (GtkToggleButton *togglebutton,
					     RBSongInfo *song_info);
static void rb_song_info_rated_cb (RBRating *rating,
				   double score,
				   RBSongInfo *song_info);
static void rb_song_info_mnemonic_cb (GtkWidget *target);
static void rb_song_info_sync_entries (RBSongInfo *dialog);
static void rb_song_info_auto_rate_conf_changed_cb (GConfClient *client,
			       guint cnxn_id,
			       GConfEntry *entry,
			       RBSongInfo *song_info);

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
	GtkWidget   *title_label;
	GtkWidget   *artist;
	GtkWidget   *artist_label;
	GtkWidget   *album;
	GtkWidget   *album_label;
	GtkWidget   *genre;
	GtkWidget   *genre_label;
	GtkWidget   *track_cur;
	GtkWidget   *track_cur_label;

	GtkWidget   *bitrate;
	GtkWidget   *duration;
	GtkWidget   *name;
	GtkWidget   *location;
	GtkWidget   *play_count;
	GtkWidget   *last_played;
	guint        auto_rate_notify_id;
	GtkWidget   *auto_rate_label;
	GtkWidget   *auto_rate;
	GtkWidget   *rating;
};

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

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;
	object_class->constructor = rb_song_info_constructor;

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
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	GtkWidget *close;
	
	/* create the dialog and some buttons backward - forward - close */
	song_info->priv = g_new0 (RBSongInfoPrivate, 1);

	g_signal_connect_object (G_OBJECT (song_info),
				 "response",
				 G_CALLBACK (rb_song_info_response_cb),
				 song_info, 0);

	gtk_dialog_set_has_separator (GTK_DIALOG (song_info), FALSE);

	close = gtk_dialog_add_button (GTK_DIALOG (song_info),
				       GTK_STOCK_CLOSE,
				       GTK_RESPONSE_CLOSE);

	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);

	gtk_container_set_border_width (GTK_CONTAINER (song_info), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (song_info)->vbox), 2);
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
	song_info->priv->title_label   = glade_xml_get_widget (xml, "title_label");
	song_info->priv->track_cur     = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->track_cur_label = glade_xml_get_widget (xml, "trackn_label");
	song_info->priv->bitrate       = glade_xml_get_widget (xml, "song_info_bitrate");
	song_info->priv->duration      = glade_xml_get_widget (xml, "song_info_duration");
	song_info->priv->location = glade_xml_get_widget (xml, "song_info_location");
	song_info->priv->play_count    = glade_xml_get_widget (xml, "song_info_playcount");
	song_info->priv->last_played   = glade_xml_get_widget (xml, "song_info_lastplayed");
	song_info->priv->name = glade_xml_get_widget (xml, "song_info_name");

	/* We add now the Pango attributes (look at bug #99867 and #97061) */
	{
		gchar *str_final;
		GtkWidget *label;

		label = glade_xml_get_widget (xml, "title_label");
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

		label = glade_xml_get_widget (xml, "bitrate_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

	}

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

	rhythmdb_read_lock (song_info->priv->db);
	for (tem = selected_entries; tem; tem = tem->next)
		if (!rhythmdb_entry_is_editable (song_info->priv->db,
						 selected_entries->data)) {
			editable = FALSE;
			break;
		}
	rhythmdb_read_unlock (song_info->priv->db);
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
	song_info->priv->auto_rate = glade_xml_get_widget (xml, "song_info_auto_rate");

        /* We add now the Pango attributes (look at bug #99867 and #97061) */
	{
		gchar *str_final;
		GtkWidget *label;

		label = glade_xml_get_widget (xml, "album_label");
		song_info->priv->album_label = label;
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "artist_label");
		song_info->priv->artist_label = label;
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);
		
		label = glade_xml_get_widget (xml, "genre_label");
		song_info->priv->genre_label = label;
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "auto_rate_label");
		song_info->priv->auto_rate_label = label;
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);

		label = glade_xml_get_widget (xml, "rating_label");
		str_final = g_strdup_printf ("<b>%s</b>",
					     gtk_label_get_label GTK_LABEL (label));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str_final);
		g_free (str_final);
	}

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

	song_info->priv->auto_rate_notify_id
		= eel_gconf_notification_add (CONF_AUTO_RATE,
					      (GConfClientNotifyFunc) rb_song_info_auto_rate_conf_changed_cb,
					      song_info);
	g_signal_connect_object (song_info->priv->auto_rate, "toggled",
				 G_CALLBACK (rb_song_info_auto_rate_toggled_cb),
				 song_info, 0);

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

	eel_gconf_notification_remove (song_info->priv->auto_rate_notify_id);
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
rb_song_info_auto_rate_conf_changed_cb (GConfClient *client,
			       guint cnxn_id,
			       GConfEntry *entry,
			       RBSongInfo *song_info)
{
	rb_song_info_update_auto_rate (song_info);
}

static void
rb_song_info_set_entry_auto_rate (RBSongInfo *song_info,
				  RhythmDBEntry *entry,
				  void *data)
{
	gboolean active = *((gboolean *) data);
	GValue value = { 0, };

	/* set the new value for auto-rate */
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, active);
	rhythmdb_entry_set (song_info->priv->db,
			    entry,
			    RHYTHMDB_PROP_AUTO_RATE,
			    &value);
	g_value_unset (&value);

}

static void
rb_song_info_auto_rate_toggled_cb (GtkToggleButton *togglebutton,
				   RBSongInfo *song_info)
{
	gboolean active;

	g_return_if_fail (RB_IS_SONG_INFO (song_info));
	g_return_if_fail (GTK_IS_TOGGLE_BUTTON (togglebutton));

	active = gtk_toggle_button_get_active (togglebutton);
	rhythmdb_write_lock (song_info->priv->db);
	rb_song_info_selection_for_each (song_info,
					 rb_song_info_set_entry_auto_rate,
					 &active);
	rhythmdb_write_unlock (song_info->priv->db);
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
	rhythmdb_entry_set (info->priv->db,
			    entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	g_value_unset (&value);
	/* since the user changed the rating, stop auto-rating */
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, FALSE);
	rhythmdb_entry_set (info->priv->db,
			    entry,
			    RHYTHMDB_PROP_AUTO_RATE,
			    &value);
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

	rhythmdb_write_lock (song_info->priv->db);
	rb_song_info_selection_for_each (song_info,
					 rb_song_info_set_entry_rating,
					 &score);
	rhythmdb_write_unlock (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "score", score,
		      NULL);

	rb_song_info_update_auto_rate (song_info);
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
	gint num;
	const char *text = NULL;
	char *tmp;
	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);

	if (!song_info->priv->current_entry)
		return;
	
	rhythmdb_read_lock (song_info->priv->db);

	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_TITLE);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);
	
	tmp = g_strdup_printf (_("%s Properties"), text);
	gtk_window_set_title (GTK_WINDOW (song_info), tmp);
	g_free (tmp);

	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_ARTIST);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist), text);
	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_ALBUM);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->album), text);
	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_GENRE);
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->genre), text);

	num = rhythmdb_entry_get_int (song_info->priv->db,
				      song_info->priv->current_entry,
				      RHYTHMDB_PROP_TRACK_NUMBER);
	if (num > 0)
		tmp = g_strdup_printf ("%.2d", num);
	else
		tmp = g_strdup (_("Never"));
	gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_cur),
			    tmp);
	g_free (tmp);

	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
	rb_song_info_update_play_count (song_info);
	rb_song_info_update_last_played (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_auto_rate (song_info);
	rb_song_info_update_rating (song_info);
	rhythmdb_read_unlock (song_info->priv->db);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	char *tmp = NULL;
	int bitrate = 0;
	bitrate = rhythmdb_entry_get_int (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_BITRATE);

	if (bitrate > 0)
		tmp = g_strdup_printf (_("%d kbps"), bitrate);
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
	duration = rhythmdb_entry_get_long (song_info->priv->db,
					    song_info->priv->current_entry,
					    RHYTHMDB_PROP_DURATION);
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
	char *basename, *dir, *desktopdir;

	g_return_if_fail (song_info != NULL);

	rhythmdb_read_lock (song_info->priv->db);
	text = rhythmdb_entry_get_string (song_info->priv->db,
					  song_info->priv->current_entry,
					  RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (song_info->priv->db);

	if (text != NULL) {
		char *tmp;
		
		basename = g_path_get_basename (text);
		tmp = gnome_vfs_unescape_string_for_display (basename);
		g_free (basename);

		if (tmp != NULL) {
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->name), tmp);
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
		
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->location), tmp);
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
	text = g_strdup_printf ("%d", rhythmdb_entry_get_int (song_info->priv->db,
							      song_info->priv->current_entry,
							      RHYTHMDB_PROP_PLAY_COUNT));
	gtk_label_set_text (GTK_LABEL (song_info->priv->play_count), text);
	g_free (text);
}

static void
rb_song_info_update_last_played (RBSongInfo *song_info)
{
	const char *str;
	str = rhythmdb_entry_get_string (song_info->priv->db,
					 song_info->priv->current_entry,
					 RHYTHMDB_PROP_LAST_PLAYED_STR);
	if (!strcmp ("", str))
		str = _("Never");
	gtk_label_set_text (GTK_LABEL (song_info->priv->last_played), str);
}

static void
rb_song_info_update_auto_rate_single (RBSongInfo *song_info)
{
	gboolean auto_rate;
	gboolean global_auto_rate;
	
	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	auto_rate = rhythmdb_entry_get_boolean (song_info->priv->db,
					    song_info->priv->current_entry,
					    RHYTHMDB_PROP_AUTO_RATE);

	/* We have to block our signal handlers from thinking this
	   is a user-originated setting */
	g_signal_handlers_block_by_func (song_info->priv->auto_rate,
		      rb_song_info_auto_rate_toggled_cb, song_info);
	g_object_set (G_OBJECT (song_info->priv->auto_rate),
		      "active", auto_rate,
		      NULL);
	g_signal_handlers_unblock_by_func (song_info->priv->auto_rate,
		      rb_song_info_auto_rate_toggled_cb, song_info);
	
	global_auto_rate = eel_gconf_get_boolean (CONF_AUTO_RATE);
	gtk_widget_set_sensitive (song_info->priv->auto_rate_label, global_auto_rate);
	gtk_widget_set_sensitive (song_info->priv->auto_rate, global_auto_rate);
}

static void
rb_song_info_update_auto_rate_multiple (RBSongInfo *song_info)
{
	gboolean first_auto_rate;
	gboolean global_auto_rate;
	gboolean inconsistent = FALSE;
	GList *tem;
	
	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	first_auto_rate = rhythmdb_entry_get_boolean (song_info->priv->db,
						      song_info->priv->selected_entries->data,
						      RHYTHMDB_PROP_AUTO_RATE);
	for (tem = song_info->priv->selected_entries; tem; tem = tem->next) {
		gboolean auto_rate
			= rhythmdb_entry_get_boolean (song_info->priv->db,
						      tem->data,
						      RHYTHMDB_PROP_AUTO_RATE);
		if (auto_rate != first_auto_rate) {
			inconsistent = TRUE;
			break;
		}
	}

	gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (song_info->priv->auto_rate),
					    inconsistent);
					    
	if (!inconsistent) {
		/* We have to block our signal handlers from thinking this
		   is a user-originated setting */
		g_signal_handlers_block_by_func (song_info->priv->auto_rate,
						 rb_song_info_auto_rate_toggled_cb, song_info);
		g_object_set (G_OBJECT (song_info->priv->auto_rate),
			      "active", first_auto_rate,
			      NULL);
		g_signal_handlers_unblock_by_func (song_info->priv->auto_rate,
						   rb_song_info_auto_rate_toggled_cb, song_info);
	}
	
	global_auto_rate = eel_gconf_get_boolean (CONF_AUTO_RATE);
	gtk_widget_set_sensitive (song_info->priv->auto_rate_label, global_auto_rate);
	gtk_widget_set_sensitive (song_info->priv->auto_rate, global_auto_rate);
}

static void
rb_song_info_update_auto_rate (RBSongInfo *song_info)
{
	if (song_info->priv->current_entry)
		rb_song_info_update_auto_rate_single (song_info);
	else
		rb_song_info_update_auto_rate_multiple (song_info);
}

static void
rb_song_info_update_rating (RBSongInfo *song_info)
{
	gdouble rating;
	
	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	rating = rhythmdb_entry_get_double (song_info->priv->db,
					    song_info->priv->current_entry,
					    RHYTHMDB_PROP_RATING);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "score", rating,
		      NULL);

}

static void
rb_song_info_sync_entries_multiple (RBSongInfo *dialog)
{
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist));
	const char *album = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album));	
	GValue val = {0,};
	GList *tem;

	rhythmdb_write_lock (dialog->priv->db);

	if (strlen (album) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, album);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next)
			rhythmdb_entry_set (dialog->priv->db,
					    tem->data, RHYTHMDB_PROP_ALBUM, &val);
		g_value_unset (&val);
	}
	
	if (strlen (artist) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, artist);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next)
			rhythmdb_entry_set (dialog->priv->db,
					    tem->data, RHYTHMDB_PROP_ARTIST, &val);
		g_value_unset (&val);
	}

	if (strlen (genre) > 0) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, genre);
		for (tem = dialog->priv->selected_entries; tem; tem = tem->next)
			rhythmdb_entry_set (dialog->priv->db,
					    tem->data, RHYTHMDB_PROP_GENRE, &val);
		g_value_unset (&val);
	}
	
	rhythmdb_write_unlock (dialog->priv->db);
}

static void
rb_song_info_sync_entry_single (RBSongInfo *dialog)
{
	const char *title = gtk_entry_get_text (GTK_ENTRY (dialog->priv->title));
	const char *genre = gtk_entry_get_text (GTK_ENTRY (dialog->priv->genre));
	const char *artist = gtk_entry_get_text (GTK_ENTRY (dialog->priv->artist));
	const char *album = gtk_entry_get_text (GTK_ENTRY (dialog->priv->album));	
	const char *tracknum_str = gtk_entry_get_text (GTK_ENTRY (dialog->priv->track_cur));
	char *endptr;
	gint tracknum;
	GValue val = {0,};

	tracknum = g_ascii_strtoull (tracknum_str, &endptr, 10);
	if (endptr == tracknum_str)
		tracknum = -1;

	rhythmdb_write_lock (dialog->priv->db);
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, title);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, tracknum);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
	g_value_unset (&val);

	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[PRE_METADATA_CHANGE], 0,
		       dialog->priv->current_entry);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, album);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, artist);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, genre);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[POST_METADATA_CHANGE], 0,
		       dialog->priv->current_entry);

	rhythmdb_write_unlock (dialog->priv->db);
}	

static void
rb_song_info_sync_entries (RBSongInfo *dialog)
{
	if (dialog->priv->current_entry)
		rb_song_info_sync_entry_single (dialog);
	else
		rb_song_info_sync_entries_multiple (dialog);
}
