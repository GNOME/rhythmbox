/*
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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

#ifndef __RB_IRADIO_YP_ITERATOR_H
#define __RB_IRADIO_YP_ITERATOR_H

#include "rb-iradio-station.h"

G_BEGIN_DECLS

#define RB_TYPE_IRADIO_YP_ITERATOR         (rb_iradio_yp_iterator_get_type ())
#define RB_IRADIO_YP_ITERATOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IRADIO_YP_ITERATOR, RBIRadioYPIterator))
#define RB_IS_IRADIO_YP_ITERATOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IRADIO_YP_ITERATOR))
#define RB_IRADIO_YP_ITERATOR_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_IRADIO_YP_ITERATOR, RBIRadioYPIteratorIface))

typedef struct RBIRadioYPIterator RBIRadioYPIterator;

typedef struct
{
	GTypeInterface g_iface;
	
	/* methods */
	RBIRadioStation*	(*impl_get_next_station)	();
} RBIRadioYPIteratorIface;

GType                   rb_iradio_yp_iterator_get_type         (void);

RBIRadioStation *	rb_iradio_yp_iterator_get_next_station (RBIRadioYPIterator *it);

G_END_DECLS

#endif /* __RB_IRADIO_YP_ITERATOR_H */
