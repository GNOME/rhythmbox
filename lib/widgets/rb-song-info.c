/* 
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
 *  $Id$
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkstock.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>
#include <string.h>
#include <monkey-media-stream-info.h>

#include "rb-file-helpers.h"
#include "rb-song-info.h"
#include "rb-library-preferences.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"

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
static gboolean rb_song_info_window_delete_cb (GtkWidget *window,
					       GdkEventAny *event,
					       RBSongInfo *song_info);
static void rb_song_info_response_cb (GtkDialog *dialog,
				      int response_id,
				      RBSongInfo *song_info);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);

static void rb_song_info_update_title (RBSongInfo *song_info);
static void rb_song_info_update_artist (RBSongInfo *song_info);
static void rb_song_info_update_album (RBSongInfo *song_info);
static void rb_song_info_update_year (RBSongInfo *song_info);
static void rb_song_info_update_track (RBSongInfo *song_info);
static void rb_song_info_update_comments (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_channels (RBSongInfo *song_info);
static void rb_song_info_update_size (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);

struct RBSongInfoPrivate
{
	RBNode *node;
	MonkeyMediaStreamInfo *info;

	/* the dialog widgets */
	GtkWidget *title;
	GtkWidget *artist;
	GtkWidget *album;
	GtkWidget *year;
	GtkWidget *track_cur;
	GtkWidget *track_max;
	GtkWidget *genre;
	GtkWidget *comments;
	GtkWidget *bitrate;
	GtkWidget *channels;
	GtkWidget *size;
	GtkWidget *duration;
	GtkWidget *location;
};

enum 
{
	PROP_0,
	PROP_NODE
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
					 PROP_NODE,
					 g_param_spec_object ("node",
					 "RBNode",
					 "RBNode object",
					 RB_TYPE_NODE,
					 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_song_info_finalize;
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	GladeXML *xml;
	
	song_info->priv = g_new0 (RBSongInfoPrivate, 1);

	g_signal_connect (G_OBJECT (song_info),
			  "delete_event",
			  G_CALLBACK (rb_song_info_window_delete_cb),
			  song_info);
	g_signal_connect (G_OBJECT (song_info),
			  "response",
			  G_CALLBACK (rb_song_info_response_cb),
			  song_info);

	gtk_dialog_add_button (GTK_DIALOG (song_info),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (song_info), _("Song Information"));

	xml = rb_glade_xml_new ("song-info.glade",
				"song_info_vbox",
				song_info);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (song_info)->vbox),
			   glade_xml_get_widget (xml, "song_info_vbox"));

	/* get the widgets */
	song_info->priv->title     = glade_xml_get_widget (xml, "song_info_title");
	song_info->priv->artist    = glade_xml_get_widget (xml, "song_info_artist");
	song_info->priv->album     = glade_xml_get_widget (xml, "song_info_album");
	song_info->priv->year      = glade_xml_get_widget (xml, "song_info_year");
	song_info->priv->track_cur = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->track_max = glade_xml_get_widget (xml, "song_info_track_max");
	song_info->priv->genre     = glade_xml_get_widget (xml, "song_info_genre");
	song_info->priv->comments  = glade_xml_get_widget (xml, "song_info_comments");
	song_info->priv->bitrate   = glade_xml_get_widget (xml, "song_info_bitrate");
	song_info->priv->channels  = glade_xml_get_widget (xml, "song_info_channels");
	song_info->priv->size      = glade_xml_get_widget (xml, "song_info_size");
	song_info->priv->duration  = glade_xml_get_widget (xml, "song_info_duration");
	song_info->priv->location  = glade_xml_get_widget (xml, "song_info_location");

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
	GError *err = NULL;
	GValue location = { 0, };
	RBNode *node;
	MonkeyMediaStreamInfo *info;

	switch (prop_id)
	{
	case PROP_NODE:

		/* get the node */
		node = g_value_get_object (value);
		song_info->priv->node = node;

		/* get the stream info */
		rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &location);
		info = monkey_media_stream_info_new (g_value_get_string(&location), &err);
		g_assert ((err == NULL) && (info != NULL));
		song_info->priv->info = info;
		g_value_unset (&location);
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
	case PROP_NODE:
		g_value_set_object (value, song_info->priv->node);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_song_info_new (RBNode *node)
{
	RBSongInfo *song_info;

	g_return_val_if_fail (node != NULL, NULL);

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO, "node", node, NULL);
	g_return_val_if_fail (song_info->priv != NULL, NULL);

	rb_song_info_populate_dialog (song_info);
	gtk_widget_show (GTK_WIDGET (song_info));

	return GTK_WIDGET (song_info);
}

