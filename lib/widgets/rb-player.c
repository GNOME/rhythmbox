/* 
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <cwalters@gnome.org>
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
#include <gtk/gtktooltips.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-stock-icons.h"
#include "rb-link.h"
#include "rb-player.h"
#include "rb-debug.h"
#include "rb-ellipsizing-label.h"

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
static long rb_player_get_duration (RBPlayer *player);
static void rb_player_set_show_timeline (RBPlayer *player,
			                 gboolean show);
static gboolean rb_player_sync_time_locked (RBPlayer *player);
static void rb_player_update_elapsed (RBPlayer *player);
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBPlayer *player);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBPlayer *player);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBPlayer *player);
static void slider_changed_callback (GtkWidget *widget, RBPlayer *player);

typedef struct
{
	long elapsed;
	long duration;
} RBPlayerState;

struct RBPlayerPrivate
{
	RBNode *node;

	char *title;
	char *urltext, *urllink;

	MonkeyMediaPlayer *mmplayer;

	GtkWidget *image;
	GtkWidget *song;

	GtkWidget *timeframe;
	GtkWidget *timeline;
	gboolean timeline_shown;
	GtkWidget *scale;
	GtkAdjustment *adjustment;
	gboolean slider_dragging;
	gboolean slider_locked;
	guint slider_moved_timeout;
	long latest_set_time;
	guint value_changed_update_handler;
	GtkWidget *elapsed;

	GtkWidget *textframe;
	GtkWidget *textline;
	gboolean textline_shown;
	GtkTooltips *tips;
	RBLink *artist;
	RBLink *album;
	GtkWidget *urlframe;
	GtkWidget *urlline;
	gboolean urlline_shown;
	RBLink *url;

	guint timeout;
	
	RBPlayerState *state;
};

enum
{
	PROP_0,
	PROP_PLAYER,
	PROP_NODE,
	PROP_TITLE,
	PROP_URLTEXT,
	PROP_URLLINK,
};

static GObjectClass *parent_class = NULL;

#define SONG_MARKUP(xSONG) g_strdup_printf ("<big><b>%s</b></big>", xSONG);

#define ALBUM_INFO_URL(xALBUM)   g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=2&sql=%s", xALBUM);
#define ARTIST_INFO_URL(xARTIST) g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=1&sql=%s", xARTIST);

GType
rb_player_get_type (void)
{
	static GType rb_player_type = 0;

	if (rb_player_type == 0) {
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
					 PROP_NODE,
					 g_param_spec_pointer ("node",
							       "RBNode",
							       "RBNode",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
							      "Player",
							      "MonkeyMediaPlayer",
							      MONKEY_MEDIA_TYPE_PLAYER,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "title",
							      "title",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URLTEXT,
					 g_param_spec_string ("urltext",
							      "urltext",
							      "urltext",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_URLLINK,
					 g_param_spec_string ("urllink",
							      "urllink",
							      "urllink",
							      NULL,
							      G_PARAM_READWRITE));

}

static void
rb_player_init (RBPlayer *player)
{
	/**
	 * The children in this widget look like this:
	 * RBPlayer
	 *   GtkHBox
	 *     GtkVBox
	 *       GtkAlignment
	 *         GtkVBox
	 *           RBEllipsizingLabel	(priv->song)
	 *           GtkHBox		(priv->textframe)
	 *	       GtkHBox		(priv->textline)
	 *               GtkLabel	("from")
	 *               RBLink		(priv->album)
	 *               GtkLabel	("by")
	 *               RBLink		(priv->artist)
	 *           GtkHBox		(priv->urlframe)
	 *	       GtkHBox		(priv->urlline)
	 *               GtkLabel	("Listening to")
	 *               RBLink		(priv->url)
	 *   GtkAlignment		(priv->timeframe)
	 *     GtkVBox			(priv->timeline)
	 *       GtkHScale		(priv->scale)
	 *       GtkAlignment
	 *         GtkLabel		(priv->elapsed)
	 */
	GtkWidget *hbox, *vbox, *textline, *urlline, *label, *align, *scalebox, *textvbox;

	player->priv = g_new0 (RBPlayerPrivate, 1);

	player->priv->state = g_new0 (RBPlayerState, 1);

	hbox = gtk_hbox_new (FALSE, 10);

