/*
 *  Copyright © 2002 Jorn Baayen.  All rights reserved.
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

/* FIXME support duplicates..... */
/* FIXME give playing row different bg color */
/* FIXME different icon for playing and paused */
/* FIXME node_activated */
/* FIXME eos */
/* FIXME tooltips on buttons */
/* FIXME Whole track foo of bar thing should ellipsize */
/* FIXME fix rblink markup */
/* FIXME reintroduce elapsed time goodies */
/* FIXME update window title again */
/* FIXME better pause indication */

#include <gtk/gtkvbox.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhscale.h>
#include <gtk/gtknotebook.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>

#include "rb-player.h"
#include "rb-stock-icons.h"
#include "rb-file-helpers.h"
#include "rb-node-view.h"
#include "rb-link.h"
#include "rb-ellipsizing-label.h"
#include "rb-library-dnd-types.h"
#include "rb-dialog.h"

static void rb_player_class_init (RBPlayerClass *klass);
static void rb_player_init (RBPlayer *player);
static void rb_player_finalize (GObject *object);
static void rb_player_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_player_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
			            GParamSpec *pspec);
static void drag_data_received_cb (GtkWidget *widget,
				   GdkDragContext *context,
			           int x, int y,
			           GtkSelectionData *data,
			           guint info, guint time,
			           RBPlayer *player);
static void update_buttons (RBPlayer *player);
static void set_playing (RBPlayer *player, RBNode *song);
static void play_cb (GtkWidget *widget, RBPlayer *player);
static void pause_cb (GtkWidget *widget, RBPlayer *player);
static void previous_cb (GtkWidget *widget, RBPlayer *player);
static void next_cb (GtkWidget *widget, RBPlayer *player);
static void nullify_info (RBPlayer *player);
static gboolean sync_time (RBPlayer *player);

struct RBPlayerPrivate
{
	RB *rb;

	GtkWidget *info_notebook;
	GtkWidget *song_label;
	GtkWidget *from_label;
	GtkWidget *album_link;
	GtkWidget *artist_link;
	GtkAdjustment *song_adjustment;
	GtkWidget *song_scale;
	gboolean slider_locked;
	GtkWidget *time_label;

	GtkWidget *first_button_box;

	GtkWidget *previous;
	GtkWidget *play;
	GtkWidget *pause;
	GtkWidget *next;

	GtkWidget *second_button_box;

	GtkWidget *shuffle;
	GtkWidget *repeat;

	RBNode *playlist;
	RBNodeView *playlist_view;

	MonkeyMediaAudioStream *stream;
	RBNode *playing;

	MonkeyMediaMixer *mixer;

	guint timeout;
};

enum
{
	PROP_0,
	PROP_RB
};

static GObjectClass *parent_class = NULL;

static const GtkTargetEntry target_table[] =
	{ { RB_LIBRARY_DND_NODE_ID_TYPE, 0, RB_LIBRARY_DND_NODE_ID } };

GType
rb_player_get_type (void)
{
	static GType rb_player_type = 0;

	if (rb_player_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_player_class_init,
			NULL,
			NULL,
			sizeof (RBPlayer),
			0,
			(GInstanceInitFunc) rb_player_init
		};

		rb_player_type = g_type_register_static (GTK_TYPE_HBOX,
							 "RBPlayer",
							 &our_info, 0);
	}

	return rb_player_type;
}

