/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include "rb-player-gst-data-tee.h"
#include "rb-marshal.h"


enum {
	DATA_TEE_INSERTED,
	DATA_TEE_PRE_REMOVE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * SECTION:rb-player-gst-data-tee
 * @short_description: player interface for processing raw data
 * @include: rb-player-gst-data-tee.h
 *
 * This interface allows a caller to add a branch to the GStreamer playback
 * pipeline that receives a copy of the raw data from the playback source
 * element.
 *
 * This interface is not currently implemented by either playback backend.
 */

static void
rb_player_gst_data_tee_interface_init (RBPlayerGstDataTeeIface *iface)
{
	/**
	 * RBPlayerGstDataTee::data-tee-inserted:
	 * @player: the #RBPlayerGstDataTee implementation
	 * @data_tee: the element which has been inserted
	 *
	 * The 'data_tee-inserted' signal is emitted when the tee element has been
	 * inserted into the pipeline and fully linked
	 **/
	signals[DATA_TEE_INSERTED] =
		g_signal_new ("data-tee-inserted",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerGstDataTeeIface, data_tee_inserted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBPlayerGstDataTee::data-tee-pre-remove:
	 * @player: the #RBPlayerGstDataTee implementation
	 * @data_tee: the element which is about to be removed
	 *
	 * The 'data_tee-pre-remove' signal is emitted immediately before the element
	 * is unlinked and removed from the pipeline
	 **/
	signals[DATA_TEE_PRE_REMOVE] =
		g_signal_new ("data-tee-pre-remove",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerGstDataTeeIface, data_tee_pre_remove),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);
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

/**
 * rb_player_gst_data_tee_add_data_tee:
 * @player: a #RBPlayerGstDataTee implementation
 * @element: data tee branch to add
 *
 * Adds a raw data tee branch to the playback pipeline.
 *
 * Return value: TRUE if the tee branch was added successfully
 */
gboolean
rb_player_gst_data_tee_add_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{
	RBPlayerGstDataTeeIface *iface = RB_PLAYER_GST_DATA_TEE_GET_IFACE (player);

	return iface->add_data_tee (player, element);
}

/**
 * rb_player_gst_data_tee_remove_data_tee:
 * @player: a #RBPlayerGstDataTee implementation
 * @element: data tee branch to add
 *
 * Removes a raw data tee branch.
 * 
 * Return value: TRUE if the tee branch was found and removed
 */
gboolean
rb_player_gst_data_tee_remove_data_tee (RBPlayerGstDataTee *player, GstElement *element)
{
	RBPlayerGstDataTeeIface *iface = RB_PLAYER_GST_DATA_TEE_GET_IFACE (player);

	return iface->remove_data_tee (player, element);
}

void
_rb_player_gst_data_tee_emit_data_tee_inserted (RBPlayerGstDataTee *player, GstElement *data_tee)
{
	g_signal_emit (player, signals[DATA_TEE_INSERTED], 0, data_tee);
}

void
_rb_player_gst_data_tee_emit_data_tee_pre_remove (RBPlayerGstDataTee *player, GstElement *data_tee)
{
	g_signal_emit (player, signals[DATA_TEE_PRE_REMOVE], 0, data_tee);
}

