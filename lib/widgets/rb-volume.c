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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-macros.h>
#include <libgnomeui/gnome-dialog-util.h>
#include "rb-volume.h"

#include "volume-zero.xpm"
#include "volume-min.xpm"
#include "volume-medium.xpm"
#include "volume-max.xpm"
#include "volume-mute.xpm"

#ifdef HAVE_LINUX_SOUNDCARD_H
#include <linux/soundcard.h>
#define OSS_API
#elif HAVE_MACHINE_SOUNDCARD_H
#include <machine/soundcard.h>
#define OSS_API
#elif HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#define OSS_API
#elif HAVE_SOUNDCARD_H
#include <soundcard.h>
#define OSS_API
#elif HAVE_SYS_AUDIOIO_H
#include <sys/audioio.h>
#define SUN_API
#elif HAVE_SYS_AUDIO_IO_H
#include <sys/audio.io.h>
#define SUN_API
#elif HAVE_SUN_AUDIOIO_H
#include <sun/audioio.h>
#define SUN_API
#elif HAVE_DMEDIA_AUDIO_H
#define IRIX_API
#include <dmedia/audio.h>
#else
#error No soundcard definition!
#endif /* SOUNDCARD_H */

#ifdef OSS_API
#define VOLUME_MAX 100
#endif
#ifdef SUN_API
#define VOLUME_MAX 255
#endif
#ifdef IRIX_API
#define VOLUME_MAX 255
#endif

enum {
	PROP_BOGUS,
	PROP_CHANNEL
};

struct _RBVolumePrivate {
	GtkObject *adjustment;
	GtkWidget *slider;
	GtkWidget *indicator_image;

	GdkPixbuf *volume_max_pixbuf;
	GdkPixbuf *volume_medium_pixbuf;
	GdkPixbuf *volume_min_pixbuf;
	GdkPixbuf *volume_zero_pixbuf;
	GdkPixbuf *volume_mute_pixbuf;

	int mixerfd;
	int vol;
	int timeout;
	int channel;

#ifdef IRIX_API
	/* Note: we are using the obsolete API to increase portability.
	 * /usr/sbin/audiopanel provides many more options...
	 */
#define MAX_PV_BUF 4
	long pv_buf[MAX_PV_BUF] = {
		AL_LEFT_SPEAKER_GAIN, 0L, AL_RIGHT_SPEAKER_GAIN, 0L
	};
#endif
};

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
static gboolean timeout_cb (RBVolume *volume);
static void rb_volume_update_slider (RBVolume *volume);
static void rb_volume_update_image (RBVolume *volume);

static gboolean open_mixer (RBVolume *volume,
			    const char *device,
			    int channel);
static int read_mixer (RBVolume *volume);
static void update_mixer (RBVolume *volume);

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
					 PROP_CHANNEL,
					 g_param_spec_int ("channel",
							   _("Mixer channel"),
							   _("The mixer channel of which the volume is controlled."),
							   RB_VOLUME_CHANNEL_PCM,
							   RB_VOLUME_CHANNEL_MASTER,
							   RB_VOLUME_CHANNEL_MASTER,
							   G_PARAM_READWRITE));
}

