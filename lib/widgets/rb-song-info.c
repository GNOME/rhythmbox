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
#include <gtk/gtkentry.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmenuitem.h>
#include <glade/glade.h>
#include <string.h>
#include <monkey-media-stream-info.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-song-info.h"
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
static void rb_song_info_response_cb (GtkDialog *dialog,
				      int response_id,
				      RBSongInfo *song_info);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);
static void rb_song_info_update_track (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_channels (RBSongInfo *song_info);
static void rb_song_info_update_size (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_genre (RBSongInfo *song_info);
static void rb_song_info_update_entry (RBSongInfo *song_info,
		                       MonkeyMediaStreamInfoField field,
		                       GtkWidget *widget);

struct RBSongInfoPrivate
{
	RBNode *node;
	MonkeyMediaStreamInfo *info;

	/* the dialog widgets */
	GtkTooltips *tooltips;
	GtkWidget *title;
	GtkWidget *artist;
	GtkWidget *album;
	GtkWidget *date;
	GtkWidget *track_cur;
	GtkWidget *track_max;
	GtkWidget *genre;
	GtkWidget *comments;
	GtkWidget *bitrate;
	GtkWidget *channels;
	GtkWidget *size;
	GtkWidget *duration;
	GtkWidget *location_ebox;
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
	song_info->priv->tooltips      = gtk_tooltips_new ();
	song_info->priv->title         = glade_xml_get_widget (xml, "song_info_title");
	song_info->priv->artist        = glade_xml_get_widget (xml, "song_info_artist");
	song_info->priv->album         = glade_xml_get_widget (xml, "song_info_album");
	song_info->priv->date          = glade_xml_get_widget (xml, "song_info_date");
	song_info->priv->track_cur     = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->track_max     = glade_xml_get_widget (xml, "song_info_track_max");
	song_info->priv->genre         = glade_xml_get_widget (xml, "song_info_genre");
	song_info->priv->comments      = glade_xml_get_widget (xml, "song_info_comments");
	song_info->priv->bitrate       = glade_xml_get_widget (xml, "song_info_bitrate");
	song_info->priv->channels      = glade_xml_get_widget (xml, "song_info_channels");
	song_info->priv->size          = glade_xml_get_widget (xml, "song_info_size");
	song_info->priv->duration      = glade_xml_get_widget (xml, "song_info_duration");
	song_info->priv->location_ebox = glade_xml_get_widget (xml, "song_info_location_ebox");
	song_info->priv->location      = glade_xml_get_widget (xml, "song_info_location");

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

	g_object_unref (G_OBJECT (song_info->priv->info));

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
	case PROP_NODE:
		{
			GValue location = { 0, };
			RBNode *node;
			MonkeyMediaStreamInfo *info;

			/* get the node */
			node = g_value_get_object (value);
			song_info->priv->node = node;

			/* get the stream info */
			rb_node_get_property (node, RB_NODE_PROPERTY_SONG_LOCATION, &location);
			info = monkey_media_stream_info_new (g_value_get_string (&location), NULL);
			song_info->priv->info = info;
			g_value_unset (&location);
			
			/* and fill it in */
			rb_song_info_populate_dialog (song_info);
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

	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO, "node", node, NULL);

	g_return_val_if_fail (song_info->priv != NULL, NULL);

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
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
				   song_info->priv->title);
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
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,
				   song_info->priv->comments);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_channels (song_info);
	rb_song_info_update_size (song_info);
	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
}

static void
rb_song_info_update_entry (RBSongInfo *song_info,
		           MonkeyMediaStreamInfoField field,
		           GtkWidget *widget)
{
	GValue value = { 0, };
	const char *text = NULL;

	if (song_info->priv->info != NULL)
	{
		monkey_media_stream_info_get_value (song_info->priv->info, 
						    field,
						    &value);
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
rb_song_info_update_track (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text;
	char **tokens;

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					    &value);
	text = g_value_get_string (&value);

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
rb_song_info_update_genre (RBSongInfo *song_info)
{
	GtkWidget *menu;
	GList *l;
	int index = -1, i = 0;
	const char *genre;
	GValue value = { 0, };

	menu = gtk_menu_new ();

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    &value);
	genre = g_value_get_string (&value);
	
	for (l = monkey_media_stream_info_list_all_genres (); l != NULL; l = g_list_next (l))
	{
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (l->data);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);

		if (genre == NULL && strcmp (_("Unknown"), l->data))
			index = i;
		if (genre != NULL && strcmp (genre, l->data) == 0)
			index = i;
		i++;
	}

	g_value_unset (&value);
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (song_info->priv->genre),
				  menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (song_info->priv->genre),
				     index);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,
					    &value);
	text = g_strdup_printf (_("%d kbps"), g_value_get_int (&value));
	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_channels (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	int channels;

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
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_size (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long size;

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    &value);
	size = g_value_get_long (&value);
	text = gnome_vfs_format_file_size_for_display (size);
	gtk_label_set_text (GTK_LABEL (song_info->priv->size), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long duration;
	int minutes, seconds;

	monkey_media_stream_info_get_value (song_info->priv->info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    &value);
	duration = g_value_get_long (&value);
	minutes = duration / 60;
	seconds = duration % 60;
	text = g_strdup_printf ("%d:%02d", minutes, seconds);

	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text;
	char *basename;

	g_return_if_fail (song_info != NULL);

	rb_node_get_property (song_info->priv->node,
			      RB_NODE_PROPERTY_SONG_LOCATION,
			      &value);
	text = g_value_get_string (&value);
	if (text != NULL)
	{
		char *tmp;
		
		basename = g_path_get_basename (text);
		tmp = gnome_vfs_unescape_string_for_display (basename);
		g_free (basename);
		gtk_label_set_text (GTK_LABEL (song_info->priv->location), tmp);
		g_free (tmp);
	
		tmp = gnome_vfs_unescape_string_for_display (text);
		gtk_tooltips_set_tip (song_info->priv->tooltips,
				      song_info->priv->location_ebox,
				      tmp, NULL);
		g_free (tmp);
	}
	g_value_unset (&value);
}
