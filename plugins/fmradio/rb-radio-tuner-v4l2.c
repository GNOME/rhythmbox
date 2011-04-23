/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  James Henstridge <james@jamesh.id.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/videodev2.h>

#include <glib.h>

#include "rb-debug.h"
#include "rb-radio-tuner.h"

struct _RBRadioTunerPrivate {
	int fd;
	guint32 range_low;
	guint32 range_high;
	guint32 current_frequency;
	guint32 freq_mul;
};

G_DEFINE_DYNAMIC_TYPE (RBRadioTuner, rb_radio_tuner, G_TYPE_OBJECT);

static void rb_radio_tuner_finalize (GObject *object);

static void
rb_radio_tuner_class_init (RBRadioTunerClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = rb_radio_tuner_finalize;

	g_type_class_add_private (class, sizeof (RBRadioTunerPrivate));
}

static void
rb_radio_tuner_class_finalize (RBRadioTunerClass *class)
{
}

static void
rb_radio_tuner_init (RBRadioTuner *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, RB_TYPE_RADIO_TUNER,
						 RBRadioTunerPrivate);
	self->priv->fd = -1;
}

static void
rb_radio_tuner_finalize (GObject *object)
{
	RBRadioTuner *self = (RBRadioTuner *)object;

	g_free (self->card_name);
	self->card_name = NULL;
	if (self->priv->fd >= 0)
		close (self->priv->fd);
	self->priv->fd = -1;

        G_OBJECT_CLASS (rb_radio_tuner_parent_class)->finalize (object);
}

RBRadioTuner *
rb_radio_tuner_new (const gchar *devname, GError **err)
{
	int fd = -1;
	struct v4l2_capability caps;
	struct v4l2_tuner tuner;
	RBRadioTuner *self;

	if (devname == NULL)
		devname = "/dev/radio0";

	fd = open(devname, O_RDONLY);
	if (fd < 0) {
		g_warning("Could not open device %s", devname);
		goto err;
	}
	memset (&caps, 0, sizeof (caps));
	if (ioctl (fd, VIDIOC_QUERYCAP, &caps) < 0) {
		g_warning("Could not query device capabilities: %s",
			g_strerror(errno));
		goto err;
	}
	if ((caps.capabilities & V4L2_CAP_TUNER) == 0) {
		g_warning("Device is not a tuner");
		goto err;
	}

	/* check the tuner */
	memset(&tuner, 0, sizeof(tuner));
	tuner.index = 0;
	if (ioctl(fd, VIDIOC_G_TUNER, &tuner) < 0) {
		g_warning("Could not query tuner info: %s", g_strerror(errno));
		goto err;
	}
	if (tuner.type != V4L2_TUNER_RADIO) {
		g_warning("Device is not a radio tuner");
		goto err;
	}


	self = RB_RADIO_TUNER (g_object_new (RB_TYPE_RADIO_TUNER, NULL));

	/* fill in information */
	self->priv->fd = fd;
	self->card_name = g_strndup((const char *)caps.card,
				    sizeof(caps.card));
	self->priv->range_low = tuner.rangelow;
	self->priv->range_high = tuner.rangehigh;
	if ((tuner.capability & V4L2_TUNER_CAP_LOW) != 0)
		self->priv->freq_mul = 16000;
	else
		self->priv->freq_mul = 16;

	self->min_freq = (double)self->priv->range_low / self->priv->freq_mul;
	self->max_freq = (double)self->priv->range_high / self->priv->freq_mul;

	rb_radio_tuner_update (self);
	return self;

 err:
	if (fd >= 0)
		close (fd);
	return NULL;
}

void
rb_radio_tuner_update (RBRadioTuner *self)
{
	struct v4l2_tuner tuner;
	struct v4l2_control control;
	struct v4l2_frequency frequency;
	gboolean has_changed = FALSE;

	memset (&tuner, 0, sizeof (tuner));
	tuner.index = 0;
	if (ioctl (self->priv->fd, VIDIOC_G_TUNER, &tuner) >= 0) {
		if (self->is_stereo != (tuner.audmode==V4L2_TUNER_MODE_STEREO))
			has_changed = TRUE;
		self->is_stereo = (tuner.audmode==V4L2_TUNER_MODE_STEREO);

		if (self->signal != tuner.signal)
			has_changed = TRUE;
		self->signal = tuner.signal;
	}

	memset (&control, 0, sizeof (control));
	control.id = V4L2_CID_AUDIO_MUTE;
	if (ioctl (self->priv->fd, VIDIOC_G_CTRL, &control) >= 0) {
		control.value = !!control.value;
		if (self->is_muted != control.value)
			has_changed = TRUE;
		self->is_muted = control.value;
	}

	memset (&frequency, 0, sizeof (frequency));
	frequency.tuner = 0;
	if (ioctl (self->priv->fd, VIDIOC_G_FREQUENCY, &frequency) >= 0) {
		if (self->priv->current_frequency != frequency.frequency)
			has_changed = TRUE;
		self->priv->current_frequency = frequency.frequency;
		self->frequency = (double)frequency.frequency
			/ self->priv->freq_mul;
	}

	rb_debug ("Tuner %s", has_changed ? "has changed" : "has not changed");

#if 0
	if (has_changed) {
		g_signal_emit (self, CHANGED);
	}
#endif
}

gboolean
rb_radio_tuner_set_frequency (RBRadioTuner *self, double frequency)
{
	struct v4l2_frequency freq;
	guint new_freq;

	new_freq = frequency * self->priv->freq_mul;
	new_freq = CLAMP (new_freq,
			  self->priv->range_low, self->priv->range_high);

	memset (&freq, 0, sizeof (freq));
	freq.tuner = 0;
	freq.type = V4L2_TUNER_RADIO;
	freq.frequency = new_freq;
	return ioctl (self->priv->fd, VIDIOC_S_FREQUENCY, &freq) >= 0;
}

gboolean
rb_radio_tuner_set_mute (RBRadioTuner *self, gboolean mute)
{
	struct v4l2_control control;

	memset(&control, 0, sizeof(control));
	control.id = V4L2_CID_AUDIO_MUTE;
	control.value = !!mute;
	return ioctl (self->priv->fd, VIDIOC_S_CTRL, &control) >= 0;
}

void
_rb_radio_tuner_register_type (GTypeModule *module)
{
	rb_radio_tuner_register_type (module);
}
