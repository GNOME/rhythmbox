/*
 *  arch-tag: Header for removable media source object
 *
 *  Copyright (C) 2005 James Livingston  <jrl@ids.org.au>
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

#ifndef __RB_REMOVABLE_MEDIA_SOURCE_H
#define __RB_REMOVABLE_MEDIA_SOURCE_H

#include "rb-shell.h"
#include "rb-browser-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOVABLE_MEDIA_SOURCE         (rb_removable_media_source_get_type ())
#define RB_REMOVABLE_MEDIA_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSource))
#define RB_REMOVABLE_MEDIA_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourceClass))
#define RB_IS_REMOVABLE_MEDIA_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE))
#define RB_IS_REMOVABLE_MEDIA_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOVABLE_MEDIA_SOURCE))
#define RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourceClass))

typedef struct
{
	RBBrowserSource parent;
} RBRemovableMediaSource;

typedef struct
{
	RBBrowserSourceClass parent;
} RBRemovableMediaSourceClass;

GType			rb_removable_media_source_get_type	(void);


G_END_DECLS

#endif /* __RB_REMOVABLE_MEDIA_SOURCE_H */
