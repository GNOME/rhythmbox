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

#ifndef __RB_GROUP_SOURCE_H
#define __RB_GROUP_SOURCE_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-source.h"
#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_GROUP_SOURCE         (rb_group_source_get_type ())
#define RB_GROUP_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GROUP_SOURCE, RBGroupSource))
#define RB_GROUP_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GROUP_SOURCE, RBGroupSourceClass))
#define RB_IS_GROUP_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GROUP_SOURCE))
#define RB_IS_GROUP_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GROUP_SOURCE))
#define RB_GROUP_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GROUP_SOURCE, RBGroupSourceClass))

typedef struct RBGroupSourcePrivate RBGroupSourcePrivate;

typedef struct
{
	RBSource parent;

	RBGroupSourcePrivate *priv;
} RBGroupSource;

typedef struct
{
	RBSourceClass parent;
} RBGroupSourceClass;

GType		rb_group_source_get_type      (void);

RBSource *	rb_group_source_new           (BonoboUIContainer *container,
					       RBLibrary *library);

GtkWidget *	rb_group_source_create_dialog (RBGroupSource *groupsrc);

RBSource *	rb_group_source_new_from_file (BonoboUIContainer *container,
					       RBLibrary *library,
					       const char *file);

void		rb_group_source_set_name      (RBGroupSource *group,
					       const char *name);

const char *	rb_group_source_get_file      (RBGroupSource *group);

void		rb_group_source_remove_file   (RBGroupSource *group);

void		rb_group_source_save          (RBGroupSource *source);
void		rb_group_source_load          (RBGroupSource *source);

void		rb_group_source_add_node      (RBGroupSource *source, 
					       RBNode *node);

G_END_DECLS

#endif /* __RB_GROUP_SOURCE_H */
