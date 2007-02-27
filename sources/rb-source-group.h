/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  Jonathan Matthew <jonathan@kaolin.wh9.net>
 *  Copyright (C) 2006  William Jon McCann <mccann@jhu.edu>
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

#ifndef __RB_SOURCE_GROUP_H
#define __RB_SOURCE_GROUP_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	RB_SOURCE_GROUP_CATEGORY_FIXED = 0,	/* library, iradio, podcast, queue */
	RB_SOURCE_GROUP_CATEGORY_PERSISTENT,	/* playlists */
	RB_SOURCE_GROUP_CATEGORY_REMOVABLE,	/* ipod, generic audio player, audio CD */
	RB_SOURCE_GROUP_CATEGORY_TRANSIENT,	/* DAAP shares */
	RB_SOURCE_GROUP_CATEGORY_LAST
} RBSourceGroupCategory;

GType rb_source_group_category_get_type (void);

#define RB_TYPE_SOURCE_GROUP_CATEGORY (rb_source_group_category_get_type())


struct RBSourceGroup
{
	char                 *name;
	char                 *display_name;
	RBSourceGroupCategory category;
};

typedef struct RBSourceGroup RBSourceGroup;

GType          rb_source_group_get_type    (void);
#define RB_TYPE_SOURCE_GROUP	(rb_source_group_get_type ())
#define RB_SOURCE_GROUP(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE_GROUP, RBSourceGroup))
#define RB_IS_SOURCE_GROUP(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE_GROUP))

#define RB_SOURCE_GROUP_LIBRARY           (rb_source_group_library_get_type ())
#define RB_SOURCE_GROUP_PLAYLISTS         (rb_source_group_playlists_get_type ())
#define RB_SOURCE_GROUP_DEVICES           (rb_source_group_devices_get_type ())
#define RB_SOURCE_GROUP_SHARED            (rb_source_group_shared_get_type ())

void                  rb_source_group_init             (void);

RBSourceGroup        *rb_source_group_get_by_name      (const char           *name);
RBSourceGroup        *rb_source_group_register         (const char           *name,
							const char           *display_name,
							RBSourceGroupCategory category);

RBSourceGroup        *rb_source_group_library_get_type   (void);
RBSourceGroup        *rb_source_group_playlists_get_type (void);
RBSourceGroup        *rb_source_group_devices_get_type   (void);
RBSourceGroup        *rb_source_group_shared_get_type    (void);

G_END_DECLS

#endif /* __RB_SOURCE_GROUP_H */
