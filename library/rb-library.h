/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_LIBRARY_H
#define __RB_LIBRARY_H

#include "rb-library-action-queue.h"
#include "rb-node.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY         (rb_library_get_type ())
#define RB_LIBRARY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY, RBLibrary))
#define RB_LIBRARY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY, RBLibraryClass))
#define RB_IS_LIBRARY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY))
#define RB_IS_LIBRARY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY))
#define RB_LIBRARY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY, RBLibraryClass))

typedef struct RBLibraryPrivate RBLibraryPrivate;

typedef struct
{
	GObject parent;

	RBLibraryPrivate *priv;
} RBLibrary;

typedef struct
{
	GObjectClass parent;
} RBLibraryClass;

GType                 rb_library_get_type           (void);

RBLibrary            *rb_library_new                (void);

void                  rb_library_add_file           (RBLibrary *library,
					             const char *uri);
void                  rb_library_remove_node        (RBLibrary *library,
					             RBNode *node);

RBNode               *rb_library_get_all_genres     (RBLibrary *library);
RBNode               *rb_library_get_all_artists    (RBLibrary *library);
RBNode               *rb_library_get_all_albums     (RBLibrary *library);
RBNode               *rb_library_get_all_songs      (RBLibrary *library);

RBLibraryActionQueue *rb_library_get_action_queue   (RBLibrary *library);

G_END_DECLS

#endif /* __RB_LIBRARY_H */
