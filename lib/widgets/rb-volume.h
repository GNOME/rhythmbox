/*
 *  Copyright (C) 2002 Jeroen Zwartepoorte <jeroen@xs4all.nl>
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

#ifndef __RB_VOLUME_H
#define __RB_VOLUME_H

#include <gtk/gtkhbox.h>

G_BEGIN_DECLS

#define RB_TYPE_VOLUME         (rb_volume_get_type ())
#define RB_VOLUME(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VOLUME, RBVolume))
#define RB_VOLUME_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VOLUME, RBVolumeClass))
#define RB_IS_VOLUME(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VOLUME))
#define RB_IS_VOLUME_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VOLUME))
#define RB_VOLUME_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VOLUME, RBVolumeClass))

typedef struct _RBVolumePrivate RBVolumePrivate;

typedef struct {
	GtkHBox parent;

	RBVolumePrivate *priv;
} RBVolume;

typedef struct {
	GtkHBoxClass parent;
} RBVolumeClass;

enum {
	RB_VOLUME_CHANNEL_PCM,
	RB_VOLUME_CHANNEL_CD,
	RB_VOLUME_CHANNEL_MASTER
};

GType     rb_volume_get_type    (void);

RBVolume *rb_volume_new         (int channel);

int	  rb_volume_get		(RBVolume *volume);
void      rb_volume_set         (RBVolume *volume,
				 int value);

gboolean  rb_volume_get_mute	(RBVolume *volume);
void      rb_volume_set_mute	(RBVolume *volume, gboolean mute);

int       rb_volume_get_channel (RBVolume *volume);
void      rb_volume_set_channel (RBVolume *volume,
				 int channel);

G_END_DECLS

#endif /* __RB_VOLUME_H */
