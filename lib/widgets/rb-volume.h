/*
 *  arch-tag: Header for Rhythmbox volume control button
 * 
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

#ifndef __RB_VOLUME_H
#define __RB_VOLUME_H

#include <gtk/gtkbutton.h>

G_BEGIN_DECLS

#define RB_TYPE_VOLUME         (rb_volume_get_type ())
#define RB_VOLUME(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VOLUME, RBVolume))
#define RB_VOLUME_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VOLUME, RBVolumeClass))
#define RB_IS_VOLUME(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VOLUME))
#define RB_IS_VOLUME_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VOLUME))
#define RB_VOLUME_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VOLUME, RBVolumeClass))

typedef struct RBVolumePrivate RBVolumePrivate;

typedef struct
{
	GtkEventBox parent;

	RBVolumePrivate *priv;
} RBVolume;

typedef struct
{
	GtkEventBoxClass parent;
} RBVolumeClass;

GType		rb_volume_get_type	(void);

RBVolume *	rb_volume_new		(void);

G_END_DECLS

#endif /* __RB_VOLUME_H */
