/*
 *  Copyright (C) 2002 Jeroen Zwartepoorte <jeroen@xs4all.nl>
 *
 *  Based on:
 *
 *  Mixer (volume control) applet.
 *
 *  (C) Copyright 2001, Richard Hult
 *
 *  Author: Richard Hult <rhult@codefactory.se>
 *
 *  Loosely based on the mixer applet:
 *
 *  GNOME audio mixer module
 *  (C) 1998 The Free Software Foundation
 *
 *  Author: Michael Fulbright <msf@redhat.com>:
 *
 *  Based on:
 *
 *  GNOME time/date display module.
 *  (C) 1997 The Free Software Foundation
 *
 *  Authors: Miguel de Icaza
 *           Federico Mena
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

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-macros.h>
#include <libgnomeui/gnome-dialog-util.h>
#include "rb-volume.h"
#include "rb-stock-icons.h"

enum {
	PROP_0,
	PROP_MIXER
};

struct _RBVolumePrivate {
	GtkObject *adjustment;
	GtkWidget *slider;
	GtkWidget *indicator;
	GtkWidget *indicator_image;

	GdkPixbuf *volume_max_pixbuf;
	GdkPixbuf *volume_medium_pixbuf;
	GdkPixbuf *volume_min_pixbuf;
	GdkPixbuf *volume_zero_pixbuf;
	GdkPixbuf *volume_mute_pixbuf;

	float vol;

	gboolean mute;
	
	GtkTooltips *tooltip;

	MonkeyMediaMixer *mixer;
};

#define VOLUME_MAX 4.0

static void rb_volume_class_init (RBVolumeClass *klass);
static void rb_volume_instance_init (RBVolume *volume);
static void rb_volume_finalize (GObject *object);
static void rb_volume_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_volume_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);
static void volume_changed_cb (GtkAdjustment *adjustment,
			       RBVolume *volume);
static void volume_mute_cb (GtkWidget* button,
			    GdkEventButton *event,
			    RBVolume* volume);
static gboolean volume_scroll_cb (GtkWidget *button,
				  GdkEvent *event,
				  RBVolume *volume);
static void rb_volume_update_slider (RBVolume *volume);
static void rb_volume_update_image (RBVolume *volume);

/* Boilerplate. */
GNOME_CLASS_BOILERPLATE (RBVolume, rb_volume, GtkHBox, GTK_TYPE_HBOX);

