/*
 *  arch-tag: Implementation of main song information display widget
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <math.h>
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

#include "rb-song-display-box.h"
#include "rb-stock-icons.h"
#include "rb-header.h"
#include "rb-debug.h"
#include "rb-ellipsizing-label.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

static void rb_header_class_init (RBHeaderClass *klass);
static void rb_header_init (RBHeader *player);
static void rb_header_finalize (GObject *object);
static void rb_header_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_header_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static long rb_header_get_duration (RBHeader *player);
static void rb_header_set_show_timeline (RBHeader *player,
			                 gboolean show);
static gboolean rb_header_sync_time_locked (RBHeader *player);
static void rb_header_update_elapsed (RBHeader *player);
static gboolean slider_press_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *player);
static gboolean slider_moved_callback (GtkWidget *widget, GdkEventMotion *event, RBHeader *player);
static gboolean slider_release_callback (GtkWidget *widget, GdkEventButton *event, RBHeader *player);
static void slider_changed_callback (GtkWidget *widget, RBHeader *player);

typedef struct
{
	long elapsed;
	long duration;
} RBHeaderState;

struct RBHeaderPrivate
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	char *title;
	char *urltext, *urllink;

	RBPlayer *mmplayer;

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
	RBSongDisplayBox *displaybox;
	gboolean displaybox_shown;
	GtkTooltips *tips;
	GtkWidget *urlframe;
	GtkWidget *urlline;
	gboolean urlline_shown;
	GnomeHRef *url;

	guint timeout;
	
	RBHeaderState *state;
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_HEADER,
	PROP_ENTRY,
	PROP_TITLE,
	PROP_URLTEXT,
	PROP_URLLINK,
};

static GObjectClass *parent_class = NULL;

#define SONG_MARKUP(xSONG) g_strdup_printf ("<big><b>%s</b></big>", xSONG);

GType
rb_header_get_type (void)
{
	static GType rb_header_type = 0;

	if (rb_header_type == 0) {
		static const GTypeInfo our_info =
		{
			sizeof (RBHeaderClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_header_class_init,
			NULL,
			NULL,
			sizeof (RBHeader),
			0,
			(GInstanceInitFunc) rb_header_init
		};

		rb_header_type = g_type_register_static (GTK_TYPE_HBOX,
							 "RBHeader",
							 &our_info, 0);
	}

	return rb_header_type;
}

static void
rb_header_class_init (RBHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_header_finalize;

	object_class->set_property = rb_header_set_property;
	object_class->get_property = rb_header_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_ENTRY,
					 g_param_spec_pointer ("entry",
							       "RhythmDBEntry",
							       "RhythmDBEntry pointer",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_HEADER,
					 g_param_spec_object ("player",
							      "Player",
							      "MonkeyMediaPlayer",
							      RB_TYPE_PLAYER,
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
rb_header_init (RBHeader *player)
{
	/**
	 * The children in this widget look like this:
	 * RBHeader
	 *   GtkHBox
	 *     GtkVBox
	 *       GtkAlignment
	 *         GtkVBox
	 *           RBEllipsizingLabel	(priv->song)
	 *           GtkHBox		(priv->textframe)
	 *	       RBSongDisplayBox (priv->displaybox)
	 *               GtkLabel	("from")
	 *               GnomeHRef	(priv->displaybox->album)
	 *               GtkLabel	("by")
	 *               GnomeHRef	(priv->displaybox->artist)
	 *           GtkHBox		(priv->urlframe)
	 *	       GtkHBox		(priv->urlline)
	 *               GtkLabel	("Listening to")
	 *               GnomeHRef	(priv->url)
	 *   GtkAlignment		(priv->timeframe)
	 *     GtkVBox			(priv->timeline)
	 *       GtkHScale		(priv->scale)
	 *       GtkAlignment
	 *         GtkLabel		(priv->elapsed)
	 */
	GtkWidget *hbox, *vbox, *urlline, *label, *align, *scalebox, *textvbox;

	player->priv = g_new0 (RBHeaderPrivate, 1);

	player->priv->state = g_new0 (RBHeaderState, 1);

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

	player->priv->displaybox = RB_SONG_DISPLAY_BOX (rb_song_display_box_new ());
	g_object_ref (G_OBJECT (player->priv->displaybox));
	player->priv->displaybox_shown = FALSE;

	/* Construct the URL display */
	player->priv->urlframe = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (textvbox), player->priv->urlframe, FALSE, TRUE, 0);

	urlline = player->priv->urlline = gtk_hbox_new (FALSE, 2);
	g_object_ref (G_OBJECT (urlline));

	label = gtk_label_new (_("Listening to "));
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (label), FALSE, TRUE, 0);
	player->priv->url = (GnomeHRef *) gnome_href_new ("", "");
	gtk_box_pack_start (GTK_BOX (urlline), GTK_WIDGET (player->priv->url), FALSE, TRUE, 0);
	player->priv->urlline_shown = FALSE;

	/* construct the time slider and display */
	scalebox = player->priv->timeline = gtk_vbox_new (FALSE, 0);
	g_object_ref (G_OBJECT (scalebox));

	player->priv->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 10.0, 1.0, 10.0, 0.0));
	player->priv->scale = gtk_hscale_new (player->priv->adjustment);
	g_signal_connect_object (G_OBJECT (player->priv->scale),
				 "button_press_event",
				 G_CALLBACK (slider_press_callback),
				 player, 0);
	g_signal_connect_object (G_OBJECT (player->priv->scale),
				 "button_release_event",
				 G_CALLBACK (slider_release_callback),
				 player, 0);
	g_signal_connect_object (G_OBJECT (player->priv->scale),
				 "motion_notify_event",
				 G_CALLBACK (slider_moved_callback),
				 player, 0);
	g_signal_connect_object (G_OBJECT (player->priv->scale),
				 "value_changed",
				 G_CALLBACK (slider_changed_callback),
				 player, 0);
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

	player->priv->timeout = g_timeout_add (1000, (GSourceFunc) rb_header_sync_time_locked, player);
}

