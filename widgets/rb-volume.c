/*
 *  arch-tag: Implementation of Rhythmbox volume control button
 * 
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
 *
 * Some portions are:
 *   (C) Copyright 2001, Richard Hult
 *
 *   Author: Richard Hult <rhult@codefactory.se>
 *
 *   Loosely based on the mixer applet:
 *
 *   GNOME audio mixer module
 *   (C) 1998 The Free Software Foundation
 *
 *   Author: Michael Fulbright <msf@redhat.com>:
 *
 *   Based on:
 *
 *   GNOME time/date display module.
 *   (C) 1997 The Free Software Foundation
 *
 *   Authors: Miguel de Icaza
 *            Federico Mena
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-url.h>

#include "rb-volume.h"
#include "rb-debug.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"

static void rb_volume_class_init (RBVolumeClass *klass);
static void rb_volume_init (RBVolume *link);
static void rb_volume_finalize (GObject *object);
static void rb_volume_sync_volume (RBVolume *volume);
static void clicked_cb (GtkButton *button, RBVolume *volume);
static gboolean scroll_cb (GtkWidget *widget, GdkEvent *event, gpointer unused);
static gboolean scale_button_release_event_cb (GtkWidget *widget,
					       GdkEventButton *event, RBVolume *volume);
static gboolean scale_button_event_cb (GtkWidget *widget, GdkEventButton *event,
				       RBVolume *volume);
static gboolean scale_key_press_event_cb (GtkWidget *widget, GdkEventKey *event,
					  RBVolume *volume);
static void mixer_value_changed_cb (GtkAdjustment *adj, RBVolume *volume);
static void volume_changed_cb (GConfClient *client, guint cnxn_id,
			       GConfEntry *entry, RBVolume *volume);

#define VOLUME_MAX 1.0

struct RBVolumePrivate
{
	GtkWidget *button;

	GtkWidget *window;

	GtkWidget *scale;
	GtkAdjustment *adj;

	GtkWidget *max_image;
	GtkWidget *medium_image;
	GtkWidget *min_image;
	GtkWidget *zero_image;
};

enum
{
	PROP_0,
};

static GtkEventBoxClass *parent_class = NULL;

GType
rb_volume_get_type (void)
{
	static GType rb_volume_type = 0;

	if (rb_volume_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBVolumeClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_volume_class_init,
			NULL,
			NULL,
			sizeof (RBVolume),
			0,
			(GInstanceInitFunc) rb_volume_init
		};

		rb_volume_type = g_type_register_static (GTK_TYPE_EVENT_BOX,
						       "RBVolume",
						       &our_info, 0);
	}

	return rb_volume_type;
}

static void
rb_volume_class_init (RBVolumeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_volume_finalize;
}

static void
rb_volume_init (RBVolume *volume)
{
	GtkWidget *frame;
	GtkWidget *inner_frame;
	GtkWidget *pluslabel, *minuslabel;
	GtkWidget *event;
	GtkWidget *box;

	volume->priv = g_new0 (RBVolumePrivate, 1);

	volume->priv->button = gtk_button_new ();

	gtk_container_add (GTK_CONTAINER (volume), volume->priv->button);

	volume->priv->max_image = gtk_image_new_from_stock (RB_STOCK_VOLUME_MAX,
							     GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (G_OBJECT (volume->priv->max_image));
	volume->priv->medium_image = gtk_image_new_from_stock (RB_STOCK_VOLUME_MEDIUM,
							     GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (G_OBJECT (volume->priv->medium_image));
	volume->priv->min_image = gtk_image_new_from_stock (RB_STOCK_VOLUME_MIN,
							   GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (G_OBJECT (volume->priv->min_image));
	volume->priv->zero_image = gtk_image_new_from_stock (RB_STOCK_VOLUME_ZERO,
							    GTK_ICON_SIZE_LARGE_TOOLBAR);
	g_object_ref (G_OBJECT (volume->priv->zero_image));

	gtk_container_add (GTK_CONTAINER (volume->priv->button), volume->priv->max_image);

	g_signal_connect (G_OBJECT (volume->priv->button), "clicked",
			  G_CALLBACK (clicked_cb), volume);
	g_signal_connect (G_OBJECT (volume->priv->button), "scroll_event",
			  G_CALLBACK (scroll_cb),
			  volume);
	gtk_widget_show_all (GTK_WIDGET (volume));

	volume->priv->window = gtk_window_new (GTK_WINDOW_POPUP);

	volume->priv->adj = GTK_ADJUSTMENT (gtk_adjustment_new (50,
							       0.0,
							       VOLUME_MAX,
							       VOLUME_MAX/20,
							       VOLUME_MAX/10,
							       0.0));
	g_signal_connect (volume->priv->adj,
			  "value-changed",
			  (GCallback) mixer_value_changed_cb,
			  volume);

	frame = gtk_frame_new (NULL);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 0);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

	inner_frame = gtk_frame_new (NULL);
	gtk_container_set_border_width (GTK_CONTAINER (inner_frame), 0);
	gtk_frame_set_shadow_type (GTK_FRAME (inner_frame), GTK_SHADOW_NONE);

	event = gtk_event_box_new ();
	/* This signal is to not let button press close the popup when the press is
	** in the scale */
	g_signal_connect_after (event, "button_press_event",
				G_CALLBACK (scale_button_event_cb), volume);

	box = gtk_vbox_new (FALSE, 0);
	volume->priv->scale = gtk_vscale_new (volume->priv->adj);
	gtk_range_set_inverted (GTK_RANGE (volume->priv->scale), TRUE);
	gtk_widget_set_size_request (volume->priv->scale, -1, 100);

	g_signal_connect (G_OBJECT (volume->priv->window), "scroll_event",
			  G_CALLBACK (scroll_cb),
			  volume);

	g_signal_connect (G_OBJECT (volume->priv->window),
			  "button-press-event",
			  (GCallback) scale_button_release_event_cb,
			  volume);

	/* button event on the scale widget are not catched by its parent window
	** so we must connect to this widget as well */
	g_signal_connect (G_OBJECT (volume->priv->scale),
			  "button-release-event",
			  (GCallback) scale_button_release_event_cb,
			  volume);

	g_signal_connect (G_OBJECT (volume->priv->scale),
			  "key-press-event",
			  (GCallback) scale_key_press_event_cb,
			  volume);

	gtk_scale_set_draw_value (GTK_SCALE (volume->priv->scale), FALSE);

	gtk_range_set_update_policy (GTK_RANGE (volume->priv->scale),
				     GTK_UPDATE_CONTINUOUS);

	gtk_container_add (GTK_CONTAINER (volume->priv->window), frame);

	gtk_container_add (GTK_CONTAINER (frame), inner_frame);

	/* Translators - The + and - refer to increasing and decreasing the volume.
	** I don't know if there are sensible alternatives in other languages */
	pluslabel = gtk_label_new (_("+"));
	minuslabel = gtk_label_new (_("-"));

	gtk_box_pack_start (GTK_BOX (box), pluslabel, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (box), minuslabel, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), volume->priv->scale, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (event), box);
	gtk_container_add (GTK_CONTAINER (inner_frame), event);

	eel_gconf_notification_add (CONF_STATE_VOLUME,
				    (GConfClientNotifyFunc) volume_changed_cb,
				    volume);
	rb_volume_sync_volume (volume);
}

