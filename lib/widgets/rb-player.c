/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkhscale.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkstock.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-link.h"
#include "rb-player.h"

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
static void rb_view_player_changed_cb (RBViewPlayer *view_player,
			               RBPlayer *player);
static void rb_player_sync (RBPlayer *player);
static void rb_player_set_show_timeline (RBPlayer *player,
			                 gboolean show);
static void rb_player_set_show_textline (RBPlayer *player,
			                 gboolean show);
static gboolean rb_player_sync_time (RBPlayer *player);
static void rb_player_adjustment_changed_cb (GtkAdjustment *adjustment,
				             RBPlayer *player);

struct RBPlayerPrivate
{
	RBViewPlayer *view_player;

	GtkWidget *image;
	GtkWidget *song;

	GtkWidget *timeframe;
	GtkWidget *timeline;
	gboolean timeline_shown;
	GtkWidget *scale;
	GtkAdjustment *adjustment;
	gboolean lock_adjustment;
	GtkWidget *elapsed;

	GtkWidget *textframe;
	GtkWidget *textline;
	gboolean textline_shown;
	RBLink *artist;
	RBLink *album;

	guint timeout;
};

enum
{
	PROP_0,
	PROP_VIEW_PLAYER
};

static GObjectClass *parent_class = NULL;

#define SONG_MARKUP(xSONG) g_strdup_printf ("<span size=\"xx-large\"><b>%s</b></span>", xSONG);

#define ALBUM_INFO_URL(xALBUM)   g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=2&sql=%s", xALBUM);
#define ARTIST_INFO_URL(xARTIST) g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=1&sql=%s", xARTIST);

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

		rb_player_type = g_type_register_static (GTK_TYPE_FRAME,
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
					 PROP_VIEW_PLAYER,
					 g_param_spec_object ("view-player",
							      "RBViewPlayer",
							      "RBVoewPlayer",
							      RB_TYPE_VIEW_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_player_init (RBPlayer *player)
{
	GtkWidget *hbox, *vbox, *textline, *label, *align, *scalebox, *textvbox;
	
	player->priv = g_new0 (RBPlayerPrivate, 1);

	gtk_frame_set_shadow_type (GTK_FRAME (player), GTK_SHADOW_NONE);

	hbox = gtk_hbox_new (FALSE, 10);

	player->priv->image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->image, FALSE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 5);

	textvbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), textvbox, FALSE, TRUE, 0);

	align = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
	player->priv->song = gtk_label_new ("");
	gtk_label_set_use_markup (GTK_LABEL (player->priv->song), TRUE);
	gtk_container_add (GTK_CONTAINER (align), player->priv->song);
	gtk_box_pack_start (GTK_BOX (textvbox), align, FALSE, FALSE, 0);

	player->priv->textframe = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), player->priv->textframe, FALSE, TRUE, 0);
	
	textline = player->priv->textline = gtk_hbox_new (FALSE, 0);
	g_object_ref (G_OBJECT (textline));

	label = gtk_label_new (_("from "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);
	
	player->priv->album = rb_link_new ();
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->album), FALSE, TRUE, 0);

	label = gtk_label_new (_(" by "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);

	player->priv->artist = rb_link_new ();
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->artist), FALSE, TRUE, 0);

	player->priv->timeframe = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), player->priv->timeframe, FALSE, FALSE, 0);

	scalebox = player->priv->timeline = gtk_hbox_new (FALSE, 2);
	g_object_ref (G_OBJECT (scalebox));
	
	player->priv->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 1.0, 0.01, 0.1, 0.1));
	g_signal_connect (G_OBJECT (player->priv->adjustment),
			  "changed",
			  G_CALLBACK (rb_player_adjustment_changed_cb),
			  player);
	player->priv->scale = gtk_hscale_new (player->priv->adjustment);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->scale), FALSE);
	gtk_widget_set_size_request (player->priv->scale, 150, -1);
	gtk_box_pack_start (GTK_BOX (scalebox), player->priv->scale, FALSE, TRUE, 0);
	player->priv->elapsed = gtk_label_new ("0:00");
	gtk_box_pack_start (GTK_BOX (scalebox), player->priv->elapsed, FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (player), hbox);

	player->priv->timeout = g_timeout_add (1000, (GSourceFunc) rb_player_sync_time, player);
}

