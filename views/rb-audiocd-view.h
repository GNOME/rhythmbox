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

#ifndef __RB_AUDIOCD_VIEW_H
#define __RB_AUDIOCD_VIEW_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-view.h"
#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_AUDIOCD_VIEW         (rb_audiocd_view_get_type ())
#define RB_AUDIOCD_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOCD_VIEW, RBAudiocdView))
#define RB_AUDIOCD_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOCD_VIEW, RBAudiocdViewClass))
#define RB_IS_AUDIOCD_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOCD_VIEW))
#define RB_IS_AUDIOCD_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOCD_VIEW))
#define RB_AUDIOCD_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOCD_VIEW, RBAudiocdViewClass))

typedef struct RBAudiocdViewPrivate RBAudiocdViewPrivate;

typedef struct
{
	RBView parent;

	RBAudiocdViewPrivate *priv;
} RBAudiocdView;

typedef struct
{
	RBViewClass parent;
} RBAudiocdViewClass;

GType       rb_audiocd_view_get_type           (void);

RBView     *rb_audiocd_view_new                (BonoboUIContainer *container);

void        rb_audiocd_view_set_name           (RBAudiocdView *audiocd,
                                                const char *name);

const char *rb_audiocd_view_get_file           (RBAudiocdView *audiocd);

void        rb_audiocd_view_remove_file        (RBAudiocdView *audiocd);

void        rb_audiocd_view_save               (RBAudiocdView *view);
void        rb_audiocd_view_load               (RBAudiocdView *view);

void 	    rb_audiocd_view_add_node           (RBAudiocdView *view,
                                                RBNode *node);

void        rb_audiocd_refresh_cd              (RBAudiocdView *view);
gboolean    rb_audiocd_is_cd_available         (RBAudiocdView *view);
gboolean    rb_audiocd_is_any_device_available ();

G_END_DECLS

#endif /* __RB_AUDIOCD_VIEW_H */