static void
rb_volume_finalize (GObject *object)
{
	RBVolume *volume;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_VOLUME (object));

	volume = RB_VOLUME (object);

	g_return_if_fail (volume->priv != NULL);

	g_free (volume->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBVolume *
rb_volume_new (void)
{
	RBVolume *volume;

	volume = RB_VOLUME (g_object_new (RB_TYPE_VOLUME, NULL));

	g_return_val_if_fail (volume->priv != NULL, NULL);

	return volume;
}

static void
rb_volume_sync_volume (RBVolume *volume)
{
	float vol;
	GtkWidget *image;

	vol = eel_gconf_get_float (CONF_STATE_VOLUME);
	rb_debug ("current volume is %f", vol);
	gtk_container_remove (GTK_CONTAINER (volume->priv->button),
			      gtk_bin_get_child (GTK_BIN (volume->priv->button)));

	if (vol <= 0)
		image = volume->priv->zero_image;
	else if (vol <= (VOLUME_MAX / 3.0))
		image = volume->priv->min_image;
	else if (vol <= 2.0 * (VOLUME_MAX / 3.0))
		image = volume->priv->medium_image;
	else
		image = volume->priv->max_image;

	gtk_widget_show (image);
	gtk_container_add (GTK_CONTAINER (volume->priv->button), image);

	gtk_adjustment_set_value (volume->priv->adj, vol);
}

static void
clicked_cb (GtkButton *button, RBVolume *volume)
{
	GtkRequisition  req;
	GdkGrabStatus pointer, keyboard;
	gint x, y;
	gint button_width, button_height;
	gint window_width, window_height;
	gint spacing = 5;
	gint max_y;

	gint volume_slider_x;
	gint volume_slider_y;
	
	rb_debug ("volume clicked");

/* 	if (GTK_WIDGET_VISIBLE (GTK_WIDGET (volume->priv->window))) */
/* 		return; */

	/*
	 * Position the popup right next to the button.
	 */
	
	max_y = gdk_screen_height ();
	
	gtk_widget_size_request (GTK_WIDGET (volume->priv->window), &req);

	gdk_window_get_origin (gtk_widget_get_parent_window (GTK_BIN (volume->priv->button)->child), &x, &y);
	gdk_drawable_get_size (gtk_widget_get_parent_window (GTK_BIN (volume->priv->button)->child), &button_width, &button_height);
	rb_debug ("window origin: %d %d; size: %d %d", x, y, button_width, button_height);


	
	gtk_widget_show_all (volume->priv->window);
	gdk_drawable_get_size (gtk_widget_get_parent_window (GTK_BIN (volume->priv->window)->child), &window_width, &window_height);
	
	volume_slider_x = x + (button_width - window_width) / 2;
	
	if (y + button_width + window_height + spacing < max_y) {
		/* if volume slider will fit on the screen, display it under
		 * the volume button
		 */
		volume_slider_y = y + button_width + spacing;
	} else {
		/* otherwise display it above the volume button */
		volume_slider_y = y - window_height - spacing;
	}
	
	gtk_window_move (GTK_WINDOW (volume->priv->window), volume_slider_x, volume_slider_y);

	/*
	 * Grab focus and pointer.
	 */
	rb_debug ("grabbing focus");
	gtk_widget_grab_focus (volume->priv->window);
	gtk_grab_add (volume->priv->window);

	pointer = gdk_pointer_grab (volume->priv->window->window,
				    TRUE,
				    (GDK_BUTTON_PRESS_MASK |
				     GDK_BUTTON_RELEASE_MASK |
				     GDK_POINTER_MOTION_MASK |
				     GDK_SCROLL_MASK),
				    NULL, NULL, GDK_CURRENT_TIME);

	keyboard = gdk_keyboard_grab (volume->priv->window->window,
				      TRUE,
				      GDK_CURRENT_TIME);

	if (pointer != GDK_GRAB_SUCCESS || keyboard != GDK_GRAB_SUCCESS) {
		/* We could not grab. */
		rb_debug ("grab failed");
		gtk_grab_remove (volume->priv->window);
		gtk_widget_hide (volume->priv->window);

		if (pointer == GDK_GRAB_SUCCESS) {
			gdk_keyboard_ungrab (GDK_CURRENT_TIME);
		}
		if (keyboard == GDK_GRAB_SUCCESS) {
			gdk_pointer_ungrab (GDK_CURRENT_TIME);
		}

		g_warning ("Could not grab X server!");
		return;
	}

	/* gtk_frame_set_shadow_type (GTK_FRAME (volume->priv->frame), GTK_SHADOW_IN); */
}

static gboolean
scroll_cb (GtkWidget *widget, GdkEvent *event, gpointer unused)
{
	float volume = eel_gconf_get_float (CONF_STATE_VOLUME);

	switch(event->scroll.direction) {
	case GDK_SCROLL_UP:
		volume += 0.1;
		if (volume > 1.0)
			volume = 1.0;
		break;
	case GDK_SCROLL_DOWN:
		volume -= 0.1;
		if (volume < 0)
			volume = 0;
		break;
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
		break;
	}

	rb_debug ("got scroll, setting volume to %f", volume);
	eel_gconf_set_float (CONF_STATE_VOLUME, volume);

	return FALSE;
}


static void
rb_volume_popup_hide (RBVolume *volume)
{
	rb_debug ("hiding popup");
	gtk_grab_remove (volume->priv->window);
	gdk_pointer_ungrab (GDK_CURRENT_TIME);
	gdk_keyboard_ungrab (GDK_CURRENT_TIME);

	gtk_widget_hide (GTK_WIDGET (volume->priv->window));

/* 	gtk_frame_set_shadow_type (GTK_FRAME (data->frame), GTK_SHADOW_NONE); */
}

static gboolean
scale_button_release_event_cb (GtkWidget *widget, GdkEventButton *event, RBVolume *volume)
{
	rb_debug ("scale release");
	rb_volume_popup_hide (volume);
	return FALSE;
}

static gboolean
scale_button_event_cb (GtkWidget *widget, GdkEventButton *event, RBVolume *volume)
{
	rb_debug ("event");
	return TRUE;
}

static gboolean
scale_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, RBVolume *volume)
{
	rb_debug ("got key press");
	switch (event->keyval) {
	case GDK_KP_Enter:
	case GDK_ISO_Enter:
	case GDK_3270_Enter:
	case GDK_Return:
	case GDK_space:
	case GDK_KP_Space:
		rb_volume_popup_hide (volume);
		return TRUE;
	default:
		break;
	}

	return FALSE;
}

static void
mixer_value_changed_cb (GtkAdjustment *adj, RBVolume *volume)
{
	float vol = gtk_adjustment_get_value (volume->priv->adj);

	rb_debug ("setting volume to %f", vol);

	eel_gconf_set_float (CONF_STATE_VOLUME, vol);
}

static void volume_changed_cb (GConfClient *client, guint cnxn_id,
			       GConfEntry *entry, RBVolume *volume)
{
	rb_debug ("volume changed");

	rb_volume_sync_volume (volume);
}
