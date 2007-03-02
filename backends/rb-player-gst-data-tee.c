/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
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
 */

#include <config.h>

#include "rb-player-gst-data-tee.h"
#include "rb-marshal.h"

static void
rb_player_gst_data_tee_interface_init (RBPlayerGstDataTeeIface *iface)
{

}

GType
rb_player_gst_data_tee_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RBPlayerGstDataTeeIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc)rb_player_gst_data_tee_interface_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RBPlayerGstDataTee", &our_info, 0);
	}

	return our_type;
}

gboolean
rb_player_gst_data_tee_add_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{
	RBPlayerGstDataTeeIface *iface = RB_PLAYER_GST_DATA_TEE_GET_IFACE (player);

	return iface->add_data_tee (player, element);
}

gboolean
rb_player_gst_data_tee_remove_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{
	RBPlayerGstDataTeeIface *iface = RB_PLAYER_GST_DATA_TEE_GET_IFACE (player);

	return iface->remove_data_tee (player, element);
}

