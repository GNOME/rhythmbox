/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew <jonathan@d14n.org>
 *  Copyright (C) 2006  William Jon McCann <mccann@jhu.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#ifndef __RB_DISPLAY_PAGE_GROUP_H
#define __RB_DISPLAY_PAGE_GROUP_H

#include <sources/rb-display-page.h>
#include <sources/rb-display-page-model.h>

G_BEGIN_DECLS

typedef enum {
	RB_DISPLAY_PAGE_GROUP_CATEGORY_FIXED = 0,
	RB_DISPLAY_PAGE_GROUP_CATEGORY_REMOVABLE,
	RB_DISPLAY_PAGE_GROUP_CATEGORY_PERSISTENT,
	RB_DISPLAY_PAGE_GROUP_CATEGORY_TRANSIENT,
	RB_DISPLAY_PAGE_GROUP_CATEGORY_TOOLS,
	RB_DISPLAY_PAGE_GROUP_CATEGORY_LAST
} RBDisplayPageGroupCategory;

GType rb_display_page_group_category_get_type (void);

#define RB_TYPE_DISPLAY_PAGE_GROUP_CATEGORY (rb_display_page_group_category_get_type())

typedef struct _RBDisplayPageGroup RBDisplayPageGroup;
typedef struct _RBDisplayPageGroupClass RBDisplayPageGroupClass;
typedef struct _RBDisplayPageGroupPrivate RBDisplayPageGroupPrivate;

struct _RBDisplayPageGroup
{
	RBDisplayPage parent;

	RBDisplayPageGroupPrivate *priv;
};

struct _RBDisplayPageGroupClass
{
	RBDisplayPageClass parent_class;
};


GType          rb_display_page_group_get_type    (void);

#define RB_TYPE_DISPLAY_PAGE_GROUP	(rb_display_page_group_get_type ())
#define RB_DISPLAY_PAGE_GROUP(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DISPLAY_PAGE_GROUP, RBDisplayPageGroup))
#define RB_IS_DISPLAY_PAGE_GROUP(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DISPLAY_PAGE_GROUP))
#define RB_DISPLAY_PAGE_GROUP_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DISPLAY_PAGE_GROUP, RBDisplayPageGroupClass))
#define RB_IS_DISPLAY_PAGE_GROUP_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DISPLAY_PAGE_GROUP))
#define RB_DISPLAY_PAGE_GROUP_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DISPLAY_PAGE_GROUP, RBDisplayPageGroupClass))

#define RB_DISPLAY_PAGE_GROUP_LIBRARY           (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("library")))
#define RB_DISPLAY_PAGE_GROUP_PLAYLISTS         (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("playlists")))
#define RB_DISPLAY_PAGE_GROUP_DEVICES           (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("devices")))
#define RB_DISPLAY_PAGE_GROUP_SHARED            (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("shared")))
#define RB_DISPLAY_PAGE_GROUP_STORES            (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("stores")))
#define RB_DISPLAY_PAGE_GROUP_TOOLS             (RB_DISPLAY_PAGE (rb_display_page_group_get_by_id ("tools")))

void                rb_display_page_group_add_core_groups       (GObject *shell,
								 RBDisplayPageModel *page_model);

RBDisplayPageGroup *rb_display_page_group_get_by_id        	(const char *id);
RBDisplayPageGroup *rb_display_page_group_new              	(GObject *shell,
								 const char *id,
								 const char *name,
								 RBDisplayPageGroupCategory category);

void                rb_display_page_group_loaded		(RBDisplayPageGroup *group);

G_END_DECLS

#endif /* __RB_DISPLAY_PAGE_GROUP_H */
