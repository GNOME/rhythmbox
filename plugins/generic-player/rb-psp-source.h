/*
 *  arch-tag: Header for PSP source object
 *
 *  Copyright (C) 2006 James Livingston  <jrl@ids.org.au>
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

#ifndef __RB_PSP_SOURCE_H
#define __RB_PSP_SOURCE_H

#include "rb-shell.h"
#include "rb-generic-player-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_PSP_SOURCE         (rb_psp_source_get_type ())
#define RB_PSP_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PSP_SOURCE, RBPspSource))
#define RB_PSP_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PSP_SOURCE, RBPspSourceClass))
#define RB_IS_PSP_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PSP_SOURCE))
#define RB_IS_PSP_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PSP_SOURCE))
#define RB_PSP_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PSP_SOURCE, RBPspSourceClass))

typedef struct
{
	RBGenericPlayerSource parent;
} RBPspSource;

typedef struct
{
	RBGenericPlayerSourceClass parent;
} RBPspSourceClass;

RBRemovableMediaSource *	rb_psp_source_new		(RBShell *shell, GnomeVFSVolume *volume);
GType			rb_psp_source_get_type		(void);
GType			rb_psp_source_register_type	(GTypeModule *module);

gboolean		rb_psp_is_volume_player		(GnomeVFSVolume *volume);

G_END_DECLS

#endif /* __RB_PSP_SOURCE_H */