static void
rb_player_class_init (RBPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_player_finalize;

	object_class->set_property = rb_player_set_property;
	object_class->get_property = rb_player_get_property;

	g_object_class_install_property (object_class,
					 PROP_RB,
					 g_param_spec_object ("rb",
							      "RB",
							      "RB object",
							      RB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static GtkWidget *
create_button_with_icon (const char *stock_id)
{
	GtkWidget *ret, *image;

	ret = gtk_button_new ();
	image = gtk_image_new_from_stock (stock_id,
					  GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (ret), image);

	return ret;
}

static void
pack_button (RBPlayer *player, GtkWidget *widget,
	     gboolean pack_in_second_box)
{
	GtkBox *box;

	if (pack_in_second_box)
		box = GTK_BOX (player->priv->second_button_box);
	else
		box = GTK_BOX (player->priv->first_button_box);

	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
}

static void
rb_player_init (RBPlayer *player)
{
	GtkWidget *hbox, *vbox, *infobox, *label;
	GtkWidget *artist_album_box, *scale_box, *song_box;
	PangoAttribute *attr;
	PangoAttrList *pattrlist;
	GError *error = NULL;

	player->priv = g_new0 (RBPlayerPrivate, 1);

	gtk_box_set_spacing (GTK_BOX (player), 6);

	player->priv->info_notebook = gtk_notebook_new ();
	gtk_notebook_set_show_border (GTK_NOTEBOOK (player->priv->info_notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (player->priv->info_notebook), FALSE);
	gtk_box_pack_start (GTK_BOX (player),
			    GTK_WIDGET (player->priv->info_notebook),
			    FALSE, FALSE, 0);

	/* player area + play controls box */
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_notebook_append_page (GTK_NOTEBOOK (player->priv->info_notebook), vbox, NULL);

	/* player area box */
	song_box = gtk_vbox_new (FALSE, 3);
	gtk_box_pack_start (GTK_BOX (vbox), song_box,
			    FALSE, FALSE, 0);

	infobox = gtk_vbox_new (FALSE, 1);
	gtk_box_pack_start (GTK_BOX (song_box), infobox,
			    FALSE, FALSE, 0);

	player->priv->song_label = rb_ellipsizing_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (player->priv->song_label), 0.0, 0.5);
	pattrlist = pango_attr_list_new ();
	attr = pango_attr_scale_new (PANGO_SCALE_XX_LARGE);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (pattrlist, attr);
	gtk_label_set_attributes (GTK_LABEL (player->priv->song_label),
				  pattrlist);
	pango_attr_list_unref (pattrlist);
	gtk_label_set_selectable (GTK_LABEL (player->priv->song_label),
				  TRUE);
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (player->priv->song_label),
				       RB_ELLIPSIZE_END);
	gtk_box_pack_start (GTK_BOX (infobox), player->priv->song_label,
			    FALSE, FALSE, 0);

	artist_album_box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (infobox), artist_album_box,
			    FALSE, FALSE, 0);

	player->priv->from_label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (artist_album_box), player->priv->from_label,
			    FALSE, FALSE, 0);
	player->priv->album_link = GTK_WIDGET (rb_link_new ());
	gtk_box_pack_start (GTK_BOX (artist_album_box), player->priv->album_link,
			    FALSE, FALSE, 0);
	label = gtk_label_new (_(" by "));
	gtk_box_pack_start (GTK_BOX (artist_album_box), label,
			    FALSE, FALSE, 0);
	player->priv->artist_link = GTK_WIDGET (rb_link_new ());
	gtk_box_pack_start (GTK_BOX (artist_album_box), player->priv->artist_link,
			    FALSE, FALSE, 0);

	scale_box = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_start (GTK_BOX (song_box), scale_box,
			    FALSE, FALSE, 0);

	player->priv->song_adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 1.0, 0.01, 0.1, 0.0));
	player->priv->song_scale = gtk_hscale_new (player->priv->song_adjustment);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->song_scale), FALSE);
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->song_scale,
			    TRUE, TRUE, 0);

	player->priv->time_label = gtk_label_new ("0:00");
	gtk_box_pack_start (GTK_BOX (scale_box), player->priv->time_label,
			    FALSE, FALSE, 0);

	/* play controls box */
	hbox = gtk_hbox_new (FALSE, 20);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    FALSE, FALSE, 0);

	player->priv->first_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->first_button_box,
			    FALSE, FALSE, 0);

	player->priv->second_button_box = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->second_button_box,
			    FALSE, FALSE, 0);

	player->priv->previous = create_button_with_icon (RB_STOCK_PREVIOUS);
	pack_button (player, player->priv->previous, FALSE);
	g_signal_connect (G_OBJECT (player->priv->previous), "clicked",
			  G_CALLBACK (previous_cb), player);
	player->priv->play     = create_button_with_icon (RB_STOCK_PLAY);
	pack_button (player, player->priv->play, FALSE);
	g_signal_connect (G_OBJECT (player->priv->play), "clicked",
			  G_CALLBACK (play_cb), player);
	player->priv->pause    = create_button_with_icon (RB_STOCK_PAUSE);
	pack_button (player, player->priv->pause, FALSE);
	g_signal_connect (G_OBJECT (player->priv->pause), "clicked",
			  G_CALLBACK (pause_cb), player);
	player->priv->next     = create_button_with_icon (RB_STOCK_NEXT);
	pack_button (player, player->priv->next, FALSE);
	g_signal_connect (G_OBJECT (player->priv->next), "clicked",
			  G_CALLBACK (next_cb), player);

	player->priv->repeat   = create_button_with_icon (RB_STOCK_REPEAT);
	pack_button (player, player->priv->repeat, TRUE);
	player->priv->shuffle  = create_button_with_icon (RB_STOCK_SHUFFLE);
	pack_button (player, player->priv->shuffle, TRUE);

	/* 'No playlist' label FIXME */
	label = gtk_label_new (_("Empty playlist"));
	pattrlist = pango_attr_list_new ();
	attr = pango_attr_scale_new (PANGO_SCALE_XX_LARGE);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (pattrlist, attr);
	gtk_label_set_attributes (GTK_LABEL (label),
				  pattrlist);
	pango_attr_list_unref (pattrlist);
	gtk_notebook_append_page (GTK_NOTEBOOK (player->priv->info_notebook), label, NULL);

	/* playlist */
	/* FIXME should be reorderable */
	player->priv->playlist = rb_node_new ();
	player->priv->playlist_view = rb_node_view_new (player->priv->playlist,
							rb_file ("rb-node-view-playlist.xml"),
							NULL);
	g_signal_connect (G_OBJECT (player->priv->playlist_view), "drag_data_received",
			  G_CALLBACK (drag_data_received_cb), player);
	gtk_drag_dest_set (GTK_WIDGET (player->priv->playlist_view), GTK_DEST_DEFAULT_ALL,
			   target_table, 1, GDK_ACTION_COPY);
	gtk_box_pack_start (GTK_BOX (player),
			    GTK_WIDGET (player->priv->playlist_view),
			    TRUE, TRUE, 0);

	player->priv->mixer = monkey_media_mixer_new (&error);
	if (error != NULL) {
		rb_error_dialog (_("Failed to create mixer, exiting. Error was:\n%s"), error->message);
		g_error_free (error);
		exit (-1);
	}

	player->priv->timeout = g_timeout_add (1000, (GSourceFunc) sync_time, player);

	gtk_widget_show_all (GTK_WIDGET (player));

	update_buttons (player);
	nullify_info (player);
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYER (object));

	player = RB_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	g_object_unref (G_OBJECT (player->priv->mixer));

	g_free (player->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_player_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBPlayer *player = RB_PLAYER (object);

	switch (prop_id)
	{
	case PROP_RB:
		player->priv->rb = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_player_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBPlayer *player = RB_PLAYER (object);

	switch (prop_id)
	{
	case PROP_RB:
		g_value_set_object (value, player->priv->rb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPlayer *
rb_player_new (RB *rb)
{
	RBPlayer *player;

	player = g_object_new (RB_TYPE_PLAYER,
			       "rb", rb,
			       NULL);

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

static void
update_buttons (RBPlayer *player)
{
	if (rb_node_view_get_first_node (player->priv->playlist_view) == NULL) {
		gtk_widget_hide (player->priv->pause);
		gtk_widget_show (player->priv->play);

		gtk_widget_set_sensitive (player->priv->play, FALSE);
		gtk_widget_set_sensitive (player->priv->previous, FALSE);
		gtk_widget_set_sensitive (player->priv->next, FALSE);

		return;
	}

	gtk_widget_set_sensitive (player->priv->play, TRUE);

	gtk_widget_set_sensitive (player->priv->next,
				  (rb_node_view_get_next_node (player->priv->playlist_view) != NULL));
	gtk_widget_set_sensitive (player->priv->previous,
				  (rb_node_view_get_previous_node (player->priv->playlist_view) != NULL));

	switch (monkey_media_mixer_get_state (player->priv->mixer)) {
	case MONKEY_MEDIA_MIXER_STATE_PLAYING:
		gtk_widget_hide (player->priv->play);
		gtk_widget_show (player->priv->pause);
		break;
	case MONKEY_MEDIA_MIXER_STATE_STOPPED:
	case MONKEY_MEDIA_MIXER_STATE_PAUSED:
		gtk_widget_hide (player->priv->pause);
		gtk_widget_show (player->priv->play);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
next (RBPlayer *player)
{
	set_playing (player, rb_node_view_get_next_node (player->priv->playlist_view));

	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PLAYING);
}

static void
previous (RBPlayer *player)
{
	set_playing (player, rb_node_view_get_previous_node (player->priv->playlist_view));

	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PLAYING);
}

static void
play (RBPlayer *player)
{
	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PLAYING);

	update_buttons (player);
}

static void
pause (RBPlayer *player)
{
	monkey_media_mixer_set_state (player->priv->mixer,
				      MONKEY_MEDIA_MIXER_STATE_PAUSED);

	update_buttons (player);
}

static void
clear (RBPlayer *player)
{
	GPtrArray *kids;
	int i;

	kids = rb_node_get_children (player->priv->playlist);
	rb_node_thaw (player->priv->playlist);
	for (i = 0; i < kids->len; i++) {
		rb_node_remove_child (player->priv->playlist, g_ptr_array_index (kids, i));
	}
}

static void
insert_song (RBPlayer *player, RBNode *song, int index)
{
	/* FIXME */
	rb_node_add_child (player->priv->playlist, song);

	if (player->priv->playing == NULL) {
		set_playing (player, song);

		monkey_media_mixer_set_state (player->priv->mixer, MONKEY_MEDIA_MIXER_STATE_PAUSED);
	} else {
		update_buttons (player);
	}
}

static void
prepend_song (RBPlayer *player, RBNode *song)
{
	insert_song (player, song, 0);
}

static void
append_song (RBPlayer *player, RBNode *song)
{
	insert_song (player, song, -1);
}

#if 0
static void
delete_song (RBPlayer *player, RBNode *song)
{
	if (player->priv->playing == song) {
		RBNode *next;

		next = rb_node_view_get_next_node (player->priv->playlist_view);
		if (next == NULL)
			next = rb_node_view_get_previous_node (player->priv->playlist_view);

		monkey_media_mixer_set_state (player->priv->mixer, MONKEY_MEDIA_MIXER_STATE_PAUSED);

		set_playing (player, next);
	}

	rb_node_remove_child (player->priv->playlist, song);
}
#endif

#define ALBUM_INFO_URL(xALBUM)   "http://www.allmusic.com/cg/amg.dll?p=amg&opt1=2&sql=xALBUM"
#define ARTIST_INFO_URL(xARTIST) "http://www.allmusic.com/cg/amg.dll?p=amg&opt1=1&sql=xARTIST"

static void
sync_info (RBPlayer *player)
{
	const char *text;
	int num;

	text = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_NAME);
	rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (player->priv->song_label), text);

	text = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_ALBUM);
	rb_link_set (RB_LINK (player->priv->album_link), text, _("Get information about this album from the web"),
		     ALBUM_INFO_URL (text));

	text = rb_node_get_property_string (player->priv->playing, RB_NODE_PROP_ARTIST);
	rb_link_set (RB_LINK (player->priv->artist_link), text, _("Get information about this artist from the web"),
		     ARTIST_INFO_URL (text));

	num = rb_node_get_property_int (player->priv->playing, RB_NODE_PROP_REAL_TRACK_NUMBER);
	if (num > 0) {
		char *tmp;

		tmp = g_strdup_printf (_("Track %d from "), num);
		gtk_label_set_text (GTK_LABEL (player->priv->from_label), tmp);
		g_free (tmp);
	} else
		gtk_label_set_text (GTK_LABEL (player->priv->from_label), _("From "));

	gtk_notebook_set_current_page (GTK_NOTEBOOK (player->priv->info_notebook), 0);
}

static void
set_playing (RBPlayer *player, RBNode *song)
{
	/* FIXME */
	player->priv->playing = song;

	rb_node_view_set_playing_node (player->priv->playlist_view, song);

	update_buttons (player);

	if (song == NULL)
		nullify_info (player);
	else
	{
		GError *error = NULL;

		player->priv->stream = monkey_media_audio_stream_new (rb_node_get_property_string (song, RB_NODE_PROP_LOCATION), &error);
		if (error != NULL) {
			rb_error_dialog (_("Failed to create stream, error was:\n%s"), error->message);
			g_error_free (error);
			return;
		}
		monkey_media_mixer_append_audio_stream (player->priv->mixer, player->priv->stream);
		monkey_media_mixer_set_playing_audio_stream (player->priv->mixer, player->priv->stream);
		g_object_unref (G_OBJECT (player->priv->stream));

		sync_info (player);
	}
}

static void
nullify_info (RBPlayer *player)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (player->priv->info_notebook), 1);
}