static void
rb_player_finalize (GObject *object)
{
	RBPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PLAYER (object));

	player = RB_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	g_source_remove (player->priv->timeout);

	g_object_unref (G_OBJECT (player->priv->textline));
	g_object_unref (G_OBJECT (player->priv->timeline));

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
	case PROP_VIEW_PLAYER:
		player->priv->view_player = g_value_get_object (value);

		g_signal_connect_object (G_OBJECT (player->priv->view_player),
					 "changed",
					 G_CALLBACK (rb_view_player_changed_cb),
					 object,
					 0);

		rb_player_sync (player);
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
	case PROP_VIEW_PLAYER:
		g_value_set_object (value, player->priv->view_player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPlayer *
rb_player_new (RBViewPlayer *view_player)
{
	RBPlayer *player;

	g_return_val_if_fail (RB_IS_VIEW_PLAYER (view_player), NULL);
	
	player = RB_PLAYER (g_object_new (RB_TYPE_PLAYER,
					  "view-player", view_player,
					  NULL));

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

static void
rb_player_sync (RBPlayer *player)
{

	MonkeyMediaAudioStream *stream = rb_view_player_get_stream (player->priv->view_player);
	GdkPixbuf *pixbuf = rb_view_player_get_pixbuf (player->priv->view_player);
	char *tmp;

	if (stream != NULL)
	{
		const char *song   = rb_view_player_get_song   (player->priv->view_player);
		const char *album  = rb_view_player_get_album  (player->priv->view_player);
		const char *artist = rb_view_player_get_artist (player->priv->view_player);

		tmp = SONG_MARKUP (song);
		gtk_label_set_markup (GTK_LABEL (player->priv->song), tmp);
		g_free (tmp);

		tmp = ALBUM_INFO_URL (album);
		rb_link_set (player->priv->album, album, tmp);
		g_free (tmp);

		tmp = ARTIST_INFO_URL (artist);
		rb_link_set (player->priv->artist, artist, tmp);
		g_free (tmp);

		rb_player_set_show_textline (player, TRUE);
		rb_player_set_show_timeline (player, TRUE);

		rb_player_sync_time (player);
	}
	else
	{
		tmp = SONG_MARKUP (_("Not Playing"));
		gtk_label_set_markup (GTK_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_player_set_show_textline (player, FALSE);
		rb_player_set_show_timeline (player, FALSE);
	}

	if (pixbuf != NULL)
		gtk_image_set_from_pixbuf (GTK_IMAGE (player->priv->image), pixbuf);
	else
		gtk_image_set_from_stock (GTK_IMAGE (player->priv->image), RB_STOCK_ALBUM, GTK_ICON_SIZE_DIALOG);
}

static void
rb_view_player_changed_cb (RBViewPlayer *view_player,
			   RBPlayer *player)
{
	rb_player_sync (player);
}

static void
rb_player_set_show_textline (RBPlayer *player,
			     gboolean show)
{
	if (player->priv->textline_shown == show)
		return;

	player->priv->textline_shown = show;

	if (show == FALSE)
		gtk_container_remove (GTK_CONTAINER (player->priv->textframe), player->priv->textline);
	else
	{
		gtk_container_add (GTK_CONTAINER (player->priv->textframe), player->priv->textline);
		gtk_widget_show_all (player->priv->textline);
	}
}

static void
rb_player_set_show_timeline (RBPlayer *player,
			     gboolean show)
{
	if (player->priv->timeline_shown == show)
		return;

	player->priv->timeline_shown = show;
	
	if (show == FALSE)
		gtk_container_remove (GTK_CONTAINER (player->priv->timeframe), player->priv->timeline);
	else
	{
		gtk_container_add (GTK_CONTAINER (player->priv->timeframe), player->priv->timeline);
		gtk_widget_show_all (player->priv->timeline);
	}
}

static gboolean
rb_player_sync_time (RBPlayer *player)
{
	MonkeyMediaAudioStream *stream;
	char *elapsed;
	int seconds = 0, minutes = 0;
	long duration;

	if (player->priv->view_player == NULL)
		return TRUE;
	
	stream = rb_view_player_get_stream (player->priv->view_player);
	if (stream == NULL)
		return TRUE;

	duration = rb_view_player_get_duration (player->priv->view_player);
	seconds = monkey_media_stream_get_elapsed_time (MONKEY_MEDIA_STREAM (stream));

	if (duration > -1)
	{
		double progress = 0;

		if (seconds > 0)
			progress = (double) seconds / duration;

		player->priv->lock_adjustment = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, progress);
		player->priv->lock_adjustment = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, TRUE);
	}
	else
	{
		player->priv->lock_adjustment = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, 0);
		player->priv->lock_adjustment = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, FALSE);
	}

	if (seconds > 0)
	{
		minutes = seconds / 60;
		seconds = seconds % 60;
	}
	
	elapsed = g_strdup_printf ("%d:%02d", minutes, seconds);
	gtk_label_set_text (GTK_LABEL (player->priv->elapsed), elapsed);
	g_free (elapsed);

	return TRUE;
}

static void
rb_player_adjustment_changed_cb (GtkAdjustment *adjustment,
				 RBPlayer *player)
{
	MonkeyMediaAudioStream *stream = rb_view_player_get_stream (player->priv->view_player);
	double progress = gtk_adjustment_get_value (adjustment);
	long duration = rb_view_player_get_duration (player->priv->view_player);
	long new = (long) progress * duration;
	monkey_media_stream_set_elapsed_time (MONKEY_MEDIA_STREAM (stream), new);
	rb_player_sync_time (player);
}
