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

#ifndef __RB_GROUP_VIEW_H
#define __RB_GROUP_VIEW_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-view.h"
#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_GROUP_VIEW         (rb_group_view_get_type ())
#define RB_GROUP_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GROUP_VIEW, RBGroupView))
#define RB_GROUP_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GROUP_VIEW, RBGroupViewClass))
#define RB_IS_GROUP_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GROUP_VIEW))
#define RB_IS_GROUP_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GROUP_VIEW))
#define RB_GROUP_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GROUP_VIEW, RBGroupViewClass))

typedef struct RBGroupViewPrivate RBGroupViewPrivate;

typedef struct
{
	RBView parent;

	RBGroupViewPrivate *priv;
} RBGroupView;

typedef struct
{
	RBViewClass parent;
} RBGroupViewClass;

GType       rb_group_view_get_type      (void);

RBView     *rb_group_view_new           (BonoboUIContainer *container,
					 RBLibrary *library);

RBView     *rb_group_view_new_from_file (BonoboUIContainer *container,
					 RBLibrary *library,
				         const char *file);

void        rb_group_view_set_name      (RBGroupView *group,
				         const char *name);

const char *rb_group_view_get_file      (RBGroupView *group);

G_END_DECLS

#endif /* __RB_GROUP_VIEW_H */
