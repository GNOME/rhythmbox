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

#ifndef __RB_LIBRARY_VIEW_H
#define __RB_LIBRARY_VIEW_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-view.h"
#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_VIEW         (rb_library_view_get_type ())
#define RB_LIBRARY_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_VIEW, RBLibraryView))
#define RB_LIBRARY_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_VIEW, RBLibraryViewClass))
#define RB_IS_LIBRARY_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_VIEW))
#define RB_IS_LIBRARY_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_VIEW))
#define RB_LIBRARY_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_VIEW, RBLibraryViewClass))

typedef struct RBLibraryViewPrivate RBLibraryViewPrivate;

typedef struct
{
	RBView parent;

	RBLibraryViewPrivate *priv;
} RBLibraryView;

typedef struct
{
	RBViewClass parent;
} RBLibraryViewClass;

GType   rb_library_view_get_type (void);

RBView *rb_library_view_new      (BonoboUIContainer *container,
			          Library *library);

G_END_DECLS

#endif /* __RB_LIBRARY_VIEW_H */