static void
rb_volume_class_init (RBVolumeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = rb_volume_get_property;
	object_class->set_property = rb_volume_set_property;
	object_class->finalize = rb_volume_finalize;

	g_object_class_install_property (object_class,
					 PROP_MIXER,
					 g_param_spec_object ("mixer",
							      "Mixer object",
							      "MonkeyMediaMixer object",
							      MONKEY_MEDIA_TYPE_MIXER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_volume_instance_init (RBVolume *volume)
{
	RBVolumePrivate *priv;

	priv = g_new0 (RBVolumePrivate, 1);
	volume->priv = priv;

	gtk_box_set_spacing (GTK_BOX (volume), 1);

	priv->volume_max_pixbuf    = gtk_widget_render_icon (GTK_WIDGET (volume),
							     RB_STOCK_VOLUME_MAX,
							     GTK_ICON_SIZE_MENU,
							     NULL);
	priv->volume_medium_pixbuf = gtk_widget_render_icon (GTK_WIDGET (volume),
							     RB_STOCK_VOLUME_MEDIUM,
							     GTK_ICON_SIZE_MENU,
							     NULL);
	priv->volume_min_pixbuf    = gtk_widget_render_icon (GTK_WIDGET (volume),
							     RB_STOCK_VOLUME_MIN,
							     GTK_ICON_SIZE_MENU,
							     NULL);
	priv->volume_zero_pixbuf   = gtk_widget_render_icon (GTK_WIDGET (volume),
							     RB_STOCK_VOLUME_ZERO,
							     GTK_ICON_SIZE_MENU,
							     NULL);
	priv->volume_mute_pixbuf   = gtk_widget_render_icon (GTK_WIDGET (volume),
							     RB_STOCK_VOLUME_MUTE,
							     GTK_ICON_SIZE_MENU,
							     NULL);
	priv->mute = FALSE;

	/* Speaker event box */
	priv->indicator = gtk_event_box_new ();
	priv->indicator_image = gtk_image_new_from_pixbuf (priv->volume_medium_pixbuf);
	gtk_container_add (GTK_CONTAINER (priv->indicator), priv->indicator_image);

	g_signal_connect (G_OBJECT (priv->indicator),
			  "button_press_event",
			  G_CALLBACK (volume_mute_cb),
			  volume);
	g_signal_connect (G_OBJECT (priv->indicator),
			  "scroll_event",
			  G_CALLBACK (volume_scroll_cb),
			  volume);

	priv->tooltip = gtk_tooltips_new ();
	gtk_tooltips_set_tip (priv->tooltip, priv->indicator, _("Click to mute"), NULL);
	gtk_box_pack_start (GTK_BOX (volume), priv->indicator, FALSE, FALSE, 0);
	gtk_widget_show (priv->indicator);

	/* Volume slider */
	priv->adjustment = gtk_adjustment_new (0, 0, VOLUME_MAX, 1.0, 2.0, 0);
	priv->slider = gtk_hscale_new (GTK_ADJUSTMENT (priv->adjustment));
	gtk_range_set_inverted (GTK_RANGE (priv->slider), TRUE);
	gtk_scale_set_draw_value (GTK_SCALE (priv->slider), FALSE);
	gtk_widget_set_size_request (priv->slider, 75, -1);
	gtk_box_pack_start (GTK_BOX (volume), priv->slider, TRUE, TRUE, 0);
	gtk_widget_show (priv->slider);

	g_signal_connect (G_OBJECT (priv->slider),
			  "scroll_event",
			  G_CALLBACK (volume_scroll_cb),
			  volume);

	g_signal_connect (G_OBJECT (priv->adjustment),
			  "value-changed",
			  G_CALLBACK (volume_changed_cb),
			  volume);
}

static void
rb_volume_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBVolume *volume = RB_VOLUME (object);

	switch (prop_id) {
	case PROP_MIXER:
		volume->priv->mixer = g_value_get_object (value);
		
		volume->priv->vol = monkey_media_mixer_get_volume (volume->priv->mixer);

		rb_volume_update_slider (volume);
		rb_volume_update_image (volume);
		break;
	default:
		break;
	}
}

static void
rb_volume_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBVolume *volume = RB_VOLUME (object);

	switch (prop_id) {
	case PROP_MIXER:
		g_value_set_object (value, volume->priv->mixer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
rb_volume_finalize (GObject *object)
{
	RBVolume *volume = RB_VOLUME (object);
	
	g_object_unref (G_OBJECT (volume->priv->volume_max_pixbuf));
	g_object_unref (G_OBJECT (volume->priv->volume_medium_pixbuf));
	g_object_unref (G_OBJECT (volume->priv->volume_min_pixbuf));
	g_object_unref (G_OBJECT (volume->priv->volume_zero_pixbuf));
	g_object_unref (G_OBJECT (volume->priv->volume_mute_pixbuf));

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_mixer (RBVolume *volume)
{
	monkey_media_mixer_set_volume (volume->priv->mixer,
				       volume->priv->vol);
	monkey_media_mixer_set_mute (volume->priv->mixer,
				     volume->priv->mute);
}

static void
volume_changed_cb (GtkAdjustment *adjustment,
		   RBVolume *volume)
{
	volume->priv->vol = VOLUME_MAX - adjustment->value;
	rb_volume_update_image (volume);
	update_mixer (volume);
}

static void
volume_mute_cb (GtkWidget *button,
		GdkEventButton *event,
		RBVolume *volume)
{
	gboolean mute;

	if (event->button != 1)
		return;

	mute = !volume->priv->mute;
	
	rb_volume_set_mute (volume, mute);
}

static gboolean
volume_scroll_cb (GtkWidget *button,
		  GdkEvent *event,
		  RBVolume *volume)
{
	GdkEventScroll *event_scroll;
	gdouble value, inc;

	if (event->type != GDK_SCROLL)
		return FALSE;

	event_scroll = (GdkEventScroll *)event;

	if (event_scroll->direction == GDK_SCROLL_UP ||
	    event_scroll->direction == GDK_SCROLL_RIGHT)
		inc = -GTK_ADJUSTMENT (volume->priv->adjustment)->page_increment / 2;
	else
		inc = GTK_ADJUSTMENT (volume->priv->adjustment)->page_increment / 2;
	
	value = GTK_ADJUSTMENT (volume->priv->adjustment)->value;
	gtk_adjustment_set_value (GTK_ADJUSTMENT (volume->priv->adjustment),
			          value + inc);
	
	return TRUE;
}

static void
rb_volume_update_slider (RBVolume *volume)
{
	gtk_adjustment_set_value (GTK_ADJUSTMENT (volume->priv->adjustment),
				  VOLUME_MAX - volume->priv->vol);
}

static void
rb_volume_update_image (RBVolume *volume)
{
	int vol;
	GdkPixbuf *pixbuf;

	vol = volume->priv->vol;

	if (volume->priv->mute)
		pixbuf = volume->priv->volume_mute_pixbuf;
	else if (vol <= 0)
		pixbuf = volume->priv->volume_zero_pixbuf;
	else if (vol <= (VOLUME_MAX / 3))
		pixbuf = volume->priv->volume_min_pixbuf;
	else if (vol <= 2 * (VOLUME_MAX / 3))
		pixbuf = volume->priv->volume_medium_pixbuf;
	else
		pixbuf = volume->priv->volume_max_pixbuf;

	gtk_image_set_from_pixbuf (GTK_IMAGE (volume->priv->indicator_image), pixbuf);
}

RBVolume *
rb_volume_new (MonkeyMediaMixer *mixer)
{
	RBVolume *volume;

	volume = RB_VOLUME (g_object_new (RB_TYPE_VOLUME,
					  "mixer", mixer,
					  NULL));

	return volume;
}

int
rb_volume_get (RBVolume *volume)
{
	g_return_val_if_fail (RB_IS_VOLUME (volume), -1);

	return volume->priv->vol;
}

void
rb_volume_set (RBVolume *volume,
	       int value)
{
	g_return_if_fail (RB_IS_VOLUME (volume));

	volume->priv->vol = value;
	rb_volume_update_slider (volume);
}

void
rb_volume_set_mute (RBVolume *volume, gboolean mute)
{
	volume->priv->mute = mute;

	if (volume->priv->mute == TRUE)
		gtk_tooltips_set_tip (volume->priv->tooltip,
				volume->priv->indicator,
				_("Click to unmute"), NULL);
	else
		gtk_tooltips_set_tip (volume->priv->tooltip,
				volume->priv->indicator,
				_("Click to mute"), NULL);

	rb_volume_update_image (volume);
	update_mixer (volume);
}

gboolean
rb_volume_get_mute (RBVolume *volume)
{
	return volume->priv->mute;
}
