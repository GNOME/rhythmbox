/*
 *  arch-tag: Header for local file source object
 *
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
 */

#ifndef __RB_LIBRARY_SOURCE_H
#define __RB_LIBRARY_SOURCE_H

#include <gtk/gtkwindow.h>
#include <bonobo/bonobo-ui-container.h>

#include "rb-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_SOURCE         (rb_library_source_get_type ())
#define RB_LIBRARY_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySource))
#define RB_LIBRARY_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourceClass))
#define RB_IS_LIBRARY_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_SOURCE))
#define RB_IS_LIBRARY_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_SOURCE))
#define RB_LIBRARY_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourceClass))

typedef struct RBLibrarySourcePrivate RBLibrarySourcePrivate;

typedef struct
{
	RBSource parent;

	RBLibrarySourcePrivate *priv;
} RBLibrarySource;

typedef struct
{
	RBSourceClass parent;
} RBLibrarySourceClass;

GType		rb_library_source_get_type	(void);

RBSource *	rb_library_source_new		(RhythmDB *db);
void		rb_library_source_show_browser	(RBLibrarySource *source,
						 gboolean show);

void		rb_library_source_add_location	(RBLibrarySource *source, GtkWindow *win);

G_END_DECLS

#endif /* __RB_LIBRARY_SOURCE_H */
