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

#include "rb-iradio-yp-iterator.h"

static void rb_iradio_yp_iterator_base_init (gpointer g_class);

GType
rb_iradio_yp_iterator_get_type (void)
{
	static GType rb_iradio_yp_iterator_type = 0;

	if (rb_iradio_yp_iterator_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioYPIteratorIface),
			rb_iradio_yp_iterator_base_init,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		rb_iradio_yp_iterator_type = g_type_register_static (G_TYPE_INTERFACE,
								     "RBIRadioYPIterator",
								     &our_info, 0);
		g_type_interface_add_prerequisite (rb_iradio_yp_iterator_type, G_TYPE_OBJECT);
	}

	return rb_iradio_yp_iterator_type;
}

static void
rb_iradio_yp_iterator_base_init (gpointer g_iface)
{
	static gboolean initialized = FALSE;

	if (initialized == TRUE)
		return;

	initialized = TRUE;
}

RBIRadioStation *
rb_iradio_yp_iterator_get_next_station (RBIRadioYPIterator *it)
{
	RBIRadioYPIteratorIface *iface = RB_IRADIO_YP_ITERATOR_GET_IFACE (it);
	return iface->impl_get_next_station (it);
}
