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

#ifndef __RB_TEST_VIEW_H
#define __RB_TEST_VIEW_H

#include "rb-view.h"
#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_TEST_VIEW         (rb_test_view_get_type ())
#define RB_TEST_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TEST_VIEW, RBTestView))
#define RB_TEST_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TEST_VIEW, RBTestViewClass))
#define RB_IS_TEST_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TEST_VIEW))
#define RB_IS_TEST_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TEST_VIEW))
#define RB_TEST_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TEST_VIEW, RBTestViewClass))

typedef struct RBTestViewPrivate RBTestViewPrivate;

typedef struct
{
	RBView parent;

	RBTestViewPrivate *priv;
} RBTestView;

typedef struct
{
	RBViewClass parent;
} RBTestViewClass;

GType   rb_test_view_get_type (void);

RBView *rb_test_view_new      (BonoboUIComponent *component,
			       Library *library);

G_END_DECLS

#endif /* __RB_TEST_VIEW_H */