#if 0
	player->priv->image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (hbox), player->priv->image, FALSE, TRUE, 0);
#endif

	vbox = gtk_vbox_new (FALSE, 5);

	align = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	gtk_box_pack_start (GTK_BOX (vbox), align, TRUE, TRUE, 0);
	textvbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (align), textvbox);
	
	player->priv->song = rb_ellipsizing_label_new ("");
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (player->priv->song), RB_ELLIPSIZE_END);
 	gtk_label_set_use_markup (GTK_LABEL (player->priv->song), TRUE);
 	gtk_label_set_selectable (GTK_LABEL (player->priv->song), TRUE);	
	gtk_misc_set_alignment (GTK_MISC (player->priv->song), 0, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), player->priv->song, FALSE, TRUE, 0);

	/* Construct the Artist/Album display */
	player->priv->textframe = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), player->priv->textframe, FALSE, TRUE, 0);

	textline = player->priv->textline = gtk_hbox_new (FALSE, 0);
	g_object_ref (G_OBJECT (textline));

	label = gtk_label_new (_("from "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);

	player->priv->album = rb_link_new ();
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->album), FALSE, FALSE, 0);

	label = gtk_label_new (_(" by "));
	gtk_box_pack_start (GTK_BOX (textline), label, FALSE, TRUE, 0);

	player->priv->artist = rb_link_new ();
	gtk_box_pack_start (GTK_BOX (textline), GTK_WIDGET (player->priv->artist), FALSE, FALSE, 0);

	player->priv->textline_shown = FALSE;

	/* Construct the URL display */
	player->priv->urlframe = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), player->priv->urlframe, FALSE, TRUE, 0);

	urlline = player->priv->urlline = gtk_hbox_new (FALSE, 2);
	g_object_ref (G_OBJECT (urlline));

	label = gtk_label_new (_("Listening to "));
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (label), FALSE, TRUE, 0);
	player->priv->url = rb_link_new();
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (player->priv->url), FALSE, TRUE, 0);
	player->priv->urlline_shown = FALSE;

	/* construct the time slider and display */
	scalebox = player->priv->timeline = gtk_vbox_new (FALSE, 0);
	g_object_ref (G_OBJECT (scalebox));

	player->priv->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 1.0, 0.01, 0.1, 0.0));
	player->priv->scale = gtk_hscale_new (player->priv->adjustment);
	g_signal_connect (G_OBJECT (player->priv->scale),
			  "button_press_event",
			  G_CALLBACK (slider_press_callback),
			  player);
	g_signal_connect (G_OBJECT (player->priv->scale),
			  "button_release_event",
			  G_CALLBACK (slider_release_callback),
			  player);
	g_signal_connect (G_OBJECT (player->priv->scale),
			  "motion_notify_event",
			  G_CALLBACK (slider_moved_callback),
			  player);
	g_signal_connect (G_OBJECT (player->priv->scale),
			  "value_changed",
			  G_CALLBACK (slider_changed_callback),
			  player);
	gtk_scale_set_draw_value (GTK_SCALE (player->priv->scale), FALSE);
	gtk_widget_set_size_request (player->priv->scale, 150, -1);
	gtk_box_pack_start (GTK_BOX (scalebox), player->priv->scale, FALSE, TRUE, 0);
	align = gtk_alignment_new (1.0, 0.5, 0.0, 0.0);
	player->priv->elapsed = gtk_label_new ("0:00");
	gtk_misc_set_padding (GTK_MISC (player->priv->elapsed), 2, 0);
	player->priv->tips = gtk_tooltips_new ();
	gtk_container_add (GTK_CONTAINER (align), player->priv->elapsed);
	gtk_box_pack_start (GTK_BOX (scalebox), align, FALSE, TRUE, 0);
	align = player->priv->timeframe = gtk_alignment_new (1.0, 0.5, 0.0, 0.0);

	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (player), hbox, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (player), align, FALSE, FALSE, 0);

	player->priv->timeout = g_timeout_add (1000, (GSourceFunc) rb_player_sync_time_locked, player);
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

	g_object_unref (G_OBJECT (player->priv->urlline));
	g_object_unref (G_OBJECT (player->priv->textline));
	g_object_unref (G_OBJECT (player->priv->timeline));

	g_free (player->priv->state);
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

	switch (prop_id) {
	case PROP_NODE:
		player->priv->node = g_value_get_pointer (value);
		break;
	case PROP_PLAYER:
		player->priv->mmplayer = g_value_get_object (value);
		break;
	case PROP_TITLE:
		player->priv->title = g_value_dup_string (value);
		break;
	case PROP_URLTEXT:
		g_free (player->priv->urltext);
		player->priv->urltext = g_value_dup_string (value);
		break;
	case PROP_URLLINK:
		g_free (player->priv->urllink);
		player->priv->urllink = g_value_dup_string (value);
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

	switch (prop_id) {
	case PROP_NODE:
		g_value_set_object (value, player->priv->node);
		break;
	case PROP_PLAYER:
		g_value_set_object (value, player->priv->mmplayer);
		break;
	case PROP_TITLE:
		g_value_set_string (value, player->priv->title);
		break;
	case PROP_URLTEXT:
		g_value_set_string (value, player->priv->urltext);
		break;
	case PROP_URLLINK:
		g_value_set_string (value, player->priv->urllink);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBPlayer *
rb_player_new (MonkeyMediaPlayer *mmplayer)
{
	RBPlayer *player;

	player = RB_PLAYER (g_object_new (RB_TYPE_PLAYER, "player", mmplayer,
					  "title", NULL, NULL));

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

void
rb_player_set_playing_node (RBPlayer *player, RBNode *node)
{
	g_object_set (G_OBJECT (player), "node", node, NULL);
}

void
rb_player_set_title (RBPlayer *player,
		     const char *title)
{
	g_object_set (G_OBJECT (player), "title", title, NULL);
}

static long
rb_player_get_duration (RBPlayer *player)
{
	if (player->priv->node)
		return rb_node_get_property_long (player->priv->node,
						  RB_NODE_PROP_DURATION);
	return -1;
}

void
rb_player_sync (RBPlayer *player)
{
	char *tmp;

	rb_debug ("syncing with node = %p", player->priv->node);
	if (player->priv->node != NULL) {
		const char *song = player->priv->title;
		char *escaped, *s;
		gboolean have_duration = rb_player_get_duration (player) > 0;
		const char *album = rb_node_get_property_string (player->priv->node,
								 RB_NODE_PROP_ALBUM);
		const char *artist = rb_node_get_property_string (player->priv->node,
								  RB_NODE_PROP_ARTIST);

		escaped = g_markup_escape_text (song, -1);
		tmp = SONG_MARKUP (escaped);
		g_free (escaped);
		rb_ellipsizing_label_set_markup (RB_ELLIPSIZING_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_player_set_show_artist_album (player, (album != NULL && artist != NULL));

		if (player->priv->textline_shown)
		{
			g_return_if_fail (album != NULL);
			g_return_if_fail (artist != NULL);

			rb_debug ("artist: %s album: %s", artist, album);

			s = tmp = g_strdup (album);
			while ((tmp = strstr (tmp, " ")) != NULL)
				*tmp = '|';
			tmp = ALBUM_INFO_URL (s);
			g_free (s);
			rb_link_set (player->priv->album, album,
				     _("Get information on this album from the web"), tmp);
			g_free (tmp);

			s = tmp = g_strdup (artist);
			while ((tmp = strstr (tmp, " ")) != NULL)
			{
				*tmp = '|';
			}
			tmp = ARTIST_INFO_URL (s);
			g_free (s);
			rb_link_set (player->priv->artist, artist,
				     _("Get information on this artist from the web"), tmp);
			g_free (tmp);
		}

		if (player->priv->urlline_shown)
		{
			g_return_if_fail (player->priv->urltext != NULL);
			rb_debug ("urlline shown, urltext: %s urllink: %s",
				  player->priv->urltext, player->priv->urllink);
			rb_link_set (player->priv->url, player->priv->urltext,
				     _("Get more information on this station from the web"),
				     player->priv->urllink);
		}

		rb_player_set_show_timeline (player, have_duration);
		if (have_duration)
			rb_player_sync_time (player);
	} else {
		rb_debug ("not playing");
		tmp = SONG_MARKUP (_("Not playing"));
		rb_ellipsizing_label_set_markup (RB_ELLIPSIZING_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_player_set_urldata (player, NULL, NULL);
		rb_player_set_show_artist_album (player, FALSE);
		rb_player_set_show_timeline (player, FALSE);
	}

#if 0
	if (pixbuf != NULL)
		gtk_image_set_from_pixbuf (GTK_IMAGE (player->priv->image), pixbuf);
	else
		gtk_image_set_from_stock (GTK_IMAGE (player->priv->image), RB_STOCK_ALBUM, GTK_ICON_SIZE_DIALOG);
#endif
}

void
rb_player_set_urldata (RBPlayer *player,
		       const char *urltext,
		       const char *urllink)
{
	gboolean show = (urltext != NULL);
	if (player->priv->urlline_shown == show)
		return;

	player->priv->urlline_shown = show;
	g_object_set (G_OBJECT (player), "urltext", urltext,
		      "urllink", urllink, NULL);

	if (show == FALSE)
		gtk_container_remove (GTK_CONTAINER (player->priv->urlframe), player->priv->urlline);
	else
	{
		gtk_container_add (GTK_CONTAINER (player->priv->urlframe), player->priv->urlline);
		gtk_widget_show_all (player->priv->urlline);
	}
}

void
rb_player_set_show_artist_album (RBPlayer *player,
				 gboolean show)
{
	if (player->priv->textline_shown == show)
		return;

	player->priv->textline_shown = show;

	if (show == FALSE)
		gtk_container_remove (GTK_CONTAINER (player->priv->textframe), player->priv->textline);
	else {
		gtk_container_add (GTK_CONTAINER (player->priv->textframe), player->priv->textline);
		gtk_widget_show_all (player->priv->textframe);
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
	else {
		gtk_container_add (GTK_CONTAINER (player->priv->timeframe), player->priv->timeline);
		gtk_widget_show_all (player->priv->timeline);
	}
}

gboolean
rb_player_sync_time_locked (RBPlayer *player)
{
	gboolean ret;

	gdk_threads_enter ();

	ret = rb_player_sync_time (player);

	gdk_threads_leave ();

	return ret;
}

gboolean
rb_player_sync_time (RBPlayer *player)
{
	int seconds;
	long duration;

	if (player->priv->mmplayer == NULL)
		return TRUE;

	if (player->priv->slider_dragging == TRUE)
		return TRUE;

	player->priv->state->duration = duration =
		rb_player_get_duration (player);
	player->priv->state->elapsed = seconds =
		monkey_media_player_get_time (player->priv->mmplayer);

	if (duration > -1) {
		double progress = 0.0;

		if (seconds > 0)
			progress = (double) ((long) seconds) / duration;

		player->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, progress);
		player->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, TRUE);
	} else {
		player->priv->slider_locked = TRUE;
		gtk_adjustment_set_value (player->priv->adjustment, 0.0);
		player->priv->slider_locked = FALSE;
		gtk_widget_set_sensitive (player->priv->scale, FALSE);
	}

	rb_player_update_elapsed (player);

	return TRUE;
}

static gboolean
slider_press_callback (GtkWidget *widget,
		       GdkEventButton *event,
		       RBPlayer *player)
{
	player->priv->slider_dragging = TRUE;
	player->priv->latest_set_time = -1;
	return FALSE;
}

static gboolean
slider_moved_timeout (RBPlayer *player)
{
	double progress;
	long duration, new;

	gdk_threads_enter ();
	
	progress = gtk_adjustment_get_value (gtk_range_get_adjustment (GTK_RANGE (player->priv->scale)));
	duration = rb_player_get_duration (player);
	new = (long) (progress * duration);
	
	monkey_media_player_set_time (player->priv->mmplayer, new);

	player->priv->latest_set_time = new;
	
	gdk_threads_leave ();

	return FALSE;
}

static gboolean
slider_moved_callback (GtkWidget *widget,
		       GdkEventMotion *event,
		       RBPlayer *player)
{
	GtkAdjustment *adjustment;
	double progress;
	long duration;

	if (player->priv->slider_dragging == FALSE)
		return FALSE;

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	duration = rb_player_get_duration (player);

	player->priv->state->elapsed = (long) (progress * duration);

	rb_player_update_elapsed (player);

	if (player->priv->slider_moved_timeout != 0) {
		g_source_remove (player->priv->slider_moved_timeout);
		player->priv->slider_moved_timeout = 0;
	}
	player->priv->slider_moved_timeout =
		g_timeout_add (40, (GSourceFunc) slider_moved_timeout, player);
	
	return FALSE;
}

static gboolean
slider_release_callback (GtkWidget *widget,
			 GdkEventButton *event,
			 RBPlayer *player)
{
	double progress;
	long duration, new;
	GtkAdjustment *adjustment;

	if (player->priv->slider_dragging == FALSE)
		return FALSE;

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	duration = rb_player_get_duration (player);
	new = (long) (progress * duration);

	if (new != player->priv->latest_set_time)
		monkey_media_player_set_time (player->priv->mmplayer, new);

	player->priv->slider_dragging = FALSE;

	rb_player_sync_time (player);

	if (player->priv->slider_moved_timeout != 0) {
		g_source_remove (player->priv->slider_moved_timeout);
		player->priv->slider_moved_timeout = 0;
	}

	return FALSE;
}

static gboolean
changed_idle_callback (RBPlayer *player)
{

	gdk_threads_enter ();

	slider_release_callback (player->priv->scale, NULL, player);

	player->priv->value_changed_update_handler = 0;
	rb_debug ("in changed_idle_callback"); 

	gdk_threads_leave ();
	return FALSE;
}

static void
slider_changed_callback (GtkWidget *widget,
		         RBPlayer *player)
{
	if (player->priv->slider_dragging == FALSE &&
	    player->priv->slider_locked == FALSE &&
	    player->priv->value_changed_update_handler == 0) {
		player->priv->slider_dragging = TRUE;
		player->priv->value_changed_update_handler =
			g_idle_add ((GSourceFunc) changed_idle_callback, player);
	}
}

static void
rb_player_update_elapsed (RBPlayer *player)
{
	char *elapsed_text;
	int seconds = 0, minutes = 0, seconds2 = -1, minutes2 = -1;

	/* sanity check */
	if ((player->priv->state->elapsed > player->priv->state->duration) || (player->priv->state->elapsed < 0))
		return;

	if (player->priv->state->elapsed > 0) {
		minutes = player->priv->state->elapsed / 60;
		seconds = player->priv->state->elapsed % 60;
	}
	if (player->priv->state->duration > 0) {
		minutes2 = player->priv->state->duration / 60;
		seconds2 = player->priv->state->duration % 60;
	}
	if (seconds2 >= 0) {
		elapsed_text = g_strdup_printf (_("%d:%02d of %d:%02d"), minutes, seconds, minutes2, seconds2);
	} else {
		elapsed_text = g_strdup_printf (_("%d:%02d"), minutes, seconds);
	}

	gtk_label_set_text (GTK_LABEL (player->priv->elapsed), elapsed_text);
	g_free (elapsed_text);
}