static void
rb_volume_instance_init (RBVolume *volume)
{
	RBVolumePrivate *priv;

	priv = g_new0 (RBVolumePrivate, 1);
	volume->priv = priv;

	priv->volume_max_pixbuf = gdk_pixbuf_new_from_xpm_data (volume_max_xpm);
	priv->volume_medium_pixbuf = gdk_pixbuf_new_from_xpm_data (volume_medium_xpm);
	priv->volume_min_pixbuf = gdk_pixbuf_new_from_xpm_data (volume_min_xpm);
	priv->volume_zero_pixbuf = gdk_pixbuf_new_from_xpm_data (volume_zero_xpm);
	priv->volume_mute_pixbuf = gdk_pixbuf_new_from_xpm_data (volume_mute_xpm);

	priv->indicator_image = gtk_image_new_from_pixbuf (priv->volume_medium_pixbuf);
	gtk_box_pack_start (GTK_BOX (volume), priv->indicator_image, FALSE, FALSE, 0);
	gtk_widget_show (priv->indicator_image);

	priv->adjustment = gtk_adjustment_new (0, 0, VOLUME_MAX, 1, 5, 0);
	priv->slider = gtk_hscale_new (GTK_ADJUSTMENT (priv->adjustment));
	gtk_range_set_inverted (GTK_RANGE (priv->slider), TRUE);
	gtk_scale_set_draw_value (GTK_SCALE (priv->slider), FALSE);
	gtk_widget_set_size_request (priv->slider, 75, -1);
	gtk_box_pack_start (GTK_BOX (volume), priv->slider, TRUE, TRUE, 0);
	gtk_widget_show (priv->slider);

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
	RBVolumePrivate *priv = volume->priv;
	const char *device;
	gboolean retval;
#ifdef SUN_API
	char *ctl = NULL;
#endif

	switch (prop_id) {
	case PROP_CHANNEL:
		if (priv->mixerfd != 0)
			close (volume->priv->mixerfd);

		if (priv->timeout != 0)
			gtk_timeout_remove (priv->timeout);

		priv->channel = g_value_get_int (value);

#ifdef OSS_API
		/* /dev/sound/mixer for devfs */
		device = "/dev/mixer";
		retval = open_mixer (volume, device, priv->channel);
		if (!retval) {
			device = "/dev/sound/mixer";
			retval = open_mixer (volume, device, priv->channel);
		}
#endif
#ifdef SUN_API
		if (!(ctl = g_getenv ("AUDIODEV")))
			ctl = "/dev/audio";
		device = g_strdup_printf ("%sctl", ctl);
		retval = open_mixer (volume, device, priv->channel);
#endif

		if (!retval) {
			GtkWidget *dialog;
			dialog = gtk_message_dialog_new (NULL,
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_CLOSE,
							 _("Couldn't open mixer device %s\n"),
							 device);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}

		priv->vol = read_mixer (volume);

		priv->timeout = gtk_timeout_add (500,
						 (GSourceFunc)timeout_cb,
						 volume);

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
	case PROP_CHANNEL:
		g_value_set_int (value, volume->priv->channel);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
rb_volume_finalize (GObject *object)
{
	RBVolume *volume = RB_VOLUME (object);

	if (volume->priv->timeout != 0)
		gtk_timeout_remove (volume->priv->timeout);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
volume_changed_cb (GtkAdjustment *adjustment,
		   RBVolume *volume)
{
	volume->priv->vol = VOLUME_MAX - adjustment->value;
	rb_volume_update_image (volume);
	update_mixer (volume);
}

static gboolean
timeout_cb (RBVolume *volume)
{
	int vol = read_mixer (volume);

	if (vol != volume->priv->vol) {
		volume->priv->vol = vol;
		rb_volume_update_slider (volume);
	}

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

	if (vol <= 0)
		pixbuf = volume->priv->volume_zero_pixbuf;
	else if (vol <= VOLUME_MAX / 3)
		pixbuf = volume->priv->volume_min_pixbuf;
	else if (vol <= 2 * VOLUME_MAX / 3)
		pixbuf = volume->priv->volume_medium_pixbuf;
	else
		pixbuf = volume->priv->volume_max_pixbuf;

	gtk_image_set_from_pixbuf (GTK_IMAGE (volume->priv->indicator_image), pixbuf);
}

static gboolean
open_mixer (RBVolume *volume,
	    const char *device,
	    int channel)
{
	RBVolumePrivate *priv = volume->priv;
	int res, ver;
#ifdef OSS_API
	int devmask;

	priv->mixerfd = open (device, O_RDWR, 0);
#endif
#ifdef SUN_API
	priv->mixerfd = open (device, O_RDWR);
#endif
#ifdef IRIX_API
	/* This is a result code, not a file descriptor, and we ignore
	 * the values read.  But the call is useful to see if we can
	 * access the default output port.
	 */
	priv->mixerfd = ALgetparams (AL_DEFAULT_DEVICE, priv->pv_buf, MAX_PV_BUF);
#endif
	if (priv->mixerfd < 0) {
		/* Probably should die more gracefully. */
		return FALSE;
	}

	/* Check driver-version. */
#ifdef OSS_GETVERSION
	res = ioctl (priv->mixerfd, OSS_GETVERSION, &ver);
	if ((res == 0) && (ver != SOUND_VERSION)) {
		g_message (_("warning: this version of gmix was compiled "
			     "with a different version of\nsoundcard.h.\n"));
	}
#endif
#ifdef OSS_API
	/* Check whether this mixer actually supports the channel we're going to
	 * try to monitor.
	 */
	res = ioctl (priv->mixerfd, MIXER_READ (SOUND_MIXER_DEVMASK), &devmask);
	if (res != 0) {
		char *s = g_strdup_printf (_("Querying available channels of "
					     "mixer device %s failed\n"), device);
		gnome_error_dialog (s);
		g_free (s);
		return TRUE;
	} else if (devmask & SOUND_MASK_PCM && channel == RB_VOLUME_CHANNEL_PCM) {
		priv->channel = SOUND_MIXER_PCM;
	} else if (devmask & SOUND_MASK_CD && channel == RB_VOLUME_CHANNEL_CD) {
		priv->channel = SOUND_MIXER_CD;
	} else if (devmask & SOUND_MASK_VOLUME) {
		priv->channel = SOUND_MIXER_VOLUME;
	} else {
		char *s = g_strdup_printf (_("Mixer device %s has neither volume"
					     " nor PCM channels.\n"), device);
		gnome_error_dialog (s);
		g_free (s);
		return TRUE;
	}
#endif
	return TRUE;
}

static int
read_mixer (RBVolume *volume)
{
	RBVolumePrivate *priv = volume->priv;
	int vol, r, l;
#ifdef OSS_API
	/* If we couldn't open the mixer. */
	if (priv->mixerfd < 0)
		return 0;

	ioctl (priv->mixerfd, MIXER_READ (priv->channel), &vol);

	l = vol & 0xff;
	r = (vol & 0xff00) >> 8;

	return (r + l) / 2;
#endif
#ifdef SUN_API
	audio_info_t ainfo;
	AUDIO_INITINFO (&ainfo);
	ioctl (priv->mixerfd, AUDIO_GETINFO, &ainfo);
	return (ainfo.play.gain);
#endif
#ifdef IRIX_API
	/* Average the current gain settings.  If we can't read the
	 * current levels use the values from the previous read.
	 */
	(void) ALgetparams (AL_DEFAULT_DEVICE, priv->pv_buf, MAX_PV_BUF);
	return (priv->pv_buf[1] + priv->pv_buf[3]) / 2;
#endif
}

static void
update_mixer (RBVolume *volume)
{
	RBVolumePrivate *priv = volume->priv;
	int vol = priv->vol;
	int tvol;
#ifdef OSS_API
	/* If we couldn't open the mixer. */
	if (priv->mixerfd < 0)
		return;

	tvol = (vol << 8) + vol;
	ioctl (priv->mixerfd, MIXER_WRITE (priv->channel), &tvol);
	ioctl (priv->mixerfd, MIXER_WRITE (SOUND_MIXER_SPEAKER), &tvol);
#endif
#ifdef SUN_API
	audio_info_t ainfo;
	AUDIO_INITINFO (&ainfo);
	ainfo.play.gain = vol;
	ioctl (priv->mixerfd, AUDIO_SETINFO, &ainfo);
#endif
#ifdef IRIX_API
	if (vol < 0) 
		tvol = 0;
	else if (vol > VOLUME_MAX)
		tvol = VOLUME_MAX;
	else
		tvol = vol;

	priv->pv_buf[1] = priv->pv_buf[3] = tvol;
	(void) ALsetparams (AL_DEFAULT_DEVICE, priv->pv_buf, MAX_PV_BUF);
#endif
}

RBVolume *
rb_volume_new (int channel)
{
	RBVolume *volume;

	volume = RB_VOLUME (g_object_new (RB_TYPE_VOLUME, "channel", channel,
					  NULL));

	return volume;
}

void
rb_volume_set (RBVolume *volume,
	       int value)
{
	g_return_if_fail (RB_IS_VOLUME (volume));

	volume->priv->vol = value;
	rb_volume_update_slider (volume);
}

int
rb_volume_get_channel (RBVolume *volume)
{
	g_return_val_if_fail (RB_IS_VOLUME (volume), -1);

	return volume->priv->channel;
}

void
rb_volume_set_channel (RBVolume *volume,
		       int channel)
{
	g_return_if_fail (RB_IS_VOLUME (volume));

	switch (channel) {
	case RB_VOLUME_CHANNEL_PCM:
	case RB_VOLUME_CHANNEL_CD:
	case RB_VOLUME_CHANNEL_MASTER:
		g_object_set (G_OBJECT (volume), "channel", channel, NULL);
		break;
	default:
		g_error (_("Invalid channel number"));
		break;
	}
}