static void
rb_header_finalize (GObject *object)
{
	RBHeader *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_HEADER (object));

	player = RB_HEADER (object);

	g_return_if_fail (player->priv != NULL);

	g_source_remove (player->priv->timeout);

	g_object_unref (G_OBJECT (player->priv->urlline));
	g_object_unref (G_OBJECT (player->priv->displaybox));
	g_object_unref (G_OBJECT (player->priv->timeline));

	g_free (player->priv->state);
	g_free (player->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_header_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBHeader *player = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		player->priv->db = g_value_get_object (value);
		break;
	case PROP_ENTRY:
		player->priv->entry = g_value_get_pointer (value);
		break;
	case PROP_HEADER:
		player->priv->mmplayer = g_value_get_object (value);
		break;
	case PROP_TITLE:
		g_free (player->priv->title);
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
rb_header_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBHeader *player = RB_HEADER (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, player->priv->db);
		break;
	case PROP_ENTRY:
		g_value_set_object (value, player->priv->entry);
		break;
	case PROP_HEADER:
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

RBHeader *
rb_header_new (RBPlayer *mmplayer)
{
	RBHeader *player;

	player = RB_HEADER (g_object_new (RB_TYPE_HEADER, "player", mmplayer,
					  "title", NULL,
					  "spacing", 6, NULL));

	g_return_val_if_fail (player->priv != NULL, NULL);

	return player;
}

void
rb_header_set_playing_entry (RBHeader *player, RhythmDBEntry *entry)
{
	g_object_set (G_OBJECT (player), "entry", entry, NULL);
}

void
rb_header_set_title (RBHeader *player, const char *title)
{
	g_object_set (G_OBJECT (player), "title", title, NULL);
}

static long
rb_header_get_duration (RBHeader *player)
{
	if (player->priv->entry) {
		return player->priv->entry->duration;
	}
	return -1;
}

static char *
info_url (guint opt, const char *data)
{
	char *ret, *d;

	/*
	 * netlabels often put URLs (or domain names) in the 'album' field
	 * of their releases; since there are no artist names in AMG that
	 * start with 'http://' or 'www.' (there is a 'www', from the 70s,
	 * strangely enough), we can safely assume anything that looks 
	 * like a URL or domain name is one.
	 *
	 * There's sometimes some trailing junk, usually after a space,
	 * so it's probably sensible to strip that off.
	 */
	if (g_str_has_prefix (data, "http://")) {
		char *end = strchr (data, ' ');
		if (end != NULL)
			ret = g_strndup (data, end - data);
		else
			ret = g_strdup (data);
	} else if (g_str_has_prefix (data, "www.")) {
		char *end = strchr (data, ' ');
		if (end != NULL)
			ret = g_strdup_printf ("http://%*s", end-data, data);
		else
			ret = g_strdup_printf ("http://%s", data);
	} else {
		char *r;

		d = g_strdup (data);
		r = d;
		while (*r != '\0') {
			if (*r == ' ')
				*r = '+';
			r++;
		}

		ret = g_strdup_printf ("http://www.allmusic.com/cg/amg.dll?p=amg&opt1=%d&sql=%s", opt, d);
		g_free (d);
	}

	return ret;
}

static char *
album_info_url (const char *album)
{
	return info_url (2, album);
}

static char *
artist_info_url (const char *album)
{
	return info_url (1, album);
}

void
rb_header_sync (RBHeader *player)
{
	char *tmp;

	rb_debug ("syncing with node = %p", player->priv->entry);
	if (player->priv->entry != NULL) {
		const char *song = player->priv->title;
		char *escaped;
		gboolean have_duration = rb_header_get_duration (player) > 0;
		const char *album; 
		const char *artist; 
		GtkTooltips *artist_href_tips, *album_href_tips;


		album = rb_refstring_get (player->priv->entry->album);
		artist = rb_refstring_get (player->priv->entry->artist);

		escaped = g_markup_escape_text (song, -1);
		tmp = SONG_MARKUP (escaped);
		g_free (escaped);
		rb_ellipsizing_label_set_markup (RB_ELLIPSIZING_LABEL (player->priv->song), tmp);
		g_free (tmp);

		rb_header_set_show_artist_album (player, (album != NULL && artist != NULL)
						 && (strlen (album) > 0 && strlen (artist) > 0));

		if (player->priv->displaybox_shown)
		{
			g_return_if_fail (album != NULL);
			g_return_if_fail (artist != NULL);

			rb_debug ("artist: %s album: %s", artist, album);

			tmp = album_info_url (album);
			gnome_href_set_url (player->priv->displaybox->album, tmp);
			escaped = g_markup_escape_text (album, -1);
			gnome_href_set_text (player->priv->displaybox->album, escaped);
			g_free (escaped);
			g_free (tmp);
   
   			album_href_tips = gtk_tooltips_new ();
			gtk_tooltips_set_tip (GTK_TOOLTIPS (album_href_tips), 
					      GTK_WIDGET (player->priv->displaybox->album),
					      _("Get information on this album from the web"), 
					      NULL);
			
			tmp = artist_info_url (artist);
			gnome_href_set_url (player->priv->displaybox->artist, tmp);
			escaped = g_markup_escape_text (artist, -1);
			gnome_href_set_text (player->priv->displaybox->artist, escaped);
			g_free (escaped);
			g_free (tmp);

   			artist_href_tips = gtk_tooltips_new ();
			gtk_tooltips_set_tip (GTK_TOOLTIPS (artist_href_tips), 
					      GTK_WIDGET (player->priv->displaybox->artist),
					      _("Get information on this artist from the web"),
					      NULL);
			
		}

		if (player->priv->urlline_shown)
		{
			g_return_if_fail (player->priv->urltext != NULL);
			rb_debug ("urlline shown, urltext: %s urllink: %s",
				  player->priv->urltext, player->priv->urllink);
			gnome_href_set_url (player->priv->url, player->priv->urllink);
			gnome_href_set_text (player->priv->url, player->priv->urltext);
		}

		rb_header_set_show_timeline (player, have_duration);
		if (have_duration)
			rb_header_sync_time (player);
	} else {
		GtkTooltips *iradio_href_tips;

		rb_debug ("not playing");
		tmp = SONG_MARKUP (_("Not playing"));
		rb_ellipsizing_label_set_markup (RB_ELLIPSIZING_LABEL (player->priv->song), tmp);
		g_free (tmp);

		iradio_href_tips = gtk_tooltips_new ();
		gtk_tooltips_set_tip (GTK_TOOLTIPS (iradio_href_tips), 
				      GTK_WIDGET (player->priv->displaybox->artist),
				      _("Get information on this station from the web"),
				      NULL);

		rb_header_set_urldata (player, NULL, NULL);
		rb_header_set_show_artist_album (player, FALSE);
		rb_header_set_show_timeline (player, FALSE);
	}

#if 0
	if (pixbuf != NULL)
		gtk_image_set_from_pixbuf (GTK_IMAGE (player->priv->image), pixbuf);
	else
		gtk_image_set_from_stock (GTK_IMAGE (player->priv->image), RB_STOCK_ALBUM, GTK_ICON_SIZE_DIALOG);
#endif
}

void
rb_header_set_urldata (RBHeader *player,
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
rb_header_set_show_artist_album (RBHeader *player,
				 gboolean show)
{
	if (player->priv->displaybox_shown == show)
		return;

	player->priv->displaybox_shown = show;

	if (show == FALSE)
		gtk_container_remove (GTK_CONTAINER (player->priv->textframe), GTK_WIDGET (player->priv->displaybox));
	else {
		gtk_container_add (GTK_CONTAINER (player->priv->textframe), GTK_WIDGET (player->priv->displaybox));
		gtk_widget_show_all (player->priv->textframe);
	}
}

static void
rb_header_set_show_timeline (RBHeader *player,
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
rb_header_sync_time_locked (RBHeader *player)
{
	gboolean ret;

	GDK_THREADS_ENTER ();

	ret = rb_header_sync_time (player);

	GDK_THREADS_LEAVE ();

	return ret;
}

gboolean
rb_header_sync_time (RBHeader *player)
{
	int seconds;
	long duration;

	if (player->priv->mmplayer == NULL)
		return TRUE;

	if (player->priv->slider_dragging == TRUE) {
		rb_debug ("slider is dragging, not syncing");
		return TRUE;
	}

	player->priv->state->duration = duration =
		rb_header_get_duration (player);
	player->priv->state->elapsed = seconds =
		rb_player_get_time (player->priv->mmplayer);

	if (duration > -1) {
		double progress = 0.0;

		if (seconds > 0)
			progress = (double) (long) seconds;
		else {
			player->priv->adjustment->upper = duration;
			g_signal_emit_by_name (G_OBJECT (player->priv->adjustment), "changed");
		}

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

	rb_header_update_elapsed (player);

	return TRUE;
}

static gboolean
slider_press_callback (GtkWidget *widget,
		       GdkEventButton *event,
		       RBHeader *player)
{
	player->priv->slider_dragging = TRUE;
	player->priv->latest_set_time = -1;
	return FALSE;
}

static gboolean
slider_moved_timeout (RBHeader *player)
{
	double progress;
	long new;

	GDK_THREADS_ENTER ();
	
	progress = gtk_adjustment_get_value (gtk_range_get_adjustment (GTK_RANGE (player->priv->scale)));
	new = (long) (progress+0.5);
	
	rb_debug ("setting time to %ld", new);
	rb_player_set_time (player->priv->mmplayer, new);

	player->priv->latest_set_time = new;
	player->priv->slider_moved_timeout = 0;
	
	GDK_THREADS_LEAVE ();

	return FALSE;
}

static gboolean
slider_moved_callback (GtkWidget *widget,
		       GdkEventMotion *event,
		       RBHeader *player)
{
	GtkAdjustment *adjustment;
	double progress;

	if (player->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	player->priv->state->elapsed = (long) (progress+0.5);

	rb_header_update_elapsed (player);

	if (player->priv->slider_moved_timeout != 0) {
		rb_debug ("removing old timer");
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
			 RBHeader *player)
{
	double progress;
	long new;
	GtkAdjustment *adjustment;

	if (player->priv->slider_dragging == FALSE) {
		rb_debug ("slider is not dragging");
		return FALSE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (widget));

	progress = gtk_adjustment_get_value (adjustment);
	new = (long) (progress+0.5);

	if (new != player->priv->latest_set_time) {
		rb_debug ("setting time to %ld", new);
		rb_player_set_time (player->priv->mmplayer, new);
	}

	player->priv->slider_dragging = FALSE;

	rb_header_sync_time (player);

	if (player->priv->slider_moved_timeout != 0) {
		g_source_remove (player->priv->slider_moved_timeout);
		player->priv->slider_moved_timeout = 0;
	}

	return FALSE;
}

static gboolean
changed_idle_callback (RBHeader *player)
{

	GDK_THREADS_ENTER ();

	slider_release_callback (player->priv->scale, NULL, player);

	player->priv->value_changed_update_handler = 0;
	rb_debug ("in changed_idle_callback"); 

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
slider_changed_callback (GtkWidget *widget,
		         RBHeader *player)
{
	if (player->priv->slider_dragging == FALSE &&
	    player->priv->slider_locked == FALSE &&
	    player->priv->value_changed_update_handler == 0) {
		player->priv->slider_dragging = TRUE;
		player->priv->value_changed_update_handler =
			g_idle_add ((GSourceFunc) changed_idle_callback, player);
	}
}

char *
rb_header_get_elapsed_string (RBHeader *player)
{
	int seconds = 0, minutes = 0, seconds2 = -1, minutes2 = -1;
	guint elapsed = rb_player_get_time (player->priv->mmplayer);

	if (elapsed > 0) {
		minutes = elapsed / 60;
		seconds = elapsed % 60;
	}

	if (player->priv->state->duration > 0) {
		minutes2 = player->priv->state->duration / 60;
		seconds2 = player->priv->state->duration % 60;
		if (eel_gconf_get_boolean (CONF_UI_TIME_DISPLAY)) {
			return g_strdup_printf (_("%d:%02d of %d:%02d"), minutes, seconds, minutes2, seconds2);
		} else {
			int remaining = player->priv->state->duration - elapsed;
			int remaining_minutes = remaining / 60;
			/* remaining could conceivably be negative. This would
			 * be a bug, but the elapsed time will display right
			 * with the abs(). */
			int remaining_seconds = abs (remaining % 60);
			return g_strdup_printf (_("%d:%02d of %d:%02d remaining"), remaining_minutes, remaining_seconds, minutes2, seconds2);
		}
	} else {
		return g_strdup_printf (_("%d:%02d"), minutes, seconds);
	}
}

static void
rb_header_update_elapsed (RBHeader *player)
{
	char *elapsed_text;

	/* sanity check */
	if ((player->priv->state->elapsed > player->priv->state->duration) || (player->priv->state->elapsed < 0))
		return;

	elapsed_text = rb_header_get_elapsed_string (player);

	gtk_label_set_text (GTK_LABEL (player->priv->elapsed), elapsed_text);
	g_free (elapsed_text);
}