static gboolean
rb_song_info_window_delete_cb (GtkWidget *window,
				       GdkEventAny *event,
				       RBSongInfo *song_info)
{
	gtk_widget_hide (GTK_WIDGET (song_info));

	return TRUE;
}

static void
rb_song_info_response_cb (GtkDialog *dialog,
				  int response_id,
				  RBSongInfo *song_info)
{
	if (response_id == GTK_RESPONSE_CLOSE)
	{
#if 0
		GValue *value = g_new0 (GValue, 1);
		const char *text = gtk_entry_get_text (GTK_ENTRY (song_info->priv->title));

		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, text);

		g_message ("about to write stuff: %s", text);
		monkey_media_stream_info_set_value (song_info->priv->info,
						    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
						    value);
		g_message ("finished");
		g_value_unset (value);
#endif
		gtk_widget_hide (GTK_WIDGET (song_info));
	}
}

static void 
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	rb_song_info_update_title (song_info);
	rb_song_info_update_artist (song_info);
	rb_song_info_update_album (song_info);
	rb_song_info_update_year (song_info);
	rb_song_info_update_track (song_info);
	// TODO: genre
	rb_song_info_update_comments (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_channels (song_info);
	rb_song_info_update_size (song_info);
	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
}

static void
rb_song_info_update_title (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_artist (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->artist), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_album (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->album), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_year (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->year), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_track (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	char **tokens;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					    &value);
	text = (char*) g_value_get_string (&value);

	if (text != NULL)
	{
		tokens = g_strsplit (text, _("of"), 2);
	
		if (tokens[0] != NULL)
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_cur), tokens[0]);
		if (tokens[1] != NULL)
			gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_max), tokens[1]);
	}
	g_value_unset (&value);
}

static void
rb_song_info_update_comments (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->comments), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,
					    &value);
	text = g_strdup_printf ("%d kbps", g_value_get_int (&value));
	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_channels (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	int channels;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS,
					    &value);
	channels = g_value_get_int (&value);
	switch (channels)
	{
		case 1:
			text = g_strdup_printf (_("%d Mono"), channels);
			break;
		case 2:
			text = g_strdup_printf (_("%d Stereo"), channels);
			break;
		default:
			text = g_strdup_printf ("%d", channels);
			break;
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->channels), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_size (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long size;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    &value);
	size = g_value_get_long (&value);
	if (size > 1024 * 1024)
	{
		text = g_strdup_printf ("%.2f Mo", (float) size / (1024 * 1024));
	}
	else
	{
		text = g_strdup_printf ("%.2f Ko", (float) size / 1024);
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->size), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long duration;
	int minutes, seconds;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    &value);
	duration = g_value_get_long(&value);
	minutes = duration / 60;
	seconds = duration % 60;
	text = g_strdup_printf ("%02d:%02d", minutes, seconds);

	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	g_return_if_fail (song_info != NULL);

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION,
					    &value);
	text = (char*) g_value_get_string (&value);
	if (text != NULL)
		gtk_label_set_text (GTK_LABEL (song_info->priv->location), text);
	g_value_unset (&value);
}