void
rb_player_queue_song (RBPlayer *player,
		      RBNode *song,
		      gboolean start_playing)
{
	if (start_playing) {
		prepend_song (player, song);
		set_playing (player, song);
	} else {
		append_song (player, song);
	}
}

RBNode *
rb_player_get_song (RBPlayer *player)
{
	return player->priv->playing;
}

void
rb_player_set_state (RBPlayer *player,
		     MonkeyMediaMixerState state)
{
	monkey_media_mixer_set_state (player->priv->mixer, state);

	update_buttons (player);
}

MonkeyMediaMixerState
rb_player_get_state (RBPlayer *player)
{
	return monkey_media_mixer_get_state (player->priv->mixer);
}

void
rb_player_load_playlist (RBPlayer *player,
			 const char *uri,
			 GError **error)
{
	clear (player);

	/* FIXME */
}

void
rb_player_save_playlist (RBPlayer *player,
			 const char *uri,
			 GError **error)
{
	/* FIXME */
}

static void
handle_songs_func (RBNode *node,
		   RBPlayer *player)
{
	append_song (player, node);
}

static void
drag_data_received_cb (GtkWidget *widget,
		       GdkDragContext *context,
		       int x, int y,
		       GtkSelectionData *data,
		       guint info, guint time,
		       RBPlayer *player)
{
	/* FIXME insert at a particular position */
	GtkTargetList *tlist;
	GdkAtom target;
	RBNode *node;

	tlist = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	target = gtk_drag_dest_find_target (widget, context, tlist);
	gtk_target_list_unref (tlist);

	if (target == GDK_NONE)
		return;

	g_assert (info == RB_LIBRARY_DND_NODE_ID);

	node = rb_node_get_from_id (atol (data->data));

	if (node != NULL) {
		rb_library_handle_songs (rb_get_library (player->priv->rb), node,
					 (GFunc) handle_songs_func, player);
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
play_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	play (player);
}

static void
pause_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	pause (player);
}

static void
previous_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	previous (player);
}

static void
next_cb (GtkWidget *widget,
	 RBPlayer *player)
{
	next (player);
}

/* FIXME re-intro seeking */
static gboolean
sync_time (RBPlayer *player)
{
	long duration, elapsed;
	double progress = 0.0;
	char *elapsed_str;

	if (player->priv->playing == NULL)
		return TRUE;

	duration = rb_node_get_property_long (player->priv->playing, RB_NODE_PROP_REAL_DURATION);
	elapsed = monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (player->priv->stream));

	if (elapsed > 0)
		progress = (double) ((long) elapsed) / duration;

	player->priv->slider_locked = TRUE;
	gtk_adjustment_set_value (player->priv->song_adjustment, progress);
	player->priv->slider_locked = FALSE;

	elapsed_str = g_strdup_printf ("%ld:%.2ld", elapsed / 60, elapsed % 60);
	gtk_label_set_text (GTK_LABEL (player->priv->time_label), elapsed_str);
	g_free (elapsed_str);

	return TRUE;
}
