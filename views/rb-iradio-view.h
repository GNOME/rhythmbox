/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002 Colin Walters <walters@debian.org>
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

#ifndef __RB_IRADIO_VIEW_H
#define __RB_IRADIO_VIEW_H

#include <bonobo/bonobo-ui-container.h>

#include "rb-view.h"
#include "rb-iradio-backend.h"

G_BEGIN_DECLS

#define RB_TYPE_IRADIO_VIEW         (rb_iradio_view_get_type ())
#define RB_IRADIO_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IRADIO_VIEW, RBIRadioView))
#define RB_IRADIO_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IRADIO_VIEW, RBIRadioViewClass))
#define RB_IS_IRADIO_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IRADIO_VIEW))
#define RB_IS_IRADIO_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IRADIO_VIEW))
#define RB_IRADIO_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IRADIO_VIEW, RBIRadioViewClass))

typedef struct RBIRadioViewPrivate RBIRadioViewPrivate;

typedef struct
{
	RBView parent;

	RBIRadioViewPrivate *priv;
} RBIRadioView;

typedef struct
{
	RBViewClass parent;
} RBIRadioViewClass;

GType   rb_iradio_view_get_type (void);

RBView *rb_iradio_view_new      (BonoboUIContainer *container,
				 RBIRadioBackend *backend);

G_END_DECLS

#endif /* __RB_IRADIO_VIEW_H */
