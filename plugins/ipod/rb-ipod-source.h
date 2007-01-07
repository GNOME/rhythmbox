/*
 *  arch-tag: Header for iPod source object
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_IPOD_SOURCE_H
#define __RB_IPOD_SOURCE_H

#include "rb-shell.h"
#include "rb-removable-media-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_IPOD_SOURCE         (rb_ipod_source_get_type ())
#define RB_IPOD_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IPOD_SOURCE, RBiPodSource))
#define RB_IPOD_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))
#define RB_IS_IPOD_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IPOD_SOURCE))
#define RB_IS_IPOD_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IPOD_SOURCE))
#define RB_IPOD_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IPOD_SOURCE, RBiPodSourceClass))

typedef struct
{
	RBRemovableMediaSource parent;
} RBiPodSource;

typedef struct
{
	RBRemovableMediaSourceClass parent;
} RBiPodSourceClass;

RBRemovableMediaSource *	rb_ipod_source_new		(RBShell *shell, GnomeVFSVolume *volume);
GType			rb_ipod_source_get_type		(void);
GType                   rb_ipod_source_register_type    (GTypeModule *module);

gboolean		rb_ipod_is_volume_ipod		(GnomeVFSVolume *volume);

G_END_DECLS

#endif /* __RB_IPOD_SOURCE_H */
